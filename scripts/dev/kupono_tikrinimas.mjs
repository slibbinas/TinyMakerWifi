/* Kupono patikra skaičiais: ką iš tikrųjų nupiešėm į ZIP.
 *
 * Skaitom PAČIUS PNG baitus iš archyvo (ne generatoriaus kintamuosius) -
 * kitaip tikrintume savo ketinimus, ne rezultatą.
 *
 *     node kupono_tikrinimas.mjs C:/PIO-build/kuponas.zip
 */
import { readFileSync } from 'fs';
import { inflateSync } from 'zlib';

const PX = 40.8 / 320;                       // mm viename pikselyje
const PLOKSTE = 40.8 * 30.6;

function zipFailai(buf) {
  const out = new Map();
  for (let i = 0; i + 30 <= buf.length; ) {
    if (buf.readUInt32LE(i) !== 0x04034b50) { i++; continue; }
    const dydis = buf.readUInt32LE(i + 18);
    const vLen = buf.readUInt16LE(i + 26), eLen = buf.readUInt16LE(i + 28);
    const vardas = buf.slice(i + 30, i + 30 + vLen).toString();
    const nuo = i + 30 + vLen + eLen;
    out.set(vardas, buf.slice(nuo, nuo + dydis));
    i = nuo + dydis;
  }
  return out;
}

/* Pilkas PNG, filtras 0 visose eilutėse - būtent toks, kokį rašo make_coupon. */
function pngPilkas(buf) {
  let w = 0, h = 0; const idat = [];
  for (let i = 8; i + 8 <= buf.length; ) {
    const ilgis = buf.readUInt32BE(i), tipas = buf.slice(i + 4, i + 8).toString();
    const d = buf.slice(i + 8, i + 8 + ilgis);
    if (tipas === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); }
    if (tipas === 'IDAT') idat.push(d);
    i += 12 + ilgis;
  }
  const raw = inflateSync(Buffer.concat(idat));
  const g = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) {
    if (raw[y * (w + 1)] !== 0) throw new Error('netikėtas PNG filtras');
    raw.copy(Buffer.from(g.buffer), y * w, y * (w + 1) + 1, y * (w + 1) + 1 + w);
  }
  return { w, h, g };
}

/* Vientisos eilučių grupės - salos, kurias plėvelė plėšia atskirai. */
function salos({ w, h, g }) {
  const yra = y => { for (let x = 0; x < w; x++) if (g[y * w + x]) return true; return false; };
  const out = []; let pr = -1;
  for (let y = 0; y < h; y++) {
    const e = yra(y);
    if (e && pr < 0) pr = y;
    if (!e && pr >= 0) { out.push([pr, y - 1]); pr = -1; }
  }
  if (pr >= 0) out.push([pr, h - 1]);
  return out;
}

const kelias = process.argv[2] || 'C:/PIO-build/kuponas.zip';
const f = zipFailai(readFileSync(kelias));
const png = [...f.keys()].filter(n => n.endsWith('.png'))
  .sort((a, b) => parseInt(a) - parseInt(b));

const pagr = pngPilkas(f.get(png[5]));
const test = pngPilkas(f.get(png[30]));

console.log(kelias);
console.log('  sluoksnių:', png.length, '· config.ini:', f.has('config.ini') ? 'yra' : 'NĖRA');

let plotis = 0;
for (let x = 0; x < pagr.w; x++)
  for (let y = 0; y < pagr.h; y++) if (pagr.g[y * pagr.w + x]) { plotis++; break; }

const ps = salos(pagr);
console.log('  pagrindo salos:', ps.length);
let viso = 0;
for (const [y0, y1] of ps) {
  const mm2 = plotis * (y1 - y0 + 1) * PX * PX; viso += mm2;
  console.log(`    y ${y0}..${y1}  ${mm2.toFixed(0)} mm²`);
}
console.log(`  viso ${viso.toFixed(0)} mm² (${(viso / PLOKSTE * 100).toFixed(0)} % plokštės),` +
  ` didžiausia sala ${Math.max(...ps.map(([a, b]) => plotis * (b - a + 1) * PX * PX)).toFixed(0)} mm²`);

const ryskumai = [...new Set(test.g)].filter(v => v).sort((a, b) => b - a);
console.log('  ryškumai teste:', ryskumai.join(', '));
/* 255 sąmoningai nėra 8 kartotinis - tai baltas etalonas, o ne kopėtėlė. */
console.log('  visi 8 kartotiniai (be balto 255):',
  ryskumai.filter(v => v !== 255).every(v => v % 8 === 0));
console.log('  juostos teste:');
for (const [y0, y1] of salos(test)) {
  const set = new Set();
  for (let y = y0; y <= y1; y++)
    for (let x = 0; x < test.w; x++) { const v = test.g[y * test.w + x]; if (v) set.add(v); }
  console.log(`    y ${y0}..${y1}  ryškumų ${set.size}`);
}
