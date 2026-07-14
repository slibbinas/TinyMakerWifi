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
    // Same style as WiFi/delete/OTA: title + a sweeping progress bar.
    // Multipart uploads don't announce total size, so the bar animates by
    // wrapping every ~1 MB while showing the running KB count.
    netProgressStart("Receiving model:", modelName.c_str());
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadRejected) return;
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
    if (up.totalSize - otaShownBytes >= 262144) { // redraw every 256 KB - drawing while flash/SD writes run corrupts SPI pixels (orange streaks, user finding)
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
  String error = "unpack failed";
  ModelImportResult result;
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

    ok = importZipModel(uploadPath.c_str(), modelName, options, result, error);
    SD.remove(uploadPath.c_str()); // free SD space, keep browser list clean
  }
  if (ok) {
    String out = "{\"ok\":true,\"done\":true,\"name\":\"";
    out += jsonEscape(result.finalName);
    out += "\",\"renamed\":";
    out += result.renamed ? "true" : "false";
    out += ",\"sourceLayers\":";
    out += String(result.summary.sourceLayers);
    out += ",\"layers\":";
    out += String(result.summary.sourceLayers);
    out += ",\"printLayers\":";
    out += String(result.summary.printLayers);
    out += ",\"heightMm\":";
    out += String(result.summary.heightMm, 2);
    out += "}";
    server.send(201, "application/json", out);
    netMessage("Model ready:", result.finalName.c_str());
    delay(1500);
  } else {
    DBGLN("Unpack FAILED");
    server.send(500, "application/json", "{\"error\":\"" + jsonEscape(error) + "\"}");
    netMessage("Upload FAILED", modelName.c_str());
    delay(1500);
  }
  // Redraw UI - upload messages overwrote whatever screen was shown.
  // Returning to the main menu keeps the 'screen' state machine consistent;
  // the new model appears in Print menu (folder list is re-read on entry).
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

void handleApiFileModelPreview() {
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
    netMessage("Firmware update", "Receiving...");
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
  out += "\",\"firmwareBuild\":\"";
#ifdef GIT_REV
  out += jsonEscape(GIT_REV);
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
  out += "\",\"lifetimePrintSecs\":";
  out += String(totalPrintSecs);
  out += ",\"lifetimePrintTime\":\"";
  out += formatDuration(totalPrintSecs);
  out += "\",\"uvLedSecs\":";
  out += String(currentUvLedSecs());
  out += ",\"uvLedTime\":\"";
  out += formatDuration(currentUvLedSecs());
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

// Include GIT_REV when available: experimental builds often share one SemVer,
// and a version-only tag can make the browser keep stale dashboard HTML.
String pageEtag() {
  String etag = "\"";
#ifdef FIRMWARE_VERSION
  etag += FIRMWARE_VERSION;
#else
  etag += "dev";
#endif
#ifdef GIT_REV
  etag += "-";
  etag += GIT_REV;
#endif
  etag += "\"";
  return etag;
}

void sendRootStyledPage(PGM_P bodyBeforeFw, const char *fw, PGM_P bodyAfterFw) {
  String etag = pageEtag();
  // The page only changes with the firmware: let the browser cache it and
  // revalidate with a tiny 304. Crucial mid-print, when the network is only
  // serviced in short windows and a ~70 KB page reload would crawl.
  server.sendHeader("ETag", etag);
  server.sendHeader("Cache-Control", "no-cache");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(PSTR(
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TinyMaker</title>"
    "<link rel='icon' href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
    "<rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/>"
    "<rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/>"
    "<rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/>"
    "<path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/>"
    // Green P badge: tells the PRINTER's pinned tab apart from the landing
    // page and the manual, which share the same base logo.
    "<circle cx='47' cy='46' r='16' fill='%232fbf4f'/>"
    "<text x='47' y='53' font-family='Arial' font-size='20' font-weight='bold' fill='white' text-anchor='middle'>P</text></svg>\">"
    // Theme boot: apply the saved choice to <html> BEFORE the stylesheet
    // parses, so a light-theme reload never flashes dark (manual does the same).
    "<script>(function(){try{if(localStorage.getItem('tmTheme')==='light')document.documentElement.setAttribute('data-theme','light')}catch(e){}})()</script>"
    "<style>"
    // All colors live in CSS variables; [data-theme=light] swaps the palette
    // (same accent orange). Keep new rules on variables, not literals.
    ":root{color-scheme:dark;--bg:#1c1c1e;--wrap:#232326;--card:#2a2a2e;--tile:#202024;--pv:#151517;"
    "--line:#3a3a3f;--line2:#555;--line3:#444;--text:#eee;--muted:#aaa;--muted2:#8a8a92;"
    "--accent:#e8720c;--link:#84bcf8;--btnsec:#3c3c42;--btnsec-t:#eee;--danger:#7b2f2f;"
    "--dis:#555;--dis-t:#aaa;--overlay:rgba(20,20,22,.93);--dim:rgba(20,20,22,.6);"
    "--toast:#2e2e33;--toast-t:#fff;--warncol:#ffb15f;--banner:#3a2818;--warnbg:#3a2320;"
    "--subh:#98938a;--fwb:#777;--wbar:#4a4a50}"
    "[data-theme=light]{color-scheme:light;--bg:#eceef1;--wrap:#f8f9fa;--card:#fff;--tile:#f4f5f7;--pv:#e8eaee;"
    "--line:#d9dbe0;--line2:#c2c6cd;--line3:#d0d3d8;--text:#1f2124;--muted:#5f6570;--muted2:#6a707a;"
    "--link:#155fb0;--btnsec:#dcdfe4;--btnsec-t:#26282c;--danger:#b23434;"
    "--dis:#c9ccd2;--dis-t:#82878f;--overlay:rgba(243,244,246,.93);--dim:rgba(100,104,112,.45);"
    "--toast:#fff;--toast-t:#1f2124;--warncol:#9a5b00;--banner:#fff1e0;--warnbg:#fdecec;"
    "--subh:#7a736a;--fwb:#9aa0a8;--wbar:#c8ccd2}"
    "*{box-sizing:border-box}"
    "body{margin:0;min-height:100vh;background:var(--bg);font-family:-apple-system,Segoe UI,Roboto,sans-serif;color:var(--text)}"
    // Orange window frame matching the printer's on-screen UI
    ".wrap{max-width:560px;margin:16px auto;padding:20px 18px;border:2px solid var(--accent);border-radius:14px;background:var(--wrap)}"
    ".head{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:18px}"
    "h1{margin:0;font-size:24px;color:var(--accent)}h2{font-size:17px;margin:0 0 12px;color:var(--text)}.fw{font-size:13px;color:var(--muted)}"
    "#themeBtn,#gsBtn{color:var(--muted);font-size:16px;text-decoration:none;margin-left:6px}#themeBtn:hover,#gsBtn:hover{color:var(--text);text-decoration:none}"
    // Getting-started rows + the little round contextual-help marks
    ".gsRow{display:flex;gap:10px;border-top:1px solid var(--line);padding:9px 0;align-items:flex-start}.gsRow:first-child{border-top:0;padding-top:0}"
    ".gsMark{cursor:pointer;color:var(--muted);font-size:16px;width:22px;flex:0 0 auto;text-align:center;line-height:1.3}.gsMark.on{color:#2fbf4f;cursor:default}"
    ".qHelp{display:inline-block;margin-left:6px;width:16px;height:16px;line-height:15px;text-align:center;border:1px solid var(--line2);border-radius:50%;font-size:11px;color:var(--muted)}.qHelp:hover{color:var(--text);text-decoration:none}"
    ".card{background:var(--card);border:1px solid var(--line3);border-radius:10px;padding:18px;margin:12px 0}"
    ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}"
    ".label{font-size:12px;color:var(--muted)}.value{font-size:16px;margin-top:4px}"
    ".files{display:grid;gap:8px}.file{display:flex;align-items:center;justify-content:space-between;gap:10px;"
    "border-top:1px solid var(--line);padding-top:10px}.file:first-child{border-top:0;padding-top:0}"
    ".rowActions{display:flex;gap:8px;align-items:center}"
    ".meta{font-size:12px;color:var(--muted2);margin-top:3px}"
    // Connect sub-tabs (markup comes from Brian's hosted app, style is ours):
    // same underline language as the Settings section tabs - one 2nd-level
    // navigation look across the product.
    ".connectTabs{display:flex;gap:22px;border-bottom:1px solid var(--line);margin-top:14px;overflow-x:auto}"
    ".connectTabs button{width:auto;flex:0 0 auto;margin:0;padding:7px 2px 9px;background:none;border:0;border-bottom:2px solid transparent;border-radius:0;color:var(--muted);font-size:13.5px;font-weight:600;cursor:pointer}"
    ".connectTabs button.active{background:none;color:var(--text);border-bottom-color:var(--accent)}"
    ".connectTabs button:hover{background:none;color:var(--text)}"
    "a{color:var(--link);text-decoration:none}a:hover{text-decoration:underline}a:visited{color:var(--link)}"
    "input[type=file],input[type=number],input[type=text],input[type=password],select{width:100%;margin:6px 0 12px;padding:10px;border:1px solid var(--line2);border-radius:8px;background:var(--bg);color:var(--text)}"
    "label span{display:block;font-size:13px;color:var(--muted)}.configGrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px 12px}"
    ".spanAll{grid-column:1/-1}"
    ".check{display:flex;align-items:center;gap:8px;margin:6px 0 12px}.check input{width:auto}.check span{display:inline;color:var(--text)}"
    ".actions{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:14px}"
    // Tabs: equal-size Dashboard/Settings/Update, active one orange
    ".toolbar{display:flex;gap:8px;margin:12px 0}.toolbar button,.toolbar .button{width:auto;flex:1;margin-top:0;background:var(--btnsec);color:var(--btnsec-t)}"
    ".toolbar .active{background:var(--accent);color:#fff}"
    // Mini WiFi signal bars (same idea as the printer's badge)
    ".wbars{display:inline-flex;gap:2px;align-items:flex-end;margin-left:8px;vertical-align:middle}"
    ".wbars i{width:4px;background:var(--wbar);border-radius:1px}.wbars i:nth-child(1){height:5px}"
    ".wbars i:nth-child(2){height:9px}.wbars i:nth-child(3){height:13px}.wbars i.on{background:#2fbf4f}"
    ".banner{background:var(--banner);border-color:var(--accent)}.banner strong{display:block;color:var(--warncol);margin-bottom:4px}"
    ".progress{height:10px;border:1px solid var(--line2);border-radius:999px;overflow:hidden;background:var(--bg);margin:10px 0 16px}"
    ".progress span{display:block;height:100%;width:45%;background:var(--accent);animation:barMove 1.1s infinite linear}"
    ".storageBar{height:10px;border:1px solid var(--line2);border-radius:999px;overflow:hidden;background:var(--bg);margin-top:8px}"
    ".storageBar span{display:block;height:100%;width:0;background:var(--accent);transition:width .2s ease}"
    "@keyframes barMove{0%{transform:translateX(-110%)}100%{transform:translateX(230%)}}"
    // Every standalone button gets a top gap so it never sticks to the content
    // above it (primary buttons used to lack this - secondary had it inline).
    // Buttons laid out in rows/grids zero it below; those containers own spacing.
    "button,.button{display:inline-block;width:100%;border:0;border-radius:8px;background:var(--accent);color:#fff;padding:12px 14px;margin-top:10px;"
    "font-size:15px;font-weight:600;text-align:center;text-decoration:none;cursor:pointer}"
    ".small,.delete{width:auto;padding:9px 11px;font-size:13px}.delete{background:var(--danger);color:#fff}.secondaryBtn{background:var(--btnsec);color:var(--btnsec-t)}"
    "button:disabled{background:var(--dis);color:var(--dis-t);cursor:not-allowed}"
    ".button.secondary{background:var(--btnsec);color:var(--btnsec-t)}"
    ".button.danger{background:var(--danger);color:#fff}"
    ".grid button,.grid .button,.actions button,.actions .button,.connectActions button,.connectActions .button,.rowActions button,.rowActions .button{margin-top:0}"
    // Full-page lock while a firmware update is in flight; cleared by the
    // automatic reload once the printer answers status polls again.
    ".updOverlay{position:fixed;inset:0;z-index:99;background:var(--overlay);display:none;flex-direction:column;align-items:center;justify-content:center;gap:12px;text-align:center;padding:24px}"
    ".updOverlay.on{display:flex}.updOverlay h2{color:var(--accent)}"
    ".modal{position:fixed;inset:0;z-index:80;background:var(--dim);display:flex;align-items:center;justify-content:center;padding:20px}"
    ".modal.hidden{display:none}"
    ".modalCard{background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:20px;max-width:420px;width:100%;box-shadow:0 12px 40px rgba(0,0,0,.55)}"
    ".modalText{color:var(--text);font-size:15px;line-height:1.5;white-space:pre-line;margin-bottom:18px}"
    // .button.secondary (0,2,0) outranks .modalBtns button (0,1,1) - without the
    // extra selector Cancel keeps its 10px margin-top and renders shorter than OK.
    ".modalBtns{display:flex;gap:10px}.modalBtns button,.modalBtns .button.secondary{margin-top:0;flex:1;width:auto;padding:12px 14px;font-size:15px}"
    ".fwbuild{color:var(--fwb);font-size:11px;font-family:monospace}.fwbuild:empty{display:none}"
    ".subhead{grid-column:1/-1;margin-top:6px;padding-top:14px;border-top:1px solid var(--line);font-size:12px;letter-spacing:.5px;text-transform:uppercase;color:var(--subh)}"
    ".pwWrap{position:relative;display:block}.pwWrap input{width:100%;padding-right:40px}"
    ".eyeBtn{position:absolute;right:4px;top:50%;transform:translateY(-50%);width:auto;background:transparent;border:0;padding:4px 8px;margin:0;font-size:16px;cursor:pointer;color:var(--muted)}.eyeBtn:hover{color:var(--text)}"
    ".updSpin{width:34px;height:34px;border:4px solid var(--btnsec);border-top-color:var(--accent);border-radius:50%;animation:uspin 1s linear infinite}@keyframes uspin{to{transform:rotate(360deg)}}"
    ".warn{color:var(--warncol)}"
    ".hidden{display:none}"
    ".hint{font-size:13px;color:var(--muted);margin:10px 0 0;line-height:1.4}"
    // Top-center: a bottom toast hid behind the action buttons and blended in.
    // Brand-orange border + slide-in: a static grey box was easy to miss.
    "#statusMsg{position:fixed;left:50%;top:14px;transform:translateX(-50%);max-width:92%;z-index:60;margin:0;background:var(--toast);color:var(--toast-t);border:2px solid var(--accent);border-radius:10px;padding:11px 18px;box-shadow:0 8px 24px rgba(0,0,0,.6);font-size:14px;font-weight:600;line-height:1.4;animation:toastIn .25s ease}"
    "@keyframes toastIn{from{opacity:0;transform:translate(-50%,-12px)}to{opacity:1;transform:translate(-50%,0)}}"
    "#statusMsg.warn{background:var(--warnbg);border-color:#d4705c;color:var(--warncol)}"
    "#statusMsg:empty{display:none}"
    ".configGrid .hint{grid-column:1/-1}"
    // Settings section tabs: 2nd navigation level uses a DIFFERENT shape
    // language than the view pills - plain text with an orange underline -
    // so the two levels never read as one (user decision, 07-14 prototype).
    ".cfgNav{display:flex;gap:22px;border-bottom:1px solid var(--line);margin:0 0 16px;overflow-x:auto}"
    ".cfgNav a{color:var(--muted);text-decoration:none;font-size:13.5px;font-weight:600;padding:7px 2px 9px;border-bottom:2px solid transparent;white-space:nowrap}"
    ".cfgNav a.on{color:var(--text);border-bottom-color:var(--accent)}"
    ".cfgNav a:hover{color:var(--text);text-decoration:none}"
    "@media(max-width:520px){.grid,.configGrid,.actions{grid-template-columns:1fr}.head{display:block}.fw{margin-top:4px}.file{align-items:flex-start;flex-direction:column}.rowActions{width:100%}.connectTabs{gap:16px}.leaderRow{grid-template-columns:32px minmax(0,1fr);gap:4px}.leaderRow .pill{width:max-content}}"
    // Desktop: widen the frame and lay the dashboard cards out in two
    // columns (status | controls, progress 3D | SD manager). The other
    // views stay a comfortable single column, centered.
    "@media(min-width:1000px){.wrap{max-width:1100px}"
    "#homeView:not(.hidden){display:grid;grid-template-columns:1fr 1fr;gap:14px;align-items:start}"
    "#gsCard{grid-column:1/-1}"
    // Left column stacks status/controls/3D; the SD manager owns the right
    // column full-height (cards used to auto-flow and overlap oddly mid-print).
    "#homeLeft{display:grid;gap:14px}"
    "#homeView .card{margin:0}"
    // Non-dashboard views used to sit in a narrow 760px band with big empty
    // margins (user finding). Connect goes full width (its tiles auto-fill);
    // the rest widen to 900 with three-column form fields, and the model
    // panel splits info | 3D preview once the preview is open.
    // Settings spans the full frame width (aligned with the view-tab row,
    // user decision); the other views stay a comfortable 900px band.
    "#updateView,#modelPanel,#dryRunBanner,#webControlBanner{max-width:900px;margin-left:auto;margin-right:auto}"
    ".configGrid{grid-template-columns:repeat(3,minmax(0,1fr))}"
    // Print settings are exactly 16 fields -> a clean 4x4 block; grids inside
    // the half-width paired cards drop back to two columns.
    ".configGrid.grid4{grid-template-columns:repeat(4,minmax(0,1fr))}"
    ".cfgPair{display:grid;grid-template-columns:1fr 1fr;gap:12px;align-items:start}.cfgPair .card{margin:12px 0 0}"
    ".cfgPair .configGrid{grid-template-columns:repeat(2,minmax(0,1fr))}"
    // Full-width backup card: the four backup/restore buttons fit one row.
    "#backupCard .actions{grid-template-columns:repeat(4,minmax(0,1fr))}"
    "#modelPanel:not(.hidden):has(#previewWrap:not(.hidden)){max-width:none;display:grid;grid-template-columns:1fr 1fr;column-gap:18px;align-items:start}"
    "#modelPanel:not(.hidden):has(#previewWrap:not(.hidden)) #modelTitle{grid-column:1/-1}"
    "#modelPanel:not(.hidden):has(#previewWrap:not(.hidden)) #previewWrap{grid-column:2;grid-row:2/span 6;margin-top:0}}"
    "</style></head><body><main class='wrap'>"));
  server.sendContent_P(bodyBeforeFw);
  server.sendContent(fw);
  server.sendContent_P(bodyAfterFw);
  server.sendContent_P(PSTR("</main></body></html>"));
}

