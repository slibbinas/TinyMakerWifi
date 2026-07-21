# TinyMakerWiFi LAN API (DRAFT v0 — 0-11)

> Status: **draft for review** — generated from the endpoint registrations in
> `src/Network.ino` (firmware 0.15.8, experimental branch). Promised in
> [Issue #12](https://github.com/slibbinas/TinyMakerWifi/issues/12) as the
> versioned contract the Connect offload spec builds on.

## Transport & conventions

- Base URL: `http://tinymaker.local` (mDNS) or the printer's IP. Plain HTTP,
  LAN only — the printer never listens on the internet.
- JSON responses carry `"ok":true`; the dashboard treats a 200 **without** it
  as a truncated body. Errors answer `{"error":"..."}` with an HTTP code.
- **Busy gate**: while printing, every SD-touching endpoint answers
  `409 {"error":"printer busy"}` (the SD bus feeds the print). Status and
  print-control endpoints keep working.
- **Web control gate**: with *Web control* off (printer → System → Advanced →
  Network), state-changing endpoints answer `403`; viewing (status, files
  list, previews) keeps working. Slicer upload and MQTT are not gated.
- **Update gate**: web firmware flashing works while idle with Web control on,
  or whenever the printer sits on its Update screen (`otaWebAllowed()`); the
  dev espota path additionally requires the Update screen to be open.

## Versioning policy

The contract rides the firmware's SemVer (`FIRMWARE_VERSION`). Additive
changes (new fields, new endpoints) bump minor and are safe to ignore.
Renames/removals are breaking and only land with a major bump, called out in
the changelog. Consumers (Connect bridge, dashboards) should ignore unknown
JSON fields.

## Discovery & status

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/version` | GET | OctoPrint-compatible identity — lets PrusaSlicer's "Send to printer" test pass |
| `/api/status` | GET | The heartbeat: everything the dashboard shows, one poll |

Key `/api/status` fields (additive; ignore unknowns):

| Field | Meaning |
|---|---|
| `firmwareVersion`, `firmwareBuild`, `buildDate` | SemVer, git rev, compile moment |
| `busy`, `paused`, `pausing`, `resuming`, `stopping` | print lifecycle booleans |
| `state`, `stateCode` | human text + numeric state |
| `canPause`, `canResume`, `canStop` | which controls are valid right now |
| `phaseTotalMs`, `phaseElapsedMs` | live phase countdown (0 = unknown) |
| `layerHeight`, `dryRun` | active settings snapshot |
| `wifiRssi`, `wifiText`, `ip` | connectivity |
| `sdReady`, `sdText` | SD card state (`Locked` while printing) |
| `lifetimePrintSecs/Time`, `uvLedSecs/Time` | lifetime counters |
| `bootReason`, `lastCrash{reason,layer,epoch}` | reset-reason telemetry (0-30); `lastCrash` null when none recorded |
| `model`, `currentLayer`, `totalLayers`, `layerText` | running print identity/progress |
| `resinUsedMl`, `resinText`, `runSecs/Time`, `remainingSecs/Time` | consumption + timing |
| `vatRemainingMl`, `vatText`, `vatLow` | resin-in-VAT estimate + low flag |
| `webControl`, `askRefill` | runtime toggles |
| `sdRev` | SD content revision — bumps on any out-of-band SD change (upload/delete/boot-anim); a client reloads its file list when this changes (0-28) |
| `freeHeap`, `minFreeHeap`, `maxAllocHeap`, `uptimeSecs` | runtime diagnostics (heap + uptime) |

## Models & SD

| Endpoint | Method | Purpose / arguments |
|---|---|---|
| `/api/files` | GET | SD inventory (models + archives) with sizes and free space |
| `/api/files/model` | GET | one model's details; `name=`, optional `estimate=1` for the resin estimate |
| `/api/files/model/metadata` | POST | update model metadata (`model.json`) |
| `/api/files/model/preview` | GET/POST | fetch / store the cached preview PNG; `name=`, `type=05|1` |
| `/api/files/layer` | GET | a single layer PNG (browser-side slicing/preview) |
| `/api/files/delete` | POST | delete an SD item; `name=` |
| `/upload` | POST | multipart model upload (`.sl1`/`.zip`); fields: `file`, `action=replace|rename` on a 409 name conflict, `source`, optional Connect credits fields |
| `/api/files/local` | POST | the same upload path with the OctoPrint shape — PrusaSlicer "Send to printer" |

Upload answers only after the on-printer unpack finishes (minutes for big
models); a name conflict returns `409` with a `conflict` body and the client
retries with `action`.

## Print control

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/print/start` | POST | start a model (`name=`); a low-resin state answers `{"warning":"low_resin",...}` first — confirm and retry with `force=1` |
| `/api/print/pause` / `resume` / `stop` | POST | lifecycle controls (guarded by `can*` flags) |
| `/api/vat/refilled` | POST | restart the resin estimate from a full VAT |

## Settings, backup, restore

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/config` | GET/POST | full settings read / form-encoded save |
| `/api/config/defaults` | POST | factory print/web settings |
| `/api/config/mqtt/defaults`, `/api/config/connect/defaults` | POST | reset one integration |
| `/api/config/backup` | GET | JSON backup download (includes secrets — handle with care) |
| `/api/config/backup/sd` | POST | write the backup to the SD card |
| `/api/config/restore`, `/api/config/restore/sd` | POST | restore from an uploaded JSON / from the SD copy |
| `/api/config/dry-run` | POST | `enabled=0|1` — the banner's quick toggle |

## Firmware update

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/update` | GET | installed vs latest (printer-side GitHub check), `hasUpdate`, `allowed` |
| `/api/update/install` | POST | self-update; no arg = latest, `version=X.Y.Z` = that release (strict SemVer validation, 400 on anything else) |
| `/update` | GET/POST | human fallback page / multipart `firmware.bin` flash |

## Integrations

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/telegram/test`, `/api/whatsapp/test`, `/api/discord/test` | POST | send a test notification with the saved credentials |
| `/api/connect/test`, `/register`, `/recovery-code`, `/backup` (GET/POST), `/restore` | POST/GET | TinyMaker Connect pairing + settings backup |
| `/api/boot-anim` (+ `/file`, `/select`, `/delete`, `/preview`, `/install`) | GET/POST | boot-animation management; `/install` allows CORS preflight for the web flasher |

## Static

`/` (gzip dashboard), `/manifest.json`, `/pwa-icon-192.png` (PWA bits).

---

*TODO before merging: add response examples for `/api/files` and
`/api/update`, and settle the deprecation-window wording with Brian.*
