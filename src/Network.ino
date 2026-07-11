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
#include <Update.h>        // web /update firmware flashing
#include <ArduinoOTA.h>    // PlatformIO espota uploads
#include <WiFiClientSecure.h> // HTTPS to GitHub for version check + self-update
#include <HTTPClient.h>       // fetch version.txt
#include <HTTPUpdate.h>       // pull-and-flash firmware.bin (self-update)
#include <esp_wifi.h>      // esp_wifi_restore() for reliable credential erase
#include <Preferences.h>   // forcePortal flag (survives reboot)
#include <PubSubClient.h>

bool networkRuntimeEnabled() {
  return wifiEnabled || wifiTemporarilyEnabled;
}

bool webDashboardRuntimeEnabled() {
  return webDashboardEnabled || webDashboardTemporarilyEnabled;
}

// Web control gate (since 0.12.0 the dashboard is view-only when it is off):
// state-changing browser/API actions - print control, SD delete, config,
// VAT refill, firmware - return 403; viewing, status polling and the
// PrusaSlicer/UVtools model upload keep working. WiFi off still kills all.
bool rejectIfWebControlOff() {
  if (webDashboardRuntimeEnabled()) return false;
  sendApiError(403, "web control is off - enable it on the printer (System > Advanced)");
  return true;
}

// Where the printer checks for a newer firmware (self-update, "Install latest").
// version.txt must contain two lines: (1) the latest version, e.g. "0.7.0",
// (2) the direct HTTPS URL of that firmware.bin. Both hosted on GitHub Pages.
#define OTA_VERSION_URL "https://slibbinas.github.io/TinyMakerWifi/version.txt"

WebServer server(80);
Preferences netPrefs;
WiFiClient mqttNet;
PubSubClient mqttClient(mqttNet);
// NOTE: no global 'UNZIP zip;' here! The UNZIP object is ~40 KB - declared
// globally it lands in static .bss and overflows WROOM's DRAM segment
// (verified: "region dram0_0_seg overflowed"). It is heap-allocated inside
// unpackModel() only for the duration of unpacking.

// Upload state
File uploadFile;
String uploadPath;      // e.g. "/Benchy.sl1"
String modelName;       // e.g. "Benchy"
bool uploadOk = false;
bool uploadRejected = false;
unsigned long otaShownBytes = 0;   // progress counter (upload + web OTA)
unsigned long mqttLastAttemptMs = 0;
unsigned long mqttLastPublishMs = 0;
bool mqttDiscoverySent = false;

extern unsigned long whitePixelsAccum;
extern bool countPixelsMode;
extern bool estimateCancelReq;
void *myOpen(const char *filename, int32_t *size);
void myClose(void *handle);
int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length);
int32_t mySeek(PNGFILE *handle, int32_t position);
void PNGDraw(PNGDRAW *pDraw);

// The ZIP/SL1 conversion pipeline (zip callbacks, layerIndexFromEntry,
// safeModelName, unpackModel) lives in Import.ino - outside the network
// guard - so the on-device "import from SD" flow works with
// ENABLE_NETWORK=0. The upload handlers below reuse it.

// ===================================================================================
// HTTP upload handlers
// ===================================================================================

bool sdCardReady() {
  File cardRoot = SD.open("/");
  if (cardRoot) {
    cardRoot.close();
    return true;
  }

  if (!SD.begin(SDCS, SD_SCK_MHZ(16))) return false;

  cardRoot = SD.open("/");
  if (!cardRoot) return false;
  cardRoot.close();
  return true;
}

String uint64Json(uint64_t value) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
  return String(buf);
}

bool sdCardUsage(uint64_t &totalBytes, uint64_t &freeBytes) {
  totalBytes = 0;
  freeBytes = 0;
  if (!sdCardReady()) return false;

  FatVolume *vol = SD.vol();
  if (!vol) return false;

  int32_t freeClusters = vol->freeClusterCount();
  if (freeClusters < 0) return false;

  uint64_t bytesPerCluster = (uint64_t)vol->blocksPerCluster() * 512ULL;
  totalBytes = (uint64_t)vol->clusterCount() * bytesPerCluster;
  freeBytes = (uint64_t)freeClusters * bytesPerCluster;
  return totalBytes > 0;
}

// Streaming part - called repeatedly with chunks of the multipart body
void handleUploadData() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    modelName = safeModelName(up.filename);
    uploadPath = "/" + modelName + ".zip";
    uploadOk = false;
    uploadRejected = false;
    otaShownBytes = 0;

    if (printerBusy()) {
      uploadRejected = true;
      DBGLN("Upload rejected: printer busy");
      return;
    }

    if (!sdCardReady()) {
      uploadRejected = true;
      DBGLN("Upload rejected: SD card unavailable");
      netMessage("Upload blocked", "No SD card");
      return;
    }

    SD.remove(uploadPath.c_str());
    uploadFile = SD.open(uploadPath.c_str(), FILE_WRITE);
    if (!uploadFile) {
      uploadRejected = true;
      DBGLN("Upload rejected: cannot open SD file");
      netMessage("Upload blocked", "SD write failed");
      return;
    }

    DBG("Upload start: %s\n", uploadPath.c_str());
    // Same style as WiFi/delete/OTA: title + a sweeping progress bar.
    // Multipart uploads don't announce total size, so the bar animates by
    // wrapping every ~1 MB while showing the running KB count.
    netProgressStart("Receiving model:", modelName.c_str());
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadRejected) return;
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
    if (up.totalSize - otaShownBytes >= 65536) { // redraw every 64 KB
      otaShownBytes = up.totalSize;
      int w = (int)((up.totalSize % 1048576L) * 136L / 1048576L); // wraps each 1 MB
      gfx2->fillRect(12, 50, 136, 12, BLACK);
      gfx2->fillRect(12, 50, w, 12, ORANGE);
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (uploadRejected) return;
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
  if (uploadRejected) {
    uploadRejected = false;
    uploadOk = false;
    if (printerBusy()) {
      server.send(409, "application/json", "{\"error\":\"printer busy\"}");
      return;
    } else {
      server.send(503, "application/json", "{\"error\":\"sd card unavailable\"}");
      netMessage("Upload blocked", "No SD card");
    }
    delay(1500);
    screen1();
    return;
  }

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

// ===================================================================================
// Firmware update over the web: GET /update (page) + POST /update (flash)
// Users download firmware.bin from GitHub Releases and upload it here.
// Runs only from loop() -> physically impossible during printing.
// ===================================================================================
bool otaWebOk = false;
bool otaBlocked = false;   // set when a flash is attempted outside the Update menu

// Security: firmware flashing is only accepted while the printer is showing
// the System -> Update screen (421) or its "Install from file" subscreen
// (422) - the latter is precisely the screen that tells the user to open the
// browser, so it must keep the gate open. Model upload from PrusaSlicer
// (/api/files/local) is intentionally NOT gated - only firmware flashing is.
static bool otaMenuOpen() { return screen == 421 || screen == 422; }

// Web flashing gate. Since 0.11.0 the browser paths (upload, Install latest,
// version picker) also work whenever the printer is idle and Web control is
// on - the strict Update-screen rule remains only for the developer espota
// path (see network_loop). Flashing while printing stays impossible.
static bool otaWebAllowed() {
  return otaMenuOpen() || (!printerBusy() && webDashboardRuntimeEnabled());
}

// Shared page chrome (head + styled card) for all firmware-update responses.
// Pass the inner card HTML; returns the full document.
String otaStyledPage(const String &inner) {
  return String(
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TinyMaker firmware update</title>"
    // Inline data-URI favicon (orange rounded square with a white T) -
    // shows in the browser tab and bookmarks, nothing stored on the device
    "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<rect width='16' height='16' rx='3' fill='%23e8720c'/>"
    "<text x='8' y='12.5' font-family='Arial' font-size='11' font-weight='bold' fill='white' text-anchor='middle'>T</text></svg>\">"
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
    "background:#1c1c1e;font-family:-apple-system,Segoe UI,Roboto,sans-serif;color:#eee}"
    ".card{background:#2a2a2e;border:2px solid #e8720c;border-radius:14px;padding:28px 26px;"
    "max-width:360px;width:90%;box-shadow:0 8px 30px rgba(0,0,0,.4);text-align:center}"
    "h1{margin:0 0 4px;font-size:20px;color:#e8720c}"
    ".sub{font-size:14px;color:#eee;margin-bottom:6px}.ver{font-size:13px;color:#84bcf8;margin-bottom:20px}"
    ".hint{font-size:13px;color:#aaa;margin:16px 0}"
    "a{color:#84bcf8;text-decoration:none}a:hover{text-decoration:underline}"
    "input[type=file]{width:100%;margin:10px 0;padding:10px;border:1px solid #555;"
    "border-radius:8px;background:#1c1c1e;color:#eee;font-size:13px}"
    "input[type=submit]{width:100%;padding:12px;margin-top:8px;border:0;border-radius:8px;"
    "background:#e8720c;color:#fff;font-size:15px;font-weight:600;cursor:pointer}"
    "input[type=submit]:hover{background:#ff8419}"
    ".ok{font-size:15px;color:#5fd08a}.err{font-size:15px;color:#ff6b5f}"
    ".warn{font-size:12px;color:#e0a030;margin-top:18px}"
    // Same tab row as the dashboard - Update is the active tab here
    ".tabs{display:flex;gap:8px;margin-bottom:16px}"
    ".tabs a,.tabs span{flex:1;padding:9px 0;border-radius:8px;background:#3c3c42;color:#eee;"
    "font-size:13px;font-weight:600;text-decoration:none;text-align:center}"
    ".tabs .active{background:#e8720c;color:#fff}"
    "</style></head><body><div class='card'>"
    "<div class='tabs'><a href='/'>Dashboard</a><a href='/#settings'>Settings</a>"
    "<span class='active'>Update</span></div>") + inner + "</div></body></html>";
}

void handleUpdatePage() {
  String inner =
    "<h1>TinyMaker</h1><div class='sub'>Firmware update page</div><div class='ver'>Current version: ";
#ifdef FIRMWARE_VERSION
  inner += FIRMWARE_VERSION;
#else
  inner += "unknown";
#endif
  inner += "</div>";

  // Refuse while printing (or when Web control is off and the printer is not
  // on the Update screen).
  if (!otaWebAllowed()) {
    inner +=
      "<div class='hint'>Firmware updates are blocked while the printer is "
      "printing (or when Web control is off).</div>"
      "<div class='hint'>Wait for the print to finish, then reload this page.</div>";
    server.send(200, "text/html", otaStyledPage(inner));
    return;
  }

  inner +=
    "<div class='hint'>Get <b>firmware.bin</b> from "
    "<a href='https://github.com/slibbinas/TinyMakerWifi/releases' target='_blank' rel='noopener'>GitHub Releases</a></div>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin' required>"
    "<input type='submit' value='Update firmware'></form>"
    "<div class='warn'>Do not power off during the update</div>";
  server.send(200, "text/html", otaStyledPage(inner));
}

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaWebOk = false;
    otaShownBytes = 0;
    // Latch the decision once, at the start: reject a flash requested while
    // the printer is busy (see handleUpdateFinish).
    otaBlocked = !otaWebAllowed();
    if (otaBlocked) {
      DBGLN("Web OTA rejected: not in Update menu");
      return;
    }
    DBG("Web OTA start: %s\n", up.filename.c_str());
    netMessage("Firmware update", "Receiving...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      DBGLN("Update.begin failed");
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaBlocked) return;
    Update.write(up.buf, up.currentSize);
    if (up.totalSize - otaShownBytes >= 131072) { // redraw every 128 KB
      otaShownBytes = up.totalSize;
      String p = String(up.totalSize / 1024) + " KB";
      netMessage("Firmware update", p.c_str());
    }
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (otaBlocked) return;
    if (Update.end(true)) otaWebOk = true;
    DBG("Web OTA end: %u bytes, ok=%d\n", up.totalSize, otaWebOk);
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (!otaBlocked) Update.abort();
  }
}

void handleUpdateFinish() {
  if (otaBlocked) {
    otaBlocked = false;
    server.send(403, "text/html", otaStyledPage(
      "<h1>TinyMaker</h1><div class='err'>Update blocked</div>"
      "<div class='hint'>Updates are not accepted while the printer is printing "
      "(or when Web control is off). Try again when it is idle.</div>"));
    return;
  }
  if (otaWebOk) {
    server.send(200, "text/html", otaStyledPage(
      "<h1>TinyMaker</h1><div class='ok'>Update OK</div>"
      "<div class='hint'>The printer is rebooting&hellip;</div>"));
    netMessage("Update OK", "Restarting...");
    delay(800);
    ESP.restart();
  } else {
    server.send(500, "text/html", otaStyledPage(
      "<h1>TinyMaker</h1><div class='err'>Update FAILED</div>"
      "<div class='hint'>Check the firmware.bin file and try again.</div>"));
    netMessage("Update FAILED", "");
    delay(1500);
    screen1();
  }
}

// ===================================================================================
// Root page: small device dashboard at http://tinymaker.local/
// ===================================================================================

String printerStateText() {
  if (screen == 1111 || screen == 1112 || screen == 11111 || screen == 11112 || screen == 11113) {
    String prefix = uvLedEnabled ? "" : "Testing - ";
    switch (current_state) {
      case 0: return prefix + "Homing";
      case 1: return uvLedEnabled ? "Curing" : "Testing";
      case 2: return prefix + "Lifting";
      case 3: return prefix + "Dropping";
      case 4: return prefix + "Canceling";
      case 5: return prefix + "Pausing";
      case 6: return prefix + "Paused";
      case 7: return prefix + "Resuming";
      case 8: return prefix + "Finished";
      case 10: return prefix + "Refill resin";
      default: return uvLedEnabled ? "Printing" : "Testing";
    }
  }
  return "Idle";
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "\\r");
  return s;
}

String formatDuration(uint32_t secs) {
  uint32_t hours = secs / 3600UL;
  uint8_t minutes = (secs % 3600UL) / 60UL;
  uint8_t seconds = secs % 60UL;
  String out;
  if (hours > 0) {
    out += String(hours);
    out += "h ";
  }
  out += String(minutes);
  out += "m";
  if (hours == 0) {
    out += " ";
    out += String(seconds);
    out += "s";
  }
  return out;
}

uint32_t currentRunSecs() {
  if (!printerBusy() || printStartMs == 0) return 0;
  return (millis() - printStartMs) / 1000UL;
}

uint32_t remainingPrintSecs() {
  if (!printerBusy() || layer_counter <= 0) return 0;
  long layersLeft = layer_counter - current_layer;
  if (layersLeft < 0) layersLeft = 0;

  uint32_t secs = 0;
  if (current_layer < Base_Layer) {
    secs += (Base_Layer - current_layer) * Base_Exposure;
  }
  secs += layersLeft * Regular_Exposure;
  if (layersLeft > 1) {
    secs += (uint32_t)((layersLeft - 1) * motor_updown_time);
  }
  return secs;
}

bool modelPngExists(const String &name, int index) {
  String path = "/" + name + "/" + String(index) + ".png";
  File entry = SD.open(path.c_str());
  bool exists = (bool)entry;
  if (entry) entry.close();
  return exists;
}