void handleRootPage() {
  if (server.header("If-None-Match") == pageEtag()) {
    server.send(304, "text/html", "");
    return;
  }
  const char *fw =
#ifdef FIRMWARE_VERSION
    FIRMWARE_VERSION;
#else
    "unknown";
#endif
  static const char rootBodyBeforeFw[] PROGMEM = R"SPA(
<div class='head'><div><h1><svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64' width='26' height='26' style='vertical-align:-4px;margin-right:8px'><rect x='8' y='40' width='48' height='9' rx='3' fill='#e8720c'/><rect x='14' y='27' width='36' height='9' rx='3' fill='#e8720c' opacity='.75'/><rect x='20' y='14' width='24' height='9' rx='3' fill='#e8720c' opacity='.5'/><path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='#4da3ff' stroke-width='5' stroke-linecap='round'/></svg>TinyMaker</h1><div class='fw'>Firmware <span id='fwVersion'>)SPA";
  // data-build bakes the page's own build tag in, so a stale page left open in
  // a tab reloads itself as soon as a status poll reports a different build.
  static const char rootBodyAfterFw[] PROGMEM = R"SPA(</span><span id='fwBuild' class='fwbuild' data-build=")SPA"
#ifdef GIT_REV
    GIT_REV
#endif
    R"SPA("></span> · <a id='manualLink' href='https://slibbinas.github.io/TinyMakerWifi/manual/?theme=dark' target='_blank' rel='noopener'>Manual</a> · <a href='https://tinymakerwifi.com' target='_blank' rel='noopener' title='Project site'>tinymakerwifi.com</a><a href='#' id='themeBtn' title='Light / dark theme'>&#9680;</a><a href='#' id='gsBtn' title='Getting started guide'>?</a></div></div></div>

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

<div id='confirmModal' class='modal hidden'><div class='modalCard'><div id='confirmText' class='modalText'></div><div id='confirmButtons' class='modalBtns'></div></div></div>

<div id='helpModal' class='modal hidden'><div class='modalCard'><div id='helpBody' style='color:var(--text);font-size:15px;line-height:1.5;margin-bottom:18px'></div><div class='modalBtns'><button id='helpClose' type='button'>Close</button></div></div></div>

<div class='toolbar'>
  <button id='homeViewButton' type='button' class='active'>Dashboard</button>
  <button id='connectViewButton' type='button' class='hidden'>Connect</button>
  <button id='configViewButton' type='button'>Settings</button>
  <button id='updateViewButton' type='button'>Update</button>
</div>

<div id='statusMsg' class='hint'></div>

<div id='homeView'>
  <section id='gsCard' class='card hidden'>
    <h2>Getting started</h2>
    <div id='gsList'></div>
    <div class='hint'>Circles are steps to do - the printer ticks some off by itself, click the rest when done. Reopen this guide any time with the ? next to Manual.</div>
    <button id='gsHide' class='button secondary' type='button'>Hide this guide</button>
  </section>
  <div id='homeLeft'>
  <section class='card'>
    <div class='grid'>
      <div><div class='label'>State</div><div id='stateValue' class='value'>Loading</div></div>
      <div><div class='label'>WiFi</div><div class='value'><span id='wifiValue'>-</span><span id='wifiBars' class='wbars'><i></i><i></i><i></i></span></div></div>
      <div><div class='label'>IP</div><div id='ipValue' class='value'>-</div></div>
      <div><div class='label'>Lifetime print time</div><div id='lifetimeValue' class='value'>-</div></div>
      <div><div class='label'>UV LED time</div><div id='uvLedValue' class='value'>-</div></div>
      <div><div class='label'>SD card</div><div id='sdValue' class='value'>-</div></div>
      <div><div class='label'>Resin left (est.)</div><div id='vatValue' class='value'>-</div></div>
      <div id='printLayerBox' class='hidden'><div class='label'>Layer</div><div id='layerValue' class='value'>-</div></div>
      <div id='printResinBox' class='hidden'><div class='label'>Resin</div><div id='resinValue' class='value'>-</div></div>
      <div id='printRunBox' class='hidden'><div class='label'>Running time</div><div id='runValue' class='value'>-</div></div>
      <div id='printRemainingBox' class='hidden'><div class='label'>Remaining time</div><div id='remainingValue' class='value'>-</div></div>
    </div>
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
    <h2 id='printPreviewTitle'>Print progress 3D</h2>
    <canvas id='printPreviewCanvas' style='width:100%;border:1px solid var(--line);border-radius:8px;background:var(--pv)'></canvas>
    <div class='storageBar'><span id='printPreviewBarFill'></span></div>
    <div id='dashModelInfo' class='hint hidden'></div>
    <button id='dashShareButton' class='secondaryBtn hidden' type='button'>Share model</button>
  </section>
  </div>

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
  <h2 id='modelTitle'>Model</h2>
  <div class='grid'>
    <div><div class='label'>Layers</div><div id='modelLayers' class='value'>-</div></div>
    <div id='modelPrintLayersBox' class='hidden'><div class='label'>Print layers</div><div id='modelPrintLayers' class='value'>-</div></div>
    <div><div class='label'>Height</div><div id='modelHeight' class='value'>-</div></div>
    <div><div class='label'>Estimated time</div><div id='modelTime' class='value'>-</div></div>
    <div id='modelResinBox' class='hidden'><div class='label'>Resin needed</div><div id='modelResin' class='value'>-</div></div>
  </div>
  <div id='modelProgress' class='progress hidden'><span></span></div>
  <div id='modelActions' class='actions'>
    <button id='modelBackButton' class='button secondary spanAll' type='button'>Back to dashboard</button>
    <button id='modelShareButton' class='secondaryBtn spanAll hidden' type='button'>Share model</button>
    <button id='modelStartButton' class='spanAll' type='button'>Start print</button>
  </div>
  <div id='previewWrap' class='hidden' style='margin-top:12px'>
    <div id='prevSpin' class='updSpin hidden' style='margin:14px auto'></div>
    <canvas id='modelPreviewCanvas' style='width:100%;border:1px solid var(--line);border-radius:8px;background:var(--pv)'></canvas>
    <div class='hint'>Preview is built in the browser from every Nth sliced layer; the box is the printer's build volume (40.8 &times; 30.6 &times; 68 mm).</div>
  </div>
</section>

<section id='connectView' class='card hidden'>
  <div id='connectHostedRoot'>
    <h2>TinyMaker Connect</h2>
    <div id='connectHostedStatus' class='hint'>Loading Connect from the configured server...</div>
  </div>
</section>

