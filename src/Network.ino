/**
 * @file Network.ino
 * @brief WiFi connectivity + model upload for TinyMaker firmware v1.0.2
 *
 * Adds:
 *  - WiFiManager captive portal ("TinyMaker-Setup" AP on first boot)
 *  - mDNS: http://tinymaker.local
 *  - Minimal OctoPrint API emulation so PrusaSlicer "Send to printer" works:
 *      GET  /api/version      -> connection test
 *      POST /api/files/local  -> multipart file upload (streamed to SD)
 *  - POST /upload             -> same handler, for curl / UVtools testing
 *  - Universal ZIP/SL1 unpacker: extracts *.png layers to /<ModelName>/1.png..N.png
 *    (handles both UVtools "1.png.." and PrusaSlicer "name00000.png.." numbering)
 *
 * INTEGRATION (two edits in TinyMaker_Firmware_v1_0_2.ino):
 *  1. setup(): insert  network_setup();  just BEFORE the final  screen1();
 *  2. loop():  insert  server.handleClient();  as the FIRST line of loop()
 *
 * SAFETY: server.handleClient() is only ever called from loop(). The print
 * process lives entirely inside loop()'s case 111 block and never returns
 * until finished, so uploads physically cannot touch the shared SPI bus
 * (SD + both LCDs) while printing. No mutexes needed by design.
 */

// Compile-time switch: define ENABLE_NETWORK 0 in the main .ino to build
// the firmware without any network code (original behavior, ~50KB+ RAM freed).
#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK


#include <WiFi.h>
#include <WiFiManager.h>   // tzapu, v2.0.x
#include <ESPmDNS.h>
#include <WebServer.h>
#include <unzipLIB.h>      // bitbank2

WebServer server(80);
// NOTE: no global 'UNZIP zip;' here! The UNZIP object is ~40 KB - declared
// globally it lands in static .bss and overflows WROOM's DRAM segment
// (verified: "region dram0_0_seg overflowed"). It is heap-allocated inside
// unpackModel() only for the duration of unpacking.

// Upload state
File uploadFile;
String uploadPath;      // e.g. "/Benchy.sl1"
String modelName;       // e.g. "Benchy"
bool uploadOk = false;

// Separate File handle for the unzipper - do NOT reuse the global
// 'myfile' from PNG.ino (it belongs to the PNGdec callbacks).
File zipSrcFile;

// ===================================================================================
// unzipLIB <-> SdFat callbacks
// NOTE: verify exact callback/getFileInfo signatures against the unzipLIB
// example sketches on first build - the library API is small but strict.
// ===================================================================================
void *zipOpen(const char *filename, int32_t *size) {
  zipSrcFile = SD.open(filename);
  if (!zipSrcFile) return NULL;
  *size = zipSrcFile.size();
  return (void *)&zipSrcFile;
}

void zipClose(void *p) {
  ZIPFILE *pzf = (ZIPFILE *)p;
  File *f = (File *)pzf->fHandle;
  if (f) f->close();
}

int32_t zipRead(void *p, uint8_t *buffer, int32_t length) {
  ZIPFILE *pzf = (ZIPFILE *)p;
  File *f = (File *)pzf->fHandle;
  return f->read(buffer, length);
}

int32_t zipSeek(void *p, int32_t position, int iType) {
  ZIPFILE *pzf = (ZIPFILE *)p;
  File *f = (File *)pzf->fHandle;
  if (iType == SEEK_SET)      f->seek(position);
  else if (iType == SEEK_CUR) f->seek(f->position() + position);
  else                        f->seek(f->size() + position); // SEEK_END
  return f->position();
}

// ===================================================================================
// Helpers
// ===================================================================================

