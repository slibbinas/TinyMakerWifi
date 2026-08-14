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
  // Route by "can actually send", not just the enable flag: a half-configured
  // channel bails before any TLS and must not cost the preview cache below.
  // (Settings allows one channel at a time, so ready-routing == flag-routing
  // in practice.)
  bool tgReady = tgEnabled && tgToken.length() > 0 && tgChat.length() > 0;
  bool waReady = waEnabled && waPhone.length() > 0 && waApiKey.length() > 0;
  bool dcReady = dcEnabled && dcWebhook.length() > 0;
  if (!tgReady && !waReady && !dcReady) return;
  // Mid-print the model-preview RAM snapshot (~66-74 KB) fragments the heap:
  // maxAllocHeap drops ~110 KB -> ~48 KB and every TLS send fails with
  // "connection refused" (mbedTLS can't get its buffers; measured on hardware
  // 08-08 - this also silently killed the old low-resin pause notification).
  // Free it around any notify: all three channels are HTTPS. The print-end
  // path already ran freePreviewCache() before its notify - this generalizes
  // that to the mid-print sends (low-resin warn/stop). No-op when empty.
  bool hadPreview = previewCacheBuf != nullptr;
  freePreviewCache();
  String error;
  if (tgReady) telegramSendMessage(text, error);
  else if (waReady) whatsappSendMessage(text, error);
  else if (dcReady) discordSendMessage(text, error);
  // Re-load the thumbnail from SD once the send is done (V 08-08): the TLS
  // connection is closed so the heap is back, and this runs in the print
  // loop itself (single thread - no SD contention with layer reads; a
  // one-shot read smaller than one layer PNG). Skipped on the cancel path
  // (tgNotifyCanceled fires ~20 lines before the print-exit free - a
  // re-capture there would be wasted SD work) and once the print is done.
  // Slack 16 -> 12 KB (auditas 08-14): gyvas siluetu buferis paaugo 7,8 KB (80x60), tad
  // su senu reikalavimu miniatiura po zinutes daug dazniau nebegriztu ir dingtu visam
  // likusiam spaudiniui. 12 KB vis dar didesnis uz didziausia likusia mid-print
  // alokacija (~12 KB statuso JSON String). Best-effort - didziausi renderiai (~70 KB)
  // vis tiek gali netilpti; tada miniatiura 409'ina iki spaudinio pabaigos.
  if (hadPreview && printerBusy() && !print_canceled && !homing_canceled)
    capturePreviewCache(12 * 1024, false);
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

// Progress context (V 08-08): every mid/end-of-print message carries
// "layer x/y" + elapsed time so the phone alone tells where the print stands.
void tgNotifyLowResin() {
  uint32_t secs = printStartMs ? (millis() - printStartMs) / 1000UL : 0;
  String msg = "Low resin - printer paused at layer " + String(current_layer) +
               "/" + String(layer_counter);
  if (secs) msg += " (" + formatDuration(secs) + " in)";
  msg += ". Refill the VAT to resume.";
  telegramNotify(msg);
}

// 0.17 #40: one-shot heads-up sent BEFORE the low-resin stop, from the print
// loop at the idle between-layers gap, so a refill is not a surprise.
// Progress as "layer x/y" (V 08-08 - the runway reads at a glance); the
// minutes-to-stop estimate is appended only when a rate exists (fires
// before layer 5 -> too early to know one; "(~0 layers, ~0 min)" was noise).
void tgNotifyLowResinSoon(float ml, int minsToStop) {
  if (!tgEnabled && !waEnabled && !dcEnabled) return;
  String msg = "Low resin soon - ~" + String(ml, 1) + " ml left (layer " +
               String(current_layer) + "/" + String(layer_counter);
  if (minsToStop > 0) msg += ", ~" + String(minsToStop) + " min to stop";
  msg += "). Refill when you can.";
  telegramNotify(msg);
}

void tgNotifyCanceled() {
  if (!tgEnabled && !waEnabled && !dcEnabled) return;
  uint32_t secs = printStartMs ? (millis() - printStartMs) / 1000UL : 0;
  String msg = "Print canceled";
  // current_layer 0 = canceled during homing, before any layer - skip x/y.
  if (current_layer > 0 && layer_counter > 0)
    msg += " at layer " + String(current_layer) + "/" + String(layer_counter);
  if (secs) msg += " after " + formatDuration(secs);
  msg += ".";
  telegramNotify(msg);
}

// 0.17: power came back and a print was interrupted mid-run. Sent once per boot
// from network_setup() after WiFi is up (resumeLayer/Total/Folder were filled by
// resumeLoad() at boot). Lets a user who is away know to resume (screen prompt +
// dashboard Resume). Same opt-in channel as the other notifications.
void tgNotifyPowerRestored() {
  if (!tgEnabled && !waEnabled && !dcEnabled) return;
  String msg = "Power restored - print interrupted at layer " +
               String(resumeLayer) + "/" + String(resumeTotal);
  if (resumeFolder[0]) msg += " (" + String(resumeFolder) + ")";
  msg += ". Resume from the dashboard or the printer.";
  telegramNotify(msg);
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
