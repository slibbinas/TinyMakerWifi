/* Antra pasukimų pakopa: pasuktus modelius PJAUSTOM.
 *
 * Pirmoji pakopa (`pasukimai.mjs`) tikrina tik tai, kas pastatyta - ar niekas
 * nekabo ore ir nekerta detalės. Bet ji nieko nesako apie tai, ko NEPASTATYTA:
 * kronšteinas 90° gavo nulį stulpų, puodelis 45° - vieną. Ar ten atramų tikrai
 * nereikia, matyti tik iš pjūvių.
 *
 * Čia kiekvienas (modelis, kampas) supjaustomas į savo zip'ą, o `pasukimai2.py`
 * jį pamatuoja tomis pačiomis taisyklėmis kaip `fizika.py`.
 *
 * Netelpantis modelis SUMAŽINAMAS (kaip darytų `autoOrient`), kad testas liktų
 * tikroviškas - kitaip biustas pasuktas iškart iškristų iš plokštės.
 *
 *   node pasukimai2.mjs            -> pas_<modelis>_<kampas>.zip
 */
import { readFileSync, writeFileSync } from 'fs';
import { deflateSync } from 'zlib';

const M = await import('file:///C:/PIO-build/exp2-wt/web/lib/slicer2.js?t=' + Date.now());

const KAMPAI = (process.env.KAMPAI || '0,20,45,70,90').split(',').map(Number);
const MODELIAI = process.argv.slice(2).length ? process.argv.slice(2)
  : ['woman-placed.stl', 'evil-placed.stl', 'bracket2.stl', 'cup.stl'];
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
    h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4); h.writeUInt16LE(0, 6);
    h.writeUInt16LE(0, 8); h.writeUInt32LE(cr, 14);
    h.writeUInt32LE(data.length, 18); h.writeUInt32LE(data.length, 22);
    h.writeUInt16LE(nb.length, 26);
    loc.push(h, nb, data);
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6);
    c.writeUInt32LE(cr, 16); c.writeUInt32LE(data.length, 20); c.writeUInt32LE(data.length, 24);
    c.writeUInt16LE(nb.length, 28); c.writeUInt32LE(off, 42);
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

function pasukti(pos, deg) {
  const a = deg * Math.PI / 180, c = Math.cos(a), s = Math.sin(a);
  const out = new Float32Array(pos.length);
  for (let i = 0; i < pos.length; i += 3) {
    const y = pos[i + 1], z = pos[i + 2];
    out[i] = pos[i]; out[i + 1] = y * c - z * s; out[i + 2] = y * s + z * c;
  }
  let b = M.bounds(out);
  /* Netelpa - mazinam, kaip darytu autoOrient (fit.scaleToFit). */
  const k = Math.min(1, PLATE.x / b.size[0], PLATE.y / b.size[1], PLATE.z / b.size[2]) * 0.98;
  if (k < 1) for (let i = 0; i < out.length; i++) out[i] *= k;
  b = M.bounds(out);
  const dx = -(b.min[0] + b.max[0]) / 2, dy = -(b.min[1] + b.max[1]) / 2, dz = -b.min[2];
  for (let i = 0; i < out.length; i += 3) { out[i] += dx; out[i + 1] += dy; out[i + 2] += dz; }
  return { pos: out, mastelis: k };
}

for (const f of MODELIAI) {
  const buf = readFileSync(f);
  const { positions } = M.parseSTL(
    buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
  for (const kampas of KAMPAI) {
    const t0 = Date.now();
    const { pos, mastelis } = pasukti(positions, kampas);
    const layers = Math.max(1, Math.ceil(M.bounds(pos).size[2] / M.LAYER_MM));
    const sup = await M.findOverhangs(pos, layers, null);
    const files = [];
    for (let i = 0; i < layers; i++) {
      /* ANTRAS argumentas - aukstis MILIMETRAIS, ne sluoksnio numeris. Padavus
         numeri pjuviai imami z = 0..N mm, t. y. beveik tuscia, ir matavimas
         parode nesamone (biustui 10 salu vietoj 2, turis 853 vietoj 18000).
         Ir modulis TAS PATS visur - slicer2 (buvo isivėles senasis). */
      const m = M.layerMask(pos, (i + 0.5) * M.LAYER_MM, sup);
      files.push([String(i).padStart(5, '0') + '.png', png(M.RES.w, M.RES.h, m)]);
    }
    const vardas = `pas_${f.replace('.stl', '')}_${kampas}.zip`;
    writeFileSync(vardas, zip(files));
    console.log(`${vardas}: ${layers} sluoksniu, stulpu ${sup.pillars.length}` +
      (mastelis < 1 ? `, sumazinta iki ${(mastelis * 100).toFixed(0)}%` : '') +
      `, ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  }
}
