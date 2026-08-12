/* Išmatuoja supportus TIESIAI iš sluoksnių: kiek jų, kokio skersmens ir kaip
   toli nuo detalės. Veikia vienodai su PrusaSlicer SL1 ir su mūsų ZIP, nes
   modelio kaukė abiem imama iš to paties STL, be supportų.

   node measure.mjs <sluoksniai.zip|sl1> <model.stl> [aukščiai mm, per kablelį]  */
import { readFileSync, writeFileSync } from 'fs';
import { inflateSync, inflateRawSync } from 'zlib';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const here = dirname(fileURLToPath(import.meta.url));
const src = readFileSync('C:/PIO-build/exp2-wt/web/lib/slicer.js', 'utf8');
const tmp = join(here, 'slicer_meas.mjs');
writeFileSync(tmp, src);
const M = await import('file:///' + tmp.replace(/\\/g, '/') + '?t=' + Date.now());

/* --- ZIP + PNG (tas pats kodas kaip isostack.mjs) --- */
function zipEntries(buf) {
  let e = buf.length - 22;
  while (e >= 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  const count = buf.readUInt16LE(e + 10);
  let o = buf.readUInt32LE(e + 16);
  const out = [];
  for (let i = 0; i < count; i++) {
    const method = buf.readUInt16LE(o + 10), csize = buf.readUInt32LE(o + 20);
    const nameLen = buf.readUInt16LE(o + 28), extraLen = buf.readUInt16LE(o + 30);
    const cmtLen = buf.readUInt16LE(o + 32), local = buf.readUInt32LE(o + 42);
    const name = buf.toString('utf8', o + 46, o + 46 + nameLen);
    const start = local + 30 + buf.readUInt16LE(local + 26) + buf.readUInt16LE(local + 28);
    const raw = buf.subarray(start, start + csize);
    out.push({ name, data: () => method ? inflateRawSync(raw) : raw });
    o += 46 + nameLen + extraLen + cmtLen;
  }
  return out;
}
function readPNG(buf) {
  let o = 8, w = 0, h = 0, colour = 0; const idat = []; let palette = null;
  while (o < buf.length) {
    const len = buf.readUInt32BE(o), type = buf.toString('ascii', o + 4, o + 8);
    const data = buf.subarray(o + 8, o + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); colour = data[9]; }
    else if (type === 'PLTE') palette = data;
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    o += 12 + len;
  }
  const ch = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colour];
  const raw = inflateSync(Buffer.concat(idat));
  const stride = w * ch, px = Buffer.alloc(stride * h);
  let p = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[p++], row = raw.subarray(p, p + stride); p += stride;
    const cur = px.subarray(y * stride, (y + 1) * stride);
    const prev = y ? px.subarray((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= ch ? cur[x - ch] : 0, b = prev ? prev[x] : 0;
      const c = prev && x >= ch ? prev[x - ch] : 0;
      let v = row[x];
      if (f === 1) v += a; else if (f === 2) v += b;
      else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      cur[x] = v & 255;
    }
  }
  const grey = Buffer.alloc(w * h);
  for (let i = 0; i < w * h; i++)
    grey[i] = colour === 3 ? (palette ? palette[px[i] * 3] : px[i]) : px[i * ch];
  return { w, h, grey };
}

/* --- modelio kaukė iš STL, be supportų --- */
const buf = readFileSync(process.argv[3]);
const { positions } = M.parseSTL(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
const best = M.autoOrient(positions);
if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
const placed = M.place(positions, best.tr);

const zip = zipEntries(readFileSync(process.argv[2]));
const pngs = zip.filter(e => /\.png$/i.test(e.name) && !/thumbnail/i.test(e.name))
                .sort((a, b) => a.name.localeCompare(b.name));
const W = M.RES.w, H = M.RES.h, N = W * H;
const heights = (process.argv[4] || '3,6,10,15,20,25').split(',').map(Number);

/* display_mirror_x = 1: PrusaSlicer sluoksnius apverčia, mūsų — ne. Prieš
   lyginant pasitikrinam, kuris variantas sutampa su modelio kauke. Be šito
   dalis detalės palaikoma supportais ir visi skaičiai meluoja (08-13). */
let mirror = false;
{
  const i = Math.floor(pngs.length * 0.35);
  const g = readPNG(pngs[i].data()).grey;
  const mm = M.layerMask(placed, (i + 0.5) * M.LAYER_MM, null);
  let same = 0, flip = 0;
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      if (g[y * W + x] <= 127) continue;
      if (mm[y * W + x] > 127) same++;
      if (mm[y * W + (W - 1 - x)] > 127) flip++;
    }
  mirror = flip > same;
  console.log('sutapimas: tiesiai %d, veidrodinis %d -> %s', same, flip,
    mirror ? 'VERČIAM' : 'kaip yra');
}
/* SLUOKSNIO POSLINKIS. PrusaSlicer pirmą sluoksnį daro storesnį
   (initial_layer_height = 0.3), tad jo sluoksnis nr. k NĖRA mūsų nr. k: visa
   krūva pastumta 6 sluoksniais. Be šito modelio kaukė imama 0,3 mm per žemai,
   nesutapimo kraštas skaičiuojamas kaip supportai, ir etalono skaičiai išeina
   DVIGUBAI didesni (Prusa ties z=20: 48 vietoj 24 — išmatuota 08-13).
   Poslinkis randamas automatiškai, tad tinka bet kokiam failui. */
