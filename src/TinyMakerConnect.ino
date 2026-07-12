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

String connectNormalizeBaseUrl(String url) {
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  return url;
}

bool connectConfigured() {
  return connectBaseUrl.length() > 0 ||
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

bool tinymakerConnectRegister(String &message) {
  if (!connectEnabled) {
    message = "TinyMaker Connect is disabled";
    return false;
  }

  connectBaseUrl = connectNormalizeBaseUrl(connectBaseUrl);
  String body = "hardware_id=" + connectUrlEncode(connectHardwareId());
  body += "&firmware_version=" + connectUrlEncode(connectFirmwareVersion());
  body += "&printer_name=" + connectUrlEncode(connectPrinterName);
  body += "&leaderboard_opt_in=";
  body += connectLeaderboardOptIn ? "1" : "0";

  String response;
  String error;
  if (!connectPostForm("/api/printers/register", body, response, error)) {
    connectLastStatus = error;
    message = error;
    return false;
  }

  String publicId = connectJsonString(response, "printer_public_id");
  String token = connectJsonString(response, "publish_token");
  if (publicId.length() == 0 || token.length() == 0) {
    connectLastStatus = "bad register response";
    message = connectLastStatus;
    return false;
  }

  connectPrinterPublicId = publicId;
  connectPublishToken = token;
  connectLastStatus = connectJsonBool(response, "blocked") ? "registered, but blocked by server" : "registered";
  saveDeviceConfig();
  message = connectLastStatus;
  return !connectJsonBool(response, "blocked");
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
  out += ",\"connectPrinterPublicId\":\"";
  out += jsonEscape(connectPrinterPublicId);
  out += "\",\"connectTokenSet\":";
  out += connectPublishToken.length() > 0 ? "true" : "false";
  // The publish token authorizes share/manage/import calls - hand it to the
  // browser only while Web control is on; a view-only dashboard gets none
  // (same rule as the MQTT password and the Telegram bot token).
  out += ",\"connectPublishToken\":\"";
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

  String message;
  if (!tinymakerConnectRegister(message)) {
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
