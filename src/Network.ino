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
#include "mbedtls/sha256.h" // anonymous stats id (hash of the efuse MAC)
#include "dashboard_html_gz.h" // gzipped web/dashboard.html (gen_dashboard_gz.py)

bool networkRuntimeEnabled() {
  return wifiEnabled || wifiTemporarilyEnabled;
}

bool webDashboardRuntimeEnabled() {
  return webDashboardEnabled || webDashboardTemporarilyEnabled;
}

// 0-28: SD content revision - bumped whenever the SD inventory changes on
// the printer's side (upload/import finished, item deleted, boot animation
// installed/removed). Rides along in /api/status so every open dashboard
// notices out-of-band changes (PrusaSlicer upload, another device, the
// printer itself) and reloads its list instead of waiting for a page reload.
// (sdRev itself lives in TinyMaker.ino - Folder.ino bumps it too, and the
// .ino concatenation order is TinyMaker, Folder, ..., Network.)

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
#define STATS_PING_URL  "https://tinymaker-stats.slibbinas.workers.dev/ping"

WebServer server(80);
Preferences netPrefs;
WiFiClient mqttNet;
PubSubClient mqttClient(mqttNet);
// NOTE: no global 'UNZIP zip;' here! The UNZIP object is ~40 KB - declared
// globally it lands in static .bss and overflows WROOM's DRAM segment
// (verified: "region dram0_0_seg overflowed"). It is heap-allocated inside
// unpackModelToEmptyDir() only for the duration of unpacking.

// Upload state
File uploadFile;
String uploadPath;      // e.g. "/Benchy.sl1"
String modelName;       // e.g. "Benchy"
bool uploadOk = false;
bool uploadRejected = false;
File previewUploadFile;
String previewUploadName;
String previewUploadPath;
String previewUploadTmpPath;
bool previewUploadOk = false;
bool previewUploadRejected = false;
unsigned long otaShownBytes = 0;   // progress counter (upload + web OTA)
long otaTotalBytes = 0;            // web OTA: Content-Length, for the progress bar
unsigned long mqttLastAttemptMs = 0;
unsigned long mqttLastPublishMs = 0;
unsigned long mqttBackoffMs = 10000;   // reconnect interval; grows 10s -> 5min while the broker is down
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
// safeModelName, importZipModel) lives in Import.ino - outside the network
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

String limitedUploadArg(const char *name, uint16_t maxLen) {
  String v = server.arg(name);
  v.trim();
  if (v.length() > maxLen) v = v.substring(0, maxLen);
  return v;
}

// SD-backup presence cache for /api/config (see configJson).
bool sdBackupCacheValid = false;
bool sdBackupCachePresent = false;
uint32_t sdBackupCacheEpoch = 0;

void refreshSdBackupCache() {
  sdBackupCachePresent = sdCardReady() && sdBackupExists();
  sdBackupCacheEpoch = sdBackupCachePresent ? sdBackupSavedEpoch() : 0;
  sdBackupCacheValid = true;
}

// Streaming part - called repeatedly with chunks of the multipart body
void handleUploadData() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    modelName = safeModelName(up.filename);
    uploadPath = "/.tm_upload_" + String((uint32_t)millis()) + ".zip";
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
    // Title + model name + a real progress bar: Content-Length covers the whole
    // multipart body, but the boundary+headers overhead is a few hundred bytes
    // on megabytes of .zip - close enough. Line2 keeps the model name, and a
    // grow-only bar needs no clearing, so nothing here can flicker.
    otaTotalBytes = server.clientContentLength();
    netProgressStart("Receiving model:", modelName.c_str());
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadRejected) return;
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
    if (up.totalSize - otaShownBytes >= 262144) { // redraw every 256 KB - drawing while flash/SD writes run corrupts SPI pixels (orange streaks, user finding)
      otaShownBytes = up.totalSize;
      netProgressBar(up.totalSize, otaTotalBytes);
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

  if (uploadOk) {
    String dest = "/" + modelName;
    bool existingPath = sdPathExists(dest);
    bool existingModel = existingPath && validPrintableModel(modelName);
    String action = limitedUploadArg("action", 16);
    if (server.uri() == "/api/files/local" && action.length() == 0) {
      action = "replace";
    }

    if (existingPath && action != "replace" && action != "rename") {
      String out = "{\"ok\":false,\"error\":\"model already exists\",\"conflict\":true,\"name\":\"";
      out += jsonEscape(modelName);
      out += "\",";
      if (existingModel) appendModelCompareJson(out, "existing", modelName);
      else out += "\"existing\":null";
      out += ",";
      appendZipCompareJson(out, "incoming", uploadPath.c_str());
      out += "}";
      SD.remove(uploadPath.c_str());
      uploadOk = false;
      server.send(409, "application/json", out);
      netMessage("Upload blocked", "Model exists");
      delay(1500);
      screen1();
      return;
    }

    ModelImportOptions options;
    options.replace = action == "replace";
    options.autoRename = action == "rename";
    options.source = limitedUploadArg("source", 24);
    if (options.source.length() == 0) options.source = "dashboard_upload";
    options.connectPublicId = limitedUploadArg("connect_public_id", 32);
    options.connectUrl = limitedUploadArg("connect_url", 160);
    options.originalCredits = limitedUploadArg("original_credits", 160);
    options.licenseName = limitedUploadArg("license", 32);
    if (server.hasArg("resin_ml")) {
      options.resinKnown = true;
      options.resinMl = server.arg("resin_ml").toDouble();
    }

    // Deferred (1-33): the unpack takes minutes and used to run right here,
    // inside the handler - freezing every other browser and leaving the
    // uploader guessing from timeouts. Queue it for the idle loop (sdJobRun):
    // answer the client now, dashboards follow via /api/status sdJob and the
    // finished model announces itself through sdRev (0-28). The slicer's
    // "sent" therefore appears while the printer is still unpacking - the
    // model shows up in the list when the import lands.
    sdJobImportOptions = options;
    sdJobZipPath = uploadPath;
    sdJobName = modelName;
    sdJobKind = "import";   // set last - this flips printerBusy() on
    uploadOk = false;
    String out = "{\"ok\":true,\"queued\":true,\"name\":\"";
    out += jsonEscape(modelName);
    out += "\"}";
    server.send(201, "application/json", out);
    return;   // no screen1() - the job draws its progress and restores the UI
  }
  DBGLN("Upload finished with nothing to import");
  server.send(500, "application/json", "{\"error\":\"upload failed\"}");
  netMessage("Upload FAILED", modelName.c_str());
  delay(1500);
  // Redraw UI - upload messages overwrote whatever screen was shown.
  screen1();
}

void handlePreviewUploadData() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    previewUploadName = server.arg("name");
    String previewType = server.arg("type");
    if (previewType == "05") previewUploadPath = "/" + previewUploadName + "/preview05.png";
    else if (previewType == "1") previewUploadPath = "/" + previewUploadName + "/preview1.png";
    else previewUploadPath = "/" + previewUploadName + "/preview.png";
    previewUploadTmpPath = previewUploadPath + ".tmp";
    previewUploadOk = false;
    previewUploadRejected = false;

    if (printerBusy() || !webDashboardRuntimeEnabled() || !sdCardReady() ||
        !validPrintableModel(previewUploadName)) {
      previewUploadRejected = true;
      return;
    }

    SD.remove(previewUploadTmpPath.c_str());
    previewUploadFile = SD.open(previewUploadTmpPath.c_str(), FILE_WRITE);
    if (!previewUploadFile) {
      previewUploadRejected = true;
      return;
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (previewUploadRejected) return;
    if (up.totalSize > 524288) {
      previewUploadRejected = true;
      if (previewUploadFile) previewUploadFile.close();
      SD.remove(previewUploadTmpPath.c_str());
      return;
    }
    if (previewUploadFile) previewUploadFile.write(up.buf, up.currentSize);
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (previewUploadRejected) return;
    if (previewUploadFile) {
      previewUploadFile.close();
      String backupPath = previewUploadPath + ".bak";
      SD.remove(backupPath.c_str());
      bool hadPreview = SD.exists(previewUploadPath.c_str());
      if (hadPreview && !SD.rename(previewUploadPath.c_str(), backupPath.c_str())) {
        SD.remove(previewUploadTmpPath.c_str());
        previewUploadRejected = true;
        return;
      }
      previewUploadOk = SD.rename(previewUploadTmpPath.c_str(), previewUploadPath.c_str());
      if (previewUploadOk) {
        if (hadPreview) SD.remove(backupPath.c_str());
      } else {
        if (hadPreview) SD.rename(backupPath.c_str(), previewUploadPath.c_str());
        SD.remove(previewUploadTmpPath.c_str());
        previewUploadRejected = true;
      }
    }
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    if (previewUploadFile) previewUploadFile.close();
    SD.remove(previewUploadTmpPath.c_str());
  }
}