<section id='configView' class='hidden'>
  <nav class='cfgNav' id='cfgNav'>
    <a href='#' data-pane='print' class='on'>Print</a>
    <a href='#' data-pane='network'>Network</a>
    <a href='#' data-pane='notif'>Notifications</a>
    <a href='#' data-pane='boot'>Boot animation</a>
    <a href='#' data-pane='backup'>Backup</a>
  </nav>
  <form id='configForm'>
  <div id='pane-print' class='cfgPane'>
  <div class='card'>
  <h2>Print settings</h2>
  <div class='configGrid grid4'>
    <label><span>Layer height (mm)<a href='#' class='qHelp' data-help='layer'>?</a></span><input name='layer_height' id='cfgLayerHeight' type='number' min='0.05' max='0.10' step='0.05'></label>
    <label><span>Base exposure (s)</span><input name='base_exposure' id='cfgBaseExposure' type='number' min='10' max='60' step='1'></label>
    <label><span>Regular exposure (s) <a href='#' id='undoRegExp' class='hidden'></a></span><input name='regular_exposure' id='cfgRegularExposure' type='number' min='1' max='30' step='1'></label>
    <label><span>Base layers</span><input name='base_layer' id='cfgBaseLayers' type='number' min='1' max='8' step='1'></label>
    <label><span>Transition layers</span><input name='transition_layer' id='cfgTransitionLayers' type='number' min='0' max='10' step='1'></label>
    <label><span>Slow lift distance (mm)</span><input name='slow_lift_distance' id='cfgSlowLiftDistance' type='number' min='1' max='3' step='1'></label>
    <label><span>Fast lift distance (mm)</span><input name='fast_lift_distance' id='cfgFastLiftDistance' type='number' min='1' max='3' step='1'></label>
    <label><span>Slow lift feedrate</span><input name='slow_lift_feedrate' id='cfgSlowLiftFeedrate' type='number' min='20' max='50' step='10'></label>
    <label><span>Fast lift feedrate</span><input name='fast_lift_feedrate' id='cfgFastLiftFeedrate' type='number' min='20' max='50' step='10'></label>
    <label><span>Drop back feedrate</span><input name='drop_back_feedrate' id='cfgDropBackFeedrate' type='number' min='20' max='50' step='10'></label>
    <label><span>VAT size (ml)<a href='#' class='qHelp' data-help='resin'>?</a></span><input name='vat_ml' id='cfgVatMl' type='number' min='10' max='40' step='1'></label>
    <label><span>Low resin warn (ml)</span><input name='low_resin_ml' id='cfgLowResinMl' type='number' min='1' max='3' step='1'></label>
    <label><span>UI timeout (s, 0=off)</span><input name='ui_timeout' id='cfgUiTimeout' type='number' min='0' max='3600' step='5'></label>
    <label class='check'><input name='low_resin_pause' id='cfgLowResinPause' type='checkbox' value='1'><span>Low resin pause (mid-print)</span></label>
    <label class='check'><input name='ask_refill' id='cfgAskRefill' type='checkbox' value='1'><span>Ask refill before print</span></label>
    <label class='check'><input name='dry_run' id='cfgDryRun' type='checkbox' value='1'><span>Dry run mode</span></label>
  </div>
  <button type='submit'>Save config</button>
  </div>
  </div>
  <div id='pane-network' class='cfgPane hidden'>
  <div class='card'>
  <h2>Network &amp; integrations</h2>
  <div class='configGrid'>
    <label class='check'><input name='wifi_enabled' id='cfgWifiEnabled' type='checkbox' value='1'><span>WiFi</span></label>
    <label class='check'><input name='web_dashboard_enabled' id='cfgWebDashboardEnabled' type='checkbox' value='1'><span>Web control (browser actions)<a href='#' class='qHelp' data-help='web'>?</a></span></label>
    <label class='check spanAll'><input name='boot_update_check' id='cfgBootUpdateCheck' type='checkbox' value='1'><span>Boot update check</span></label>
    <label class='check spanAll'><input name='stats_ping' id='cfgStatsPing' type='checkbox' value='1'><span>Anonymous usage ping (version + print hours, once per update)</span></label>
    <div class='subhead'>Smart home - MQTT</div>
    <label class='check spanAll'><input name='mqtt_enabled' id='cfgMqttEnabled' type='checkbox' value='1'><span>Enable MQTT</span></label>
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
    <div class='subhead'>TinyMaker Connect</div>
    <label class='check spanAll'><input name='connect_enabled' id='cfgConnectEnabled' type='checkbox' value='1'><span>Enable TinyMaker Connect</span></label>
    <div id='connectFields' class='spanAll hidden'>
      <div class='configGrid'>
        <label class='spanAll'><span>Connect server URL</span><input name='connect_base_url' id='cfgConnectBaseUrl' type='text' maxlength='128' placeholder='https://connect.tinymakerwifi.com'></label>
        <label><span>Printer display name</span><input name='connect_printer_name' id='cfgConnectPrinterName' type='text' maxlength='64' placeholder='My printer'></label>
        <label class='check'><input name='connect_leaderboard' id='cfgConnectLeaderboard' type='checkbox' value='1'><span>Share printer stats on leaderboard</span></label>
      </div>
      <div id='connectHint' class='hint'>Registering stores a printer token for publishing models, ratings and bookmarks. Leaderboard sharing is optional.</div>
      <div id='connectReclaimBox' class='hidden'>
        <div class='hint'><b>This printer has been connected before.</b><br>Insert your recovery code to access your old profile.</div>
        <label><span>Recovery code</span><input id='cfgConnectRecoveryCode' type='password' maxlength='80' autocomplete='off'></label>
        <div class='actions'>
          <button id='connectReclaimButton' class='button secondary' type='button'>Access old profile</button>
          <button id='connectNewProfileButton' class='button secondary' type='button'>Setup as a new printer</button>
        </div>
      </div>
      <div id='connectRecoveryCodeBox' class='hidden'>
        <div class='subhead'>TinyMaker Connect Recovery code</div>
        <div class='hint'>Please save this somewhere safe to restore your Connect profile if you ever reset your printer and don't have a backup.</div>
        <div class='actions'>
          <button id='connectRevealRecoveryButton' class='button secondary' type='button'>Retrieve</button>
          <button id='connectCopyRecoveryButton' class='button secondary' type='button' disabled>Copy</button>
        </div>
        <input id='connectRecoveryCodeValue' type='password' readonly value=''>
      </div>
      <button id='connectTestButton' class='button secondary' type='button'>Test Connect server</button>
      <button id='connectRegisterButton' class='button secondary' type='button'>Register TinyMaker Connect</button>
      <button id='configConnectResetButton' class='button secondary hidden' type='button'>Reset TinyMaker Connect</button>
    </div>
  </div>
  <button id='configSaveButton' type='submit'>Save config</button>
  </div>
  </div>
  <div id='pane-notif' class='cfgPane hidden'>
  <div class='card'>
  <h2>Phone notifications</h2>
  <div class='configGrid'>
    <div class='spanAll' style='display:flex;gap:18px;flex-wrap:wrap'>
      <label class='check'><input type='radio' name='notify_channel' id='ntfNone' value='none'><span>Off</span></label>
      <label class='check'><input type='radio' name='notify_channel' id='ntfTg' value='tg'><span>Telegram<a href='#' class='qHelp' data-help='tg'>?</a></span></label>
      <label class='check'><input type='radio' name='notify_channel' id='ntfWa' value='wa'><span>WhatsApp<a href='#' class='qHelp' data-help='wa'>?</a></span></label>
      <label class='check'><input type='radio' name='notify_channel' id='ntfDc' value='dc'><span>Discord<a href='#' class='qHelp' data-help='dc'>?</a></span></label>
    </div>
    <div id='tgFields' class='spanAll hidden'>
      <div class='configGrid'>
        <label class='spanAll'><span>Bot token</span><span class='pwWrap'><input name='tg_token' id='cfgTgToken' type='password' maxlength='64' autocomplete='off' placeholder='Leave blank to keep current'><button id='cfgTgTokenShow' class='eyeBtn' type='button' title='Show/hide what you typed'>&#128065;</button></span></label>
        <label class='spanAll'><span>Chat ID</span><input name='tg_chat' id='cfgTgChat' type='text' maxlength='32' placeholder='123456789'></label>
      </div>
      <div id='tgHint' class='hint'>Messages you when a print finishes, pauses for low resin, or is canceled.</div>
      <button id='tgTestButton' class='button secondary' type='button'>Send test message</button>
    </div>
    <div id='waFields' class='spanAll hidden'>
      <div class='configGrid'>
        <label><span>Phone (with country code)</span><input name='wa_phone' id='cfgWaPhone' type='text' maxlength='20' placeholder='+3706xxxxxxx'></label>
        <label><span>CallMeBot API key</span><input name='wa_apikey' id='cfgWaKey' type='password' maxlength='16' autocomplete='off' placeholder='Leave blank to keep current'></label>
      </div>
      <div id='waHint' class='hint'>Messages go through the free CallMeBot gateway - press ? above for the one-time activation.</div>
      <button id='waTestButton' class='button secondary' type='button'>Send test message</button>
    </div>
    <div id='dcFields' class='spanAll hidden'>
      <label class='spanAll'><span>Webhook URL</span><input name='dc_webhook' id='cfgDcWebhook' type='password' maxlength='200' autocomplete='off' placeholder='https://discord.com/api/webhooks/...'></label>
      <div id='dcHint' class='hint'>Create one in your Discord server: Server Settings &gt; Integrations &gt; Webhooks.</div>
      <button id='dcTestButton' class='button secondary' type='button'>Send test message</button>
    </div>
  </div>
  <button type='submit'>Save config</button>
  </div>
  </div>
  </form>
  <div id='pane-boot' class='cfgPane hidden'>
  <div class='card'>
  <h2>Boot animation</h2>
  <div id='bootAnimList'></div>
  <div id='bootAnimHint' class='hint'>Pick which animation plays at power-on and press Save config. Show plays it on the printer's screen (idle only) - check the printer. Delete removes the file from the SD card.</div>
  <button id='bootAnimSaveButton' type='button' disabled>Save config</button>
  </div>
  </div>
  <div id='pane-backup' class='cfgPane hidden'>
  <div id='backupCard' class='card'>
  <h2>Backup &amp; restore<a href='#' class='qHelp' data-help='backup'>?</a></h2>
  <div id='connectBackupTools' class='hidden'>
    <button id='connectAutoBackupButton' class='button' type='button'>Enable Auto backup to Connect</button>
    <div class='actions'>
      <button id='connectBackupDownloadButton' class='button secondary' type='button'>Download from Connect</button>
      <button id='connectBackupRestoreButton' class='button secondary' type='button'>Restore from Connect</button>
    </div>
    <div id='connectBackupHint' class='hint'>Requires TinyMaker Connect registration.</div>
  </div>
  <div class='actions'>
    <button id='backupDownloadButton' class='button secondary' type='button'>Download backup</button>
    <button id='backupSdButton' class='button secondary' type='button'>Backup to SD</button>
    <button id='restoreButton' class='button secondary' type='button'>Restore from file</button>
    <button id='restoreSdButton' class='button secondary' type='button'>Restore from SD</button>
  </div>
  <input id='restoreFile' type='file' accept='.json,application/json' class='hidden'>
  <div id='backupHint' class='hint'>The backup holds every setting and the lifetime counters. With a backup on the SD card, the printer offers to restore it on the first boot after a full USB reflash.</div>
  <button id='configDefaultsButton' class='button secondary' type='button'>Reset to defaults</button>
  <button id='configMqttResetButton' class='button secondary hidden' type='button'>Reset MQTT</button>
  </div>
  </div>
  <div id='configHint' class='hint'>Config locks automatically while printing.</div>
</section>

<section id='updateView' class='card hidden'>
  <h2>Firmware update</h2>
  <div class='grid'>
    <div><div class='label'>Installed</div><div id='updInstalled' class='value'>-</div></div>
    <div><div class='label'>Latest</div><div id='updLatest' class='value'>-</div></div>
  </div>
  <div id='updMsg' class='hint'>Checking...</div>
  <div id='communityStats' class='hint hidden'></div>
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
  <div class='hint'>Need a full USB reflash or recovery? Use the <a href='https://connect.tinymakerwifi.com/flash.php' target='_blank' rel='noopener'>browser flasher</a> (Chrome/Edge + USB cable).</div>
</section>

