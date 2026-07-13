/**
 * @file TinyMakerConnect.ino
 * @brief TinyMaker Connect cloud-service integration.
 *
 * Kept separate from Network.ino so the local dashboard/server stays readable.
 * Network.ino owns local routes and UI; this file owns calls to the external
 * TinyMaker Connect server.
 */

#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

bool connectBackupPending = false;
unsigned long connectBackupDueMs = 0;

String connectNormalizeBaseUrl(String url) {
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  return url;
}

bool connectConfigured() {
  // The base URL defaults to a non-empty value, so "has a URL" alone would
  // make this true on every factory-fresh device.
  return connectEnabled ||
         connectBaseUrl != "https://tinymaker.inductie.nu" ||
         connectPrinterPublicId.length() > 0 ||
         connectPublishToken.length() > 0;
}

String connectFirmwareVersion() {
#ifdef FIRMWARE_VERSION
  return String(FIRMWARE_VERSION);
#else
  return "unknown";
#endif
}

String connectHardwareId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[18];
  snprintf(buf, sizeof(buf), "%04X%08X",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

String connectUrlEncode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3);
  for (size_t i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String connectJsonString(const String &json, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\":\"";
  int start = json.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  String out;
  bool escape = false;
  for (int i = start; i < (int)json.length(); i++) {
    char c = json[i];
    if (escape) {
      out += c;
      escape = false;
    } else if (c == '\\') {
      escape = true;
    } else if (c == '"') {
      break;
    } else {
      out += c;
    }
  }
  return out;
}

bool connectJsonBool(const String &json, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\":true";
  return json.indexOf(needle) >= 0;
}

String connectJsonObject(const String &json, const char *key) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int start = json.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\n' || json[start] == '\r')) start++;
  if (start >= (int)json.length() || json[start] != '{') return "";
  int depth = 0;
  bool inString = false;
  bool escape = false;
  for (int i = start; i < (int)json.length(); i++) {
    char c = json[i];
    if (inString) {
      if (escape) escape = false;
      else if (c == '\\') escape = true;
      else if (c == '"') inString = false;
    } else {
      if (c == '"') inString = true;
      else if (c == '{') depth++;
      else if (c == '}') {
        depth--;
        if (depth == 0) return json.substring(start, i + 1);
      }
    }
  }
  return "";
}

long connectJsonLong(const String &json, const char *key, long def = 0) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int start = json.indexOf(needle);
  if (start < 0) return def;
  start += needle.length();
  return atol(json.c_str() + start);
}

bool connectPostForm(const String &path, const String &body, String &response, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  String base = connectNormalizeBaseUrl(connectBaseUrl);
  if (base.length() == 0) {
    error = "TinyMaker Connect server URL is empty";
    return false;
  }

  String url = base + path;
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    error = "could not start HTTP request";
    return false;
  }

  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  response = http.getString();
  String httpError = code < 0 ? http.errorToString(code) : "";
  http.end();

  if (code < 200 || code >= 300) {
    error = code < 0 ? ("connection failed: " + httpError) : ("server returned HTTP " + String(code));
    return false;
  }
  if (response.indexOf("\"ok\":false") >= 0) {
    String serverError = connectJsonString(response, "error");
    error = serverError.length() ? serverError : "server rejected request";
    return false;
  }
  return true;
}

bool connectPostJson(const String &path, const String &body, String &response, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }
  if (connectPublishToken.length() == 0) {
    error = "TinyMaker Connect is not registered";
    return false;
  }

  String base = connectNormalizeBaseUrl(connectBaseUrl);
  if (base.length() == 0) {
    error = "TinyMaker Connect server URL is empty";
    return false;
  }

  String url = base + path;
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    error = "could not start HTTP request";
    return false;
  }

  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-TinyMaker-Token", connectPublishToken);
  int code = http.POST(body);
  response = http.getString();
  String httpError = code < 0 ? http.errorToString(code) : "";
  http.end();

  if (code < 200 || code >= 300) {
    error = code < 0 ? ("connection failed: " + httpError) : ("server returned HTTP " + String(code));
    return false;
  }
  if (response.indexOf("\"ok\":false") >= 0) {
    String serverError = connectJsonString(response, "error");
    error = serverError.length() ? serverError : "server rejected request";
    return false;
  }
  return true;
}

