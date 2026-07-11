# Contributing to TinyMakerWifi

Thanks for wanting to help! This firmware runs on real printers in real
homes, so the project values **stability over speed** — we are heading to a
`1.0.0` stable release, and the bar for what lands on `main` reflects that.
This page explains how contributions land smoothly.

## Where does my change go?

| Change | Target |
|---|---|
| Small fix or small, self-contained feature | PR to **`main`** |
| Large feature, new subsystem, work-in-progress, anything touching many files | PR to **`experimental`** |
| Companion servers / apps (PHP, cloud services…) | **Own repository** — this repo stays firmware-only. We link to companion projects from the README once they stabilize. |

**`main` is protected:** every PR needs the green **build** check (CI compiles
both PlatformIO environments). Nothing merges red.

**`experimental`** is the home for ambitious work: it can move fast, stay
half-finished, and be merged into `main` in reviewed slices when ready.
If in doubt where to target — open the PR against `experimental` or just ask
in an issue first. A quick "I'm planning X, sound OK?" issue before a big PR
saves everyone time and avoids rework.

## Build requirements

- **PlatformIO** (not Arduino IDE). `pio run` must succeed for **both**
  environments: `tinymaker` (USB) and `tinymaker-ota` (WiFi) — CI enforces this.
- **Do not upgrade** `platform = espressif32@6.5.0` (Arduino core 2.0.14).
  The vendored `Arduino_GFX` 1.2.0 breaks on 3.x.
- **Do not replace the libraries in `lib/`** (`AccelStepper` 1.64,
  `Arduino_GFX` 1.2.0, `PNGdec` 1.0.1, `SdFat` 1.1.2) with registry
  versions — the APIs changed.

## Code notes (read before your first PR)

- All `src/*.ino` files compile as **one translation unit**. Functions
  defined inside `#if ENABLE_NETWORK` blocks don't get auto-prototypes —
  forward-declare them near the top of `TinyMaker.ino` (see the examples
  there).
- The firmware must still compile with **`ENABLE_NETWORK 0`** (the original,
  network-free build). If your feature is network-only, guard it.
- **UI drawing goes through the `ui*` helpers** at the top of
  `Interface.ino` (frames, titles, buttons, hints) — never restyle
  individual screens.
- **The ESP32 has 4 MB flash and no PSRAM.** Watch heap usage and flash
  size; large buffers go on the heap inside the function that needs them.
- **Printing is timing-sensitive.** During a print the network is serviced
  only in narrow windows between layers; never add blocking calls (TLS,
  long SD scans…) to paths that can run mid-print.

## Testing

Flash your change onto a real printer if you can (`pio run -t upload -e
tinymaker`, or OTA). If you can't test on hardware, say so in the PR — the
maintainer or a beta tester will.

## PR checklist

- [ ] CI is green (both envs compile)
- [ ] Still compiles with `ENABLE_NETWORK 0` (if you touched shared code)
- [ ] No new blocking work in mid-print network paths
- [ ] Screenshots/photos for UI changes (LCD or dashboard)
- [ ] Note in the PR description if the change alters **user-visible
      behavior** — the README and the user manual are updated in batch
      before each release, and this note is what feeds that list

## Versioning & releases

Releases use the fork's own SemVer (`FIRMWARE_VERSION` in
`platformio.ini`) and are cut by the maintainer. New releases are published
with a short **beta delay**: the GitHub Release and version picker get the
build first, and the self-update channel (`version.txt`) is raised a day or
two later, after beta testers confirm it. Please don't bump versions in PRs.

## Communication

- **Bugs / feature ideas / "planning a big PR"** → GitHub Issues
- **Community chat** → the [TinyMaker open source community Facebook group](https://www.facebook.com/groups/1486879621729571)

Firmware is MIT-licensed; by contributing you agree your contribution is
MIT too. Credits for significant contributions go in the README and the
user manual — that's a promise.
