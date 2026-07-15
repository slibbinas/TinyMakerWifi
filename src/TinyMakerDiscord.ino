/**
 * @file TinyMakerDiscord.ino
 * @brief Discord notifications via a channel webhook.
 *
 * Third notification channel next to Telegram and WhatsApp (one active at a
 * time, radio in Settings). The cheapest of the three: the user creates a
 * webhook in their Discord server (Server Settings > Integrations > Webhooks)
 * and pastes the URL - no bot, no keys. The webhook URL is a secret (anyone
 * holding it can post to the channel): stored in NVS, in the backup, never
 * echoed to the browser (config JSON reports dcHookSet + a 4-char tail).
 */

#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool discordSendMessage(const String &text, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }
  // Only real Discord webhook URLs - keeps the field from being pointed at
  // arbitrary hosts.
  if (!dcWebhook.startsWith("https://discord.com/api/webhooks/") &&
      !dcWebhook.startsWith("https://discordapp.com/api/webhooks/")) {
    error = "not a Discord webhook URL";
    return false;
  }

  String body = "{\"content\":\"" + jsonEscape(text) + "\"}";

  HTTPClient http;
  WiFiClientSecure secure;
  secure.setInsecure();   // outbound only; matches the other notify clients
  if (!http.begin(secure, dcWebhook)) {
    error = "could not start HTTPS request";
    return false;
  }

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();

  if (code < 200 || code >= 300) {   // Discord answers 204 on success
    error = code < 0 ? ("connection failed: " + http.errorToString(code))
                     : ("Discord returned HTTP " + String(code));
    return false;
  }
  return true;
}

// Appended to configJson(). The webhook URL is never sent to the browser.
String tinymakerDiscordConfigJson() {
  String out = ",\"dcEnabled\":";
  out += dcEnabled ? "true" : "false";
  out += ",\"dcHookSet\":";
  out += dcWebhook.length() > 0 ? "true" : "false";
  out += ",\"dcHookTail\":\"";
  out += jsonEscape(dcWebhook.length() > 4 ? dcWebhook.substring(dcWebhook.length() - 4) : "");
  out += "\"";
  return out;
}

// POST /api/discord/test -> send a test message with the saved settings.
void handleApiDiscordTest() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    sendApiError(409, "printer busy");   // same gate as the other test buttons
    return;
  }

  String error;
  if (!discordSendMessage("TinyMaker: test notification", error)) {
    sendApiError(502, error.c_str());
    return;
  }
  sendApiOk("\"message\":\"Test message sent\"");
}

#endif
