# Resin profile library (gh-pages `/resin/`)

Source of the resin profiles the dashboard offers under **Settings → Resin →
Tested resin library** (0.17, plan item 0-16). Publish this folder to the
`gh-pages` branch as `/resin/`, next to `/bootanims/`.

## How it reaches the printer

The **browser** fetches `manifest.json` and then the profile file, and posts the
values to the printer's `/api/resin-profile/save`. The printer never fetches a
URL of its own, so there is no allowlist to maintain and no SSRF surface: a
profile is ~200 B of JSON, unlike a boot animation, which is why it does not
need the printer-side download path that `/bootanims/` uses.

## Preparing a profile

Do not hand-write these files. `scripts/dev/resin-publish.html` (served from the
dev hub, `http://localhost:8899/resin-publish.html`) loads the published library,
fills a form from the real file, checks every value against the firmware's own
limits and writes both `<slug>.json` and the whole `manifest.json` at once, so the
two cannot drift apart. It publishes nothing by itself - the files are committed
here and to `gh-pages` by hand.

The page also owns the lifecycle a static folder has no room for:

- **Copy** - the same resin at the other layer height (0.05 / 0.10), which is the
  normal case rather than the exception.
- **Pause** - the entry leaves `profiles[]` for `hidden[]` (with a date, a reason
  and the row it stood in) while a complaint is being sorted out, and one click
  puts it back exactly where it was.
- **Retire** - same move, into `retired[]`, permanently.

`hidden` and `retired` live in the same `manifest.json`; the dashboard reads only
`.profiles`, so they are invisible to it.

⚠️ **A fix does not reach anyone who already installed the profile.** The library
list only offers profiles the printer does not have yet, and nothing re-checks it
afterwards. A correction that matters therefore ships under a **new slug** (the
page's *revision* button: `sunlu-tough` → `sunlu-tough-r2`), which is what makes
it show up again for everyone.

## Files

- `manifest.json` — what the dashboard lists. Per entry: `name` (slug, also the
  filename on the SD card), `display`, `file`, `layerHeight`, `testedBy`,
  `testedOn`, and optionally `buyUrl`.
- `<slug>.json` — the profile itself. Same keys the printer writes, so a profile
  saved on a printer can be published here unchanged.

## Two rules for adding a resin

1. **`tested_by` only for resin actually printed on a TinyMaker.** The badge
   names who tested it and travels inside the profile, so it stays visible after
   installation — that is what makes the recommendation worth anything.
2. **No manufacturer datasheet values.** This printer's colour TFT absorbs a lot
   of UV, so a "2-3 s" resin wants roughly 8-15 s here. Every number in a
   profile comes from the exposure test plus a weighing on the machine.

## `buyUrl` (affiliate, GitHub #55)

⚠️ The three profiles published today still carry manufacturer URLs
(`sunlu.com`, `anycubic.com`). The firmware accepts a `buy_url` only from
`https://tinymakerwifi.com/` or `https://slibbinas.github.io/`
(`resinBuyUrlAllowed()`), so the Buy link shows in the library list and then
disappears once the profile is installed. They are regenerated together once the
redirect worker exists.

Optional. Point it at `https://tinymakerwifi.com/r/<slug>` — a redirect worker
that resolves geography and the partner programme. Keeping it here rather than
in the firmware means the partner can change without a firmware release, and the
printer stays free of any of it. A profile without `buyUrl` simply shows no link.
