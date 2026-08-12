/* Nepriklausoma geometrijos patikra ant TIKRO modelio, ne ant dėžučių.
   Netiki `selfCheck` — matuoja pats, iš tos pačios spindulių mašinos:
     1. ar kiekvieno stulpo apačia remiasi (plokštė / medžiaga),
     2. ar stulpai, tiltai ir jungtys nekerta detalės.
   node verify.mjs <placed.stl> [modulis.js]  */
import { readFileSync } from 'fs';

const MOD = process.argv[3] || 'C:/PIO-build/exp2-wt/web/lib/slicer2.js';
const M = await import('file:///' + MOD.replace(/\\/g, '/') + '?t=' + Date.now());
const buf = readFileSync(process.argv[2]);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const best = M.autoOrient(positions);
if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
const placed = M.place(positions, best.tr);
const t = await M.buildSupportTree(placed, M.CFG);
console.log('modulis %s v%s · stulpu %d · tiltu %d · jungciu %d',
  MOD.split('/').pop(), M.VERSION, t.pillars.length, t.bridges.length, t.links.length);

const UP = [0, 0, 1], DOWN = [0, 0, -1];
const EPS = 1e-3, tol = EPS + 0.05;

/* 1 · atrama po apačia */
let air = 0; const airList = [];
for (const p of t.pillars) {
  if (p.bottom <= 1e-6 || p.partial) continue;
  const hr = t.mesh.rayHit([p.x, p.y, p.bottom + EPS], DOWN);
  if (hr.inside || hr.dist <= tol) continue;
  air++; if (airList.length < 5)
    airList.push({ x: +p.x.toFixed(2), y: +p.y.toFixed(2),
                   bottom: +p.bottom.toFixed(2), tarpas: +hr.dist.toFixed(2) });
}

/* 2 · ar taškas medžiagos viduje: uždaram tinklui pirmas paviršius į viršų
       kertamas iš VIDAUS (triHit ženklas a < 0) tada ir tik tada. */
const insideBody = q => t.mesh.rayHit(q, UP).inside;
const walk = (a, b, skipTop = 0) => {   // skipTop mm nuo viršutinio galo praleidžiam
  const L = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
  const n = Math.max(1, Math.ceil(L / 0.25));
  let hits = 0;
  for (let i = 1; i < n; i++) {
    const u = i / n;
    if (L * (1 - u) < skipTop) continue;
    if (insideBody([a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u,
                    a[2] + (b[2] - a[2]) * u])) hits++;
  }
  return hits;
};

/* Galvutė kabinasi į detalę tyčia (head_penetration 0.3), tad viršutinį
   galą praleidžiam — tikrinam tik tai, kas eiti kiaurai NETURI. */
const SKIP = M.CFG.head_penetration_mm + 0.1;
let inPillar = 0, inBridge = 0, inLink = 0;
for (const p of t.pillars)
  if (walk([p.x, p.y, p.bottom], [p.x, p.y, p.top], SKIP)) inPillar++;
for (const c of t.bridges) if (walk(c.a, c.b, SKIP)) inBridge++;
for (const c of t.links)   if (walk(c.a, c.b, 0)) inLink++;

console.log('  apacia ore: %d / %d  %s', air,
  t.pillars.filter(p => p.bottom > 1e-6 && !p.partial).length, JSON.stringify(airList));
console.log('  kerta detale: stulpu %d/%d · tiltu %d/%d · jungciu %d/%d',
  inPillar, t.pillars.length, inBridge, t.bridges.length, inLink, t.links.length);
console.log('  selfCheck() sako:', M.selfCheck(t, t.mesh, M.CFG));