int countModelSourceLayers(const String &name) {
  int count = 0;
  while (modelPngExists(name, count + 100)) count += 100;
  while (modelPngExists(name, count + 1)) count++;
  return count;
}

bool modelStats(const String &name, int &printLayers, float &heightMm, uint32_t &timeSecs) {
  int sourceLayers = countModelSourceLayers(name);
  if (sourceLayers <= 0 || sourceLayers > MAX_LAYER_FILES) return false;

  get_motor_updown_time();
  printLayers = sourceLayers;
  if (Layer_Height < 0.09) {
    heightMm = sourceLayers * 0.05;
  }
  if (Layer_Height > 0.06) {
    printLayers = sourceLayers / 2;
    heightMm = 0.1 * printLayers;
  }

  long exposureLayers = printLayers - Base_Layer;
  if (exposureLayers < 0) exposureLayers = 0;
  long movementLayers = printLayers - 1;
  if (movementLayers < 0) movementLayers = 0;
  timeSecs = (Base_Layer * Base_Exposure) +
             (exposureLayers * Regular_Exposure) +
             (uint32_t)(motor_updown_time * movementLayers);
  return true;
}

bool estimateModelResin(const String &name, int printLayers, double &ml) {
  if (printLayers <= 0) return false;

  double volMm3 = 0.0;
  countPixelsMode = true;
  estimateCancelReq = false;

  for (int layer = 1; layer <= printLayers; layer++) {
    int idx = layer;
    if (Layer_Height > 0.06) idx = layer * 2 - 1;
    String fn = "/" + name + "/" + String(idx) + ".png";
    char nc[120];
    fn.toCharArray(nc, sizeof(nc));
    whitePixelsAccum = 0;
    if (png.open((const char *)nc, myOpen, myClose, myRead, mySeek, PNGDraw) == PNG_SUCCESS) {
      png.decode(NULL, 0);
      png.close();
    }
    volMm3 += (double)whitePixelsAccum * 0.01626 * Layer_Height;
  }

  countPixelsMode = false;
  ml = volMm3 / 1000.0;
  return true;
}

long formLong(const char *name, long fallback, long minVal, long maxVal) {
  if (!server.hasArg(name)) return fallback;
  long v = server.arg(name).toInt();
  if (v < minVal) return minVal;
  if (v > maxVal) return maxVal;
  return v;
}

String formString(const char *name, const String &fallback, uint16_t maxLen) {
  if (!server.hasArg(name)) return fallback;
  String v = server.arg(name);
  v.trim();
  if (v.length() > maxLen) v = v.substring(0, maxLen);
  return v;
}

void sendApiError(int code, const char *message) {
  String out = "{\"ok\":false,\"error\":\"";
  out += jsonEscape(message);
  out += "\"}";
  server.send(code, "application/json", out);
}

void sendApiOk(const String &extra) {
  String out = "{\"ok\":true";
  if (extra.length() > 0) {
    out += ",";
    out += extra;
  }
  out += "}";
  server.send(200, "application/json", out);
}

bool safeRootName(const String &name) {
  if (name.length() == 0 || name.length() > 100) return false;
  if (name == "." || name == "..") return false;
  return name.indexOf('/') < 0 && name.indexOf('\\') < 0;
}

bool rootEntryManaged(const String &name, bool isDir) {
  if (!safeRootName(name)) return false;
  if (isDir) {
    File probe = SD.open(("/" + name + "/1.png").c_str());
    if (!probe) return false;
    probe.close();
    return true;
  }
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".sl1") || lower.endsWith(".zip");
}

void sendRootStyledPage(PGM_P bodyBeforeFw, const char *fw, PGM_P bodyAfterFw);

bool validPrintableModel(const String &name) {
  if (!safeRootName(name)) return false;
  File entry = SD.open(("/" + name).c_str());
  if (!entry) return false;
  bool isDir = entry.isDirectory();
  entry.close();
  return isDir && rootEntryManaged(name, true);
}

void handleApiFiles() {
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  if (!sdCardReady()) {
    sendApiError(503, "sd card unavailable");
    return;
  }

  File dir = SD.open("/");
  if (!dir) {
    sendApiError(503, "sd card unavailable");
    return;
  }

  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  bool usageOk = sdCardUsage(totalBytes, freeBytes);
  uint64_t usedBytes = (usageOk && totalBytes >= freeBytes) ? (totalBytes - freeBytes) : 0;
  int usagePct = (usageOk && totalBytes > 0) ? (int)((usedBytes * 100ULL) / totalBytes) : 0;

  String out = "{\"ok\":true,\"sdReady\":true";
  out += ",\"usageKnown\":";
  out += usageOk ? "true" : "false";
  out += ",\"totalBytes\":\"";
  out += uint64Json(totalBytes);
  out += "\",\"freeBytes\":\"";
  out += uint64Json(freeBytes);
  out += "\",\"usedBytes\":\"";
  out += uint64Json(usedBytes);
  out += "\",\"usagePct\":";
  out += String(usagePct);
  out += ",\"items\":[";
  bool first = true;
  int shown = 0;
  int skipped = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    char rawName[101];
    entry.getName(rawName, sizeof(rawName));
    String name = String(rawName);
    bool isDir = entry.isDirectory();
    uint64_t bytes = isDir ? 0 : (uint64_t)entry.size();
    entry.close();

    if (!rootEntryManaged(name, isDir)) {
      skipped++;
      continue;
    }
    if (shown >= 64) {
      skipped++;
      continue;
    }

    if (!first) out += ",";
    first = false;
    out += "{\"name\":\"";
    out += jsonEscape(name);
    out += "\",\"type\":\"";
    out += isDir ? "model" : "archive";
    out += "\",\"printable\":";
    out += isDir ? "true" : "false";
    // Folder sizes are NOT computed here anymore: sdFolderSize walked every
    // layer file of every model, making the list O(models x layers) slow.
    // Archives keep their (cheap) file size; model folders report 0.
    out += ",\"sizeBytes\":";
    out += "\"";
    out += uint64Json(bytes);
    out += "\"";
    out += "}";
    shown++;
  }
  dir.close();

  out += "],\"hiddenCount\":";
  out += String(skipped);
  out += "}";
  server.send(200, "application/json", out);
}

void handleApiFileModel() {
  // Plain details are read-only, but the ?estimate scan occupies the printer
  // for minutes (decodes every layer) - that is an action, so gate it.
  if (server.hasArg("estimate") && rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  if (!sdCardReady()) {
    sendApiError(503, "sd card unavailable");
    return;
  }

  String name = server.arg("name");
  if (!validPrintableModel(name)) {
    sendApiError(404, "model not found");
    return;
  }

  int printLayers = 0;
  float heightMm = 0;
  uint32_t timeSecs = 0;
  if (!modelStats(name, printLayers, heightMm, timeSecs)) {
    sendApiError(400, "model is not printable");
    return;
  }

  String out = "{\"ok\":true,\"name\":\"";
  out += jsonEscape(name);
  out += "\",\"layers\":";
  out += String(printLayers);
  out += ",\"heightMm\":";
  out += String(heightMm, 2);
  out += ",\"estimatedSecs\":";
  out += String(timeSecs);
  out += ",\"estimatedTime\":\"";
  out += formatDuration(timeSecs);
  out += "\"";

  if (server.hasArg("estimate")) {
    double ml = 0;
    bool ok = estimateModelResin(name, printLayers, ml);
    out += ",\"resinEstimated\":";
    out += ok ? "true" : "false";
    if (ok) {
      out += ",\"resinMl\":";
      out += String(ml, 1);
    }
  }

  out += "}";
  server.send(200, "application/json", out);
}

bool deleteSdItem(const String &requestedName, String &error) {
  if (printerBusy()) {
    error = "printer busy";
    return false;
  }

  if (!sdCardReady()) {
    error = "sd card unavailable";
    return false;
  }

  String name = requestedName;
  if (!safeRootName(name)) {
    error = "invalid sd item";
    return false;
  }

  String path = "/" + name;
  File entry = SD.open(path.c_str());
  if (!entry) {
    error = "sd item not found";
    return false;
  }
  bool isDir = entry.isDirectory();
  entry.close();

  if (!rootEntryManaged(name, isDir)) {
    error = "unsupported sd item";
    return false;
  }

  bool ok = isDir ? deleteModelFolder(path.c_str(), false) : SD.remove(path.c_str());
  if (!ok) {
    error = "delete failed";
    return false;
  }

  return true;
}

// GET /api/files/layer?name=X&i=N - one sliced-layer PNG straight from SD.
// Feeds the dashboard's 3D preview (the browser renders; the ESP32 only
// streams files). i is a PRINT layer index - mapped to the source file the
// same way print_next_png() does (0.10 mm mode uses every other file).
// Read-only, so allowed with Web control off; blocked while printing (SD busy).
void handleApiFileLayer() {
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  if (!sdCardReady()) {
    sendApiError(503, "sd card unavailable");
    return;
  }
  String name = server.arg("name");
  if (!validPrintableModel(name)) {
    sendApiError(404, "model not found");
    return;
  }
  long i = server.arg("i").toInt();
  if (i < 1 || i > 20000) {
    sendApiError(400, "bad layer index");
    return;
  }
  long fileIdx = (Layer_Height > 0.06) ? (2 * i - 1) : i;
  String path = "/" + name + "/" + String(fileIdx) + ".png";
  File f = SD.open(path.c_str());
  if (!f) {
    sendApiError(404, "layer not found");
    return;
  }
  server.sendHeader("Cache-Control", "max-age=86400"); // layers never change
  server.setContentLength(f.size());
  server.send(200, "image/png", "");
  uint8_t buf[512];
  int n;
  WiFiClient client = server.client();
  while ((n = f.read(buf, sizeof(buf))) > 0) client.write(buf, n);
  f.close();
}

void handleApiFileDelete() {
  if (rejectIfWebControlOff()) return;
  String error;
  if (!deleteSdItem(server.arg("name"), error)) {
    int code = 400;
    if (error == "printer busy") code = 409;
    if (error == "sd card unavailable") code = 503;
    if (error == "sd item not found") code = 404;
    if (error == "delete failed") code = 500;
    sendApiError(code, error.c_str());
    return;
  }

  sendApiOk("\"deleted\":true");
}

bool queueModelPrint(const String &requestedName, String &error) {
  if (printerBusy()) {
    error = "printer busy";
    return false;
  }
  if (!sdCardReady()) {
    error = "sd card unavailable";
    return false;
  }

  String name = requestedName;
  if (!safeRootName(name)) {
    error = "invalid model";
    return false;
  }

  String path = "/" + name;
  File entry = SD.open(path.c_str());
  if (!entry) {
    error = "model not found";
    return false;
  }
  bool isDir = entry.isDirectory();
  entry.close();

  if (!isDir || !rootEntryManaged(name, true)) {
    error = "only printable model folders can be started";
    return false;
  }

  name.toCharArray(foldersel_long, sizeof(foldersel_long));
  foldersel = name;
  selIsArchive = false;
  root = SD.open("/");

  if (!prepareSelectedPrintPreview()) {
    delay(1000);
    screen1();
    error = "model is not printable";
    return false;
  }

  webStartPrint = true;
  return true;
}

String configJson() {
  bool mqttConfigured = mqttEnabled || mqttHost.length() > 0 || mqttUser.length() > 0 ||
                        mqttPass.length() > 0 || mqttPort != 1883 || mqttTopic != "TinyMaker";
  String out = "\"locked\":";
  out += printerBusy() ? "true" : "false";
  out += ",\"layerHeight\":";
  out += String(Layer_Height, 2);
  out += ",\"baseExposure\":";
  out += String(Base_Exposure);
  out += ",\"regularExposure\":";
  out += String(Regular_Exposure);
  out += ",\"baseLayers\":";
  out += String(Base_Layer);
  out += ",\"transitionLayers\":";
  out += String(Transition_Layer);
  out += ",\"slowLiftDistance\":";
  out += String(Slow_Lift_Distance);
  out += ",\"fastLiftDistance\":";
  out += String(Fast_Lift_Distance);
  out += ",\"slowLiftFeedrate\":";
  out += String(Slow_Lift_Feedrate);
  out += ",\"fastLiftFeedrate\":";
  out += String(Fast_Lift_Feedrate);
  out += ",\"dropBackFeedrate\":";
  out += String(Drop_Back_Feedrate);
  out += ",\"vatMl\":";
  out += String(Vat_Capacity_Ml);
  out += ",\"lowResinPause\":";
  out += lowResinPauseEnabled ? "true" : "false";
  out += ",\"lowResinMl\":";
  out += String(lowResinThresholdMl);
  out += ",\"askRefill\":";
  out += askRefillEnabled ? "true" : "false";
  out += ",\"uiTimeoutSecs\":";
  out += String(uiTimeoutSecs);
  out += ",\"dryRun\":";
  out += uvLedEnabled ? "false" : "true";
  out += ",\"uvLedEnabled\":";
  out += uvLedEnabled ? "true" : "false";
  out += ",\"wifiEnabled\":";
  out += wifiEnabled ? "true" : "false";
  out += ",\"webDashboardEnabled\":";
  out += webDashboardEnabled ? "true" : "false";
  out += ",\"bootUpdateCheck\":";
  out += bootUpdateCheckEnabled ? "true" : "false";
  out += ",\"mqttEnabled\":";
  out += mqttEnabled ? "true" : "false";
  out += ",\"mqttConfigured\":";
  out += mqttConfigured ? "true" : "false";
  out += ",\"mqttHost\":\"";
  out += jsonEscape(mqttHost);
  out += "\",\"mqttPort\":";
  out += String(mqttPort);
  out += ",\"mqttUser\":\"";
  out += jsonEscape(mqttUser);
  out += "\",\"mqttPasswordSet\":";
  out += mqttPass.length() > 0 ? "true" : "false";
  out += ",\"mqttTopic\":\"";
  out += jsonEscape(mqttTopic);
  out += "\"";
  return out;
}

void applyConfigRequest() {
  float requestedLayer = server.hasArg("layer_height") ? server.arg("layer_height").toFloat() : Layer_Height;
  Layer_Height = requestedLayer < 0.075 ? 0.05 : 0.10;
  Base_Exposure = formLong("base_exposure", Base_Exposure, 10, 60);
  Regular_Exposure = formLong("regular_exposure", Regular_Exposure, 1, 30);
  Base_Layer = formLong("base_layer", Base_Layer, 1, 8);
  Transition_Layer = formLong("transition_layer", Transition_Layer, 0, 10);
  Slow_Lift_Distance = formLong("slow_lift_distance", Slow_Lift_Distance, 1, 3);
  Fast_Lift_Distance = formLong("fast_lift_distance", Fast_Lift_Distance, 1, 3);
  Slow_Lift_Feedrate = formLong("slow_lift_feedrate", Slow_Lift_Feedrate, 20, 50);
  Fast_Lift_Feedrate = formLong("fast_lift_feedrate", Fast_Lift_Feedrate, 20, 50);
  Drop_Back_Feedrate = formLong("drop_back_feedrate", Drop_Back_Feedrate, 20, 50);
  Vat_Capacity_Ml = formLong("vat_ml", Vat_Capacity_Ml, 10, 40);
  lowResinPauseEnabled = server.hasArg("low_resin_pause");
  lowResinThresholdMl = formLong("low_resin_ml", lowResinThresholdMl, 1, 3);
  askRefillEnabled = server.hasArg("ask_refill");
  uiTimeoutSecs = formLong("ui_timeout", uiTimeoutSecs, 0, 3600);
  uvLedEnabled = !server.hasArg("dry_run");
  wifiEnabled = server.hasArg("wifi_enabled");
  webDashboardEnabled = wifiEnabled && server.hasArg("web_dashboard_enabled");
  bootUpdateCheckEnabled = server.hasArg("boot_update_check");
  mqttEnabled = server.hasArg("mqtt_enabled");
  if (!wifiEnabled) mqttEnabled = false;
  mqttHost = formString("mqtt_host", mqttHost, 80);
  mqttPort = formLong("mqtt_port", mqttPort, 1, 65535);
  mqttUser = formString("mqtt_user", mqttUser, 64);
  if (server.hasArg("mqtt_password") && server.arg("mqtt_password").length() > 0) {
    mqttPass = formString("mqtt_password", mqttPass, 64);
  }
  mqttTopic = formString("mqtt_topic", mqttTopic, 64);
  if (mqttTopic.length() == 0) mqttTopic = "TinyMaker";

  savePrintSettings();
  saveDeviceConfig();
}

void handleApiConfigGet() {
  sendApiOk(configJson());
}

void handleApiConfigSave() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  bool wifiWasEnabled = wifiEnabled;
  applyConfigRequest();
  mqttClient.disconnect();
  mqttDiscoverySent = false;
  sendApiOk(configJson());
  if (wifiWasEnabled && !wifiEnabled) {
    // WiFi turned off from the browser: the network stack has no runtime
    // teardown path, so reboot to shut the radio down cleanly. The printer
    // is idle here (printerBusy() gate above).
    delay(700); // let the response reach the client first
    ESP.restart();
  }
}

