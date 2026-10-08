# Firmware hosting for self-update (GitHub Pages)

The printer's **System → Update** screen can check for a newer firmware and
install it over WiFi with no computer ("Install"). The files live on **GitHub
Pages** (the `gh-pages` branch), served from one host with no redirects.

Since 0.18 the printer trusts **our signature, not GitHub's certificate**. It
fetches a signed manifest `{"version","sha256","sig"}`, verifies the ECDSA-P256
signature against the public key compiled into the firmware
(`src/update_pubkey.h`), and flashes `firmware.bin` only if its SHA-256 matches.
A manifest that does not verify means no update (fail closed). The URLs are set
in `src/Network.ino`:

```c
#define OTA_MANIFEST_URL "https://slibbinas.github.io/TinyMakerWifi/update.json"
#define OTA_VERSION_URL  "https://slibbinas.github.io/TinyMakerWifi/version.txt"  // only for the base directory
```

The private signing key lives outside the repo (`~/.tinymaker/fw_signing_key.pem`,
or `$TINYMAKER_SIGNING_KEY`); create it once with `scripts/dev/gen_fw_signing_key.py`.
`release.py` refuses to publish without it. **Losing it means no printer can
update itself again until each one is flashed by hand** - keep a backup.

## Files to publish on `gh-pages`

`release.py` writes all of them (list below). For orientation: `firmware.bin` is
the app-only image (OTA image, **not** `firmware-full.bin`), built at
`C:/PIO-build/TinyMakerWiFi/tinymaker/firmware.bin`.

## One-time setup

1. Create the `gh-pages` branch (can be an orphan branch with only these files).
2. In the repo: **Settings → Pages → Build and deployment → Source: Deploy from a
   branch → `gh-pages` / root**.
3. Confirm the files are live:
   - `https://slibbinas.github.io/TinyMakerWifi/version.txt`
   - `https://slibbinas.github.io/TinyMakerWifi/firmware.bin`

## Each release (automated)

Since 0.11.0 the whole flow is one script (run from the repo root, with
`FIRMWARE_VERSION` already bumped in **both** envs of `platformio.ini` and the
release commit in place):

```
%USERPROFILE%\.platformio\penv\Scripts\python.exe scripts\release.py --notes-file notes.md
```

It builds both envs, pushes `main` + the `vX.Y.Z` tag, publishes to `gh-pages`
(`firmware.bin`, `firmware-X.Y.Z.bin`, `version.txt`, `versions.txt`) and
creates the GitHub Release with `firmware.bin` + `firmware-full.bin` attached.
`--dry-run` stops after the build; the GitHub token comes from the git
credential helper automatically.

## Files on `gh-pages`

- `update.json` - the **signed manifest of the stable release**; this is what
  the self-update reads.
- `firmware-X.Y.Z.json` - the signed manifest of each release; the version
  picker verifies it before installing that version.
- `firmware.bin` - the stable build; `firmware-X.Y.Z.bin` - one copy per release.
- `version.txt` - two lines: stable version + firmware.bin URL (kept for the
  dashboard and older references; the self-update no longer reads it).
- `version-beta.txt` - the newest beta (follows stable when there is none).
- `versions.txt` - the picker's list: one `X.Y.Z` per line, newest first.
- `builds.json` - `firmware.elf` SHA-256 of every release -> its version, so a
  crash report's build fingerprint resolves to the exact release.

A **beta** (`release.py --beta`) publishes `firmware-X.Y.Z.bin/.json`, the picker
list and `version-beta.txt`, but leaves `update.json` / `version.txt` /
`firmware.bin` on the previous stable. `--promote` later copies that version's
already-signed manifest to `update.json`, so promotion needs no key and ships the
exact bytes that were signed.

### Slicer module (0.17 SL-mod)

The slicer has its own ladder next to the firmware one, because it is published
far more often and installing it reboots nothing:

- `slicer-version.txt` - the newest slicer module version, one line.
- `slicer-versions.txt` - the picker's manifest: one `X.Y.Z` per line, newest first.
- `lib/slicer-X.Y.Z.sha256` - one line per file, `<64 hex>  <name>`. This is what
  the printer fetches to decide which bytes it will accept onto its card.
- `lib/*.gz` - the five files of the set, **already gzipped**: `slicer-wasm-X.Y.Z.js.gz`,
  `slicer-core-X.Y.Z.js.gz`, `slicer-wasm-worker-X.Y.Z.js.gz`, `sla-web-X.Y.Z.js.gz`
  and `sla-web-X.Y.Z.wasm.gz` (~3.6 MB in total).

The `.gz` copies must be published by us, not produced in the browser: browser
compression is not byte-reproducible, so no checksum would ever match (learned
with three.js on 08-12). The sums in `slicer-X.Y.Z.sha256` are the sums of the
**gzipped** files, which is exactly what travels over the wire.
  The browser fetches it straight from GitHub Pages (CORS is open).

The version check compares `MAJOR.MINOR.PATCH`, so "Install" only lights up when
`version.txt` is strictly newer than the running firmware. The dashboard's
picker also allows downgrades (with a warning).
