# TinyMaker status - Chrome extension

Printer status on the Chrome toolbar. Reads `GET /api/status` once a minute (the
dashboard itself asks every 4 s) and never writes to the printer.

- **Icon badge:** `42%` while printing (orange), `II` paused, `SD` while the card is
  unpacking/deleting, `✓` after a print ends (until the popup is opened), `!` when the
  printer does not answer, nothing when idle.
- **Popup:** state, model, progress bar, layer, running/remaining time with the end
  clock, resin used and left, SD, WiFi; **Open dashboard** (switches to an already open dashboard tab with the same
  address, or opens a new one); **Refresh**.
- **Printer address:** popup -> *Printer address* (default `tinymaker.local`; the IP works too).
  **Save** asks Chrome for access to that one address; a fresh install opens this page by
  itself and shows `?` on the icon until the address is saved.

## Install

1. Copy this folder somewhere stable (e.g. `%USERPROFILE%\Tools\TinyMaker\tinymaker-chrome`).
2. `chrome://extensions` -> switch on **Developer mode** (top right).
3. **Load unpacked** -> pick the folder.
4. Puzzle icon on the toolbar -> pin **TinyMaker status**.

After changing the files: `chrome://extensions` -> the extension's reload arrow.

Since 0.2.0 the extension holds no site access when installed: `http://*/*` is only an
*optional* host permission, and the options page requests `http://<printer>/*` when the
address is saved (and drops the previous printer's access). The Chrome Web Store reviews
broad host access at length, and the extension only ever talks to the printer.

Package for the store: `python scripts/dev/pack_chrome_ext.py` (zip named after the
manifest version).
