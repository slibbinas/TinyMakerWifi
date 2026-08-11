# Browser slicer — design (targets 1.1)

Status: **design only, nothing implemented.** Lives on `experimental2`, off `experimental`
at `6c3566f`. Neither `experimental` nor `main` is touched.

Origin: issue #96. Requirements from the maintainer, 2026-08-12.

---

## The idea in one paragraph

The user drops an STL into the dashboard. The browser orients it, checks that it fits,
generates supports and slices it. The printer receives the same archive PrusaSlicer sends,
so **the printer needs no new path for saving at all**. The result: an STL can be printed
without installing any software, from a phone.

All computation happens in the browser. The printer only stores the slicer's code on the SD
card and serves it — exactly what it already does for three.js.

---

## Three facts that make this cheap

Verified in the code before designing, not assumed:

**1. The upload path already accepts what a slicer would produce.**
`Import.ino` unpacks a ZIP of `<n>.png` entries plus a `config.ini` carrying the layer height
(`unpackModelToEmptyDir`, layer index parsed by `layerIndexFromName`). The browser can build
exactly that and post it to `/api/files/local` — the same route PrusaSlicer's "Send to printer"
uses. **No firmware code is needed to save a sliced model.**

**2. Antialiasing will actually work.**
`PNGDraw()` pushes the decoded line to the masking LCD with `draw16bitRGBBitmap` — grey pixels
reach the panel as grey. The luminance threshold (`> 128`) is applied *only* to the resin
pixel count, not to what is displayed. So a grey edge pixel passes less UV and cures partially:
antialiasing is real optical quality here, not cosmetics. It matters more on this machine than
on a high-resolution mono LCD.

**3. three.js is already on the SD card** and proved the mechanism, including SHA-256 pinning
that rejects foreign code (verified: a file with one byte changed is refused). The slicer is
the second module through the same door. **Flash does not grow.**

### The limit worth stating up front

The masking display is **320 × 240**, i.e. one pixel = **0.1275 mm**. The slicer cannot produce
detail finer than the printer can show. That is the hardware, not our design — but it sets
expectations, and it is why antialiasing earns its place.

---

## Module system (the slicer is the first module)

Today three.js is loaded through a route built specifically for it. Rather than build a second
one-off, generalise it once:

```
GET  /lib/<name>.js      -> serve from SD, gzip, long cache
POST /api/lib/<name>     -> store on SD, accepted ONLY if SHA-256 matches

firmware: one table
  { "three",  THREE_SHA,  600 KB }
  { "slicer", SLICER_SHA, 400 KB }
```

Browser side, one function every module uses:

```js
const mod = await loadModule('slicer');   // SD -> our gh-pages -> unavailable
```

| Property | How |
|---|---|
| Loaded on demand | Fetched only when the slicer card is opened. Non-users pay nothing. |
| Works offline | After the first fetch it lives on the card. Internet is needed once, or on a version bump. |
| Foreign code cannot land | Only a file whose SHA-256 matches the constant compiled into the firmware. |
| Flash unaffected | The firmware grows by a table row (~40 B), not by the module's size. |

---

## Where it lives in the UI

The four-button bar is not touched. The slicer is **one more card**, like the SD block,
collapsed by default:

```
┌─ Slice an STL ────────────────────────── [ Open ] ─┐
│  Turn a 3D file into a printable model.            │
└────────────────────────────────────────────────────┘
```

Opening it expands three steps **in place** — no separate page, no new navigation:

1. **Model** — drop an STL, see it in the build volume, rotate, check that it fits.
2. **Settings** — supports, antialiasing. Defaults good enough to print without touching anything.
3. **Slice** — progress bar, resin and time estimate, then *Save to printer*.

The 3D view reuses **the same three.js scene and the same camera angle as the model preview**.
To the user it is not a new tool; it is the box they already know how to turn.

---

## Pipeline

