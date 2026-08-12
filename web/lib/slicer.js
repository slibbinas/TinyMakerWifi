/* TinyMaker browser slicer — module.
 *
 * Stage B: read an STL, measure it, decide whether it fits, and hand the
 * dashboard a mesh it can show in the build volume it already draws.
 * Slicing itself lands in stage C; nothing here writes to the printer.
 *
 * Loaded on demand through loadModule() — from the SD card once pinned, from
 * our gh-pages while the code is still moving. Never from a third-party CDN:
 * the dashboard imports this as same-origin code.
 *
 * Coordinates: "plate space" — x and y in mm from the plate centre, z in mm
 * above the plate. The dashboard maps that into its scene.
 */

export const PLATE = { x: 40.8, y: 30.6, z: 68.0 };
export const PIXEL_MM = 40.8 / 320;                 // 0.1275 — the display's limit
const PIXEL_AREA = PIXEL_MM * PIXEL_MM;             // 0.01626 mm²

/* ---------------------------------------------------------------- parsing */

/* Binary STL: 80-byte header, uint32 count, then 50 bytes per triangle.
   A file can *look* binary and be ASCII, so the count is checked against the
   real length rather than trusting the header. */
function parseBinary(buf) {
  const dv = new DataView(buf);
  const n = dv.getUint32(80, true);
  if (84 + n * 50 !== buf.byteLength) return null;
  const pos = new Float32Array(n * 9);
  let o = 84, p = 0;
  for (let i = 0; i < n; i++) {
    o += 12;                                        // normal — recomputed later
    for (let v = 0; v < 9; v++) { pos[p++] = dv.getFloat32(o, true); o += 4; }
    o += 2;                                         // attribute byte count
  }
  return pos;
}

function parseAscii(text) {
  const nums = [];
  const re = /vertex\s+(-?[\d.eE+-]+)\s+(-?[\d.eE+-]+)\s+(-?[\d.eE+-]+)/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    nums.push(+m[1], +m[2], +m[3]);
  }
  if (nums.length < 9 || nums.length % 9 !== 0) return null;
  return new Float32Array(nums);
}

/** Returns { positions: Float32Array (9 per triangle), triangles } or throws. */
export function parseSTL(buf) {
  let pos = null;
  if (buf.byteLength > 84) pos = parseBinary(buf);
  if (!pos) {
    // Only decode as text when the binary layout did not add up — a 40 MB
    // binary file decoded to a string would cost far more than it should.
    const head = new TextDecoder().decode(new Uint8Array(buf, 0, Math.min(2048, buf.byteLength)));
    if (/^\s*solid/i.test(head)) pos = parseAscii(new TextDecoder().decode(buf));
  }
  if (!pos) throw new Error('This does not look like an STL file.');
  if (!pos.length) throw new Error('The STL file has no triangles.');
  return { positions: pos, triangles: pos.length / 9 };
}

/* ---------------------------------------------------------------- measure */

export function bounds(pos) {
  let ax = Infinity, ay = Infinity, az = Infinity;
  let bx = -Infinity, by = -Infinity, bz = -Infinity;
  for (let i = 0; i < pos.length; i += 3) {
    const x = pos[i], y = pos[i + 1], z = pos[i + 2];
    if (x < ax) ax = x; if (x > bx) bx = x;
    if (y < ay) ay = y; if (y > by) by = y;
    if (z < az) az = z; if (z > bz) bz = z;
  }
  return { min: [ax, ay, az], max: [bx, by, bz],
           size: [bx - ax, by - ay, bz - az] };
}

/** Surface area in mm², used for the detail budget below. */
export function surfaceArea(pos) {
  let a = 0;
  for (let i = 0; i < pos.length; i += 9) {
    const ux = pos[i + 3] - pos[i], uy = pos[i + 4] - pos[i + 1], uz = pos[i + 5] - pos[i + 2];
    const vx = pos[i + 6] - pos[i], vy = pos[i + 7] - pos[i + 1], vz = pos[i + 8] - pos[i + 2];
    const cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    a += Math.sqrt(cx * cx + cy * cy + cz * cz) * 0.5;
  }
  return a;
}

/* How many triangles this printer can still distinguish on a part of this
   size. Past roughly one triangle per pixel of surface, extra detail cannot
   reach the resin — it is only weight. The budget therefore belongs to the
   part, not to a constant. */
export function detailBudget(pos) {
  return Math.round(surfaceArea(pos) / PIXEL_AREA);
}

/* --------------------------------------------------------------- transform */

/* Rotations are kept as whole quarter turns plus a scale, so the numbers stay
   exact and the user can always get back to where they were. */
export function makeTransform() {
  return { rx: 0, ry: 0, rz: 0, scale: 1 };   // rx/ry/rz in quarter turns
}

function rotateQuarter(x, y, n) {
  n = ((n % 4) + 4) % 4;
  for (let i = 0; i < n; i++) { const t = x; x = -y; y = t; }
  return [x, y];
}

/** Applies a transform and drops the result onto the plate, centred. */
export function place(pos, tr) {
  const out = new Float32Array(pos.length);
  const s = tr.scale;
  for (let i = 0; i < pos.length; i += 3) {
    let x = pos[i] * s, y = pos[i + 1] * s, z = pos[i + 2] * s;
    let r;
    r = rotateQuarter(y, z, tr.rx); y = r[0]; z = r[1];
    r = rotateQuarter(z, x, tr.ry); z = r[0]; x = r[1];
    r = rotateQuarter(x, y, tr.rz); x = r[0]; y = r[1];
    out[i] = x; out[i + 1] = y; out[i + 2] = z;
  }
  const b = bounds(out);
  const cx = (b.min[0] + b.max[0]) / 2, cy = (b.min[1] + b.max[1]) / 2;
  for (let i = 0; i < out.length; i += 3) {
    out[i] -= cx; out[i + 1] -= cy; out[i + 2] -= b.min[2];   // sit on the plate
  }
  return out;
}

/* -------------------------------------------------------------------- fit */

/** fits, plus the scale that would make it fit and which axis is the problem. */
export function fitCheck(size) {
  const room = [PLATE.x, PLATE.y, PLATE.z];
  const over = [size[0] / room[0], size[1] / room[1], size[2] / room[2]];
  const worst = Math.max(over[0], over[1], over[2]);
  const axis = ['width', 'depth', 'height'][over.indexOf(worst)];
  return {
    fits: worst <= 1,
    worst,
    axis,
    scaleToFit: worst > 1 ? Math.floor((1 / worst) * 1000) / 1000 : 1
  };
}

/* How much surface would actually rest on the plate in this orientation:
   the area of downward-facing facets sitting within 0.2 mm of the lowest
   point. That is what "flat side down" means to a person — the widest face
   touching the plate — and it is not the same as the shortest bounding box,
   which is what an earlier version picked (maintainer, 08-12). */
function contactArea(pos) {
  let zmin = Infinity;
  for (let i = 2; i < pos.length; i += 3) if (pos[i] < zmin) zmin = pos[i];
  let a = 0;
  for (let i = 0; i < pos.length; i += 9) {
    const z0 = pos[i + 2], z1 = pos[i + 5], z2 = pos[i + 8];
    if (Math.max(z0, z1, z2) > zmin + 0.2) continue;      // not on the plate
    const ux = pos[i + 3] - pos[i], uy = pos[i + 4] - pos[i + 1], uz = z1 - z0;
    const vx = pos[i + 6] - pos[i], vy = pos[i + 7] - pos[i + 1], vz = z2 - z0;
    const cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    const len = Math.sqrt(cx * cx + cy * cy + cz * cz);
    if (len === 0) continue;
    if (Math.abs(cz / len) < 0.85) continue;              // not facing down
    a += len * 0.5;
  }
  return a;
}

/* All six axis-aligned orientations. Preference: it has to fit; then the
   widest face down; then, as a tie-break, the shortest print. */
export function autoOrient(pos) {
  const options = [
    { rx: 0, ry: 0, rz: 0 }, { rx: 1, ry: 0, rz: 0 }, { rx: 2, ry: 0, rz: 0 },
    { rx: 3, ry: 0, rz: 0 }, { rx: 0, ry: 1, rz: 0 }, { rx: 0, ry: 3, rz: 0 }
  ];
  let best = null;
  for (const o of options) {
    const tr = Object.assign(makeTransform(), o);
    const placed = place(pos, tr);
    const b = bounds(placed);
    const f = fitCheck(b.size);
    const score = (f.fits ? 1e6 : 0) + contactArea(placed) - b.size[2] * 0.01;
    if (!best || score > best.score) best = { score, tr, size: b.size, fit: f };
  }
  /* Pasirinkus, kuri puse zemyn, lieka antras klausimas: kaip pasukti ant
     ploksstes. Ji 40.8 x 30.6 mm - pailga, tad pailgas objektas turi gultis
     isilgai. Renkam ta posuki, kuris palieka daugiausiai atsargos (V 08-12). */
  for (const rz of [1]) {
    const tr = Object.assign({}, best.tr, { rz: (best.tr.rz + rz) % 4 });
    const b = bounds(place(pos, tr));
    const f = fitCheck(b.size);
    if (f.worst < best.fit.worst - 1e-6) best = { score: best.score, tr, size: b.size, fit: f };
  }
  return best;
}

/* ------------------------------------------------------------------- mesh */

/** Plate-space positions -> what the dashboard's scene expects. */
export function toSceneMesh(pos) {
  const out = new Float32Array(pos.length);
  for (let i = 0; i < pos.length; i += 3) {
    out[i] = pos[i];                       // x across the plate
    out[i + 1] = pos[i + 2] - PLATE.z / 2; // height, box centred on origin
    out[i + 2] = pos[i + 1];               // y into the plate
  }
  return out;
}

/* ================================================================ slicing */
/* Stage C: turn the placed mesh into the archive the printer already knows.
 *
 * No polygon stitching. Every triangle crossing a layer plane gives one
 * segment, and the raster is filled by even-odd scanline crossings straight
 * from those segments. Stitching contours is where slicers break on
 * non-manifold files, and we do not need contours for anything.
 */

export const LAYER_MM = 0.05;
export const RES = { w: 320, h: 240 };

/* Sub-scanlines per pixel row. Three is the point where the staircase stops
   being visible on this display and more stops paying for itself. */
const SUB = 3;

/** Segments where triangles cross plane z. Each is [x0,y0,x1,y1] in mm,
 *  pointing so that solid is always on its left — the triangle's own normal
 *  decides which way that is. The fill below counts on that direction; without
 *  it two overlapping bodies leave a hole where they meet.
 */
