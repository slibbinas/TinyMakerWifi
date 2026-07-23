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

Already built and being tested for the next update:

- **A print-aware screen saver** — dims the idle screen, gently moves the
  printer name and IP around (no burn-in), shows live print progress while
  printing, and is on by default.
- **Reorganized menus on the printer and the dashboard** — Advanced settings
  grouped (Network / Resin / Display), a new **Statistics** screen (print
  hours, UV time, boot info), and Update kept visible with an
  "update available" badge.
- **A self-refreshing model list** — a model uploaded from one device shows up
  on every open dashboard, no reload needed.
- **Fresher print-time and resin estimates** right after a settings change.
- **"Why did it restart?" diagnostics** — after an unexpected reboot the
  printer records the reason (and the layer, if it was printing) and shows it
  in Statistics.
- **Mobile-navigation and reliability fixes**, including steadier MQTT
  reconnects.
- **Mid-print preview from any device** — open the dashboard mid-print from a
  new phone and still see the model and live progress.
- **Release notes in the updater** — a link to what changed next to every
  version.
- **Model details in the status panel** (layers, time, resin) and a smoother
  3D progress view.
- **Clear live feedback on Stop / Pause / Resume** — one message with a live
  countdown ("Stopping — finishing the current layer · ~18s"), shown on every
  open dashboard, not just the device that pressed the button.
- **SD work runs in the background** — deleting a model or importing an upload
  no longer freezes the dashboard; every connected device sees what the
  printer is doing and when it finished.
- **Power-loss resume** — after an outage the printer offers to pick the print
  back up from where it stopped (no re-printing from scratch), or to safely
  lift the plate off a stuck print — and you can answer that **from the
  dashboard on your phone**, not just at the machine. Field-tested with real
  power cuts and a resin print.
- **A "recently fixed" list** in the feedback form, plus a status link for your
  own reports so you can see what happened to them.

0.16 is content-complete and being packaged. It reaches the **beta** channel
first (*Update → version picker*), and the automatic self-update follows once
early testers
confirm it.

## Next after 0.16: the 0.17 exposure update

The exposure work moved into its own release so 0.16 doesn't have to wait for
test materials:

- **Sub-second exposure timing** — set exposure in tenths of a second instead
  of whole seconds (needed for fast resins).
- **Named resin profiles** — switch resins and the right settings follow.
- **Print-time impact preview** — change an exposure setting and see what it
  does to a print's duration before saving.
- **"Pick the winning bar" helper** — run the exposure test, tap the best bar
  in the dashboard, and it computes and offers the new exposure time.

0.17 is validated with a fast test resin (arriving ~August) together with
whatever 0.16 field feedback surfaces.

## On the way

- **TinyMaker Connect** — a community platform for sharing print-ready models
  straight to the printer.

## Toward 1.0.0

1.0.0 is 0.17, promoted once it has proven itself: the beta channel quiet with
no open regressions, clean multi-hour print soak tests, and a repeated full
code review. Confidence, not new features.

## After 1.0.0

Once 1.0.0 lands, the community will help shape what comes next — you'll be able
to vote on priorities in the [Facebook group](https://www.facebook.com/groups/1486879621729571).
The poll sets the direction; final calls on what actually ships stay with the
maintainer.

Software feature ideas already on the table:

- **A basic / advanced firmware mode** — keep the dashboard simple, or go deep.
- **More notification options** for print start / finish / errors.
- **More dashboard customization** — extra themes and layout choices.
- **Smarter print helpers** — settings-mismatch warnings and better estimates.

Got an idea? The vote opens with 1.0.0 — and the feedback form in the dashboard
is open any time.

## Contributing

Small fixes, beta testing and bug reports are all first-class help. See
[CONTRIBUTING.md](CONTRIBUTING.md), or just send a note through the feedback
form in the dashboard — 30 seconds, no account.