void resetWebConfigToDefaults() {
  resetSettingsToDefault();
  uiTimeoutSecs = 0;
  uvLedEnabled = true;
  wifiEnabled = true;
  webDashboardEnabled = true;
  bootUpdateCheckEnabled = true;
  saveDeviceConfig();
}

void resetMqttConfigToDefaults() {
  mqttEnabled = false;
  mqttHost = "";
  mqttPort = 1883;
  mqttUser = "";
  mqttPass = "";
  mqttTopic = "TinyMaker";
  saveDeviceConfig();
}

void handleApiConfigDefaults() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  resetWebConfigToDefaults();
  sendApiOk(configJson());
}

void handleApiConfigMqttDefaults() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  resetMqttConfigToDefaults();
  mqttClient.disconnect();
  mqttDiscoverySent = false;
  sendApiOk(configJson());
}

void handleApiConfigDryRun() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  bool enabled = server.hasArg("enabled") &&
                 server.arg("enabled") != "0" &&
                 server.arg("enabled") != "false";
  uvLedEnabled = !enabled;
  saveDeviceConfig();
  sendApiOk(configJson());
}

bool requestPrintPause(String &error) {
  if (!printerBusy()) {
    error = "printer is not printing";
    return false;
  }
  if (current_state == 5) {
    return true;
  }
  if (print_paused || current_state == 0 || current_state == 4 || current_state == 6 ||
      current_state == 7 || current_state == 8 || current_state == 10) {
    error = "pause is not available in this state";
    return false;
  }

  screen1111();
  current_state = 5;
  screen1111_state();
  screen1112();
  gfx2->fillRect(136, 12, 16, 16, 0x8410);
  gfx2->fillTriangle(136, 52, 136, 68, 152, 60, 0x8410);
  gfx2->drawRoundRect(128, 44, 32, 32, 3, 0x8410);
  print_paused = true;
  return true;
}

bool requestPrintResume(String &error) {
  if (printerBusy() && current_state == 7) {
    return true;
  }
  if (!printerBusy() || (current_state != 6 && current_state != 10)) {
    error = "printer is not paused";
    return false;
  }

  current_state = 7;
  screen1111_state();
  webResumePrint = true;
  return true;
}

bool requestPrintStop(String &error) {
  if (!printerBusy()) {
    error = "printer is not printing";
    return false;
  }
  if (current_state == 4) {
    return true;
  }

  bool wasHoming = current_state == 0;
  digitalWrite(LED, LOW);
  screen1111();
  current_state = 4;
  screen1111_state();
  gfx2->fillRect(136, 12, 16, 16, 0x8410);
  gfx2->fillRect(136, 52, 6, 16, 0x8410);
  gfx2->fillRect(146, 52, 6, 16, 0x8410);
  gfx2->drawRoundRect(128, 4, 32, 32, 3, 0x8410);
  print_canceled = true;
  print_paused = false;
  webResumePrint = false;
  if (wasHoming) homing_canceled = true;
  return true;
}

String mqttBaseTopic() {
  String base = mqttTopic;
  base.trim();
  if (base.length() == 0) base = "TinyMaker";
  while (base.endsWith("/")) base.remove(base.length() - 1);
  return base;
}

String mqttDeviceId() {
  String id = WiFi.macAddress();
  id.replace(":", "");
  id.toLowerCase();
  return "tinymaker_" + id;
}

String mqttAvailabilityTopic() {
  return mqttBaseTopic() + "/status/availability";
}

const char *mqttFirmwareVersion() {
#ifdef FIRMWARE_VERSION
  return FIRMWARE_VERSION;
#else
  return "unknown";
#endif
}

String mqttDeviceJson() {
  String id = mqttDeviceId();
  String out = "{\"identifiers\":[\"";
  out += id;
  out += "\"],\"name\":\"TinyMaker\",\"manufacturer\":\"TinyMaker\",\"model\":\"TinyMaker MSLA\",\"sw_version\":\"";
  out += mqttFirmwareVersion();
  out += "\"}";
  return out;
}

bool mqttPublishRaw(const String &topic, const String &payload, bool retained = false) {
  return mqttClient.connected() && mqttClient.publish(topic.c_str(), payload.c_str(), retained);
}

void mqttPublishDiscoveryItem(const char *component, const char *objectSuffix, const char *name,
                              const String &stateTopic, const char *extra = "") {
  String id = mqttDeviceId();
  String objectId = id + "_" + objectSuffix;
  String topic = "homeassistant/";
  topic += component;
  topic += "/";
  topic += objectId;
  topic += "/config";

  String payload = "{\"name\":\"";
  payload += name;
  payload += "\",\"unique_id\":\"";
  payload += objectId;
  payload += "\",\"state_topic\":\"";
  payload += stateTopic;
  payload += "\",\"availability_topic\":\"";
  payload += mqttAvailabilityTopic();
  payload += "\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"device\":";
  payload += mqttDeviceJson();
  if (extra && strlen(extra) > 0) {
    payload += ",";
    payload += extra;
  }
  payload += "}";

  mqttPublishRaw(topic, payload, true);
}