export function sliceAt(pos, z, out) {
  out.length = 0;
  for (let i = 0; i < pos.length; i += 9) {
    const zA = pos[i + 2], zB = pos[i + 5], zC = pos[i + 8];
    const above = (zA > z) + (zB > z) + (zC > z);
    if (above === 0 || above === 3) continue;          // wholly on one side
    const px = [], py = [];
    for (let e = 0; e < 3; e++) {
      const a = i + e * 3, b = i + ((e + 1) % 3) * 3;
      const za = pos[a + 2], zb = pos[b + 2];
      if ((za > z) === (zb > z)) continue;
      const t = (z - za) / (zb - za);
      px.push(pos[a] + (pos[b] - pos[a]) * t);
      py.push(pos[a + 1] + (pos[b + 1] - pos[a + 1]) * t);
    }
    if (px.length !== 2) continue;
    /* Outward normal of this triangle (STL winding). Turned a quarter turn in
       the plane it gives the direction the cut has to run: ẑ × n = (−n.y, n.x). */
    const ux = pos[i + 3] - pos[i], uy = pos[i + 4] - pos[i + 1], uz = zB - zA;
    const vx = pos[i + 6] - pos[i], vy = pos[i + 7] - pos[i + 1], vz = zC - zA;
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz;
    if ((px[1] - px[0]) * -ny + (py[1] - py[0]) * nx < 0)
      out.push(px[1], py[1], px[0], py[0]);            // wrong way round
    else
      out.push(px[0], py[0], px[1], py[1]);
  }
  return out;
}

/** Scanline fill with sub-row sampling; returns a 320x240 grey map.
 *  `discs` are supports drawn straight into the same layer — see below.
 *
 *  Inside is decided by counting, not by alternating: a crossing that runs one
 *  way adds one, the other way takes one away, and the pixel is solid wherever
 *  the count is not zero. That is the same rule stl2png applies on the CPU and
 *  Matt Keeter's DLP slicer applies on the GPU stencil buffer, and it is what
 *  makes two overlapping bodies read as one solid instead of leaving a hole
 *  between them. Alternating (even-odd) got that case wrong (V 08-13).
 */
function rasterise(seg, grey, aa, discs) {
  grey.fill(0);
  const W = RES.w, H = RES.h;
  const sx = W / PLATE.x, sy = H / PLATE.y;               // mm -> px
  const subs = aa ? SUB : 1;
  const xs = [], ws = [];
  for (let row = 0; row < H; row++) {
    for (let sub = 0; sub < subs; sub++) {
      // pixel row centre in mm, measured from the plate's near edge
      const yPix = row + (sub + 0.5) / subs;
      const yMm = yPix / sy - PLATE.y / 2;
      xs.length = 0; ws.length = 0;
      for (let s = 0; s < seg.length; s += 4) {
        const y0 = seg[s + 1], y1 = seg[s + 3];
        if ((y0 > yMm) === (y1 > yMm)) continue;
        const t = (yMm - y0) / (y1 - y0);
        xs.push((seg[s] + (seg[s + 2] - seg[s]) * t + PLATE.x / 2) * sx);
        ws.push(y1 > y0 ? 1 : -1);
      }
      if (xs.length < 2) continue;
      // Few crossings per row, and the winding has to travel with its x —
      // insertion sort keeps the two arrays in step without building objects.
      for (let a = 1; a < xs.length; a++) {
        const vx = xs[a], vw = ws[a];
        let b = a - 1;
        while (b >= 0 && xs[b] > vx) { xs[b + 1] = xs[b]; ws[b + 1] = ws[b]; b--; }
        xs[b + 1] = vx; ws[b + 1] = vw;
      }
      let depth = 0, from = 0;
      const base = row * W;
      for (let k = 0; k < xs.length; k++) {
        const was = depth;
        depth += ws[k];
        if (was === 0 && depth !== 0) { from = xs[k]; continue; }
        if (was === 0 || depth !== 0) continue;         // still inside
        let a = from, b = xs[k];
        if (b <= 0 || a >= W) continue;
        if (a < 0) a = 0; if (b > W) b = W;
        const ia = Math.floor(a), ib = Math.floor(b);
        if (ia === ib) { grey[base + ia] += (b - a) / subs; continue; }
        grey[base + ia] += (ia + 1 - a) / subs;
        for (let x = ia + 1; x < ib; x++) grey[base + x] += 1 / subs;
        if (ib < W) grey[base + ib] += (b - ib) / subs;
      }
    }
  }
  if (discs && discs.length) drawDiscs(discs, grey);
  return grey;
}

/* ---------------------------------------------------------------- supports */
/* Supports are drawn straight into the layers, not built as 3D geometry: a
 * pillar is a circle repeated on every layer from the plate up to the overhang
 * it holds. Nothing to intersect, so nothing to break on a messy STL — and the
 * preview shows exactly what will be printed.
 *
 * No settings. Every number here is picked for this printer's pixel (0.1275 mm)
 * and stays in the code (V: "a smart answer, where you would not have to pick").
 */
export const SUP = {
  /* How far a piece may stick out over nothing before it needs holding. This
     is THE question — not "is this pixel new", which is what we asked before
     and why we ended up with 400 supports where PrusaSlicer puts ~30. A ledge
     reaching 1.5 mm past solid material carries itself in resin; one reaching
     5 mm does not. Measured as distance to the nearest supported pixel below,
     so an island (nothing below at all) always qualifies (V 08-13). */
  reachMm: 1.0,
  /* Both numbers set by measurement, not by feel. PrusaSlicer on biomechanical
     woman (same printer profile, same 35 % scale) ends up with about 25
     supports; we swept ours against that:

         grid 3.0 / reach 1.5  ->   6   too few, the maintainer could count them
         grid 2.0 / reach 1.0  ->  26   alongside PrusaSlicer
         grid 1.5 / reach 0.5  -> 108   a forest

     PrusaSlicer has no "reach" filter at all — it supports every overhang and
     controls the count purely by spacing. At 0.1275 mm per pixel a one-pixel
     step is not an overhang, so a small cutoff stays, but it is now 1 mm.

     Nuo 0.10.0 tinklelis tarnauja ir taškams iš geometrijos (žr. angleDeg):
     jis yra tas atstumas, kas kiek išbarstomi taškai ant kabančio paviršiaus. */
  gridMm:  3.0,
  /* KAMPAS — nuo 0.10.0 tai pagrindinis klausimas, kur reikia paramos.
     Rasterio klausimas („ar po šituo pikseliu praeitame sluoksnyje kas nors
     buvo") randa tik STAČIAS nuokabas. Apvalus paviršius, kuris palengva
     pasisuka žemyn, kiekviename sluoksnyje turi po savimi kaimyną ir atrodo
     laikomas — todėl ant ScreamingEvil likdavo 6 stulpeliai ten, kur
     PrusaSlicer stato ~25 (V 08-13: „supportų neliko visai").

     Visi slicer'iai klausia nuolydžio: veidas, kurio normalė nukreipta žemyn
     stačiau nei šitiek laipsnių nuo horizontalės, kabo. 45° yra ir
     PrusaSlicer'io, ir Chitubox, ir Lychee numatytoji riba. */
  angleDeg: 45,
  /* Kas kiek milimetrų aukštyn imamas naujas taškas toje pačioje vietoje.
     Be šito apvalus šonas duotų taškų kiekviename sluoksnyje — tinklelis
     ribotų tik plotį, ne aukštį. */
  ptZMm: 3.0,
  /* Kiek bokštas laikosi atokiai nuo detalės silueto. PrusaSlicer
     support_base_safety_distance = 1 mm; išmatavus jo sluoksnius (08-13) jo
     stulpų mediana z=10 mm aukštyje stovėjo 7,9 mm nuo detalės, mūsų — 2,0 mm,
     ir todėl narvas skendo modelio siluete. Tai MINIMUMAS, ne taikinys:
     neradus tokios vietos, imama artimesnė. */
  standoffMm: 1.0,
  /* Measured off PrusaSlicer's own output for this printer (screamingEvil.zip,
     same 320×240 / 40.8×30.6 / 0.05 mm): its pillars come out at Ø1.13 mm
     median, and its default pillar diameter is 1.0 mm. We were at Ø0.9 mm —
     thinner than the thing we are copying, in the same resin. Ø1.0 mm it is. */
  rMm:     0.5,    // pillar body, Ø1.0 mm
  /* Tip = PrusaSlicer's "head front diameter", 0.4 mm. One pixel (0.14 mm) was
     tempting — the smallest mark this display can make — but that is a third of
     what a slicer built for resin considers safe, and a tip that thin snaps off
     while printing instead of after. The physics is the same for both of us;
     only the pixel differs. */
  tipMm:   0.2,    // tip, Ø0.4 mm
  taperMm: 1.2,    // the tip narrows over this last stretch
  /* Head penetration — how far the tip goes INTO the part. PrusaSlicer needs
     it because it builds a 3D tree and then unions it with the model: without
     an overlap the two only touch and the join is weak. We draw into the same
     layer as the part, so the two are already one solid there — for the layers
     this changes nothing. It matters only in the 3D view, where the head would
     otherwise stop visibly short of the surface. */
  penMm:   0.3,
  /* Foot on the plate. It has to be wide enough to hold and to survive the
     peel, so it flares out over its height instead of being a flat pancake:
     Ø0.9 mm where the pillar starts, Ø2.4 mm where it meets the plate.
     Ø2.4 mm on a 3 mm grid still leaves gaps between neighbours — wider feet
     (Ø4 mm was the first guess) merge into one sheet across the whole plate,
     and a sheet that size tears at the FEP far harder than it should. */
  padMm:   1.5,    // how tall the flare is
  raftRMm: 1.2,    // radius where it touches the plate
  /* The last stretch leans aside, so the pillar arrives at the part at an
     angle rather than head-on: the contact stays a point, the body stands
     clear of the surface, and it peels off instead of tearing (V 08-13). */
  tiltMm:  1.5,
  /* Two overhangs in one cell count as the same pillar when they stand on the
     same thing — within 1 mm of each other. Further apart (an arm over an arm)
     they each get their own. */
  sameBase: 20,      // layers = 1 mm
  /* Apsauga nuo beprotybės, NE norma. Buvo 400 ir tai tyliai nukirsdavo visą
     viršutinę modelio pusę: sluoksniai einami iš apačios į viršų, riba
     išsisemdavo ties ~28 mm, ir 700 iš 1092 kampo taškų nebegaudavo nieko —
     būtent tai matėsi renderyje kaip „supportai tik apačioje" (V 08-13).
     Skaičius didelis todėl, kad kontaktų daug NĖRA tas pats, kas stulpų daug:
     dauguma jų prisijungia prie jau stovinčio bokšto tiltu. */
  maxPillars: 3000,
  /* Braces. A lone 20 mm pillar of Ø0.9 mm sways in the resin and snaps; tied
     to its neighbours it stops being a stick and becomes a frame — which is
     what every SLA slicer's support tower is (maintainer, 08-13). They run at
     45°, so the moving disc overlaps itself from layer to layer and the strand
     comes out continuous.

     Visi keturi skaičiai — išmatuoti iš PrusaSlicer'io 2.9.6 sluoksnių, tą
     patį modelį suslicinus jo paties su V TinyMaker profiliu (08-13):
     jo jungties skersmuo sluoksnyje yra toks pat kaip stulpo (mediana 1,0–1,2 mm),
     o ne plonesnis, kaip buvom pasidarę. Dėl to mūsų narvas ekrane buvo
     vos matomas: stulpai teisingi (p90 0,97 mm), o tarp jų — Ø0,6 gijos. */
  braceRMm:   0.5,   // Ø1.0 mm — kaip stulpas, taip daro ir jis
  /* Tilto galvutė ten, kur jis liečia detalę. PrusaSlicer:
     support_head_front_diameter = 0.5 (spindulys 0.25) ir head_width = 3 mm —
     tiek tęsiasi siaurėjantis kūgis. */
  headRMm: 0.25,
  headMm:  3.0,
  braceMaxMm: 10.0,  // jo support_max_pillar_link_distance = 10
  braceEveryMm: 8.0, // tankiau: 8 mm paliko didelius tuščius tarpus
  braceMinMm: 4.0,   // trumpesni stulpeliai stovi patys
  bracePerPillar: 2, // jo support_max_bridges_on_pillar = 3
  maxBraces: 3000,
  /* A tall pillar with no neighbour to lean on gets one built for it —
     "single long self standing support pillars are now complemented by another
     one or two additional pillars" (PrusaSlicer 1.42). The companion carries
     nothing; it only stops the tall one from swaying. */
  loneMinMm: 10.0,   // taller than this and alone → give it company
  loneOffMm: 3.0,    // how far to the side the companion stands
  /* Not everything that starts in mid-air is an island. The tip of a hair, the
     edge of an ornament — they appear detached for a layer and then grow into
     the mass above them, and nothing can float away because there is nowhere
     to float from. Measured on biomechanical woman: 58 of 60 "islands" were
     exactly that. PrusaSlicer does not support them either. So we look ahead
     half a millimetre: if the patch has become part of something far bigger,
     it needed no pillar (V 08-13: „kam po apačia objekto dėjai suportus"). */
  lookLayers: 10,      // 0.5 mm ahead
  mergeFactor: 8,      // grown this many times over → it was never alone
  islandBigMm2: 1.0,   // a big island is held regardless — too much force
  /* A pillar shorter than this is not a support, it is a plug. They appear
     when the axis steps aside onto part that ended a few layers below: the
     result is a 0.5 mm stub holding a ledge that is already attached to the
     model beside it. 51 of 62 "pillars" on biomechanical woman were exactly
     that — invisible on screen, which is how the maintainer caught the count
     being nonsense while the number said 62 (V 08-13). Does not apply to
     pillars from the plate, which are real however short. */
  minLenMm: 1.0,

  /* ---- Bokštas + tiltas (0.9.0) ----------------------------------------
     Iki šiol klausėm „kur PO šituo tašku galima atsistoti" ir, neradę laisvo
     kelio per 1,5 mm, remdavomės į pačią detalę — 8 iš 12 supportų ant
     biowoman. PrusaSlicer klausia kitaip: „iš KUR šitą tašką galima pasiekti".
     Jo bokštas nueina iki 10 mm į šoną, atsistoja ant plokštės ir permeta
     tiltą, o vienas bokštas dažnai laiko kelis taškus. */
  style: 'tower',      // 'tower' | 'direct' (senasis, palyginimui stende)
  bridgeMaxMm: 10.0,   // kiek toli tiltas gali siekti
  bridgeRMm:   0.3,    // tilto storis, Ø0.6 mm — kaip jungčių
  /* Per tiek bokštas priima dar vieną tašką. Buvo 5 mm — ir tai pasirodė esąs
     tikrasis stabdys: bokštas priimdavo tik artimus taškus, tad NEAUGDAVO, o
     neaugęs bokštas nebepasiekdavo nė vieno aukštesnio. Ant biowoman kampo
     taisyklė randa 1092 taškus visuose aukščiuose, o stulpelių virš 28 mm
     likdavo 5 iš 126 (diagnostika 08-13). PrusaSlicer čia turi 10 mm
     (support_max_bridge_length), tiek pat, kiek ir tilto siekis — riba viena. */
  towerJoinMm: 10.0,
  towerHeadMm: 1.0,    // tiltas prikimba tiek zemiau bokšto viršaus

  /* Padas — PrusaSlicer „Pad: Around object". Plokščias pagrindas, kuris apima
     modelio kontaktą su plokšte IR visų bokštų kojas, plius apvadas aplink.
     Duoda tris dalykus: daiktas laikosi tvirčiau, atplėšimo jėga pasiskirsto
     per didesnį plotą, ir nuo plokštės nuimamas vienas gabalas, o ne dvidešimt
     kojų atskirai. V spausdina tiesiai ant plokštės, tad modelio nekeliam —
     padas tiesiog apjuosia tai, kas ir taip ten stovi. */
  padLayers: 8,        // 0.4 mm
  padWallMm: 2.0       // kiek apvadas išsikiša už kontūro
};

