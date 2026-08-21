/**
 * @file TinyMakerGateway.ino
 * @brief TinyMaker Live gateway - remote status and commands over the internet.
 *
 * The printer is always the client: it POSTs a small signed status frame on a
 * timer and the reply carries queued commands. Nothing listens on the internet,
 * so there is no inbound port and no NAT traversal to explain to anyone.
 *
 * Separate from TinyMakerConnect.ino on purpose - different server, different
 * secret, different failure domain. The wire contract is docs/gateway-spec.md.
 */

#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK

#include "mbedtls/md.h"

// Beat cadence. Idle is chatty enough for a dashboard to feel live; printing is
// deliberately slower and on a tighter timeout, because every millisecond spent
// here is a millisecond the print loop is not running.
static const unsigned long GATEWAY_IDLE_MS      = 30000UL;
static const unsigned long GATEWAY_PRINT_MS     = 60000UL;
static const uint16_t      GATEWAY_IDLE_TIMEOUT = 2500;
static const uint16_t      GATEWAY_PRINT_TIMEOUT= 1200;
static const uint8_t       GATEWAY_SEQ_STRIDE   = 32;   // NVS writes per counter step
static const uint8_t       GATEWAY_MAX_FAILS    = 6;

unsigned long gatewayNextBeatMs = 0;
unsigned long gatewayBackoffMs = 0;     // 0 = healthy, otherwise the current penalty
uint8_t gatewayFailStreak = 0;
String gatewayPendingAck = "";          // ids executed since the last beat

bool gatewayConfigured() {
  return gatewayBaseUrl.length() > 0 && gatewayDeviceKey.length() > 0;
}

bool gatewayRuntimeEnabled() {
  return gatewayEnabled && gatewayConfigured() && WiFi.status() == WL_CONNECTED;
}

String gatewayHmacHex(const String &key, const String &message) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return "";
  unsigned char digest[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  // 1 = HMAC mode; without it setup() prepares a plain digest and the hmac_*
  // calls below fail.
  if (mbedtls_md_setup(&ctx, info, 1) != 0) {
    mbedtls_md_free(&ctx);
    return "";
  }
  mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char *)message.c_str(), message.length());
  mbedtls_md_hmac_finish(&ctx, digest);
  mbedtls_md_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", digest[i]);
  hex[64] = 0;
  return String(hex);
}

// The counter must never repeat: the server rejects a replayed seq, and a
// printer that reuses one locks itself out until someone re-pairs it. Writing
// every beat would burn flash, so we persist in strides and loadDeviceConfig()
// jumps a whole stride ahead on boot - a power loss costs a few unused numbers,
// never a reused one.
void gatewayNextSeq() {
  gatewaySeq++;
  if (gatewaySeq - gatewaySeqPersisted >= GATEWAY_SEQ_STRIDE) {
    gatewaySeqPersisted = gatewaySeq;
    sysPrefs.begin("tinymaker", false);
    sysPrefs.putULong("tmgSeq", gatewaySeq);
    sysPrefs.end();
  }
}

// Deliberately not /api/status: that payload is ~1.5 KB of dashboard detail,
// and this one rides a print loop. Short keys, only what a remote viewer can
// act on.
String gatewayBuildPayload() {
  bool busy = printerBusy();
  String out = "{\"v\":1,\"st\":\"";
  out += jsonEscape(printerStateText());
  out += "\",\"by\":";
  out += busy ? "1" : "0";
  out += ",\"ly\":";
  out += String(busy ? current_layer : 0);
  out += ",\"lt\":";
  out += String(busy ? layer_counter : 0);
  out += ",\"rs\":";
  out += String(busy ? remainingPrintSecs() : 0);
  out += ",\"ml\":";
  out += String(resinUsedMl, 1);
  if (busy && strlen(foldersel_long) > 0) {
    out += ",\"mo\":\"";
    out += jsonEscape(String(foldersel_long));   // char[] on this side, not a String
    out += "\"";
  }
  out += ",\"fw\":\"";
  out += connectFirmwareVersion();
  out += "\",\"up\":";
  out += String(millis() / 1000);
  out += ",\"hp\":";
  out += String(ESP.getFreeHeap());
  if (gatewayPendingAck.length() > 0) {
    out += ",\"ack\":[";
    out += gatewayPendingAck;
    out += "]";
  }
  out += "}";
  return out;
}

// One command from the reply. Returns true if it was accepted, so the caller
// can acknowledge it and the server can stop re-sending.
bool gatewayRunCommand(const String &cmd) {
  // Remote control obeys the same switch as the dashboard: with Web control off
  // the printer answers to its own buttons only.
  if (!webDashboardRuntimeEnabled()) return false;
  String error;
  if (cmd == "pause")  return requestPrintPause(error);
  if (cmd == "resume") return requestPrintResume(error);
  if (cmd == "stop")   return requestPrintStop(error);
  // Unknown verbs are acknowledged, not retried: an older firmware must not
  // trap a newer server in a resend loop it can never satisfy.
  return true;
}

