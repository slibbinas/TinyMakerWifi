# TinyMaker Live gateway (Cloudflare Worker)

The server half of [`docs/gateway-spec.md`](../../docs/gateway-spec.md). Lets a
printer be watched and paused from anywhere, without opening anything on the
owner's router: the printer is always the client and this worker is the only
thing on the internet.

Separate from `tinymaker-feedback` on purpose — a bug in the feedback inbox
must never be able to stall a beat.

## Routes

| Route | Who calls it |
|---|---|
| `POST /gw/v1/claim` | the printer, once, trading a claim code for its key |
| `POST /gw/v1/beat` | the printer, on a timer: signed status in, commands out |
| `GET /gw/p/<id>?k=<viewKey>` | the owner's phone |
| `POST /gw/p/<id>/cmd?k=..&cmd=pause\|resume\|stop` | that page's buttons |
| `POST /gw/admin/new?key=<ADMIN_KEY>&name=..` | you, minting a claim code |

## Deploy

```bash
cd Firmware_Hosting/gateway-worker
npx wrangler kv namespace create GATEWAY     # put the id into wrangler.jsonc
npx wrangler secret put ADMIN_KEY            # any long random string
npx wrangler deploy
```

### The one setting that is easy to miss

The printer speaks **plain HTTP and does not follow redirects**. If the zone
forces HTTPS on `/gw/*`, every beat gets a 301 the firmware will not act on and
the printer looks permanently offline.

In the Cloudflare dashboard add a rule for `tinymakerwifi.com/gw/*` with
**Automatic HTTPS Rewrites: Off** and **Always Use HTTPS: Off**. The phone page
is fine over HTTPS — only the beat path needs this.

(Plain HTTP is a deliberate trade, not an oversight: TLS cannot run mid-print on
an ESP32 without PSRAM, so both directions are signed with HMAC-SHA256 instead.
Status is not secret; commands cannot be forged or replayed.)

## Pairing a printer

1. `curl -X POST "https://tinymakerwifi.com/gw/admin/new?key=$ADMIN_KEY&name=Bench"`
   → returns a claim code, valid for an hour, single use.
2. The printer calls `/gw/v1/claim` with that code and stores the returned
   `deviceKey` in NVS. (Until the dashboard has a pairing button, do this by
   hand: paste the key into Settings → Network → Remote access, with the
   gateway URL `http://tinymakerwifi.com/gw`.)
3. The response also carries `publicId` and `viewKey` — the phone page is
   `https://tinymakerwifi.com/gw/p/<publicId>?k=<viewKey>`. Anyone with that
   link can watch and control the printer, so treat it as a password.

## Free plan limits

Workers KV stops accepting writes at ~1000/day. Every accepted beat writes once
(the `seq` replay guard has to be persisted or it is not a guard), so the reply
sets the pace: **120 s idle, 60 s printing** ≈ 700–900 writes/day for one
printer. That is one printer, comfortably; a second one needs the $5 Workers
paid plan, which also removes the ceiling entirely.

If beats start failing with KV write errors, that is the limit — not the
printer.

## Tests

```bash
node test/beat.test.mjs
```

No dependencies and no wrangler needed: the suite drives `fetch()` directly with
a Map standing in for KV. It covers what actually matters — the signature in
both directions, a tampered body, a replayed `seq`, the view-key gate, and the
command queue's ack round-trip.
