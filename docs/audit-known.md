# Known and accepted - do not report these again

Findings that have been looked at, understood and **deliberately left as they
are**. An audit that lists them again is repeating work, not finding anything:
say "known" and move on. Anything not on this list is fair game.

Each entry says why it stays. If the reason stops being true, the entry goes.

| Where | What | Why it stays |
|---|---|---|
| `src/Network.ino`, resin rename | The rewrite normalises the file: values are clamped to their allowed ranges, missing keys are materialised from the factory `slow` recipe, and fields the reader does not know are dropped. | Fixing it means editing JSON in place on an ESP32 to preserve bytes nobody reads. The effective values do not change - the reader would have clamped them anyway. Cost far above the benefit. |
| `src/ResinProfile.ino`, `RESIN_JSON_MAX` | A profile file of 1 KB or more cannot be renamed (the read fails, the endpoint answers 500). | Real profiles are ~430 B. Only a hand-written file could hit this, and raising the limit costs RAM on a board that has none to spare. |
| `web/parts/slicer.js`, `slicerScaleUI` | The scale field rounds **down** (2.75 % is shown and applied as 2.7 %), so the number in the box is not exactly the model's scale. | Deliberate: rounding to nearest let a mere tap on the control make the model bigger than it was, which pushed it back outside the build volume. Down is the safe direction. |
| `web/parts/slicer.js`, slice Stop | Stopping relies on the slicer module (loaded from gh-pages, not in this repo) not catching the exception thrown from our progress callback. | Cannot be verified from this repository. The module has no try/catch around those calls today, and there is a second guard after the slice returns. Worst case is cosmetic: the button stays "Stopping..." until the run ends and no result is issued. |
| `web/parts/slicer.js`, save flow | `namesBefore` is read from `filesItems`, which can be stale, so the rename path may fail to identify the new model and fall back to "pick it from the list". | Graceful degradation by design: opening the wrong model is the thing being avoided, and the fallback says exactly what to do. |
| `web/dashboard.html`, `repaintPreviewForTheme` | Two branches (`markPreviewStopped`, `dashPreviewPlaceholder`) are effectively unreachable, because both paint through `paintPreviewProgress` and are therefore caught by the remembered-message branch above them. | Dead code, identical behaviour either way. Removing it would mean re-testing six preview states for no user-visible gain. |

## Deferred, with a decision behind it

| Where | What | Decision |
|---|---|---|
| `src/ResinProfile.ino`, `resinBuyUrlAllowed` | Library profiles carry vendor "Buy" links (`sunlu.com`, `anycubic.com`), but the firmware only accepts links from `tinymakerwifi.com` and `slibbinas.github.io`, so those links are dropped on read and no Buy button appears. | **Not a bug to fix here.** It is the affiliate model's shape: the link should point at our own catalogue, which then forwards. V decided (08-17) to settle it when the release and its documentation are prepared, not before. |
