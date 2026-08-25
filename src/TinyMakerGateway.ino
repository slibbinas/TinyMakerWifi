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
 *
 * Three deliberate choices, all of them about not hurting a running print:
 *  - plain HTTP, never TLS. A handshake's heap spike is what the print-time
 *    preview cache budget assumes never happens (see Network.ino).
 *  - a hand-rolled request instead of HTTPClient, so the host is resolved while
 *    idle and the print-time beat talks to a cached IP with a real deadline.
 *    HTTPClient cannot do either: it resolves inside connect() (DNS is not
 *    covered by its timeout) and it silently drops a custom Host header.
 *  - both directions are signed. An unsigned reply would let anything on the
 *    path stop an eight-hour print with one forged packet.
 */

#ifndef ENABLE_NETWORK
#define ENABLE_NETWORK 1
#endif
#if ENABLE_NETWORK

#include "mbedtls/md.h"

static const unsigned long GATEWAY_IDLE_MS       = 30000UL;
static const unsigned long GATEWAY_PRINT_MS      = 60000UL;
static const uint16_t      GATEWAY_IDLE_TIMEOUT  = 2500;
static const uint16_t      GATEWAY_PRINT_TIMEOUT = 1200;
static const uint8_t       GATEWAY_SEQ_STRIDE    = 32;
static const uint8_t       GATEWAY_MAX_FAILS     = 6;
static const size_t        GATEWAY_MAX_BODY      = 1024;   // replies are tiny by contract
static const unsigned long GATEWAY_DNS_TTL_MS    = 600000UL;

unsigned long gatewayNextBeatMs = 0;
uint8_t gatewayFailStreak = 0;
unsigned long gatewayServerNextMs = 0;   // cadence the gateway asked for, clamped

// Parsed once from gatewayBaseUrl, refreshed whenever the setting changes.
String gatewayHost = "";
uint16_t gatewayPort = 80;
String gatewayPath = "";
String gatewayParsedFrom = "";      // the URL these three came from
IPAddress gatewayIp;
bool gatewayIpValid = false;
unsigned long gatewayIpAtMs = 0;

String gatewayPendingAck = "";

bool gatewayConfigured() {
  return gatewayBaseUrl.length() > 0 && gatewayDeviceKey.length() > 0;
}

bool gatewayRuntimeEnabled() {
  return gatewayEnabled && gatewayConfigured() && WiFi.status() == WL_CONNECTED;
}

// http:// only in v1. https would have to be idle-only anyway (no TLS during a
// print), and a half-available transport is worse than one honest rule: the
// HMAC on both directions is what makes the channel trustworthy, not the URL.
bool gatewayParseUrl() {
  if (gatewayParsedFrom == gatewayBaseUrl) return gatewayHost.length() > 0;
  gatewayParsedFrom = gatewayBaseUrl;
  gatewayHost = ""; gatewayPort = 80; gatewayPath = "";
  gatewayIpValid = false;

  String url = gatewayBaseUrl;
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  if (!url.startsWith("http://")) {
    gatewayLastStatus = "gateway URL must start with http://";
    return false;
  }
  url.remove(0, 7);

  int slash = url.indexOf('/');
  if (slash >= 0) { gatewayPath = url.substring(slash); url.remove(slash); }
  int colon = url.indexOf(':');
  if (colon >= 0) {
    long p = url.substring(colon + 1).toInt();
    if (p < 1 || p > 65535) { gatewayLastStatus = "bad gateway port"; return false; }
    gatewayPort = (uint16_t)p;
    url.remove(colon);
  }
  if (url.length() == 0) { gatewayLastStatus = "bad gateway URL"; return false; }
  gatewayHost = url;
  return true;
}

