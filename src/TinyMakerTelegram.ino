/**
 * @file TinyMakerTelegram.ino
 * @brief Telegram outbound notifications (V1).
 *
 * Kept separate from Network.ino so the local dashboard/server stays readable.
 * V1 is outbound-only and deliberately tiny: three "while you're away" events
 * (finished, low-resin pause, canceled), one On/Off switch, a bot token and a
 * chat id. No inbound commands - those need an async net task and stay a
 * post-1.0.0 item (see the ideas backlog). All sends are blocking HTTPS POSTs,
 * which is safe here because every trigger fires when the printer is NOT
 * mid-exposure (print end, or a lifted pause).
 *
 * The bot token is a secret: it is stored in NVS and included in the backup
 * (same as the MQTT password) but is NEVER echoed back to the browser - the
 * config JSON only reports tgTokenSet. Reuses connectUrlEncode() from
 * TinyMakerConnect.ino (same translation unit).
 */

#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// POST a message to the Telegram Bot API. Returns false with a human-readable
// reason in `error` on any failure (used by the "Send test" button).
bool telegramSendMessage(const String &text, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "WiFi is not connected";
    return false;
  }
  if (tgToken.length() == 0) {
    error = "Telegram bot token is empty";
    return false;
  }
  if (tgChat.length() == 0) {
    error = "Telegram chat ID is empty";
    return false;
  }

  String url = "https://api.telegram.org/bot" + tgToken + "/sendMessage";
  String body = "chat_id=" + connectUrlEncode(tgChat) +
                "&text=" + connectUrlEncode(text);

  HTTPClient http;
  WiFiClientSecure secure;
  secure.setInsecure();   // outbound only; matches the Connect client
  if (!http.begin(secure, url)) {
    error = "could not start HTTPS request";
    return false;
  }

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  String response = http.getString();
  String httpError = code < 0 ? http.errorToString(code) : "";
  http.end();

  if (code < 200 || code >= 300) {
    error = code < 0 ? ("connection failed: " + httpError)
                     : ("Telegram returned HTTP " + String(code));
    return false;
  }
  if (response.indexOf("\"ok\":true") < 0) {
    error = "Telegram rejected the message";
    return false;
  }
  return true;
}

// Best-effort notification routed to the active channel (Telegram or
// WhatsApp - one at a time, picked in Settings). Failures are swallowed:
// a print must never stall because a chat message could not be delivered.
void telegramNotify(const String &text) {
  String error;
  if (tgEnabled) telegramSendMessage(text, error);
  else if (waEnabled) whatsappSendMessage(text, error);
  else if (dcEnabled) discordSendMessage(text, error);
}

void tgNotifyFinished() {
  if (!tgEnabled && !waEnabled && !dcEnabled) return;
  // savePrintTime() folds printStartMs into the lifetime total but leaves the
  // variable set, so the elapsed time is still valid at this exit point.
  uint32_t secs = printStartMs ? (millis() - printStartMs) / 1000UL : 0;
  String msg = "Print finished - " + formatDuration(secs) +
               ", ~" + String(resinUsedMl, 1) + " ml used";
  telegramNotify(msg);
}

void tgNotifyLowResin() {
  telegramNotify("Low resin - printer paused. Refill the VAT to resume.");
}

void tgNotifyCanceled() {
  telegramNotify("Print canceled.");
}

// Appended to configJson(). The token itself is never sent to the browser.
String tinymakerTelegramConfigJson() {
  String out = ",\"tgEnabled\":";
  out += tgEnabled ? "true" : "false";
  out += ",\"tgTokenSet\":";
  out += tgToken.length() > 0 ? "true" : "false";
  // Last 4 chars only, for "did I paste the right token?" verification without
  // echoing the secret (empty for short tokens).
  out += ",\"tgTokenTail\":\"";
  out += jsonEscape(tgToken.length() > 4 ? tgToken.substring(tgToken.length() - 4) : "");
  out += "\",\"tgChat\":\"";
  out += jsonEscape(tgChat);
  out += "\"";
  return out;
}

// POST /api/telegram/test -> send a test message with the saved settings.
void handleApiTelegramTest() {
  if (rejectIfWebControlOff()) return;
  if (printerBusy()) {
    // Same rule as the Connect test: a blocking TLS request (up to 8 s) must
    // never run inside the print loop's narrow network windows.
    sendApiError(409, "printer busy");
    return;
  }

  String error;
  if (!telegramSendMessage("TinyMaker: test notification", error)) {
    sendApiError(502, error.c_str());
    return;
  }
  sendApiOk("\"message\":\"Test message sent\"");
}

#endif
