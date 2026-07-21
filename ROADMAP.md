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

## Delivered (up to 0.15.8)

Everything through the current stable **0.15.8** — on your printer now via
*System → Update*:

- **WiFi that reconnects on its own** after a router dropout — no more
  re-entering the password to get back online.
- **Safer critical screens** — the leveling screen warns before it homes
  downward, plus a power-outage recovery note, so a bad moment stays recoverable.
- **Live progress on every long operation** (unpacking a model, flashing
  firmware) — no frozen or flickering screens.
- **Light and dark themes** across the dashboard.
- **Readable boot-animation list** in both themes.

Full, version-by-version history is in the
[changelog](https://github.com/slibbinas/TinyMakerWifi/blob/main/CHANGELOG.md).

## Now in development (0.16)

The next update bundles the workflow and reliability work in progress:

- **Power-loss resume** — pick a print back up after an outage instead of
  starting over, with a guided, safe plate raise.
- **A print-aware screen saver** — dims the idle screen and shows the printer
  name and IP.
- **A self-refreshing model list** — a model uploaded from one device shows up
  on every open dashboard, no reload needed.
- **Fresher print-time and resin estimates** right after a settings change.
- **Mid-print preview from any device** — the model preview is served from the
  printer during a print, so any browser can watch.
- **Cleaner menus and navigation**, plus mobile fixes.

These are still in development and not yet released — you won't see them on your
printer yet. When 0.16 is ready it reaches the **beta** channel first (*Update →
version picker*), and the automatic self-update follows once early testers
confirm it.

## On the way

- **TinyMaker Connect** — a community platform for sharing print-ready models
  straight to the printer.

## Toward 1.0.0

1.0.0 is about confidence, not new features: the beta channel quiet with no
open regressions, clean multi-hour print soak tests, and a repeated full code
review.

## After 1.0.0

Once 1.0.0 lands, the community will help shape what comes next — you'll be able
to vote on priorities in the [Facebook group](https://www.facebook.com/groups/1486879621729571).
The poll sets the direction; final calls on what actually ships stay with the
maintainer.

Software feature ideas already on the table:

- A **basic / advanced** firmware mode — keep the dashboard simple, or go deep.
- **More notification options** for print start / finish / errors.
- **More dashboard customization** — extra themes and layout choices.
- **Smarter print helpers** — settings-mismatch warnings and better estimates.

Got an idea? The vote opens with 1.0.0 — and the feedback form in the dashboard
is open any time.

## Contributing

Small fixes, beta testing and bug reports are all first-class help. See
[CONTRIBUTING.md](CONTRIBUTING.md), or just send a note through the feedback
form in the dashboard — 30 seconds, no account.
