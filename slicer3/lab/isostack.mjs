/* Izometrinis renderis TIESIAI iš sluoksnių krūvos (SL1 / ZIP su PNG).
   Abi puses — PrusaSlicer ir mūsų — piešia tas pats kodas, tad skirtumas
   vaizde yra skirtumas algoritme, o ne piešime.

   Naudojimas: node isostack.mjs <sl1> <out.png> [kampas]
   Jokių priklausomybių: ZIP ir PNG skaitomi čia pat, per zlib. */
import { readFileSync, writeFileSync } from 'fs';
import { inflateSync, inflateRawSync, deflateSync } from 'zlib';

/* ------------------------------------------------------------------- ZIP */
function zipEntries(buf) {
  // Central directory end: signature 0x06054b50, ieškom nuo galo.
  let e = buf.length - 22;
  while (e >= 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  if (e < 0) throw new Error('ne ZIP');
  const count = buf.readUInt16LE(e + 10);
  let o = buf.readUInt32LE(e + 16);
  const out = [];
  for (let i = 0; i < count; i++) {
    const method = buf.readUInt16LE(o + 10);
    const csize = buf.readUInt32LE(o + 20);
    const nameLen = buf.readUInt16LE(o + 28);
    const extraLen = buf.readUInt16LE(o + 30);
    const cmtLen = buf.readUInt16LE(o + 32);
    const local = buf.readUInt32LE(o + 42);
    const name = buf.toString('utf8', o + 46, o + 46 + nameLen);
    const lNameLen = buf.readUInt16LE(local + 26);
    const lExtraLen = buf.readUInt16LE(local + 28);
    const start = local + 30 + lNameLen + lExtraLen;
    const raw = buf.subarray(start, start + csize);
    // ZIP saugo GRYNĄ deflate srautą, be zlib antraštės — todėl Raw.
    out.push({ name, data: () => method ? inflateRawSync(raw) : raw });
    o += 46 + nameLen + extraLen + cmtLen;
  }
  return out;
}

/* ------------------------------------------------------------------- PNG */
function readPNG(buf) {
  let o = 8, w = 0, h = 0, depth = 8, colour = 0;
  const idat = [];
  let palette = null;
  while (o < buf.length) {
    const len = buf.readUInt32BE(o);
    const type = buf.toString('ascii', o + 4, o + 8);
    const data = buf.subarray(o + 8, o + 8 + len);
    if (type === 'IHDR') {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      depth = data[8]; colour = data[9];
      if (data[12]) throw new Error('interlace nepalaikomas');
    } else if (type === 'PLTE') palette = data;
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    o += 12 + len;
  }
  const ch = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[colour];
  if (depth !== 8) throw new Error('bitų gylis ' + depth + ' nepalaikomas');
  const raw = inflateSync(Buffer.concat(idat));
  const stride = w * ch;
  const px = Buffer.alloc(stride * h);
  let p = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[p++];
    const row = raw.subarray(p, p + stride); p += stride;
    const cur = px.subarray(y * stride, (y + 1) * stride);
    const prev = y ? px.subarray((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; x++) {
      const a = x >= ch ? cur[x - ch] : 0;
      const b = prev ? prev[x] : 0;
      const c = prev && x >= ch ? prev[x - ch] : 0;
      let v = row[x];
      if (f === 1) v += a;
      else if (f === 2) v += b;
      else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      cur[x] = v & 255;
    }
  }
  // Į vieną baitą per pikselį: mums rūpi tik „yra medžiaga ar ne".
  const grey = Buffer.alloc(w * h);
  for (let i = 0; i < w * h; i++) {
    if (colour === 3) { const q = px[i] * 3; grey[i] = palette ? palette[q] : px[i]; }
    else grey[i] = px[i * ch];
  }
  return { w, h, grey };
}

function writePNG(w, h, rgb) {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++)
    rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
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
  ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),
    chunk('IHDR', ih), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

/* ---------------------------------------------------------------- render */
const LAYER = 0.05, PIX = 40.8 / 320, SIZE = 900;
const YAW = (process.argv[4] ? +process.argv[4] : 35) * Math.PI / 180;
const PITCH = 22 * Math.PI / 180;

/* Modelio kaukė iš to paties STL — iš jos matyti, kas sluoksnyje yra detalė, o
   kas supportas. Be šito abu piešiami viena spalva ir narvas dingsta detalės
   fone (V 08-13: „supportų spalva ir vaizdas gaunamas plokščias"). */
