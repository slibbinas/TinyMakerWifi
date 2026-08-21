# TinyMaker Live gateway — contract (draft v1)

Status: **draft**, branch `feature/connect-live` (0.18). Firmware side lives in
`src/TinyMakerGateway.ino`; the server is a separate companion project
(`CONTRIBUTING.md`: this repo stays firmware-only).

Goal: see and control the printer **from the internet**, without port forwarding
and without touching print reliability.

---

## 1. Shape

The printer is always the client. It POSTs a small status frame on a timer and
the reply carries any queued commands. Nothing listens on the internet, so there
is no inbound port, no NAT traversal and no certificate on the printer.

```
printer ──POST /v1/beat (status + HMAC)──▶  gateway  ◀── browser (HTTPS)
        ◀──── 200 {commands:[...]} ──────
```

## 2. Why plain HTTP, not TLS

TLS is impossible while printing: there is no PSRAM, idle `maxAllocHeap` sits
around 110 KB, and the 30 KB print-time preview cache is only safe because
"print loops only service plain HTTP (no TLS)" (`src/Network.ino`, preview cache
comment). A handshake mid-print risks a heap failure during a job that runs for
hours.

So the transport is plain HTTP and **every frame is signed** (§4). The signature
is what makes commands trustworthy; the status payload itself is not secret
(layer counter, progress, model name). Browsers still reach the gateway over
normal HTTPS — only the printer↔gateway hop is plain.

Operators who want the hop encrypted can point `gatewayBaseUrl` at an `https://`
URL: the firmware will use TLS **while idle** and skip beats while printing.

## 3. Endpoints (server side)

### `POST /v1/beat`

Request headers:

| Header | Value |
|---|---|
| `Content-Type` | `application/json` |
| `X-TM-Device` | device id — `connectHardwareId()`, the eFuse MAC as hex |
| `X-TM-Seq` | monotonic counter, decimal (replay guard, §4) |
| `X-TM-Sig` | `HMAC-SHA256(deviceKey, seq + "." + body)`, lowercase hex |

Request body (compact by design — not the full `/api/status`):

```json
{"v":1,"st":"printing","by":1,"ly":42,"lt":480,"rs":5400,"ml":12.4,
 "mo":"skull","fw":"0.18.0","up":98311,"hp":121400}
```

| Field | Meaning |
|---|---|
| `v` | payload version |
| `st` | state text (`idle`, `printing`, `paused`, …) |
| `by` | busy flag, 0/1 |
| `ly` / `lt` | current layer / total layers |
| `rs` | remaining seconds (0 = unknown) |
| `ml` | resin used, ml |
| `mo` | model name (omitted when idle) |
| `fw` | firmware version |
| `up` | uptime seconds |
| `hp` | free heap (diagnostics) |

Response `200`:

```json
{"ok":true,"cmds":[{"id":"c17","cmd":"pause"}],"next":30}
```

- `cmds` — zero or more queued commands, oldest first. v1 set: `pause`,
  `resume`, `stop`. The printer executes them from `loop()`, never inside the
  HTTP call.
- `next` — seconds the server suggests until the next beat; the printer clamps
  it to its own floor/ceiling, so a broken server cannot make it hammer.
- Commands are acknowledged by their `id` on the **next** beat
  (`"ack":["c17"]`), so a lost reply cannot silently drop a command. The server
  re-sends anything unacknowledged.

Errors: `401` bad signature, `409` unknown device, `429` rate limited. The
printer treats any failure as "skip this beat" and backs off.

### `POST /v1/claim` (pairing, §5)

Body: `code`, `device`, `name`. Returns the device key once.

## 4. Signing and replay

`X-TM-Sig = HMAC-SHA256(deviceKey, X-TM-Seq + "." + rawBody)`, hex.

- `deviceKey` is a shared secret stored in the printer's NVS (`tmgKey`), never
  echoed back to the browser (same rule the MQTT password and Connect token
  already follow).
- `X-TM-Seq` is a counter persisted in NVS. The server rejects a `seq` it has
  already seen for that device, so a captured frame cannot be replayed. The
  counter is written every 32 beats (flash wear) and jumped forward by 32 on
  boot, so a power loss can never reuse a number.
- HMAC-SHA256 comes from mbedtls, already in the ESP32 core — no new dependency
  (`mbedtls/sha256.h` is used today for the anonymous stats hash).

## 5. Pairing

1. The dashboard (on the LAN) shows a QR / short code from the gateway.
2. The user opens it on a phone, signed in to their account, and confirms.
3. The printer calls `POST /v1/claim` with the code and stores the returned
   `deviceKey` + device id in NVS.

The key never travels to the browser and never leaves NVS afterwards.

## 6. Timing rules the firmware guarantees

| Situation | Beat interval | HTTP timeout |
|---|---|---|
| Idle | 30 s | 2500 ms |
| Printing | 60 s | 1200 ms |
| Failing | 30 s → ×2 → capped 30 min | as above |

While printing, a beat is only sent from **one safe point**: right after the
layer's exposure ends and before the peel move starts. A short pause there is
harmless (resin settling), it never overlaps UV exposure, and it never sits
inside a stepper move. The exposure wait loop services HTTP only
(`src/UVLED.ino`), so a beat cannot reach it by construction.

A beat is skipped, never queued, when the printer is mid-move, unpacking, or
homing. Missing one is invisible to the user; delaying a layer is not.

## 7. Free vs paid (product note)

Suggested split: free = one printer, read-only status; paid = remote commands,
uploads from anywhere, several printers, print history. The firmware does not
implement tiers — the gateway simply stops issuing commands for a free device.
Keeping the split server-side means no firmware release is needed to change it.

## 8. Safety

Remote start of a print (UV + motion with nobody in the room) is deliberately
**out of v1**. The v1 command set only pauses, resumes or stops work the user
already started. Anything that begins a job needs a separate decision, an
explicit warning, and probably a physical confirmation at the printer.