// Resolved while idle only. A print-time beat either has a cached address or
// skips - DNS is the one step whose duration we cannot bound.
bool gatewayResolveHost() {
  if (gatewayIpValid && millis() - gatewayIpAtMs < GATEWAY_DNS_TTL_MS) return true;
  IPAddress ip;
  if (!WiFi.hostByName(gatewayHost.c_str(), ip)) {
    gatewayLastStatus = "cannot resolve " + gatewayHost;
    return false;
  }
  gatewayIp = ip;
  gatewayIpValid = true;
  gatewayIpAtMs = millis();
  return true;
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

// Constant-time-ish compare. Not a real side-channel defence over the network,
// but free, and it keeps a lazy `==` from becoming the interesting part.
bool gatewaySigEquals(const String &a, const String &b) {
  if (a.length() != b.length() || a.length() == 0) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}

// The counter must never repeat: the server rejects a replayed seq, and a
// printer that reuses one locks itself out until someone re-pairs it. Writing
// every beat would burn flash, so we persist in strides - and loadDeviceConfig()
// writes the boot jump immediately, so a power loss costs unused numbers rather
// than reused ones.
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
  String out;
  out.reserve(256);
  out += "{\"v\":1,\"st\":\"";
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
  // Whether the printer would obey a command at all. Without this the phone
  // page offers Pause/Stop, the printer silently drops them (Web control is
  // off), and the ack still says 'done' - the person is told they stopped a
  // print that is still running. Sent every beat so the page can grey the
  // buttons out instead of lying about them.
  out += ",\"wc\":";
  out += webDashboardRuntimeEnabled() ? "1" : "0";
  if (gatewayPendingAck.length() > 0) {
    out += ",\"ack\":[";
    out += gatewayPendingAck;
    out += "]";
  }
  out += "}";
  return out;
}

// Every command is acknowledged, including ones we could not carry out. The
// server re-sends whatever is unacknowledged, so refusing to ack a "resume"
// that arrived while already printing would pin that command at the head of the
// queue forever - and a later "stop" behind it would never be delivered.
void gatewayRunCommand(const String &cmd) {
  // Remote control obeys the same switch as the dashboard: with Web control off
  // the printer answers to its own buttons only.
  if (!webDashboardRuntimeEnabled()) return;
  String error;
  if (cmd == "pause")       requestPrintPause(error);
  else if (cmd == "resume") requestPrintResume(error);
  else if (cmd == "stop")   requestPrintStop(error);
  // Unknown verbs fall through: an older firmware must not trap a newer server
  // in a resend loop it can never satisfy.
}

// The server sets the pace: it knows its own write budget and how many printers
// it is holding, and only it can slow everyone down during a busy hour. Clamped
// on this side so a broken or hostile reply can neither hammer the link nor
// silence the printer for a day - and the signature is already checked by the
// time we get here.
void gatewayParseNext(const String &response) {
  gatewayServerNextMs = 0;
  int pos = response.indexOf("\"next\":");
  if (pos < 0) return;
  long secs = atol(response.c_str() + pos + 7);
  if (secs < 10) secs = 10;
  if (secs > 900) secs = 900;
  gatewayServerNextMs = (unsigned long)secs * 1000UL;
}

void gatewayHandleCommands(const String &response) {
  gatewayPendingAck = "";
  int pos = response.indexOf("\"cmds\"");
  if (pos < 0) return;
  uint8_t guard = 0;
  while (guard++ < 8) {
    int idPos = response.indexOf("\"id\":\"", pos);
    if (idPos < 0) break;
    idPos += 6;
    int idEnd = response.indexOf('"', idPos);
    if (idEnd < 0 || idEnd - idPos > 32) break;      // ids are short by contract
    String id = response.substring(idPos, idEnd);

    int cmdPos = response.indexOf("\"cmd\":\"", idEnd);
    if (cmdPos < 0) break;
    cmdPos += 7;
    int cmdEnd = response.indexOf('"', cmdPos);
    if (cmdEnd < 0 || cmdEnd - cmdPos > 24) break;
    String cmd = response.substring(cmdPos, cmdEnd);

    gatewayRunCommand(cmd);
    if (gatewayPendingAck.length() > 0) gatewayPendingAck += ",";
    gatewayPendingAck += "\"" + jsonEscape(id) + "\"";
    pos = cmdEnd;
  }
}