| Step | Runs on | How |
|---|---|---|
| Read STL | browser | Binary and ASCII. Size in mm shown immediately. |
| Place and check | browser | Auto lay-flat, centre, fits/does-not-fit with a concrete number. |
| Cross-sections | browser (Worker) | Triangle/Z-plane intersection, triangles pre-sorted by height so it is one sweep, not one pass per layer. |
| Rasterise | browser | Contours filled into a 320 × 240 canvas with antialiasing, then PNG. |
| Supports | browser | **Drawn straight into the slices**, not built as 3D geometry — see below. |
| Archive | browser | ZIP, stored (PNGs are already compressed) + `config.ini` with 0.05 mm. |
| Upload | printer | **Existing path.** The printer unpacks it like any PrusaSlicer upload. |

### Measured cost

Layer PNG measured at 3 539 B; upload speed measured on this printer.

| Height | Layers | Archive | Slice in browser | Upload |
|---|---|---|---|---|
| 10 mm | 200 | ~0.7 MB | ~1 s | ~4 s |
| 20 mm | 400 | ~1.4 MB | ~2 s | ~7 s |
| 40 mm | 800 | ~2.7 MB | ~4 s | ~14 s |
| 68 mm (max) | 1360 | ~4.6 MB | ~7 s | ~24 s |

About 140 such models fit in the 203 MB free on the card.

---

## The decision that simplifies everything

**Supports are drawn into the slices, not built as 3D geometry.**

A normal slicer builds a support mesh, merges it with the model and slices both. That needs
robust 3D boolean work, which is exactly what breaks on messy files.

We do not need it, because **the output is a stack of images anyway**. A support pillar is a
circle drawn in every layer from the plate up to its contact point. A raft is a filled shape in
the first few layers. No 3D operations, nothing to break on a broken STL, and the result is
exactly what the user saw in the preview.

Cost: pillars are vertical, with no slanted struts or branching. For this machine
(40.8 × 30.6 mm plate, small parts) that is sufficient — slanted supports matter when weight
topples a column.

---

## Functions

### Required (from the maintainer)

| Function | Approach |
|---|---|
| STL upload | Drag-drop or button. Dimensions and triangle count shown at once. Non-manifold files are accepted — watertightness is not required to slice for MSLA. |
| Does not fit | Three answers, **rotate offered first** because it is usually enough: *Rotate* / *Scale automatically* / *Scale by hand (%)*. Never silent — scaling changes the part's size, which is the user's call. |
| Manual orientation | 90° buttons per axis plus free drag. A *lay flat* button snaps the bottom down. |
| Antialiasing | On/off plus strength 1–3. |
| Support settings (two) | **Density** (sparse / normal / dense) and **tip diameter** (0.3–0.8 mm) — the same two that decide 90 % of cases in PrusaSlicer. |
| 0.05 mm layers | Fixed, as for every model. Shown but not editable, so it cannot drift from the printer's settings. |
| Save to SD | Name + *Save to printer*. An existing name asks before overwriting. |

### Proposed additions — without these it is incomplete

| Function | Why it is necessary, not nice |
|---|---|
| **Raft** <span>(accepted)</span> | Without it thin supports peel off the plate. PrusaSlicer adds one automatically; if we do not, the first print fails and the slicer takes the blame. One switch, on by default. |
| **Resin and time estimate before saving** | We already have the maths (calibration ×1.092 + 0.39). Showing "18.4 ml · 2 h 40 min" *before* printing is where the expensive decision is made. |
| **Layer preview slider** | The only way to see whether supports are where they should be and whether anything floats. Cheap for us: the slices are already in memory and we have the renderer. |
| ~~Exposure from the resin profile~~ | **Dropped (maintainer, 08-12).** Exposure belongs to the printer, set per resin through the profiles and the exposure test — one place, one truth. This also matches what the firmware already does: from `config.ini` it reads exactly one key, `layerHeight`, and the time estimate uses the printer's own `Base_Exposure`/`Regular_Exposure`. A slicer carrying its own numbers would be the only model source behaving differently, and changing resin would leave older models quietly holding stale values. |
| **Island warning** | A layer touching nothing below will float in the vat and ruin the print. Detection is nearly free (we already find connected regions for supports); saying "3 spots hang in mid-air, look at layer 214" is worth a few lines. |

