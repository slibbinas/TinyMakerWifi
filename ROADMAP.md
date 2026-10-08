# TinyMakerWifi Roadmap

Where the WiFi / wireless-upload / OTA firmware for the open-source TinyMaker
MSLA resin printer is headed.

**The full, always-current roadmap - with the detail behind each line - lives at
[tinymakerwifi.com/roadmap](https://tinymakerwifi.com/roadmap/).** That page is the
single source and is refreshed at every release. This file is a short pointer, so
the repo never carries a second copy of the feature lists that drifts out of date.

## Where we are

| Version | State |
|---|---|
| **1.0.0** | current **stable** - what the automatic self-update installs. The 0.17-0.18 line promoted: the built-in browser slicer, live 3D progress, named resin profiles, power-loss resume, signed self-updates and drag-and-drop upload |
| **1.0.x** | fixes only - the features are frozen |
| **1.1** | next - new features; the list is on the [roadmap page](https://tinymakerwifi.com/roadmap/) |

New builds normally ship first as **betas** (from the [Releases page](https://github.com/slibbinas/TinyMakerWifi/releases)
and the dashboard's version picker); the automatic self-update stays on the previous
stable until the beta has proven itself, then it is promoted - so nobody is pushed
onto an untested build.

## Contributing

Small fixes, beta testing and bug reports are all first-class help. See
[CONTRIBUTING.md](CONTRIBUTING.md): small fixes → PRs to `main` (CI must be green),
ambitious work → PRs to `experimental`. Or just send a note through the
[feedback form](https://tinymakerwifi.com/feedback/) - 30 seconds, no account.

Version-by-version history: [CHANGELOG.md](CHANGELOG.md).