<script>
const $=id=>document.getElementById(id);
let statusData=null,selectedModel='',selectedModelConnectPublicId='',sdFreeBytes=0,sdTotalBytes=0,connectConfig=null,connectTab='models';
const setText=(id,v)=>{const e=$(id);if(e)e.textContent=v;};
const show=(id,on)=>{const e=$(id);if(e)e.classList.toggle('hidden',!on);};
const enc=v=>encodeURIComponent(v||'').replace(/'/g,'%27');
const esc=v=>String(v||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const api=async(path,opt,timeoutMs)=>{
  const o=Object.assign({cache:'no-store'},opt||{});let timer=null,ctrl=null;
  if(timeoutMs&&typeof AbortController!=='undefined'){ctrl=new AbortController();o.signal=ctrl.signal;timer=setTimeout(()=>ctrl.abort(),timeoutMs);}
  try{
    let r;
    try{r=await fetch(path,o);}
    catch(e){
      // The browser reuses keep-alive sockets the printer has already closed;
      // such an attempt dies before reaching the printer (POSTs are not
      // auto-retried like GETs), so one fresh-connection retry is safe.
      if(e.name==='AbortError')throw e;
      await new Promise(res=>setTimeout(res,300));
      r=await fetch(path,o);
    }
    let j={};try{j=await r.json();}catch(e){}
    if(!r.ok||j.ok===false)throw new Error(j.error||('HTTP '+r.status));
    return j;
  }catch(e){
    if(e.name==='AbortError')throw new Error('timeout');
    if(e instanceof TypeError)throw new Error('Printer unreachable - check the connection and try again.');
    throw e;
  }
  finally{if(timer)clearTimeout(timer);}
};
// Top-center snackbar. Info messages auto-hide; warnings stay until replaced.
// The toast anchors just below the user's last click/tap, so pressing a button
// at the bottom of a long page shows the feedback right there, not off at the
// top of the viewport. No pointer yet (poll errors at load) -> top fallback.
document.addEventListener('pointerdown',ev=>{msg._y=ev.clientY;},true);
const msg=(t,warn)=>{const e=$('statusMsg');e.textContent=t||'';e.classList.toggle('warn',!!warn);
  if(t){
    const y=(typeof msg._y==='number')?Math.min(Math.max(msg._y+28,14),innerHeight-80):14;
    e.style.top=y+'px';
    e.style.animation='none';void e.offsetWidth;e.style.animation=''; // restart the slide-in on every message
  }
  clearTimeout(msg._t);if(t&&!warn)msg._t=setTimeout(()=>{if(e.textContent===t)e.textContent='';},5000);};
// Styled replacement for window.confirm() - returns a Promise. Matches the
// dashboard instead of the browser's native (unstyleable) dialog.
let uiConfirmRes=null;
const uiChoice=(message,choices)=>{
  $('confirmText').textContent=message;
  const box=$('confirmButtons');box.innerHTML='';
  choices.forEach(ch=>{
    const b=document.createElement('button');
    b.type='button';b.textContent=ch.label;
    if(ch.secondary)b.className='button secondary';
    if(ch.danger)b.classList.add('delete');
    b.addEventListener('click',()=>uiConfirmClose(ch.value));
    box.appendChild(b);
  });
  show('confirmModal',true);
  const focus=box.querySelector('button:not(.secondary)')||box.querySelector('button');
  if(focus)focus.focus();
  return new Promise(r=>{uiConfirmRes=r;});
};
const uiConfirm=(message,opts)=>{opts=opts||{};return uiChoice(message,[{label:opts.cancel||'Cancel',value:false,secondary:true},{label:opts.ok||'OK',value:true,danger:!!opts.danger}]);};
const uiConfirmClose=v=>{if(!uiConfirmRes)return;show('confirmModal',false);const r=uiConfirmRes;uiConfirmRes=null;r(v);};
$('confirmModal').addEventListener('click',e=>{if(e.target===$('confirmModal'))uiConfirmClose(false);});
document.addEventListener('keydown',e=>{if(uiConfirmRes){if(e.key==='Escape'){e.preventDefault();uiConfirmClose(false);}else if(e.key==='Enter'){e.preventDefault();const b=document.activeElement;if(b&&b.tagName==='BUTTON')b.click();}}});
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
      else {const err=new Error(j.error||('HTTP '+xhr.status));err.status=xhr.status;err.data=j;reject(err);}
    };
    xhr.onerror=()=>{clearInterval(timer);reject(new Error('upload failed'));};
    xhr.onabort=()=>{clearInterval(timer);reject(new Error('upload cancelled'));};
    render();
    xhr.send(fd);
  });
};
const uploadSummary=t=>{
  if(!t)return 'unknown';
  let p=[];
  if(t.layers!==undefined)p.push(t.layers+' layers');
  if(t.printLayers!==undefined&&t.layers!==undefined&&Number(t.printLayers)!==Number(t.layers))p.push(t.printLayers+' print layers');
  if(t.heightMm!==undefined)p.push(Number(t.heightMm).toFixed(2)+' mm');
  if(t.sizeBytes!==undefined)p.push(formatBytes(t.sizeBytes));
  if(t.estimatedTime)p.push(t.estimatedTime);
  return p.length?p.join(' - '):'unknown';
};
const uploadConflictChoice=async d=>{
  const text='A model named "'+(d.name||'Model')+'" already exists.\n\nExisting: '+uploadSummary(d.existing)+'\nIncoming: '+uploadSummary(d.incoming)+'\n\nReplace keeps the old model until the new one has unpacked successfully. Rename imports this as a new model.';
  return uiChoice(text,[{label:'Cancel',value:'cancel',secondary:true},{label:'Rename',value:'rename',secondary:true},{label:'Replace',value:'replace',danger:true}]);
};
const modelUploadFd=(blob,filename,action,meta)=>{
  const fd=new FormData();meta=meta||{};
  if(action)fd.append('action',action);
  fd.append('source',meta.source||'dashboard_upload');
  ['connect_public_id','connect_url','original_credits','license','resin_ml'].forEach(k=>{if(meta[k]!==undefined&&meta[k]!==null&&String(meta[k]).length)fd.append(k,meta[k]);});
  fd.append('file',blob,filename);
  return fd;
};
const uploadModelPayload=async(blob,filename,hintEl,meta)=>{
  const send=action=>uploadWithProgress(modelUploadFd(blob,filename,action,meta),hintEl);
  try{return await send('');}
  catch(e){
    if(e.status!==409||!e.data||!e.data.conflict)throw e;
    const choice=await uploadConflictChoice(e.data);
    if(choice!=='replace'&&choice!=='rename')throw new Error('Upload cancelled');
    return await send(choice);
  }
};
const uploadModelPreview=async(name,blob,type)=>{
  if(!name||!blob)return false;
  const fd=new FormData();
  const suffix=type==='05'?'05':(type==='1'?'1':'');
  fd.append('preview',blob,'preview'+suffix+'.png');
  await api('/api/files/model/preview?name='+enc(name)+(type?'&type='+enc(type):''),{method:'POST',body:fd},30000);
  return true;
};
const uploadPreviewFromUrl=async(name,url,type)=>{
  if(!name||!url)return false;
  const r=await fetch(url,{cache:'no-store'});
  if(!r.ok)throw new Error('preview download failed (HTTP '+r.status+')');
  const blob=await r.blob();
  const img=new Image();
  const obj=URL.createObjectURL(blob);
  try{
    img.src=obj;
    await new Promise((res,rej)=>{img.onload=res;img.onerror=()=>rej(new Error('preview image failed to load'));});
    const cv=document.createElement('canvas');
    cv.width=PREV_W;cv.height=PREV_H;
    const ctx=cv.getContext('2d');
    ctx.fillStyle='#151517';ctx.fillRect(0,0,cv.width,cv.height);
    const scale=Math.min(cv.width/img.naturalWidth,cv.height/img.naturalHeight);
    const w=Math.max(1,Math.round(img.naturalWidth*scale)),h=Math.max(1,Math.round(img.naturalHeight*scale));
    ctx.drawImage(img,Math.round((cv.width-w)/2),Math.round((cv.height-h)/2),w,h);
    await uploadModelPreview(name,await canvasBlob(cv),type);
    return true;
  }finally{URL.revokeObjectURL(obj);}
};
const localPreviewBlob=async(name,type)=>{
  const r=await fetch('/api/files/model/preview?name='+enc(name)+'&type='+enc(type)+'&r='+Date.now(),{cache:'no-store'});
  if(!r.ok)throw new Error('saved preview '+type+' failed to load');
  return await r.blob();
};
let statusInFlight=false,statusFailCount=0,pendingPrintCmd='',pendingPrintInFlight=false,localPrintStartedAt=0,lpsSynced=false,uploadBusy=false,updLock=false,updSawDown=false,updLockAt=0;
const showUpdLock=()=>{updLock=true;updSawDown=false;updLockAt=Date.now();$('updOverlay').classList.add('on');};
const hideUpdLock=()=>{updLock=false;$('updOverlay').classList.remove('on');};
const pageFirmwareVersion=()=>$('fwVersion').textContent.trim();
const reloadIfFirmwareChanged=s=>{
  const live=String(s.firmwareVersion||'').trim(),page=pageFirmwareVersion();
  const liveB=String(s.firmwareBuild||'').trim(),pageB=($('fwBuild').dataset.build||'').trim();
  if((!live||!page||live===page)&&(!liveB||!pageB||liveB===pageB))return false;
  location.replace('/?fw='+encodeURIComponent(live)+'&r='+Date.now());
  return true;
};
// Selection is staged: picking a row only marks it, bootAnimSaveButton applies.
// The "Default library" below the SD list comes from the project's gh-pages
// (same host as self-update) - picking Install pulls the .tmb onto the SD card.
let bootAnimSel='';        // selection as saved on the printer
let bootAnimPending=null;  // staged pick (null = nothing staged)
let bootAnimSd=[];         // animations currently on the SD card
let bootAnimLib=null;      // gh-pages manifest cache (null until fetched)
const BOOTANIM_LIB='https://slibbinas.github.io/TinyMakerWifi/bootanims/';
const smallBtn=(txt,danger)=>{
  const b=document.createElement('button');
  b.type='button';b.textContent=txt;
  b.style.cssText='flex:0 0 auto;width:auto;white-space:nowrap;margin:0;padding:5px 13px;font-size:12.5px;background:transparent;border:1px solid '+(danger?'#44343a':'#3a3a3f')+';color:'+(danger?'#e08a92':'#c9c9ce')+';border-radius:7px;cursor:pointer';
  return b;
};
const renderBootAnims=()=>{
  const wrap=$('bootAnimList');
  wrap.innerHTML='';
  wrap.style.cssText='display:flex;flex-direction:column;gap:2px;margin:8px 0 4px;max-height:320px;overflow-y:auto';
  const shown=bootAnimPending===null?bootAnimSel:bootAnimPending;
  // __shuffle: Brian's pseudo-entry - the firmware picks a random installed
  // animation at boot. Offered once there is more than one to shuffle.
  const rows=[{name:'',display:'Default (built-in)'}]
    .concat(bootAnimSd.length>1?[{name:'__shuffle',display:'Shuffle installed animations'}]:[])
    .concat(bootAnimSd);
  rows.forEach(a=>{
    const active=shown===a.name;
    const row=document.createElement('div');
    row.style.cssText='display:flex;align-items:center;gap:12px;padding:9px 10px;border-radius:8px'+(active?';background:rgba(255,138,30,.09)':'');

    const pick=document.createElement('button');
    pick.type='button';
    pick.style.cssText='flex:1;min-width:0;display:flex;align-items:center;gap:11px;background:none;border:0;cursor:pointer;padding:0;margin:0;text-align:left';

    const dot=document.createElement('span');
    dot.style.cssText='flex:0 0 auto;width:13px;height:13px;border-radius:50%;border:2px solid '+(active?'#ff8a1e':'#6a6a72')+';background:'+(active?'#ff8a1e':'transparent')+';box-shadow:'+(active?'inset 0 0 0 2px #1e1e23':'none');

    const label=document.createElement('span');
    label.style.cssText='min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:'+(active?'#ff8a1e':'#e9e9ec')+';font-weight:'+(active?'600':'500');
    label.textContent=a.display;

    pick.appendChild(dot);pick.appendChild(label);
    if(a.name){
      const meta=document.createElement('span');
      meta.style.cssText='flex:0 0 auto;color:#8a8a92;font-size:12px';
      meta.textContent=a.name==='__shuffle'?'random':formatBytes(a.sizeBytes);
      pick.appendChild(meta);
    }
    pick.addEventListener('click',()=>{bootAnimPending=(a.name===bootAnimSel)?null:a.name;renderBootAnims();});
    row.appendChild(pick);

    if(a.name&&a.name!=='__shuffle'){
      const show=smallBtn('Show');
      show.addEventListener('click',()=>previewBootAnim(a.name,show));
      row.appendChild(show);
      const del=smallBtn('Delete',true);
      del.addEventListener('click',()=>deleteBootAnim(a.name,a.display));
      row.appendChild(del);
    }
    wrap.appendChild(row);
  });
  $('bootAnimSaveButton').disabled=bootAnimPending===null;
  // Default library entries not yet on the SD card
  if(bootAnimLib&&bootAnimLib.length){
    const missing=bootAnimLib.filter(e=>!bootAnimSd.some(a=>a.name===e.name));
    if(missing.length){
      const head=document.createElement('div');
      head.className='subhead';head.textContent='Default library';
      wrap.appendChild(head);
      missing.forEach(e=>{
        const row=document.createElement('div');
        row.style.cssText='display:flex;align-items:center;gap:12px;padding:9px 10px;border-radius:8px';
        const label=document.createElement('span');
        label.style.cssText='flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#c9c9ce';
        label.textContent=e.display;
        const meta=document.createElement('span');
        meta.style.cssText='flex:0 0 auto;color:#8a8a92;font-size:12px';
        meta.textContent=formatBytes(e.sizeBytes);
        const inst=smallBtn('Install');
        inst.addEventListener('click',()=>installBootAnim(e,inst));
        row.appendChild(label);row.appendChild(meta);row.appendChild(inst);
        wrap.appendChild(row);
      });
    }
  }
};
const loadBootAnims=async()=>{
  const wrap=$('bootAnimList');
  if(!wrap.childElementCount)wrap.innerHTML='<div class="hint">Loading animations...</div>';
  try{
    const d=await api('/api/boot-anim');
    bootAnimSel=d.selected||'';
    if(bootAnimPending===bootAnimSel)bootAnimPending=null;
    bootAnimSd=d.animations||[];
    renderBootAnims();
  }catch(e){$('bootAnimHint').textContent=e.message;return;}
  if(bootAnimLib===null){
    try{const r=await fetch(BOOTANIM_LIB+'manifest.json',{cache:'no-store'});
      bootAnimLib=r.ok?(await r.json()).animations||[]:[];}
    catch(e){bootAnimLib=[];}
    renderBootAnims();
  }
};
const previewBootAnim=async(name,btn)=>{
  btn.disabled=true;
  try{await api('/api/boot-anim/preview',{method:'POST',body:new URLSearchParams({name})},12000);
    msg('Playing on the printer - check its screen.');}
  catch(e){msg(e.message,true);}
  setTimeout(()=>{btn.disabled=false;},1500);
};
const installBootAnim=async(e,btn)=>{
  btn.disabled=true;btn.textContent='Installing...';
  try{await api('/api/boot-anim/install',{method:'POST',body:new URLSearchParams({url:BOOTANIM_LIB+e.file,name:e.name})},90000);
    msg('Installed "'+e.display+'".');loadBootAnims();}
  catch(err){msg(err.message,true);btn.disabled=false;btn.textContent='Install';}
};
const deleteBootAnim=async(name,display)=>{
  if(!await uiConfirm('Delete "'+display+'" from the printer?',{danger:true}))return;
  try{await api('/api/boot-anim/delete',{method:'POST',body:new URLSearchParams({name})});
    if(bootAnimPending===name)bootAnimPending=null;
    msg('Deleted "'+display+'".');loadBootAnims();}
  catch(e){msg(e.message,true);}
};
$('bootAnimSaveButton').addEventListener('click',async()=>{
  if(bootAnimPending===null){msg('Pick an animation first - the current one is already saved.');return;}
  const name=bootAnimPending;
  try{await api('/api/boot-anim/select',{method:'POST',body:new URLSearchParams({name})});
    bootAnimPending=null;
    msg(name==='__shuffle'?'Shuffle enabled. Reboot to see a random installed animation.':(name?'Boot animation set. Reboot to see it.':'Using the built-in boot animation.'));loadBootAnims();}
  catch(e){msg(e.message,true);}
});
const openView=view=>{
  // Leaving the model view cancels an in-flight slice run (fetchSlices checks
  // the sequence each layer) - it must not keep loading behind another view.
  if(view!=='model')fetchSlicesSeq++;
  if(view!=='connect'||connectTab!=='boot')clearBootAnimPreviews();
  show('homeView',view==='home');
  show('modelPanel',view==='model');
  show('connectView',view==='connect');
  show('configView',view==='config');
  show('updateView',view==='update');
  $('homeViewButton').classList.toggle('active',view==='home'||view==='model');
  $('connectViewButton').classList.toggle('active',view==='connect');
  $('configViewButton').classList.toggle('active',view==='config');
  $('updateViewButton').classList.toggle('active',view==='update');
  if(view==='home')loadFiles();
  if(view==='connect')loadConnectApp().then(()=>loadConfig()).then(()=>loadConnectTab()).catch(e=>msg(e.message,true));
  if(view==='config'){loadConfig();loadBootAnims();}
  if(view==='update')loadUpdate();
};

const applyStatus=s=>{
    const was=statusData&&statusData.busy; statusData=s;
    setText('fwBuild',s.firmwareBuild?('('+s.firmwareBuild+')'):'');
    if(s.busy&&typeof s.runSecs==='number'){const c=Date.now()-s.runSecs*1000;if(!lpsSynced||c<localPrintStartedAt){localPrintStartedAt=c;lpsSynced=true;}}
    if(!s.busy){localPrintStartedAt=0;lpsSynced=false;}
    if((pendingPrintCmd==='stop'&&s.stopping)||(pendingPrintCmd==='pause'&&(s.pausing||s.paused))||(pendingPrintCmd==='resume'&&s.resuming))pendingPrintCmd='';
    setText('stateValue',s.state); setText('wifiValue',s.wifiText); setText('ipValue',s.ip); setText('lifetimeValue',s.lifetimePrintTime); setText('uvLedValue',s.uvLedTime||'-'); setText('sdValue',s.sdText);
    if(typeof s.freeHeap==='number'){const u=s.uptimeSecs||0,ud=Math.floor(u/86400),uh=Math.floor(u%86400/3600),um=Math.floor(u%3600/60);setText('debugValue','heap '+Math.round(s.freeHeap/1024)+'k | min '+Math.round(s.minFreeHeap/1024)+'k | blk '+Math.round(s.maxAllocHeap/1024)+'k | up '+(ud?ud+'d ':'')+uh+'h '+um+'m');}
    const wb=$('wifiBars').children,wr=s.wifiRssi,wn=(wr&&wr<0)?(wr>-60?3:(wr>-75?2:1)):0;for(let i=0;i<3;i++)wb[i].classList.toggle('on',i<wn);
    const eta=(s.busy&&s.remainingSecs>0)?new Date(Date.now()+s.remainingSecs*1000).toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'}):'';
    setText('layerValue',s.layerText); setText('resinValue',s.resinText); if(!s.busy)setText('runValue',s.runTime); setText('remainingValue',eta?s.remainingTime+' · ~'+eta:s.remainingTime); tickLocalStatus();
    setText('vatValue',s.vatLow?s.vatText+' (low!)':s.vatText); $('vatValue').style.color=s.vatLow?'#ff6b5f':'';
    const wc=s.webControl!==false;
    show('webControlBanner',!wc);
    $('vatRefillButton').disabled=(!!s.busy&&!s.canResume)||!wc;
    $('modelStartButton').disabled=!wc;
    // dashboard upload is UI-locked only - the slicer endpoint stays open
    $('uploadButton').disabled=!wc||uploadBusy;
    $('uploadFile').disabled=!wc;
    show('dryRunBanner',!!s.dryRun);
    $('disableDryRunButton').disabled=!!s.busy;
    $('disableDryRunButton').textContent=s.busy?'Disable when idle':'Press here to disable';
    ['printLayerBox','printResinBox','printRunBox','printRemainingBox','printControls'].forEach(id=>show(id,s.busy));
    // Keep the SD manager card visible mid-print (user preference over the
    // 0.13.4 hide): the last-known list stays on screen, locked - no fresh
    // /api/files calls while printing (SD is busy feeding layers).
    // firmware actions lock the moment a print starts, even mid-visit
    if(s.busy)['updInstallLatest','updInstallSelected','updUploadButton','updFile','updVersionSelect'].forEach(id=>$(id).disabled=true);
    // 3D print progress: reuses slices prefetched before the start (or by an
    // earlier Preview 3D) - zero printer traffic while printing. A page
    // refresh restores them from localStorage.
    if(s.busy&&s.model&&(slicesCache.name!==s.model||!slicesCache.slices.length))restoreSlicesFromStorage(s.model);
    if(s.busy&&s.model&&s.totalLayers>0&&slicesCache.name===s.model&&slicesCache.slices.length){
      dashPreviewName='';  // the print takes the card over from an idle preview
      $('printPreviewTitle').textContent='Print progress 3D';
      show('dashModelInfo',false);show('dashShareButton',false);
      show('printPreviewCard',true);
      const frac=Math.min(1,s.currentLayer/s.totalLayers);
      $('printPreviewBarFill').style.width=Math.round(frac*100)+'%';  // smooth, every poll
      if(Math.abs(frac-lastPrevFrac)>=0.004){lastPrevFrac=frac;drawIso($('printPreviewCanvas'),frac);}
    }else{
      // Idle: the card stays up when a model preview is being shown in it.
      show('printPreviewCard',!s.busy&&!!dashPreviewName);
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
    updateConnectView(connectConfig);
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
    // While the update overlay is up the printer is rebooting: a poll on the
    // old connection can hang the full 30s and, via statusInFlight, block the
    // reload from firing. Short timeout so recovery is caught within seconds.
    const s=await api('/api/status',null,updLock?4000:30000);
    if(reloadIfFirmwareChanged(s))return;
    if(updLock&&updSawDown)location.reload();
    if(updLock&&!updSawDown&&Date.now()-updLockAt>90000){updLock=false;$('updOverlay').classList.remove('on');msg('Update did not start - the printer never went down. Check System > Update on the printer.',true);}
    statusFailCount=0;
    applyStatus(s);
    if(typeof renderGs==='function')renderGs(); // auto-tick "first print"
    if(!pendingPrintCmd)msg('',false);
  }catch(e){
    statusFailCount++;
    if(updLock)updSawDown=true;
    if(deleteBusy)msg('Deleting - the printer is busy removing files...');
    else if(statusData&&statusData.busy)msg('Syncing with printer at the next safe network window...',true);
    // A single missed poll is routine while the printer does SD-heavy work
    // (scans, slice streaming) - only speak up when it keeps failing.
    else if(statusFailCount>=3)msg('Status unavailable: '+e.message,true);
  }finally{statusInFlight=false;}
};