/** Fills circles {x,y,r} in mm. Both axes share one scale (320/40.8 = 240/30.6
    = 7.843 px/mm), so a circle stays a circle. */
function drawDiscs(discs, grey) {
  const W = RES.w, H = RES.h;
  const sx = W / PLATE.x, sy = H / PLATE.y;   // equal by construction, see above
  for (const d of discs) {
    const cx = (d.x + PLATE.x / 2) * sx, cy = (d.y + PLATE.y / 2) * sy;
    const r = d.r * (sx + sy) / 2, r2 = r * r;
    const x0 = Math.max(0, Math.floor(cx - r)), x1 = Math.min(W - 1, Math.ceil(cx + r));
    const y0 = Math.max(0, Math.floor(cy - r)), y1 = Math.min(H - 1, Math.ceil(cy + r));
    for (let y = y0; y <= y1; y++) {
      const dy = y + 0.5 - cy;
      for (let x = x0; x <= x1; x++) {
        const dx = x + 0.5 - cx;
        if (dx * dx + dy * dy <= r2) grey[y * W + x] = 1;
      }
    }
    // A tip narrower than one pixel would otherwise vanish: never let it.
    const ix = Math.floor(cx), iy = Math.floor(cy);
    if (ix >= 0 && ix < W && iy >= 0 && iy < H) grey[iy * W + ix] = 1;
  }
}

/** The circles a given height needs: every pillar between its foot and its top,
    with the last 0.6 mm tapering to the tip so it snaps off cleanly.
    `raft` widens the ones standing on the plate into feet. */
export function pillarDiscs(pillars, z) {
  const out = [];
  for (const p of pillars) {
    if (z > p.top || z < p.bottom) continue;
    const left = p.top - z, up = z - p.bottom;
    let r = SUP.rMm;
    // Narrowing to the tip — the part of the pillar that touches the model.
    if (left < SUP.taperMm)
      r = SUP.tipMm + (SUP.rMm - SUP.tipMm) * (left / SUP.taperMm);
    if (p.bottom === 0) {
      // Flaring to the foot — only for the ones that stand on the plate.
      if (up < SUP.padMm && p.top > SUP.padMm) {
        const flare = SUP.raftRMm + (SUP.rMm - SUP.raftRMm) * (up / SUP.padMm);
        if (flare > r) r = flare;
      }
    } else if (up < SUP.taperMm) {
      /* Standing ON the part: the lower end has to be as fine as the upper one,
         or it leaves a Ø0.9 mm scar where it started (V 08-13). */
      const foot = SUP.tipMm + (SUP.rMm - SUP.tipMm) * (up / SUP.taperMm);
      if (foot < r) r = foot;
    }
    /* Leaning in: the body stands at cx,cy and only the last stretch walks
       over to the contact point, so it meets the surface at an angle. */
    let x = p.x, y = p.y;
    if (left > SUP.tiltMm && p.cx !== undefined) { x = p.cx; y = p.cy; }
    else if (p.cx !== undefined) {
      const t = 1 - left / SUP.tiltMm;              // 0 at the lean, 1 at the tip
      x = p.cx + (p.x - p.cx) * t;
      y = p.cy + (p.y - p.cy) * t;
    }
    out.push({ x, y, r });
  }
  return out;
}

/** Diagonal strands tying neighbouring pillars together. Each is a straight
    run from one pillar at z0 to another at z1; at any height in between it is
    a single disc sliding along that line. */
function makeBraces(pillars) {
  const out = [];
  const tall = pillars.filter(p => p.top - p.bottom >= SUP.braceMinMm);
  /* A brace must land on the pillar's BODY, which stands at cx,cy — not at
     x,y, where only the leaning tip arrives. Tying to x,y left the crosses
     hanging in mid-air, attached to nothing (V spotted it in the 3D view). */
  const axis = p => [p.cx !== undefined ? p.cx : p.x,
                     p.cy !== undefined ? p.cy : p.y];
  /* And only along the straight part: above this the pillar has already
     leaned away, so there is nothing there to hold on to. */
  /* Bokštas visas vertikalus iki pat viršaus — pasvirusio galo neturi, nes į
     detalę jį veda tiltas. Tad rišti jį galima iki viršūnės. */
  const straightTop = p => p.tower ? p.top : Math.max(p.bottom, p.top - SUP.tiltMm);
  for (let i = 0; i < tall.length && out.length < SUP.maxBraces; i++) {
    const a = tall[i], [axx, axy] = axis(a);
    const near = [];
    for (let j = 0; j < tall.length; j++) {
      if (j === i) continue;
      const b = tall[j], [bxx, bxy] = axis(b);
      const d = Math.hypot(bxx - axx, bxy - axy);
      if (d > SUP.braceMaxMm || d < 0.5) continue;
      near.push({ b, d, bxx, bxy });
    }
    near.sort((p, q) => p.d - q.d);
    for (const { b, d, bxx, bxy } of near.slice(0, SUP.bracePerPillar)) {
      // Each pair once: the higher-x partner does not repeat the same tie.
      if (bxx < axx || (bxx === axx && bxy < axy)) continue;
      const lo = Math.max(a.bottom, b.bottom, SUP.padMm);
      const hi = Math.min(straightTop(a), straightTop(b));
      if (hi - lo < d) continue;                 // no room even for one run
      for (let z = lo + SUP.braceEveryMm * 0.5; z + d <= hi;
           z += SUP.braceEveryMm) {
        if (out.length >= SUP.maxBraces) break;
        // Crossed pair: one strand each way, so the frame resists both ways.
        out.push({ ax: axx, ay: axy, bx: bxx, by: bxy, z0: z, z1: z + d });
        out.push({ ax: bxx, ay: bxy, bx: axx, by: axy, z0: z, z1: z + d });
      }
    }
  }
  return out;
}

