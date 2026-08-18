/* Pilkumo kuponas: nuo kokio ryškumo mūsų geležis apskritai ką nors sukietina.
 *
 * KODĖL JO REIKIA. Sliceris piešia pilkus kraštus (antialiasing), ir iki šiol
 * į ekraną keliavo visa skalė, įskaitant ryškumus 1..90. Publikuotos ribos
 * (UVtools: gedimas nuo ~190, tuštuma ties ~160) gautos ant MONO LCD su 2-3 s
 * ekspozicija. Mūsų ekranas spalvotas IPS, ekspozicija ~14 s, tad doza tam
 * pačiam ryškumui 5-7 kartus didesnė ir riba visai kita. Nusirašyti negalima.
 *
 * KĄ MATUOJA. Jacobs darbinė kreivė (Cd = Dp·ln(E/Ec)) sako, kad kietėjimo
 * gylis nuo ekspozicijos priklauso LOGARITMIŠKAI ir kad yra kritinė energija
 * Ec, žemiau kurios nekietėja niekas. Kadangi E ∝ ryškumas, ta riba mums yra
 * ryškumo riba. Kuponas ieško dviejų dalykų:
 *
 *   A juosta — atskiri stulpeliai, kiekvienas savo ryškumu. Stulpelis užauga
 *              tik jei kiekvienas jo sluoksnis prilimpa prie ankstesnio, t. y.
 *              jei Cd ≥ sluoksnio storis. Duoda ribą „nuo kada laiko".
 *   B juosta — balta šerdis su pilku apvadu iš abiejų pusių. Matuojama
 *              slankmačiu: ar pilkas pikselis ŠALIA balto prideda medžiagos.
 *              Ši riba svarbesnė, nes mūsų pilki pikseliai visada šalia baltų,
 *              ir ji bus ŽEMESNĖ už A (šoninė sklaida padeda).
 *
 * KAIP SKAITYTI. Kur A stulpeliai baigiasi - ten AA_FLOOR viršutinė riba.
 * Kur B apvadas nustoja platinti briauną - ten tikroji riba. Jei B plotis auga
 * tolygiai, `f/(1+f)` transformacija pagrįsta; jei šuoliais - reikia kreivės.
 *
 * ⚠️ Sąmoningai NĖRA kabančių dėmių: nesukietėjusi derva pieniška plėvele
 * nusėstų ant FEP ir gadintų kitus spaudinius.
 *
 *     node scripts/dev/make_coupon.mjs [isvestis.zip]
 */
import { writeFileSync } from 'fs';
import { deflateSync } from 'zlib';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const here = dirname(fileURLToPath(import.meta.url));
const M = await import('file:///' +
  join(here, '..', '..', 'web', 'lib', 'slicer.js').replace(/\\/g, '/'));

const W = M.RES.w, H = M.RES.h;                       // 320 x 240
const OUT = process.argv[2] || 'C:/PIO-build/kuponas.zip';

/* Ryškumai: 16 kopėtėlių per visą skalę. Visi yra 8 kartotiniai, nes ekranas
   piešiamas per RGB565 - realių pilkumo lygių ten ~32, ne 256, ir ne kartotinis
   skaičius ekrane virstų kaimyniniu. */
const RYSKUMAI = [255, 240, 224, 208, 192, 176, 160, 144, 128, 112, 96, 80, 64, 48, 32, 16];

const PAGRINDO_SL = 12;          // 0,60 mm - storesnis nei bet kuris base_layers
const TESTO_SL = 40;             // 2,00 mm
const SL = PAGRINDO_SL + TESTO_SL;

const X0 = 4, JUOSTA = 15, TARPAS = 4;                // 16 x 19 = 304 px
const A_Y0 = 16, A_Y1 = 48;                           // stulpeliai, 4,2 mm
const B_Y0 = 70, B_Y1 = 150;                          // briaunos, 10,2 mm
const B_SERDIS = 8, B_APVADAS = 4;                    // 1,02 mm + 0,51 mm iš šonų
const PAGR_Y0 = 4, PAGR_Y1 = 155;

const stulpX = i => X0 + i * (JUOSTA + TARPAS);

function sluoksnis(n) {
  const g = new Uint8Array(W * H);                    // 0 = tamsu
  const dazyk = (x0, x1, y0, y1, v) => {
    for (let y = Math.max(0, y0); y <= Math.min(H - 1, y1); y++)
      for (let x = Math.max(0, x0); x <= Math.min(W - 1, x1); x++) g[y * W + x] = v;
  };

  if (n < PAGRINDO_SL) {
    dazyk(X0, stulpX(15) + JUOSTA, PAGR_Y0, PAGR_Y1, 255);
    /* Įpjova kairiajame apatiniame kampe: PNG rašomi veidrodiniai, tad ant
       atspausdintos detalės be žymės neatskirtum, kuris galas yra 255. */
    if (n >= PAGRINDO_SL - 3) dazyk(X0, X0 + 7, PAGR_Y0, PAGR_Y0 + 7, 0);
    return g;
  }

  for (let i = 0; i < RYSKUMAI.length; i++) {
    const v = RYSKUMAI[i], x = stulpX(i);
    dazyk(x, x + JUOSTA - 1, A_Y0, A_Y1, v);          // A: visas stulpelis savo ryškumu
    const c0 = x + ((JUOSTA - B_SERDIS) >> 1);        // B: balta šerdis...
    dazyk(c0, c0 + B_SERDIS - 1, B_Y0, B_Y1, 255);
    dazyk(c0 - B_APVADAS, c0 - 1, B_Y0, B_Y1, v);     // ...su pilku apvadu
    dazyk(c0 + B_SERDIS, c0 + B_SERDIS + B_APVADAS - 1, B_Y0, B_Y1, v);
    /* Liniuotės žymos kas ketvirtą, BALTOS. Be jų ant atspausdintos detalės
       tektų skaičiuoti juostas nuo krašto - o būtent tos, kurias norim
       suskaičiuoti, ten ir bus dingusios. Baltos, nes žyma privalo užaugti
       net prie ryškumo 16. */
    if (i % 4 === 0) {
      dazyk(x, x + JUOSTA - 1, A_Y1 + 4, A_Y1 + 9, 255);
      if (i % 8 === 0) dazyk(x, x + JUOSTA - 1, A_Y1 + 12, A_Y1 + 17, 255);
    }
  }
  return g;
}

/* Pilkas PNG be filtrų. Tas pats koderis kaip `slicer-lab/ourslices.mjs`:
   naršyklėje piešia canvas, o čia jo nėra. */
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

const px = 40.8 / 320;
console.log(`kuponas: ${OUT}`);
console.log(`  ${SL} sluoksniai (${PAGRINDO_SL} pagrindo + ${TESTO_SL} testo) = ${(SL * M.LAYER_MM).toFixed(2)} mm`);
console.log(`  16 juostų, ryškumai ${RYSKUMAI[0]} … ${RYSKUMAI[15]} (žingsnis 16)`);
console.log(`  A stulpelis ${(JUOSTA * px).toFixed(2)} x ${((A_Y1 - A_Y0) * px).toFixed(2)} mm`);
console.log(`  B šerdis ${(B_SERDIS * px).toFixed(2)} mm, apvadas ${(B_APVADAS * px).toFixed(2)} mm iš abiejų pusių`);
console.log(`  trukmė ~${Math.round((PAGRINDO_SL * 35 + TESTO_SL * 14) / 60)} min be lifto`);