// Hand-rolled so the whole exchange sits under one deadline and the request can
// go to a cached IP while still sending the right Host header.
bool gatewayBeat(bool busy) {
  if (!gatewayParseUrl()) return false;
  if (!gatewayIpValid) {
    // Resolving is idle-only work; a print-time beat waits for the next idle
    // window rather than risk an unbounded lookup between two layers.
    if (busy) return false;
    if (!gatewayResolveHost()) return false;
  }

  // One budget for the whole exchange, split in two on purpose: connect may
  // spend at most half of it, so a slow-but-alive server can never eat the
  // deadline before the reply has been read. Without the split a sluggish
  // gateway looks exactly like a dead one - every print-time beat times out
  // and the printer backs off from a server that was answering all along.
  uint16_t budget = busy ? GATEWAY_PRINT_TIMEOUT : GATEWAY_IDLE_TIMEOUT;
  uint16_t connectBudget = budget / 2;
  unsigned long deadline = millis() + budget;

  String body = gatewayBuildPayload();
  gatewayNextSeq();
  String seq = String(gatewaySeq);
  String sig = gatewayHmacHex(gatewayDeviceKey, seq + "." + body);
  if (sig.length() == 0) return false;

  WiFiClient client;
  client.setTimeout(budget / 1000 + 1);
  if (!client.connect(gatewayIp, gatewayPort, connectBudget)) {
    gatewayIpValid = false;                 // stale address: re-resolve when idle
    gatewayLastStatus = "gateway unreachable";
    return false;
  }

  // Headers and body go out as ONE write. Two client.print() calls can each
  // sit in the core's write-retry loop (10 rounds of a 1 s select, where the
  // socket timeout does not apply), which would add seconds to a beat that is
  // supposed to be bounded by `budget` - in the middle of a layer cycle. One
  // buffer is not a hard guarantee, but it halves the exposure and costs
  // nothing.
  String req;
  req.reserve(320 + body.length());
  req += "POST " + (gatewayPath.length() ? gatewayPath : String("")) + "/v1/beat HTTP/1.1\r\n";
  req += "Host: " + gatewayHost + "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "X-TM-Device: " + connectHardwareId() + "\r\n";
  req += "X-TM-Seq: " + seq + "\r\n";
  req += "X-TM-Sig: " + sig + "\r\n";
  req += "Connection: close\r\n\r\n";
  req += body;
  client.print(req);

  // --- response, all of it under `deadline` --------------------------------
  String status = "";
  String respSig = "";
  size_t contentLength = 0;
  bool headersDone = false;
  String line = "";
  String respBody = "";
  bool first = true;

  while (client.connected() || client.available()) {
    if ((long)(millis() - deadline) >= 0) {
      client.stop();
      gatewayLastStatus = "gateway timed out";
      return false;
    }
    if (!client.available()) { delay(1); continue; }

    if (!headersDone) {
      char c = client.read();
      if (c == '\n') {
        line.trim();
        if (first) { status = line; first = false; }
        else if (line.length() == 0) {
          headersDone = true;
          // One allocation for the whole body, now that Content-Length is known.
          // (String::capacity() cannot be used as an 'already reserved' test - a
          // short string lives inside the object and reports non-zero capacity.)
          respBody.reserve((contentLength ? contentLength : 256) + 1);
        }
        else if (line.startsWith("X-TM-RSig:") || line.startsWith("x-tm-rsig:")) {
          respSig = line.substring(10); respSig.trim();
        } else if (line.startsWith("Content-Length:") || line.startsWith("content-length:")) {
          long n = line.substring(15).toInt();
          if (n < 0 || n > (long)GATEWAY_MAX_BODY) {
            client.stop();
            gatewayLastStatus = "gateway reply too large";
            return false;
          }
          contentLength = (size_t)n;
        }
        line = "";
      } else if (c != '\r' && line.length() < 200) {
        line += c;
      }
      continue;
    }

    // Body: bounded by Content-Length, and by GATEWAY_MAX_BODY if the server
    // omitted it (chunked replies are not part of the contract). Read in
    // blocks into a String reserved once: appending a char at a time would
    // reallocate up to a thousand times per beat, and this runs on the print
    // path, where a fragmented heap is the failure we spend the most effort
    // avoiding.
    uint8_t chunk[64];
    size_t room = GATEWAY_MAX_BODY - respBody.length();
    if (contentLength > 0) {
      size_t left = contentLength - respBody.length();
      if (left < room) room = left;
    }
    size_t want = client.available();
    if (want > sizeof(chunk)) want = sizeof(chunk);
    if (want > room) want = room;
    if (want == 0) break;
    int got = client.read(chunk, want);
    if (got <= 0) break;
    for (int i = 0; i < got; i++) respBody += (char)chunk[i];
    if (respBody.length() >= GATEWAY_MAX_BODY) break;
    if (contentLength > 0 && respBody.length() >= contentLength) break;
  }
  client.stop();

  if (status.indexOf(" 200") < 0) {
    gatewayLastStatus = status.length() ? status : "no reply from gateway";
    return false;
  }

  // The reply decides whether a print keeps running, so it has to prove it came
  // from the gateway. Signed over the same seq, so a valid reply cannot be
  // replayed onto a later beat either.
  String expect = gatewayHmacHex(gatewayDeviceKey, seq + "." + respBody);
  if (!gatewaySigEquals(expect, respSig)) {
    gatewayLastStatus = "gateway reply failed signature check";
    return false;
  }

  gatewayLastStatus = "connected";
  gatewayParseNext(respBody);
  gatewayHandleCommands(respBody);
  return true;
}

