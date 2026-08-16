# TinyMakerWiFi LAN API (DRAFT v0 — 0-11)

> Status: **draft for review** — generated from the endpoint registrations in
> `src/Network.ino`; the contract rides `FIRMWARE_VERSION` (see Versioning policy
> below), so this doc is not pinned to a single release. Promised in
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
| `previewCached` | the active model's preview PNG is held in RAM and fetchable mid-print (0-19) |
| `resinUsedMl`, `resinText`, `runSecs/Time`, `remainingSecs/Time` | consumption + timing |
| `vatRemainingMl`, `vatText`, `vatLow` | resin-in-VAT estimate + low flag |
| `vatGrams` | the same estimate in grams, using the configured resin density (R-cal, 0.17). `vatText` deliberately stays ml-only so older clients render unchanged |
| `webControl`, `askRefill` | runtime toggles |
| `sdRev` | SD content revision — bumps on any out-of-band SD change (upload/delete/boot-anim); a client reloads its file list when this changes (0-28) |
| `freeHeap`, `minFreeHeap`, `maxAllocHeap`, `uptimeSecs` | runtime diagnostics (heap + uptime) |

## Models & SD

| Endpoint | Method | Purpose / arguments |
|---|---|---|
| `/api/files` | GET | SD inventory (models + archives) with sizes and free space |
| `/api/files/model` | GET | one model's details; `name=`, optional `estimate=1` for the resin estimate |
| `/api/files/model/metadata` | POST | update model metadata (`model.json`) |
| `/api/files/model/preview` | GET/POST | fetch / store the cached preview PNG; `name=`, `type=05|1`. While printing, the **active** model's preview is served from a RAM snapshot taken at print start (`type` ignored); other names, or a snapshot that did not fit in heap, answer `409` (0-19) |
| `/api/files/layer` | GET | a single layer PNG (browser-side slicing/preview). `source=1` (what the dashboard always sends) makes `i` the file number; without it `i` is a PRINT layer and 0.10 mm maps it to every other file |
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
| `/api/resume/accept` / `lift` / `discard` | POST | answer the boot power-loss prompt remotely; valid only while `/api/status` reports a non-null `resumePending` (any button press at the printer consumes the prompt and these answer 409). `accept` resumes the print, `lift` raises the plate off the stuck print (up only) and discards, `discard` just clears the checkpoint. All three queue the action for the printer's main loop and return `{"ok":true,"queued":true}` |
| `/api/vat/refilled` | POST | restart the resin estimate from a full VAT |
| `/api/resin/calibrate` | POST | R-cal (0.17): teach the printer what a print really costs. `slot=1\|2&raw=<ml>&grams=<g>` writes ONE named sample (`clear=1` empties it); `grams=` alone records against the last finished print; `density=<g/ml>` alone stores a measured density and re-fits both samples; `reset=1` clears everything but the density. Idle-only (409 while printing), 400 when the numbers cannot match the estimate. Returns `{factor, fixedMl, twoPoint, ...}` |

### Resin calibration model (0.17)

The pixel estimate misses two independent things, so the firmware keeps two numbers:

```
used_ml = raw_geometric_ml * resinCalFactor + resinFixedMl
```

`resinCalFactor` scales with the model (pixel area, layer height, over-cure); `resinFixedMl`
is the per-print film left on the plate, which does not scale. A single weighed print cannot
separate a slope from an offset, so `/api/resin/calibrate` keeps up to two samples of clearly
different size and solves the line through them (`twoPoint:true`). With one sample only the
slope is fitted and the offset is left as it is.

`model.json` caches the **raw** geometric estimate, never the calibrated one — re-calibrating
therefore refreshes already-scanned models without re-decoding a single PNG.

`/api/config` exposes `resinCalFactor`, `resinFixedMl`, `resinDensity`, `lastPrintRawMl`
(-1 = no finished print yet), `calTwoPoint` (whether a real two-point fit is in force) and
`calSamples[] = [{slot, raw, grams, ml}]` — samples are stored in **grams** (what the scale
showed); `ml` is derived with the current density, so changing the density re-fits both.
Backups carry `calUnit:1` to mark the grams era; older files restore safely.

`/api/status` additionally reports `endstop` (the raw optical Z sensor reading) — added for
homing diagnostics.

