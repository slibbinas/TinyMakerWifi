/* Tie patys modeliai, pasukti skirtingais kampais.
 *
 * V mintis (2026-08-19): atramų generavimui pasuktas modelis yra NAUJAS
 * modelis. Keturi modeliai vienoje padėtyje - keturi bandymai; po penkis
 * kampus - dvidešimt. Tik taip matyti, ar algoritmas veikia, ar tiesiog
 * pataikė į keturis atvejus.
 *
 * Sukam apie X ašį (modelis verčiamas ant šono), po to nuleidžiam ant plokštės
 * ir centruojam. Auto-orientavimo NEtaikom - kitaip jis viską sugrąžintų atgal
 * ir testo nebūtų.
 *
 *   node pasukimai.mjs [modelis.stl ...]
 */
import { readFileSync } from 'fs';

const MOD = process.env.SLICER || 'C:/PIO-build/exp2-wt/web/lib/slicer2.js';
const M = await import('file:///' + MOD.split('\\').join('/') + '?t=' + Date.now());

const KAMPAI = (process.env.KAMPAI || '0,20,45,70,90').split(',').map(Number);
const MODELIAI = process.argv.slice(2).length ? process.argv.slice(2)
  : ['woman-placed.stl', 'evil-placed.stl', 'bracket2.stl', 'cup.stl'];
const PLATE = { x: 40.8, y: 30.6, z: 68 };

/** Pasukimas apie X ašį + nuleidimas ant plokštės ir centravimas XY. */
function pasukti(pos, deg) {
  const a = deg * Math.PI / 180, c = Math.cos(a), s = Math.sin(a);
  const out = new Float32Array(pos.length);
  for (let i = 0; i < pos.length; i += 3) {
    const y = pos[i + 1], z = pos[i + 2];
    out[i] = pos[i];
    out[i + 1] = y * c - z * s;
    out[i + 2] = y * s + z * c;
  }
  const b = M.bounds(out);
  const dx = -(b.min[0] + b.max[0]) / 2, dy = -(b.min[1] + b.max[1]) / 2, dz = -b.min[2];
  for (let i = 0; i < out.length; i += 3) {
    out[i] += dx; out[i + 1] += dy; out[i + 2] += dz;
  }
  return out;
}

const EPS = 1e-3, tol = EPS + 0.05, DOWN = [0, 0, -1], UP = [0, 0, 1];

function sargai(t) {
  let air = 0;
  for (const p of t.pillars) {
    if (p.bottom <= 1e-6 || p.partial || p.anchored) continue;
    const hr = t.mesh.rayHit([p.x, p.y, p.bottom + EPS], DOWN);
    if (!(hr.inside || hr.dist <= tol)) air++;
  }
  let anchorAir = 0;
  for (const c of (t.bridges || [])) {
    if (!c.anchor) continue;
    const hr = t.mesh.rayHit([c.b[0], c.b[1], c.b[2] + EPS], DOWN);
    if (!(hr.inside || hr.dist <= tol)) anchorAir++;
  }
  const SKIP = M.CFG.head_penetration_mm + 0.1;
  const inside = q => t.mesh.rayHit(q, UP).inside;
  const walk = (a, b, skipTop) => {
    const L = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
    const n = Math.max(1, Math.ceil(L / 0.25));
    for (let i = 1; i < n; i++) {
      const u = i / n;
      if (L * (1 - u) < skipTop) continue;
      if (inside([a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u,
                  a[2] + (b[2] - a[2]) * u])) return true;
    }
    return false;
  };
  let kerta = 0;
  for (const p of t.pillars) if (walk([p.x, p.y, p.bottom], [p.x, p.y, p.top], SKIP)) kerta++;
  for (const c of t.bridges) if (walk(c.a, c.b, SKIP)) kerta++;
  for (const c of t.links) if (walk(c.a, c.b, 0)) kerta++;
  return { air, anchorAir, kerta };
}

const eilute = (a) => console.log(
  String(a[0]).padEnd(16) + String(a[1]).padStart(6) + String(a[2]).padStart(8) +
  String(a[3]).padStart(8) + String(a[4]).padStart(7) + String(a[5]).padStart(8) +
  String(a[6]).padStart(9) + String(a[7]).padStart(8) + String(a[8]).padStart(7));
eilute(['modelis', 'kampas', 'telpa', 'stulpu', 'kabo', 'ore', 'galv.ore', 'kerta', 'sek']);
let blogu = 0;
for (const f of MODELIAI) {
  const buf = readFileSync(f);
  const { positions } = M.parseSTL(
    buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
  for (const kampas of KAMPAI) {
    const pos = pasukti(positions, kampas);
    const b = M.bounds(pos);
    const telpa = b.size[0] <= PLATE.x && b.size[1] <= PLATE.y && b.size[2] <= PLATE.z;
    const t0 = Date.now();
    let eil;
    try {
      const t = await M.buildSupportTree(pos, M.CFG);
      const s = sargai(t);
      /* NE salos. `t.log.islands` neegzistuoja, o `selfCheck` skaiciuoja
         KABANCIAS atramas (jo rezultata `findOverhangs` tik pavadina „islands").
         Tikros salos matomos tik supjausCius - tai antra pakopa. Ir paduodam
         VISA `t`, ne savo rinkini: `danglingBraces` laukia ax/ay lauku, kuriu
         `t.bridges` neturi, ir krisdavo su „Cannot read properties". */
      const kabo = M.selfCheck(t, t.mesh, M.CFG);
      const bl = s.air + s.anchorAir + s.kerta;
      if (bl) blogu++;
      eil = [f.replace('.stl', ''), kampas + '°', telpa ? 'taip' : 'NE',
        t.pillars.length, kabo, s.air, s.anchorAir, s.kerta,
        ((Date.now() - t0) / 1000).toFixed(1)];
    } catch (e) {
      blogu++;
      eil = [f.replace('.stl', ''), kampas + '°', telpa ? 'taip' : 'NE',
        'KLAIDA', e.message.slice(0, 20), '', '', '', ''];
    }
    eilute(eil);
  }
}
console.log('\nblogu atveju: %d is %d', blogu, MODELIAI.length * KAMPAI.length);
