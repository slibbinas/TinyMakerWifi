/* Du renderiai greta viename PNG: kairėje PrusaSlicer, dešinėje mūsų.
   node montage.mjs kaire.png desine.png isvestis.png */
import { readFileSync, writeFileSync } from 'fs';
import { inflateSync, deflateSync } from 'zlib';

function readPNG(buf) {
  let o = 8, w = 0, h = 0, colour = 0; const idat = [];
  while (o < buf.length) {
    const len = buf.readUInt32BE(o), type = buf.toString('ascii', o + 4, o + 8);
    const data = buf.subarray(o + 8, o + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); colour = data[9]; }
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
  return { w, h, ch, px };
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
    chunk('IHDR', ih), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

/* 5x7 šriftas antraštėms — be jo neaišku, kuri pusė kieno. */
const FONT = {
  A:['01110','10001','10001','11111','10001','10001','10001'],
  C:['01110','10001','10000','10000','10000','10001','01110'],
  E:['11111','10000','10000','11110','10000','10000','11111'],
  I:['11111','00100','00100','00100','00100','00100','11111'],
  L:['10000','10000','10000','10000','10000','10000','11111'],
  M:['10001','11011','10101','10101','10001','10001','10001'],
  P:['11110','10001','10001','11110','10000','10000','10000'],
  R:['11110','10001','10001','11110','10100','10010','10001'],
  S:['01111','10000','10000','01110','00001','00001','11110'],
  U:['10001','10001','10001','10001','10001','10001','01110'],
  'B':['11110','10001','10001','11110','10001','10001','11110'],
  'D':['11110','10001','10001','10001','10001','10001','11110'],
  'F':['11111','10000','10000','11110','10000','10000','10000'],
  'G':['01110','10001','10000','10111','10001','10001','01110'],
  'H':['10001','10001','10001','11111','10001','10001','10001'],
  'J':['00111','00010','00010','00010','00010','10010','01100'],
  'K':['10001','10010','10100','11000','10100','10010','10001'],
  'N':['10001','11001','10101','10011','10001','10001','10001'],
  'O':['01110','10001','10001','10001','10001','10001','01110'],
  'Q':['01110','10001','10001','10001','10101','10010','01101'],
  'T':['11111','00100','00100','00100','00100','00100','00100'],
  'V':['10001','10001','10001','10001','10001','01010','00100'],
  'W':['10001','10001','10001','10101','10101','11011','10001'],
  'X':['10001','10001','01010','00100','01010','10001','10001'],
  'Y':['10001','10001','01010','00100','00100','00100','00100'],
  'Z':['11111','00001','00010','00100','01000','10000','11111'],
  '0':['01110','10011','10101','10101','10101','11001','01110'],
  '1':['00100','01100','00100','00100','00100','00100','01110'],
  '2':['01110','10001','00001','00110','01000','10000','11111'],
  '3':['11111','00010','00100','00010','00001','10001','01110'],
  '4':['00010','00110','01010','10010','11111','00010','00010'],
  '5':['11111','10000','11110','00001','00001','10001','01110'],
  '6':['00110','01000','10000','11110','10001','10001','01110'],
  '7':['11111','00001','00010','00100','01000','01000','01000'],
  '8':['01110','10001','10001','01110','10001','10001','01110'],
  '9':['01110','10001','10001','01111','00001','00010','01100'],
  '-':['00000','00000','00000','11111','00000','00000','00000'],
  '.':['00000','00000','00000','00000','00000','01100','01100'],
  ' ':['00000','00000','00000','00000','00000','00000','00000'],
};
function text(px, W, s, x0, y0, scale, col) {
  let cx = x0;
  for (const chr of s.toUpperCase()) {
    const g = FONT[chr] || FONT[' '];
    for (let r = 0; r < 7; r++)
      for (let c = 0; c < 5; c++) {
        if (g[r][c] !== '1') continue;
        for (let dy = 0; dy < scale; dy++)
          for (let dx = 0; dx < scale; dx++) {
            const x = cx + c * scale + dx, y = y0 + r * scale + dy;
            const k = (y * W + x) * 3;
            px[k] = col[0]; px[k+1] = col[1]; px[k+2] = col[2];
          }
      }
    cx += 6 * scale;
  }
}

const L = readPNG(readFileSync(process.argv[2]));
const R = readPNG(readFileSync(process.argv[3]));
const BAR = 46, GAP = 8;
const W = L.w + GAP + R.w, H = BAR + Math.max(L.h, R.h);
const out = Buffer.alloc(W * H * 3);
for (let i = 0; i < out.length; i += 3) { out[i] = 20; out[i+1] = 20; out[i+2] = 23; }
const blit = (src, ox) => {
  for (let y = 0; y < src.h; y++)
    for (let x = 0; x < src.w; x++) {
      const s = (y * src.w + x) * src.ch, d = ((y + BAR) * W + x + ox) * 3;
      out[d] = src.px[s]; out[d+1] = src.px[s+1]; out[d+2] = src.px[s+2];
    }
};
blit(L, 0);
blit(R, L.w + GAP);
/* Antrastes - argumentais. Kietai irasytos meluodavo, kai greta dedamos dvi
   MUSU versijos (pries/po). */
text(out, W, (process.argv[5] || 'PRUSASLICER').toUpperCase(), 24, 14, 3, [176, 186, 205]);
text(out, W, (process.argv[6] || 'MUSU').toUpperCase(), L.w + GAP + 24, 14, 3, [232, 130, 40]);
writeFileSync(process.argv[4], writePNG(W, H, out));
console.log('irasyta:', process.argv[4], W + 'x' + H);