void finishPreviewUpload() {
  if (previewUploadRejected) {
    previewUploadRejected = false;
    previewUploadOk = false;
    if (printerBusy()) sendApiError(409, "printer busy");
    else if (!webDashboardRuntimeEnabled()) sendApiError(403, "web control is off - enable it on the printer (System > Advanced)");
    else if (!sdCardReady()) sendApiError(503, "sd card unavailable");
    else if (!validPrintableModel(previewUploadName)) sendApiError(404, "model not found");
    else sendApiError(400, "preview upload failed");
    return;
  }

  if (!previewUploadOk) {
    sendApiError(400, "preview upload failed");
    return;
  }

  previewUploadOk = false;
  sendApiOk("\"preview\":true");
}

// 0-19: the active model's preview PNG, snapshotted to RAM at print start so
// a browser opened mid-print (new phone, reload) still gets an image while
// the SD bus feeds the print. Captured from the print-arm block and freed at
// the single print-exit point - both hooks live in TinyMaker.ino.
uint8_t *previewCacheBuf = nullptr;
size_t previewCacheLen = 0;
String previewCacheModel;

void freePreviewCache() {
  if (previewCacheBuf) { free(previewCacheBuf); previewCacheBuf = nullptr; }
  previewCacheLen = 0;
  previewCacheModel = "";
}

void capturePreviewCache() {
  freePreviewCache();
  if (!sdCardReady()) return;
  String name = String(foldersel_long);
  if (!name.length()) return;
  // Same pick as the serve path below: the render matching the active layer
  // height, legacy single preview as the fallback.
  String path = Layer_Height > 0.06 ? "/" + name + "/preview1.png" : "/" + name + "/preview05.png";
  File f = SD.open(path.c_str());
  if (!f) f = SD.open(("/" + name + "/preview.png").c_str());
  if (!f) return;
  size_t sz = f.size();
  // No PSRAM on the WROOM: cap the snapshot and require slack in the largest
  // free block. Calibrated on hardware: real renders are 66-74 KB and idle
  // maxAllocHeap sits ~110 KB, so a bigger slack would silently reject every
  // preview. 30 KB is safe: print loops only service plain HTTP (no TLS),
  // and the end-of-print TLS notify runs after freePreviewCache(). Too big /
  // too tight -> silently no cache, the endpoint answers 409 as before.
  if (sz == 0 || sz > 120 * 1024 || ESP.getMaxAllocHeap() < sz + 30 * 1024) { f.close(); return; }
  previewCacheBuf = (uint8_t *)malloc(sz);
  if (!previewCacheBuf) { f.close(); return; }
  size_t got = 0;
  while (got < sz) {
    int n = f.read(previewCacheBuf + got, sz - got > 512 ? 512 : sz - got);
    if (n <= 0) break;
    got += n;
  }
  f.close();
  if (got != sz) { freePreviewCache(); return; }
  previewCacheLen = sz;
  previewCacheModel = name;
}

