/* Kiek slicerio ciklas BLOKUOJA giją.
 *
 * Naršyklė lieka gyva tik tarp `await` taškų. Tad matuojam ne bendrą laiką, o
 * ILGIAUSIĄ vientisą gabalą: paleidžiam laikmatį, kuris turėtų tiksėti kas 4 ms,
 * ir žiūrim, kokia didžiausia pauzė tarp jo tiksėjimų. Ta pauzė ir yra tai,
 * kiek naršyklė būtų „pakibusi".
 *
 *   node blokavimas.mjs <model.stl> [raw]
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

/* Tiksintis laikmatis - musu „naršyklės gyvybės" matuoklis. */
let prev = performance.now(), worst = 0, tikai = 0;
const t = setInterval(() => {
  const now = performance.now();
  worst = Math.max(worst, now - prev);
  prev = now;
  tikai++;
}, 4);

const layers = Math.max(1, Math.ceil(M.bounds(pos).size[2] / M.LAYER_MM));
const t0 = performance.now();
const sup = await M.findOverhangs(pos, layers, null);
const tSup = performance.now() - t0;

/* Pjaustymas: kiekvienas sluoksnis atskirai - matuojam ir viena sluoksni. */
const t1 = performance.now();
let vienas = 0;
for (let i = 0; i < Math.min(layers, 60); i++) {
  const a = performance.now();
  await M.layerMask(pos, i, sup);
  vienas = Math.max(vienas, performance.now() - a);
}
const tSl = performance.now() - t1;

clearInterval(t);
console.log(`${process.argv[2]}
  atramos                     ${tSup.toFixed(0)} ms
  60 sluoksniu                ${tSl.toFixed(0)} ms (vienas iki ${vienas.toFixed(1)} ms)
  ILGIAUSIA PAUZE be atodusio ${worst.toFixed(0)} ms
  laikmatis (4 ms) tikseio    ${tikai} k. - jei 0, gija buvo uzimta VISA laika`);