## Settings, backup, restore

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/config` | GET/POST | full settings read / form-encoded save |
| `/api/config/defaults` | POST | factory print/web settings, and with them the resin: the active profile returns to `slow`, its overlay is deleted from the card and the resin calibration is cleared. The printer's own "Back to Default" does the same and then reboots (the device flags only take effect at boot) |
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
| `/api/boot-anim` (+ `/file`, `/select`, `/delete`, `/preview`, `/install`) | GET/POST | boot-animation management; `/install` pulls a `.tmb` only from allowlisted hosts (gh-pages library, configured Connect server) — other sources go onto the SD card by hand |
| `/api/resin-profile` | GET | resin profiles (0-16): the two built-in ones (`fast`, `slow`) plus `/resin/*.json` on the card, with `selected`. Works without an SD card — the built-ins live in flash |
| `/api/resin-profile/select` | POST | `name=<slug>` — copies the profile into the live settings (exposure, layers, all four lift settings, density, R-cal) and saves them |
| `/api/resin-profile/save` | POST | `name=<slug>&display=<label>[&mode=new]` — writes the CURRENT settings into that profile. With `base_exposure=…` and friends it writes those values instead, which is how the dashboard installs a library profile it fetched itself. `mode=new` refuses an existing name (409) |
| `/api/resin-profile/delete` | POST | `name=<slug>` — deletes the file; for a built-in this drops the overlay and restores the factory values |
| `/api/resin-profile/rename` | POST | `name=<slug>&to=<slug>` — SD-card profiles only (409 for a built-in) |
| `/api/vat/weight` | POST | `grams=<full vat on the scale>` — sets the remaining resin from a weighing, using the empty-vat weight (`vatEmptyG`) and the profile's density |

All four mutating resin routes are idle-only (`rejectIfBusy`) and also refuse
while a power-loss resume is pending (409): the second half of an interrupted
print has to come out with the same exposure as the first. **The same 409 guards every
other route that can move the layer height**: `/api/config`,
`/api/config/defaults`, `/api/config/restore`, `/api/config/restore/sd` and
`/api/connect/restore`. `POST /api/print/start` and `POST /api/files/delete` refuse
too: the plate is still in the vat with an unfinished object, and the record points at
that very model. That is because
the resume move is computed from the layer height in force when Resume is
pressed, and a changed height would drive the plate into the cured object. The
press itself re-checks, and refuses on the printer if the height no longer
matches the record.

`GET /api/resin-profile` lists each profile with `edited` (its numbers differ
from the built-in ones) and `overlay` (a file exists on the card). They are not
the same thing: an overlay holding the factory values is not an edit, but it is
still there to delete.

## Static

`/` (gzip dashboard), `/manifest.json`, `/pwa-icon-192.png` (PWA bits).

---

## Response examples

### `GET /api/files` (idle only — 409 `printer busy` while printing or during an SD job)

```json
{
  "ok": true,
  "sdReady": true,
  "usageKnown": true,
  "totalBytes": "31902400512",
  "freeBytes": "29804923904",
  "usedBytes": "2097476608",
  "usagePct": 6,
  "hiddenCount": 0,
  "items": [
    { "name": "ScreamingEvil", "type": "model",   "printable": true,
      "sizeBytes": "0", "connectPublicId": "pub_ab12cd34" },
    { "name": "Benchy.sl1",    "type": "archive", "printable": false,
      "sizeBytes": "12582912" }
  ]
}
```

Byte counts are JSON **strings** (they can exceed 32-bit). `type` is
`model` (unpacked folder) or `archive` (an `.sl1`/`.zip` still in the SD
root — OK on the printer imports it). Model folders report `sizeBytes`
`"0"` — walking every layer file made the list O(models × layers) slow;
archives keep their cheap single-file size. `connectPublicId` appears only
on models imported from TinyMaker Connect. At most 64 items are listed;
`hiddenCount` is everything skipped (unmanaged root entries + overflow).

### `GET /api/update`

```json
{
  "ok": true,
  "installed": "0.15.8",
  "latest": "0.15.8",
  "state": 2,
  "hasUpdate": false,
  "allowed": true
}
```

May block a few seconds while the (5-minute-cached) GitHub Pages version
check runs; mid-print it returns the cached state immediately. `state` is
the check's progress/outcome code; `allowed` mirrors the web-flash gate
(idle + Web control on, or the printer's Update screen) — when it is
`false`, `POST /api/update/install` will answer 403.

---

*TODO before 1.0.0: settle the deprecation-window wording with Brian.*