void handleApiFileModelPreview() {
  // 0-19: while printing, the active model's preview is served from the RAM
  // snapshot - no SD touch, so the no-SD-reads-mid-print rule still holds.
  // The type arg is ignored here: the snapshot already matches the active
  // layer height. Other models (or no snapshot) fall through to the 409.
  if (printerBusy() && previewCacheBuf && server.arg("name") == previewCacheModel) {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.setContentLength(previewCacheLen);
    server.send(200, "image/png", "");
    server.client().write(previewCacheBuf, previewCacheLen);
    return;
  }
  // The one SD read that had no busy gate (audit finding). Our own UI never
  // asks for it mid-print, but HTTP is now serviced from inside the print
  // loops - an SD stream from in there costs motor/button latency, and the
  // no-SD-reads-mid-print rule is only a rule if it has no exceptions.
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

  String previewType = server.arg("type");
  String path;
  if (previewType == "05") path = "/" + name + "/preview05.png";
  else if (previewType == "1") path = "/" + name + "/preview1.png";
  // One consistent look (user decision): our voxel render everywhere; the
  // archive/slicer thumbnail is only the fallback when no render is cached
  // yet - it is big (slow off the SD), soft when scaled, and off-style.
  else path = Layer_Height > 0.06 ? "/" + name + "/preview1.png" : "/" + name + "/preview05.png";
  File f = SD.open(path.c_str());
  if (!f && previewType.length() == 0) f = SD.open(("/" + name + "/preview.png").c_str());
  if (!f) {
    sendApiError(404, "preview not found");
    return;
  }

  server.sendHeader("Cache-Control", "max-age=86400");
  server.setContentLength(f.size());
  server.send(200, "image/png", "");
  uint8_t buf[512];
  int n;
  WiFiClient client = server.client();
  while ((n = f.read(buf, sizeof(buf))) > 0) client.write(buf, n);
  f.close();
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
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TinyMaker firmware update</title>"
    // Inline data-URI favicon (project logo: layer stack + WiFi arc) -
    // shows in the browser tab and bookmarks, nothing stored on the device
    "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
    "<rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/>"
    "<rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/>"
    "<rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/>"
    "<path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/>"
    // Green P badge: tells the PRINTER's pinned tab apart from the landing
    // page and the manual, which share the same base logo.
    "<circle cx='47' cy='46' r='16' fill='%232fbf4f'/>"
    "<text x='47' y='53' font-family='Arial' font-size='20' font-weight='bold' fill='white' text-anchor='middle'>P</text></svg>\">"
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
    netProgressStart("Firmware update", "Receiving...");
    // The multipart body is the .bin plus a boundary and a few headers - a couple
    // hundred bytes on ~1.4 MB. Close enough for a bar, and it is the only total
    // we get: Update.begin() is handed UPDATE_SIZE_UNKNOWN.
    otaTotalBytes = server.clientContentLength();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      DBGLN("Update.begin failed");
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaBlocked) return;
    Update.write(up.buf, up.currentSize);
    if (up.totalSize - otaShownBytes >= 524288) { // redraw every 512 KB - see the upload handler note on SPI streaks
      otaShownBytes = up.totalSize;
      String p = String(up.totalSize / 1024) + " KB";
      netProgressBar(up.totalSize, otaTotalBytes);
      netProgressText(p.c_str());
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
  if (sdJobKind == "delete") return "Deleting model";
  if (sdJobKind == "import") return "Importing model";
  if (screen == 1111 || screen == 1112 || screen == 11111 || screen == 11112 || screen == 11113) {
    String prefix = uvLedEnabled ? "" : "Testing - ";
    switch (current_state) {
      case 0: return prefix + "Homing";
      case 1: return prefix + "Curing";   // dry run: "Testing - Curing", like the other phases (was a bare "Testing")
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

// Lifetime LED-on seconds, live: persisted total + the running print's session.
uint32_t currentUvLedSecs() { return totalUvLedSecs + uvLedSessionMs / 1000UL; }

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

bool modelSummaryForModel(const String &name, ModelSummary &summary) {
  // Layer count from model.json when available - the directory scan below
  // costs seconds on 1000+ layer models (user finding: details sat empty).
  int sourceLayers = 0;
  if (!getModelMetadataSourceLayers(name, sourceLayers)) {
    sourceLayers = countModelSourceLayers(name);
    backfillModelMetadataLayers(name, sourceLayers);  // pay the scan once
  }
  return modelSummaryFromSourceLayers(sourceLayers, summary);
}

bool modelStats(const String &name, int &printLayers, float &heightMm, uint32_t &timeSecs) {
  ModelSummary summary;
  if (!modelSummaryForModel(name, summary)) return false;
  printLayers = summary.printLayers;
  heightMm = summary.heightMm;
  timeSecs = summary.estimatedSecs;
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

uint64_t sdEntrySizeRecursive(const String &path) {
  File entry = SD.open(path.c_str());
  if (!entry) return 0;
  if (!entry.isDirectory()) {
    uint64_t size = (uint64_t)entry.size();
    entry.close();
    return size;
  }

  uint64_t total = 0;
  while (true) {
    File child = entry.openNextFile();
    if (!child) break;
    char name[101];
    child.getName(name, sizeof(name));
    bool isDir = child.isDirectory();
    uint64_t size = (uint64_t)child.size();
    child.close();
    String childPath = path + "/" + String(name);
    total += isDir ? sdEntrySizeRecursive(childPath) : size;
  }
  entry.close();
  return total;
}

void appendSummaryObjectJson(String &out, const ModelSummary &summary, uint64_t sizeBytes) {
  out += "{\"sourceLayers\":";
  out += String(summary.sourceLayers);
  out += ",\"layers\":";
  out += String(summary.sourceLayers);
  out += ",\"printLayers\":";
  out += String(summary.printLayers);
  out += ",\"heightMm\":";
  out += String(summary.heightMm, 2);
  out += ",\"estimatedSecs\":";
  out += String(summary.estimatedSecs);
  out += ",\"estimatedTime\":\"";
  out += formatDuration(summary.estimatedSecs);
  out += "\",\"sizeBytes\":\"";
  out += uint64Json(sizeBytes);
  out += "\"}";
}

void appendModelCompareJson(String &out, const char *key, const String &name) {
  out += "\"";
  out += key;
  out += "\":";
  ModelSummary summary;
  if (!modelSummaryForModel(name, summary)) {
    out += "null";
    return;
  }
  appendSummaryObjectJson(out, summary, sdEntrySizeRecursive("/" + name));
}

void appendZipCompareJson(String &out, const char *key, const char *zipPath) {
  ModelSummary summary;
  out += "\"";
  out += key;
  out += "\":";
  if (!scanZipModel(zipPath, summary)) {
    out += "null";
    return;
  }
  appendSummaryObjectJson(out, summary, summary.sizeBytes);
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
  if (name.startsWith(".tm_")) return false;
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
  // One upfront allocation instead of a realloc per append: right after a big
  // upload+unpack the heap is fragmented, and a failed String append is SILENT
  // - the list went out truncated and the dashboard's SD manager "disappeared"
  // (external beta report, feedback #4).
  out.reserve(12288);
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
    if (isDir) {
      String publicId;
      if (getModelMetadataConnectPublicId(name, publicId)) {
        out += ",\"connectPublicId\":\"";
        out += jsonEscape(publicId);
        out += "\"";
      }
    }
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

  ModelSummary summary;
  if (!modelSummaryForModel(name, summary)) {
    sendApiError(400, "model is not printable");
    return;
  }

  String out = "{\"ok\":true,\"name\":\"";
  out += jsonEscape(name);
  out += "\",\"sourceLayers\":";
  out += String(summary.sourceLayers);
  out += ",\"layers\":";
  out += String(summary.sourceLayers);
  out += ",\"printLayers\":";
  out += String(summary.printLayers);
  out += ",\"heightMm\":";
  out += String(summary.heightMm, 2);
  out += ",\"estimatedSecs\":";
  out += String(summary.estimatedSecs);
  out += ",\"estimatedTime\":\"";
  out += formatDuration(summary.estimatedSecs);
  out += "\",\"preview\":";
  bool preview05 = sdPathExists("/" + name + "/preview05.png");
  bool preview1 = sdPathExists("/" + name + "/preview1.png");
  bool previewLegacy = sdPathExists("/" + name + "/preview.png");
  bool previewExists = (Layer_Height > 0.06 ? preview1 : preview05) || previewLegacy;
  out += previewExists ? "true" : "false";
  out += ",\"preview05\":";
  out += preview05 ? "true" : "false";
  out += ",\"preview1\":";
  out += preview1 ? "true" : "false";

  String connectPublicId;
  if (getModelMetadataConnectPublicId(name, connectPublicId)) {
    out += ",\"connectPublicId\":\"";
    out += jsonEscape(connectPublicId);
    out += "\"";
  }

  double ml = 0;
  bool resinKnown = getModelMetadataResin(name, ml);
  bool resinOk = resinKnown;
  if (!resinKnown && server.hasArg("estimate")) {
    resinOk = estimateModelResin(name, summary.printLayers, ml);
    if (resinOk) setModelMetadataResin(name, ml);
  }
  out += ",\"resinEstimated\":";
  out += resinOk ? "true" : "false";
  if (resinOk) {
    out += ",\"resinMl\":";
    out += String(ml, 1);
  }

  out += "}";
  server.send(200, "application/json", out);
}

void handleApiFileModelMetadata() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  if (!sdCardReady()) {
    sendApiError(503, "sd card unavailable");
    return;
  }

  String name = formString("name", "", 100);
  if (!validPrintableModel(name)) {
    sendApiError(404, "model not found");
    return;
  }

  ModelImportOptions options;
  options.connectPublicId = formString("connect_public_id", "", 32);
  options.connectUrl = formString("connect_url", "", 160);
  options.originalCredits = formString("original_credits", "", 160);
  options.licenseName = formString("license", "", 32);
  if (server.hasArg("resin_ml")) {
    options.resinKnown = true;
    options.resinMl = server.arg("resin_ml").toDouble();
  }
  String sharedModelName = formString("shared_model_name", "", 120);

  if (!updateModelMetadataConnectShare(name, options, sharedModelName)) {
    sendApiError(500, "metadata update failed");
    return;
  }
  sendApiOk("\"metadata\":true");
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

  bool ok;
  if (isDir) {
    // Deferred (1-32): a folder delete takes tens of seconds and used to run
    // right here, inside the handler - freezing every other browser (the
    // single-threaded server can't answer anyone mid-handler). Queue it for
    // the idle loop instead (sdJobRun): the handler answers now, the
    // dashboards follow the job via /api/status sdJob fields.
    sdJobName = name;
    sdJobKind = "delete";   // set last - this flips printerBusy() on
    return true;
  } else {
    ok = SD.remove(path.c_str());   // archives are single files - near-instant
  }
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
  bool rawSource = server.hasArg("source") && server.arg("source") == "1";
  float layerHeight = server.hasArg("layer_height") ? server.arg("layer_height").toFloat() : Layer_Height;
  long fileIdx = rawSource ? i : ((layerHeight > 0.06) ? (2 * i - 1) : i);
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

  if (sdJobKind == "delete") {
    // Folder queued for the idle loop - the dashboard tracks it via status.
    sendApiOk("\"queued\":true");
    return;
  }
  sdRev++;  // 0-28 (single-file path completed synchronously above)
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
  out += ",\"prevRegularExposure\":";
  out += String(prevRegularExposure);
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
  out += ",\"statsPing\":";
  out += statsPingEnabled ? "true" : "false";
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
  // Cached: the dashboard polls /api/config and the three SD opens per
  // request (ready + exists + epoch) added up. Refreshed lazily once and
  // after every Backup-to-SD write.
  if (!sdBackupCacheValid && !printerBusy()) refreshSdBackupCache();
  bool sdBk = !printerBusy() && sdBackupCachePresent;
  out += ",\"sdBackupPresent\":";
  out += sdBk ? "true" : "false";
  out += ",\"sdBackupEpoch\":";
  out += String(sdBk ? sdBackupCacheEpoch : 0);
  out += tinymakerConnectConfigJson();
  out += tinymakerTelegramConfigJson();
  out += tinymakerWhatsAppConfigJson();
  out += tinymakerDiscordConfigJson();
  return out;
}

void applyConfigRequest() {
  float requestedLayer = server.hasArg("layer_height") ? server.arg("layer_height").toFloat() : Layer_Height;
  Layer_Height = requestedLayer < 0.075 ? 0.05 : 0.10;
  Base_Exposure = formLong("base_exposure", Base_Exposure, 10, 60);
  long oldRegular = Regular_Exposure;
  Regular_Exposure = formLong("regular_exposure", Regular_Exposure, 1, 30);
  rememberPrevRegularExposure(oldRegular);   // no-op unless it actually changed
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
  statsPingEnabled = server.hasArg("stats_ping");
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
  connectEnabled = server.hasArg("connect_enabled");
  if (!wifiEnabled) connectEnabled = false;
  connectBaseUrl = connectNormalizeBaseUrl(formString("connect_base_url", connectBaseUrl, 128));
  connectPrinterName = formString("connect_printer_name", connectPrinterName, 64);
  if (connectEnabled) {
    connectLeaderboardOptIn = server.hasArg("connect_leaderboard");
  }
  if (server.hasArg("connect_auto_backup_set")) {
    connectAutoBackup = server.hasArg("connect_auto_backup");
  }
  // One notification channel at a time (radio in the form): Telegram OR
  // WhatsApp OR off. Credentials of the inactive channel are kept.
  String ntf = formString("notify_channel", tgEnabled ? "tg" : (waEnabled ? "wa" : (dcEnabled ? "dc" : "none")), 8);
  tgEnabled = wifiEnabled && ntf == "tg";
  waEnabled = wifiEnabled && ntf == "wa";
  dcEnabled = wifiEnabled && ntf == "dc";
  // Token is a secret: only overwrite when a new one is supplied, so a blank
  // field keeps the stored value (same rule as the MQTT password).
  if (server.hasArg("tg_token") && server.arg("tg_token").length() > 0) {
    tgToken = formString("tg_token", tgToken, 64);
  }
  tgChat = formString("tg_chat", tgChat, 32);
  waPhone = formString("wa_phone", waPhone, 20);
  if (server.hasArg("wa_apikey") && server.arg("wa_apikey").length() > 0) {
    waApiKey = formString("wa_apikey", waApiKey, 16);
  }
  if (server.hasArg("dc_webhook") && server.arg("dc_webhook").length() > 0) {
    dcWebhook = formString("dc_webhook", dcWebhook, 200);
    dcWebhook.trim();
  }

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
  tinymakerConnectScheduleBackup();
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

// GET /api/config/backup -> the full settings backup as a downloadable file.
// Web-control-gated: the backup contains the MQTT password.
void handleApiConfigBackupGet() {
  if (rejectIfWebControlOff()) return;
  server.sendHeader("Content-Disposition", "attachment; filename=tinymaker-backup.json");
  server.send(200, "application/json", buildConfigBackupJson());
}

// POST /api/config/backup/sd -> write the backup file onto the SD card, so a
// future full USB reflash can offer to restore it at first boot.
void handleApiConfigBackupSd() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  if (!sdCardReady()) {
    sendApiError(409, "SD card not ready");
    return;
  }
  if (!writeBackupToSd()) {
    sendApiError(500, "could not write the backup to SD");
    return;
  }
  refreshSdBackupCache();
  sendApiOk("\"message\":\"Backup saved to SD (tinymaker-backup.json).\"");
}

// POST /api/config/restore (raw JSON body) -> apply an uploaded backup.
void handleApiConfigRestore() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  String body = server.arg("plain");
  if (body.length() < 10 || backupNum(body, "backupVersion", 0) < 1) {
    sendApiError(400, "not a TinyMaker backup file");
    return;
  }
  bool wifiWasEnabled = wifiEnabled;
  applyConfigBackup(body);
  mqttClient.disconnect();
  mqttDiscoverySent = false;
  tinymakerConnectScheduleBackup();
  sendApiOk(configJson());
  if (wifiWasEnabled && !wifiEnabled) {
    delay(700); // same as the config-save path: reboot to shut the radio down
    ESP.restart();
  }
}

// POST /api/config/restore/sd -> apply the backup stored on the SD card
// (tinymaker-backup.json), the same file the first-boot prompt offers after a
// full USB reflash. Lets a user restore without keeping the file on a computer.
void handleApiConfigRestoreSd() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }
  if (!sdCardReady()) {
    sendApiError(409, "SD card not ready");
    return;
  }
  if (!sdBackupExists()) {
    sendApiError(404, "no backup on the SD card (tinymaker-backup.json)");
    return;
  }
  bool wifiWasEnabled = wifiEnabled;
  if (!restoreFromSdBackup()) {
    sendApiError(500, "could not read the backup from SD");
    return;
  }
  mqttClient.disconnect();
  mqttDiscoverySent = false;
  tinymakerConnectScheduleBackup();
  sendApiOk(configJson());
  if (wifiWasEnabled && !wifiEnabled) {
    delay(700); // same as the config-save path: reboot to shut the radio down
    ESP.restart();
  }
}

