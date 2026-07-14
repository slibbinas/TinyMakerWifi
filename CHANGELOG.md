# Changelog

All notable changes to **TinyMakerWifi** — the WiFi / wireless-upload / OTA
firmware for the open-source TinyMaker MSLA resin printer. Written for humans:
skim it to see what each version added and who contributed what, without
digging through pull requests.

Versions use the fork's own SemVer (`FIRMWARE_VERSION` in `platformio.ini`);
the upstream TinyMaker3D firmware is `1.0.2`. Format follows
[Keep a Changelog](https://keepachangelog.com/). Full release assets are on the
[Releases page](https://github.com/slibbinas/TinyMakerWifi/releases).

Credits: features are by **Viktoras Sidlauskas ([@slibbinas](https://github.com/slibbinas))**
unless noted. Community contributors are tagged inline.

## [Unreleased] — `experimental` branch

### Added
- **WhatsApp and Discord notifications** — same three messages as Telegram
  (finished / low-resin pause / canceled): WhatsApp through the free CallMeBot
  gateway (one-time activation, inline help), Discord through a channel
  webhook (no bot needed). Settings has one *Phone notifications* choice:
  Off / Telegram / WhatsApp / Discord.
- **Exposure undo** — when the exposure test (or a config save) replaces your
  Regular exposure, the old value is remembered and an *Undo (Xs)* link
  appears next to the field.
- The dashboard header links the project site, **tinymakerwifi.com**.
- **Light theme** — the dashboard gets a light/dark toggle (the crescent next
  to *Manual*); your choice sticks per browser. Same orange, no flash on load.
- **Branded WiFi setup** — the `TinyMaker-Setup` portal now shows the project
  logo, firmware version and where to find the manual, styled to match the
  dashboard.
- **Getting started guide** — a dismissible first-steps checklist on the
  dashboard (WiFi → slicer → first model → first print → exposure →
  integrations); the printer ticks steps off by itself where it can. Small
  **?** marks next to tricky settings (layer height, resin tracking, web
  control, backup) open short explanations.
- **Exposure test result entry** — after the test strip, the printer asks
  *"Best bar (count dots)?"*: cycle to the number of dots on the crispest bar
  and it sets *Regular exposure* itself — no manual Settings trip. Two extra
  positions shift the whole ladder shorter/longer for a re-run when no bar
  was right.
- **Install confirmation on the printer** — the on-device self-update now asks
  *"Install update?"* (with versions) before flashing, so a stray OK on the
  Update screen can't start it.
- **Anonymous usage ping** — once per firmware version (the first boot after a
  flash) the printer sends a hash of its factory MAC, the firmware version and
  the lifetime print hours, so we know how many printers are out there and
  which versions they run. Nothing else is sent, ever; switch it off in
  Settings (*Anonymous usage ping*).
- **Project logo** — the dashboard favicon is now the layer-stack + WiFi mark,
  and the Update tab shows a community counter (*N printers running
  TinyMakerWifi*) fed by the anonymous ping.
- The manual gained an advanced section on deriving your resin's **working
  curve (Jacobs)** from the test strip with calipers.

## [0.14.3] — 2026-07-13

### Added
- **Telegram notifications** — the printer messages you when a print
  **finishes** (with time + resin used), **pauses for low resin**, or is
  **canceled**. One On/Off switch, a bot token and a chat id, and a *Send test*
  button; inline setup steps (@BotFather / @userinfobot) right in the dashboard.
- **Restore from SD** button on the dashboard (enabled only when a backup file
  is present), and the **date of the SD backup** shown in Settings.
- **Downloadable boot animations** — a small on-device library of named
  animations you can pick on the printer or in the dashboard, and install
  straight from the community site. *(contributed by [@Tann2019](https://github.com/Tann2019))*
- **Build tag in the dashboard header** — experimental builds show their git
  commit next to the version (`Firmware 0.14.3 (abc1234)`), so you can tell
  exactly which build is flashed.
- **TinyMaker Connect (early preview)** — an opt-in link to the community
  model-sharing service being built by [@Briadark](https://github.com/Briadark).
  Off by default; when enabled in Settings, a Connect tab shows your shared-model
  activity. The service itself is still in testing — more when it opens up.

### Changed
- **Dashboard styling pass:** native browser confirm dialogs replaced with a
  modal that matches the printer UI; the status toast moved to a bolder,
  animated snackbar that appears next to your click; Settings split into
  *Print settings / Network & integrations / Backup & restore* cards; the
  Connect tab slimmed to just the model activity (registration details moved to
  the Settings Connect section); secret fields (Telegram, Connect) show only a
  masked *ends in XXXX* tail and are never echoed back.

### Fixed
- A page left open no longer serves a stale version after a reflash (the cache
  tag now includes the build), and the "Updating firmware" overlay clears
  within seconds instead of lingering after the printer is back.
- A homing abort/error no longer sends a "Print finished" notification.
- Blocking test/health calls (Telegram, Connect) are gated so they can't run
  mid-print; the Connect manager buttons and settings-backup were hardened
  (script-injection escaping, the Connect registration is now included in the
  backup).

## [0.14.2] — 2026-07-12

### Fixed
- Status messages (Config saved, Backup to SD, resets…) were invisible in the
  Settings and Update tabs — the toast lived in a hidden part of the page.
  Moved so feedback shows in every tab; most visibly, **Backup to SD** now
  confirms it worked.

## [0.14.1] — 2026-07-12

### Added / Changed
- **Exposure calibration test** improvements: each bar carries its number as
  **dots** (so a peeled strip stays identifiable), and the bar times are now
  **proportional** to your Regular exposure (40–160%), so the spread is
  meaningful for fast and slow resins alike.
- **WiFi signal bars** on the WiFi Info screen; light/dark **manual themes**.

### Fixed
- Running-time display no longer flickers between two formats each second.

## [0.14.0] — 2026-07-11

The last feature release before the pre-1.0.0 freeze.

### Added
- **Settings backup & restore** — one file holds every setting and the lifetime
  counters; keep it on the SD card and the printer offers to restore it on the
  first boot after a full USB reflash.
- **Boot update check** — shortly after WiFi connects the printer checks for new
  firmware and offers *Install / Later* on screen. *(contributed by [@Briadark](https://github.com/Briadark))*
- **Exposure calibration test**, **UV LED lifetime hours**, **print-finish ETA**
  (NTP), heap/uptime instrumentation for soak testing, and an illustrated
  [user manual](https://slibbinas.github.io/TinyMakerWifi/manual/).

### Credits
- The **initial web dashboard** and the **Advanced menu** (WiFi & Web-control
  switches) were contributed by **Brian Karmelk ([@Briadark](https://github.com/Briadark))**.

## Earlier (0.9.x – 0.13.x)

The WiFi stack, captive-portal setup, PrusaSlicer "Send to printer" (SL1 /
OctoPrint emulation), SD import, OTA self-update, the 3D model preview & live
print progress, resin estimation & VAT tracking, MQTT / Home Assistant, and the
read-only dashboard mode were built up across the 0.9–0.13 series. See the
[Releases page](https://github.com/slibbinas/TinyMakerWifi/releases) for details.
