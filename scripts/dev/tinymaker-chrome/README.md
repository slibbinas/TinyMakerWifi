# TinyMaker status - Chrome extension (dev, owner's use)

Printer status on the Chrome toolbar. Reads `GET /api/status` once a minute (the
dashboard itself asks every 4 s) and never writes to the printer.

- **Icon badge:** `42%` while printing (orange), `II` paused, `SD` while the card is
  unpacking/deleting, `✓` after a print ends (until the popup is opened), `!` when the
  printer does not answer, nothing when idle.
- **Popup:** state, model, progress bar, layer, running/remaining time with the end
  clock, resin used and left, SD, WiFi; **Open dashboard**; **Refresh**.
- **Printer address:** popup -> *Printer address* (default `192.168.1.138`).

## Install

1. Copy this folder somewhere stable (e.g. `%USERPROFILE%\Tools\TinyMaker\tinymaker-chrome`).
2. `chrome://extensions` -> switch on **Developer mode** (top right).
3. **Load unpacked** -> pick the folder.
4. Puzzle icon on the toolbar -> pin **TinyMaker status**.

After changing the files: `chrome://extensions` -> the extension's reload arrow.

`host_permissions` is `http://*/*` so the address can be changed to any LAN name or IP
without editing the manifest.
