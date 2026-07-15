# TinyMaker Connect offload — spec draft

Status: **draft for discussion** (V. Sidlauskas). Started 2026-07-13 after the
team-chat agreement; rewritten 2026-07-15 once measurements settled the
architecture. Audience: @Briadark (Connect server), @Tann2019.

Goal we already agree on: **Connect can grow as big as we like on the server
while the printer's flash footprint stays flat**, and Brian can ship Connect UI
without waiting for a firmware release.

---

## 1. Why this is worth doing

| | Today |
|---|---|
| Flash | 73.1% of 4 MB used. Every Connect screen competes with print features for the same bytes. |
| Iteration | A Connect UI tweak needs: firmware PR → review → release → every user OTAs. Brian's own product, gated on my calendar. |
| Transfers | Importing a model streams it **twice**: server → browser → printer. |

None of that is anybody's fault — Connect started inside the firmware because
that was the fastest way to prove it. It works. It just doesn't scale.

---

## 2. The constraint that decides the architecture

The obvious design — "Connect site talks to the printer's LAN API directly" —
**cannot work**, and it's worth stating plainly so nobody spends a weekend on it.

Measured 2026-07-15, Chrome 150:

```
page https://tinymakerwifi.com  →  fetch('http://192.168.1.138/api/status')
  ⇒ blocked: "Mixed Content: The page was loaded over HTTPS, but requested an
    insecure resource ... This request has been blocked."
```

The printer serves plain HTTP on the LAN and has no certificate, so **no HTTPS
page may call it from the browser** — CORS never even gets a say. This kills the
"printer as a REST API for the site" model regardless of how we configure it.
(It also means the CORS header currently sitting on `/api/boot-anim/install`
does nothing for an HTTPS caller — worth knowing before we rely on it.)

Two escape routes exist and both are worse: shipping a self-signed cert (browser
warnings on every visit) or leaning on Private Network Access (Chrome-only,
still moving). So we design around the constraint instead.

---

## 3. Proposed architecture: hosted UI in an iframe + a printer-side bridge

```
┌─ dashboard (http://tinymaker.local) ─ served by the printer ───────────┐
│                                                                        │
│  Connect tab                                                           │
│  ┌─ <iframe src="https://connect.tinymakerwifi.com/embed"> ─────────┐  │
│  │                                                                  │  │
│  │   Connect UI — 100% Brian's: models, previews, leaderboard,      │  │
│  │   boot-anim library, ratings, whatever comes next.               │  │
│  │   Ships when Brian deploys. No firmware release. No flash.       │  │
│  │                                                                  │  │
│  └──────────── postMessage bridge (versioned command set) ──────────┘  │
│                              │                                         │
│  dashboard JS = the gate: validates every command, then calls the      │
│  printer's own API same-origin (no mixed content, no CORS)             │
└────────────────────────────┬───────────────────────────────────────────┘
                             │  printer-pull for anything big
                             ▼
                  https://connect.tinymakerwifi.com/model/<id>.zip
```

**Why an iframe rather than today's `<script src=connect>` into our page.**
Today's hosted script runs *as* the dashboard: it can read the Connect token,
start prints, rewrite settings, anything. That's not a statement about trust —
it's that neither of us can audit or bound it, and it only gets harder as the
Connect UI grows. An iframe from a different origin can't touch the dashboard's
DOM or its APIs, so the blast radius becomes exactly the command list in §4.
It also ends the DOM coupling we're papering over today (`tidyConnectHosted()`
node-moves the hosted header; `.connectTabs` has to inherit our classes) — with
an iframe there is nothing to move and nothing to restyle.

**What each side owns**

| Printer firmware | Connect server |
|---|---|
| Connect settings + registration (secrets stay in NVS) | all model browsing, previews, ratings, "my models" management |
| The bridge and its command allowlist | leaderboard, boot-anim library, community pages |
| `/api/models/install`, `/api/boot-anim/install` (printer-pull) | serving `/embed` and the model/anim files |
| `/api/status`, `/api/files` (so the UI can say "already on SD") | look, layout, copy, iteration speed |
| Share-from-SD (only the printer can read layer PNGs) | |

---

## 4. The bridge contract

The iframe posts a message; the dashboard validates and answers. Nothing else
is reachable — no ad-hoc endpoint access, no token handout.

```js
// iframe → dashboard
{ v: 1, id: "<uuid>", cmd: "<name>", args: { ... } }
// dashboard → iframe
{ v: 1, id: "<uuid>", ok: true, data: { ... } }
{ v: 1, id: "<uuid>", ok: false, error: "printer busy" }
```

Both sides pin `event.origin` to the configured `connectBaseUrl` and drop
anything else. `v` is the contract version: the dashboard refuses a `v` it
doesn't implement, and the hosted UI can feature-detect via `hello`.

**v1 command set**

