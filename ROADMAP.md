# TinyMakerWifi Roadmap

*Updated: 2026-07-12*

## Where we are: feature freeze → road to 1.0.0

**v0.14.0 was the last feature release** before a stability push. From here
until **1.0.0** the focus is bug hunting, testing and polish — small `0.14.x`
releases only. New feature ideas are collected (see below), not implemented.

**Beta practice:** new releases start as betas — available immediately from
GitHub Releases and the dashboard's *Update → version picker*, while the
automatic self-update channel follows a day or two later, once early birds
confirm the build. Findings → [GitHub Issues](https://github.com/slibbinas/TinyMakerWifi/issues).

### 1.0.0 criteria

- Beta channel quiet (no open regressions)
- Heap soak tests clean over multi-hour prints (`min_free_heap` telemetry)
- Full code review repeated, open Issues resolved

## After 1.0.0: the community decides

We'll run a **community poll** (Facebook group) on what comes next. Already
agreed for the post-1.0.0 era:

- **TinyMaker Connect** integration lands on `main` (the community
  model-sharing platform — its firmware side already lives on the
  `experimental` branch)
- Basic/advanced firmware split with an update-channel switch
- Poll winners from the idea pool (Telegram notifications, boot animations,
  layer-height mismatch warning, dashboard themes, and more)

## How to contribute

See [CONTRIBUTING.md](CONTRIBUTING.md): small fixes → PRs to `main` (CI must
be green), ambitious work → PRs to `experimental`. Beta testing counts as a
first-class contribution — run the picker's newest version and report
anything odd.