void mqttPublishDiscovery() {
  String base = mqttBaseTopic();
  mqttPublishDiscoveryItem("sensor", "state", "State", base + "/status/state");
  mqttPublishDiscoveryItem("binary_sensor", "busy", "Busy", base + "/status/busy",
                           "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"");
  mqttPublishDiscoveryItem("binary_sensor", "dry_run", "Dry run", base + "/status/dry_run",
                           "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"");
  mqttPublishDiscoveryItem("binary_sensor", "sd_ready", "SD ready", base + "/sd/ready",
                           "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"");
  mqttPublishDiscoveryItem("sensor", "wifi_rssi", "WiFi RSSI", base + "/status/wifi_rssi",
                           "\"device_class\":\"signal_strength\",\"unit_of_measurement\":\"dBm\",\"state_class\":\"measurement\"");
  mqttPublishDiscoveryItem("sensor", "current_layer", "Current layer", base + "/print/current_layer",
                           "\"state_class\":\"measurement\"");
  mqttPublishDiscoveryItem("sensor", "total_layers", "Total layers", base + "/print/total_layers",
                           "\"state_class\":\"measurement\"");
  mqttPublishDiscoveryItem("sensor", "resin_used_ml", "Resin used", base + "/print/resin_used_ml",
                           "\"unit_of_measurement\":\"mL\",\"state_class\":\"measurement\"");
  mqttPublishDiscoveryItem("sensor", "vat_remaining_ml", "Resin left", base + "/vat/remaining_ml",
                           "\"unit_of_measurement\":\"mL\",\"state_class\":\"measurement\"");
  mqttPublishDiscoveryItem("binary_sensor", "vat_low", "Resin low", base + "/vat/low",
                           "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"problem\"");
  mqttPublishDiscoveryItem("sensor", "running_time_sec", "Running time", base + "/print/running_time_sec",
                           "\"device_class\":\"duration\",\"unit_of_measurement\":\"s\",\"state_class\":\"measurement\"");
  mqttPublishDiscoveryItem("sensor", "remaining_time_sec", "Remaining time", base + "/print/remaining_time_sec",
                           "\"device_class\":\"duration\",\"unit_of_measurement\":\"s\",\"state_class\":\"measurement\"");
  // Lowest free heap since boot - HA graphs it during soak tests (a slow
  // decline = leak). Diagnostic category keeps it out of the main card.
  mqttPublishDiscoveryItem("sensor", "min_free_heap", "Min free heap", base + "/system/min_free_heap",
                           "\"unit_of_measurement\":\"B\",\"state_class\":\"measurement\",\"entity_category\":\"diagnostic\"");
  mqttDiscoverySent = true;
}

void mqttPublishStatus() {
  bool busy = printerBusy();
  bool sdReady = busy ? false : sdCardReady();
  String base = mqttBaseTopic();
  int mqttCurrentLayer = busy ? current_layer : 0;
  int mqttTotalLayers = busy ? layer_counter : 0;
  double mqttResinMl = busy ? resinUsedMl : 0.0;

  mqttPublishRaw(mqttAvailabilityTopic(), "online", true);
  mqttPublishRaw(base + "/status/state", printerStateText(), true);
  mqttPublishRaw(base + "/status/busy", busy ? "ON" : "OFF", true);
  mqttPublishRaw(base + "/status/dry_run", uvLedEnabled ? "OFF" : "ON", true);
  mqttPublishRaw(base + "/status/ip", WiFi.localIP().toString(), true);
  mqttPublishRaw(base + "/status/wifi_rssi", String(WiFi.RSSI()), true);
  mqttPublishRaw(base + "/sd/ready", sdReady ? "ON" : "OFF", true);
  mqttPublishRaw(base + "/print/current_layer", String(mqttCurrentLayer), true);
  mqttPublishRaw(base + "/print/total_layers", String(mqttTotalLayers), true);
  mqttPublishRaw(base + "/print/resin_used_ml", String(mqttResinMl, 1), true);
  mqttPublishRaw(base + "/vat/remaining_ml", String(vatRemaining(), 1), true);
  mqttPublishRaw(base + "/vat/low",
                 vatRemaining() <= (float)lowResinThresholdMl ? "ON" : "OFF", true);
  mqttPublishRaw(base + "/print/running_time_sec", String(currentRunSecs()), true);
  mqttPublishRaw(base + "/print/remaining_time_sec", String(remainingPrintSecs()), true);
  mqttPublishRaw(base + "/system/min_free_heap", String(ESP.getMinFreeHeap()), true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t = topic;
  if (t != "homeassistant/status") return;

  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  if (msg == "online") {
    mqttDiscoverySent = false;
  }
}

bool mqttConnect() {
  if (!mqttEnabled || mqttHost.length() == 0 || WiFi.status() != WL_CONNECTED) return false;

  mqttClient.setServer(mqttHost.c_str(), mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  mqttClient.setSocketTimeout(1);
  mqttClient.setKeepAlive(120);

  String id = mqttDeviceId();
  bool ok;
  if (mqttUser.length() > 0) {
    ok = mqttClient.connect(id.c_str(), mqttUser.c_str(), mqttPass.c_str(),
                            mqttAvailabilityTopic().c_str(), 0, true, "offline");
  } else {
    ok = mqttClient.connect(id.c_str(), mqttAvailabilityTopic().c_str(), 0, true, "offline");
  }

  if (!ok) return false;
  mqttClient.subscribe("homeassistant/status");
  mqttPublishRaw(mqttAvailabilityTopic(), "online", true);
  mqttDiscoverySent = false;
  mqttLastPublishMs = 0;
  return true;
}

void mqtt_loop() {
  if (!mqttEnabled || mqttHost.length() == 0 || WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) mqttClient.disconnect();
    mqttDiscoverySent = false;
    return;
  }

  if (printerBusy()) {
    if (mqttClient.connected()) {
      mqttClient.loop();
      unsigned long now = millis();
      if (now - mqttLastPublishMs >= 10000UL) {
        mqttLastPublishMs = now;
        mqttPublishStatus();
      }
    }
    return;
  }

  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - mqttLastAttemptMs < 10000UL) return;
    mqttLastAttemptMs = now;
    if (!mqttConnect()) return;
  }

  mqttClient.loop();
  if (!mqttDiscoverySent) mqttPublishDiscovery();

  unsigned long now = millis();
  if (now - mqttLastPublishMs >= 5000UL) {
    mqttLastPublishMs = now;
    mqttPublishStatus();
  }
}

void handleApiPrintStart() {
  if (rejectIfWebControlOff()) return;
  // Low-resin pre-start check (mirrors the LCD screen 114 warning). The
  // browser confirms and retries with force=1.
  if (!server.hasArg("force") && !printerBusy() &&
      vatRemaining() <= (float)lowResinThresholdMl) {
    String out = "\"warning\":\"low_resin\",\"vatRemainingMl\":";
    out += String(vatRemaining(), 1);
    sendApiOk(out);
    return;
  }

  String error;
  if (!queueModelPrint(server.arg("name"), error)) {
    int code = 400;
    if (error == "printer busy") code = 409;
    if (error == "sd card unavailable") code = 503;
    if (error == "model not found") code = 404;
    sendApiError(code, error.c_str());
    return;
  }

  sendApiOk("\"queued\":true");
}

void handleApiVatRefilled() {
  if (rejectIfWebControlOff()) return;
  vatMarkRefilled();
  String out = "\"vatRemainingMl\":";
  out += String(vatRemaining(), 1);
  sendApiOk(out);
}

void handleApiPrintPause() {
  if (rejectIfWebControlOff()) return;
  String error;
  if (!requestPrintPause(error)) {
    sendApiError(409, error.c_str());
    return;
  }

  sendApiOk("\"paused\":true");
}

void handleApiPrintResume() {
  if (rejectIfWebControlOff()) return;
  String error;
  if (!requestPrintResume(error)) {
    sendApiError(409, error.c_str());
    return;
  }

  sendApiOk("\"resumeQueued\":true");
}

void handleApiPrintStop() {
  if (rejectIfWebControlOff()) return;
  String error;
  if (!requestPrintStop(error)) {
    sendApiError(409, error.c_str());
    return;
  }

  sendApiOk("\"stopping\":true");
}

void handleApiStatus() {
  bool busy = printerBusy();
  bool connected = WiFi.status() == WL_CONNECTED;
  bool sdReady = busy ? false : sdCardReady();
  int statusCurrentLayer = busy ? current_layer : 0;
  int statusTotalLayers = busy ? layer_counter : 0;
  double statusResinMl = busy ? resinUsedMl : 0.0;

  String out = "{";
  out += "\"firmwareVersion\":\"";
#ifdef FIRMWARE_VERSION
  out += jsonEscape(FIRMWARE_VERSION);
#else
  out += "unknown";
#endif
  out += "\",\"busy\":";
  out += busy ? "true" : "false";
  out += ",\"paused\":";
  out += print_paused ? "true" : "false";
  out += ",\"pausing\":";
  out += current_state == 5 ? "true" : "false";
  out += ",\"resuming\":";
  out += current_state == 7 ? "true" : "false";
  out += ",\"stopping\":";
  out += current_state == 4 ? "true" : "false";
  out += ",\"dryRun\":";
  out += uvLedEnabled ? "false" : "true";
  out += ",\"canPause\":";
  out += (busy && !print_paused && current_state >= 1 && current_state <= 3) ? "true" : "false";
  out += ",\"canResume\":";
  out += (current_state == 6 || current_state == 10) ? "true" : "false";
  out += ",\"canStop\":";
  out += (busy && current_state != 4 && current_state != 8) ? "true" : "false";
  out += ",\"state\":\"";
  out += jsonEscape(printerStateText());
  out += "\",\"stateCode\":";
  out += String(current_state);
  out += ",\"wifiRssi\":";
  out += connected ? String(WiFi.RSSI()) : String(0);
  out += ",\"wifiText\":\"";
  out += connected ? String(WiFi.RSSI()) + " dBm" : String("Offline");
  out += "\",\"ip\":\"";
  out += connected ? WiFi.localIP().toString() : String("-");
  out += "\",\"sdReady\":";
  out += sdReady ? "true" : "false";
  out += ",\"sdText\":\"";
  out += busy ? String("Locked") : (sdReady ? String("Ready") : String("Missing"));
  out += "\",\"lifetimePrintSecs\":";
  out += String(totalPrintSecs);
  out += ",\"lifetimePrintTime\":\"";
  out += formatDuration(totalPrintSecs);
  out += "\",\"model\":\"";
  out += busy ? jsonEscape(String(foldersel_long)) : String("");
  out += "\",\"currentLayer\":";
  out += String(statusCurrentLayer);
  out += ",\"totalLayers\":";
  out += String(statusTotalLayers);
  out += ",\"layerText\":\"";
  out += String(statusCurrentLayer) + " / " + String(statusTotalLayers);
  out += "\",\"resinUsedMl\":";
  out += String(statusResinMl, 1);
  // Resin shown like layers: "used / total ml". Total = the fresh model
  // estimate when one exists, otherwise a running per-layer average
  // (needs a few layers to settle; base layers skew it high at first).
  double statusResinTotal = -1;
  if (busy) {
    if (resinNeedForModelMl > 0) statusResinTotal = resinNeedForModelMl;
    else if (current_layer >= 3)
      statusResinTotal = resinUsedMl / current_layer * layer_counter;
  }
  out += ",\"resinText\":\"";
  if (statusResinTotal > 0)
    out += String(statusResinMl, 1) + " / ~" + String(statusResinTotal, 1) + " ml";
  else
    out += String(statusResinMl, 1) + " ml";
  out += "\",\"runSecs\":";
  out += String(currentRunSecs());
  out += ",\"runTime\":\"";
  out += formatDuration(currentRunSecs());
  out += "\",\"remainingSecs\":";
  out += String(remainingPrintSecs());
  out += ",\"remainingTime\":\"";
  out += formatDuration(remainingPrintSecs());
  out += "\",\"webControl\":";
  out += webDashboardRuntimeEnabled() ? "true" : "false";
  out += ",\"askRefill\":";
  out += askRefillEnabled ? "true" : "false";
  out += ",\"vatRemainingMl\":";
  out += String(vatRemaining(), 1);
  out += ",\"vatText\":\"";
  out += String(vatRemaining(), 1) + " ml\",\"vatLow\":";
  out += (vatRemaining() <= (float)lowResinThresholdMl) ? "true" : "false";
  // Heap/uptime instrumentation - the 1.0.0 stability yardstick.
  // minFreeHeap = lowest free heap since boot (leak detector);
  // maxAllocHeap = largest allocatable block (fragmentation indicator).
  out += ",\"freeHeap\":";
  out += String(ESP.getFreeHeap());
  out += ",\"minFreeHeap\":";
  out += String(ESP.getMinFreeHeap());
  out += ",\"maxAllocHeap\":";
  out += String(ESP.getMaxAllocHeap());
  out += ",\"uptimeSecs\":";
  out += String(millis() / 1000UL);
  out += "}";

  server.send(200, "application/json", out);
}

void sendRootStyledPage(PGM_P bodyBeforeFw, const char *fw, PGM_P bodyAfterFw) {
#ifdef FIRMWARE_VERSION
  // The page only changes with the firmware: let the browser cache it and
  // revalidate with a tiny 304. Crucial mid-print, when the network is only
  // serviced in short windows and a ~70 KB page reload would crawl.
  server.sendHeader("ETag", "\"" FIRMWARE_VERSION "\"");
  server.sendHeader("Cache-Control", "no-cache");
#endif
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(PSTR(
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TinyMaker</title>"
    "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
    "<rect width='16' height='16' rx='3' fill='%23e8720c'/>"
    "<text x='8' y='12.5' font-family='Arial' font-size='11' font-weight='bold' fill='white' text-anchor='middle'>T</text></svg>\">"
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;background:#1c1c1e;font-family:-apple-system,Segoe UI,Roboto,sans-serif;color:#eee}"
    // Orange window frame matching the printer's on-screen UI
    ".wrap{max-width:560px;margin:16px auto;padding:20px 18px;border:2px solid #e8720c;border-radius:14px;background:#232326}"
    ".head{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:18px}"
    "h1{margin:0;font-size:24px;color:#e8720c}h2{font-size:17px;margin:0 0 12px;color:#eee}.fw{font-size:13px;color:#aaa}"
    ".card{background:#2a2a2e;border:1px solid #444;border-radius:10px;padding:18px;margin:12px 0}"
    ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}"
    ".label{font-size:12px;color:#aaa}.value{font-size:16px;margin-top:4px}"
    ".files{display:grid;gap:8px}.file{display:flex;align-items:center;justify-content:space-between;gap:10px;"
    "border-top:1px solid #3a3a3f;padding-top:10px}.file:first-child{border-top:0;padding-top:0}"
    ".rowActions{display:flex;gap:8px;align-items:center}"
    ".meta{font-size:12px;color:#aaa;margin-top:3px}"
    "a{color:#84bcf8;text-decoration:none}a:hover{text-decoration:underline}a:visited{color:#84bcf8}"
    "input[type=file],input[type=number],input[type=text],input[type=password],select{width:100%;margin:6px 0 12px;padding:10px;border:1px solid #555;border-radius:8px;background:#1c1c1e;color:#eee}"
    "label span{display:block;font-size:13px;color:#aaa}.configGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 12px}"
    ".spanAll{grid-column:1/-1}"
    ".check{display:flex;align-items:center;gap:8px;margin:6px 0 12px}.check input{width:auto}.check span{display:inline;color:#eee}"
    ".actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:14px}"
    // Tabs: equal-size Dashboard/Settings/Update, active one orange
    ".toolbar{display:flex;gap:8px;margin:12px 0}.toolbar button,.toolbar .button{width:auto;flex:1;margin-top:0;background:#3c3c42;color:#eee}"
    ".toolbar .active{background:#e8720c;color:#fff}"
    // Mini WiFi signal bars (same idea as the printer's badge)
    ".wbars{display:inline-flex;gap:2px;align-items:flex-end;margin-left:8px;vertical-align:middle}"
    ".wbars i{width:4px;background:#4a4a50;border-radius:1px}.wbars i:nth-child(1){height:5px}"
    ".wbars i:nth-child(2){height:9px}.wbars i:nth-child(3){height:13px}.wbars i.on{background:#2fbf4f}"
    ".banner{background:#3a2818;border-color:#e8720c}.banner strong{display:block;color:#ffb15f;margin-bottom:4px}"
    ".progress{height:10px;border:1px solid #555;border-radius:999px;overflow:hidden;background:#1c1c1e;margin:10px 0 16px}"
    ".progress span{display:block;height:100%;width:45%;background:#e8720c;animation:barMove 1.1s infinite linear}"
    ".storageBar{height:10px;border:1px solid #555;border-radius:999px;overflow:hidden;background:#1c1c1e;margin-top:8px}"
    ".storageBar span{display:block;height:100%;width:0;background:#e8720c;transition:width .2s ease}"
    "@keyframes barMove{0%{transform:translateX(-110%)}100%{transform:translateX(230%)}}"
    "button,.button{display:inline-block;width:100%;border:0;border-radius:8px;background:#e8720c;color:#fff;padding:12px 14px;"
    "font-size:15px;font-weight:600;text-align:center;text-decoration:none;cursor:pointer}"
    ".small,.delete{width:auto;padding:9px 11px;font-size:13px}.delete{background:#7b2f2f}.secondaryBtn{background:#3c3c42}"
    "button:disabled{background:#555;color:#aaa;cursor:not-allowed}"
    ".button.secondary{background:#3c3c42;margin-top:10px}"
    // Full-page lock while a firmware update is in flight; cleared by the
    // automatic reload once the printer answers status polls again.
    ".updOverlay{position:fixed;inset:0;z-index:99;background:rgba(20,20,22,.93);display:none;flex-direction:column;align-items:center;justify-content:center;gap:12px;text-align:center;padding:24px}"
    ".updOverlay.on{display:flex}.updOverlay h2{color:#e8720c}"
    ".updSpin{width:34px;height:34px;border:4px solid #3c3c42;border-top-color:#e8720c;border-radius:50%;animation:uspin 1s linear infinite}@keyframes uspin{to{transform:rotate(360deg)}}"
    ".warn{color:#ffb15f}"
    ".hidden{display:none}"
    ".hint{font-size:13px;color:#aaa;margin:10px 0 0;line-height:1.4}"
    ".configGrid .hint{grid-column:1/-1}"
    "@media(max-width:520px){.grid,.configGrid,.actions{grid-template-columns:1fr}.head{display:block}.fw{margin-top:4px}.file{align-items:flex-start;flex-direction:column}.rowActions{width:100%}}"
    // Desktop: widen the frame and lay the dashboard cards out in two
    // columns (status | controls, progress 3D | SD manager). The other
    // views stay a comfortable single column, centered.
    "@media(min-width:1000px){.wrap{max-width:1100px}"
    "#homeView:not(.hidden){display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}"
    "#homeView .card{margin:0}"
    "#modelPanel,#configView,#updateView,#dryRunBanner,#webControlBanner{max-width:760px;margin-left:auto;margin-right:auto}}"
    "</style></head><body><main class='wrap'>"));
  server.sendContent_P(bodyBeforeFw);
  server.sendContent(fw);
  server.sendContent_P(bodyAfterFw);
  server.sendContent_P(PSTR("</main></body></html>"));
}

void handleRootPage() {
#ifdef FIRMWARE_VERSION
  if (server.header("If-None-Match") == "\"" FIRMWARE_VERSION "\"") {
    server.send(304, "text/html", "");
    return;
  }
#endif
  const char *fw =
#ifdef FIRMWARE_VERSION
    FIRMWARE_VERSION;
#else
    "unknown";
#endif
  static const char rootBodyBeforeFw[] PROGMEM = R"SPA(
<div class='head'><div><h1>TinyMaker</h1><div class='fw'>Firmware <span id='fwVersion'>)SPA";
  static const char rootBodyAfterFw[] PROGMEM = R"SPA(</span></div></div></div>

<section id='dryRunBanner' class='card banner hidden'>
  <strong>Dry run mode enabled.</strong>
  <div>Prints will not print, only for testing purposes.</div>
  <button id='disableDryRunButton' class='button secondary' type='button'>Press here to disable</button>
</section>

<section id='webControlBanner' class='card banner hidden'>
  <strong>Web control is off - view only.</strong>
  <div>All actions are disabled: print controls, SD changes, uploads, settings and firmware updates. Monitoring and "Send to printer" from the slicer keep working. Enable: printer &rarr; System &rarr; Advanced.</div>
</section>

<div id='updOverlay' class='updOverlay'><div class='updSpin'></div><h2>Updating firmware</h2><div class='hint'>Do not power off the printer.<br>This page reloads automatically when it is back.</div></div>

<div class='toolbar'>
  <button id='homeViewButton' type='button' class='active'>Dashboard</button>
  <button id='configViewButton' type='button'>Settings</button>
  <button id='updateViewButton' type='button'>Update</button>
</div>

<div id='homeView'>
  <section class='card'>
    <div class='grid'>
      <div><div class='label'>State</div><div id='stateValue' class='value'>Loading</div></div>
      <div><div class='label'>WiFi</div><div class='value'><span id='wifiValue'>-</span><span id='wifiBars' class='wbars'><i></i><i></i><i></i></span></div></div>
      <div><div class='label'>IP</div><div id='ipValue' class='value'>-</div></div>
      <div><div class='label'>Lifetime print time</div><div id='lifetimeValue' class='value'>-</div></div>
      <div><div class='label'>SD card</div><div id='sdValue' class='value'>-</div></div>
      <div><div class='label'>Resin left (est.)</div><div id='vatValue' class='value'>-</div></div>
      <div id='printLayerBox' class='hidden'><div class='label'>Layer</div><div id='layerValue' class='value'>-</div></div>
      <div id='printResinBox' class='hidden'><div class='label'>Resin</div><div id='resinValue' class='value'>-</div></div>
      <div id='printRunBox' class='hidden'><div class='label'>Running time</div><div id='runValue' class='value'>-</div></div>
      <div id='printRemainingBox' class='hidden'><div class='label'>Remaining time</div><div id='remainingValue' class='value'>-</div></div>
    </div>
    <div id='statusMsg' class='hint'></div>
    <div id='debugValue' class='meta'></div>
    <button id='vatRefillButton' class='button secondary' type='button'>VAT refilled</button>
  </section>

  <section id='printControls' class='card hidden'>
    <h2>Print controls</h2>
    <div class='actions'>
      <button id='pauseButton' type='button'>Pause</button>
      <button id='resumeButton' type='button'>Resume</button>
      <button id='stopButton' class='delete' type='button'>Stop</button>
    </div>
  </section>

  <section id='printPreviewCard' class='card hidden'>
    <h2>Print progress 3D</h2>
    <canvas id='printPreviewCanvas' style='width:100%;border:1px solid #3a3a3f;border-radius:8px;background:#151517'></canvas>
    <div class='storageBar'><span id='printPreviewBarFill'></span></div>
  </section>

  <section id='sdSection' class='card'>
    <h2>SD manager</h2>
    <div id='sdUsageBox' class='hidden' style='margin-bottom:12px'>
      <div class='label'>SD memory usage</div>
      <div id='sdUsageText' class='value'>-</div>
      <div class='storageBar'><span id='sdUsageBar'></span></div>
    </div>
    <form id='uploadForm'>
      <input id='uploadFile' type='file' name='file' accept='.sl1,.zip' required>
      <button id='uploadButton' class='button secondary' type='submit'>Upload model</button>
    </form>
    <div id='uploadHint' class='hint'>Uploaded SL1/ZIP files are unpacked into printable model folders on the SD card.</div>
    <input id='filesFilter' type='text' class='hidden' placeholder='Filter models...'>
    <div id='filesList' class='files'></div>
  </section>
</div>

<section id='modelPanel' class='card hidden'>
  <button id='modelBackButton' class='button secondary' type='button'>Back to dashboard</button>
  <h2 id='modelTitle'>Model</h2>
  <div class='grid'>
    <div><div class='label'>Layers</div><div id='modelLayers' class='value'>-</div></div>
    <div><div class='label'>Height</div><div id='modelHeight' class='value'>-</div></div>
    <div><div class='label'>Estimated time</div><div id='modelTime' class='value'>-</div></div>
    <div id='modelResinBox' class='hidden'><div class='label'>Resin needed</div><div id='modelResin' class='value'>-</div></div>
  </div>
  <div id='modelProgress' class='progress hidden'><span></span></div>
  <div class='actions'>
    <button id='modelPreviewButton' type='button'>Preview 3D</button>
    <button id='modelMlButton' type='button'>Calculate ml</button>
    <button id='modelStartButton' class='spanAll' type='button'>Start print</button>
  </div>
  <div id='previewWrap' class='hidden' style='margin-top:12px'>
    <canvas id='modelPreviewCanvas' style='width:100%;border:1px solid #3a3a3f;border-radius:8px;background:#151517'></canvas>
    <div class='hint'>Preview is built in the browser from every Nth sliced layer; the box is the printer's build volume (40.8 &times; 30.6 &times; 68 mm).</div>
  </div>
</section>

<section id='configView' class='card hidden'>
  <h2>Settings</h2>
  <form id='configForm' class='configGrid'>
    <label><span>Layer height (mm)</span><input name='layer_height' id='cfgLayerHeight' type='number' min='0.05' max='0.10' step='0.05'></label>
    <label><span>Base exposure (s)</span><input name='base_exposure' id='cfgBaseExposure' type='number' min='10' max='60' step='1'></label>
    <label><span>Regular exposure (s)</span><input name='regular_exposure' id='cfgRegularExposure' type='number' min='1' max='30' step='1'></label>
    <label><span>Base layers</span><input name='base_layer' id='cfgBaseLayers' type='number' min='1' max='8' step='1'></label>
    <label><span>Transition layers</span><input name='transition_layer' id='cfgTransitionLayers' type='number' min='0' max='10' step='1'></label>
    <label><span>Slow lift distance (mm)</span><input name='slow_lift_distance' id='cfgSlowLiftDistance' type='number' min='1' max='3' step='1'></label>
    <label><span>Fast lift distance (mm)</span><input name='fast_lift_distance' id='cfgFastLiftDistance' type='number' min='1' max='3' step='1'></label>
    <label><span>Slow lift feedrate</span><input name='slow_lift_feedrate' id='cfgSlowLiftFeedrate' type='number' min='20' max='50' step='10'></label>
    <label><span>Fast lift feedrate</span><input name='fast_lift_feedrate' id='cfgFastLiftFeedrate' type='number' min='20' max='50' step='10'></label>
    <label><span>Drop back feedrate</span><input name='drop_back_feedrate' id='cfgDropBackFeedrate' type='number' min='20' max='50' step='10'></label>
    <label><span>VAT size (ml)</span><input name='vat_ml' id='cfgVatMl' type='number' min='10' max='40' step='1'></label>
    <label><span>Low resin warn (ml)</span><input name='low_resin_ml' id='cfgLowResinMl' type='number' min='1' max='3' step='1'></label>
    <label><span>UI timeout (s, 0=off)</span><input name='ui_timeout' id='cfgUiTimeout' type='number' min='0' max='3600' step='5'></label>
    <label class='check'><input name='low_resin_pause' id='cfgLowResinPause' type='checkbox' value='1'><span>Low resin pause (mid-print)</span></label>
    <label class='check'><input name='ask_refill' id='cfgAskRefill' type='checkbox' value='1'><span>Ask refill before print</span></label>
    <label class='check'><input name='dry_run' id='cfgDryRun' type='checkbox' value='1'><span>Dry run mode</span></label>
    <label class='check'><input name='wifi_enabled' id='cfgWifiEnabled' type='checkbox' value='1'><span>WiFi</span></label>
    <label class='check'><input name='web_dashboard_enabled' id='cfgWebDashboardEnabled' type='checkbox' value='1'><span>Web control (browser actions)</span></label>
    <label class='check spanAll'><input name='boot_update_check' id='cfgBootUpdateCheck' type='checkbox' value='1'><span>Boot update check</span></label>
    <label class='check spanAll'><input name='mqtt_enabled' id='cfgMqttEnabled' type='checkbox' value='1'><span>Enable MQTT? (SmartHome integration)</span></label>
    <div id='mqttFields' class='spanAll hidden'>
      <div class='configGrid'>
        <label><span>MQTT broker host</span><input name='mqtt_host' id='cfgMqttHost' type='text' maxlength='80' placeholder='192.168.1.10'></label>
        <label><span>MQTT broker port</span><input name='mqtt_port' id='cfgMqttPort' type='number' min='1' max='65535' step='1'></label>
        <label><span>MQTT username</span><input name='mqtt_user' id='cfgMqttUser' type='text' maxlength='64' autocomplete='username'></label>
        <label><span>MQTT password</span><input name='mqtt_password' id='cfgMqttPassword' type='password' maxlength='64' autocomplete='current-password' placeholder='Leave blank to keep current'></label>
        <label class='spanAll'><span>MQTT topic prefix</span><input name='mqtt_topic' id='cfgMqttTopic' type='text' maxlength='64' placeholder='TinyMaker'></label>
      </div>
      <div id='mqttHint' class='hint'>MQTT publishing will use these settings in the SmartHome integration step.</div>
    </div>
    <button id='configSaveButton' class='spanAll' type='submit'>Save config</button>
  </form>
  <button id='configDefaultsButton' class='button secondary' type='button'>Reset to defaults</button>
  <button id='configMqttResetButton' class='button secondary hidden' type='button'>Reset MQTT</button>
  <div id='configHint' class='hint'>Config locks automatically while printing.</div>
</section>

<section id='updateView' class='card hidden'>
  <h2>Firmware update</h2>
  <div class='grid'>
    <div><div class='label'>Installed</div><div id='updInstalled' class='value'>-</div></div>
    <div><div class='label'>Latest</div><div id='updLatest' class='value'>-</div></div>
  </div>
  <div id='updMsg' class='hint'>Checking...</div>
  <div class='actions'>
    <button id='updInstallLatest' class='spanAll' type='button' disabled>Install latest</button>
  </div>
  <div id='updPickRow' class='configGrid hidden' style='margin-top:10px'>
    <label><span>Install a specific version</span><select id='updVersionSelect' disabled></select></label>
    <button id='updInstallSelected' class='button secondary' type='button' disabled style='align-self:end;margin:6px 0 12px'>Install selected</button>
  </div>
  <form id='updUploadForm' style='margin-top:14px'>
    <div class='label'>Or upload a firmware.bin from <a href='https://github.com/slibbinas/TinyMakerWifi/releases' target='_blank' rel='noopener'>GitHub Releases</a>:</div>
    <input id='updFile' type='file' name='firmware' accept='.bin' disabled required>
    <button id='updUploadButton' class='button secondary' type='submit' disabled>Upload &amp; flash</button>
  </form>
  <div class='hint'>Updates are blocked while printing. Do not power off during an update - the printer reboots by itself when done.</div>
</section>

<script>
const $=id=>document.getElementById(id);
let statusData=null,selectedModel='',sdFreeBytes=0,sdTotalBytes=0;
const setText=(id,v)=>{const e=$(id);if(e)e.textContent=v;};
const show=(id,on)=>{const e=$(id);if(e)e.classList.toggle('hidden',!on);};
const enc=v=>encodeURIComponent(v||'').replace(/'/g,'%27');
const esc=v=>String(v||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const api=async(path,opt,timeoutMs)=>{
  const o=Object.assign({cache:'no-store'},opt||{});let timer=null,ctrl=null;
  if(timeoutMs&&typeof AbortController!=='undefined'){ctrl=new AbortController();o.signal=ctrl.signal;timer=setTimeout(()=>ctrl.abort(),timeoutMs);}
  try{
    const r=await fetch(path,o);let j={};try{j=await r.json();}catch(e){}
    if(!r.ok||j.ok===false)throw new Error(j.error||('HTTP '+r.status));
    return j;
  }catch(e){if(e.name==='AbortError')throw new Error('timeout');throw e;}
  finally{if(timer)clearTimeout(timer);}
};
const msg=(t,warn)=>{const e=$('statusMsg');e.textContent=t||'';e.classList.toggle('warn',!!warn);};
const bytesNum=v=>Number(v||0)||0;
const formatBytes=v=>{
  let n=bytesNum(v),u=['B','KB','MB','GB'],i=0;
  while(n>=1024&&i<u.length-1){n/=1024;i++;}
  return (i? n.toFixed(n<10?1:0):Math.round(n))+' '+u[i];
};
const formatShortTime=ms=>{
  const s=Math.max(0,Math.floor(ms/1000)),m=Math.floor(s/60),r=s%60;
  return m?m+'m '+r+'s':r+'s';
};
const uploadRequiredBytes=bytes=>Math.ceil(bytesNum(bytes)*2.1);
const checkUploadFits=(bytes,hintEl)=>{
  if(!sdFreeBytes)return true;
  const need=uploadRequiredBytes(bytes);
  if(need<=sdFreeBytes)return true;
  hintEl.textContent='Not enough SD space. Need about '+formatBytes(need)+' free while importing; available '+formatBytes(sdFreeBytes)+'.';
  return false;
};
const updateSdUsage=d=>{
  sdFreeBytes=d&&d.usageKnown?bytesNum(d.freeBytes):0;
  sdTotalBytes=d&&d.usageKnown?bytesNum(d.totalBytes):0;
  show('sdUsageBox',!!(d&&d.usageKnown));
  if(!(d&&d.usageKnown))return;
  const used=bytesNum(d.usedBytes),pct=Math.max(0,Math.min(100,Number(d.usagePct)||0));
  setText('sdUsageText',formatBytes(used)+' / '+formatBytes(sdTotalBytes)+' ('+pct+'%)');
  $('sdUsageBar').style.width=pct+'%';
};
const uploadWithProgress=(fd,hintEl)=>{
  const started=Date.now();
  let lastLoaded=0,total=0;
  const render=()=>{
    const elapsed=Date.now()-started, pct=total?Math.round(lastLoaded*100/total):0;
    const speed=elapsed>0?formatBytes(lastLoaded/(elapsed/1000))+'/s':'-';
    hintEl.textContent='Uploading'+(total?' '+pct+'%':'')+' - '+formatShortTime(elapsed)+' - '+speed;
  };
  return new Promise((resolve,reject)=>{
    const xhr=new XMLHttpRequest();
    const timer=setInterval(render,500);
    xhr.open('POST','/upload');
    xhr.upload.onprogress=e=>{lastLoaded=e.loaded||lastLoaded;total=e.lengthComputable?e.total:total;render();};
    xhr.onload=()=>{
      clearInterval(timer);
      let j={};try{j=JSON.parse(xhr.responseText||'{}');}catch(e){}
      if(xhr.status>=200&&xhr.status<300&&j.ok!==false)resolve(j);
      else reject(new Error(j.error||('HTTP '+xhr.status)));
    };
    xhr.onerror=()=>{clearInterval(timer);reject(new Error('upload failed'));};
    xhr.onabort=()=>{clearInterval(timer);reject(new Error('upload cancelled'));};
    render();
    xhr.send(fd);
  });
};
let statusInFlight=false,statusFailCount=0,pendingPrintCmd='',pendingPrintInFlight=false,localPrintStartedAt=0,lpsSynced=false,uploadBusy=false,updLock=false,updSawDown=false,updLockAt=0;
const showUpdLock=()=>{updLock=true;updSawDown=false;updLockAt=Date.now();$('updOverlay').classList.add('on');};
const pageFirmwareVersion=()=>$('fwVersion').textContent.trim();
const reloadIfFirmwareChanged=s=>{
  const live=String(s.firmwareVersion||'').trim(),page=pageFirmwareVersion();
  if(!live||!page||live===page)return false;
  location.replace('/?fw='+encodeURIComponent(live)+'&r='+Date.now());
  return true;
};
const openView=view=>{
  show('homeView',view==='home');
  show('modelPanel',view==='model');
  show('configView',view==='config');
  show('updateView',view==='update');
  $('homeViewButton').classList.toggle('active',view==='home'||view==='model');
  $('configViewButton').classList.toggle('active',view==='config');
  $('updateViewButton').classList.toggle('active',view==='update');
  if(view==='home')loadFiles();
  if(view==='config')loadConfig();
  if(view==='update')loadUpdate();
};

const applyStatus=s=>{
    const was=statusData&&statusData.busy; statusData=s;
    if(s.busy&&typeof s.runSecs==='number'){const c=Date.now()-s.runSecs*1000;if(!lpsSynced||c<localPrintStartedAt){localPrintStartedAt=c;lpsSynced=true;}}
    if(!s.busy){localPrintStartedAt=0;lpsSynced=false;}
    if((pendingPrintCmd==='stop'&&s.stopping)||(pendingPrintCmd==='pause'&&(s.pausing||s.paused))||(pendingPrintCmd==='resume'&&s.resuming))pendingPrintCmd='';
    setText('stateValue',s.state); setText('wifiValue',s.wifiText); setText('ipValue',s.ip); setText('lifetimeValue',s.lifetimePrintTime); setText('sdValue',s.sdText);
    if(typeof s.freeHeap==='number'){const u=s.uptimeSecs||0,ud=Math.floor(u/86400),uh=Math.floor(u%86400/3600),um=Math.floor(u%3600/60);setText('debugValue','heap '+Math.round(s.freeHeap/1024)+'k | min '+Math.round(s.minFreeHeap/1024)+'k | blk '+Math.round(s.maxAllocHeap/1024)+'k | up '+(ud?ud+'d ':'')+uh+'h '+um+'m');}
    const wb=$('wifiBars').children,wr=s.wifiRssi,wn=(wr&&wr<0)?(wr>-60?3:(wr>-75?2:1)):0;for(let i=0;i<3;i++)wb[i].classList.toggle('on',i<wn);
    setText('layerValue',s.layerText); setText('resinValue',s.resinText); setText('runValue',s.runTime); setText('remainingValue',s.remainingTime);
    setText('vatValue',s.vatLow?s.vatText+' (low!)':s.vatText); $('vatValue').style.color=s.vatLow?'#ff6b5f':'';
    const wc=s.webControl!==false;
    show('webControlBanner',!wc);
    $('vatRefillButton').disabled=(!!s.busy&&!s.canResume)||!wc;
    $('modelStartButton').disabled=!wc;
    // estimate scan occupies the printer -> action; keep disabled while running
    $('modelMlButton').disabled=!wc||$('modelMlButton').textContent!=='Calculate ml';
    // preview is read-only (allowed with Web control off) but needs the SD idle
    $('modelPreviewButton').disabled=!!s.busy||$('modelPreviewButton').textContent!=='Preview 3D';
    // dashboard upload is UI-locked only - the slicer endpoint stays open
    $('uploadButton').disabled=!wc||uploadBusy;
    $('uploadFile').disabled=!wc;
    show('dryRunBanner',!!s.dryRun);
    $('disableDryRunButton').disabled=!!s.busy;
    $('disableDryRunButton').textContent=s.busy?'Disable when idle':'Press here to disable';
    ['printLayerBox','printResinBox','printRunBox','printRemainingBox','printControls'].forEach(id=>show(id,s.busy));
    show('sdSection',!s.busy);   // an empty, disabled SD manager mid-print is just noise
    // firmware actions lock the moment a print starts, even mid-visit
    if(s.busy)['updInstallLatest','updInstallSelected','updUploadButton','updFile','updVersionSelect'].forEach(id=>$(id).disabled=true);
    // 3D print progress: reuses slices prefetched before the start (or by an
    // earlier Preview 3D) - zero printer traffic while printing. A page
    // refresh restores them from localStorage.
    if(s.busy&&s.model&&(slicesCache.name!==s.model||!slicesCache.slices.length))restoreSlicesFromStorage(s.model);
    if(s.busy&&s.model&&s.totalLayers>0&&slicesCache.name===s.model&&slicesCache.slices.length){
      show('printPreviewCard',true);
      const frac=Math.min(1,s.currentLayer/s.totalLayers);
      $('printPreviewBarFill').style.width=Math.round(frac*100)+'%';  // smooth, every poll
      if(Math.abs(frac-lastPrevFrac)>=0.004){lastPrevFrac=frac;drawIso($('printPreviewCanvas'),frac);}
    }else{
      show('printPreviewCard',false);
      if(!s.busy)lastPrevFrac=-1;
    }
    const pause=$('pauseButton'), resume=$('resumeButton');
    const showResume=s.canResume||s.resuming;
    pause.classList.toggle('hidden',showResume);
    resume.classList.toggle('hidden',!showResume);
    pause.textContent=s.pausing?'Pausing...':'Pause';
    resume.textContent=s.resuming?'Resuming...':'Resume';
    pause.disabled=!s.canPause||!wc;
    resume.disabled=!s.canResume||!wc;
    $('stopButton').textContent=s.stopping?'Stopping...':'Stop';
    $('stopButton').disabled=!s.canStop||!wc;
    applyPendingPrintUi();
    if(!$('configView').classList.contains('hidden')){
      setConfigDisabled(configIsLocallyLocked()||!wc);
      if(configIsLocallyLocked())$('configHint').textContent='Config is locked while printing.';
    }
    $('uploadButton').disabled=s.busy||!s.sdReady; $('uploadFile').disabled=s.busy||!s.sdReady;
    if(s.busy){$('uploadHint').textContent='Uploads and SD actions are disabled while the printer is busy.';} else if(!s.sdReady){$('uploadHint').textContent='Insert an SD card before uploading or managing models.';} else {$('uploadHint').textContent='Uploaded SL1/ZIP files are unpacked into printable model folders on the SD card.';}
    if(was!==s.busy){loadFiles();loadConfig();}
};
const localBusyStatus=(state,code)=>Object.assign({},statusData||{},{
  busy:true,paused:false,pausing:false,resuming:false,stopping:false,dryRun:statusData&&statusData.dryRun,
  canPause:false,canResume:false,canStop:true,state:state,stateCode:code,wifiText:statusData?statusData.wifiText:'-',ip:statusData?statusData.ip:'-',
  sdReady:false,sdText:'Locked',lifetimePrintTime:statusData?statusData.lifetimePrintTime:'-',layerText:statusData?statusData.layerText:'-',
  resinText:statusData?statusData.resinText:'-',runSecs:statusData?statusData.runSecs:0,runTime:statusData?statusData.runTime:'0m 0s',remainingTime:statusData?statusData.remainingTime:'-'
});
const refreshStatus=async()=>{
  if(statusInFlight)return;
  statusInFlight=true;
  try{
    const s=await api('/api/status',null,30000);
    if(reloadIfFirmwareChanged(s))return;
    if(updLock&&updSawDown)location.reload();
    if(updLock&&!updSawDown&&Date.now()-updLockAt>90000){updLock=false;$('updOverlay').classList.remove('on');msg('Update did not start - the printer never went down. Check System > Update on the printer.',true);}
    statusFailCount=0;
    applyStatus(s);
    if(!pendingPrintCmd)msg('',false);
  }catch(e){
    statusFailCount++;
    if(updLock)updSawDown=true;
    if(statusData&&statusData.busy)msg('Syncing with printer at the next safe network window...',true);
    else msg('Status unavailable: '+e.message,true);
  }finally{statusInFlight=false;}
};

let filesItems=[],filesHidden=0,filesPage=0,filesQuery='';
const FILES_PER_PAGE=12;
const renderFiles=()=>{
  const list=$('filesList');
  const q=filesQuery.toLowerCase();
  const items=q?filesItems.filter(it=>it.name.toLowerCase().indexOf(q)>=0):filesItems;
  const pages=Math.max(1,Math.ceil(items.length/FILES_PER_PAGE));
  if(filesPage>=pages)filesPage=pages-1;
  if(filesPage<0)filesPage=0;
  const slice=items.slice(filesPage*FILES_PER_PAGE,(filesPage+1)*FILES_PER_PAGE);
  const dis=(statusData&&statusData.webControl===false)?' disabled':'';
  let h='';
  if(!slice.length)h='<div class="hint">'+(q?'No models match the filter.':'No printable model folders or SL1/ZIP archives found.')+'</div>';
  slice.forEach(it=>{
    const meta=it.type==='model'?'Model folder':'Archive - '+formatBytes(it.sizeBytes);
    h+='<div class="file"><div><strong>'+esc(it.name)+'</strong><div class="meta">'+esc(meta)+'</div></div><div class="rowActions">';
    if(it.type==='model')h+='<button class="small secondaryBtn" onclick="modelDetails(\''+enc(it.name)+'\',false)">Details</button><button class="small"'+dis+' onclick="startPrint(\''+enc(it.name)+'\')">Start</button>';
    h+='<button class="delete"'+dis+' onclick="deleteFile(\''+enc(it.name)+'\')">Delete</button></div></div>';
  });
  if(pages>1)h+='<div class="rowActions" style="justify-content:center;margin-top:10px"><button class="small secondaryBtn"'+(filesPage===0?' disabled':'')+' onclick="filesNav(-1)">&laquo; Prev</button><span class="meta">'+(filesPage+1)+' / '+pages+'</span><button class="small secondaryBtn"'+(filesPage+1>=pages?' disabled':'')+' onclick="filesNav(1)">Next &raquo;</button></div>';
  if(filesHidden>0)h+='<div class="hint">'+filesHidden+' other SD item(s) hidden.</div>';
  list.innerHTML=h;
};
const loadFiles=async()=>{
  const list=$('filesList');
  if(statusData&&statusData.busy){list.innerHTML='<div class="hint warn">SD manager actions are disabled while printing.</div>';show('filesFilter',false);return;}
  try{
    const d=await api('/api/files');
    updateSdUsage(d);
    filesItems=d.items;filesHidden=d.hiddenCount||0;
    show('filesFilter',filesItems.length>FILES_PER_PAGE);
    renderFiles();
  }catch(e){updateSdUsage(null);list.innerHTML='<div class="hint warn">'+e.message+'</div>';}
};

const modelDetails=async(nameEnc,estimate)=>{
  const name=decodeURIComponent(nameEnc); selectedModel=name; openView('model');
  if(!estimate){
    setText('modelTitle',name); setText('modelLayers','Loading'); setText('modelHeight','-'); setText('modelTime','-');
    show('modelResinBox',false); show('modelProgress',false); show('previewWrap',false);
  } else {
    $('modelMlButton').disabled=true;
    $('modelMlButton').textContent='Calculating...';
    show('modelProgress',true);
  }
  try{
    const d=await api('/api/files/model?name='+enc(name)+(estimate?'&estimate=1':''));
    setText('modelTitle',d.name); setText('modelLayers',d.layers); setText('modelHeight',Number(d.heightMm).toFixed(2)+' mm'); setText('modelTime',d.estimatedTime);
    show('modelResinBox',!!d.resinEstimated); if(d.resinEstimated)setText('modelResin',Number(d.resinMl).toFixed(1)+' ml');
  }catch(e){msg(e.message,true);}
  finally{$('modelMlButton').disabled=false;$('modelMlButton').textContent='Calculate ml';show('modelProgress',false);}
};

// --- 3D preview: fetch every Nth sliced layer, render an isometric stack
// inside the build-volume box. All drawing happens in the browser. Slices are
// kept in slicesCache so the print-progress view can reuse them with zero
// printer traffic while printing.
const PREV_W=720,PREV_H=420,PREV_S=4.6,PREV_CX=360,PREV_CY=272;
const isoPt=(x,y,z)=>({X:PREV_CX+(x-y)*0.866*PREV_S,Y:PREV_CY+(x+y)*0.35*PREV_S-z*0.8*PREV_S});
let slicesCache={name:'',slices:[],gw:80,gh:60,modelH:0,layers:0};
let lastPrevFrac=-1;
const drawIso=(cv,doneFrac)=>{
  const {slices,gw,gh,modelH}=slicesCache;
  cv.width=PREV_W;cv.height=PREV_H;
  const ctx=cv.getContext('2d');ctx.clearRect(0,0,PREV_W,PREV_H);
  const MX=40.8,MY=30.6,MZ=68;
  ctx.strokeStyle='#4a4a52';ctx.lineWidth=1;
  const C=[[0,0,0],[MX,0,0],[MX,MY,0],[0,MY,0],[0,0,MZ],[MX,0,MZ],[MX,MY,MZ],[0,MY,MZ]];
  [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]].forEach(e=>{
    const a=isoPt(...C[e[0]]),b=isoPt(...C[e[1]]);
    ctx.beginPath();ctx.moveTo(a.X,a.Y);ctx.lineTo(b.X,b.Y);ctx.stroke();
  });
  const N=slices.length;
  for(let k=0;k<N;k++){
    const t=N>1?k/(N-1):0,z=t*modelH;
    const s=slices[k],solid=t<=doneFrac;
    for(let j=gh-1;j>=0;j--)for(let i=0;i<gw;i++){
      if(!s[j*gw+i])continue;
      // model edge (any empty 4-neighbour) -> darker: silhouette lines
      const edge=i===0||i===gw-1||j===0||j===gh-1||!s[j*gw+i-1]||!s[j*gw+i+1]||!s[(j-1)*gw+i]||!s[(j+1)*gw+i];
      if(solid){
        // directional light from the front-left corner + height gradient
        const lit=(0.55+0.45*((i/gw)+(1-j/gh))/2)*(edge?0.5:1);
        ctx.fillStyle='rgb('+Math.round((150+105*t)*lit)+','+Math.round((80+90*t)*lit)+','+Math.round((30+50*t)*lit)+')';
      }else{
        ctx.fillStyle='rgba(150,150,165,'+(edge?0.20:0.05)+')'; // not-yet-printed ghost
      }
      const p=isoPt((i+0.5)/gw*MX,(j+0.5)/gh*MY,z);
      ctx.fillRect(p.X-1,p.Y-1,2.2,2.2);
    }
  }
  ctx.fillStyle='#aaa';ctx.font='14px sans-serif';
  let cap=slicesCache.name+' - '+modelH.toFixed(1)+' mm - '+slicesCache.layers+' layers';
  if(doneFrac<1)cap+=' - '+Math.round(doneFrac*100)+'% printed';
  ctx.fillText(cap,12,PREV_H-10);
};
// Slices persist in localStorage (~230 KB) so an F5 mid-print does not lose
// the 3D progress view - the layer endpoint is unavailable while printing.
const saveSlicesToStorage=()=>{
  try{
    const data=slicesCache.slices.map(s=>{let b='';for(let i=0;i<s.length;i++)b+=String.fromCharCode(s[i]);return btoa(b);});
    localStorage.setItem('tmSlices',JSON.stringify({name:slicesCache.name,gw:slicesCache.gw,gh:slicesCache.gh,modelH:slicesCache.modelH,layers:slicesCache.layers,data:data}));
  }catch(e){}
};
const restoreSlicesFromStorage=name=>{
  try{
    const o=JSON.parse(localStorage.getItem('tmSlices')||'null');
    if(!o||o.name!==name||!o.data||!o.data.length)return false;
    const slices=o.data.map(b64=>{const b=atob(b64);const s=new Uint8Array(b.length);for(let i=0;i<b.length;i++)s[i]=b.charCodeAt(i);return s;});
    slicesCache={name:o.name,slices:slices,gw:o.gw,gh:o.gh,modelH:o.modelH,layers:o.layers};
    return true;
  }catch(e){return false;}
};
const fetchSlices=async(name,layers,modelH,btn)=>{
  const N=Math.min(36,layers),gw=80,gh=60,slices=[];
  const oc=document.createElement('canvas');oc.width=gw;oc.height=gh;
  const octx=oc.getContext('2d',{willReadFrequently:true});
  for(let k=0;k<N;k++){
    const li=N>1?1+Math.round(k*(layers-1)/(N-1)):1;
    const img=new Image();
    img.src='/api/files/layer?name='+enc(name)+'&i='+li;
    await new Promise((res,rej)=>{img.onload=res;img.onerror=()=>rej(new Error('layer '+li+' failed to load'));});
    octx.drawImage(img,0,0,gw,gh);
    const d=octx.getImageData(0,0,gw,gh).data;
    const s=new Uint8Array(gw*gh);
    for(let p=0;p<gw*gh;p++)s[p]=d[p*4]>96?1:0;
    slices.push(s);
    if(btn)btn.textContent='Loading '+Math.round(100*(k+1)/N)+'%';
  }
  slicesCache={name:name,slices:slices,gw:gw,gh:gh,modelH:modelH,layers:layers};
  saveSlicesToStorage();
};
const modelPreview=async()=>{
  if(!selectedModel)return;
  const layers=parseInt($('modelLayers').textContent)||0;
  if(!layers){msg('Model details are still loading - try again.',true);return;}
  const modelH=parseFloat($('modelHeight').textContent)||layers*0.05;
  const btn=$('modelPreviewButton');btn.disabled=true;
  show('previewWrap',true);
  try{
    if(slicesCache.name!==selectedModel||!slicesCache.slices.length)
      await fetchSlices(selectedModel,layers,modelH,btn);
    drawIso($('modelPreviewCanvas'),1);
  }catch(e){msg(e.message,true);show('previewWrap',false);}
  btn.disabled=false;btn.textContent='Preview 3D';
};

const applyPendingPrintUi=()=>{
  if(!pendingPrintCmd)return;
  if(pendingPrintCmd==='pause'){$('pauseButton').textContent='Pause requested...';$('pauseButton').disabled=true;}
  if(pendingPrintCmd==='resume'){$('resumeButton').textContent='Resume requested...';$('resumeButton').disabled=true;}
  if(pendingPrintCmd==='stop'){$('stopButton').textContent='Stop requested...';$('stopButton').disabled=true;}
};
const retryPendingPrintCommand=async()=>{
  if(!pendingPrintCmd||pendingPrintInFlight)return;
  pendingPrintInFlight=true;
  const cmd=pendingPrintCmd;
  try{
    await api('/api/print/'+cmd,{method:'POST'},30000);
    pendingPrintCmd='';
    msg((cmd==='stop'?'Stop':cmd==='pause'?'Pause':'Resume')+' accepted.');
    refreshStatus();
  }catch(e){
    if(e.message.indexOf('not printing')>=0||e.message.indexOf('not paused')>=0){pendingPrintCmd='';msg(e.message,true);}
    else msg((cmd==='stop'?'Stop':cmd==='pause'?'Pause':'Resume')+' requested. Waiting for the next safe network window...',true);
  }finally{pendingPrintInFlight=false;applyPendingPrintUi();}
};
const startPrint=async(nameEnc,force)=>{const name=decodeURIComponent(nameEnc||enc(selectedModel));if(!name)return;if(!force&&!confirm('Start this print?'))return;
  if(!force&&statusData&&statusData.askRefill){
    if(confirm('Did you refill the VAT since the last print?\nOK = yes (restart the estimate from a full VAT), Cancel = no.')){
      try{await api('/api/vat/refilled',{method:'POST'});}catch(e){}
    }
  }
  // prefetch slices for the 3D progress view (best effort - the print starts
  // either way; while printing the layer endpoint is unavailable)
  if(!force||!(slicesCache.name===name&&slicesCache.slices.length)){
    try{
      if(slicesCache.name!==name||!slicesCache.slices.length){
        msg('Preparing 3D progress preview...');
        const d=await api('/api/files/model?name='+enc(name));
        await fetchSlices(name,d.layers,Number(d.heightMm)||d.layers*0.05,null);
      }
    }catch(e){}
  }
  try{
  const r=await api('/api/print/start?name='+enc(name)+(force?'&force=1':''),{method:'POST'},8000);
  if(r&&r.warning==='low_resin'){if(confirm('Low resin: ~'+r.vatRemainingMl+' ml left in the VAT (estimate).\nStart anyway?'))startPrint(nameEnc,true);return;}
  msg('Print queued. Waiting for printer sync...');localPrintStartedAt=Date.now();lpsSynced=false;applyStatus(localBusyStatus('Homing',0));openView('home');refreshStatus();}catch(e){msg(e.message,true);}};
const deleteFile=async nameEnc=>{const name=decodeURIComponent(nameEnc);if(!confirm('Delete this SD item?'))return;try{await api('/api/files/delete?name='+enc(name),{method:'POST'});msg('Deleted '+name+'.');loadFiles();}catch(e){msg(e.message,true);}};
const printCommand=async(cmd,confirmText)=>{if(confirmText&&!confirm(confirmText))return;pendingPrintCmd=cmd;applyPendingPrintUi();msg((cmd==='stop'?'Stop':cmd==='pause'?'Pause':'Resume')+' requested. Waiting for printer connection...',true);retryPendingPrintCommand();};
const tickLocalStatus=()=>{
  if(statusData&&statusData.busy&&localPrintStartedAt){
    setText('runValue',formatShortTime(Date.now()-localPrintStartedAt));
  }
};

const setConfigDisabled=disabled=>{document.querySelectorAll('#configForm input,#configForm button,#configDefaultsButton,#configMqttResetButton').forEach(e=>e.disabled=disabled);};
const configIsLocallyLocked=()=>!!(statusData&&statusData.busy);
const updateNetworkFields=()=>{$('cfgWebDashboardEnabled').disabled=!$('cfgWifiEnabled').checked;};
const confirmNetworkToggle=e=>{
  if(e.target.checked){updateNetworkFields();return;}
  const text=e.target.id==='cfgWifiEnabled'
    ? 'Turn WiFi off?\nThe printer will reboot and you will lose web access until WiFi is re-enabled on the printer (System > Advanced).'
    : 'Turn web control off?\nThe dashboard becomes view-only: print controls, SD delete, settings and firmware updates are disabled (monitoring and slicer upload keep working). Re-enable on the printer (System > Advanced).';
  if(!confirm(text))e.target.checked=true;
  updateNetworkFields();
};
const updateMqttFields=()=>show('mqttFields',$('cfgMqttEnabled').checked);
const loadConfig=async()=>{
  try{
    const c=await api('/api/config');
    $('cfgLayerHeight').value=Number(c.layerHeight).toFixed(2); $('cfgBaseExposure').value=c.baseExposure; $('cfgRegularExposure').value=c.regularExposure; $('cfgBaseLayers').value=c.baseLayers; $('cfgTransitionLayers').value=c.transitionLayers;
    $('cfgSlowLiftDistance').value=c.slowLiftDistance; $('cfgFastLiftDistance').value=c.fastLiftDistance; $('cfgSlowLiftFeedrate').value=c.slowLiftFeedrate; $('cfgFastLiftFeedrate').value=c.fastLiftFeedrate; $('cfgDropBackFeedrate').value=c.dropBackFeedrate; $('cfgVatMl').value=c.vatMl; $('cfgLowResinMl').value=c.lowResinMl; $('cfgLowResinPause').checked=!!c.lowResinPause; $('cfgAskRefill').checked=!!c.askRefill; $('cfgUiTimeout').value=c.uiTimeoutSecs; $('cfgDryRun').checked=!!c.dryRun; $('cfgWifiEnabled').checked=!!c.wifiEnabled; $('cfgWebDashboardEnabled').checked=!!c.webDashboardEnabled; $('cfgBootUpdateCheck').checked=!!c.bootUpdateCheck;
    $('cfgMqttEnabled').checked=!!c.mqttEnabled; $('cfgMqttHost').value=c.mqttHost||''; $('cfgMqttPort').value=c.mqttPort||1883; $('cfgMqttUser').value=c.mqttUser||''; $('cfgMqttPassword').value=''; $('cfgMqttTopic').value=c.mqttTopic||'TinyMaker';
    $('mqttHint').textContent=c.mqttPasswordSet?'Password is saved. Enter a new one only if you want to replace it.':'MQTT password is not set.';
    updateNetworkFields();updateMqttFields();
    show('configMqttResetButton',!!c.mqttConfigured);
    $('configDefaultsButton').textContent=c.mqttConfigured?'Reset to defaults (Excluding MQTT)':'Reset to defaults';
    const noWc=!c.webDashboardEnabled;
    const locked=!!c.locked||configIsLocallyLocked()||noWc;
    setConfigDisabled(locked); $('configHint').textContent=noWc?'Settings are disabled while Web control is off (enable it on the printer: System > Advanced).':(locked?'Config is locked while printing.':'Config locks automatically while printing.');
  }catch(e){const locked=configIsLocallyLocked();setConfigDisabled(locked);$('configHint').textContent=locked?'Config is locked while printing.':e.message;}
};

window.modelDetails=modelDetails;
window.startPrint=startPrint;
window.deleteFile=deleteFile;
window.filesNav=d=>{filesPage+=d;renderFiles();};
$('filesFilter').addEventListener('input',e=>{filesQuery=e.target.value.trim();filesPage=0;renderFiles();});

$('uploadForm').addEventListener('submit',async e=>{e.preventDefault();const f=$('uploadFile').files[0];if(!f)return;if(!checkUploadFits(f.size,$('uploadHint')))return;const fd=new FormData();fd.append('file',f);uploadBusy=true;$('uploadButton').disabled=true;$('uploadHint').textContent='Uploading...';const started=Date.now();try{await uploadWithProgress(fd,$('uploadHint'));$('uploadFile').value='';$('uploadButton').classList.add('secondary');$('uploadHint').textContent='Upload complete in '+formatShortTime(Date.now()-started)+'.';loadFiles();}catch(err){$('uploadHint').textContent=err.message;}finally{uploadBusy=false;$('uploadButton').disabled=false;}});
$('configForm').addEventListener('submit',async e=>{e.preventDefault();try{await api('/api/config',{method:'POST',body:new FormData(e.target)});msg('Config saved.');loadConfig();}catch(err){msg(err.message,true);}});
$('configDefaultsButton').addEventListener('click',async()=>{const keep=$('configDefaultsButton').textContent.indexOf('MQTT')>=0;if(!confirm(keep?'Reset config to defaults and keep MQTT settings?':'Reset config to defaults?'))return;try{await api('/api/config/defaults',{method:'POST'});msg(keep?'Defaults restored. MQTT settings kept.':'Defaults restored.');loadConfig();}catch(e){msg(e.message,true);}});
$('configMqttResetButton').addEventListener('click',async()=>{if(!confirm('Reset MQTT settings?'))return;try{await api('/api/config/mqtt/defaults',{method:'POST'});msg('MQTT settings reset.');loadConfig();}catch(e){msg(e.message,true);}});
$('disableDryRunButton').addEventListener('click',async()=>{if(!confirm('Disable dry run mode? Future prints will use the UV LEDs.'))return;try{await api('/api/config/dry-run?enabled=0',{method:'POST'});msg('Dry run disabled.');loadConfig();refreshStatus();}catch(e){msg(e.message,true);}});
$('vatRefillButton').addEventListener('click',async()=>{if(!confirm('Mark the VAT as refilled? The resin estimate restarts from a full VAT.'))return;try{const r=await api('/api/vat/refilled',{method:'POST'});msg('VAT marked as refilled ('+r.vatRemainingMl+' ml).');refreshStatus();}catch(e){msg(e.message,true);}});
$('cfgWifiEnabled').addEventListener('change',confirmNetworkToggle);
$('cfgWebDashboardEnabled').addEventListener('change',confirmNetworkToggle);
$('cfgMqttEnabled').addEventListener('change',updateMqttFields);
$('homeViewButton').addEventListener('click',()=>openView('home'));
$('configViewButton').addEventListener('click',()=>openView('config'));
$('updateViewButton').addEventListener('click',()=>openView('update'));
$('modelBackButton').addEventListener('click',()=>openView('home'));

let updInstalledVer='';
const cmpVer=(a,b)=>{const pa=String(a).split('.').map(Number),pb=String(b).split('.').map(Number);for(let i=0;i<3;i++){if((pa[i]||0)!==(pb[i]||0))return(pa[i]||0)-(pb[i]||0);}return 0;};
// Install selected turns orange only when pressing it would change something
const refreshInstallSelected=()=>{
  const v=$('updVersionSelect').value;
  const same=!v||(updInstalledVer&&cmpVer(v,updInstalledVer)===0);
  $('updInstallSelected').classList.toggle('secondary',!!same);
  if(same)$('updInstallSelected').disabled=true;
  else if(!(statusData&&statusData.busy))$('updInstallSelected').disabled=false;
};
const loadUpdate=async()=>{
  // Installed is known instantly (printed into the page header) and the
  // version picker comes straight from GitHub Pages - neither should wait
  // for the printer's slow (~5-12 s, blocking TLS) latest-version check.
  updInstalledVer=$('fwVersion').textContent.trim();
  setText('updInstalled',updInstalledVer||'-');setText('updLatest','-');
  $('updMsg').textContent='Checking the latest version - takes up to ~10 s...';
  $('updInstallLatest').disabled=true;
  try{
    const r=await fetch('https://slibbinas.github.io/TinyMakerWifi/versions.txt',{cache:'no-store'});
    if(!r.ok)throw 0;
    const list=(await r.text()).split('\n').map(s=>s.trim()).filter(s=>/^\d+\.\d+\.\d+$/.test(s));
    const sel=$('updVersionSelect');sel.innerHTML='';
    list.forEach(v=>{const o=document.createElement('option');o.value=v;o.textContent=v+(cmpVer(v,updInstalledVer)===0?' (installed)':'');sel.appendChild(o);});
    show('updPickRow',list.length>0);
    // usable right away when idle (the server still enforces its own gates);
    // while printing everything stays locked from the first paint
    if(!(statusData&&statusData.busy)){
      $('updVersionSelect').disabled=false;
      $('updUploadButton').disabled=false;$('updFile').disabled=false;
    }
    refreshInstallSelected();
  }catch(e){show('updPickRow',false);}
  try{
    const u=await api('/api/update',null,30000);
    updInstalledVer=u.installed;
    setText('updInstalled',u.installed);setText('updLatest',u.latest&&u.latest.length?u.latest:'-');
    $('updInstallLatest').disabled=!(u.hasUpdate&&u.allowed);
    $('updUploadButton').disabled=!u.allowed;
    $('updFile').disabled=!u.allowed;
    $('updVersionSelect').disabled=!u.allowed;
    if(u.allowed)refreshInstallSelected();else $('updInstallSelected').disabled=true;
    $('updMsg').textContent=u.state===4?'Version check failed - the printer could not reach GitHub. Picked versions and file upload still work.':(!u.allowed?'Updates are blocked right now (printing, or Web control is off).':(u.hasUpdate?'A newer firmware is available.':'Firmware is up to date.'));
  }catch(e){$('updMsg').textContent='Version check did not respond ('+e.message+'). Picked versions and file upload still work.';}
};
const installFirmware=async v=>{
  if(v&&updInstalledVer&&cmpVer(v,updInstalledVer)===0){msg('Version '+v+' is already installed.',true);return;}
  let warn='Install '+(v||'the latest firmware')+'? The printer reboots when done.';
  if(v&&updInstalledVer&&cmpVer(v,updInstalledVer)<0)warn='Downgrade to '+v+'? The older firmware may ignore or reset newer settings.\nThe printer reboots when done.';
  if(!confirm(warn))return;
  try{await api('/api/update/install'+(v?'?version='+v:''),{method:'POST'},20000);msg('Updating... the printer reboots when done.');showUpdLock();}catch(e){msg(e.message,true);}
};
$('updInstallLatest').addEventListener('click',()=>installFirmware(''));
$('updInstallSelected').addEventListener('click',()=>installFirmware($('updVersionSelect').value));
$('updVersionSelect').addEventListener('change',refreshInstallSelected);
$('updUploadForm').addEventListener('submit',async e=>{
  e.preventDefault();
  if(!confirm('Flash the selected firmware.bin? The printer reboots when done.'))return;
  $('updUploadButton').disabled=true;
  try{
    const r=await fetch('/update',{method:'POST',body:new FormData(e.target)});
    if(!r.ok)throw new Error('update rejected (HTTP '+r.status+')');
    msg('Firmware flashed. The printer is rebooting...');showUpdLock();
  }catch(err){msg(err.message,true);}
  $('updUploadButton').disabled=false;
});
$('updFile').addEventListener('change',()=>$('updUploadButton').classList.toggle('secondary',!$('updFile').files.length));
$('uploadFile').addEventListener('change',()=>$('uploadButton').classList.toggle('secondary',!$('uploadFile').files.length));

$('pauseButton').addEventListener('click',()=>printCommand('pause','Pause this print?'));
$('resumeButton').addEventListener('click',()=>printCommand('resume','Resume this print?'));
$('stopButton').addEventListener('click',()=>printCommand('stop','Stop this print?'));
$('modelMlButton').addEventListener('click',()=>modelDetails(enc(selectedModel),true));
$('modelPreviewButton').addEventListener('click',modelPreview);
$('modelStartButton').addEventListener('click',()=>startPrint(enc(selectedModel)));

openView(location.hash==='#settings'?'config':'home');refreshStatus();loadConfig();setInterval(tickLocalStatus,1000);setInterval(()=>{refreshStatus();retryPendingPrintCommand();},2000);
</script>
)SPA";
  sendRootStyledPage(rootBodyBeforeFw, fw, rootBodyAfterFw);
}