/** Every brace has to start and end on a pillar body that exists at that
 *  height. Returns the ones that do not — it must always be empty, and a
 *  non-empty answer means something would print hanging in the air. */
export function danglingBraces(pillars, braces) {
  const bad = [];
  const holds = (x, y, z) => pillars.some(p => {
    const cx = p.cx !== undefined ? p.cx : p.x;
    const cy = p.cy !== undefined ? p.cy : p.y;
    const upto = p.tower ? p.top : Math.max(p.bottom, p.top - SUP.tiltMm);
    return Math.hypot(cx - x, cy - y) <= SUP.rMm + 1e-6 &&
           z >= p.bottom - 1e-6 && z <= upto + 1e-6;
  });
  for (const c of braces) {
    /* Tiltai netikrinami šituo matu: jų viršutinis galas remiasi ne į stulpelį,
       o į pačią detalę — tai ir yra jų darbas. Ar jie nekabo, pasako bendra
       savikontrolė (findOverhangs ant galutinių sluoksnių). */
    if (c.bridge) { if (!holds(c.ax, c.ay, c.z0)) bad.push(c); continue; }
    if (!holds(c.ax, c.ay, c.z0) || !holds(c.bx, c.by, c.z1)) bad.push(c);
  }
  return bad;
}

/** Supports as real 3D geometry — the same pillars the layers are drawn from,
 *  but as tubes, so the dashboard can show them next to the model instead of
 *  guessing their shape from a stack of pictures. Returns plate-space triangles
 *  (x,y from the plate centre, z above the plate), ready for toSceneMesh.
 */
export function supportMesh(pillars, braces) {
  /* Twelve sides, not six: a pillar and its foot are round things, and six
     sides read as a rectangular post once you zoom in (V 08-13). The cost is
     a few thousand triangles, which the GPU does not notice. */
  const SIDES = 12, tri = [];
  /* One tapered tube between two points. */
  const tube = (x1, y1, z1, r1, x2, y2, z2, r2) => {
    let ax = x2 - x1, ay = y2 - y1, az = z2 - z1;
    const len = Math.hypot(ax, ay, az);
    if (len < 1e-6) return;
    ax /= len; ay /= len; az /= len;
    // any vector not parallel to the axis, to build the ring from
    let ux = 0, uy = 0, uz = 1;
    if (Math.abs(az) > 0.9) { ux = 1; uz = 0; }
    let px = uy * az - uz * ay, py = uz * ax - ux * az, pz = ux * ay - uy * ax;
    const pl = Math.hypot(px, py, pz);
    px /= pl; py /= pl; pz /= pl;
    const qx = ay * pz - az * py, qy = az * px - ax * pz, qz = ax * py - ay * px;
    const ring = (cx, cy, cz, r) => {
      const o = [];
      for (let s = 0; s < SIDES; s++) {
        const a = s / SIDES * Math.PI * 2, c = Math.cos(a), n = Math.sin(a);
        o.push([cx + (px * c + qx * n) * r,
                cy + (py * c + qy * n) * r,
                cz + (pz * c + qz * n) * r]);
      }
      return o;
    };
    const A = ring(x1, y1, z1, r1), B = ring(x2, y2, z2, r2);
    for (let s = 0; s < SIDES; s++) {
      const t = (s + 1) % SIDES;
      tri.push(...A[s], ...B[s], ...B[t]);
      tri.push(...A[s], ...B[t], ...A[t]);
      /* Caps. Without them the tube is a sleeve: on screen you see straight
         into it and a support head reads as an open funnel (V 08-13). */
      tri.push(x1, y1, z1, ...A[t], ...A[s]);
      tri.push(x2, y2, z2, ...B[s], ...B[t]);
    }
  };

  /* Vertical-ish sections keep HORIZONTAL rings and just move their centre.
     That is what makes a leaning head meet a straight pillar cleanly: both
     rings lie in the same plane, so they coincide exactly and the joint needs
     no cover. The junction ball that was here instead read as a skirt around
     the pillar — PrusaSlicer has no such thing (V 08-13). */
  const shaft = (x1, y1, z1, r1, x2, y2, z2, r2) => {
    const A = [], B = [];
    for (let s = 0; s < SIDES; s++) {
      const a = s / SIDES * Math.PI * 2, c = Math.cos(a), n = Math.sin(a);
      A.push([x1 + c * r1, y1 + n * r1, z1]);
      B.push([x2 + c * r2, y2 + n * r2, z2]);
    }
    for (let s = 0; s < SIDES; s++) {
      const t = (s + 1) % SIDES;
      tri.push(...A[s], ...B[s], ...B[t]);
      tri.push(...A[s], ...B[t], ...A[t]);
      tri.push(x1, y1, z1, ...A[t], ...A[s]);
      tri.push(x2, y2, z2, ...B[s], ...B[t]);
    }
  };

  for (const p of pillars) {
    const cx = p.cx !== undefined ? p.cx : p.x, cy = p.cy !== undefined ? p.cy : p.y;
    const bendAt = Math.max(p.bottom, p.top - SUP.tiltMm);
    if (p.bottom === 0 && p.top > SUP.padMm) {
      /* Foot: a short straight collar on the plate, then the flare up to the
         body. Without the collar the cone meets the plate on an edge and the
         contact is a line, not a face (V 08-13). */
      const collar = SUP.padMm * 0.2;
      shaft(cx, cy, 0, SUP.raftRMm, cx, cy, collar, SUP.raftRMm);
      shaft(cx, cy, collar, SUP.raftRMm, cx, cy, SUP.padMm, SUP.rMm);
      if (bendAt > SUP.padMm) shaft(cx, cy, SUP.padMm, SUP.rMm, cx, cy, bendAt, SUP.rMm);
    } else if (bendAt > p.bottom) {
      // Standing on the part: a fine point there too, then the full body.
      const fine = Math.min(bendAt, p.bottom + SUP.taperMm);
      shaft(cx, cy, p.bottom, SUP.tipMm, cx, cy, fine, SUP.rMm);
      if (bendAt > fine) shaft(cx, cy, fine, SUP.rMm, cx, cy, bendAt, SUP.rMm);
    }
    /* The leaning head, narrowing to the tip and carrying on a little INTO the
       part, along its own direction — otherwise it visibly stops short of the
       surface in the 3D view. */
    const hl = Math.hypot(p.x - cx, p.y - cy, p.top - bendAt) || 1;
    const ex = cx + (p.x - cx) * (1 + SUP.penMm / hl);
    const ey = cy + (p.y - cy) * (1 + SUP.penMm / hl);
    const ez = bendAt + (p.top - bendAt) * (1 + SUP.penMm / hl);
    shaft(cx, cy, bendAt, SUP.rMm, ex, ey, ez, SUP.tipMm);
  }
  /* Tiltas piešiamas dviem dalimis: kūnas vienodo storio ir siaurėjanti
     galvutė paskutinius SUP.headMm — kad 3D vaizde matytųsi tas pats, kas
     bus sluoksniuose (žr. braceDiscs). Jungtys tarp bokštų — vientisos. */
  for (const c of braces || []) {
    if (!c.bridge) {
      tube(c.ax, c.ay, c.z0, SUP.braceRMm, c.bx, c.by, c.z1, SUP.braceRMm);
      continue;
    }
    const span = c.z1 - c.z0;
    const f = span > SUP.headMm ? 1 - SUP.headMm / span : 0;
    const mx = c.ax + (c.bx - c.ax) * f, my = c.ay + (c.by - c.ay) * f;
    const mz = c.z0 + span * f;
    if (f > 0) tube(c.ax, c.ay, c.z0, SUP.braceRMm, mx, my, mz, SUP.braceRMm);
    tube(mx, my, mz, f > 0 ? SUP.braceRMm : SUP.braceRMm * (span / SUP.headMm),
         c.bx, c.by, c.z1, SUP.headRMm);
  }

  return new Float32Array(tri);
}

/** The braces crossing a given height, as circles. */
export function braceDiscs(braces, z) {
  const out = [];
  for (const c of braces) {
    if (z < c.z0 || z > c.z1) continue;
    const t = (z - c.z0) / (c.z1 - c.z0);
    let r = SUP.braceRMm;
    /* Tiltas baigiasi ANT DETALĖS, tad paskutinę atkarpą jis siaurėja į
       smaigalį — kitaip Ø1 mm gija atsiremia į paviršių buku galu, palieka
       tokią pat žymę ir nulūžta ne ten, kur reikia (V 08-13: „kaip liečia
       detalę — nėra suplonėjimo"). Jungtys tarp bokštų lieka vienodo storio:
       jos nieko neliečia. */
    if (c.bridge) {
      const left = c.z1 - z;
      if (left < SUP.headMm)
        r = SUP.headRMm + (SUP.braceRMm - SUP.headRMm) * (left / SUP.headMm);
    }
    out.push({ x: c.ax + (c.bx - c.ax) * t,
               y: c.ay + (c.by - c.ay) * t, r });
  }
  return out;
}

/** Nuokabos taškai iš pačios geometrijos — pagal paviršiaus nuolydį.
 *
 *  Veidas, kurio normalė nukreipta žemyn stačiau nei SUP.angleDeg nuo
 *  horizontalės, kabo ir jam reikia paramos. Tai tas pats klausimas, kurį
 *  užduoda PrusaSlicer, Chitubox ir Lychee; rasterio klausimas („ar po šituo
 *  pikseliu kas nors buvo") atsako tik apie stačias nuokabas ir todėl apvalų
 *  pilvą palieka be nieko.
 *
 *  Dideli veidai išbarstomi tinkleliu: vienas taškas per visą plokščią pilvą
 *  jo nelaikytų. Grąžina Map: sluoksnis -> pikselių indeksai. Visa likusi
 *  mašinerija (kur atsistoti, bokštai, tiltai, X jungtys, padas) nesikeičia —
 *  pasikeitė tik KAS laikoma nuokaba.
 */
