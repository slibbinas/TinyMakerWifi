/**
 * @file TinyMakerWhatsApp.ino
 * @brief WhatsApp outbound notifications via the free CallMeBot gateway.
 *
 * Mirrors TinyMakerTelegram.ino: same three "while you're away" events, one
 * channel selected at a time (radio in Settings - Telegram OR WhatsApp).
 * Unlike Telegram (printer talks to api.telegram.org directly), messages here
 * pass through CallMeBot's third-party service - documented in the dashboard
 * help popup. The user activates CallMeBot once from their WhatsApp and gets
 * a personal API key.
 *
 * The API key is a secret: stored in NVS, included in the backup, never
 * echoed to the browser (config JSON reports waKeySet + a 4-char tail).
 */

#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool whatsappSendMessage(const String &text, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }
  if (waPhone.length() == 0) {
    error = "WhatsApp phone number is empty";
    return false;
  }
  if (waApiKey.length() == 0) {
    error = "CallMeBot API key is empty";
    return false;
  }

  String url = "https://api.callmebot.com/whatsapp.php?phone=" +
               connectUrlEncode(waPhone) +
               "&text=" + connectUrlEncode(text) +
               "&apikey=" + connectUrlEncode(waApiKey);

  HTTPClient http;
  WiFiClientSecure secure;
  secure.setInsecure();   // outbound only; matches the Telegram/Connect clients
  if (!http.begin(secure, url)) {
    error = "could not start HTTPS request";
    return false;
  }

  http.setTimeout(10000); // CallMeBot is slower than Telegram's API
  int code = http.GET();
  String response = http.getString();
  String httpError = code < 0 ? http.errorToString(code) : "";
  http.end();

  if (code < 200 || code >= 300) {
    error = code < 0 ? ("connection failed: " + httpError)
                     : ("CallMeBot returned HTTP " + String(code));
    return false;
  }
  // CallMeBot answers 200 with an error text when the key/phone is wrong.
  if (response.indexOf("APIKey is invalid") >= 0 ||
      response.indexOf("blocked") >= 0) {
    error = "CallMeBot rejected the message - check the phone and API key";
    return false;
  }
  return true;
}

// Appended to configJson(). The API key itself is never sent to the browser.
String tinymakerWhatsAppConfigJson() {
  String out = ",\"waEnabled\":";
  out += waEnabled ? "true" : "false";
  out += ",\"waKeySet\":";
  out += waApiKey.length() > 0 ? "true" : "false";
  out += ",\"waKeyTail\":\"";
  out += jsonEscape(waApiKey.length() > 4 ? waApiKey.substring(waApiKey.length() - 4) : "");
  out += "\",\"waPhone\":\"";
  out += jsonEscape(waPhone);
  out += "\"";
  return out;
}

// POST /api/whatsapp/test -> send a test message with the saved settings.
void handleApiWhatsAppTest() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");   // same gate as the Telegram test
    return;
  }

  String error;
  if (!whatsappSendMessage("TinyMaker: test notification", error)) {
    sendApiError(502, error.c_str());
    return;
  }
  sendApiOk("\"message\":\"Test message sent\"");
}

#endif
