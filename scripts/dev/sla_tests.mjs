/* SLA pjaustymo testai — matematinė verifikacija be išorinio STL.
 *
 * Tai V prašytos C++ aplinkos (Catch2 + AddressSanitizer + libslic3r) atitikmuo:
 * įrankis kitas, tikrinimai tie patys.
 *
 *   Catch2 TEST_CASE/REQUIRE -> node:test + assert
 *   AddressSanitizer         -> invariantų sargai (NaN/Infinity, uždarumas)
 *   its_make_cube(10,10,10)  -> kubas generuojamas KODE, 12 trikampių
 *   Slic3r::SVG              -> writeSVG(), kad pjūvį būtų galima atsiųsti tekstu
 *   std::cout žingsniai      -> console.log kiekviename žingsnyje
 *
 * Paleisti:  node scripts/dev/sla_tests.mjs
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdirSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const HERE = dirname(fileURLToPath(import.meta.url));
const OUT = join(HERE, 'test-out');
try { mkdirSync(OUT, { recursive: true }); } catch {}

const SLICER = process.env.SLICER || join(HERE, '..', '..', 'web', 'lib', 'slicer.js');
const M = await import('file:///' + SLICER.replace(/\\/g, '/') + '?t=' + process.pid);
console.log('[setup] modulis:', SLICER, '· versija', M.VERSION);

/* ------------------------------------------------------------------ kubas */
/** Tobulas kubas be jokio failo: 12 trikampių, XY centre nulio, Z nuo 0.
 *  Atitinka libslic3r its_make_cube(x,y,z) prasmę — tik pastatytas ten, kur
 *  mūsų pipeline stato modelį (plokštė yra z = 0). */
function makeCube(sx, sy, sz) {
  const x0 = -sx / 2, x1 = sx / 2, y0 = -sy / 2, y1 = sy / 2, z0 = 0, z1 = sz;
  const v = [
    [x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],   // 0..3 apačia
    [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1],   // 4..7 viršus
  ];
  // Kiekvienas veidas — du trikampiai, apėjimas prieš laikrodžio rodyklę
  // žiūrint IŠ IŠORĖS, kad normalė rodytų laukan (to tikisi sliceAt).
  const f = [
    [0, 2, 1], [0, 3, 2],       // apačia (normalė žemyn)
    [4, 5, 6], [4, 6, 7],       // viršus
    [0, 1, 5], [0, 5, 4],       // y0
    [1, 2, 6], [1, 6, 5],       // x1
    [2, 3, 7], [2, 7, 6],       // y1
    [3, 0, 4], [3, 4, 7],       // x0
  ];
  const pos = new Float32Array(f.length * 9);
  let p = 0;
  for (const t of f) for (const i of t) { pos[p++] = v[i][0]; pos[p++] = v[i][1]; pos[p++] = v[i][2]; }
  return pos;
}

/** Poligono plotas iš pjūvio atkarpų — Gauso (batraiščio) formulė.
 *  Atkarpos gali eiti bet kokia tvarka: suma nepriklauso nuo jų eiliškumo,
 *  nes kiekviena įneša savo trikampio su pradžia nulyje plotą. Ženklas rodo
 *  apėjimo kryptį, tad skylė atimasi savaime. */
function segArea(seg) {
  let a = 0;
  for (let i = 0; i < seg.length; i += 4)
    a += seg[i] * seg[i + 3] - seg[i + 2] * seg[i + 1];
  return a / 2;
}

/** Ar atkarpos sudaro UŽDARUS kontūrus: kiekvienas taškas turi būti ir
 *  pradžia, ir pabaiga. Tai mūsų „sanitizer" geometrijai — atviras kontūras
 *  reiškia prakiurusį pjūvį, kurio plotas atsitiktinis. */
function openEnds(seg, eps = 1e-4) {
  const key = (x, y) => Math.round(x / eps) + ',' + Math.round(y / eps);
  const bal = new Map();
  for (let i = 0; i < seg.length; i += 4) {
    const a = key(seg[i], seg[i + 1]), b = key(seg[i + 2], seg[i + 3]);
    bal.set(a, (bal.get(a) || 0) - 1);
    bal.set(b, (bal.get(b) || 0) + 1);
  }
  let bad = 0;
  for (const v of bal.values()) if (v !== 0) bad++;
  return bad;
}

/** Skaičių sargas: nė vienos NaN ar begalybės. C++ pusėje tą patį vaidmenį
 *  atlieka AddressSanitizer — čia blogi skaičiai, ne bloga atmintis. */
function finiteAll(arr) {
  for (let i = 0; i < arr.length; i++) if (!Number.isFinite(arr[i])) return false;
  return true;
}

/** Pjūvis į SVG — kad sugedus geometrijai V galėtų atsiųsti failo tekstą.
 *  Vienas milimetras = vienas SVG vienetas, Y apverstas (SVG auga žemyn). */
