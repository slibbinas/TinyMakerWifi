/* Kur dingsta laikas: slicerio etapu lentele.
 *
 * `buildSupportTree` pats matuoja etapus (`log.ms`), tik niekas ju nerodo.
 * Cia isspausdinam - kad pauze („atodusis") butu dedama TEN, kur laikas, o ne
 * ten, kur atrodo.
 *
 *   node etapai.mjs <model.stl> [raw]
 */
import { readFileSync } from 'fs';

const MOD = process.env.SLICER || 'C:/PIO-build/exp2-wt/web/lib/slicer2.js';
const M = await import('file:///' + MOD.split('\\').join('/') + '?t=' + Date.now());

const buf = readFileSync(process.argv[2]);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
let pos = positions;
if (process.argv[3] !== 'raw') {
  const best = M.autoOrient(positions);
  if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
  pos = M.place(positions, best.tr);
}

const layers = Math.max(1, Math.ceil(M.bounds(pos).size[2] / M.LAYER_MM));
const t0 = performance.now();
const sup = await M.findOverhangs(pos, layers, null);
const viso = performance.now() - t0;

const ms = (sup.log && sup.log.ms) || (M.CFG && M.CFG.__ms) || null;
console.log(`${process.argv[2]}: is viso ${viso.toFixed(0)} ms`);
if (ms) {
  const eil = Object.entries(ms).sort((a, b) => b[1] - a[1]);
  for (const [k, v] of eil)
    console.log(`  ${k.padEnd(14)} ${String(v).padStart(7)} ms  ${(v / viso * 100).toFixed(0)}%`);
} else {
  console.log('  (etapu lentele nepasiekiama per sup.log)');
  console.log('  raktai:', Object.keys(sup).join(', '));
}