let filesItems=[],filesHidden=0,filesPage=0,filesQuery='',connectLocalModels={},localBootAnims={},activeBootAnim='';
const FILES_PER_PAGE=12;
const renderFiles=()=>{
  const list=$('filesList');
  const q=filesQuery.toLowerCase();
  // Stable A-Z order (models first, then archives) - the raw list follows
  // SD/FAT directory order, which reshuffles after deletes.
  const sorted=filesItems.slice().sort((a,b)=>a.type!==b.type?(a.type==='model'?-1:1):a.name.localeCompare(b.name,undefined,{sensitivity:'base'}));
  const items=q?sorted.filter(it=>it.name.toLowerCase().indexOf(q)>=0):sorted;
  const pages=Math.max(1,Math.ceil(items.length/FILES_PER_PAGE));
  if(filesPage>=pages)filesPage=pages-1;
  if(filesPage<0)filesPage=0;
  const slice=items.slice(filesPage*FILES_PER_PAGE,(filesPage+1)*FILES_PER_PAGE);
  const dis=(statusData&&(statusData.webControl===false||statusData.busy))?' disabled':'';
  const busy=!!(statusData&&statusData.busy);
  let h=busy?'<div class="hint warn">Locked while printing.</div>':'';
  if(!slice.length)h+='<div class="hint">'+(q?'No models match the filter.':(busy?'SD contents load after the print finishes.':'No printable model folders or SL1/ZIP archives found.'))+'</div>';
  slice.forEach(it=>{
    const meta=it.type==='model'?'Model folder':'Archive - '+formatBytes(it.sizeBytes);
    h+='<div class="file"><div><strong>'+esc(it.name)+'</strong><div class="meta">'+esc(meta)+'</div></div><div class="rowActions">';
    if(it.type==='model')h+='<button class="small secondaryBtn"'+dis+' onclick="dashPreview(\''+enc(it.name)+'\')">Preview</button><button class="small"'+dis+' onclick="startPrint(\''+enc(it.name)+'\')">Start</button>';
    h+='<button class="delete"'+dis+' onclick="deleteFile(\''+enc(it.name)+'\')">Delete</button></div></div>';
  });
  const nModels=items.filter(it=>it.type==='model').length,nArch=items.length-nModels;
  // Counts and the hidden-items note share one row (left | right) - stacked
  // they stretched the card for nothing (user finding).
  let foot='';
  if(items.length>3)foot+='<span class="meta">'+nModels+' model'+(nModels===1?'':'s')+(nArch?' · '+nArch+' archive'+(nArch===1?'':'s'):'')+'</span>';
  if(filesHidden>0)foot+='<span class="meta">'+filesHidden+' other SD item(s) hidden</span>';
  if(foot)h+='<div style="display:flex;justify-content:space-between;gap:10px;margin-top:10px">'+foot+'</div>';
  if(pages>1)h+='<div class="rowActions" style="justify-content:center;margin-top:10px"><button class="small secondaryBtn"'+(filesPage===0?' disabled':'')+' onclick="filesNav(-1)">&laquo; Prev</button><span class="meta">'+(filesPage+1)+' / '+pages+'</span><button class="small secondaryBtn"'+(filesPage+1>=pages?' disabled':'')+' onclick="filesNav(1)">Next &raquo;</button></div>';
  list.innerHTML=h;
};
const loadFiles=async()=>{
  const list=$('filesList');
  if(statusData&&statusData.busy){renderFiles();show('filesFilter',false);return;} // no SD reads mid-print; show the cached list, locked
  if(!filesItems.length)list.innerHTML='<div class="hint">Loading the SD card...</div>';
  try{
    const d=await api('/api/files');
    updateSdUsage(d);
    filesItems=d.items;filesHidden=d.hiddenCount||0;
    connectLocalModels={};
    filesItems.forEach(it=>{if(it.type==='model'&&it.connectPublicId)connectLocalModels[it.connectPublicId]=it.name;});
    show('filesFilter',filesItems.length>FILES_PER_PAGE);
    renderFiles();
    if(typeof renderGs==='function')renderGs(); // auto-tick "model on SD"
  }catch(e){updateSdUsage(null);list.innerHTML='<div class="hint warn">'+e.message+'</div>';}
};

const modelDetails=async(nameEnc,estimate)=>{
  const name=decodeURIComponent(nameEnc); selectedModel=name; openView('model');
  // Cancel a previous model's slice run RIGHT AWAY: it hogs the printer and
  // would keep this model's details request (and its parameters) waiting.
  fetchSlicesSeq++;
  if(!estimate){
    selectedModelConnectPublicId='';
    setText('modelTitle',name); setText('modelLayers','Loading'); setText('modelHeight','-'); setText('modelTime','-');
    setText('modelPrintLayers','-');
    show('modelPrintLayersBox',false); show('modelResinBox',false); show('modelProgress',false);
    // No button dance (user finding): the whole action row stays hidden while
    // the details load and appears ONCE in its final state below. The preview
    // block is always there - an empty build-volume box until a render lands.
    show('modelActions',false);
    show('previewWrap',true); show('prevSpin',false); drawVolumeBox($('modelPreviewCanvas'));
  } else {
    setText('modelResin','Calculating exact...');show('modelResinBox',true);
    show('modelProgress',true);
  }
  try{
    const d=await api('/api/files/model?name='+enc(name)+(estimate?'&estimate=1':''));
    selectedModelConnectPublicId=d.connectPublicId||'';
    setText('modelTitle',d.name); setText('modelLayers',d.layers); setText('modelHeight',Number(d.heightMm).toFixed(2)+' mm'); setText('modelTime',d.estimatedTime);
    const showPrint=d.printLayers!==undefined&&Number(d.printLayers)!==Number(d.layers);
    show('modelPrintLayersBox',showPrint); if(showPrint)setText('modelPrintLayers',d.printLayers);
    // Resin box is always there: exact value when known, otherwise the quick
    // estimate lands with the auto preview render below ("Estimating...").
    show('modelResinBox',true);
    if(d.resinEstimated)setText('modelResin',Number(d.resinMl).toFixed(1)+' ml');
    else if(!estimate)setText('modelResin','Estimating...');
    show('modelShareButton',connectIsReady()&&!selectedModelConnectPublicId);
    if(!estimate){
      // No buttons (user decision): the preview renders itself. Cached PNG is
      // used only when the exact resin is also known - otherwise we need the
      // slices anyway for the quick estimate. Runs are sequenced, so browsing
      // to another model or leaving the view supersedes this one.
      const lh1=statusData?Number(statusData.layerHeight)>0.06:!!d.preview1;
      const hasVoxel=lh1?d.preview1:d.preview05;
      if(hasVoxel&&d.resinEstimated){
        try{await loadSavedPreview(name);}
        catch(e){modelPreview();}
      }else modelPreview(); // not awaited - the action row appears right away
    }
  }catch(e){msg(e.message,true);}
  // Reveal the action row in one shot (Back stays reachable on errors too).
  finally{show('modelActions',true);show('modelProgress',false);}
};

// --- 3D preview: fetch every Nth sliced layer, render an isometric stack
// inside the build-volume box. All drawing happens in the browser. Slices are
// kept in slicesCache so the print-progress view can reuse them with zero
// printer traffic while printing.
const PREV_W=720,PREV_H=420,PREV_S=4.6,PREV_CX=360,PREV_CY=272;
const isoPt=(x,y,z)=>({X:PREV_CX+(x-y)*0.866*PREV_S,Y:PREV_CY+(x+y)*0.35*PREV_S-z*0.8*PREV_S});
let slicesCache={name:'',mode:'',slices:[],gw:80,gh:60,modelH:0,layers:0};
let lastPrevFrac=-1;
const loadSavedPreview=name=>loadSavedPreviewTo($('modelPreviewCanvas'),name);
const loadSavedPreviewTo=async(cv,name)=>{
  const ctx=cv.getContext('2d'),img=new Image();
  // The cached PNG is ~100 KB off the ESP (~2-3 s) - say so instead of an
  // empty box (user finding; no % here, it is one file, not slices).
  paintPreviewProgress(cv,'Loading preview...',null);
  img.src='/api/files/model/preview?name='+enc(name)+'&r='+Date.now();
  await new Promise((res,rej)=>{img.onload=res;img.onerror=()=>rej(new Error('saved preview failed to load'));});
  cv.width=PREV_W;cv.height=PREV_H;
  // Slicer thumbnails ship in their own palette (PrusaSlicer teal etc.):
  // rotate the image's dominant hue onto the brand orange so every preview
  // matches the dashboard. Our own renders are already orange -> rotation ~0.
  // Grays (the floor grid) carry no hue, so they are untouched.
  let rot=0;
  try{
    const tc=document.createElement('canvas');tc.width=64;tc.height=48;
    const tx=tc.getContext('2d',{willReadFrequently:true});
    tx.drawImage(img,0,0,64,48);
    const px=tx.getImageData(0,0,64,48).data;
    let sx=0,sy=0;
    for(let p=0;p<px.length;p+=4){
      const r=px[p],g=px[p+1],b=px[p+2],mx=Math.max(r,g,b),d=mx-Math.min(r,g,b);
      if(d<30||px[p+3]<200)continue;
      let h=mx===r?((g-b)/d)%6:(mx===g?(b-r)/d+2:(r-g)/d+4);
      h*=60;if(h<0)h+=360;
      sx+=Math.cos(h*Math.PI/180);sy+=Math.sin(h*Math.PI/180);
    }
    if(sx||sy){const H=Math.atan2(sy,sx)*180/Math.PI;rot=Math.round(25-H);if(Math.abs(rot)<15)rot=0;}
  }catch(e){}
  ctx.clearRect(0,0,PREV_W,PREV_H);
  if(rot)ctx.filter='hue-rotate('+rot+'deg)';
  ctx.drawImage(img,0,0,PREV_W,PREV_H);
  ctx.filter='none';
  show('previewWrap',true);
};
// Just the build-volume box - shown as a placeholder in details before any
// model is rendered, and reused by drawIso underneath the voxels.
const drawVolumeBox=cv=>{
  cv.width=PREV_W;cv.height=PREV_H;
  const ctx=cv.getContext('2d');ctx.clearRect(0,0,PREV_W,PREV_H);
  const MX=40.8,MY=30.6,MZ=68;
  ctx.strokeStyle='#4a4a52';ctx.lineWidth=1;
  const C=[[0,0,0],[MX,0,0],[MX,MY,0],[0,MY,0],[0,0,MZ],[MX,0,MZ],[MX,MY,MZ],[0,MY,MZ]];
  [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]].forEach(e=>{
    const a=isoPt(...C[e[0]]),b=isoPt(...C[e[1]]);
    ctx.beginPath();ctx.moveTo(a.X,a.Y);ctx.lineTo(b.X,b.Y);ctx.stroke();
  });
  return ctx;
};
// Loading state painted straight onto a preview canvas: big percentage in the
// box plus an orange progress bar underneath - the usual "image loading" look.
// White label everywhere; the brand orange is reserved for the progress bar
// itself (drawn only when frac is a number - the slice-by-slice render).
const paintPreviewProgress=(cv,label,frac)=>{
  const ctx=drawVolumeBox(cv);
  ctx.fillStyle='#e9e9ec';
  ctx.font='bold 34px sans-serif';ctx.textAlign='center';
  ctx.fillText(label,PREV_CX,PREV_CY-16);ctx.textAlign='left';
  if(frac===null)return;
  const bw=PREV_W-160;
  ctx.strokeStyle='#4a4a52';ctx.strokeRect(80,PREV_H-26,bw,10);
  ctx.fillStyle='#e8720c';
  if(frac>0)ctx.fillRect(81,PREV_H-25,Math.max(2,(bw-2)*Math.min(frac,1)),8);
};
const drawIso=(cv,doneFrac)=>{
  const {slices,gw,gh,modelH}=slicesCache;
  const ctx=drawVolumeBox(cv);
  const MX=40.8,MY=30.6;
  const N=slices.length;
  for(let k=0;k<N;k++){
    const t=N>1?k/(N-1):0,z=t*modelH;
    // Strict boundary: at 0% nothing is printed yet - the old t<=doneFrac
    // painted the whole first slice solid before the print even started.
    const s=slices[k],solid=doneFrac>0&&t<=doneFrac;
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
    localStorage.setItem('tmSlices',JSON.stringify({name:slicesCache.name,mode:slicesCache.mode,gw:slicesCache.gw,gh:slicesCache.gh,modelH:slicesCache.modelH,layers:slicesCache.layers,data:data}));
  }catch(e){}
};
const restoreSlicesFromStorage=name=>{
  try{
    const o=JSON.parse(localStorage.getItem('tmSlices')||'null');
    if(!o||o.name!==name||!o.data||!o.data.length)return false;
    const slices=o.data.map(b64=>{const b=atob(b64);const s=new Uint8Array(b.length);for(let i=0;i<b.length;i++)s[i]=b.charCodeAt(i);return s;});
    slicesCache={name:o.name,mode:o.mode||'',slices:slices,gw:o.gw,gh:o.gh,modelH:o.modelH,layers:o.layers};
    return true;
  }catch(e){return false;}
};
let fetchSlicesSeq=0;
const fetchSlices=async(name,layers,modelH,btn,mode)=>{
  // Only one slice run at a time: a second run (another model opened, Start
  // pressed) supersedes this one - two loops in parallel starve the printer.
  const mySeq=++fetchSlicesSeq;
  mode=mode||'current';
  const N=Math.min(36,layers),gw=80,gh=60,slices=[];
  let whiteSum=0; // for the quick resin estimate below
  const oc=document.createElement('canvas');oc.width=gw;oc.height=gh;
  const octx=oc.getContext('2d',{willReadFrequently:true});
  for(let k=0;k<N;k++){
    if(mySeq!==fetchSlicesSeq)throw new Error('preview superseded');
    const li=N>1?1+Math.round(k*(layers-1)/(N-1)):1;
    const img=new Image();
    let url='/api/files/layer?name='+enc(name)+'&i='+li;
    if(mode==='source05')url+='&source=1';
    else if(mode==='print1')url+='&layer_height=0.10';
    img.src=url;
    // A hung request used to stall the preview forever with no way out -
    // give every layer fetch a hard timeout (user finding, 0.14.3).
    await new Promise((res,rej)=>{const t=setTimeout(()=>{img.src='';rej(new Error('layer '+li+' timed out - try again'));},20000);img.onload=()=>{clearTimeout(t);res();};img.onerror=()=>{clearTimeout(t);rej(new Error('layer '+li+' failed to load'));};});
    octx.drawImage(img,0,0,gw,gh);
    const d=octx.getImageData(0,0,gw,gh).data;
    const s=new Uint8Array(gw*gh);
    let white=0;
    for(let p=0;p<gw*gh;p++){const v=d[p*4]>96?1:0;s[p]=v;white+=v;}
    whiteSum+=white/(gw*gh);
    slices.push(s);
    if(btn)btn.textContent='Loading '+Math.round(100*(k+1)/N)+'%';
  }
  // Quick resin estimate straight from the sampled slices: mean white area x
  // plate area x model height. Coarse (36 samples, 80x60 grid) but instant -
  // the exact printer-side scan stays behind Calculate ml.
  const mlEst=N?(whiteSum/N)*40.8*30.6*modelH/1000:0;
  slicesCache={name:name,mode:mode,slices:slices,gw:gw,gh:gh,modelH:modelH,layers:layers,mlEst:mlEst};
  saveSlicesToStorage();
};
// Shared voxel-preview pipeline: slices (with painted % progress) -> quick
// resin estimate -> isometric render -> SD cache. Returns the quick estimate.
const renderVoxelPreview=async(cv,name,layers,modelH)=>{
  if(slicesCache.name!==name||slicesCache.mode!=='current'||!slicesCache.slices.length){
    paintPreviewProgress(cv,'Loading preview...',0);
    await fetchSlices(name,layers,modelH,{set textContent(v){
      const p=parseInt(v.replace(/\D/g,''))||0;
      paintPreviewProgress(cv,'Loading preview '+p+'%',p/100);
    }});
  }
  // Slices restored from an older localStorage format have no estimate yet -
  // it is cheap to derive from the slices we already hold.
  if(!slicesCache.mlEst&&slicesCache.slices.length){
    let w=0;for(const s of slicesCache.slices){let n=0;for(let p=0;p<s.length;p++)n+=s[p];w+=n/s.length;}
    slicesCache.mlEst=(w/slicesCache.slices.length)*40.8*30.6*slicesCache.modelH/1000;
  }
  drawIso(cv,1);
  const blob=await canvasBlob(cv);
  try{await uploadModelPreview(name,blob,statusData&&Number(statusData.layerHeight)>0.06?'1':'05');}
  catch(e){}  // cache write is best effort
  return slicesCache.mlEst;
};
// Details-view preview (Brian's Connect app deep-links here via modelDetails).
const modelPreview=async()=>{
  if(!selectedModel)return;
  const name=selectedModel;
  const layers=parseInt($('modelPrintLayers').textContent)||parseInt($('modelLayers').textContent)||0;
  if(!layers)return;
  const modelH=parseFloat($('modelHeight').textContent)||layers*0.05;
  show('previewWrap',true);show('prevSpin',false);
  try{
    const mlEst=await renderVoxelPreview($('modelPreviewCanvas'),name,layers,modelH);
    if(selectedModel!==name)return;  // user opened another model meanwhile
    // Free byproduct of the render: the quick resin estimate ("~" = rough;
    // clicking the value runs the exact printer-side scan).
    if(mlEst&&$('modelResin').textContent.indexOf(' ml')<0){
      setText('modelResin','~'+mlEst.toFixed(1)+' ml (quick)');
      $('modelResin').title='Click to run the exact scan on the printer (takes minutes)';
      $('modelResin').style.cursor='pointer';
    }
  }catch(e){
    if(e.message==='preview superseded')return;  // navigated away - stay quiet
    msg(e.message,true);
    if(selectedModel===name)drawVolumeBox($('modelPreviewCanvas'));
  }
};
// Dashboard preview: the SD row's Preview button renders straight into the
// Print progress 3D card (retitled Model preview while idle) - no view change.
let dashPreviewName='';
const dashPreview=async nameEnc=>{
  const name=decodeURIComponent(nameEnc);
  if(statusData&&statusData.busy)return;   // the print progress owns the card
  dashPreviewName=name;
  fetchSlicesSeq++;                        // cancel a previous run right away
  const cv=$('printPreviewCanvas');
  $('printPreviewTitle').textContent='Model preview';
  $('printPreviewBarFill').style.width='0%';
  show('dashModelInfo',false);show('dashShareButton',false);
  show('printPreviewCard',true);
  paintPreviewProgress(cv,'Loading preview...',null);
  try{
    const d=await api('/api/files/model?name='+enc(name));
    if(dashPreviewName!==name)return;
    let resin=d.resinEstimated?Number(d.resinMl).toFixed(1)+' ml':'';
    const setInfo=()=>{
      $('dashModelInfo').textContent=d.name+' · '+(d.printLayers||d.layers)+' layers · '+
        Number(d.heightMm).toFixed(1)+' mm · '+d.estimatedTime+(resin?' · '+resin:'');
      show('dashModelInfo',true);};
    setInfo();
    const lh1=statusData?Number(statusData.layerHeight)>0.06:!!d.preview1;
    const hasVoxel=lh1?d.preview1:d.preview05;
    if(hasVoxel&&d.resinEstimated)await loadSavedPreviewTo(cv,name);
    else{
      const layers=Number(d.printLayers)||Number(d.layers)||0;
      const mlEst=await renderVoxelPreview(cv,name,layers,Number(d.heightMm)||layers*0.05);
      if(dashPreviewName!==name)return;
      if(!resin&&mlEst){resin='~'+mlEst.toFixed(1)+' ml (quick)';setInfo();}
    }
    if(dashPreviewName!==name)return;
    show('dashShareButton',connectIsReady()&&!(d.connectPublicId||'').length);
  }catch(e){
    if(e.message==='preview superseded')return;
    msg(e.message,true);
    if(dashPreviewName===name)paintPreviewProgress(cv,'Preview failed',null);
  }
};
window.dashPreview=dashPreview;