function writeSVG(seg, file, note) {
  let minx = Infinity, miny = Infinity, maxx = -Infinity, maxy = -Infinity;
  for (let i = 0; i < seg.length; i += 4) {
    minx = Math.min(minx, seg[i], seg[i + 2]); maxx = Math.max(maxx, seg[i], seg[i + 2]);
    miny = Math.min(miny, seg[i + 1], seg[i + 3]); maxy = Math.max(maxy, seg[i + 1], seg[i + 3]);
  }
  if (!Number.isFinite(minx)) { minx = miny = 0; maxx = maxy = 1; }
  const pad = 2;
  const w = (maxx - minx) + pad * 2, h = (maxy - miny) + pad * 2;
  const X = x => (x - minx + pad).toFixed(4);
  const Y = y => (maxy - y + pad).toFixed(4);
  let s = `<?xml version="1.0" encoding="UTF-8"?>\n` +
    `<svg xmlns="http://www.w3.org/2000/svg" width="${w.toFixed(2)}mm" height="${h.toFixed(2)}mm" ` +
    `viewBox="0 0 ${w.toFixed(4)} ${h.toFixed(4)}">\n` +
    `<!-- ${note} · ${seg.length / 4} atkarpų · plotas ${segArea(seg).toFixed(6)} mm2 -->\n` +
    `<rect width="100%" height="100%" fill="white"/>\n`;
  for (let i = 0; i < seg.length; i += 4)
    s += `<line x1="${X(seg[i])}" y1="${Y(seg[i + 1])}" x2="${X(seg[i + 2])}" y2="${Y(seg[i + 3])}" ` +
         `stroke="black" stroke-width="0.05"/>\n`;
  s += `</svg>\n`;
  writeFileSync(file, s);
  return file;
}

/* ------------------------------------------------------------------ testai */

test('Basic 3D Cube Slicing and Area Verification', () => {
  console.log('[1/4] kuriam 10x10x10 mm kubą kode (12 trikampių)…');
  const cube = makeCube(10, 10, 10);
  assert.equal(cube.length, 12 * 9, 'kubas turi būti 12 trikampių');
  assert.ok(finiteAll(cube), 'kube neturi būti NaN/Infinity');

  console.log('[2/4] pjauname ties Z = 5.0 mm…');
  const seg = [];
  M.sliceAt(cube, 5.0, seg);
  console.log('      gauta atkarpų:', seg.length / 4);
  assert.ok(seg.length >= 4 * 4, 'kvadratas turi duoti bent 4 atkarpas');
  assert.ok(finiteAll(seg), 'pjūvyje neturi būti NaN/Infinity');
  assert.equal(openEnds(seg), 0, 'kontūras turi būti uždaras');

  console.log('[3/4] skaičiuojame plotą…');
  const area = Math.abs(segArea(seg));
  console.log('      plotas =', area.toFixed(6), 'mm2 (laukiama 100.000000)');
  assert.ok(Math.abs(area - 100.0) < 0.001, `plotas ${area}, laukta 100 ±0.001`);

  console.log('[4/4] rašome SVG…');
  console.log('      ', writeSVG(seg, join(OUT, 'cube-z5.svg'), 'kubas 10x10x10, Z=5'));
});

test('Slice height does not change a prism cross-section', () => {
  const cube = makeCube(10, 10, 10);
  for (const z of [0.05, 2.5, 5.0, 7.5, 9.95]) {
    const seg = [];
    M.sliceAt(cube, z, seg);
    const area = Math.abs(segArea(seg));
    console.log('      Z =', z, '-> plotas', area.toFixed(6));
    assert.ok(Math.abs(area - 100.0) < 0.001, `Z=${z}: plotas ${area}`);
  }
});

test('Above and below the body there is nothing', () => {
  const cube = makeCube(10, 10, 10);
  for (const z of [-1, 10.5, 50]) {
    const seg = [];
    M.sliceAt(cube, z, seg);
    console.log('      Z =', z, '-> atkarpų', seg.length / 4);
    assert.equal(seg.length, 0, `Z=${z} neturi duoti nieko`);
  }
});

test('A hole subtracts its own area', () => {
  console.log('[skylė] 10x10x10 kubas su 4x4 kanalu per vidurį…');
  const outer = makeCube(10, 10, 10);
  const inner = makeCube(4, 4, 10);
  /* Vidinį kubą apsukam: normalės į vidų, tad medžiaga lieka IŠORĖJE — taip
     nurodoma skylė. Sukeičiam dvi kiekvieno trikampio viršūnes. */
  const flipped = new Float32Array(inner.length);
  for (let i = 0; i < inner.length; i += 9) {
    for (let k = 0; k < 3; k++) flipped[i + k] = inner[i + k];
    for (let k = 0; k < 3; k++) flipped[i + 3 + k] = inner[i + 6 + k];
    for (let k = 0; k < 3; k++) flipped[i + 6 + k] = inner[i + 3 + k];
  }
  const both = new Float32Array(outer.length + flipped.length);
  both.set(outer, 0); both.set(flipped, outer.length);

  const seg = [];
  M.sliceAt(both, 5.0, seg);
  const area = Math.abs(segArea(seg));
  console.log('      plotas =', area.toFixed(6), 'mm2 (laukiama 100 - 16 = 84)');
  assert.equal(openEnds(seg), 0, 'abu kontūrai turi būti uždari');
  assert.ok(Math.abs(area - 84.0) < 0.001, `plotas ${area}, laukta 84 ±0.001`);
  console.log('      ', writeSVG(seg, join(OUT, 'cube-hole-z5.svg'), 'kubas su 4x4 skyle, Z=5'));
});

test('Rasterised layer area matches the geometry', () => {
  /* Antras, nepriklausomas kelias iki to paties skaičiaus: ne atkarpos, o
     tai, kas realiai keliaus į ekraną. Pilkas kraštinis pikselis įneša savo
     dalį, tad plotas turi sutapti pikselio tikslumu. */
  const cube = makeCube(10, 10, 10);
  const mask = M.layerMask(cube, 5.0, null);
  let sum = 0;
  for (let i = 0; i < mask.length; i++) sum += mask[i] / 255;
  const area = sum * M.PIXEL_MM * M.PIXEL_MM;
  console.log('      rastrizuotas plotas =', area.toFixed(4), 'mm2 (geometrinis 100)');
  assert.ok(Math.abs(area - 100.0) < 0.5, `rastro plotas ${area}, laukta 100 ±0.5`);
});