export function downwardPoints(pos, layers) {
  const W = RES.w, H = RES.h;
  const sx = W / PLATE.x, sy = H / PLATE.y;
  const cosLimit = -Math.cos(SUP.angleDeg * Math.PI / 180);
  const seen = new Set();                     // 3D ląstelė -> vienas taškas
  const byLayer = new Map();
  const add = (x, y, z) => {
    if (z < SUP.minLenMm) return;             // prie pat plokštės — laikysis pats
    const gx = Math.floor((x + PLATE.x / 2) / SUP.gridMm);
    const gy = Math.floor((y + PLATE.y / 2) / SUP.gridMm);
    const gz = Math.floor(z / SUP.ptZMm);
    const key = (gz * 1024 + gx) * 1024 + gy;
    if (seen.has(key)) return;
    const px = Math.round((x + PLATE.x / 2) * sx);
    const py = Math.round((y + PLATE.y / 2) * sy);
    if (px < 0 || px >= W || py < 0 || py >= H) return;
    const i = Math.floor(z / LAYER_MM);
    if (i < 1 || i >= layers) return;
    seen.add(key);
    let list = byLayer.get(i);
    if (!list) { list = []; byLayer.set(i, list); }
    list.push(py * W + px);
  };
  for (let t = 0; t + 8 < pos.length; t += 9) {
    const ax = pos[t],     ay = pos[t + 1], az = pos[t + 2];
    const ux = pos[t + 3] - ax, uy = pos[t + 4] - ay, uz = pos[t + 5] - az;
    const vx = pos[t + 6] - ax, vy = pos[t + 7] - ay, vz = pos[t + 8] - az;
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const nl = Math.hypot(nx, ny, nz);
    if (nl < 1e-12 || nz / nl >= cosLimit) continue;      // nekabo pakankamai
    add(ax + (ux + vx) / 3, ay + (uy + vy) / 3, az + (uz + vz) / 3);
    /* Didesnis už tinklelio langelį veidas gauna daugiau nei vieną tašką —
       barstom baricentriškai tiek kartų, kiek žingsnių telpa į kraštines. */
    const nu = Math.min(24, Math.floor(Math.hypot(ux, uy, uz) / SUP.gridMm));
    const nv = Math.min(24, Math.floor(Math.hypot(vx, vy, vz) / SUP.gridMm));
    if (!nu && !nv) continue;
    for (let iu = 0; iu <= nu; iu++)
      for (let iv = 0; iv <= nv; iv++) {
        const u = nu ? iu / nu : 0, v = nv ? iv / nv : 0;
        if (u + v > 1) continue;
        add(ax + ux * u + vx * v, ay + uy * u + vy * v, az + uz * u + vz * v);
      }
  }
  return byLayer;
}

/* Where the part hangs in the air. One extra pass at the printing resolution —
 * a coarser scan would put pillars beside the overhang instead of under it.
 *
 * A pixel is an overhang when it is filled now and none of the nine pixels
 * below it (itself plus eight neighbours) was filled — that finds the sheer
 * drops. The slopes come from downwardPoints() above, by angle.
 */