void resetWebConfigToDefaults() {
  resetSettingsToDefault();
  uiTimeoutSecs = 60;  // matches the fresh-install default (0-23)
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

void resetConnectConfigToDefaults() {
  connectEnabled = false;
  connectBaseUrl = "https://connect.tinymakerwifi.com";
  connectPrinterName = "";
  connectLeaderboardOptIn = false;
  connectPrinterPublicId = "";
  connectPublishToken = "";
  connectRecoveryCode = "";
  connectLastStatus = "";
  connectAutoBackup = false;
  connectBackupEpoch = 0;
  saveDeviceConfig();
}

void handleApiConfigDefaults() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  resetWebConfigToDefaults();
  tinymakerConnectScheduleBackup();
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
  tinymakerConnectScheduleBackup();
  sendApiOk(configJson());
}

void handleApiConfigConnectDefaults() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  resetConnectConfigToDefaults();
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
  tinymakerConnectScheduleBackup();
  sendApiOk(configJson());
}

bool requestPrintPause(String &error) {
  if (sdJobKind.length() > 0) {   // "busy" here is an SD job, not a print
    error = "printer is not printing";
    return false;
  }
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
  if (sdJobKind.length() > 0) {   // "busy" here is an SD job, not a print
    error = "printer is not paused";
    return false;
  }
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
  if (sdJobKind.length() > 0) {   // "busy" here is an SD job, not a print
    error = "printer is not printing";
    return false;
  }
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
    mqttBackoffMs = 10000;   // reset so re-enabling / reconnecting WiFi retries promptly
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
    if (now - mqttLastAttemptMs < mqttBackoffMs) return;
    mqttLastAttemptMs = now;
    if (!mqttConnect()) {
      // Broker down: back off 10s -> 5min. mqttConnect() blocks (~socket
      // timeout), so hammering it every 10s briefly freezes the single loop
      // (LCD + web + print) with no on-screen sign - the "hangs silently" bug.
      mqttBackoffMs = min(mqttBackoffMs * 2, 300000UL);
      return;
    }
    mqttBackoffMs = 10000;   // reconnected: back to a snappy retry
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

  // "ok":true is the browser's integrity mark: api() rejects a 200 whose JSON
  // lacks it as a truncated/garbled body. Status and the boot-anim list were
  // the two JSON answers built without it.
  String out = "{\"ok\":true,";
  out += "\"firmwareVersion\":\"";
#ifdef FIRMWARE_VERSION
  out += jsonEscape(FIRMWARE_VERSION);
#else
  out += "unknown";
#endif
  out += "\",\"firmwareBuild\":\"";
#ifdef GIT_REV
  out += jsonEscape(GIT_REV);
#endif
  out += "\",\"buildDate\":\"";
  out += __DATE__ " " __TIME__;  // compile moment - shown on Settings > About
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
  // 1-32/1-33: what the SD job (if any) is doing - lets EVERY dashboard show
  // "Deleting/Importing X" instead of the presser knowing and observers guessing.
  out += ",\"sdJob\":\"";
  out += sdJobKind;
  out += "\",\"sdJobName\":\"";
  out += jsonEscape(sdJobName);
  out += "\"";
  // Phase countdown - curing/lifting/dropping mid-print, plus the final lift
  // (state 4 Canceling / 8 Finished) and the pause/resume travels (state 5
  // Pausing / 7 Resuming), whose durations are computed from distance and
  // speed. Total 0 = unknown; the dashboard shows no number then.
  {
    bool phased = busy && phaseTotalMs > 0 &&
                  ((current_state >= 1 && current_state <= 5) ||
                   current_state == 7 || current_state == 8);
    out += ",\"phaseTotalMs\":";
    out += String((unsigned long)(phased ? phaseTotalMs : 0));
    out += ",\"phaseElapsedMs\":";
    out += String((unsigned long)(phased ? millis() - phaseStartMs : 0));
  }
  out += ",\"layerHeight\":";
  out += String(Layer_Height, 2);
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
  out += "\",\"sdRev\":";   // 0-28: SD content revision (numeric, no quotes)
  out += String(sdRev);
  out += ",\"lifetimePrintSecs\":";
  out += String(totalPrintSecs);
  out += ",\"lifetimePrintTime\":\"";
  out += formatDuration(totalPrintSecs);
  out += "\",\"uvLedSecs\":";
  out += String(currentUvLedSecs());
  out += ",\"uvLedTime\":\"";
  out += formatDuration(currentUvLedSecs());
  // 0-30 reset-reason telemetry: why the last boot happened, and the last
  // recorded mid-print death (null = none on record).
  out += "\",\"bootReason\":\"";
  out += resetReasonName((uint8_t)bootResetReason);
  out += "\",\"lastCrash\":";
  if (crashSeen) {
    out += "{\"reason\":\"";
    out += resetReasonName(crashReason);
    out += "\",\"layer\":";
    out += String(crashLayer);
    out += ",\"epoch\":";
    out += String(crashEpoch);
    out += "}";
  } else {
    out += "null";
  }
  out += ",\"model\":\"";
  out += busy ? jsonEscape(String(foldersel_long)) : String("");
  out += "\",\"previewCached\":";   // 0-19: RAM preview available mid-print
  out += previewCacheBuf ? "true" : "false";
  out += ",\"currentLayer\":";
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

// Include GIT_REV when available: experimental builds often share one SemVer,
// and a version-only tag can make the browser keep stale dashboard HTML.
String pageEtag() {
  // Content hash of the gzipped dashboard (DASHBOARD_ETAG, from
  // gen_dashboard_gz.py) - changes iff the page changes, so it invalidates the
  // browser cache exactly when needed, and never on a dirty rebuild that left
  // the page untouched (same GIT_REV used to yield a stale 304).
  String etag = "\"";
#ifdef DASHBOARD_ETAG
  etag += DASHBOARD_ETAG;
#else
  etag += "dev";
#endif
  etag += "\"";
  return etag;
}

void handleRootPage() {
  if (server.header("If-None-Match") == pageEtag()) {
    server.send(304, "text/html", "");
    return;
  }
  // The dashboard is one static, version-agnostic page (web/dashboard.html),
  // gzipped at build time into dashboard_html_gz.h. #fwVersion / #fwBuild fill
  // from /api/status at runtime, so the whole page is one pre-compressed blob.
  // Let the browser cache it and revalidate with a tiny 304 - crucial mid-print,
  // when the network is serviced in short windows and a full reload would crawl.
  server.sendHeader("ETag", pageEtag());
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Vary", "Accept-Encoding");
  server.send_P(200, "text/html", (const char *)DASHBOARD_HTML_GZ, DASHBOARD_HTML_GZ_LEN);
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
  // A FAILED check must be remembered too, or every /api/update GET re-runs
  // the blocking TLS call: the single-threaded loop then stops answering the
  // web UI and the LCD for seconds at a time whenever GitHub is slow.
  if (otaState == 4 && millis() - otaCheckedAt < 60000UL) return;
  otaState = 1;
  otaLatestVer = "";
  otaBinUrl = "";
  // No cache stamp here: this path is instant, so it costs nothing to retry.
  if (WiFi.status() != WL_CONNECTED) { otaState = 4; return; }

  WiFiClientSecure client;
  client.setInsecure();            // home LAN: skip cert validation
  HTTPClient https;
  https.setConnectTimeout(timeoutMs);
  https.setTimeout(timeoutMs);
  if (!https.begin(client, OTA_VERSION_URL)) { otaState = 4; otaCheckedAt = millis(); return; }

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
    otaCheckedAt = millis();       // and the failure, for a shorter while
  }
  https.end();
}