void gatewayTick(bool busy) {
  if (!gatewayRuntimeEnabled()) return;
  if ((long)(millis() - gatewayNextBeatMs) < 0) return;

  unsigned long base = busy ? GATEWAY_PRINT_MS : GATEWAY_IDLE_MS;
  if (gatewayBeat(busy)) {
    gatewayFailStreak = 0;
    gatewayNextBeatMs = millis() + (gatewayServerNextMs ? gatewayServerNextMs : base);
    return;
  }

  // Same shape as the Connect sync backoff: a dead server must not be retried
  // on the normal cadence, because every attempt is a blocking call.
  if (gatewayFailStreak < GATEWAY_MAX_FAILS) gatewayFailStreak++;
  unsigned long penalty = base << (gatewayFailStreak - 1);
  if (penalty > 1800000UL) penalty = 1800000UL;      // cap at 30 min
  gatewayNextBeatMs = millis() + penalty;
}

// Idle path, plus a paused print: the motor is stopped and the UV LED is off
// while paused, so a beat there is as safe as an idle one - and without it a
// remote "pause" could never be followed by a remote "resume".
//
// But "paused" has to mean the plate has stopped, not that the flag went up.
// Pause is accepted from inside the lift loop (Motor.ino sets print_paused
// while stepper.distanceToGo() != 0), and that loop serves the network every
// 300 ms - so the flag can be set with the plate still mid travel. A beat there
// would hold stepper.run() for the whole exchange and freeze the plate in the
// middle of a peel with the coils energised. distanceToGo() == 0 is the
// difference between "asked to pause" and "standing still".
void gatewayLoop() {
  if (printerBusy() && !(print_paused && stepper.distanceToGo() == 0)) return;
  gatewayTick(printerBusy());
}

// Print path: called from ONE safe point in the layer cycle - after the layer
// has finished curing and before the peel move starts. A short pause there is
// harmless (the resin is settling anyway), it can never overlap UV exposure,
// and it is never inside a stepper move.
void gatewayPrintTick() {
  if (!printerBusy()) return;
  gatewayTick(true);
}

// Settings changed: re-parse, drop the cached address and beat promptly, so a
// corrected URL or key is visible in seconds instead of after a 30 min backoff.
void gatewayConfigChanged() {
  gatewayParsedFrom = "";
  gatewayIpValid = false;
  gatewayFailStreak = 0;
  gatewayNextBeatMs = millis();
}

#endif