bool connectGet(const String &path, String &response, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }

  String base = connectNormalizeBaseUrl(connectBaseUrl);
  if (base.length() == 0) {
    error = "TinyMaker Connect server URL is empty";
    return false;
  }

  String url = base + path;
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    error = "could not start HTTP request";
    return false;
  }

  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  response = http.getString();
  String httpError = code < 0 ? http.errorToString(code) : "";
  http.end();

  if (code < 200 || code >= 300) {
    error = code < 0 ? ("connection failed: " + httpError) : ("server returned HTTP " + String(code));
    return false;
  }
  if (response.indexOf("\"ok\":true") < 0) {
    error = "server health check failed";
    return false;
  }
  return true;
}

bool connectGetAuthorized(const String &path, String &response, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }
  if (connectPublishToken.length() == 0) {
    error = "TinyMaker Connect is not registered";
    return false;
  }

  String base = connectNormalizeBaseUrl(connectBaseUrl);
  if (base.length() == 0) {
    error = "TinyMaker Connect server URL is empty";
    return false;
  }

  String url = base + path;
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok = false;
  if (url.startsWith("https://")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) {
    error = "could not start HTTP request";
    return false;
  }

  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.addHeader("X-TinyMaker-Token", connectPublishToken);
  int code = http.GET();
  response = http.getString();
  String httpError = code < 0 ? http.errorToString(code) : "";
  http.end();

  if (code < 200 || code >= 300) {
    error = code < 0 ? ("connection failed: " + httpError) : ("server returned HTTP " + String(code));
    return false;
  }
  if (response.indexOf("\"ok\":false") >= 0) {
    String serverError = connectJsonString(response, "error");
    error = serverError.length() ? serverError : "server rejected request";
    return false;
  }
  return true;
}

bool tinymakerConnectRegister(String &message, const String &recoveryCode = "", bool newProfile = false) {
  if (!connectEnabled) {
    message = "TinyMaker Connect is disabled";
    return false;
  }

  connectBaseUrl = connectNormalizeBaseUrl(connectBaseUrl);
  connectPrinterName.trim();
  if (connectPrinterName.length() == 0) {
    message = "Enter a printer display name first";
    connectLastStatus = message;
    saveDeviceConfig();
    return false;
  }

  String body = "hardware_id=" + connectUrlEncode(connectHardwareId());
  body += "&firmware_version=" + connectUrlEncode(connectFirmwareVersion());
  body += "&printer_name=" + connectUrlEncode(connectPrinterName);
  body += "&leaderboard_opt_in=";
  body += connectLeaderboardOptIn ? "1" : "0";
  if (connectPublishToken.length() > 0) {
    body += "&publish_token=" + connectUrlEncode(connectPublishToken);
  }
  if (recoveryCode.length() > 0) {
    body += "&recovery_code=" + connectUrlEncode(recoveryCode);
  }
  if (newProfile) {
    body += "&new_profile=1";
  }

  String response;
  String error;
  String path = recoveryCode.length() > 0 ? "/api/printers/reclaim" : "/api/printers/register";
  if (!connectPostForm(path, body, response, error)) {
    if (response.indexOf("\"reclaim_required\":true") >= 0) {
      connectLastStatus = "reclaim required";
      message = "This printer has been connected before. Enter the recovery code or set it up as a new printer.";
    } else {
      connectLastStatus = error;
      message = error;
    }
    saveDeviceConfig();
    return false;
  }

  String publicId = connectJsonString(response, "printer_public_id");
  String token = connectJsonString(response, "publish_token");
  String recovery = connectJsonString(response, "recovery_code");
  if (publicId.length() == 0 || token.length() == 0) {
    connectLastStatus = "bad register response";
    message = connectLastStatus;
    return false;
  }

  connectPrinterPublicId = publicId;
  connectPublishToken = token;
  if (recovery.length() > 0) connectRecoveryCode = recovery;
  {
    String backupResponse;
    String backupError;
    if (connectGetAuthorized("/api/printers/me/backup", backupResponse, backupError)) {
      long epoch = connectJsonLong(backupResponse, "updated_epoch", 0);
      if (epoch > 0) connectBackupEpoch = (uint32_t)epoch;
    }
  }
  connectLastStatus = connectJsonBool(response, "blocked") ? "registered, but blocked by server" : "registered";
  saveDeviceConfig();
  message = connectLastStatus;
  return !connectJsonBool(response, "blocked");
}