// Small status line on the UI LCD (gfx2)
void netMessage(const char *line1, const char *line2) {
  gfx2->fillScreen(BLACK);
  gfx2->fillRoundRect(0, 0, 160, 80, 5, ORANGE);
  gfx2->fillRoundRect(2, 2, 156, 76, 3, BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(8, 32);
  gfx2->print(line1);
  gfx2->setCursor(8, 56);
  gfx2->print(line2);
}

// Extract layer number from a zip entry name, or -1 if not a layer PNG.
// Accepts "17.png", "Benchy00016.png", "slice/12.png". Rejects thumbnails
// and non-png entries. Number = trailing digits right before ".png".
int layerIndexFromEntry(const char *entryName) {
  String n = String(entryName);
  n.toLowerCase();
  if (n.indexOf("thumbnail") >= 0) return -1;
  if (!n.endsWith(".png")) return -1;
  int slash = n.lastIndexOf('/');
  String base = n.substring(slash + 1, n.length() - 4); // strip dir + ".png"
  int i = base.length() - 1;
  if (i < 0 || !isDigit(base[i])) return -1;
  while (i > 0 && isDigit(base[i - 1])) i--;
  return base.substring(i).toInt();
}

// Make a safe SD folder name from the uploaded filename (no extension).
// Folder name buffer in the stock firmware is 101 chars; keep well under.
String safeModelName(String fn) {
  int slash = max(fn.lastIndexOf('/'), fn.lastIndexOf('\\'));
  if (slash >= 0) fn = fn.substring(slash + 1);
  int dot = fn.lastIndexOf('.');
  if (dot > 0) fn = fn.substring(0, dot);
  String out = "";
  for (unsigned int i = 0; i < fn.length() && out.length() < 40; i++) {
    char c = fn[i];
    if (isAlphaNumeric(c) || c == '-' || c == '_') out += c;
  }
  if (out.length() == 0) out = "Model";
  return out;
}

// ===================================================================================
// ZIP / SL1 unpacker
// Pass 1: find lowest layer number and count. Pass 2: extract each *.png
// as /<dest>/<n - min + 1>.png so the stock firmware (which probes 1.png,
// 2.png, ... with no gaps) sees a valid model regardless of source format.
// ===================================================================================
bool unpackModel(const char *zipPath, const char *destDir) {
  char entry[256];
  const int BUFSZ = 4096;

  // UNZIP object is ~40 KB -> allocate on heap only while unpacking.
  // Never make it global/static (overflows WROOM DRAM at link time).
  UNZIP *zip = new UNZIP();
  uint8_t *buf = (uint8_t *)malloc(BUFSZ);
  if (!zip || !buf) {
    if (zip) delete zip;
    if (buf) free(buf);
    return false;
  }

  // ---- Pass 1: scan - find lowest layer number and count
  int minN = 0x7FFFFFFF, total = 0;
  if (zip->openZIP(zipPath, zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) {
    delete zip; free(buf);
    return false;
  }
  zip->gotoFirstFile();
  do {
    zip->getFileInfo(NULL, entry, sizeof(entry), NULL, 0, NULL, 0);
    int n = layerIndexFromEntry(entry);
    if (n >= 0) { total++; if (n < minN) minN = n; }
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();
  if (total == 0) { delete zip; free(buf); return false; }
  DBG("Unpack: %d layers (min index %d)\n", total, minN);

  // ---- Prepare destination
  SD.mkdir(destDir);

  // Remove stale layers from a previous upload with the same model name.
  // Leftover files above the new layer count would inflate the count seen
  // by the firmware's contiguous-file probing (mixed/oversized model!).
  for (int i = 1; ; i++) {
    String p = String(destDir) + "/" + String(i) + ".png";
    if (!SD.remove(p.c_str())) break;
  }

  // ---- Pass 2: extract each *.png as <n - minN + 1>.png
  bool ok = true;
  int done = 0;
  if (zip->openZIP(zipPath, zipOpen, zipClose, zipRead, zipSeek) != UNZ_OK) {
    delete zip; free(buf);
    return false;
  }
  zip->gotoFirstFile();
  do {
    zip->getFileInfo(NULL, entry, sizeof(entry), NULL, 0, NULL, 0);
    int n = layerIndexFromEntry(entry);
    if (n < 0) continue;

    String outPath = String(destDir) + "/" + String(n - minN + 1) + ".png";
    SD.remove(outPath.c_str());
    File out = SD.open(outPath.c_str(), FILE_WRITE);
    if (!out) { ok = false; break; }

    if (zip->openCurrentFile() != UNZ_OK) { out.close(); ok = false; break; }
    int rc;
    while ((rc = zip->readCurrentFile(buf, BUFSZ)) > 0)
      out.write(buf, rc);
    zip->closeCurrentFile();
    out.close();
    if (rc < 0) { ok = false; break; }

    done++;
    if (done % 20 == 0 || done == total) {
      String p = String(done) + " / " + String(total);
      netMessage("Unpacking layers", p.c_str());
    }
  } while (zip->gotoNextFile() == UNZ_OK);
  zip->closeZIP();
  delete zip;
  free(buf);

  // On failure remove the partial folder - a model with missing layers
  // would otherwise print incomplete (firmware probes until first gap).
  if (!ok) {
    for (int i = 1; i <= total; i++) {
      String p = String(destDir) + "/" + String(i) + ".png";
      SD.remove(p.c_str());
    }
    SD.rmdir(destDir);
  }
  return ok;
}

// ===================================================================================
// HTTP upload handlers
// ===================================================================================

// Streaming part - called repeatedly with chunks of the multipart body
void handleUploadData() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    modelName = safeModelName(up.filename);
    uploadPath = "/" + modelName + ".zip";
    uploadOk = false;
    SD.remove(uploadPath.c_str());
    uploadFile = SD.open(uploadPath.c_str(), FILE_WRITE);
    DBG("Upload start: %s\n", uploadPath.c_str());
    netMessage("Receiving model:", modelName.c_str());
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) { uploadFile.close(); uploadOk = true; }
    DBG("Upload done: %u bytes\n", up.totalSize);
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    SD.remove(uploadPath.c_str());
  }
}