let modelMask = null;
if (process.argv[5]) {
  const { fileURLToPath } = await import('url');
  const { dirname, join } = await import('path');
  const here = dirname(fileURLToPath(import.meta.url));
  const jsSrc = readFileSync('C:/PIO-build/exp2-wt/web/lib/slicer.js', 'utf8');
  const tmp = join(here, 'slicer_iso2.mjs');
  writeFileSync(tmp, jsSrc);
  const M = await import('file:///' + tmp.replace(/\\/g, '/') + '?t=' + Date.now());
  const b = readFileSync(process.argv[5]);
  const { positions } = M.parseSTL(b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength));
  /* RAW=1 — kaukė be `autoOrient`, kai sluoksniai pjaustyti `rawslices.mjs`
     (kronšteinas, puodelis). Su neatitinkančia kaukė piešinys meluoja: pėdos
     nusidažo detalės spalva ir atrodo, tarsi slicer'is būtų pridirbęs
     (V pastaba 08-15). */
  let placed = positions;
  if (!process.env.RAW) {
    const best = M.autoOrient(positions);
    if (!best.fit.fits) best.tr.scale = best.fit.scaleToFit;
    placed = M.place(positions, best.tr);
  }
  modelMask = i => M.layerMask(placed, (i + 0.5) * M.LAYER_MM, null);
}

const zip = zipEntries(readFileSync(process.argv[2]));
const names = zip.filter(e => /\.png$/i.test(e.name) && !/thumbnail/i.test(e.name))
                 .sort((a, b) => a.name.localeCompare(b.name));
console.log(names.length, 'sluoksniu');
const stack = names.map(e => readPNG(e.data()));
const { w: W, h: H } = stack[0];
const n = stack.length;

/* Printerio profilyje display_mirror_x = 1 — PrusaSlicer sluoksnius apverčia,
   nes taip juos matys ekranas. Mūsų kaukė neapversta, tad prieš lyginant
   pasitikrinam, kuris variantas sutampa geriau, ir pasukam. Be šito pusė
   detalės nusidažo supportų spalva (08-13). */
let mirror = false;
/* MIRROR=1|0 — priverstinis nustatymas. Atspėjimas simetriškam daiktui
   (kronšteinas!) yra monetos metimas: sienelės sutampa abiem atvejais, o
   asimetriškos pėdos tada nukrenta ant apverstos kaukės ir nusidažo detalės
   spalva — atrodo, tarsi slicer'is būtų pridirbęs (V pastaba 08-15). */
if (process.env.MIRROR !== undefined) {
  mirror = process.env.MIRROR === '1';
} else if (modelMask) {
  /* Vienas sluoksnis simetriškam daiktui (kaukolė!) duoda beveik lygiąsias —
     13012 prieš 13566. Imam kelis aukščius, kad asimetrija susidėtų. */
  let same = 0, flip = 0;
  for (const f of [0.2, 0.35, 0.5, 0.65, 0.8]) {
    const probe = Math.floor(n * f);
    const g = stack[probe].grey, mm = modelMask(probe);
    for (let y = 0; y < H; y++)
      for (let x = 0; x < W; x++) {
        if (g[y * W + x] <= 127) continue;
        if (mm[y * W + x] > 127) same++;
        if (mm[y * W + (W - 1 - x)] > 127) flip++;
      }
  }
  mirror = flip > same;
  console.log('sutapimas: tiesiai %d, veidrodinis %d -> %s',
    same, flip, mirror ? 'VERČIAM' : 'kaip yra');
  if (mirror)
    for (const s of stack)
      for (let y = 0; y < H; y++)
        for (let x = 0; x < W >> 1; x++) {
          const a = y * W + x, b = y * W + (W - 1 - x);
          const t = s.grey[a]; s.grey[a] = s.grey[b]; s.grey[b] = t;
        }
}

const zb = new Float32Array(SIZE * SIZE).fill(-Infinity);
const px = Buffer.alloc(SIZE * SIZE * 3);
for (let i = 0; i < px.length; i += 3) { px[i] = 28; px[i+1] = 28; px[i+2] = 32; }

const span = Math.max(W * PIX, H * PIX, n * LAYER) * 1.15;
const scale = SIZE / span;
const cx = W * PIX / 2, cy = H * PIX / 2, cz = n * LAYER / 2;
const cyw = Math.cos(YAW), syw = Math.sin(YAW);
const cp = Math.cos(PITCH), sp = Math.sin(PITCH);
const DOT = Math.max(2, Math.ceil(scale * PIX));   // voxelio dėmė ekrane