### Later

| Function | Why deferred |
|---|---|
| Hollowing + drain holes | Saves a lot of resin, but honest hollowing needs 3D work. A cheap approximation exists (erode each layer inward); worth doing once the base works. |
| Copies / array | Useful (the Tooth model is six teeth in one file) but convenience, not necessity. |
| Manual support points | Large UI job. First see whether automatic placement suffices. |
| Automatic best orientation | Sounds good, debatable in practice — better to show cross-section area and let the human decide. |

---

## What the firmware needs

| Work | Size | Why |
|---|---|---|
| Module table instead of the three.js-specific route | ~40 lines | So the slicer is not a second one-off. One route, one SHA table. |
| Nothing else | — | Saving uses the existing upload path; slicing happens in the browser; the 3D view reuses three.js. |

Flash stays essentially where it is (72.2 %). That was the point: **the feature grows on the
card, not in the chip.**

---

## Risks

| Risk | Mitigation |
|---|---|
| A large STL (500k+ triangles) exhausts phone memory | Stream the parse, keep only triangle bounds; above a threshold say "this file is too heavy for a phone, try a computer" instead of dying silently. |
| Slicing freezes the UI | Everything in a Web Worker. The interface stays alive, the bar moves, *Cancel* works. |
| Upload during a print | Not allowed to start — the card belongs to the print, same rule as everywhere else. |
| Users expect PrusaSlicer's power | The name and copy tell the truth: a **quick** slicer for straightforward models. PrusaSlicer is not going anywhere. |

---

## Stages

| Stage | What | Done when |
|---|---|---|
| **A** | Module table in firmware + `loadModule()` in the dashboard | three.js loads through the new shared route, nothing regressed |
| **B** | STL parse + 3D view in the build volume + fit check | Drop an STL, see it in the same box as the preview |
| **C** | Slice + rasterise + ZIP + save | A cube from STL appears in the SD list and **prints** |
| **D** | Supports, raft, antialiasing, island warning | A real model with supports prints without detaching |
| **E** | Estimates, layer preview, exposure from profile | Millilitres, time and layer inspection before saving |

A–C already give a working tool; D–E make it usable daily. Each stage is tested on hardware,
like everything else.

---

## Decisions (all closed 2026-08-12)

1. ~~**Raft on by default?**~~ **Decided: yes** (maintainer, 08-12).
2. ~~**Does the slicer set exposure?**~~ **Decided: no** (maintainer, 08-12). The model inherits
   the printer's settings, exactly as a PrusaSlicer upload does. Two consequences: the slicer is
   **not** blocked behind `0-16` and can be built in parallel; and its resin/time estimate must read
   the printer's current settings via `/api/config` rather than invent any — so the number shown is
   the number the printer will actually deliver. Layer height still travels in `config.ini`, because
   that is the one key the firmware does read (and LH-chk already warns on a mismatch).
3. ~~**Where is "too complex"?**~~ **Decided: a budget derived from the part, not a fixed number**
   (08-12). One printer pixel covers 0.01626 mm². Once a model carries roughly one triangle per
   pixel of its own surface, additional triangles cannot be shown by this machine at all — they are
   pure weight. So the budget is `surface_area / pixel_area`, computed per file:

   | Part | Surface | Budget |
   |---|---|---|
   | Tooth, scull (15–24 mm) | ~2 000 mm² | ~120k triangles |
   | Typical on this shelf (~35 mm) | 4 150 mm² | ~255k |
   | Largest here (ScreamingEvil, 53 mm) | 8 638 mm² | ~531k |
   | Whole build volume | 12 207 mm² | ~751k — the absolute ceiling |

   Behaviour in three steps: **below budget** — silence; **above budget** — one sentence, "this file
   holds more detail than the printer can show", and carry on; **above 750k** (18–36 MB of geometry
   alone) — offer to simplify, and on a phone refuse to start, because there it ends in a frozen tab
   rather than a slower slice.
4. ~~**Own 3D view or shared with the preview?**~~ **Decided: shared** (maintainer, 08-12) — same
   angle, same controls, nothing new to learn.
