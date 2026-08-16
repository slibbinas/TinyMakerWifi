/* Mūsų sluoksniai į tokį patį ZIP su PNG, kokį duoda PrusaSlicer — kad tas
   pats izometrinis renderis piešė abi puses ir palyginimas būtų apie algoritmą.
   Naudojimas: node ourslices.mjs <model.stl> <out.zip> */
import { readFileSync, writeFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { deflateSync } from 'zlib';

/* Modulis imamas TIESIAI iš repo, ne per kopiją scratchpad'e: slicer2.js
   importuoja kaimyninį slicer.js, o kopija to kaimyno neturėtų.
   Kitą modulį nurodyk per SLICER=…\slicer2.js */
const here = dirname(fileURLToPath(import.meta.url));
/* NUMATYTASIS - slicer2.js. Buvo `slicer.js` (senasis, v0.9.0), ir tai spastas:
   butent Sis skriptas gamina zip'us, kuriuos matuoja `fizika.py`, tad dalis
   matavimu galejo buti apie NE TA algoritma. Biustui skirtumas milzinis:
   senasis 54 stulpai / krasta liecia 856 sluoksniai, slicer2 - 31 / 23.
   Sena moduli dabar reikia nurodyti aiskiai: SLICER=...\slicer.js */
const MOD = process.env.SLICER || 'C:/PIO-build/exp2-wt/web/lib/slicer2.js';
const M = await import('file:///' + MOD.replace(/\\/g, '/') + '?t=' + Date.now());
console.log('modulis:', MOD, 'v' + M.VERSION);

function png(w, h, grey) {
  const raw = Buffer.alloc((w + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w + 1)] = 0;
    Buffer.from(grey.buffer, grey.byteOffset + y * w, w).copy(raw, y * (w + 1) + 1);
  }
  const T = new Int32Array(256);
  for (let n = 0; n < 256; n++) { let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c; }
  const crc = b => { let c = ~0; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };
  const chunk = (t, d) => {
    const l = Buffer.alloc(4); l.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t), d]);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
    return Buffer.concat([l, td, c]);
  };
  const ih = Buffer.alloc(13);
  ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 0;   // greyscale
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),
    chunk('IHDR', ih), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

const buf = readFileSync(process.argv[2]);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
/* RAW=1 - modelio nesukam. Kronsteinas ir puodelis matavimuose imami butent
   taip (`oriented=False` fizika.py/krastas2.py lentelese): autoOrient juos
   apverstu taip, kad nuokabu nebeliktu, ir palyginimas butu ne apie ta pati. */
let placed;
if (process.env.RAW) {
  placed = positions;
} else {
  const best = M.autoOrient(positions);
  if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
  placed = M.place(positions, best.tr);
}
const b = M.bounds(placed);
const layers = Math.max(1, Math.ceil(b.size[2] / M.LAYER_MM));
const sup = await M.findOverhangs(placed, layers, null);
console.log('stulpeliu %d (bokstu %s, tiltu %s, ant modelio %d) · salu %d',
  sup.pillars.length, sup.towers, sup.bridges, sup.onModel, sup.islands);

const files = [];
for (let i = 0; i < layers; i++) {
  const mask = M.layerMask(placed, (i + 0.5) * M.LAYER_MM, sup);
  files.push({ name: String(i + 1).padStart(5, '0') + '.png',
               data: new Uint8Array(png(M.RES.w, M.RES.h, mask)) });
}
const zip = M.zipStore(files);            // naršyklėje tai Blob, čia — baitai
writeFileSync(process.argv[3],
  Buffer.from(zip instanceof Blob ? await zip.arrayBuffer() : zip));
console.log('irasyta:', process.argv[3], layers, 'sluoksniu');
