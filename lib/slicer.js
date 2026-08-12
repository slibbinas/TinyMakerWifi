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

/** Segments where triangles cross plane z. Each is [x0,y0,x1,y1] in mm. */
function sliceAt(pos, z, out) {
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
    if (px.length === 2) out.push(px[0], py[0], px[1], py[1]);
  }
  return out;
}

/** Even-odd scanline fill with sub-row sampling; returns a 320x240 grey map.
    `discs` are supports drawn straight into the same layer — see below. */
function rasterise(seg, grey, aa, discs) {
  grey.fill(0);
  const W = RES.w, H = RES.h;
  const sx = W / PLATE.x, sy = H / PLATE.y;               // mm -> px
  const subs = aa ? SUB : 1;
  const xs = [];
  for (let row = 0; row < H; row++) {
    for (let sub = 0; sub < subs; sub++) {
      // pixel row centre in mm, measured from the plate's near edge
      const yPix = row + (sub + 0.5) / subs;
      const yMm = yPix / sy - PLATE.y / 2;
      xs.length = 0;
      for (let s = 0; s < seg.length; s += 4) {
        const y0 = seg[s + 1], y1 = seg[s + 3];
        if ((y0 > yMm) === (y1 > yMm)) continue;
        const t = (yMm - y0) / (y1 - y0);
        xs.push((seg[s] + (seg[s + 2] - seg[s]) * t + PLATE.x / 2) * sx);
      }
      if (xs.length < 2) continue;
      xs.sort((a, b) => a - b);
      for (let k = 0; k + 1 < xs.length; k += 2) {
        let a = xs[k], b = xs[k + 1];
        if (b <= 0 || a >= W) continue;
        if (a < 0) a = 0; if (b > W) b = W;
        const ia = Math.floor(a), ib = Math.floor(b);
        const base = row * W;
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
const SUP = {
  gridMm:  3.0,    // one pillar per 3 mm of overhang — also caps their number
  rMm:     0.45,   // pillar body, Ø0.9 mm
  tipMm:   0.15,   // tip, Ø0.3 mm — thin enough to snap off cleanly
  taperMm: 0.6,    // the tip narrows over this last stretch
  raftLayers: 5,   // 0.25 mm of foot
  /* Ø2.4 mm feet on a 3 mm grid stay separate islands. Wider ones (Ø4 mm was
     the first guess) merge into one sheet across the whole plate, and a sheet
     that size peels off the FEP with far more force than it should. */
  raftRMm: 1.2,
  /* Two overhangs in one cell count as the same pillar when they stand on the
     same thing — within 1 mm of each other. Further apart (an arm over an arm)
     they each get their own. */
  sameBase: 20,      // layers = 1 mm
  maxPillars: 400    // a messy STL must not turn into a forest
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
function pillarDiscs(pillars, z, raft) {
  const out = [];
  for (const p of pillars) {
    if (z > p.top || z < p.bottom) continue;
    const left = p.top - z;
    let r = left < SUP.taperMm
              ? SUP.tipMm + (SUP.rMm - SUP.tipMm) * (left / SUP.taperMm)
              : SUP.rMm;
    /* A foot only makes sense under a pillar that actually stands on the plate
       and climbs above the raft. One that starts on the model has nothing to
       stand on down here, and one ending inside the raft would just be a disc. */
    if (raft && p.bottom === 0 && p.top > SUP.raftLayers * LAYER_MM) r = SUP.raftRMm;
    out.push({ x: p.x, y: p.y, r });
  }
  return out;
}

/* Where the part hangs in the air. One extra pass at the printing resolution —
 * a coarser scan would put pillars beside the overhang instead of under it.
 *
 * A pixel is an overhang when it is filled now and none of the nine pixels
 * below it (itself plus eight neighbours) was filled. One pixel of slack means
 * a slope shallower than ~21 deg asks for no help, which is what it should be.
 */
async function findOverhangs(pos, layers, onProgress) {
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
  const seg = [];
  const cells = new Map();            // 3 mm cell -> the pillars standing in it
  const cellPx = SUP.gridMm * (W / PLATE.x);
  let islands = 0, firstIsland = 0, total = 0, dropped = false;

  for (let i = 0; i < layers; i++) {
    sliceAt(pos, (i + 0.5) * LAYER_MM, seg);
    rasterise(seg, grey, false);      // no antialiasing: the mask is all we need
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
    }
    /* Most layers hang nowhere; those cost nothing beyond the scan above. */
    if (hanging) {
      for (let p = 0; p < N; p++) {
        if (!over[p]) continue;
        const x = p % W, y = (p / W) | 0;
        const cx = (x / cellPx) | 0, cy = (y / cellPx) | 0;
        const k = cx * 4096 + cy;
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
          c = { px: x, py: y, d: Infinity, layer: -1, base };
          list.push(c); total++;
        }
        // Within one layer the pillar takes the overhang pixel nearest the
        // middle of the cell. The average of them would read nicer, but on a
        // ring-shaped overhang the average lands in the hole — the pillar would
        // then grow beside the thing it is meant to hold (audit, 08-13).
        if (c.layer !== i) { c.d = Infinity; c.layer = i; }
        const mx = (cx + 0.5) * cellPx, my = (cy + 0.5) * cellPx;
        const d = (x - mx) * (x - mx) + (y - my) * (y - my);
        if (d < c.d) { c.d = d; c.px = x; c.py = y; c.base = base; }
      }
      /* An island is a patch with nothing at all beneath it — every pixel of it
         hangs. Same walk answers it, so it costs one flood fill. */
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
        if (all === air) { islands++; if (!firstIsland) firstIsland = i + 1; }
      }
    }
    for (let p = 0; p < N; p++) if (cur[p]) lastSolid[p] = i;
    prev.set(cur);
    if (onProgress && (i % 8 === 0 || i === layers - 1)) {
      onProgress(i + 1, layers);
      await new Promise(r => setTimeout(r, 0));
    }
  }

  const sx = W / PLATE.x, sy = H / PLATE.y;
  const pillars = [];
  let onModel = 0;
  for (const list of cells.values()) {
    for (const c of list) {
      /* Starts on top of whatever was last there, or on the plate if nothing
         ever was. Half a layer up so the foot sits on the part, not inside it. */
      if (c.base >= 0) onModel++;
      pillars.push({
        x: (c.px + 0.5) / sx - PLATE.x / 2,
        y: (c.py + 0.5) / sy - PLATE.y / 2,
        top: (c.layer + 0.5) * LAYER_MM,
        bottom: c.base >= 0 ? (c.base + 0.5) * LAYER_MM : 0
      });
    }
  }
  return { pillars, islands, firstIsland, onModel, dropped };
}

/* Store-only ZIP. The PNGs are already compressed, so deflating them again
   would cost time and save nothing. */
function crc32(buf, table) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = table[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
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
  const sup = opts && opts.supports === false
    ? { pillars: [], islands: 0, firstIsland: 0 }
    : await findOverhangs(pos, layers,
        onProgress && ((d, t) => onProgress(d, t, 'scan')));

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
  const PW = 160, PH = 120, PN = Math.min(72, layers);
  const preview = [];
  const previewAt = new Set();
  for (let k = 0; k < PN; k++)
    previewAt.add(PN > 1 ? Math.round(k * (layers - 1) / (PN - 1)) : 0);

  for (let i = 0; i < layers; i++) {
    const z = (i + 0.5) * LAYER_MM;                 // sample mid-layer
    sliceAt(pos, z, seg);
    /* The raft is the first 0.25 mm: the same pillars, widened into feet, so
       they hold on to the plate. With nothing to support it stays empty by
       itself — a part lying flat gets no raft and needs none. */
    const discs = pillarDiscs(sup.pillars, z, i < SUP.raftLayers);
    rasterise(seg, grey, aa, discs);
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
                       raft: sup.pillars.some(p => p.bottom === 0),
                       islands: sup.islands, firstIsland: sup.firstIsland },
           preview: { slices: preview, gw: PW, gh: PH,
                      modelH: layers * LAYER_MM } };
}

export const VERSION = '0.7.1';