// Final part - runs once after the whole request body is received
void finishUpload() {
  bool ok = false;
  if (uploadOk) {
    String dest = "/" + modelName;
    ok = unpackModel(uploadPath.c_str(), dest.c_str());
    SD.remove(uploadPath.c_str()); // free SD space, keep browser list clean
  }
  if (ok) {
    server.send(201, "application/json", "{\"done\":true}");
    netMessage("Model ready:", modelName.c_str());
    delay(1500);
  } else {
    DBGLN("Unpack FAILED");
    server.send(500, "application/json", "{\"error\":\"unpack failed\"}");
    netMessage("Upload FAILED", modelName.c_str());
    delay(1500);
  }
  // Redraw UI - upload messages overwrote whatever screen was shown.
  // Returning to the main menu keeps the 'screen' state machine consistent;
  // the new model appears in Print menu (folder list is re-read on entry).
  screen1();
}

// ===================================================================================
// Setup - call from setup() just BEFORE the final screen1()
// ===================================================================================

// --- Boot-time progress UI: two text lines + bounded progress bar ---
// The bar fills toward the timeout, so the user can see how long is left.
void netProgressStart(const char *line1, const char *line2) {
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 18);
  gfx2->print(line1);
  gfx2->setCursor(5, 38);
  gfx2->print(line2);
  gfx2->drawRoundRect(10, 48, 140, 16, 3, WHITE);
}

void netProgressBar(int step, int total) {
  int w = (int)(136L * step / total);
  if (w > 136) w = 136;
  if (w > 0) gfx2->fillRect(12, 50, w, 12, ORANGE);
}

// --- WiFi status badge on the main menu (top-right corner, above the icons):
// green dot = connected, grey dot = offline. Called from screen1/2/3 and
// refreshed periodically from network_loop().
void drawWifiBadge() {
  uint16_t c = (WiFi.status() == WL_CONNECTED) ? GREEN : DARKGREY;
  gfx2->fillCircle(154, 4, 3, c);
}

void network_setup() {
  WiFiManager wm;
  bool saved = wm.getWiFiIsSaved();

  if (saved) {
    // Credentials stored in NVS: try to connect ourselves with a visible
    // 15 s progress bar. NO config portal on failure - the printer may
    // simply be away from its home network; boot into offline mode instead.
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    const int steps = 30; // 30 x 500 ms = 15 s
    netProgressStart("WiFi connecting...", "");
    for (int i = 1; i <= steps && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      netProgressBar(i, steps);
    }
  } else {
    // No credentials yet (first boot / after Reset WiFi): captive portal
    // in NON-blocking mode so we can draw the bar filling toward the
    // 120 s timeout while wm.process() serves the portal.
    wm.setConfigPortalBlocking(false);
    wm.startConfigPortal("TinyMaker-Setup");
    netProgressStart("WiFi setup - join AP:", "TinyMaker-Setup");
    const int totalSec = 120;
    unsigned long t0 = millis();
    int lastSec = -1;
    while (WiFi.status() != WL_CONNECTED) {
      wm.process();
      int sec = (int)((millis() - t0) / 1000);
      if (sec != lastSec) {
        lastSec = sec;
        netProgressBar(sec, totalSec);
      }
      if (sec >= totalSec) break;
      delay(10);
    }
    wm.stopConfigPortal();
  }

  if (WiFi.status() != WL_CONNECTED) {
    netMessage("WiFi: offline mode", "");
    delay(1200);
    return;
  }

  MDNS.begin("tinymaker"); // http://tinymaker.local

  // OctoPrint-style API for PrusaSlicer "Send to printer".
  // NOTE: SL1-derived profiles make PrusaSlicer use its SL1Host client,
  // which REQUIRES the version "text" to start with "Prusa SLA" -
  // otherwise Test fails with "Could not connect to Prusa SLA".
  server.on("/api/version", HTTP_GET, []() {
    server.send(200, "application/json",
      "{\"api\":\"0.1\",\"server\":\"1.5.0\",\"text\":\"Prusa SLA (TinyMaker)\"}");
  });
  server.on("/api/files/local", HTTP_POST, finishUpload, handleUploadData);

  // Plain endpoint for curl / UVtools testing:
  //   curl -F "file=@model.zip" http://tinymaker.local/upload
  server.on("/upload", HTTP_POST, finishUpload, handleUploadData);

  server.begin();

  String ip = "IP: " + WiFi.localIP().toString();
  netMessage("WiFi connected", ip.c_str());
  delay(1500);
}