/* Kas ekrano taške: 1 = detalė, 2 = supportas. Spalvinam po renderio, kai jau
   žinom paviršiaus formą iš gylio — taip vaizdas nustoja būti plokščias. */
const kind = new Uint8Array(SIZE * SIZE);

for (let i = 0; i < n; i++) {
  const g = stack[i].grey;
  const below = i ? stack[i - 1].grey : null;
  const above = i + 1 < n ? stack[i + 1].grey : null;
  const mm = modelMask ? modelMask(i) : null;
  const dz = i * LAYER - cz;
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const p = y * W + x;
      if (g[p] < 128) continue;
      /* Tik paviršius — vidus vis tiek nematomas, o taškų sumažėja dešimteriopai. */
      const inside = x > 0 && x < W - 1 && y > 0 && y < H - 1 &&
        g[p-1] > 127 && g[p+1] > 127 && g[p-W] > 127 && g[p+W] > 127 &&
        below && below[p] > 127 && above && above[p] > 127;
      if (inside) continue;
      const dx = x * PIX - cx, dy = y * PIX - cy;
      const rx = dx * cyw - dy * syw, ry = dx * syw + dy * cyw;
      const sx = Math.round(SIZE / 2 + rx * scale);
      const sy = Math.round(SIZE / 2 - (dz * cp - ry * sp) * scale);
      if (sx < 0 || sx >= SIZE || sy < 0 || sy >= SIZE) continue;
      const depth = ry * cp + dz * sp;
      const isSup = mm ? (mm[p] < 128 ? 2 : 1) : 1;
      /* Vienas voxelis ekrane užima kelis pikselius — piešiam jį dėme, ne
         tašku. Be to sluoksnis nuo sluoksnio lieka tarpas ir vaizdas išeina
         dryžuotas (pirmas bandymas, 08-13). */
      for (let oy = 0; oy < DOT; oy++)
        for (let ox = 0; ox < DOT; ox++) {
          const tx = sx + ox, ty = sy + oy;
          if (tx < 0 || tx >= SIZE || ty < 0 || ty >= SIZE) continue;
          const k = ty * SIZE + tx;
          if (depth <= zb[k]) continue;
          zb[k] = depth;
          kind[k] = isSup;
        }
    }
}

/* Apšvietimas iš gylio: normalė imama iš z-buferio nuolydžio, tad paviršius
   įgauna formą, o ne lieka plokščia dėme. Plius kontūras ten, kur gylis
   staiga šoka — nuo to supportai atsiskiria nuo detalės už jų. */
{
  const MODEL = [225, 118, 32], SUP = [176, 186, 205];
  for (let y = 0; y < SIZE; y++)
    for (let x = 0; x < SIZE; x++) {
      const k = y * SIZE + x;
      if (!kind[k]) continue;
      const z0 = zb[k];
      const zx = x + 1 < SIZE && kind[k+1] ? zb[k+1] : z0;
      const zy = y + 1 < SIZE && kind[k+SIZE] ? zb[k+SIZE] : z0;
      // Ekrano normalė: kuo staigesnis gylio nuolydis, tuo labiau nusuktas paviršius.
      const gx = (zx - z0) * 8, gy = (zy - z0) * 8;
      const nl = Math.sqrt(gx*gx + gy*gy + 1);
      let lit = (0.45 * gx - 0.35 * gy + 1) / nl;
      lit = Math.max(0.35, Math.min(1.15, 0.55 + 0.55 * lit));
      // Kontūras: kaimynas tuščias arba daug toliau.
      let edge = 0;
      for (const d of [1, -1, SIZE, -SIZE]) {
        const q = k + d;
        if (q < 0 || q >= SIZE * SIZE) continue;
        if (!kind[q] || kind[q] !== kind[k] || z0 - zb[q] > 1.2) { edge = 1; break; }
      }
      const c = kind[k] === 2 ? SUP : MODEL;
      const f = edge ? 0.55 : lit;
      px[k*3] = Math.min(255, c[0] * f);
      px[k*3+1] = Math.min(255, c[1] * f);
      px[k*3+2] = Math.min(255, c[2] * f);
    }
}
writeFileSync(process.argv[3], writePNG(SIZE, SIZE, px));
console.log('irasyta:', process.argv[3]);