// ===================================================================================
// Self-update: check GitHub Pages for a newer firmware and pull-and-flash it.
// version.txt is two lines: latest version + direct firmware.bin URL.
// Reached from the Update screen (421) and, since 0.11.0, from the dashboard
// Update tab (idle + Web control gate, see otaWebAllowed()).
// ===================================================================================
String otaLatestVer = "";   // latest version parsed from version.txt
String otaBinUrl    = "";   // direct URL of the latest firmware.bin
// State of the last check: 0=unknown, 1=checking, 2=up-to-date, 3=update available, 4=error
int otaState = 0;

// Compare two "MAJOR.MINOR.PATCH" strings. >0 if a>b, <0 if a<b, 0 if equal.
// Tolerates a leading "v"/"V" (e.g. a version.txt copied from a git tag name) -
// without this, "v0.8.0" would parse as 0.0.0 and silently report "Up to date".
static int cmpSemver(const char *a, const char *b) {
  if (*a == 'v' || *a == 'V') a++;
  if (*b == 'v' || *b == 'V') b++;
  int va[3] = {0, 0, 0}, vb[3] = {0, 0, 0};
  sscanf(a, "%d.%d.%d", &va[0], &va[1], &va[2]);
  sscanf(b, "%d.%d.%d", &vb[0], &vb[1], &vb[2]);
  for (int i = 0; i < 3; i++) if (va[i] != vb[i]) return va[i] - vb[i];
  return 0;
}

