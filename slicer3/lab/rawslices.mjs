/* Kaip `ourslices.mjs`, tik BE `autoOrient` — modelis pjaustomas toks, koks
   yra faile. Reikia tada, kai norim isbandyti PATI supportu algoritma
   pasirinktoje padetyje, o ne orientavima (jis, pvz., apverte kronsteina taip,
   kad nuokabu nebeliko, ir testas nieko nebetikrino).

   SLICER=…/slicer2.js node rawslices.mjs <model.stl> <out.zip>  */
import { readFileSync, writeFileSync } from 'fs';
import { deflateSync } from 'zlib';

const MOD = process.env.SLICER || 'C:/PIO-build/exp2-wt/web/lib/slicer.js';
const M = await import('file:///' + MOD.split('\\').join('/') + '?t=' + Date.now());

function png(w, h, grey) {
  const raw = Buffer.alloc((w + 1) * h);
  for (let y = 0; y < h; y++) {
    raw[y * (w + 1)] = 0;
    Buffer.from(grey.buffer, grey.byteOffset + y * w, w).copy(raw, y * (w + 1) + 1);
  }
  const T = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    T[n] = c;
  }
  const crc = b => { let c = ~0; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc(body));
    return Buffer.concat([len, body, c]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

const buf = readFileSync(process.argv[2]);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const b = M.bounds(positions);
const layers = Math.max(1, Math.ceil(b.size[2] / M.LAYER_MM));
const sup = await M.findOverhangs(positions, layers, null);
console.log('stulpeliu %d (bokstu %s, tiltu %s) · salu %d',
  sup.pillars.length, sup.towers, sup.bridges, sup.islands);

const files = [];
for (let i = 0; i < layers; i++)
  files.push({ name: String(i + 1).padStart(5, '0') + '.png',
               data: new Uint8Array(png(M.RES.w, M.RES.h,
                 M.layerMask(positions, (i + 0.5) * M.LAYER_MM, sup))) });
const zip = M.zipStore(files);
writeFileSync(process.argv[3],
  Buffer.from(zip instanceof Blob ? await zip.arrayBuffer() : zip));
console.log('irasyta:', process.argv[3], layers, 'sluoksniu');
