/* Rafto storio testas: keturi storiai vienoje plokštėje.
 *
 * KODĖL VIENAME FAILE. Klausimas yra lyginamasis - „nuo kurio storio raftas
 * dar laikosi spausdinant, bet nusiima be jėgos". Keturi atskiri spaudiniai
 * lygintųsi per skirtingą dervos temperatūrą, plėvelės būvį ir stalo švarą;
 * vienoje plokštėje visi keturi patiria tą patį. Ir tai vienas ~12 min
 * spaudinys vietoj keturių.
 *
 * KODĖL BŪTENT ŠIE KETURI. 0,30 mm - V etalonas iš patirties (riba, kur dar
 * laikosi, bet nusiima be jėgos). 0,15 mm - MŪSŲ dabartinis numatytasis
 * (`bridge.cpp` `make_pad_config`: wall_thickness 0,15 + wall_height 0,
 * o `Pad.hpp:83` sako, kad pado aukštis yra jų suma), t. y. dvigubai
 * plonesnis už etaloną. 0,60 ir 1,00 - kita pusė, kad matytųsi, nuo kada
 * darosi per stipru.
 *
 * KĄ ŠIS TESTAS TIKRINA IR KO NE. Tikrina STORĮ. Netikrina mūsų pado FORMOS
 * (apvado 1,6 mm, nuolydžio, apkabinimo aplink detalę) - čia raftai
 * stačiakampiai. Formos klausimas atskiras ir eina po šito.
 *
 * ŽYMĖJIMAS. Kiekvienas padas turi 1..4 įpjovas krašte - tiek, koks jo
 * numeris. Ant atspausdintos detalės kitaip neatskirtum, kuris kuris.
 *
 *     node scripts/dev/make_raft_test.mjs [isvestis.zip]
 */
import { writeFileSync } from 'fs';
import { deflateSync } from 'zlib';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const here = dirname(fileURLToPath(import.meta.url));
const M = await import('file:///' +
  join(here, '..', '..', 'web', 'lib', 'slicer.js').replace(/\\/g, '/'));

const W = M.RES.w, H = M.RES.h;
const OUT = process.argv[2] || 'C:/PIO-build/raftai.zip';
const PX = 40.8 / 320;

/* Storiai sluoksniais prie 0,05 mm. */
const RAFTAI = [
  { sl: 3,  mm: 0.15, kas: 'musu numatytasis' },
  { sl: 6,  mm: 0.30, kas: 'V etalonas' },
  { sl: 12, mm: 0.60, kas: '' },
  { sl: 20, mm: 1.00, kas: '' },
];

const DETALES_SL = 20;                     // 1,00 mm - tiek, kad butu uz ko paimti
const SL = Math.max(...RAFTAI.map(r => r.sl)) + DETALES_SL;

/* Padai 2x2. Kiekvienas 94 x 70 px = 11,99 x 8,93 mm = 107 mm².
   Keturi kartu 428 mm² - 34 % plokstes, gerokai maziau uz kupono 60 %. */
const PAD_W = 94, PAD_H = 70;
const VIETOS = [
  [24, 26], [202, 26],
  [24, 144], [202, 144],
];
const DET = 47;                            // detale 47 x 47 px = 5,99 mm

function sluoksnis(n) {
  const g = new Uint8Array(W * H);
  const dazyk = (x0, y0, w, h, v) => {
    for (let y = Math.max(0, y0); y < Math.min(H, y0 + h); y++)
      for (let x = Math.max(0, x0); x < Math.min(W, x0 + w); x++) g[y * W + x] = v;
  };

  RAFTAI.forEach((r, i) => {
    const [x0, y0] = VIETOS[i];
    if (n < r.sl) {
      dazyk(x0, y0, PAD_W, PAD_H, 255);
      /* Ipjovos: tiek, koks pado numeris. Kertam is pado krasto, tad jos
         matomos ir tada, kai padas jau nuimtas nuo stalo. */
      if (n >= r.sl - 2)
        for (let k = 0; k <= i; k++) dazyk(x0 + 4 + k * 12, y0, 6, 6, 0);
    } else if (n < r.sl + DETALES_SL) {
      dazyk(x0 + ((PAD_W - DET) >> 1), y0 + ((PAD_H - DET) >> 1), DET, DET, 255);
    }
  });
  return g;
}

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

const files = [];
for (let i = 0; i < SL; i++) {
  const g = sluoksnis(i);
  const m = new Uint8Array(W * H);                    // display_mirror_x
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) m[y * W + x] = g[y * W + (W - 1 - x)];
  files.push({ name: (i + 1) + '.png', data: new Uint8Array(png(W, H, m)) });
}
files.push({
  name: 'config.ini',
  data: new TextEncoder().encode(
    `layerHeight = ${M.LAYER_MM}\njobDir = tinymaker\nnumFast = ${SL}\n`),
});

const zip = M.zipStore(files);
writeFileSync(OUT, Buffer.from(zip instanceof Blob ? await zip.arrayBuffer() : zip));

console.log('raftu testas:', OUT);
console.log(`  ${SL} sluoksniai = ${(SL * M.LAYER_MM).toFixed(2)} mm auksčio`);
RAFTAI.forEach((r, i) => console.log(
  `  ${i + 1} įpjova(-os): raftas ${r.mm.toFixed(2)} mm (${r.sl} sl.)` +
  (r.kas ? ` - ${r.kas}` : '')));
console.log(`  padas ${(PAD_W * PX).toFixed(1)} x ${(PAD_H * PX).toFixed(1)} mm =` +
  ` ${(PAD_W * PAD_H * PX * PX).toFixed(0)} mm², keturi kartu` +
  ` ${(4 * PAD_W * PAD_H * PX * PX).toFixed(0)} mm²` +
  ` (${(4 * PAD_W * PAD_H * PX * PX / (40.8 * 30.6) * 100).toFixed(0)} % plokštės)`);
console.log(`  detalė ant kiekvieno ${(DET * PX).toFixed(1)} x ${(DET * PX).toFixed(1)} mm,` +
  ` ${(DETALES_SL * M.LAYER_MM).toFixed(2)} mm aukščio`);
console.log(`  trukmė ~${Math.round((6 * 35 + (SL - 6) * 14) / 60)} min be lifto`);