const connectIsReady=()=>!!(connectConfig&&connectConfig.connectEnabled&&connectConfig.connectPrinterPublicId&&connectConfig.connectTokenSet);
const connectBase=()=>String((connectConfig&&connectConfig.connectBaseUrl)||'https://connect.tinymakerwifi.com').replace(/\/+$/,'');
let connectAppPromise=null;
const loadConnectApp=()=>{
  if(!connectIsReady()){
    const root=$('connectHostedRoot');
    if(root)root.innerHTML='<h2>TinyMaker Connect</h2><div class="hint">Enable and register TinyMaker Connect in Settings before loading the hosted Connect app.</div>';
    return Promise.reject(new Error('TinyMaker Connect is not registered.'));
  }
  if(window.TinyMakerConnectHostedReady)return Promise.resolve();
  if(connectAppPromise)return connectAppPromise;
  const root=$('connectHostedRoot');
  if(root)root.innerHTML='<h2>TinyMaker Connect</h2><div class="hint">Loading Connect from '+esc(connectBase())+'...</div>';
  connectAppPromise=new Promise((resolve,reject)=>{
    const s=document.createElement('script');
    s.src=connectBase()+'/assets/printer-connect.js?v=)SPA"
#ifdef FIRMWARE_VERSION
    FIRMWARE_VERSION
#else
    "dev"
#endif
    R"SPA(';
    s.async=true;
    s.onload=()=>window.TinyMakerConnectHostedReady?resolve():reject(new Error('Connect app did not initialize'));
    s.onerror=()=>reject(new Error('Connect app failed to load from '+connectBase()));
    document.head.appendChild(s);
  }).catch(e=>{connectAppPromise=null;const r=$('connectHostedRoot');if(r)r.innerHTML='<h2>TinyMaker Connect</h2><div class="hint warn">'+esc(e.message)+'</div><button type="button" onclick="loadConnectApp().then(()=>loadConfig()).then(()=>loadConnectTab()).catch(err=>msg(err.message,true))">Retry</button>';throw e;});
  return connectAppPromise;
};
window.loadConnectApp=loadConnectApp;
const clearBootAnimPreviews=()=>{if(window.TinyMakerConnectClearBootAnimPreviews)window.TinyMakerConnectClearBootAnimPreviews();};
const loadConnectTab=async()=>{if(window.TinyMakerConnectLoadTab)return window.TinyMakerConnectLoadTab();};
const setConnectTab=tab=>{if(window.TinyMakerConnectSetTab)return window.TinyMakerConnectSetTab(tab);connectTab=tab;};

const canvasBlob=cv=>new Promise(res=>cv.toBlob(b=>res(b),'image/png'));
const shareModel=async name=>{
  try{
    await loadConnectApp();
    if(window.TinyMakerConnectShareModel)return window.TinyMakerConnectShareModel(name);
    throw new Error('Connect app did not expose sharing');
  }catch(e){msg(e.message,true);}
};
window.shareModel=shareModel;

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
const startPrint=async(nameEnc,force)=>{const name=decodeURIComponent(nameEnc||enc(selectedModel));if(!name)return;if(!force&&!await uiConfirm('Start this print?'))return;
  if(!force&&statusData&&statusData.askRefill){
    if(await uiConfirm('Did you refill the VAT since the last print?\nOK = yes (restart the estimate from a full VAT), Cancel = no.')){
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
        const layers=Number(d.printLayers)||Number(d.layers)||0;
        // Big models take tens of seconds here and it looked like a hang
        // (user finding): feed fetchSlices' per-layer progress into the toast.
        // Progress updates rewrite the text directly - calling msg() each time
        // restarts the slide-in animation and the toast flickers (user finding).
        const progTxt=v=>'Preparing 3D progress preview - '+v.replace('Loading ','')+' (the print starts right after)';
        const prog={set textContent(v){const e=$('statusMsg');
          if(e.textContent.indexOf('Preparing 3D')===0){e.textContent=progTxt(v);clearTimeout(msg._t);}
          else msg(progTxt(v));
          // Starting from the SD row keeps the dashboard visible - mirror the
          // progress onto the Print progress 3D box there.
          const p=parseInt(v.replace(/\D/g,''))||0;
          try{paintPreviewProgress($('printPreviewCanvas'),'Preparing 3D preview '+p+'%',p/100);}catch(_){}
        }};
        // Keep the 3D card on screen through the prefetch (starting from an
        // SD row leaves the dashboard visible) - the busy status takes over.
        dashPreviewName=name;
        $('printPreviewTitle').textContent='Print progress 3D';
        show('dashModelInfo',false);show('dashShareButton',false);
        show('printPreviewCard',true);
        try{paintPreviewProgress($('printPreviewCanvas'),'Preparing 3D preview...',0);}catch(_){}
        await fetchSlices(name,layers,Number(d.heightMm)||layers*0.05,prog);
      }
    }catch(e){}
  }
  try{
  const r=await api('/api/print/start?name='+enc(name)+(force?'&force=1':''),{method:'POST'},8000);
  if(r&&r.warning==='low_resin'){if(await uiConfirm('Low resin: ~'+r.vatRemainingMl+' ml left in the VAT (estimate).\nStart anyway?'))startPrint(nameEnc,true);return;}
  msg('Print queued. Waiting for printer sync...');localPrintStartedAt=Date.now();lpsSynced=false;applyStatus(localBusyStatus('Homing',0));openView('home');refreshStatus();}catch(e){msg(e.message,true);}};
// Deleting a big model removes hundreds of layer files and blocks the printer
// for a while - keep a persistent "deleting" toast and silence the status-poll
// timeouts instead of flashing "Status unavailable" (user finding, 0.14.3).
let deleteBusy=false;
const deleteFile=async nameEnc=>{const name=decodeURIComponent(nameEnc);if(!await uiConfirm('Delete this SD item?',{danger:true}))return;deleteBusy=true;msg('Deleting '+name+' - large models take a while...');try{await api('/api/files/delete?name='+enc(name),{method:'POST'},180000);deleteBusy=false;msg('Deleted '+name+'.');if(dashPreviewName===name){dashPreviewName='';show('printPreviewCard',false);}loadFiles();refreshStatus();}catch(e){deleteBusy=false;msg(e.message,true);}};
const printCommand=async(cmd,confirmText)=>{if(confirmText&&!await uiConfirm(confirmText,{danger:cmd==='stop'}))return;pendingPrintCmd=cmd;applyPendingPrintUi();msg((cmd==='stop'?'Stop':cmd==='pause'?'Pause':'Resume')+' requested. Waiting for printer connection...',true);retryPendingPrintCommand();};
const fmtDur=ms=>{const s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h>0?h+'h '+m+'m':m+'m '+(s%60)+'s';};
const tickLocalStatus=()=>{
  if(statusData&&statusData.busy&&localPrintStartedAt){
    setText('runValue',fmtDur(Date.now()-localPrintStartedAt));
  }
};