// ===================================================================================
// Loop hook - call as the FIRST line of loop()
// Wrapper so the main .ino never touches the 'server' global directly:
// globals defined in later .ino tabs are NOT visible earlier in the
// combined compilation unit (functions are - prototypes are auto-generated).
// ===================================================================================
void network_loop() {
  server.handleClient();

  // Live refresh of the WiFi info screen (312): redraw values every 2 s
  // while the screen is open. 'screen' global is defined in the main .ino
  // (earlier in the combined compilation unit), so it is visible here.
  static unsigned long wifiInfoTs = 0;
  if (screen == 312 && millis() - wifiInfoTs > 2000) {
    wifiInfoTs = millis();
    wifiInfoValues();
  }

  // Refresh the main-menu WiFi badge every 5 s (connection may drop/return
  // while the printer sits in the menu)
  static unsigned long badgeTs = 0;
  if ((screen == 1 || screen == 2 || screen == 3 || screen == 4) && millis() - badgeTs > 5000) {
    badgeTs = millis();
    drawWifiBadge();
  }
}

// ===================================================================================
// WiFi Info screen (312) - opened from Settings list item 12
// and Reset WiFi confirmation (3121)
// ===================================================================================

// Redrawable middle part: signal + IP (called on open and every 2 s)
void wifiInfoValues() {
  gfx2->fillRect(0, 20, 160, 40, BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 35);
  if (WiFi.status() == WL_CONNECTED) {
    long r = WiFi.RSSI();
    gfx2->print("Sig: ");
    gfx2->print(r);
    gfx2->print(" dBm ");
    gfx2->print(r > -60 ? "(Good)" : (r > -75 ? "(OK)" : "(Weak)"));
    gfx2->setCursor(5, 55);
    gfx2->print("IP: ");
    gfx2->print(WiFi.localIP());
  } else {
    gfx2->print("Not connected");
  }
}

// Full screen draw; sets screen = 312.
// Buttons on 312 (handled in main loop switches):
//   Back -> back to Settings list (item 12), OK -> Reset WiFi confirmation
void screenWifiInfo() {
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 15);
  if (WiFi.status() == WL_CONNECTED) {
    gfx2->print("WiFi: ");
    gfx2->print(WiFi.SSID());
  } else {
    gfx2->print("WiFi: Offline");
  }
  wifiInfoValues();
  gfx2->setCursor(5, 74);
  gfx2->print("FW ");
#ifdef FIRMWARE_VERSION
  gfx2->print(FIRMWARE_VERSION);
#else
  gfx2->print("?");
#endif
  screen = 312;
}

// Confirmation screen; sets screen = 3121.
// Buttons on 3121: OK -> wifiDoReset(), Back -> screenWifiInfo()
void screenWifiResetConfirm() {
  gfx2->fillScreen(BLACK);
  gfx2->setFont(&FreeSans8pt7b);
  gfx2->setTextColor(WHITE);
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 25);
  gfx2->print("Reset WiFi settings?");
  gfx2->setCursor(5, 55);
  gfx2->print("OK=Yes  Back=No");
  screen = 3121;
}

// Erase stored credentials and reboot -> captive portal on next boot
void wifiDoReset() {
  netMessage("WiFi reset...", "Restarting");
  WiFiManager wm;
  wm.resetSettings();
  delay(500);
  ESP.restart();
}

#endif // ENABLE_NETWORK
