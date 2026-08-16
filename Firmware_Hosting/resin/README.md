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

Optional. Point it at `https://tinymakerwifi.com/r/<slug>` — a redirect worker
that resolves geography and the partner programme. Keeping it here rather than
in the firmware means the partner can change without a firmware release, and the
printer stays free of any of it. A profile without `buyUrl` simply shows no link.