unsigned long otaCheckedAt = 0;   // millis() of the last successful check

// Fetch version.txt over HTTPS and work out whether an update is available.
// Blocking (a few seconds); the caller should show "checking..." first.
// A successful result is cached for 5 min so reopening the Update screen
// (or bouncing to/from "Install from file") does not re-block the UI.
void otaCheckLatest(uint16_t timeoutMs) {
  if ((otaState == 2 || otaState == 3) && millis() - otaCheckedAt < 300000UL)
    return;                        // recent successful check - reuse it
  otaState = 1;
  otaLatestVer = "";
  otaBinUrl = "";
  if (WiFi.status() != WL_CONNECTED) { otaState = 4; return; }

  WiFiClientSecure client;
  client.setInsecure();            // home LAN: skip cert validation
  HTTPClient https;
  https.setConnectTimeout(timeoutMs);
  https.setTimeout(timeoutMs);
  if (!https.begin(client, OTA_VERSION_URL)) { otaState = 4; return; }

  int code = https.GET();
  if (code == HTTP_CODE_OK) {
    String body = https.getString();
    int nl = body.indexOf('\n');
    if (nl < 0) {
      otaLatestVer = body;
      otaLatestVer.trim();
    } else {
      otaLatestVer = body.substring(0, nl);      otaLatestVer.trim();
      otaBinUrl    = body.substring(nl + 1);      otaBinUrl.trim();
    }
#ifdef FIRMWARE_VERSION
    int c = cmpSemver(otaLatestVer.c_str(), FIRMWARE_VERSION);
#else
    int c = 1;
#endif
    otaState = (c > 0) ? 3 : 2;    // newer available vs. already current
    otaCheckedAt = millis();       // cache successful result (see above)
  } else {
    otaState = 4;
  }
  https.end();
}

