/* Kiek laiko uzima kiekvienas etapas.
 *
 * Sliceris sukasi NARSYKLEJE, tad greitis cia ne kosmetika: vartotojas laukia
 * prie ikelto STL. Node ir narsykle skiriasi, bet tas pats kodas ir tas pats
 * modelis leidzia bent matyti, kuris etapas brangus ir ar jis nepablogejo.
 *
 *   SLICER=…/slicer2.js node timing.mjs <placed.stl>
 */
import { readFileSync } from 'fs';

const MOD = process.env.SLICER || 'C:/PIO-build/exp2-wt/web/lib/slicer.js';
const M = await import('file:///' + MOD.split('\\').join('/') + '?t=' + Date.now());

const buf = readFileSync(process.argv[2]);
const t0 = Date.now();
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const best = M.autoOrient(positions);
if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
const placed = M.place(positions, best.tr);
const t1 = Date.now();

const b = M.bounds(placed);
const layers = Math.max(1, Math.ceil(b.size[2] / M.LAYER_MM));
const sup = await M.findOverhangs(placed, layers, null);
const t2 = Date.now();

const name = process.argv[2].split(/[\\/]/).pop();
console.log('%s · %d trikampiu · %d sluoksniu · modulis %s',
  name, positions.length / 9, layers, MOD.split('/').pop());
console.log('  STL + orientavimas: %s s', ((t1 - t0) / 1000).toFixed(1));
console.log('  supportai:          %s s   (stulpu %d, tiltu %s)',
  ((t2 - t1) / 1000).toFixed(1), sup.pillars.length, sup.bridges);
if (sup.log && sup.log.ms) console.log('  etapais (ms):', JSON.stringify(sup.log.ms));
