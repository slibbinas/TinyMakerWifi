# TinyMakerWifi Roadmap

Where the WiFi / wireless-upload / OTA firmware for the open-source TinyMaker
MSLA resin printer is headed. Updated at each release.

## Where we are

The **0.15.x** line is a stability push toward **1.0.0**. The feature set is
frozen — the focus is bug hunting, testing and polish, not new features.

New builds ship first as **betas** from the dashboard's *Update → version
picker* and the [Releases page](https://github.com/slibbinas/TinyMakerWifi/releases);
the automatic self-update channel follows a day or two later, once early testers
confirm the build.

## Recently shipped

- **WiFi that reconnects on its own** after a router dropout — no more
  re-entering the password to get back online.
- **Safer critical screens** — the leveling screen warns before it homes
  downward, plus a power-outage recovery note, so a bad moment stays recoverable.
- **Live progress on every long operation** (unpacking a model, flashing
  firmware) — no frozen or flickering screens.
- **Readable boot-animation list** in both light and dark themes.

Full, version-by-version history is in the
[changelog](https://github.com/slibbinas/TinyMakerWifi/blob/main/CHANGELOG.md).

## Now in development

The next update (**0.16**) is a batch of dashboard and workflow refinements:

- **The model list refreshes itself** when the SD card changes — a model
  uploaded from one device shows up on every open dashboard, no reload needed.
- **Fresher print-time and resin estimates** right after a settings change.
- **A print-aware screen saver**, plus smaller mobile-navigation and workflow
  fixes.

These are still in development and not yet released — you won't see them on your
printer yet. When 0.16 is ready it reaches the **beta** channel first (*Update →
version picker*), and the automatic self-update follows once early testers
confirm it.

## On the way

- **Power-loss resume** — pick a print back up from an SD checkpoint after an
  outage, instead of starting over.
- **TinyMaker Connect** — a community platform for sharing print-ready models
  straight to the printer.
- **More dashboard polish** — themes, a layer-height mismatch warning, and
  print notifications.

## Toward 1.0.0

1.0.0 is about confidence, not new features: the beta channel quiet with no
open regressions, clean multi-hour print soak tests, and a repeated full code
review.

## After 1.0.0

The community picks what comes next — a poll in the
[Facebook group](https://www.facebook.com/groups/1486879621729571) decides the
priorities. Software feature ideas already on the table:

- A **basic / advanced** firmware mode — keep the dashboard simple, or go deep.
- **More notification options** for print start / finish / errors.
- **Dashboard customization** — more themes and layout choices.
- **Smarter print helpers** — settings-mismatch warnings and better estimates.

Have an idea? Vote in the group or send it through the feedback form.

## Contributing

Small fixes, beta testing and bug reports are all first-class help. See
[CONTRIBUTING.md](CONTRIBUTING.md), or just send a note through the feedback
form in the dashboard — 30 seconds, no account.