export async function findOverhangs(pos, layers, onProgress, withSupports) {
  const W = RES.w, H = RES.h, N = W * H;
  const grey = new Float32Array(N);
  const cur = new Uint8Array(N), prev = new Uint8Array(N), over = new Uint8Array(N);
  const seen = new Uint8Array(N), stack = new Int32Array(N);
  /* The last layer at which each spot still had part in it. That is where a
     pillar has to start when the plate cannot be reached from here — the
     classic "support on model" case: an upper arm hanging over a lower one.
     Growing from the plate regardless would drive the pillar straight through
     the part (V 08-13). */
  const lastSolid = new Int32Array(N).fill(-1);
  /* Pirmas sluoksnis, kuriame toje vietoje atsirado medžiaga. Iš jo matyti, ar
     nuo plokštės iki tam tikro aukščio kelias laisvas — o tai ir yra klausimas,
     kurį užduoda PrusaSlicer statydamas bokštą. */
  const firstSolid = new Int32Array(N).fill(-1);
  const seg = [];
  /* Distance from every pixel to the nearest one that had material below it.
     Chamfer 3-4 in two sweeps: 3 per straight step, 4 per diagonal, so the
     numbers are 3x the pixel distance and never need a square root. */
  const dist = new Int32Array(N);
  const BIG = 1 << 28;
  const reach3 = Math.round(SUP.reachMm * (W / PLATE.x) * 3);
  const distanceToSupport = () => {
    for (let p = 0; p < N; p++) dist[p] = prev[p] ? 0 : BIG;
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const p = y * W + x;
        let d = dist[p];
        if (d === 0) continue;
        if (x > 0 && dist[p - 1] + 3 < d) d = dist[p - 1] + 3;
        if (y > 0) {
          if (dist[p - W] + 3 < d) d = dist[p - W] + 3;
          if (x > 0 && dist[p - W - 1] + 4 < d) d = dist[p - W - 1] + 4;
          if (x < W - 1 && dist[p - W + 1] + 4 < d) d = dist[p - W + 1] + 4;
        }
        dist[p] = d;
      }
    for (let y = H - 1; y >= 0; y--)
      for (let x = W - 1; x >= 0; x--) {
        const p = y * W + x;
        let d = dist[p];
        if (d === 0) continue;
        if (x < W - 1 && dist[p + 1] + 3 < d) d = dist[p + 1] + 3;
        if (y < H - 1) {
          if (dist[p + W] + 3 < d) d = dist[p + W] + 3;
          if (x < W - 1 && dist[p + W + 1] + 4 < d) d = dist[p + W + 1] + 4;
          if (x > 0 && dist[p + W - 1] + 4 < d) d = dist[p + W - 1] + 4;
        }
        dist[p] = d;
      }
  };
  /* Islands get a pillar of their own, outside the cell grid. A cell holds one
     pillar per thing it stands on, so an island sharing its 3 mm square with an
     ordinary overhang would lose the draw and print unsupported — the self-check
     found 30 such spots on biowoman (V 08-13). An island is the one case where
     "no support" means the piece falls off. */
  const islandPillars = [];
  /* Taškai pagal nuolydį. Savikontrolės praėjime (withSupports) jų neimam:
     ten klausiam „ar dar kas nors liko fiziškai nelaikoma", o veido kampas
     nuo supportų pridėjimo nepasikeičia — kiekvienas paremtas šlaitas vis
     tiek atsilieptų ir savikontrolė niekada nenutiltų. */
  const geoPts = withSupports ? null : downwardPoints(pos, layers);
  const cells = new Map();            // 3 mm cell -> the pillars standing in it
  const cellPx = SUP.gridMm * (W / PLATE.x);
  let islands = 0, firstIsland = 0, total = 0, dropped = false;

  /* Where a pillar's body should stand, given the point it has to reach.
     Straight below the contact the part itself is often in the way; then the
     pillar would start on the model and leave a mark. So we step aside and take
     the spot with the clearest way down — PrusaSlicer's "find a path to the
     build platform", in its simplest form: one ring of candidates, first one
     that reaches the plate wins. No detour hunting (V 08-13). */
  /* Which way the surface hangs, at this pixel. Everything supported nearby sits
     on one side; the overhang leans away from it. The direction from that mass
     towards us is, near enough, the in-plane part of the surface normal — and
     PrusaSlicer aims its heads along the normal ("cones perpendicular to the
     object surface"). Conveniently it is also the side with the clearest way
     down, since the part is behind us. */
  const hangDir = (x, y) => {
    let sx2 = 0, sy2 = 0, n = 0;
    for (let dy = -3; dy <= 3; dy++) {
      const yy = y + dy;
      if (yy < 0 || yy >= H) continue;
      for (let dx = -3; dx <= 3; dx++) {
        const xx = x + dx;
        if (xx < 0 || xx >= W) continue;
        if (prev[yy * W + xx]) { sx2 += dx; sy2 += dy; n++; }
      }
    }
    if (!n) return null;                          // island: nothing to lean off
    const len = Math.hypot(sx2, sy2);
    if (len < 1e-6) return null;                  // supported all round
    return [-sx2 / len, -sy2 / len];
  };

  const pickAxis = (x, y, base, dir) => {
    if (base < 0) return { ax: x, ay: y, abase: -1 };
    let bx = x, by = y, bs = base;
    const off = Math.round(SUP.tiltMm * (W / PLATE.x));
    // Start looking where the surface hangs, then fan out to both sides.
    const a0 = dir ? Math.atan2(dir[1], dir[0]) : 0;
    for (let k = 0; k < 8; k++) {
      const ang = a0 + Math.ceil(k / 2) * (k % 2 ? -1 : 1) * Math.PI / 4;
      const nx = Math.round(x + Math.cos(ang) * off);
      const ny = Math.round(y + Math.sin(ang) * off);
      if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
      const s = lastSolid[ny * W + nx];
      if (s < bs) { bs = s; bx = nx; by = ny; }
      if (bs < 0) break;                          // reaches the plate — done
    }
    return { ax: bx, ay: by, abase: bs };
  };

  for (let i = 0; i < layers; i++) {
    const zNow = (i + 0.5) * LAYER_MM;
    sliceAt(pos, zNow, seg);
    /* Second pass (self-check) looks at the layers WITH the supports in them. */
    let extra = null;
    if (withSupports) {
      extra = pillarDiscs(withSupports.pillars, zNow);
      if (withSupports.braces && zNow >= SUP.padMm)
        for (const d of braceDiscs(withSupports.braces, zNow)) extra.push(d);
    }
    rasterise(seg, grey, false, extra);   // no antialiasing: the mask is enough
    let hanging = 0;
    for (let p = 0; p < N; p++) { cur[p] = grey[p] > 0.5 ? 1 : 0; over[p] = 0; }
    if (i > 0) {
      for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
          const p = y * W + x;
          if (!cur[p]) continue;
          let held = 0;
          for (let dy = -1; dy <= 1 && !held; dy++) {
            const yy = y + dy;
            if (yy < 0 || yy >= H) continue;
            for (let dx = -1; dx <= 1; dx++) {
              const xx = x + dx;
              if (xx < 0 || xx >= W) continue;
              if (prev[yy * W + xx]) { held = 1; break; }
            }
          }
          if (!held) { over[p] = 1; hanging++; }
        }
      }
      /* Islands FIRST, before any filtering: a patch with nothing at all beneath
         it must be held even if something else happens to stand nearby. Nothing
         connects them, so "close to support" does not mean "supported". */
      if (hanging) {
        seen.fill(0);
        for (let s = 0; s < N; s++) {
          if (!over[s] || seen[s]) continue;
          let top = 0, all = 0, air = 0;
          stack[top++] = s; seen[s] = 1;
          while (top) {
            const p = stack[--top];
            all++; if (over[p]) air++;
            const x = p % W, y = (p / W) | 0;
            for (let dy = -1; dy <= 1; dy++) {
              const yy = y + dy;
              if (yy < 0 || yy >= H) continue;
              for (let dx = -1; dx <= 1; dx++) {
                const xx = x + dx;
                if (xx < 0 || xx >= W) continue;
                const q = yy * W + xx;
                if (cur[q] && !seen[q]) { seen[q] = 1; stack[top++] = q; }
              }
            }
          }
          if (all === air) {
            if (islandPillars.length + total < SUP.maxPillars) {
              const a = pickAxis(s % W, (s / W) | 0, lastSolid[s], null);
              islandPillars.push({ px: s % W, py: (s / W) | 0, seed: s,
                                   area: all * PIXEL_AREA,
                                   ax: a.ax, ay: a.ay, layer: i, abase: a.abase });
            } else dropped = true;
          }
        }
      }
      /* Ledges shorter than the reach carry themselves. PrusaSlicer has no such
         filter — it supports every overhang and controls the count by spacing
         instead — but at 0.1275 mm per pixel a one-pixel step is not an overhang
         at all, so a small cutoff stays. It is much smaller than it was. */
      if (hanging) {
        distanceToSupport();
        for (let p = 0; p < N; p++)
          if (over[p] && dist[p] <= reach3) { over[p] = 0; hanging--; }
      }
      /* Šlaitai iš geometrijos — PO „reach" filtro, nes jiems tas klausimas
         netinka: šlaitas visada turi kaimyną po savimi (tuo jis ir skiriasi
         nuo stačios nuokabos), tad filtras išmestų juos visus iki vieno. */
      if (geoPts) {
        const geo = geoPts.get(i);
        if (geo) for (const p of geo) if (cur[p] && !over[p]) { over[p] = 1; hanging++; }
      }
    }
    /* Most layers hang nowhere; those cost nothing beyond the scan above. */
    if (hanging) {
      for (let p = 0; p < N; p++) {
        if (!over[p]) continue;
        const x = p % W, y = (p / W) | 0;
        const cx = (x / cellPx) | 0, cy = (y / cellPx) | 0;
        const k = cx * 4096 + cy;
        const mx = (cx + 0.5) * cellPx, my = (cy + 0.5) * cellPx;
        let list = cells.get(k);
        if (!list) { list = []; cells.set(k, list); }
        // lastSolid is still the state BELOW this layer — it is updated after.
        const base = lastSolid[p];
        /* One cell can hold several pillars, one per thing they stand on: a
           C-shaped part has a lower arm hanging over the plate and an upper arm
           hanging over the lower one, both in the same 3 mm square. Keeping only
           the highest would leave the lower arm with nothing at all (V 08-13). */
        let c = null;
        for (const q of list) if (Math.abs(q.base - base) <= SUP.sameBase) { c = q; break; }
        if (!c) {
          if (total >= SUP.maxPillars) { dropped = true; continue; }   // never silent
          c = { px: x, py: y, ax: x, ay: y, d: Infinity, layer: -1, base, mx, my };
          list.push(c); total++;
        }
        // Within one layer the pillar takes the overhang pixel nearest the
        // middle of the cell. The average of them would read nicer, but on a
        // ring-shaped overhang the average lands in the hole — the pillar would
        // then grow beside the thing it is meant to hold (audit, 08-13).
        if (c.layer !== i) { c.d = Infinity; c.layer = i; }
        const d = (x - mx) * (x - mx) + (y - my) * (y - my);
        if (d < c.d) {
          const a = pickAxis(x, y, base, hangDir(x, y));
          /* base = what is under the CONTACT point; it groups pillars into
             "the same thing they hold". abase = what is under the BODY; that
             is where the pillar actually starts. Mixing the two made every
             pixel start its own pillar (caught by the C-shape test). */
          c.d = d; c.px = x; c.py = y;
          c.ax = a.ax; c.ay = a.ay; c.base = base; c.abase = a.abase;
        }
      }
    }   // islands were handled above, before the distance filter
    for (let p = 0; p < N; p++) if (cur[p]) {
      lastSolid[p] = i;
      if (firstSolid[p] < 0) firstSolid[p] = i;
    }
    prev.set(cur);
    if (onProgress && (i % 8 === 0 || i === layers - 1)) {
      onProgress(i + 1, layers);
      await new Promise(r => setTimeout(r, 0));
    }
  }

  /* Which of the candidates were really islands. Costs one extra slice each,
     and there are only a handful — cheap next to the pass we just did. */
  {
    const keep = [];
    for (const isl of islandPillars) {
      if (isl.area > SUP.islandBigMm2) { keep.push(isl); continue; }
      const j = Math.min(layers - 1, isl.layer + SUP.lookLayers);
      sliceAt(pos, (j + 0.5) * LAYER_MM, seg);
      rasterise(seg, grey, false);
      for (let p = 0; p < N; p++) cur[p] = grey[p] > 0.5 ? 1 : 0;
      let size = 0;
      if (cur[isl.seed]) {
        seen.fill(0);
        let top = 0;
        stack[top++] = isl.seed; seen[isl.seed] = 1;
        while (top) {
          const p = stack[--top]; size++;
          const x = p % W, y = (p / W) | 0;
          for (let dy = -1; dy <= 1; dy++) {
            const yy = y + dy;
            if (yy < 0 || yy >= H) continue;
            for (let dx = -1; dx <= 1; dx++) {
              const xx = x + dx;
              if (xx < 0 || xx >= W) continue;
              const q = yy * W + xx;
              if (cur[q] && !seen[q]) { seen[q] = 1; stack[top++] = q; }
            }
          }
        }
      }
      // Grown into something far bigger → it was a beginning, not an island.
      if (size * PIXEL_AREA <= isl.area * SUP.mergeFactor) keep.push(isl);
    }
    islands = keep.length;
    firstIsland = keep.length ? keep[0].layer + 1 : 0;
    islandPillars.length = 0;
    for (const k of keep) islandPillars.push(k);
  }

  const sx = W / PLATE.x, sy = H / PLATE.y;

  /* Kaip toli nuo detalės silueto stovi kiekvienas plokštės taškas. Siluetas =
     viskas, kur nors kada nors buvo medžiagos (firstSolid >= 0). PrusaSlicer
     laiko bokštą atokiai (support_base_safety_distance = 1 mm), ir išmatavus
     jo sluoksnius matyti, kad praktikoje jo stulpai stovi kelis milimetrus nuo
     detalės — būtent todėl jo narvas matomas iš išorės, o mūsų buvo prilipęs
     (V 08-13: „vos matosi supportai"). Chamfer 3-4, kaip ir kitur: skaičiai
     yra 3× pikselių atstumas. */
  const silh = new Int32Array(N);
  {
    const BIG2 = 1 << 28;
    for (let p = 0; p < N; p++) silh[p] = firstSolid[p] >= 0 ? 0 : BIG2;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const p = y * W + x; let d = silh[p]; if (!d) continue;
      if (x > 0 && silh[p-1] + 3 < d) d = silh[p-1] + 3;
      if (y > 0) {
        if (silh[p-W] + 3 < d) d = silh[p-W] + 3;
        if (x > 0 && silh[p-W-1] + 4 < d) d = silh[p-W-1] + 4;
        if (x < W-1 && silh[p-W+1] + 4 < d) d = silh[p-W+1] + 4;
      }
      silh[p] = d;
    }
    for (let y = H-1; y >= 0; y--) for (let x = W-1; x >= 0; x--) {
      const p = y * W + x; let d = silh[p]; if (!d) continue;
      if (x < W-1 && silh[p+1] + 3 < d) d = silh[p+1] + 3;
      if (y < H-1) {
        if (silh[p+W] + 3 < d) d = silh[p+W] + 3;
        if (x < W-1 && silh[p+W+1] + 4 < d) d = silh[p+W+1] + 4;
        if (x > 0 && silh[p+W-1] + 4 < d) d = silh[p+W-1] + 4;
      }
      silh[p] = d;
    }
  }
  const standoff3 = Math.round(SUP.standoffMm * sx * 3);

  /* ---------------------------------------------------------------- towers */
  /* Kiekvienam atramos taškui klausiam ne „kur po juo atsistoti", o „iš kur jį
     pasiekti" — ir einam eilės tvarka, sustodami ties pirma pavykusia:
       1) tiesiai žemyn, jei kelias iki plokštės laisvas (tilto nereikia);
       2) į jau stovintį bokštą per ≤10 mm — vienas bokštas laiko kelis taškus,
          ir būtent tai neleidžia bokštams pridygti;
       3) naujas bokštas į šoną, ieškant žiedais po 1 mm iki 10 mm;
       4) į pačią detalę — tik jei niekas nepavyko. Iki 0.9.0 tai buvo pirmas
          atsakymas, ir todėl 8 iš 12 supportų lipdavo ant modelio. */
  const towerMode = SUP.style !== 'direct';
  const towers = [], bridges = [];
  if (towerMode) {
    const joinPx = SUP.towerJoinMm * sx;
    const reachPx = SUP.bridgeMaxMm * sx;
    const headLayers = Math.round(SUP.towerHeadMm / LAYER_MM);
    const freeToPlate = (px, py, upto) => {
      const f = firstSolid[py * W + px];
      return f < 0 || f >= upto;
    };
    /* 45° tiltas: kiek pakyla, tiek ir nueina, tad sluoksnių skaičius lygus
       atstumui pikseliais, perskaičiuotam per sluoksnio storį. */
    const climb = dPx => Math.round(dPx / sx / LAYER_MM);

    const pts = [];
    for (const list of [...cells.values(), islandPillars])
      for (const c of list) pts.push(c);
    // Žemesni pirma: tada aukštesni turi į ką kabintis.
    pts.sort((a, b) => a.layer - b.layer);

    for (const c of pts) {
      const px = c.px, py = c.py;
      /* Vienas kelias visiems: surandam bokštą (esamą arba naują) ir permetam
         nuo jo tiltą. Net kai bokštas stovi tiesiai po tašku, „tiltas" tiesiog
         vertikalus — tai jo galvutė. Taip bokštas gali AUGTI ir aptarnauti
         kelis taškus, o kiekvienas taškas turi savo kontaktą: anksčiau, keliant
         bokšto viršūnę, ankstesnis taškas jo netekdavo. */
      let tower = null, span = 0;

      // 1 · jau stovintis bokštas per ≤10 mm. Bokštas brangus, tiltas pigus.
      let bestD = Infinity;
      for (const t of towers) {
        const d = Math.hypot(t.px - px, t.py - py);
        /* Tiltas laikosi 45°: kiek nueina į šoną, tiek ir pakyla. Jei taškas
           per žemas, kad tas kampas tilptų, bokštas netinka — kitaip gaunam
           beveik horizontalią giją, kuri pati kabo ore. */
        const wants = c.layer - climb(d) - headLayers;
        /* Bokštas AUGA, kai priima aukštesnį tašką — tad tikrinam ne tik ar jis
           stovi, bet ar jo kelias nuo plokštės laisvas IKI TO NAUJO AUKŠČIO.
           Be šito bokštas, pastatytas žemam taškui, vėliau priimdavo aukštą ir
           užaugdavo tiesiai per detalę (V 08-13: „eina kiaurai per modelį"). */
        if (d <= joinPx && d < bestD && wants > 0 &&
            freeToPlate(t.px, t.py, wants)) { bestD = d; tower = t; span = d; }
      }
      /* 2 · tiesiai žemyn — naujas bokštas po pačiu tašku, BET tik jei ten
         nėra prilipta prie detalės. Būtent ši pakopa ir laikė mūsų narvą
         priglaudusį: taškas beveik visada yra ant detalės krašto, po juo
         kelias laisvas, ir bokštas atsistodavo per 0,17 mm nuo paviršiaus.
         PrusaSlicer tuo pačiu atveju pasitraukia (išmatuota: jo stulpų
         mediana 2–9 mm nuo detalės, mūsų buvo 0,17 mm). Nepavykus — krenta
         į šoninę paiešką žemiau, kuri ieško vietos su tarpu. */
      if (!tower && silh[py * W + px] >= standoff3 && freeToPlate(px, py, c.layer)) {
        tower = { px, py, top: 0 };
        towers.push(tower);
        span = 0;
      }
      /* 3 · naujas bokštas į šoną, žiedais po 1 mm. DU praėjimai: pirmas ieško
         vietos, kur medžiagos nebuvo NIEKADA — t. y. lauke, už modelio silueto.
         Tik neradus tokios, tenkinamės vieta, kur laisva iki reikiamo aukščio.
         Be šito bokštai lipdavo į modelio įdubas ir dingdavo jo siluete;
         PrusaSlicer stato narvą aplink, ir tai matosi iš pirmo žvilgsnio
         (V 08-13, palyginus renderius). */
      /* TRYS pakopos, ne dvi: pirmiausia lauke IR atokiau nei standoff, tada
         tiesiog lauke, ir tik paskui bet kur, kur laisva iki reikiamo aukščio.
         Pirmoji pakopa yra tai, kas mūsų narvą iškelia iš detalės silueto. */
      if (!tower) {
        for (const step of [2, 1, 0]) {
          for (let r = sx; r <= reachPx && !tower; r += sx) {
            for (let a = 0; a < 16; a++) {
              const ang = a / 16 * Math.PI * 2;
              const nx = Math.round(px + Math.cos(ang) * r);
              const ny = Math.round(py + Math.sin(ang) * r);
              if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
              const start = c.layer - climb(r) - headLayers;
              if (start <= 0) continue;
              const q = ny * W + nx;
              if (step === 2 && (firstSolid[q] >= 0 || silh[q] < standoff3)) continue;
              if (step === 1 && firstSolid[q] >= 0) continue;
              if (step === 0 && !freeToPlate(nx, ny, start)) continue;
              tower = { px: nx, py: ny, top: 0 };
              towers.push(tower);
              span = r;
              break;
            }
          }
          if (tower) break;
        }
      }
      // 4 · niekas nepavyko — lieka senasis kelias, atsiremti į detalę
      if (!tower) { c.done = false; continue; }

      const start = c.layer - climb(span) - headLayers;
      if (start <= 0) { c.done = false; continue; }   // 45° netelpa
      /* Bokštas, kuris gaunasi trumpesnis nei 1 mm, nėra bokštas — taškas yra
         prie pat plokštės ir laikysis pats. Be šito 44 iš 50 „bokštų" buvo
         0,03 mm ilgio: skaitiklis rodė 50, ekrane matėsi šeši (V 08-13,
         antrą kartą ta pati klaidos rūšis). */
      if (start * LAYER_MM < SUP.minLenMm) { c.done = false; continue; }
      if (start > tower.top) tower.top = start;
      bridges.push({ ax: tower.px, ay: tower.py, z0: start,
                     bx: px, by: py, z1: c.layer });
      c.done = true;
    }
  }

  const pillars = [];
  let onModel = 0;
  /* Bokštai — tikri stulpeliai nuo plokštės; tiltai pridedami prie jungčių,
     nes piešiami lygiai taip pat (slenkantis diskas). */
  for (const t of towers) {
    /* Bokštas įrašomas į sąrašą anksčiau, nei paaiškėja, ar jo prireiks: jei
       visi jį naudoję taškai pasirodė per žemi, jis lieka nulinio aukščio.
       Tokių į geometriją neleidžiam — būtent jie ir buvo tie „50 supportų,
       kurių ekrane šeši". */
    if (t.top * LAYER_MM < SUP.minLenMm) continue;
    pillars.push({ x: (t.px + 0.5) / sx - PLATE.x / 2,
                   y: (t.py + 0.5) / sy - PLATE.y / 2,
                   cx: (t.px + 0.5) / sx - PLATE.x / 2,
                   cy: (t.py + 0.5) / sy - PLATE.y / 2,
                   top: (t.top + 0.5) * LAYER_MM, bottom: 0, tower: true });
  }
  for (const list of [...cells.values(), islandPillars]) {
    for (const c of list) {
      // Bokštas jį jau pasiekė — antro supporto tam pačiam taškui nereikia.
      if (c.done) continue;
      /* Starts on top of whatever was last there, or on the plate if nothing
         ever was. Half a layer up so the foot sits on the part, not inside it. */
      const bottomMm = c.abase >= 0 ? (c.abase + 0.5) * LAYER_MM : 0;
      const topMm = (c.layer + 0.5) * LAYER_MM;
      /* Too short to be a support — see SUP.minLenMm. Islands are exempt: a
         patch floating 0.3 mm above the plate is still floating. */
      if (c.seed === undefined && topMm - bottomMm < SUP.minLenMm) continue;
      if (c.abase >= 0) onModel++;
      pillars.push({
        x: (c.px + 0.5) / sx - PLATE.x / 2,     // where it touches the part
        y: (c.py + 0.5) / sy - PLATE.y / 2,
        cx: (c.ax + 0.5) / sx - PLATE.x / 2,    // where its body stands
        cy: (c.ay + 0.5) / sy - PLATE.y / 2,
        top: topMm,
        bottom: bottomMm
      });
    }
  }
  /* Companions for the lonely. A pillar that got no brace and stands tall is
     a bare stick in the resin; PrusaSlicer answers that by adding one or two
     pillars beside it. The companion runs from the plate up to just under the
     tall one's bend, and the braces are recomputed so the two get tied. */
  /* Tiltai nuo bokštų į atramos taškus. Piešiami lygiai kaip jungtys, tad
     tiesiog prisideda prie to paties sąrašo. Skiriasi tik tuo, kad jie eina
     iki paties kontakto taško ir yra šiek tiek storesni. */
  const asBridge = b => ({
    ax: (b.ax + 0.5) / sx - PLATE.x / 2, ay: (b.ay + 0.5) / sy - PLATE.y / 2,
    bx: (b.bx + 0.5) / sx - PLATE.x / 2, by: (b.by + 0.5) / sy - PLATE.y / 2,
    z0: (b.z0 + 0.5) * LAYER_MM, z1: (b.z1 + 0.5) * LAYER_MM, bridge: true
  });
  let braces = makeBraces(pillars);
  for (const b of bridges) braces.push(asBridge(b));
  const tied = new Set();
  const at = (x, y) => x.toFixed(2) + ',' + y.toFixed(2);
  for (const c of braces) { tied.add(at(c.ax, c.ay)); tied.add(at(c.bx, c.by)); }
  const companions = [];
  for (const p of pillars) {
    if (p.top - p.bottom < SUP.loneMinMm) continue;
    if (tied.has(at(p.cx, p.cy))) continue;
    if (pillars.length + companions.length >= SUP.maxPillars) { dropped = true; break; }
    /* Beside it, on the side where the way down is clearest. */
    const px = Math.round((p.cx + PLATE.x / 2) * (W / PLATE.x));
    const py = Math.round((p.cy + PLATE.y / 2) * (H / PLATE.y));
    const off = Math.round(SUP.loneOffMm * (W / PLATE.x));
    let best = null;
    for (let a = 0; a < 8; a++) {
      const ang = a * Math.PI / 4;
      const nx = px + Math.round(Math.cos(ang) * off);
      const ny = py + Math.round(Math.sin(ang) * off);
      if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
      const s = lastSolid[ny * W + nx];
      if (!best || s < best.s) best = { nx, ny, s };
      if (s < 0) break;
    }
    if (!best) continue;
    const bottom = best.s >= 0 ? (best.s + 0.5) * LAYER_MM : 0;
    const topAt = Math.max(p.bottom, p.top - SUP.tiltMm);
    if (topAt - bottom < SUP.braceMinMm) continue;   // too short to be of use
    const x = (best.nx + 0.5) / sx - PLATE.x / 2;
    const y = (best.ny + 0.5) / sy - PLATE.y / 2;
    companions.push({ x, y, cx: x, cy: y, top: topAt, bottom, companion: true });
  }
  if (companions.length) {
    for (const c of companions) pillars.push(c);
    // Perskaičiuojam, bet tiltai lieka — jie ne tarp bokštų, o į detalę.
    braces = makeBraces(pillars);
    for (const b of bridges) braces.push(asBridge(b));
  }

  /* Pado kaukė: kas liečia plokštę (modelis) plius bokštų kojos, išplėsta per
     apvadą. Chamfer 3-4, tas pats principas kaip atstumo iki paramos matas. */
  let pad = null;
  if (SUP.padLayers > 0) {
    const BIGP = 1 << 28, wall3 = Math.round(SUP.padWallMm * sx * 3);
    const d2 = new Int32Array(N).fill(BIGP);
    for (let p = 0; p < N; p++) if (firstSolid[p] === 0) d2[p] = 0;
    for (const t of towers) d2[t.py * W + t.px] = 0;
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        const p = y * W + x; let d = d2[p];
        if (!d) continue;
        if (x > 0 && d2[p-1] + 3 < d) d = d2[p-1] + 3;
        if (y > 0) {
          if (d2[p-W] + 3 < d) d = d2[p-W] + 3;
          if (x > 0 && d2[p-W-1] + 4 < d) d = d2[p-W-1] + 4;
          if (x < W-1 && d2[p-W+1] + 4 < d) d = d2[p-W+1] + 4;
        }
        d2[p] = d;
      }
    for (let y = H-1; y >= 0; y--)
      for (let x = W-1; x >= 0; x--) {
        const p = y * W + x; let d = d2[p];
        if (!d) continue;
        if (x < W-1 && d2[p+1] + 3 < d) d = d2[p+1] + 3;
        if (y < H-1) {
          if (d2[p+W] + 3 < d) d = d2[p+W] + 3;
          if (x < W-1 && d2[p+W+1] + 4 < d) d = d2[p+W+1] + 4;
          if (x > 0 && d2[p+W-1] + 4 < d) d = d2[p+W-1] + 4;
        }
        d2[p] = d;
      }
    pad = new Uint8Array(N);
    let area = 0;
    for (let p = 0; p < N; p++) if (d2[p] <= wall3) { pad[p] = 1; area++; }
    pad.areaMm2 = area * PIXEL_AREA;
  }

  return { pillars, braces, companions: companions.length,
           towers: towers.length, bridges: bridges.length, pad,
           padMm2: pad ? pad.areaMm2 : 0,
           islands, firstIsland, onModel, dropped };
}