void otaCheckLatest() { otaCheckLatest(6000); }

const char *otaLatestVerStr() { return otaLatestVer.c_str(); }
int  otaVersionState()        { return otaState; }
bool otaHasUpdate()           { return otaState == 3 && otaBinUrl.length() > 0; }

void otaBootCheckMaybePrompt() {
  if (!bootUpdateCheckEnabled || WiFi.status() != WL_CONNECTED) return;
  otaCheckLatest(2500);
  if (otaHasUpdate()) screenBootUpdatePrompt();
}

// Download a firmware image over HTTPS and flash it. Shows progress on the
// LCD; reboots on success. Shared by "Install latest" and the version picker.
void otaFlashUrl(const String &url, const char *subtitle) {
  netProgressStart("Updating...", subtitle);
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(true);
  httpUpdate.onProgress([](int done, int total) { netProgressBar(done, total); });
  t_httpUpdate_return ret = httpUpdate.update(client, url);
  if (ret == HTTP_UPDATE_FAILED) {   // on success the ESP reboots itself
    netMessage("Update FAILED", httpUpdate.getLastErrorString().c_str());
    delay(1800);
    screen1();
  }
}

// Download the latest firmware.bin and flash it. Reboots on success.
void otaInstallLatest() {
  if (!otaHasUpdate()) return;
  otaFlashUrl(otaBinUrl, "");
}