let sdBackupPresent=false; // from /api/config; part of the restoreSdButton gate below
// restoreSdButton's disabled state is owned HERE (lock + backup-present) - the
// 2s status poll re-runs this, so a per-button override elsewhere gets wiped.
// Settings section panes: one visible at a time, same interaction as the
// Connect sub-tabs. Hidden panes stay in the form, so any Save posts the
// full config - no fields are lost by saving from another section.
const CFG_PANES=['print','network','notif','boot','backup'];
const setCfgPane=p=>{
  document.querySelectorAll('#cfgNav a').forEach(a=>a.classList.toggle('on',a.dataset.pane===p));
  CFG_PANES.forEach(x=>show('pane-'+x,x===p));
};
document.querySelectorAll('#cfgNav a').forEach(a=>a.addEventListener('click',e=>{e.preventDefault();setCfgPane(a.dataset.pane);}));
const setConfigDisabled=disabled=>{document.querySelectorAll('#configForm input,#configForm button,#configDefaultsButton,#configMqttResetButton,#backupDownloadButton,#backupSdButton,#restoreButton,#restoreSdButton,#connectAutoBackupButton,#connectBackupDownloadButton,#connectBackupRestoreButton').forEach(e=>e.disabled=disabled);$('restoreSdButton').disabled=disabled||!sdBackupPresent;$('bootAnimSaveButton').disabled=disabled||bootAnimPending===null;};
const configFormData=autoBackupOverride=>{const fd=new URLSearchParams(new FormData($('configForm')));const auto=typeof autoBackupOverride==='boolean'?autoBackupOverride:!!(connectConfig&&connectConfig.connectAutoBackup);fd.append('connect_auto_backup_set','1');if(auto)fd.append('connect_auto_backup','1');return fd;};
const configIsLocallyLocked=()=>!!(statusData&&statusData.busy);
const updateNetworkFields=()=>{$('cfgWebDashboardEnabled').disabled=!$('cfgWifiEnabled').checked;};
const confirmNetworkToggle=async e=>{
  if(e.target.checked){updateNetworkFields();return;}
  const text=e.target.id==='cfgWifiEnabled'
    ? 'Turn WiFi off?\nThe printer will reboot and you will lose web access until WiFi is re-enabled on the printer (System > Advanced).'
    : 'Turn web control off?\nThe dashboard becomes view-only: print controls, SD delete, settings and firmware updates are disabled (monitoring and slicer upload keep working). Re-enable on the printer (System > Advanced).';
  const ok=await uiConfirm(text,{danger:true});
  // Set the box explicitly per the answer: while the modal was open a status
  // poll may have run loadConfig and rewritten the checkbox under the user.
  e.target.checked=!ok;
  updateNetworkFields();
};
const updateMqttFields=()=>show('mqttFields',$('cfgMqttEnabled').checked);
const updateConnectFields=()=>show('connectFields',$('cfgConnectEnabled').checked);
const updateTgFields=()=>{show('tgFields',$('ntfTg').checked);show('waFields',$('ntfWa').checked);show('dcFields',$('ntfDc').checked);};
const updateConnectView=c=>{
  c=c||connectConfig||{};
  if(window.TinyMakerConnectHostedUpdate)window.TinyMakerConnectHostedUpdate(c);
  else if($('connectHostedRoot'))$('connectHostedRoot').innerHTML=connectIsReady()?'<h2>TinyMaker Connect</h2><div class="hint">Connect UI is loaded from '+esc(connectBase())+'.</div>':'<h2>TinyMaker Connect</h2><div class="hint">Enable and register TinyMaker Connect in Settings before loading the hosted Connect app.</div>';
  show('modelShareButton',connectIsReady()&&!!selectedModel&&!selectedModelConnectPublicId);
};
const loadConfig=async()=>{
  if(!connectConfig)$('configHint').textContent='Loading settings...';
  try{
    const c=await api('/api/config');
    connectConfig=c;
    $('cfgLayerHeight').value=Number(c.layerHeight).toFixed(2); $('cfgBaseExposure').value=c.baseExposure; $('cfgRegularExposure').value=c.regularExposure; $('cfgBaseLayers').value=c.baseLayers; $('cfgTransitionLayers').value=c.transitionLayers;
    // One-click undo of the last replaced exposure (test pick / config save)
    const prevR=Number(c.prevRegularExposure)||0;
    show('undoRegExp',prevR>0&&prevR!==Number(c.regularExposure));
    $('undoRegExp').textContent='Undo ('+prevR+'s)';
    $('undoRegExp').dataset.v=prevR;
    $('cfgSlowLiftDistance').value=c.slowLiftDistance; $('cfgFastLiftDistance').value=c.fastLiftDistance; $('cfgSlowLiftFeedrate').value=c.slowLiftFeedrate; $('cfgFastLiftFeedrate').value=c.fastLiftFeedrate; $('cfgDropBackFeedrate').value=c.dropBackFeedrate; $('cfgVatMl').value=c.vatMl; $('cfgLowResinMl').value=c.lowResinMl; $('cfgLowResinPause').checked=!!c.lowResinPause; $('cfgAskRefill').checked=!!c.askRefill; $('cfgUiTimeout').value=c.uiTimeoutSecs; $('cfgDryRun').checked=!!c.dryRun; $('cfgWifiEnabled').checked=!!c.wifiEnabled; $('cfgWebDashboardEnabled').checked=!!c.webDashboardEnabled; $('cfgBootUpdateCheck').checked=!!c.bootUpdateCheck; $('cfgStatsPing').checked=!!c.statsPing;
    $('cfgMqttEnabled').checked=!!c.mqttEnabled; $('cfgMqttHost').value=c.mqttHost||''; $('cfgMqttPort').value=c.mqttPort||1883; $('cfgMqttUser').value=c.mqttUser||''; $('cfgMqttPassword').value=''; $('cfgMqttTopic').value=c.mqttTopic||'TinyMaker';
    $('mqttHint').textContent=c.mqttPasswordSet?'Password is saved. Enter a new one only if you want to replace it.':'MQTT password is not set.';
    $('cfgConnectEnabled').checked=!!c.connectEnabled; $('cfgConnectBaseUrl').value=c.connectBaseUrl||'https://connect.tinymakerwifi.com'; $('cfgConnectPrinterName').value=c.connectPrinterName||''; $('cfgConnectLeaderboard').checked=!!c.connectLeaderboardOptIn;
    const connectId=c.connectPrinterPublicId||''; const reclaim=!!c.connectReclaimRequired&&!connectId; $('connectHint').textContent=connectId?(c.connectTokenSet?('Registered as '+connectId+'. Publish token stored'+(c.connectTokenTail?(' (ends in '+c.connectTokenTail+')'):'')+'. '+(c.connectLeaderboardOptIn?'Leaderboard sharing on.':'Leaderboard sharing off.')):('Registered as '+connectId+', but the local publish token is missing. Reclaim this printer with your recovery code or set it up as a new printer.')):(reclaim?'This printer has been connected before. Enter the recovery code or set it up as a new printer.':(c.connectLastStatus||'Registering stores a printer token for publishing models, ratings and bookmarks. Leaderboard sharing is optional.'));$('connectRegisterButton').textContent=connectId?'Update TinyMaker Connect':'Register TinyMaker Connect';
    show('connectReclaimBox',reclaim);
    show('connectRecoveryCodeBox',!!connectId&&!!c.connectTokenSet);
    $('connectRecoveryCodeValue').value='';
    $('connectRecoveryCodeValue').type='password';
    $('connectCopyRecoveryButton').disabled=true;
    $('ntfTg').checked=!!c.tgEnabled; $('ntfWa').checked=!!c.waEnabled; $('ntfDc').checked=!!c.dcEnabled; $('ntfNone').checked=!c.tgEnabled&&!c.waEnabled&&!c.dcEnabled;
    $('cfgWaPhone').value=c.waPhone||''; $('cfgWaKey').value='';
    $('cfgWaKey').placeholder=c.waKeySet?('Saved key: ****'+(c.waKeyTail||'')+' - type a new one to replace it'):'Key from the CallMeBot activation reply';
    $('cfgDcWebhook').value='';
    $('cfgDcWebhook').placeholder=c.dcHookSet?('Saved webhook: ****'+(c.dcHookTail||'')+' - paste a new one to replace it'):'https://discord.com/api/webhooks/...';
    $('cfgTgToken').value=''; $('cfgTgToken').type='password'; $('cfgTgChat').value=c.tgChat||'';
    $('cfgTgToken').placeholder=c.tgTokenSet?('Saved token: ********'+(c.tgTokenTail||'')+' (hidden) - type a new one to replace it'):'Paste the token from @BotFather';
    $('tgHint').textContent=(c.tgTokenSet?('Bot token saved (last 4 chars: '+(c.tgTokenTail||'?')+' - check they match your token). '):'Bot token is not set. ')+'Messages you when a print finishes, pauses for low resin, or is canceled.';
    updateConnectView(c);
    updateNetworkFields();updateMqttFields();updateConnectFields();updateTgFields();
    show('configMqttResetButton',!!c.mqttConfigured);
    show('configConnectResetButton',!!c.connectConfigured);
    // The hosted Connect tab exists only after registration, so an
    // unconfigured printer never injects remote Connect JavaScript.
    show('connectViewButton',connectIsReady());
    if(!connectIsReady()&&!$('connectView').classList.contains('hidden'))openView('home');
    $('configDefaultsButton').textContent=(c.mqttConfigured||c.connectConfigured)?'Reset to defaults (Excluding integrations)':'Reset to defaults';
    const noWc=!c.webDashboardEnabled;
    const locked=!!c.locked||configIsLocallyLocked()||noWc;
    setConfigDisabled(locked); $('configHint').textContent=noWc?'Settings are disabled while Web control is off (enable it on the printer: System > Advanced).':(locked?'Config is locked while printing.':'Config locks automatically while printing.');
    sdBackupPresent=!!c.sdBackupPresent; $('restoreSdButton').disabled=locked||!sdBackupPresent; $('restoreSdButton').title=sdBackupPresent?'':'No backup found on the SD card';
    const fmt24=e=>{const d=new Date(e*1000),p=n=>('0'+n).slice(-2);return d.getFullYear()+'-'+p(d.getMonth()+1)+'-'+p(d.getDate())+' '+p(d.getHours())+':'+p(d.getMinutes());};
    $('backupHint').textContent=(c.sdBackupPresent?('Backup on SD: '+(c.sdBackupEpoch?fmt24(c.sdBackupEpoch):'date unknown')+'. '):'No backup on the SD card yet. ')+'With a backup on the SD card, the printer offers to restore it on the first boot after a full USB reflash.';
    show('connectBackupTools',!!c.connectEnabled);
    $('connectAutoBackupButton').textContent=c.connectAutoBackup?'Disable Auto backup to Connect':'Enable Auto backup to Connect';
    $('connectAutoBackupButton').classList.toggle('danger',!!c.connectAutoBackup);
    $('connectBackupHint').textContent=(c.connectAutoBackup?'Connect auto backup is on. ':'Connect auto backup is off. ')+(c.connectBackupEpoch?('Last Connect backup: '+fmt24(c.connectBackupEpoch)+'. '):'No Connect backup has been saved yet. ')+'Connect backups do not include stored MQTT, Telegram or Connect tokens. '+(c.connectTokenSet?'':'Register TinyMaker Connect first.');
    $('connectAutoBackupButton').disabled=locked||!c.connectTokenSet;
    $('connectBackupDownloadButton').disabled=locked||!c.connectTokenSet||!c.connectBackupEpoch;
    $('connectBackupRestoreButton').disabled=locked||!c.connectTokenSet||!c.connectBackupEpoch;
    return c;
  }catch(e){const locked=configIsLocallyLocked();setConfigDisabled(locked);updateConnectView(null);$('configHint').textContent=locked?'Config is locked while printing.':e.message;return null;}
};

window.modelDetails=modelDetails;
window.startPrint=startPrint;
window.deleteFile=deleteFile;
window.filesNav=d=>{filesPage+=d;renderFiles();};
$('filesFilter').addEventListener('input',e=>{filesQuery=e.target.value.trim();filesPage=0;renderFiles();});