void otaCheckLatest() { otaCheckLatest(6000); }

const char *otaLatestVerStr() { return otaLatestVer.c_str(); }
int  otaVersionState()        { return otaState; }
bool otaHasUpdate()           { return otaState == 3 && otaBinUrl.length() > 0; }

void otaBootCheckMaybePrompt() {
  // Never hijack the boot with an update prompt while a power-loss resume is
  // about to start the print (screen 427 -> network_setup() -> print).
  if (resumeBootPending) return;
  if (!bootUpdateCheckEnabled || WiFi.status() != WL_CONNECTED) return;
  otaCheckLatest(2500);
  if (otaHasUpdate()) screenBootUpdatePrompt();
}

// ---- Anonymous install stats ----------------------------------------------
// Once per firmware version (the first boot with WiFi after a flash) the
// printer sends ONE anonymous ping: a SHA-256 hash of the factory MAC, the
// firmware version and the lifetime print hours. Nothing else is sent or
// stored (see the manual). Switchable in Settings; a failed ping is silent
// and retried on the next boot.
String statsHardwareHash() {
  String seed = "TinyMakerWiFi:" + connectHardwareId();
  unsigned char digest[32];
  mbedtls_sha256_ret((const unsigned char *)seed.c_str(), seed.length(), digest, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
  hex[64] = 0;
  return String(hex);
}

void statsPingMaybe() {
  if (!statsPingEnabled || WiFi.status() != WL_CONNECTED) return;
  String cur = connectFirmwareVersion();
  sysPrefs.begin("tinymaker", true);
  String pinged = sysPrefs.getString("statsPingVer", "");
  sysPrefs.end();
  if (pinged == cur) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, STATS_PING_URL)) return;
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"id\":\"" + statsHardwareHash() + "\",\"version\":\"" + cur +
                "\",\"hours\":" + String(totalPrintSecs / 3600.0, 1) + "}";
  int code = http.POST(body);
  http.end();
  if (code >= 200 && code < 300) {
    sysPrefs.begin("tinymaker", false);
    sysPrefs.putString("statsPingVer", cur);
    sysPrefs.end();
    DBGLN("Stats ping sent");
  }
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
  // Cleared area starts at 99 to cover the dry-run chip slot too.
  gfx2->fillRect(99, 1, 59, 11, BLACK);
  if (!uvLedEnabled) {
    // Dry-run "DR" chip (V pick 07-22) - the printer had no on-device hint
    // that the UV LED is disabled. Chip ends at x116: the cloud's left edge
    // is x124, so the gap matches the 8 px between the cloud and the bars.
    gfx2->fillRoundRect(99, 1, 17, 11, 2, ORANGE);
    gfx2->setFont(NULL);          // built-in 6x8 font fits the 11 px row
    gfx2->setTextSize(1);
    gfx2->setTextColor(BLACK);
    gfx2->setCursor(102, 3);
    gfx2->print("DR");
    gfx2->setCursor(103, 3);      // 1 px double-strike = faux bold
    gfx2->print("DR");
    gfx2->setTextColor(WHITE);
  }
  if (connectEnabled) {
    uint16_t cloudColor = (WiFi.status() == WL_CONNECTED) ? 0x879F : DARKGREY;
    gfx2->fillCircle(127, 8, 3, cloudColor);
    gfx2->fillCircle(132, 7, 4, cloudColor);
    gfx2->fillCircle(136, 8, 3, cloudColor);
    gfx2->fillRect(126, 8, 12, 3, cloudColor);
  }
  uint16_t c = (WiFi.status() == WL_CONNECTED) ? GREEN : DARKGREY;
  gfx2->fillRect(148, 8, 2, 3, c);   // short bar
  gfx2->fillRect(151, 5, 2, 6, c);   // medium bar
  gfx2->fillRect(154, 2, 2, 9, c);   // tall bar (ends 2 px from the edge)
}

// Boot-animation install: the printer pulls a TMB1 file from a trusted URL and
// stores it in the /bootanim library, then makes it the active boot animation.
// The URL is allowlisted to hosts we control (gh-pages default library and the
// configured Connect server) — anything else goes onto the SD card by hand.
//   POST /api/boot-anim/install   body: url=<allowlisted .tmb url>&name=<slug>
String bootAnimMetadataPath(const String &name) {
  return String(BOOTANIM_DIR) + "/" + name + ".json";
}

bool readBootAnimMetadataJson(const String &name, String &json) {
  File f = SD.open(bootAnimMetadataPath(name).c_str());
  if (!f) return false;
  json = "";
  uint32_t sz = f.size();
  json.reserve((sz < 2048 ? sz : 2047) + 1);
  while (f.available() && json.length() < 2048) json += (char)f.read();
  f.close();
  return json.length() > 0 && json.length() < 2048;
}

void appendBootAnimMetadataString(String &out, const String &json, const char *jsonKey, const char *apiKey) {
  String v;
  if (!readJsonStringField(json, jsonKey, v)) return;
  out += ",\"";
  out += apiKey;
  out += "\":\"";
  out += jsonEscape(v);
  out += "\"";
}

void appendBootAnimMetadataFields(String &out, const String &name) {
  String json;
  if (!readBootAnimMetadataJson(name, json)) return;
  appendBootAnimMetadataString(out, json, "source", "source");
  appendBootAnimMetadataString(out, json, "connect_public_id", "connectPublicId");
  appendBootAnimMetadataString(out, json, "connect_url", "connectUrl");
  appendBootAnimMetadataString(out, json, "animation_name", "animationName");
  appendBootAnimMetadataString(out, json, "version", "version");
  appendBootAnimMetadataString(out, json, "checksum_sha256", "checksumSha256");
  appendBootAnimMetadataString(out, json, "original_credits", "originalCredits");
  appendBootAnimMetadataString(out, json, "license", "license");
}