// Install a specific "X.Y.Z" hosted as firmware-X.Y.Z.bin next to version.txt
// on gh-pages. The URL is built here (never taken from the request), so the
// endpoint cannot be steered to another host.
void otaInstallVersion(const String &ver) {
  String base = OTA_VERSION_URL;
  base = base.substring(0, base.lastIndexOf('/') + 1);
  otaFlashUrl(base + "firmware-" + ver + ".bin", ver.c_str());
}

// GET /api/update -> installed/latest versions for the dashboard Update tab.
// Runs the (5-min-cached) version check, so it may block a few seconds.
void handleApiUpdateGet() {
  // Never run the blocking (~5-12 s TLS) version check mid-print - it would
  // stall the print loop's network window. Cached state is returned instead.
  if (!printerBusy()) otaCheckLatest();
  String out = "\"installed\":\"";
#ifdef FIRMWARE_VERSION
  out += FIRMWARE_VERSION;
#else
  out += "unknown";
#endif
  out += "\",\"latest\":\"";
  out += jsonEscape(otaLatestVer);
  out += "\",\"state\":";
  out += String(otaState);
  out += ",\"hasUpdate\":";
  out += otaHasUpdate() ? "true" : "false";
  out += ",\"allowed\":";
  out += otaWebAllowed() ? "true" : "false";
  sendApiOk(out);
}

// POST /api/update/install[?version=X.Y.Z] -> self-update from the dashboard.
// Without ?version installs the latest; with it, that exact hosted version.
void handleApiUpdateInstall() {
  if (rejectIfWebControlOff()) return;
  if (!otaWebAllowed()) {
    sendApiError(409, "updates are blocked while printing");
    return;
  }
  String ver = server.arg("version");
  if (ver.length() > 0) {
    int a, b, c;
    char tail;
    bool digitsOnly = true;
    for (size_t i = 0; i < ver.length(); i++)
      if (!isDigit(ver[i]) && ver[i] != '.') digitsOnly = false;
    if (!digitsOnly || sscanf(ver.c_str(), "%d.%d.%d%c", &a, &b, &c, &tail) != 3) {
      sendApiError(400, "bad version");
      return;
    }
    sendApiOk("\"installing\":\"" + ver + "\"");
    delay(300);   // let the response reach the browser before flashing blocks
    otaInstallVersion(ver);
    return;
  }
  otaCheckLatest();
  if (!otaHasUpdate()) {
    sendApiError(400, "no update available");
    return;
  }
  sendApiOk("\"installing\":\"" + otaLatestVer + "\"");
  delay(300);
  otaInstallLatest();
}

// --- WiFi status badge on the main menu (top-right corner, above the icons):
// three mini signal bars - green = connected, grey = offline. 2 px margin
// from the screen edge so it does not blend into screen frames.
// Called from screen1/2/3 and refreshed periodically from network_loop().
void drawWifiBadge() {
  // Bars end at y10; the menu boxes start at y13, so a clear
  // row always separates the badge from the System-box outline.
  uint16_t c = (WiFi.status() == WL_CONNECTED) ? GREEN : DARKGREY;
  gfx2->fillRect(148, 8, 2, 3, c);   // short bar
  gfx2->fillRect(151, 5, 2, 6, c);   // medium bar
  gfx2->fillRect(154, 2, 2, 9, c);   // tall bar (ends 2 px from the edge)
}

void network_setup() {
  if (!networkRuntimeEnabled()) {
    WiFi.mode(WIFI_OFF);
    netMessage("WiFi disabled", "");
    delay(1000);
    return;
  }

  // One-shot flag set by wifiDoReset() (menu: System -> WiFi Info -> OK)
  netPrefs.begin("tinymaker", false);
  bool forcePortal = netPrefs.getBool("forcePortal", false);
  if (forcePortal) netPrefs.putBool("forcePortal", false);
  netPrefs.end();

  // Emergency WiFi reset: hold BACK button while powering the printer on
  if (digitalRead(buttonBack) == LOW) {
    wifiEraseCredentials();
    forcePortal = true;
  }

  WiFiManager wm;

  // esp_wifi must be initialized before reading its config
  WiFi.mode(WIFI_STA);

  // Reliable "do we have credentials?" check. getWiFiIsSaved() is NOT
  // trustworthy right after a fresh full flash (uninitialized NVS can read
  // back as "saved" -> printer tries to connect to nothing and never starts
  // the AP; reported by a user on v0.5). Instead read the SSID straight from
  // the esp_wifi config and treat empty SSID as "no credentials".
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  bool hasSSID = strlen((const char*)conf.sta.ssid) > 0;
  bool saved = hasSSID && !forcePortal;

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
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/files", HTTP_GET, handleApiFiles);
  server.on("/api/files/model", HTTP_GET, handleApiFileModel);
  server.on("/api/files/layer", HTTP_GET, handleApiFileLayer);
  server.on("/api/files/delete", HTTP_POST, handleApiFileDelete);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigSave);
  server.on("/api/config/defaults", HTTP_POST, handleApiConfigDefaults);
  server.on("/api/config/mqtt/defaults", HTTP_POST, handleApiConfigMqttDefaults);
  server.on("/api/config/dry-run", HTTP_POST, handleApiConfigDryRun);
  server.on("/api/print/start", HTTP_POST, handleApiPrintStart);
  server.on("/api/vat/refilled", HTTP_POST, handleApiVatRefilled);
  server.on("/api/update", HTTP_GET, handleApiUpdateGet);
  server.on("/api/update/install", HTTP_POST, handleApiUpdateInstall);
  server.on("/api/print/pause", HTTP_POST, handleApiPrintPause);
  server.on("/api/print/resume", HTTP_POST, handleApiPrintResume);
  server.on("/api/print/stop", HTTP_POST, handleApiPrintStop);
  server.on("/api/files/local", HTTP_POST, finishUpload, handleUploadData);

  // Plain endpoint for curl / UVtools testing:
  //   curl -F "file=@model.zip" http://tinymaker.local/upload
  server.on("/upload", HTTP_POST, finishUpload, handleUploadData);

  // Browser dashboard: http://tinymaker.local/
  server.on("/", HTTP_GET, handleRootPage);

  // Web firmware update (users): http://tinymaker.local/update
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateFinish, handleUpdateUpload);

  // Needed for the dashboard's ETag revalidation (WebServer only stores
  // request headers that were registered up front).
  static const char *collectKeys[] = {"If-None-Match"};
  server.collectHeaders(collectKeys, 1);

  server.begin();

  // Developer OTA: PlatformIO 'espota' uploads over WiFi (env:tinymaker-ota).
  // No password by default (home LAN); add ArduinoOTA.setPassword("...")
  // + upload_flags = --auth=... in platformio.ini if the network is shared.
  ArduinoOTA.setHostname("tinymaker");
  ArduinoOTA.onStart([]() { netProgressStart("PlatformIO OTA...", ""); });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    netProgressBar(done, total);
  });
  ArduinoOTA.onEnd([]() { netMessage("OTA OK", "Restarting..."); });
  ArduinoOTA.onError([](ota_error_t e) {
    netMessage("OTA FAILED", "");
    delay(1200);
    screen1();
  });
  ArduinoOTA.begin();

  String ip = "IP: " + WiFi.localIP().toString();
  netMessage("WiFi connected", ip.c_str());
  delay(700);
  otaBootCheckMaybePrompt();
  if (screen == 424) return;
  delay(800);
}

// ===================================================================================
// Loop hook - call as the FIRST line of loop()
// Wrapper so the main .ino never touches the 'server' global directly:
// globals defined in later .ino tabs are NOT visible earlier in the
// combined compilation unit (functions are - prototypes are auto-generated).
// ===================================================================================
void network_loop() {
  if (!networkRuntimeEnabled()) return;
  server.handleClient();   // dashboard stays viewable with Web control off
                           // (actions are 403'd - see rejectIfWebControlOff)
  // Dev espota OTA is answered only while the printer is on the Update screen
  // (same safety gate as the web /update flasher).
  if (otaMenuOpen()) ArduinoOTA.handle();
  mqtt_loop();

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
  if (!uiBlanked && (screen == 1 || screen == 2 || screen == 3 || screen == 4) && millis() - badgeTs > 5000) {
    badgeTs = millis();
    drawWifiBadge();
  }
}

void network_service_window(uint16_t durationMs) {
  unsigned long until = millis() + durationMs;
  do {
    network_loop();
    delay(1);
  } while ((long)(until - millis()) > 0);
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
  gfx2->setTextColor(ORANGE);   // orange title, matching About / Update
  gfx2->setTextSize(1);
  gfx2->setCursor(5, 15);
  if (WiFi.status() == WL_CONNECTED) {
    gfx2->print("WiFi: ");
    gfx2->print(WiFi.SSID());
  } else {
    gfx2->print("WiFi: Offline");
  }
  wifiInfoValues();   // resets color to WHITE for the values below
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

// Robust credential erase. NOTE (verified on hardware): WiFiManager's
// resetSettings() alone is UNRELIABLE on Arduino core 2.0.x - the NVS
// entry sometimes survives and getWiFiIsSaved() stays true after reboot.
void wifiEraseCredentials() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);
  WiFi.disconnect(true, true);  // wifioff = true, eraseap = true
  WiFi.persistent(false);
  delay(100);
  esp_wifi_restore();           // second hammer: restore driver defaults in NVS
}

// Erase stored credentials and reboot -> captive portal on next boot
void wifiDoReset() {
  netMessage("WiFi reset...", "Restarting");
  wifiEraseCredentials();
  // Belt and braces: one-shot flag forces the config portal on next boot
  // even if the NVS erase above misbehaved
  netPrefs.begin("tinymaker", false);
  netPrefs.putBool("forcePortal", true);
  netPrefs.end();
  delay(400);
  ESP.restart();
}

#endif // ENABLE_NETWORK