$('uploadForm').addEventListener('submit',async e=>{e.preventDefault();const f=$('uploadFile').files[0];if(!f)return;if(!checkUploadFits(f.size,$('uploadHint')))return;uploadBusy=true;$('uploadButton').disabled=true;$('uploadHint').textContent='Uploading...';const started=Date.now();try{const r=await uploadModelPayload(f,f.name,$('uploadHint'),{source:'dashboard_upload'});$('uploadFile').value='';$('uploadButton').classList.add('secondary');$('uploadHint').textContent=(r&&r.renamed?'Imported as '+r.name+'. ':'Upload complete in '+formatShortTime(Date.now()-started)+'.');loadFiles();}catch(err){$('uploadHint').textContent=err.message;}finally{uploadBusy=false;$('uploadButton').disabled=false;}});
$('configForm').addEventListener('submit',async e=>{e.preventDefault();try{await api('/api/config',{method:'POST',body:configFormData()});msg('Config saved.');loadConfig();}catch(err){msg(err.message,true);}});
$('connectAutoBackupButton').addEventListener('click',async()=>{const enabled=!!(connectConfig&&connectConfig.connectAutoBackup);const next=!enabled;const text=next?'Enable Auto backup to Connect? Settings will be uploaded to TinyMaker Connect after settings changes and after prints. Stored MQTT, Telegram and Connect tokens are not included.':'Disable Auto backup to Connect? New settings changes and prints will no longer update your Connect backup.';if(!await uiConfirm(text,{danger:!next,ok:next?'Enable':'Disable'}))return;try{await api('/api/config',{method:'POST',body:configFormData(next)},12000);msg(next?'Connect auto backup enabled.':'Connect auto backup disabled.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('connectBackupDownloadButton').addEventListener('click',async()=>{try{const r=await fetch('/api/connect/backup',{cache:'no-store'});if(!r.ok){let j={};try{j=await r.json();}catch(e){}throw new Error(j.error||('backup failed (HTTP '+r.status+')'));}const b=await r.blob();const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='tinymaker-connect-backup.json';a.click();URL.revokeObjectURL(a.href);msg('Connect backup downloaded.');}catch(e){msg(e.message,true);loadConfig();}});
$('connectBackupRestoreButton').addEventListener('click',async()=>{if(!await uiConfirm('Restore all settings from the TinyMaker Connect backup? Current settings will be overwritten.',{danger:true}))return;try{await api('/api/connect/restore',{method:'POST'},12000);msg('Settings restored from Connect backup.');loadConfig();refreshStatus();}catch(e){msg(e.message,true);loadConfig();}});
$('configDefaultsButton').addEventListener('click',async()=>{const keep=$('configDefaultsButton').textContent.indexOf('integrations')>=0;if(!await uiConfirm(keep?'Reset config to defaults and keep integration settings?':'Reset config to defaults?',{danger:true}))return;try{await api('/api/config/defaults',{method:'POST'});msg(keep?'Defaults restored. Integration settings kept.':'Defaults restored.');loadConfig();}catch(e){msg(e.message,true);}});
$('configMqttResetButton').addEventListener('click',async()=>{if(!await uiConfirm('Reset MQTT settings?',{danger:true}))return;try{await api('/api/config/mqtt/defaults',{method:'POST'});msg('MQTT settings reset.');loadConfig();}catch(e){msg(e.message,true);}});
$('configConnectResetButton').addEventListener('click',async()=>{if(!await uiConfirm('Reset TinyMaker Connect settings and forget this printer token?',{danger:true}))return;try{await api('/api/config/connect/defaults',{method:'POST'});msg('TinyMaker Connect settings reset.');loadConfig();}catch(e){msg(e.message,true);}});
$('connectTestButton').addEventListener('click',async()=>{msg('Testing the Connect server...');try{await api('/api/config',{method:'POST',body:configFormData()});const r=await api('/api/connect/test',{method:'POST'},12000);msg(r.message||'TinyMaker Connect server reachable.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('connectRegisterButton').addEventListener('click',async()=>{const updating=!!(connectConfig&&connectConfig.connectPrinterPublicId);if($('cfgConnectPrinterName').value.trim()===''){msg('Enter a printer display name first.',true);$('cfgConnectPrinterName').focus();return;}if($('cfgConnectLeaderboard').checked&&!await uiConfirm('Share this printer on the public leaderboard?'))return;msg(updating?'Updating the Connect registration...':'Registering with TinyMaker Connect...');try{await api('/api/config',{method:'POST',body:configFormData()});const r=await api('/api/connect/register',{method:'POST'},12000);msg(r.message||(updating?'TinyMaker Connect updated.':'TinyMaker Connect registered.'));loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('connectReclaimButton').addEventListener('click',async()=>{const code=$('cfgConnectRecoveryCode').value.trim();if(!code){msg('Enter your recovery code first.',true);$('cfgConnectRecoveryCode').focus();return;}try{await api('/api/config',{method:'POST',body:configFormData()});const fd=new URLSearchParams();fd.append('recovery_code',code);const r=await api('/api/connect/register',{method:'POST',body:fd},12000);$('cfgConnectRecoveryCode').value='';msg(r.message||'TinyMaker Connect profile restored.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('connectNewProfileButton').addEventListener('click',async()=>{if(!await uiConfirm('Setup as a new TinyMaker Connect printer? The old Connect profile will stay on the server and this printer will receive a new recovery code.'))return;try{await api('/api/config',{method:'POST',body:configFormData()});const fd=new URLSearchParams();fd.append('new_profile','1');const r=await api('/api/connect/register',{method:'POST',body:fd},12000);msg(r.message||'New TinyMaker Connect profile created.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('connectRevealRecoveryButton').addEventListener('click',async()=>{try{const r=await api('/api/connect/recovery-code',null,12000);$('connectRecoveryCodeValue').value=r.recoveryCode||'';$('connectRecoveryCodeValue').type='text';$('connectCopyRecoveryButton').disabled=!$('connectRecoveryCodeValue').value;msg('Recovery code retrieved.');}catch(e){msg(e.message,true);}});
$('connectCopyRecoveryButton').addEventListener('click',async()=>{const v=$('connectRecoveryCodeValue').value;if(!v)return;try{if(navigator.clipboard&&navigator.clipboard.writeText){await navigator.clipboard.writeText(v);}else{$('connectRecoveryCodeValue').select();document.execCommand('copy');}msg('Recovery code copied.');}catch(e){msg('Could not copy recovery code.',true);}});
$('backupDownloadButton').addEventListener('click',async()=>{try{const r=await fetch('/api/config/backup',{cache:'no-store'});if(!r.ok)throw new Error('backup failed (HTTP '+r.status+')');const b=await r.blob();const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='tinymaker-backup.json';a.click();URL.revokeObjectURL(a.href);msg('Backup downloaded.');}catch(e){msg(e.message,true);}});
$('backupSdButton').addEventListener('click',async()=>{try{const r=await api('/api/config/backup/sd',{method:'POST'});msg(r.message||'Backup saved to SD.');loadConfig();}catch(e){msg(e.message,true);}});
$('restoreButton').addEventListener('click',()=>$('restoreFile').click());
$('restoreSdButton').addEventListener('click',async()=>{if(!await uiConfirm('Restore all settings from the SD card backup? Current settings will be overwritten.',{danger:true}))return;try{await api('/api/config/restore/sd',{method:'POST'});msg('Settings restored from SD backup.');loadConfig();refreshStatus();}catch(e){msg(e.message,true);}});
$('restoreFile').addEventListener('change',async()=>{const f=$('restoreFile').files[0];$('restoreFile').value='';if(!f)return;if(!await uiConfirm('Restore all settings from '+f.name+'? Current settings will be overwritten.',{danger:true}))return;try{const t=await f.text();await api('/api/config/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:t});msg('Settings restored from backup.');loadConfig();refreshStatus();}catch(e){msg(e.message,true);}});
$('disableDryRunButton').addEventListener('click',async()=>{if(!await uiConfirm('Disable dry run mode? Future prints will use the UV LEDs.'))return;try{await api('/api/config/dry-run?enabled=0',{method:'POST'});msg('Dry run disabled.');loadConfig();refreshStatus();}catch(e){msg(e.message,true);}});
$('vatRefillButton').addEventListener('click',async()=>{if(!await uiConfirm('Mark the VAT as refilled? The resin estimate restarts from a full VAT.'))return;try{const r=await api('/api/vat/refilled',{method:'POST'});msg('VAT marked as refilled ('+r.vatRemainingMl+' ml).');refreshStatus();}catch(e){msg(e.message,true);}});
$('cfgWifiEnabled').addEventListener('change',confirmNetworkToggle);
$('cfgWebDashboardEnabled').addEventListener('change',confirmNetworkToggle);
$('cfgMqttEnabled').addEventListener('change',updateMqttFields);
$('cfgConnectEnabled').addEventListener('change',updateConnectFields);
$('undoRegExp').addEventListener('click',async e=>{e.preventDefault();const v=Number(e.target.dataset.v)||0;if(!v)return;$('cfgRegularExposure').value=v;try{await api('/api/config',{method:'POST',body:configFormData()});msg('Regular exposure back to '+v+'s.');loadConfig();}catch(err){msg(err.message,true);}});
$('ntfNone').addEventListener('change',updateTgFields);
$('ntfTg').addEventListener('change',updateTgFields);
$('ntfWa').addEventListener('change',updateTgFields);
$('waTestButton').addEventListener('click',async()=>{msg('Sending a test message...');try{await api('/api/config',{method:'POST',body:configFormData()});const r=await api('/api/whatsapp/test',{method:'POST'},15000);msg(r.message||'Test message sent.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('ntfDc').addEventListener('change',updateTgFields);
$('dcTestButton').addEventListener('click',async()=>{msg('Sending a test message...');try{await api('/api/config',{method:'POST',body:configFormData()});const r=await api('/api/discord/test',{method:'POST'},12000);msg(r.message||'Test message sent.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('cfgTgTokenShow').addEventListener('click',()=>{const i=$('cfgTgToken');i.type=i.type==='password'?'text':'password';});
// Contextual help: one modal, content picked by key (.qHelp marks + Telegram).
const HELP={
 tg:"<b>Telegram setup</b><ol style='margin:10px 0 0;padding-left:20px;line-height:1.6'><li>In Telegram, message <b>@BotFather</b>, send <b>/newbot</b>, follow the prompts and paste the token it gives into <b>Bot token</b>.</li><li>Open your new bot and press <b>Start</b> (or send it any message) - a bot cannot message you until you do.</li><li>Message <b>@userinfobot</b>; it replies with your numeric <b>Id</b> - that is your <b>Chat ID</b>. (For a group, add <b>@RawDataBot</b> to it and use the negative id it prints.)</li><li>Press <b>Save config</b>, then <b>Send test message</b> - if it arrives, you are done.</li></ol>",
 layer:"<b>Layer height</b><p>Unlike FDM, the printed layer height is decided by THIS setting, not by the sliced file. Files are always a stack of 0.05 mm images: at 0.05 the printer uses every image, at 0.10 it takes every other one. Always slice with the 0.05 mm profile.</p>",
 resin:"<b>Resin tracking</b><p>The printer has no resin sensor - it counts down an estimate from a full VAT (this size). Press <b>VAT refilled</b> after topping up, and keep <b>Ask refill</b> on so the estimate stays honest. It warns before a print that will not fit and can pause mid-print when low.</p>",
 web:"<b>Web control</b><p>Off = this dashboard turns view-only: anyone can watch, but print control, uploads, settings and firmware updates are disabled. Turn it back on at the printer (System &gt; Advanced). Slicer upload and MQTT keep working.</p>",
 backup:"<b>Backup &amp; restore</b><p>One file holds every setting plus the lifetime counters. <b>Backup to SD</b> before a full USB reflash - the printer offers to restore it on first boot. The file contains your MQTT password and tokens, so treat it like a secret.</p>",
 wa:"<b>WhatsApp setup (CallMeBot)</b><ol style='margin:10px 0 0;padding-left:20px;line-height:1.6'><li>Open <b>callmebot.com</b> &gt; <i>WhatsApp text messages</i> and add the bot's current phone number to your contacts.</li><li>Send it the message <b>I allow callmebot to send me messages</b> from your WhatsApp.</li><li>It replies with your personal <b>API key</b> - enter it here with your phone number (with country code).</li><li><b>Save config</b>, then <b>Send test message</b>.</li></ol><p class='hint'>Messages travel through the free CallMeBot gateway (a third-party service) - unlike Telegram, which the printer talks to directly.</p>",
 dc:"<b>Discord setup (webhook)</b><ol style='margin:10px 0 0;padding-left:20px;line-height:1.6'><li>In your Discord server open <b>Server Settings &gt; Integrations &gt; Webhooks</b>.</li><li><b>New Webhook</b> - pick the channel the printer should post to, then <b>Copy Webhook URL</b>.</li><li>Paste the URL here, <b>Save config</b>, then <b>Send test message</b>.</li></ol><p class='hint'>The webhook URL is a secret - anyone holding it can post to that channel. The printer talks to Discord directly.</p>"};
const showHelp=k=>{$('helpBody').innerHTML=HELP[k]||'';show('helpModal',true);};
document.addEventListener('click',e=>{const q=e.target.closest('.qHelp');if(q){e.preventDefault();showHelp(q.dataset.help);}});
// Getting started: dismissible first-steps checklist. Some steps tick
// themselves from live data; the rest are clicked off and remembered.
const GS=[
 {k:'wifi',t:'Connect to WiFi',d:'Done - you are looking at the dashboard.',auto:()=>true},
 {k:'slicer',t:'Set up PrusaSlicer',d:"Import the profile, add a physical printer at tinymaker.local - <a href='https://slibbinas.github.io/TinyMakerWifi/manual/#print-prusa' target='_blank' rel='noopener'>manual</a>."},
 {k:'model',t:'Get a model onto the SD card',d:'Send from the slicer, upload in SD manager, or copy an .sl1/.zip to the card.',auto:()=>filesItems.some(i=>i.type==='model')},
 {k:'print',t:'Run your first print',d:'Pick the model in the Print menu or press Start here.',auto:()=>!!(statusData&&Number(statusData.lifetimePrintSecs)>0)},
 {k:'expo',t:'Calibrate exposure for your resin',d:"System &gt; Advanced &gt; Exposure test - pick the best bar and the printer sets the time. <a href='https://slibbinas.github.io/TinyMakerWifi/manual/#advanced' target='_blank' rel='noopener'>Manual</a>."},
 {k:'integr',t:'Optional: integrations',d:'Telegram, Home Assistant (MQTT) and TinyMaker Connect live in Settings.'}];
const gsDone=s=>(s.auto&&s.auto())||localStorage.getItem('tmGs_'+s.k)==='1';
const renderGs=()=>{
  const hidden=localStorage.getItem('tmGsHidden')==='1';
  show('gsCard',!hidden);
  if(hidden)return;
  $('gsList').innerHTML=GS.map(s=>{
    const on=gsDone(s);
    return "<div class='gsRow'><span class='gsMark"+(on?" on":"")+"' data-gs='"+s.k+"'>"+(on?"&#10003;":"&#9675;")+"</span><div><b>"+s.t+"</b><div class='meta'>"+s.d+"</div></div></div>";
  }).join('');
};
$('gsList').addEventListener('click',e=>{const m=e.target.closest('.gsMark');if(!m)return;const s=GS.find(x=>x.k===m.dataset.gs);if(!s||(s.auto&&s.auto()))return;const key='tmGs_'+s.k;localStorage.setItem(key,localStorage.getItem(key)==='1'?'0':'1');renderGs();});
$('gsHide').addEventListener('click',()=>{localStorage.setItem('tmGsHidden','1');renderGs();});
$('gsBtn').addEventListener('click',e=>{e.preventDefault();localStorage.setItem('tmGsHidden','0');openView('home');renderGs();$('gsCard').scrollIntoView({behavior:'smooth',block:'start'});});
renderGs();
// Light/dark toggle: flips the html data-theme attribute the boot script set,
// persists to localStorage and keeps the Manual link's theme param in sync.
const applyThemeLink=()=>{const l=document.documentElement.getAttribute('data-theme')==='light';$('manualLink').href='https://slibbinas.github.io/TinyMakerWifi/manual/'+(l?'':'?theme=dark');};
$('themeBtn').addEventListener('click',e=>{e.preventDefault();const toLight=document.documentElement.getAttribute('data-theme')!=='light';if(toLight)document.documentElement.setAttribute('data-theme','light');else document.documentElement.removeAttribute('data-theme');try{localStorage.setItem('tmTheme',toLight?'light':'dark');}catch(err){}applyThemeLink();});
applyThemeLink();
$('helpClose').addEventListener('click',()=>show('helpModal',false));
$('helpModal').addEventListener('click',e=>{if(e.target===$('helpModal'))show('helpModal',false);});
$('tgTestButton').addEventListener('click',async()=>{msg('Sending a test message...');try{await api('/api/config',{method:'POST',body:configFormData()});const r=await api('/api/telegram/test',{method:'POST'},12000);msg(r.message||'Test message sent.');loadConfig();}catch(e){msg(e.message,true);loadConfig();}});
$('homeViewButton').addEventListener('click',()=>openView('home'));
$('connectViewButton').addEventListener('click',()=>openView('connect'));
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
  // Community install counter - straight from the stats endpoint, best-effort.
  fetch('https://tinymaker-stats.slibbinas.workers.dev/stats').then(r=>r.json()).then(s=>{
    if(!s.printers)return;
    let top='',n=0;const bv=s.by_version||{};
    for(const v in bv)if(bv[v]>n){n=bv[v];top=v;}
    setText('communityStats','Community: '+s.printers+' printer'+(s.printers===1?'':'s')+' running TinyMakerWifi'+(top?' - most on '+top:''));
    show('communityStats',true);
  }).catch(()=>{});
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
  if(!await uiConfirm(warn))return;
  showUpdLock();   // immediate feedback - the download+flash takes a while
  try{await api('/api/update/install'+(v?'?version='+v:''),{method:'POST'},20000);msg('Updating... the printer reboots when done.');showUpdLock();}
  // On timeout keep the overlay: the printer is likely mid-flash and cannot
  // answer; the 90s watchdog clears it if the update never actually started.
  catch(e){if(e.message!=='timeout'){hideUpdLock();msg(e.message,true);}}
};
$('updInstallLatest').addEventListener('click',()=>installFirmware(''));
$('updInstallSelected').addEventListener('click',()=>installFirmware($('updVersionSelect').value));
$('updVersionSelect').addEventListener('change',refreshInstallSelected);
$('updUploadForm').addEventListener('submit',async e=>{
  e.preventDefault();
  if(!await uiConfirm('Flash the selected firmware.bin? The printer reboots when done.'))return;
  $('updUploadButton').disabled=true;
  showUpdLock();   // immediate feedback - the upload+flash takes a while
  try{
    const r=await fetch('/update',{method:'POST',body:new FormData(e.target)});
    if(!r.ok)throw new Error('update rejected (HTTP '+r.status+')');
    msg('Firmware flashed. The printer is rebooting...');showUpdLock();
  }catch(err){hideUpdLock();msg(err.message,true);}
  $('updUploadButton').disabled=false;
});
$('updFile').addEventListener('change',()=>$('updUploadButton').classList.toggle('secondary',!$('updFile').files.length));
$('uploadFile').addEventListener('change',()=>$('uploadButton').classList.toggle('secondary',!$('uploadFile').files.length));

$('pauseButton').addEventListener('click',()=>printCommand('pause','Pause this print?'));
$('resumeButton').addEventListener('click',()=>printCommand('resume','Resume this print?'));
$('stopButton').addEventListener('click',()=>printCommand('stop','Stop this print?'));
$('dashShareButton').addEventListener('click',()=>{if(dashPreviewName)shareModel(dashPreviewName);});
$('modelResin').addEventListener('click',async()=>{
  if($('modelResin').textContent.indexOf('~')!==0)return;
  if(!await uiConfirm('Run the exact resin scan on the printer? It decodes every layer and can take minutes.'))return;
  modelDetails(enc(selectedModel),true);
});
$('modelShareButton').addEventListener('click',()=>shareModel(selectedModel));
$('modelStartButton').addEventListener('click',()=>startPrint(enc(selectedModel)));

openView(location.hash==='#settings'?'config':(location.hash==='#connect'?'connect':'home'));refreshStatus();loadConfig();setInterval(tickLocalStatus,1000);setInterval(()=>{refreshStatus();retryPendingPrintCommand();},2000);
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
  gfx2->fillRect(122, 1, 36, 11, BLACK);
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

// Boot-animation install: the printer pulls a TMB1 file from a community URL and
// stores it in the /bootanim library, then makes it the active boot animation.
// CORS header so the cross-origin "Send to printer" page can read our reply.
//   POST /api/boot-anim/install   body: url=<http/https .tmb url>&name=<slug>
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
  server.sendHeader("Access-Control-Allow-Origin", "*");

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

  uint32_t lastDraw = 0;
  while (http.connected() && remaining != 0) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
      if (n <= 0) break;
      out.write(buf, n);
      total += n;
      if (total > MAX_ANIM_BYTES) { tooBig = true; break; }
      if (remaining > 0) { remaining -= n; if (remaining == 0) break; }
      if (millis() - lastDraw > 120) {       // size may be unknown; bar wraps every 256 KB
        lastDraw = millis();
        int w = (int)((total % 262144L) * 136L / 262144L);
        gfx2->fillRect(12, 50, 136, 12, BLACK);
        gfx2->fillRect(12, 50, w, 12, ORANGE);
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

  // Install only downloads: the active choice stays whatever it was, picking
  // is an explicit act (dashboard pick + Save config, or the printer menu).
  // Auto-selecting here silently overrode the user's choice on every install.
  writeBootAnimMetadataFile(slug);
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
  String out = "{\"selected\":\"" + jsonEscape(selected) + "\",\"animations\":[";
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
  playTmbByName(name);
  screen1();
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
  server.on("/api/boot-anim/install", HTTP_OPTIONS, []() {   // CORS preflight
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
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