bool writeBootAnimMetadataFile(const String &slug) {
  String publicId = formString("public_id", "", 32);
  String connectUrl = formString("connect_url", "", 160);
  String version = formString("version", "", 32);
  String checksum = formString("checksum_sha256", "", 80);
  String animationName = formString("animation_name", "", 120);
  String credits = formString("original_credits", "", 160);
  String licenseName = formString("license", "", 32);
  SD.remove(bootAnimMetadataPath(slug).c_str());
  if (!publicId.length() && !connectUrl.length() && !version.length() && !checksum.length() &&
      !animationName.length() && !credits.length() && !licenseName.length()) return true;

  File f = SD.open(bootAnimMetadataPath(slug).c_str(), FILE_WRITE);
  if (!f) return false;
  f.print("{\n  \"format_version\": 1,\n  \"source\": \"connect\",\n  \"install_name\": \"");
  f.print(jsonEscape(slug));
  f.print("\"");
  if (publicId.length()) { f.print(",\n  \"connect_public_id\": \""); f.print(jsonEscape(publicId)); f.print("\""); }
  if (connectUrl.length()) { f.print(",\n  \"connect_url\": \""); f.print(jsonEscape(connectUrl)); f.print("\""); }
  if (animationName.length()) { f.print(",\n  \"animation_name\": \""); f.print(jsonEscape(animationName)); f.print("\""); }
  if (version.length()) { f.print(",\n  \"version\": \""); f.print(jsonEscape(version)); f.print("\""); }
  if (checksum.length()) { f.print(",\n  \"checksum_sha256\": \""); f.print(jsonEscape(checksum)); f.print("\""); }
  if (credits.length()) { f.print(",\n  \"original_credits\": \""); f.print(jsonEscape(credits)); f.print("\""); }
  if (licenseName.length()) { f.print(",\n  \"license\": \""); f.print(jsonEscape(licenseName)); f.print("\""); }
  f.print("\n}\n");
  f.close();
  return true;
}

void handleApiBootAnimInstall() {
  if (rejectIfWebControlOff()) return;                       // 403 when web control off
  if (printerBusy())   { sendApiError(409, "printer busy"); return; }
  if (!sdCardReady())  { sendApiError(503, "sd card unavailable"); return; }
  if (WiFi.status() != WL_CONNECTED) { sendApiError(503, "wifi not connected"); return; }

  String url = server.arg("url");
  url.trim();
  if (!(url.startsWith("http://") || url.startsWith("https://"))) {
    sendApiError(400, "missing or invalid url");
    return;
  }
  // SSRF guard: only pull from hosts we control. Both are ours, so following
  // redirects stays safe; other sources install by copying onto the SD card.
  if (!(url.startsWith("https://slibbinas.github.io/") ||
        (connectBaseUrl.length() > 0 &&
         url.startsWith(connectNormalizeBaseUrl(connectBaseUrl) + "/")))) {
    sendApiError(403, "url host not allowed");
    return;
  }

  String slug = sanitizeAnimName(server.arg("name"));

  netProgressStart("Boot animation:", "downloading");

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok;
  if (url.startsWith("https://")) { secure.setInsecure(); ok = http.begin(secure, url); }
  else                            { ok = http.begin(plain, url); }
  if (!ok) { sendApiError(502, "could not start download"); netMessage("Boot animation", "download failed"); delay(1200); screen1(); return; }

  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (connectPublishToken.length() > 0 &&
      url.startsWith(connectNormalizeBaseUrl(connectBaseUrl) + "/")) {
    http.addHeader("X-TinyMaker-Token", connectPublishToken);
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    sendApiError(502, (String("download HTTP ") + code).c_str());
    netMessage("Boot animation", "download failed");
    delay(1200); screen1();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  int remaining = http.getSize();  // -1 when the length is unknown (chunked)

  // Read the first chunk and verify the TMB1 magic BEFORE we open the target file,
  // so a bad download can't clobber an existing animation of the same name.
  uint8_t buf[1024];
  uint32_t waitStart = millis();
  int first = 0;
  // Accumulate until we have the full 12-byte header (or time out): a slow server
  // can deliver the magic across several small segments, and breaking after the
  // first read would false-reject a valid TMB1 file.
  while (first < 12 && millis() - waitStart < 12000) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf + first, avail > sizeof(buf) - first ? sizeof(buf) - first : avail);
      if (n > 0) first += n;
    } else {
      delay(2);
    }
  }
  if (first < 12 || buf[0] != 'T' || buf[1] != 'M' || buf[2] != 'B' || buf[3] != '1') {
    http.end();
    sendApiError(422, "not a TMB1 animation");
    netMessage("Boot animation", "invalid file");
    delay(1200); screen1();
    return;
  }

  // A TMB1 file states its own size, so we know what a complete download weighs
  // before a byte of it lands on the card - and unlike Content-Length, the
  // header is there even when the server sends no length at all. The dimension
  // bounds are the player's own (playTmbByName): outside them the file could
  // never be drawn anyway, and they keep the size below from overflowing.
  uint16_t animW = buf[4] | (buf[5] << 8);
  uint16_t animH = buf[6] | (buf[7] << 8);
  uint16_t animN = buf[8] | (buf[9] << 8);
  if (animW == 0 || animW > 160 || animH == 0 || animH > 80 || animN == 0) {
    http.end();
    sendApiError(422, "not a TMB1 animation");
    netMessage("Boot animation", "invalid file");
    delay(1200); screen1();
    return;
  }
  const size_t expectedBytes = 12 + (size_t)animW * animH * 2 * animN;

  SD.mkdir(BOOTANIM_DIR);
  String savePath = String(BOOTANIM_DIR) + "/" + slug + ".tmb";
  SD.remove(savePath.c_str());
  File out = SD.open(savePath.c_str(), FILE_WRITE);
  if (!out) { http.end(); sendApiError(500, "sd write failed"); netMessage("Boot animation", "SD write failed"); delay(1200); screen1(); return; }

  const size_t MAX_ANIM_BYTES = 8UL * 1024 * 1024;   // reject runaway/chunked downloads
  bool tooBig = false;
  size_t total = 0;
  out.write(buf, first);
  total += first;
  if (remaining > 0) remaining -= first;

  // A real grow-only bar: the TMB header just told us the exact payload size
  // (expectedBytes), which beats both Content-Length and the old time-driven
  // wrapping bar - that one cleared and redrew the strip every 120 ms during SD
  // writes (flicker + SPI streak risk). Redraw by bytes, like the other paths.
  size_t shownBytes = 0;
  while (http.connected() && remaining != 0) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n <= 0) break;
      out.write(buf, n);
      total += n;
      if (total > MAX_ANIM_BYTES) { tooBig = true; break; }
      if (remaining > 0) { remaining -= n; if (remaining == 0) break; }
      if (total - shownBytes >= 131072) {    // every 128 KB (anims are ~0.5-2 MB)
        shownBytes = total;
        netProgressBar(total, expectedBytes);
      }
    } else {
      if (!http.connected()) break;
      delay(2);
    }
  }
  out.close();
  http.end();

  if (tooBig) {
    SD.remove(savePath.c_str());          // don't leave a giant partial file eating the card
    sendApiError(413, "animation too large");
    netMessage("Boot animation", "file too large");
    delay(1200); screen1();
    return;
  }

  // A dropped or stalled connection just ends the loop above, and until now
  // nothing downstream noticed: the partial file was kept, metadata written and
  // ok:true returned, so the printer announced a successful install of a file
  // that stops halfway through playback. Reported from the field on a slow link
  // - a 1.4 MB animation arrived as 538 KB and still looked installed.
  if (total != expectedBytes) {
    SD.remove(savePath.c_str());
    String err = "download incomplete - " + String((unsigned long)total) +
                 " of " + String((unsigned long)expectedBytes) + " bytes";
    sendApiError(502, err.c_str());
    netMessage("Boot animation", "download incomplete");
    delay(1200); screen1();
    return;
  }

  // Install only downloads: the active choice stays whatever it was, picking
  // is an explicit act (dashboard pick + Save config, or the printer menu).
  // Auto-selecting here silently overrode the user's choice on every install.
  writeBootAnimMetadataFile(slug);
  sdRev++;  // 0-28
  sendApiOk("\"bytes\":" + String((unsigned long)total) + ",\"name\":\"" + jsonEscape(slug) + "\"");
  netMessage("Boot animation:", bootAnimDisplay(slug).c_str());
  delay(1200);
  screen1();
}

