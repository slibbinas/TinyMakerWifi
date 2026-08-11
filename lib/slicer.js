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

export const VERSION = '0.1.0-stageB';
