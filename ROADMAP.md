# TinyMakerWifi Roadmap

Where the WiFi / wireless-upload / OTA firmware for the open-source TinyMaker
MSLA resin printer is headed.

**The full, always-current roadmap — with the detail behind each line — lives at
[tinymakerwifi.com/roadmap](https://tinymakerwifi.com/roadmap/).** That page is the
single source and is refreshed at every release. This file is a short pointer, so
the repo never carries a second copy of the feature lists that drifts out of date.

## Where we are

| Version | State |
|---|---|
| **0.15.8** | current **stable** — what the automatic self-update installs |
| **0.16.0** | shipped as a **beta** (*Update → version picker*), headlined by **power-loss resume** — the printer picks a print back up after an outage, or safely lifts the plate, answerable from your phone |
| **0.16.x** | **now** — beta stabilization: fixes from tester feedback, then it is promoted to the automatic self-update channel |
| **0.17** | next — the exposure wave: sub-second exposure timing and named resin profiles |
| **1.0.0** | 0.17, promoted once it has proven itself |

New builds ship first as **betas** (from the [Releases page](https://github.com/slibbinas/TinyMakerWifi/releases)
and the dashboard's version picker); the automatic self-update stays on the previous
stable until enough testers confirm the beta, then it is promoted — so nobody is
pushed onto an untested build.

## Contributing

Small fixes, beta testing and bug reports are all first-class help. See
[CONTRIBUTING.md](CONTRIBUTING.md): small fixes → PRs to `main` (CI must be green),
ambitious work → PRs to `experimental`. Or just send a note through the
[feedback form](https://tinymakerwifi.com/feedback/) — 30 seconds, no account.

Version-by-version history: [CHANGELOG.md](CHANGELOG.md).