// GET /api/boot-anim - list installed animations + which one is active.
void handleApiBootAnimList() {
  if (printerBusy())  { sendApiError(409, "printer busy"); return; }
  if (!sdCardReady()) { sendApiError(503, "sd card unavailable"); return; }
  String names[24];
  int n = listBootAnims(names, 24);
  String selected = bootAnimName;
  if (bootAnimShuffleSelected(selected) && n < 2) selected = "";
  String out = "{\"ok\":true,\"selected\":\"" + jsonEscape(selected) + "\",\"animations\":[";
  for (int i = 0; i < n; i++) {
    if (i) out += ",";
    String path = String(BOOTANIM_DIR) + "/" + names[i] + ".tmb";
    File f = SD.open(path.c_str());
    long sz = f ? (long)f.size() : 0;
    if (f) f.close();
    out += "{\"name\":\"" + jsonEscape(names[i]) + "\",\"display\":\"" +
           jsonEscape(bootAnimDisplay(names[i])) + "\",\"sizeBytes\":" + String(sz);
    appendBootAnimMetadataFields(out, names[i]);
    out += "}";
  }
  out += "]}";
  server.send(200, "application/json", out);
}

// GET /api/boot-anim/file?name=<slug> - stream an installed TMB1 animation for
// browser preview. Read-only, but still blocked while printing because SD is busy.
void handleApiBootAnimFile() {
  if (printerBusy())  { sendApiError(409, "printer busy"); return; }
  if (!sdCardReady()) { sendApiError(503, "sd card unavailable"); return; }
  String name = sanitizeAnimName(server.arg("name"));
  if (name.length() == 0 || !bootAnimExists(name)) { sendApiError(404, "animation not found"); return; }

  String path = String(BOOTANIM_DIR) + "/" + name + ".tmb";
  File f = SD.open(path.c_str());
  if (!f) { sendApiError(404, "animation not found"); return; }

  server.sendHeader("Cache-Control", "max-age=86400");
  server.setContentLength(f.size());
  server.send(200, "application/octet-stream", "");
  uint8_t buf[512];
  int n;
  WiFiClient client = server.client();
  while ((n = f.read(buf, sizeof(buf))) > 0) client.write(buf, n);
  f.close();
}

// POST /api/boot-anim/select  body: name=<slug|empty>  ("" = built-in Default)
void handleApiBootAnimSelect() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) { sendApiError(409, "printer busy"); return; }
  String name = server.arg("name");
  name.trim();
  if (name.length() > 0 && !bootAnimShuffleSelected(name)) name = sanitizeAnimName(name);
  if (bootAnimShuffleSelected(name)) {
    String names[2];
    if (listBootAnims(names, 2) < 2) { sendApiError(409, "shuffle needs at least two animations"); return; }
  }
  if (name.length() > 0 && !bootAnimShuffleSelected(name) && !bootAnimExists(name)) { sendApiError(404, "animation not found"); return; }
  bootAnimName = name;
  saveDeviceConfig();
  sendApiOk("\"selected\":\"" + jsonEscape(bootAnimName) + "\"");
}

// POST /api/boot-anim/delete  body: name=<slug>
void handleApiBootAnimDelete() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy())  { sendApiError(409, "printer busy"); return; }
  if (!sdCardReady()) { sendApiError(503, "sd card unavailable"); return; }
  String name = server.arg("name");
  name.trim();
  name = sanitizeAnimName(name);
  if (name.length() == 0 || !bootAnimExists(name)) { sendApiError(404, "animation not found"); return; }
  String path = String(BOOTANIM_DIR) + "/" + name + ".tmb";
  SD.remove(path.c_str());
  SD.remove(bootAnimMetadataPath(name).c_str());
  String names[2];
  if (bootAnimName == name || (bootAnimShuffleSelected(bootAnimName) && listBootAnims(names, 2) < 2)) {
    bootAnimName = "";
    saveDeviceConfig();
  }  // fall back to Default
  sdRev++;  // 0-28
  sendApiOk("");
}

// POST /api/boot-anim/preview  body: name=<slug> - plays the animation on the
// printer's screen right away (idle only, same frame budget as the boot play).
// The HTTP reply goes out BEFORE the playback: a full 10 s animation would
// otherwise time the browser call out while the loop is busy drawing.
void handleApiBootAnimPreview() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy())  { sendApiError(409, "printer busy"); return; }
  if (!sdCardReady()) { sendApiError(503, "sd card unavailable"); return; }
  String name = server.arg("name");
  name.trim();
  if (name.length() == 0 || !bootAnimExists(name)) { sendApiError(404, "animation not found"); return; }
  sendApiOk("");
  uiWakeScreen();               // display may be blanked by the UI timeout
  playTmbByName(name);
  screen1();
}

// PWA icon bytes live in PwaIcon.ino, which the Arduino builder concatenates
// AFTER this file - forward-declare them for the /pwa-icon-192.png route.
extern const uint8_t PWA_ICON_192[];
extern const size_t PWA_ICON_192_LEN;

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
    netWifiBarsStart("WiFi connecting...");
    const int steps = 60; // 60 x 250 ms = 15 s
    for (int i = 0; i < steps && WiFi.status() != WL_CONNECTED; i++) {
      netWifiBarsPhase(1 + (i % 4), false);
      delay(250);
    }
  } else {
    // No credentials yet (first boot / after Reset WiFi): captive portal
    // in NON-blocking mode so we can draw the bar filling toward the
    // 120 s timeout while wm.process() serves the portal.
    // Branded to match the dashboard: dark theme + orange accent + project
    // logo/version and reference links (the phone is offline here, so the
    // URLs are for later - jot-down info, not navigation).
    wm.setDarkMode(true);
    wm.setCustomHeadElement(
      "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
      "<rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/>"
      "<rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/>"
      "<rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/>"
      "<path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/>"
    // Green P badge: tells the PRINTER's pinned tab apart from the landing
    // page and the manual, which share the same base logo.
    "<circle cx='47' cy='46' r='16' fill='%232fbf4f'/>"
    "<text x='47' y='53' font-family='Arial' font-size='20' font-weight='bold' fill='white' text-anchor='middle'>P</text></svg>\">"
      "<style>button{background:#e8720c;border:0}button:hover,button:focus{background:#c95f06}"
      "a,a:visited{color:#4da3ff}</style>");
    wm.setCustomMenuHTML(
      "<div style='text-align:center;margin:6px 0 16px'>"
      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64' width='56' height='56'>"
      "<rect x='8' y='40' width='48' height='9' rx='3' fill='#e8720c'/>"
      "<rect x='14' y='27' width='36' height='9' rx='3' fill='#e8720c' opacity='.75'/>"
      "<rect x='20' y='14' width='24' height='9' rx='3' fill='#e8720c' opacity='.5'/>"
      "<path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='#4da3ff' stroke-width='5' stroke-linecap='round'/></svg>"
      "<div style='font-size:15px;font-weight:600;margin-top:2px'>TinyMakerWifi"
#ifdef FIRMWARE_VERSION
      " v" FIRMWARE_VERSION
#endif
      "</div>"
      "<div style='font-size:12px;color:#888;line-height:1.6;margin-top:6px'>"
      "Pick your home WiFi below - the printer connects and shows its address.<br>"
      "Manual: slibbinas.github.io/TinyMakerWifi/manual<br>"
      "Project: tinymakerwifi.com &middot; Printer: tinymaker3d.com</div></div>");
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

  // Modem sleep (the default) delays the first request after an idle spell by
  // seconds and drops the multicast packets mDNS lives on - tinymaker.local
  // would die minutes after boot. Mains-powered device: keep the radio awake.
  WiFi.setSleep(false);

  // Rejoin on our own when the AP drops us. The core's background reconnect
  // runs in the WiFi task (not the print loop, so it never touches a curing
  // layer), and persistent(true) keeps the stored credentials so a drop never
  // sends the user back to WiFi setup (reported: connection drops mid-use, only
  // recovered by re-entering the password). The idle watchdog in network_loop()
  // is the backup nudge + mDNS re-announce.
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

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
  // PWA: manifest + home-screen icon (bytes in PwaIcon.ino, declared below)
  server.on("/manifest.json", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send(200, "application/manifest+json", PSTR(
      "{\"name\":\"TinyMaker\",\"short_name\":\"TinyMaker\",\"start_url\":\"/\",\"scope\":\"/\","
      "\"display\":\"standalone\",\"background_color\":\"#1c1c1e\",\"theme_color\":\"#1c1c1e\","
      "\"icons\":[{\"src\":\"/pwa-icon-192.png\",\"sizes\":\"192x192\",\"type\":\"image/png\",\"purpose\":\"any\"}]}"));
  });
  server.on("/pwa-icon-192.png", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "image/png", (const char *)PWA_ICON_192, PWA_ICON_192_LEN);
  });
  server.on("/api/files/model/metadata", HTTP_POST, handleApiFileModelMetadata);
  server.on("/api/files/model/preview", HTTP_GET, handleApiFileModelPreview);
  server.on("/api/files/model/preview", HTTP_POST, finishPreviewUpload, handlePreviewUploadData);
  server.on("/api/files/layer", HTTP_GET, handleApiFileLayer);
  server.on("/api/files/delete", HTTP_POST, handleApiFileDelete);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigSave);
  server.on("/api/config/defaults", HTTP_POST, handleApiConfigDefaults);
  server.on("/api/config/mqtt/defaults", HTTP_POST, handleApiConfigMqttDefaults);
  server.on("/api/config/connect/defaults", HTTP_POST, handleApiConfigConnectDefaults);
  server.on("/api/config/backup", HTTP_GET, handleApiConfigBackupGet);
  server.on("/api/config/backup/sd", HTTP_POST, handleApiConfigBackupSd);
  server.on("/api/config/restore", HTTP_POST, handleApiConfigRestore);
  server.on("/api/config/restore/sd", HTTP_POST, handleApiConfigRestoreSd);
  server.on("/api/config/dry-run", HTTP_POST, handleApiConfigDryRun);
  server.on("/api/connect/test", HTTP_POST, handleApiConnectTest);
  server.on("/api/connect/register", HTTP_POST, handleApiConnectRegister);
  server.on("/api/connect/recovery-code", HTTP_GET, handleApiConnectRecoveryCode);
  server.on("/api/connect/backup", HTTP_GET, handleApiConnectBackup);
  server.on("/api/connect/backup", HTTP_POST, handleApiConnectBackup);
  server.on("/api/connect/restore", HTTP_POST, handleApiConnectRestore);
  server.on("/api/telegram/test", HTTP_POST, handleApiTelegramTest);
  server.on("/api/whatsapp/test", HTTP_POST, handleApiWhatsAppTest);
  server.on("/api/discord/test", HTTP_POST, handleApiDiscordTest);
  server.on("/api/print/start", HTTP_POST, handleApiPrintStart);
  server.on("/api/vat/refilled", HTTP_POST, handleApiVatRefilled);
  server.on("/api/update", HTTP_GET, handleApiUpdateGet);
  server.on("/api/update/install", HTTP_POST, handleApiUpdateInstall);
  server.on("/api/print/pause", HTTP_POST, handleApiPrintPause);
  server.on("/api/print/resume", HTTP_POST, handleApiPrintResume);
  server.on("/api/print/stop", HTTP_POST, handleApiPrintStop);
  server.on("/api/boot-anim", HTTP_GET, handleApiBootAnimList);
  server.on("/api/boot-anim/file", HTTP_GET, handleApiBootAnimFile);
  server.on("/api/boot-anim/select", HTTP_POST, handleApiBootAnimSelect);
  server.on("/api/boot-anim/delete", HTTP_POST, handleApiBootAnimDelete);
  server.on("/api/boot-anim/preview", HTTP_POST, handleApiBootAnimPreview);
  server.on("/api/boot-anim/install", HTTP_POST, handleApiBootAnimInstall);
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

  // Background SNTP sync (UTC, non-blocking). The dashboard computes the
  // print-ETA in the browser's own timezone, so nothing here depends on
  // this succeeding - it seeds on-device time for future features.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (saved) {
    // Routine boot: the bars just turn green - the separate "WiFi connected"
    // screen (and its 1.5 s of delays) is gone, the main screen's badge
    // carries the state from here.
    netWifiBarsPhase(4, true);
    delay(400);
  } else {
    // First-time setup via the portal: the portal page promises the printer
    // "shows its address", and this is the one moment the user needs it.
    String ip = "IP: " + WiFi.localIP().toString();
    netMessage("WiFi connected", ip.c_str());
    delay(1800);
  }
  statsPingMaybe();
  otaBootCheckMaybePrompt();
  if (screen == 424) return;
}