let shift = 0;
{
  const probe = [0.35, 0.5, 0.65].map(f => Math.floor(pngs.length * f));
  let bestBad = Infinity;
  for (let sh = -12; sh <= 12; sh++) {
    let bad = 0, ok = true;
    for (const i of probe) {
      const k = i + sh;
      if (k < 0 || k >= pngs.length) { ok = false; break; }
      const g0 = readPNG(pngs[k].data()).grey;
      const mm = M.layerMask(placed, (i + 0.5) * M.LAYER_MM, null);
      for (let y = 0; y < H; y++)
        for (let x = 0; x < W; x++) {
          const xm = mirror ? W - 1 - x : x;
          if ((mm[y * W + x] > 127) !== (g0[y * W + xm] > 127)) bad++;
        }
    }
    if (ok && bad < bestBad) { bestBad = bad; shift = sh; }
  }
  console.log('sluoksniu poslinkis: %d (%s mm)', shift, (shift * M.LAYER_MM).toFixed(2));
}

const unmirror = g => {
  if (!mirror) return g;
  const out = Buffer.alloc(g.length);
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) out[y * W + x] = g[y * W + (W - 1 - x)];
  return out;
};

console.log('sluoksniu faile:', pngs.length);
console.log('z mm |  n  |  Ø mm: p10 p50 p90  | tarpas iki detalės: p10 p50 p90');

for (const zmm of heights) {
  const i = Math.round(zmm / M.LAYER_MM - 0.5);
  if (i + shift < 0 || i + shift >= pngs.length) continue;
  const layer = unmirror(readPNG(pngs[i + shift].data()).grey);
  const model = M.layerMask(placed, (i + 0.5) * M.LAYER_MM, null);

  /* Atstumas nuo modelio: chamfer 3-4, tie patys svoriai kaip slicer'yje. */
  const BIG = 1 << 28, dist = new Int32Array(N);
  for (let p = 0; p < N; p++) dist[p] = model[p] > 127 ? 0 : BIG;
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const p = y * W + x; let d = dist[p]; if (!d) continue;
    if (x > 0 && dist[p-1] + 3 < d) d = dist[p-1] + 3;
    if (y > 0) {
      if (dist[p-W] + 3 < d) d = dist[p-W] + 3;
      if (x > 0 && dist[p-W-1] + 4 < d) d = dist[p-W-1] + 4;
      if (x < W-1 && dist[p-W+1] + 4 < d) d = dist[p-W+1] + 4;
    }
    dist[p] = d;
  }
  for (let y = H-1; y >= 0; y--) for (let x = W-1; x >= 0; x--) {
    const p = y * W + x; let d = dist[p]; if (!d) continue;
    if (x < W-1 && dist[p+1] + 3 < d) d = dist[p+1] + 3;
    if (y < H-1) {
      if (dist[p+W] + 3 < d) d = dist[p+W] + 3;
      if (x < W-1 && dist[p+W+1] + 4 < d) d = dist[p+W+1] + 4;
      if (x > 0 && dist[p+W-1] + 4 < d) d = dist[p+W-1] + 4;
    }
    dist[p] = d;
  }

  /* Supportai = kas yra sluoksnyje, bet ne modelyje (su 1 px atlaida). */
  const sup = new Uint8Array(N);
  for (let p = 0; p < N; p++) if (layer[p] > 127 && dist[p] > 3) sup[p] = 1;

  const seen = new Uint8Array(N), stack = new Int32Array(N);
  const blobs = [];
  for (let s = 0; s < N; s++) {
    if (!sup[s] || seen[s]) continue;
    let top = 0, area = 0, minD = Infinity, maxD = 0;
    stack[top++] = s; seen[s] = 1;
    while (top) {
      const p = stack[--top]; area++;
      const d = dist[p] / 3 * M.PIXEL_MM;
      if (d < minD) minD = d;
      if (d > maxD) maxD = d;
      const x = p % W, y = (p / W) | 0;
      for (let dy = -1; dy <= 1; dy++) { const yy = y + dy; if (yy < 0 || yy >= H) continue;
        for (let dx = -1; dx <= 1; dx++) { const xx = x + dx; if (xx < 0 || xx >= W) continue;
          const q = yy * W + xx; if (sup[q] && !seen[q]) { seen[q] = 1; stack[top++] = q; } } }
    }
    if (area >= 2) blobs.push({ area, minD, maxD });
  }
  const px2 = M.PIXEL_MM * M.PIXEL_MM;
  const dia = blobs.map(b => 2 * Math.sqrt(b.area * px2 / Math.PI)).sort((a, b) => a - b);
  const gaps = blobs.map(b => b.minD).sort((a, b) => a - b);
  const q = (a, f) => a.length ? a[Math.min(a.length - 1, Math.floor(a.length * f))] : 0;
  console.log('%s | %s |  %s %s %s | %s %s %s',
    String(zmm).padStart(4), String(blobs.length).padStart(3),
    q(dia, 0.1).toFixed(2), q(dia, 0.5).toFixed(2).padStart(6), q(dia, 0.9).toFixed(2).padStart(6),
    q(gaps, 0.1).toFixed(2).padStart(6), q(gaps, 0.5).toFixed(2).padStart(6),
    q(gaps, 0.9).toFixed(2).padStart(6));
}