/* Store-only ZIP. The PNGs are already compressed, so deflating them again
   would cost time and save nothing. */
function crc32(buf, table) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = table[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
/** Vieno sluoksnio kaukė — modelis PLIUS supportai, tokia, kokia keliaus į
 *  ekraną. Skirta patikroms be naršyklės (`slice()` koduoja PNG per canvas, o
 *  jo Node'e nėra): stendas ir izometrinis palyginimas su PrusaSlicer'io
 *  sluoksniais naudoja būtent šitą, kad matytų TĄ PATĮ, ką matys derva. */
export function layerMask(pos, z, sup) {
  const grey = new Float32Array(RES.w * RES.h);
  const seg = [];
  sliceAt(pos, z, seg);
  let extra = null;
  if (sup) {
    extra = pillarDiscs(sup.pillars, z);
    if (sup.braces && sup.braces.length && z >= SUP.padMm)
      for (const d of braceDiscs(sup.braces, z)) extra.push(d);
  }
  rasterise(seg, grey, true, extra);
  // Padas — tas pats kelias kaip slice(): ne diskai, o gatavas pikselių žemėlapis.
  const layer = Math.round(z / LAYER_MM - 0.5);
  if (sup && sup.pad && layer < SUP.padLayers)
    for (let p = 0; p < sup.pad.length; p++) if (sup.pad[p]) grey[p] = 1;
  const out = new Uint8Array(RES.w * RES.h);
  for (let i = 0; i < out.length; i++) out[i] = Math.min(255, Math.round(grey[i] * 255));
  return out;
}

function crcTable() {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
}
export function zipStore(files) {          // [{name, data:Uint8Array}]
  const T = crcTable(), parts = [], dir = [];
  let off = 0;
  const enc = new TextEncoder();
  for (const f of files) {
    const nm = enc.encode(f.name), c = crc32(f.data, T), n = f.data.length;
    const h = new Uint8Array(30 + nm.length), dv = new DataView(h.buffer);
    dv.setUint32(0, 0x04034b50, true); dv.setUint16(4, 20, true);
    dv.setUint32(14, c, true); dv.setUint32(18, n, true); dv.setUint32(22, n, true);
    dv.setUint16(26, nm.length, true);
    h.set(nm, 30);
    parts.push(h, f.data);
    const d = new Uint8Array(46 + nm.length), dd = new DataView(d.buffer);
    dd.setUint32(0, 0x02014b50, true); dd.setUint16(4, 20, true); dd.setUint16(6, 20, true);
    dd.setUint32(16, c, true); dd.setUint32(20, n, true); dd.setUint32(24, n, true);
    dd.setUint16(28, nm.length, true); dd.setUint32(42, off, true);
    d.set(nm, 46);
    dir.push(d);
    off += h.length + n;
  }
  let dirLen = 0; for (const d of dir) dirLen += d.length;
  const end = new Uint8Array(22), de = new DataView(end.buffer);
  de.setUint32(0, 0x06054b50, true);
  de.setUint16(8, dir.length, true); de.setUint16(10, dir.length, true);
  de.setUint32(12, dirLen, true); de.setUint32(16, off, true);
  return new Blob([...parts, ...dir, end], { type: 'application/zip' });
}

/**
 * Slices placed plate-space geometry into the printer's archive.
 * onProgress(done, total) is called between layers so the caller can paint.
 */
export async function slice(pos, opts, onProgress) {
  const aa = opts && opts.antialias !== false;
  const b = bounds(pos);
  const layers = Math.max(1, Math.ceil(b.size[2] / LAYER_MM));

  /* Supports first, layers second. A pillar has to know how high to climb, and
     that is only known after walking the whole part once (V 08-13). The caller
     is told which pass is running so its bar does not restart at zero. */
  /* Supportų ieškotoją galima paduoti iš šalies (opts.findSupports). Tuo
     naudojasi antrasis algoritmas `slicer2.js`: sluoksnių piešimas, peržiūra
     ir ZIP jam tokie patys, skiriasi tik supportai — dubliuoti visą šitą
     funkciją reikštų dvi vietas tai pačiai klaidai (V 08-13). */
  const finder = (opts && opts.findSupports) || findOverhangs;
  const sup = opts && opts.supports === false
    ? { pillars: [], braces: [], islands: 0, firstIsland: 0, onModel: 0 }
    : await finder(pos, layers,
        onProgress && ((d, t) => onProgress(d, t, 'scan')));

  /* Self-check. The supports themselves must not hang: every one of them is
     drawn from the plate or from the part upwards, so a second look at the
     FINISHED layers must not find any island that was not there before. Today
     this kind of mistake was caught by eye, three times running — the code has
     to catch it itself (V 08-13). */
  let hanging = 0;
  if (sup.pillars.length) {
    const after = await (finder === findOverhangs ? findOverhangs(pos, layers, null, sup) : Promise.resolve({ islands: sup.islands }));
    hanging = Math.max(0, after.islands - sup.islands);
  }

  const grey = new Float32Array(RES.w * RES.h);
  const seg = [];
  const cv = document.createElement('canvas');
  cv.width = RES.w; cv.height = RES.h;
  const ctx = cv.getContext('2d', { willReadFrequently: true });
  const img = ctx.createImageData(RES.w, RES.h);
  const files = [];
  let whiteSum = 0;

  /* Kartu su pilnos raiskos sluoksniais renkam reta 80x60 rinkini: butent tokio
     tikisi esamas 3D pieseejas, kuris jau piesia spausdinimo progresa. Renkam
     OR budu - uztenka vieno uzdegto pikselio, kad langelis butu pilnas, nes
     vidurkinimas nuzudo plonus supportus. */
  /* Slicer’io vaizdas VISADA detalus: pilna raiska jau atmintyje, tad rinkti
     grubiau nera jokios priezasties. 80x60/36 buvo tiesiog nukopijuotas SD
     modeliu greitosios perziuros skaicius (V 08-12). */
  /* Peržiūros tinklelis — PILNA raiška. 160x120 buvo paveldėta iš SD modelių
     greitosios peržiūros, kur ji ir prasminga: ten sluoksniai atkeliauja per
     tinklą. Čia jie jau atmintyje, o atmintis yra NARŠYKLĖS, ne printerio —
     320x240x160 yra 12 MB, kompiuteriui niekis, o supportų gijos pagaliau
     matomos tokios, kokios yra (V 08-13: „naršyklės atmintis dzin"). */
  const PW = RES.w, PH = RES.h, PN = Math.min(160, layers);
  const preview = [];
  const previewAt = new Set();
  for (let k = 0; k < PN; k++)
    previewAt.add(PN > 1 ? Math.round(k * (layers - 1) / (PN - 1)) : 0);

  for (let i = 0; i < layers; i++) {
    const z = (i + 0.5) * LAYER_MM;                 // sample mid-layer
    sliceAt(pos, z, seg);
    /* Piešėją, kaip ir supportų ieškotoją, galima paduoti iš šalies: antrasis
       algoritmas turi savus matmenis, ir piešiant jį šito failo skaičiais
       ekrane matėsi ne tai, kas suskaičiuota (auditas 08-13). Nepaduotas —
       viskas kaip buvo. */
    const discs = (opts && opts.discsFor)
      ? opts.discsFor(sup, z, i)
      : (() => {
          const d = pillarDiscs(sup.pillars, z);
          // Braces start above the feet — down there the pillars are already wide.
          if (sup.braces && sup.braces.length && z >= SUP.padMm)
            for (const b of braceDiscs(sup.braces, z)) d.push(b);
          return d;
        })();
    rasterise(seg, grey, aa, discs);
    // Padas — pirmi sluoksniai užpildomi ištisai po viskuo, kas ten stovi.
    const padLayers = (opts && opts.padLayers) || SUP.padLayers;
    if (sup.pad && i < padLayers)
      for (let p = 0; p < sup.pad.length; p++) if (sup.pad[p]) grey[p] = 1;
    let lit = 0;
    for (let p = 0, q = 0; p < grey.length; p++, q += 4) {
      let v = grey[p]; if (v > 1) v = 1;
      const g = (v * 255) | 0;
      img.data[q] = g; img.data[q + 1] = g; img.data[q + 2] = g; img.data[q + 3] = 255;
      lit += v;
    }
    whiteSum += lit / grey.length;
    if (previewAt.has(i)) {
      const c = new Uint8Array(PW * PH);
      for (let y = 0; y < RES.h; y++) {
        const row = ((y * PH / RES.h) | 0) * PW;
        for (let x = 0; x < RES.w; x++)
          if (grey[y * RES.w + x] > 0.5) c[row + ((x * PW / RES.w) | 0)] = 1;
      }
      preview.push(c);
    }
    ctx.putImageData(img, 0, 0);
    const blob = await new Promise(r => cv.toBlob(r, 'image/png'));
    files.push({ name: (i + 1) + '.png', data: new Uint8Array(await blob.arrayBuffer()) });
    if (onProgress && (i % 8 === 0 || i === layers - 1)) {
      onProgress(i + 1, layers, 'draw');
      await new Promise(r => setTimeout(r, 0));     // let the bar actually paint
    }
  }

  const ini = 'layerHeight = ' + LAYER_MM.toFixed(2) + '\n' +
              'jobDir = tinymaker\n' +
              'numFast = ' + layers + '\n';
  files.push({ name: 'config.ini', data: new TextEncoder().encode(ini) });

  // Same maths the printer uses, so the number shown is the number it means.
  const rawMl = (whiteSum / layers) * PLATE.x * PLATE.y * (layers * LAYER_MM) / 1000;
  // files grazinami ir kvieciancia jam: is ju piesiama sluoksniu perziura,
  // kad zmogus pamatytu, ka issaugos, PRIES issaugodamas.
  return { blob: zipStore(files), files, layers, rawMl,
           supports: { pillars: sup.pillars.length, onModel: sup.onModel,
                       braces: sup.braces.length, hanging,
                       raft: sup.pillars.some(p => p.bottom === 0),
                       islands: sup.islands, firstIsland: sup.firstIsland,
                       /* Sarasas lieka - is jo pultas piesia supportus KITA
                          spalva, kad zmogus matytu, kur modelis, o kur atrama
                          (V 08-13, butina). */
                       list: sup.pillars, braceList: sup.braces },
           preview: { slices: preview, gw: PW, gh: PH,
                      modelH: layers * LAYER_MM } };
}

export const VERSION = '0.9.0';