// ===================================================================================
// Loop hook - call as the FIRST line of loop()
// Wrapper so the main .ino never touches the 'server' global directly:
// globals defined in later .ino tabs are NOT visible earlier in the
// combined compilation unit (functions are - prototypes are auto-generated).
// ===================================================================================
// HTTP only - safe inside the exposure wait loop: full network_loop() also
// runs MQTT and the Connect sync, whose timeouts can hold the caller for
// seconds, and during an exposure that caller is also the button poll.
void network_service_http() {
  if (!networkRuntimeEnabled()) return;
  server.handleClient();
}

// Deferred-SD-job HTTP servicing (1-32/1-33): called from the delete/unpack
// inner loops so other browsers keep getting status answers during a long SD
// job. Gated on sdJobRunning - the flag is set only in loop()-context callers
// (sdJobRun, the printer's own menu delete/import), never inside an HTTP
// handler, where nested handleClient would corrupt the pending response.
// While it runs, printerBusy() is true (sdJobKind set), so every SD-touching
// endpoint answers its usual 409 instead of racing the job.
void sdJobService() {
  if (!sdJobRunning) return;
  static unsigned long svcAt = 0;
  if (millis() - svcAt < 200) return;
  svcAt = millis();
  network_service_http();
}

// Executes a queued web delete/import. Called ONLY from the idle loop() -
// never from service windows mid-print or the motor/pause loops - so a job
// can't interleave with printing or manual moves. The job still blocks the
// printer UI like the old in-handler version did (by design - the SD must
// not be shared), but HTTP stays answered via sdJobService above.
void sdJobRun() {
  if (sdJobKind.length() == 0 || sdJobRunning) return;
  uiWakeScreen();   // web-started work must be visible on the printer
  sdJobRunning = true;
  if (sdJobKind == "delete") {
    String path = "/" + sdJobName;
    // Same screen the printer-menu delete shows - without it the LCD looked
    // frozen to anyone at the printer (delete blocks the UI by design).
    netProgressStart("Deleting:", sdJobName.c_str());
    bool ok = deleteModelFolder(path.c_str(), true);
    if (ok) sdRev++;  // 0-28
    DBG("SD job delete %s: %s\n", sdJobName.c_str(), ok ? "ok" : "FAILED");
  } else if (sdJobKind == "import") {
    ModelImportResult result;
    String error;
    bool ok = importZipModel(sdJobZipPath.c_str(), sdJobName, sdJobImportOptions,
                             result, error);
    SD.remove(sdJobZipPath.c_str());  // free SD space, keep browser list clean
    if (ok) {
      sdRev++;  // 0-28: every dashboard refreshes its list and names the model
      netMessage("Model ready:", result.finalName.c_str());
    } else {
      DBGLN("SD job import FAILED");
      netMessage("Import FAILED", sdJobName.c_str());
    }
    delay(1500);
  }
  sdJobRunning = false;
  sdJobKind = ""; sdJobName = ""; sdJobZipPath = "";
  screen1();   // the progress screen overwrote whatever was shown
}

void network_loop() {
  if (!networkRuntimeEnabled()) return;
  server.handleClient();   // dashboard stays viewable with Web control off
                           // (actions are 403'd - see rejectIfWebControlOff)
  // Dev espota OTA is answered only while the printer is on the Update screen
  // (same safety gate as the web /update flasher).
  if (otaMenuOpen()) ArduinoOTA.handle();
  mqtt_loop();
  tinymakerConnectLoop();

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

  // WiFi reconnect watchdog. Lives ONLY here in network_loop(), which the
  // exposure path never calls (turn_on_LED services network_service_http()
  // only), so it can never touch a curing layer. setAutoReconnect() already
  // rejoins in the background; this is the belt-and-suspenders nudge for when
  // the core gives up, and it re-announces mDNS - tinymaker.local dies across a
  // reconnect - once the link is back. Non-blocking: reconnect() just kicks the
  // WiFi task. Dormant during a print (network_loop isn't reached then); the
  // background auto-reconnect covers that window.
  static unsigned long wifiWatchTs = 0;
  static bool wifiWasDown = false;
  if (millis() - wifiWatchTs > 15000) {
    wifiWatchTs = millis();
    if (WiFi.status() != WL_CONNECTED) {
      wifiWasDown = true;
      WiFi.reconnect();
    } else if (wifiWasDown) {
      wifiWasDown = false;
      MDNS.end();
      MDNS.begin("tinymaker");
    }
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
    // No strength bars here: the dBm + word already say it, and the bars
    // collided with the "(Good)" text (user finding, 0.14.3). The main-menu
    // badge keeps its bars.
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
