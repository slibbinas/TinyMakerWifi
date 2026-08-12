/* Išsaugo TAI, ką slicina mūsų pipeline (po autoOrient + place), kaip binary
   STL. Tada PrusaSlicer gauna identišką geometriją ir identišką padėtį —
   palyginimas tampa apie algoritmą, ne apie orientavimą. */
import { readFileSync, writeFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const here = dirname(fileURLToPath(import.meta.url));
const src = readFileSync('C:/PIO-build/exp2-wt/web/lib/slicer.js', 'utf8');
const tmp = join(here, 'slicer_placed.mjs');
writeFileSync(tmp, src);
const M = await import('file:///' + tmp.replace(/\\/g, '/') + '?t=' + Date.now());

const buf = readFileSync(process.argv[2]);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const best = M.autoOrient(positions);
if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
const placed = M.place(positions, best.tr);
const b = M.bounds(placed);
console.log('dydis %s x %s x %s mm', ...b.size.map(v => v.toFixed(2)));
console.log('x %s..%s  y %s..%s  z %s..%s',
  b.min[0].toFixed(2), b.max[0].toFixed(2),
  b.min[1].toFixed(2), b.max[1].toFixed(2),
  b.min[2].toFixed(2), b.max[2].toFixed(2));

/* PrusaSlicer plokštė yra 0..40.8 / 0..30.6, mūsų — centruota apie nulį.
   Perstumiam, kad jis matytų tą patį daiktą toje pačioje vietoje. */
const n = placed.length / 9;
const out = Buffer.alloc(84 + n * 50);
out.writeUInt32LE(n, 80);
let o = 84;
for (let i = 0; i < placed.length; i += 9) {
  o += 12;                                        // normalę jis perskaičiuoja
  for (let v = 0; v < 9; v += 3) {
    out.writeFloatLE(placed[i + v] + M.PLATE.x / 2, o);
    out.writeFloatLE(placed[i + v + 1] + M.PLATE.y / 2, o + 4);
    out.writeFloatLE(placed[i + v + 2], o + 8);
    o += 12;
  }
  o += 2;
}
writeFileSync(process.argv[3], out);
console.log('irasyta:', process.argv[3], n, 'trikampiu');