void gatewayHandleCommands(const String &response) {
  gatewayPendingAck = "";
  int pos = response.indexOf("\"cmds\"");
  if (pos < 0) return;
  // Walk the array by hand - ArduinoJson is not a dependency here and the
  // shape is fixed by our own contract.
  while (true) {
    int idPos = response.indexOf("\"id\":\"", pos);
    if (idPos < 0) break;
    idPos += 6;
    int idEnd = response.indexOf('"', idPos);
    if (idEnd < 0) break;
    String id = response.substring(idPos, idEnd);

    int cmdPos = response.indexOf("\"cmd\":\"", idEnd);
    if (cmdPos < 0) break;
    cmdPos += 7;
    int cmdEnd = response.indexOf('"', cmdPos);
    if (cmdEnd < 0) break;
    String cmd = response.substring(cmdPos, cmdEnd);

    if (gatewayRunCommand(cmd)) {
      if (gatewayPendingAck.length() > 0) gatewayPendingAck += ",";
      gatewayPendingAck += "\"" + jsonEscape(id) + "\"";
    }
    pos = cmdEnd;
    if (gatewayPendingAck.length() > 200) break;   // sanity bound
  }
}

// Blocking, but bounded: see the timeout constants. Returns false on any
// failure - a missed beat is invisible to the user, a stalled print is not.
bool gatewayBeat(bool busy) {
  String base = gatewayBaseUrl;
  base.trim();
  while (base.endsWith("/")) base.remove(base.length() - 1);
  if (base.length() == 0) return false;

  String body = gatewayBuildPayload();
  gatewayNextSeq();
  String seq = String(gatewaySeq);
  String sig = gatewayHmacHex(gatewayDeviceKey, seq + "." + body);
  if (sig.length() == 0) return false;

  uint16_t timeout = busy ? GATEWAY_PRINT_TIMEOUT : GATEWAY_IDLE_TIMEOUT;
  String url = base + "/v1/beat";

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool started;
  if (url.startsWith("https://")) {
    // TLS is for idle beats only - the handshake's heap spike is exactly what
    // the print-time preview cache budget assumes never happens (Network.ino).
    if (busy) return false;
    secure.setInsecure();
    secure.setTimeout(timeout / 1000 + 1);
    started = http.begin(secure, url);
  } else {
    started = http.begin(plain, url);
  }
  if (!started) return false;

  // Both halves matter: setTimeout() alone still lets a dead host hold us in
  // the TCP connect.
  http.setConnectTimeout(timeout);
  http.setTimeout(timeout);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-TM-Device", connectHardwareId());
  http.addHeader("X-TM-Seq", seq);
  http.addHeader("X-TM-Sig", sig);

  int code = http.POST(body);
  String response = code > 0 ? http.getString() : String("");
  http.end();

  if (code < 200 || code >= 300) {
    gatewayLastStatus = code < 0 ? "gateway unreachable" : ("gateway HTTP " + String(code));
    return false;
  }

  gatewayLastStatus = "connected";
  gatewayHandleCommands(response);
  return true;
}

// Shared by both entry points below. `busy` decides the cadence, the timeout and
// whether TLS is allowed at all.
void gatewayTick(bool busy) {
  if (!gatewayRuntimeEnabled()) return;
  if ((long)(millis() - gatewayNextBeatMs) < 0) return;

  bool ok = gatewayBeat(busy);
  unsigned long base = busy ? GATEWAY_PRINT_MS : GATEWAY_IDLE_MS;
  if (ok) {
    gatewayFailStreak = 0;
    gatewayBackoffMs = 0;
    gatewayNextBeatMs = millis() + base;
    return;
  }

  // Same shape as the Connect sync backoff: a dead server must not be retried
  // on the normal cadence, because every attempt is a blocking call.
  if (gatewayFailStreak < GATEWAY_MAX_FAILS) gatewayFailStreak++;
  gatewayBackoffMs = base << (gatewayFailStreak - 1);
  if (gatewayBackoffMs > 1800000UL) gatewayBackoffMs = 1800000UL;   // cap at 30 min
  gatewayNextBeatMs = millis() + gatewayBackoffMs;
}

// Idle path: called from network_loop(), which the print loop never reaches
// except through network_service_window() - hence the busy guard here.
void gatewayLoop() {
  if (printerBusy()) return;
  gatewayTick(false);
}

// Print path: called from ONE safe point in the layer cycle - after the layer
// has finished curing and before the peel move starts. A short pause there is
// harmless (the resin is settling anyway), it can never overlap UV exposure,
// and it is never inside a stepper move.
void gatewayPrintTick() {
  if (!printerBusy()) return;
  gatewayTick(true);
}

#endif
