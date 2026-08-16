/* „Vos telpa": tas pats modelis, uzimantis vis daugiau plokstes.
 *
 * V pastaba (2026-08-19): kai detale padeta beveik per visa ploksSte, soninės
 * atramos nebetelpa ir nupjaunamos - o jei detale dar ir nesimetriskai, viena
 * puse atrodo kitaip nei kita. Todel testuojam CENTRUOTA modeli, keliais
 * uzimtumo laipsniais.
 *
 * `uzimtumas` - kiek plokstes ploCio (ar gylio, kas ankSCiau) uzima modelis:
 * 1,00 = iki pat krastu, 0,90 = su 10 % atsarga.
 *
 *   node telpa.mjs <model.stl> [1.0,0.95,0.9]
 */
import { readFileSync, writeFileSync } from 'fs';
import { deflateSync } from 'zlib';

const M = await import('file:///C:/PIO-build/exp2-wt/web/lib/slicer2.js?t=' + Date.now());
if (M.setFitMargin) M.setFitMargin(0);          // mastelį valdom patys

const STL = process.argv[2] || 'cup.stl';
const DALYS = (process.argv[3] || '1.0,0.95,0.9').split(',').map(Number);
const PLATE = { x: 40.8, y: 30.6, z: 68 };

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
  ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 0;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ih), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

function zip(files) {
  const loc = [], cen = []; let off = 0;
  const T = new Int32Array(256);
  for (let n = 0; n < 256; n++) { let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c; }
  const crc = b => { let c = ~0; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return ~c >>> 0; };
  for (const [name, data] of files) {
    const nb = Buffer.from(name), cr = crc(data);
    const h = Buffer.alloc(30);
    h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4);
    h.writeUInt32LE(cr, 14); h.writeUInt32LE(data.length, 18);
    h.writeUInt32LE(data.length, 22); h.writeUInt16LE(nb.length, 26);
    loc.push(h, nb, data);
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6);
    c.writeUInt32LE(cr, 16); c.writeUInt32LE(data.length, 20);
    c.writeUInt32LE(data.length, 24); c.writeUInt16LE(nb.length, 28);
    c.writeUInt32LE(off, 42);
    cen.push(c, nb);
    off += 30 + nb.length + data.length;
  }
  const cb = Buffer.concat(cen);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(files.length, 8);
  end.writeUInt16LE(files.length, 10); end.writeUInt32LE(cb.length, 12);
  end.writeUInt32LE(off, 16);
  return Buffer.concat([...loc, cb, end]);
}

const buf = readFileSync(STL);
const { positions } = M.parseSTL(
  buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));

for (const dalis of DALYS) {
  const b0 = M.bounds(positions);
  /* Mastelis taip, kad plaCiausia kryptis uzimtu butent `dalis` plokstes. */
  const k = dalis * Math.min(PLATE.x / b0.size[0], PLATE.y / b0.size[1],
                             PLATE.z / b0.size[2]);
  const pos = new Float32Array(positions.length);
  for (let i = 0; i < positions.length; i++) pos[i] = positions[i] * k;
  const b = M.bounds(pos);
  const dx = -(b.min[0] + b.max[0]) / 2, dy = -(b.min[1] + b.max[1]) / 2, dz = -b.min[2];
  for (let i = 0; i < pos.length; i += 3) { pos[i] += dx; pos[i + 1] += dy; pos[i + 2] += dz; }
  const bb = M.bounds(pos);
  const layers = Math.max(1, Math.ceil(bb.size[2] / M.LAYER_MM));
  const sup = await M.findOverhangs(pos, layers, null);
  const files = [];
  for (let i = 0; i < layers; i++)
    files.push([String(i).padStart(5, '0') + '.png',
                png(M.RES.w, M.RES.h, M.layerMask(pos, (i + 0.5) * M.LAYER_MM, sup))]);
  const vardas = `telpa_${STL.replace('.stl', '')}_${Math.round(dalis * 100)}.zip`;
  writeFileSync(vardas, zip(files));
  console.log(`${vardas}: ${bb.size.map(v => v.toFixed(1)).join(' x ')} mm, ` +
    `stulpu ${sup.pillars.length}, sluoksniu ${layers}`);
}