| cmd | args | does | gated by |
|---|---|---|---|
| `hello` | — | returns `{fw, build, v, caps[], printerName, connectId}` | — |
| `status` | — | the same object as `/api/status` | — |
| `files` | — | SD model list (so the UI can mark "on SD") | — |
| `model.install` | `{url, name, meta}` | **printer-pull**: the printer downloads and unpacks the ZIP itself | web control, idle, SD ready |
| `bootanim.install` | `{url, name, meta}` | same for a `.tmb` | web control, idle |
| `print.start` | `{name}` | starts a model already on SD | web control, idle, user confirm |
| `ui.resize` | `{height}` | iframe tells us how tall it is | — |
| `ui.theme` | — | dashboard pushes `light`/`dark` on load and on change | — |

Rules that don't change: **Web control off ⇒ every mutating command refuses**;
**printing ⇒ 409**; destructive things ask the user in *our* modal, not the
iframe's. `model.install` and `print.start` always confirm — the click came
from a page we don't control.

The publish token stays on the printer and is **never** handed to the iframe.
When the hosted UI needs to authenticate to its own server it uses its own
session (§6).

---

## 5. Printer-pull, and why `Import` and `Install` should become one thing

We already have two flows that mean the same thing to a user — "put this on my
printer" — built two different ways:

- **Boot animations** — `POST /api/boot-anim/install {url}` → the printer
  downloads the file itself. One stream, progress is real, browser is free.
- **Models (Connect Import)** — the browser downloads the ZIP, then uploads it
  to the printer. Two streams, the phone is the bottleneck, and the progress bar
  can only report the upload half (which is why it looked frozen at 100% while
  the ESP unpacked — fixed in 0.15.0, but the shape of the problem stays).

`model.install` collapses them: the printer pulls from `url` exactly like it
pulls a `.tmb`, and progress comes from the device that's actually working.
Roughly half the bytes over the air, and phones stop mattering.

There's a third win that only showed up while measuring. Today the unpack runs
*inside* the `/upload` handler, so the ESP can't answer `/api/status` until it
finishes — the browser cannot show the layer count that the printer's own
screen is displaying at that very moment. Answer `202` first and unpack from
`loop()`, and that number becomes readable over HTTP like any other status.

Brian, you called this "might hit two birds with one stone" when we discussed
the import indicator — this is that stone.

**Firmware side** (new, mirrors `handleApiBootAnimInstall`):
```
POST /api/models/install
  body: url=<https url>&name=<slug>&meta=<json>
  gates: web control, !busy, SD ready, url must start with connectBaseUrl
  behaviour: stream → temp folder → unpack → atomic swap (same as /upload)
  reply: 202 immediately; progress readable in /api/status
```
The `url` allowlist matters: without it the endpoint is a "make my printer
download anything" button.

---

## 6. Open question for Brian: where does the identity live?

"My shared models" management needs the publish token **on the site**, but the
token lives in printer NVS and, in the iframe model, the iframe never sees it.

- **(a) Server session** — the user claims their printer on the site with the
  recovery code, and from then on the site knows who they are without the
  printer. Clean, survives the printer being off, needs server work.
- **(b) Bridge-issued scoped token** — the dashboard asks the printer for a
  short-lived, read-only token and hands *that* to the iframe.
  No server work, but it's still a credential crossing a boundary.

I lean (a): it's the only one that still works when someone browses Connect from
the office. Your call — it's your server, and it decides the shape of the login
UI you'd have to build anyway.

---

## 7. Migration

| Step | Who | What |
|---|---|---|
| **0.15.0** (shipping) | done | Connect tab already slim: Models + boot-anim install; leaderboard is a link. |
| **1** | Brian | `/embed` route: the current Connect UI, no printer calls, bridge-ready. Ship whenever — nothing depends on firmware. |
| **2** | Viktoras | `POST /api/models/install` (printer-pull) + `model.install` in the bridge. |
| **3** | both | Dashboard swaps `<script>` for `<iframe>`; `tidyConnectHosted()` and the class-sharing hacks are deleted. Contract v1 frozen. |
| **4** | Brian | Model browsing/management move into `/embed`; firmware Connect tab shrinks to registration + the frame. |
| **5** | Viktoras | Delete the dead firmware paths; measure the flash we got back. |

Steps 1–2 are independent and can run in parallel. Nothing here needs a big-bang
release: the bridge can go in while the hosted UI still does everything the old
way.

---

## 8. What I'd like feedback on

1. Does `/embed` + bridge fit how you'd want to build Connect, or does it box you
   in somewhere I can't see?
2. The v1 command list — anything missing that you'd need in the next few months?
   Cheaper to add now than to version later.
3. Identity: (a) or (b) in §6?
4. Anything in §2 you've solved differently already — if HTTPS→LAN works for you
   somewhere, I'd want to know, because it would reopen the simpler design.