bool tinymakerConnectBackupSettings(String &message) {
  if (!connectEnabled || !connectAutoBackup || connectPublishToken.length() == 0) {
    message = "";
    return true;
  }
  if (printerBusy()) {
    message = "printer busy";
    return false;
  }

  String response;
  String error;
  if (!connectPostJson("/api/printers/me/backup", buildConfigBackupJson(false), response, error)) {
    connectLastStatus = error;
    message = error;
    saveDeviceConfig();
    return false;
  }

  long epoch = connectJsonLong(response, "updated_epoch", 0);
  if (epoch > 0) {
    connectBackupEpoch = (uint32_t)epoch;
  } else {
    time_t nowT = time(nullptr);
    connectBackupEpoch = (uint32_t)(nowT > 1700000000 ? nowT : 0);
  }
  connectLastStatus = "backup saved to Connect";
  saveDeviceConfig();
  message = connectLastStatus;
  return true;
}

void tinymakerConnectScheduleBackup() {
  if (connectEnabled && connectAutoBackup && connectPublishToken.length() > 0) {
    connectBackupPending = true;
    connectBackupDueMs = millis() + 3000UL;
  }
}

void tinymakerConnectLoop() {
  if (!connectBackupPending || printerBusy()) return;
  if ((long)(millis() - connectBackupDueMs) < 0) return;
  connectBackupPending = false;
  String message;
  tinymakerConnectBackupSettings(message);
}

bool tinymakerConnectFetchBackup(String &backupJson, uint32_t &epoch, String &message) {
  backupJson = "";
  epoch = 0;
  if (!connectEnabled) {
    message = "TinyMaker Connect is disabled";
    return false;
  }

  String response;
  String error;
  if (!connectGetAuthorized("/api/printers/me/backup", response, error)) {
    connectLastStatus = error;
    message = error;
    saveDeviceConfig();
    return false;
  }

  backupJson = connectJsonObject(response, "backup");
  if (backupJson.length() < 10 || backupNum(backupJson, "backupVersion", 0) < 1) {
    connectLastStatus = "bad Connect backup";
    message = connectLastStatus;
    saveDeviceConfig();
    return false;
  }
  epoch = (uint32_t)connectJsonLong(response, "updated_epoch", 0);
  if (epoch > 0) connectBackupEpoch = epoch;
  connectLastStatus = "backup available on Connect";
  saveDeviceConfig();
  message = connectLastStatus;
  return true;
}

String tinymakerConnectConfigJson() {
  bool configured = connectConfigured();
  String out = ",\"connectEnabled\":";
  out += connectEnabled ? "true" : "false";
  out += ",\"connectConfigured\":";
  out += configured ? "true" : "false";
  out += ",\"connectBaseUrl\":\"";
  out += jsonEscape(connectBaseUrl);
  out += "\",\"connectPrinterName\":\"";
  out += jsonEscape(connectPrinterName);
  out += "\",\"connectLeaderboardOptIn\":";
  out += connectLeaderboardOptIn ? "true" : "false";
  out += ",\"connectAutoBackup\":";
  out += connectAutoBackup ? "true" : "false";
  out += ",\"connectBackupEpoch\":";
  out += String(connectBackupEpoch);
  out += ",\"connectReclaimRequired\":";
  out += connectLastStatus == "reclaim required" ? "true" : "false";
  out += ",\"connectPrinterPublicId\":\"";
  out += jsonEscape(connectPrinterPublicId);
  out += "\",\"connectTokenSet\":";
  out += connectPublishToken.length() > 0 ? "true" : "false";
  out += ",\"connectRecoveryCodeSet\":";
  out += connectRecoveryCode.length() > 0 ? "true" : "false";
  // Last 4 chars only, for a "token is stored" hint without echoing the secret.
  out += ",\"connectTokenTail\":\"";
  out += jsonEscape(connectPublishToken.length() > 4 ? connectPublishToken.substring(connectPublishToken.length() - 4) : "");
  // The full publish token authorizes share/manage/import calls - hand it to
  // the browser only while Web control is on; a view-only dashboard gets none
  // (same rule as the MQTT password and the Telegram bot token).
  out += "\",\"connectPublishToken\":\"";
  out += webDashboardRuntimeEnabled() ? jsonEscape(connectPublishToken) : String("");
  out += "\",\"connectLastStatus\":\"";
  out += jsonEscape(connectLastStatus);
  out += "\"";
  return out;
}

