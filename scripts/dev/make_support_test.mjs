/* Atramų atplėšiamumo testas (T-92): maža detalė, kurios atramos liečia
 * matomą paviršių.
 *
 * KLAUSIMAS, Į KURĮ ATSAKO. Mūsų atramas dabar generuoja PrusaSlicer
 * libslic3r grandinė su smaigaliu 0,5 mm (`bridge.cpp` `make_support_cfg`:
 * support_head_front_diameter). Ar toks smaigalys mūsų dervai tinka -
 * nusilupa palikdamas taškelį, ar išplėšia kraterį - failas nepasako.
 * Atsako tik pirštai, perbraukiantys per atspausdintą šoną.
 *
 * KODĖL PAKREIPTAS PUODELIS. Reikia lygaus, matomo paviršiaus, po kuriuo
 * atramos tikrai atsiras. Pakreiptas cilindras duoda būtent tai: viena pusė
 * lieka švari palyginimui, kita nusėta smaigalių pėdsakais.
 *
 * DYDIS. Numatytieji 10 mm - apie 200 sluoksnių, ~50 min. Spaudinio laikas
 * čia svarbus (testų ratas ilgas), o pėdsakui pamatyti didelės detalės
 * nereikia.
 *
 *     node scripts/dev/make_support_test.mjs [--mm=10] [--stl=...] [isvestis.zip]
 */
import fs from 'fs';
import { createRequire } from 'module';
import { pathToFileURL } from 'url';

const argv = process.argv.slice(2);
const vertė = (p, num) => {
  const a = argv.find(x => x.startsWith(p));
  return a ? (num ? Number(a.slice(p.length)) : a.slice(p.length)) : null;
};

/* Numatytieji = TIKSLIAI tai, kuo padarytas geleziai isduotas T-92 failas
   (patikrinta 2026-08-24 lyginant archyvus baitas i baita). Anksciau cia stovejo
   cup45 @10 mm, ir paleidus be argumentu iseidavo KITAS spaudinys nei tas, kuri
   V laiko rankose - o tai butent tas atvejis, kai testas ir tikrove issiskiria
   tyliai. */
const AUKSTIS = vertė('--mm=', true) || 12;
const STL = vertė('--stl=') || 'C:/PIO-build/slicer-lab/cup9.stl';
const OUT = argv.find(a => !a.startsWith('--')) || 'C:/PIO-build/atramu-testas.zip';
const WASM = process.env.WASM_BUILD || 'C:/PIO-build/wasm-verify';

const S = await import(pathToFileURL('C:/PIO-build/exp2-wt/web/lib/slicer.js').href);
const require = createRequire(import.meta.url);
const M = await require(`${WASM}/sla-web.js`)();

const buf = fs.readFileSync(STL);
const raw = S.parseSTL(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength)).positions;

/* Pastatom taip, kaip pastatytų pultas, tada sumažinam iki prašyto aukščio.
   Mastelis taikomas PO pastatymo - kitaip `place` nuspręstų kitaip. */
const pastatytas = S.place(raw, S.autoOrient(raw).tr);
const pr = S.bounds(pastatytas).size;
const k = AUKSTIS / pr[2];
const pos = new Float32Array(pastatytas.length);
for (let i = 0; i < pastatytas.length; i++) pos[i] = pastatytas[i] * k;
const dydis = S.bounds(pos).size;

const ptr = M._malloc(pos.length * 4);
M.HEAPF32.set(pos, ptr >> 2);
let rez;
try {
  rez = JSON.parse(M.ccall('sla_slice_mesh', 'string',
    ['number', 'number', 'number', 'number'], [ptr, pos.length / 9, 0.05, 0]));
} finally { M._free(ptr); }

if (rez.klaida) { console.error('klaida:', rez.klaida); process.exit(1); }

const eks = JSON.parse(M.ccall('sla_export_sl1', 'string', ['string', 'string'],
  ['/isvestis.sl1', 'tinymaker']));
if (eks.klaida) { console.error('klaida:', eks.klaida); process.exit(1); }
fs.writeFileSync(OUT, Buffer.from(M.FS.readFile('/isvestis.sl1')));

console.log('atramu testas:', OUT);
console.log(`  modelis: ${STL.split('/').pop()}, sumazintas iki` +
  ` ${dydis[0].toFixed(1)} x ${dydis[1].toFixed(1)} x ${dydis[2].toFixed(1)} mm`);
console.log('  sluoksniu:', eks.sluoksniu, `= ${(eks.sluoksniu * 0.05).toFixed(2)} mm`);
for (const [k2, v] of Object.entries(rez))
  if (typeof v !== 'object') console.log(`  ${k2}: ${v}`);
console.log(`  trukme ~${Math.round((6 * 35 + (eks.sluoksniu - 6) * 14) / 60)} min be lifto`);