void handleApiConnectRegister() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  String recoveryCode = server.hasArg("recovery_code") ? server.arg("recovery_code") : "";
  recoveryCode.trim();
  bool newProfile = server.hasArg("new_profile") &&
                    server.arg("new_profile") != "0" &&
                    server.arg("new_profile") != "false";

  String message;
  if (!tinymakerConnectRegister(message, recoveryCode, newProfile)) {
    sendApiError(502, message.c_str());
    return;
  }

  String out = "\"message\":\"";
  out += jsonEscape(message);
  out += "\",\"connectPrinterPublicId\":\"";
  out += jsonEscape(connectPrinterPublicId);
  out += "\",\"connectTokenSet\":";
  out += connectPublishToken.length() > 0 ? "true" : "false";
  sendApiOk(out);
}

void handleApiConnectRecoveryCode() {
  if (rejectIfWebControlOff()) return;
  if (connectRecoveryCode.length() == 0) {
    sendApiError(404, "recovery code is not available");
    return;
  }

  String out = "\"recoveryCode\":\"";
  out += jsonEscape(connectRecoveryCode);
  out += "\"";
  sendApiOk(out);
}

void handleApiConnectBackup() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  if (server.method() == HTTP_GET) {
    String backupJson;
    uint32_t epoch = 0;
    String message;
    if (!tinymakerConnectFetchBackup(backupJson, epoch, message)) {
      sendApiError(502, message.c_str());
      return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=tinymaker-connect-backup.json");
    server.send(200, "application/json", backupJson);
    return;
  }

  String message;
  if (!tinymakerConnectBackupSettings(message)) {
    sendApiError(502, message.c_str());
    return;
  }

  String out = "\"message\":\"";
  out += jsonEscape(message.length() ? message : "Connect auto backup is disabled");
  out += "\",\"connectBackupEpoch\":";
  out += String(connectBackupEpoch);
  sendApiOk(out);
}

void handleApiConnectRestore() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");
    return;
  }

  String backupJson;
  uint32_t epoch = 0;
  String message;
  if (!tinymakerConnectFetchBackup(backupJson, epoch, message)) {
    sendApiError(502, message.c_str());
    return;
  }

  bool wifiWasEnabled = wifiEnabled;
  applyConfigBackup(backupJson);
  connectBackupEpoch = epoch;
  saveDeviceConfig();
  mqttClient.disconnect();
  mqttDiscoverySent = false;
  sendApiOk(configJson());
  if (wifiWasEnabled && !wifiEnabled) {
    delay(700);
    ESP.restart();
  }
}

void handleApiConnectTest() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    // The health check is a blocking TLS request (up to 8 s) - never let it
    // stall the print loop's narrow network windows.
    sendApiError(409, "printer busy");
    return;
  }

  String response;
  String error;
  if (!connectGet("/health.php", response, error)) {
    connectLastStatus = error;
    saveDeviceConfig();
    sendApiError(502, error.c_str());
    return;
  }

  connectLastStatus = "server reachable";
  saveDeviceConfig();
  String out = "\"message\":\"server reachable\"";
  sendApiOk(out);
}

#endif
