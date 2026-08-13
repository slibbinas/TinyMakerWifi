/* slicer2 formulių sargai — kad interpretacijai neliktų vietos.
 *
 * Kiekvienas testas tikrina VIENĄ formulę prieš atsakymą, kurį žinome iš
 * geometrijos arba iš PrusaSlicer šaltinio, ir kiekvienas turi nuorodą į tą
 * šaltinio vietą. Jei kas nors „patobulins" formulę pagal supratimą, testas
 * kris — būtent taip buvo pagautas apverstas Möller–Trumbore ženklas ir
 * praleistas minusas zigzage (08-13).
 *
 * Taisyklė: nauja formulė = nauja šaltinio ištrauka komentare + naujas testas.
 *
 * Paleisti:  node scripts/dev/slicer2_invariants.mjs
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const HERE = dirname(fileURLToPath(import.meta.url));
const M = await import('file:///' +
  join(HERE, '..', '..', 'web', 'lib', 'slicer2.js').replace(/\\/g, '/') +
  '?t=' + process.pid);

/** Dėžė kaip trikampiai: viskas tikrinama ant figūros, kurios atsakymus
 *  galima suskaičiuoti ranka. */
function box(sx, sy, sz, z0, into = []) {
  const x0 = -sx / 2, x1 = sx / 2, y0 = -sy / 2, y1 = sy / 2, z1 = z0 + sz;
  const v = [[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],
             [x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]];
  const f = [[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],
             [1,2,6],[1,6,5],[2,3,7],[2,7,6],[3,0,4],[3,4,7]];
  for (const t of f) for (const i of t) into.push(...v[i]);
  return into;
}

/** Trikampių rinkinį pastumia XY plokštumoje (dėžė piešiama apie centrą). */
function shift(tris, dx, dy) {
  for (let i = 0; i < tris.length; i += 3) { tris[i] += dx; tris[i + 1] += dy; }
  return tris;
}

/** Plokštė su stačiakampe kiauryme — figūra, kurios sluoksnis turi kontūrą IR
 *  skylę. Būtent čia plokščias Clipper kelių sąrašas apgaudinėja even-odd
 *  patikrą, tad be šios figūros skylių taisymo patikrinti nėra kuo. */
function plateWithHole(outer, inner, z0, h, into = []) {
  const a = outer / 2, b = inner / 2, z1 = z0 + h;
  const O = [[-a, -a], [a, -a], [a, a], [-a, a]];
  const I = [[-b, -b], [b, -b], [b, b], [-b, b]];
  const tri = (p, q, r) => into.push(...p, ...q, ...r);
  for (let i = 0; i < 4; i++) {
    const j = (i + 1) % 4;
    const Ob = k => [O[k][0], O[k][1], z0], Ot = k => [O[k][0], O[k][1], z1];
    const Ib = k => [I[k][0], I[k][1], z0], It = k => [I[k][0], I[k][1], z1];
    // viršus (+Z) ir apačia (−Z): žiedas tarp kontūro ir skylės
    tri(Ot(i), Ot(j), It(j)); tri(Ot(i), It(j), It(i));
    tri(Ob(i), Ib(j), Ob(j)); tri(Ob(i), Ib(i), Ib(j));
    // išorinė siena — normalė laukan; vidinė — į skylę, tad priešinga
    tri(Ob(i), Ob(j), Ot(j)); tri(Ob(i), Ot(j), Ot(i));
    tri(Ib(i), It(j), Ib(j)); tri(Ib(i), It(i), It(j));
  }
  return into;
}


/** 2D profilis, ištęstas per `depth` — figūra, kurios pjūvis KEIČIASI su
 *  aukščiu. Dėžutės to neduoda, o būtent ties tokiais laiptais gyvena plokščios
 *  nuokabos. Profilis XZ plokštumoje, tąsa per Y. */
function extrude(profile, depth, into = []) {
  const n = profile.length, y0 = -depth / 2, y1 = depth / 2;
  const tri = (a, b, c) => into.push(...a, ...b, ...c);
  // šonai
  for (let i = 0; i < n; i++) {
    const [x0, z0] = profile[i], [x1, z1] = profile[(i + 1) % n];
    tri([x0, y0, z0], [x1, y0, z1], [x1, y1, z1]);
    tri([x0, y0, z0], [x1, y1, z1], [x0, y1, z0]);
  }
  // galai — vėduokle nuo pirmos viršūnės (profilis išgaubtas dalimis, o
  // pjaustymui svarbu tik uždarumas)
  for (let i = 1; i + 1 < n; i++) {
    const a = profile[0], b = profile[i], c = profile[i + 1];
    tri([a[0], y1, a[1]], [b[0], y1, b[1]], [c[0], y1, c[1]]);
    tri([a[0], y0, a[1]], [c[0], y0, c[1]], [b[0], y0, b[1]]);
  }
  return into;
}

/* --------------------------------------------------------- spinduliai */

test('beamHit: atstumas iki dėžės sutampa su geometrija', async () => {
  const pos = new Float32Array(box(8, 8, 3, 0));
  const t = await M.buildSupportTree(pos, M.CFG);
  const r = M.CFG.head_back_radius_mm;
  /* SupportTreeUtils.hpp:179 — spindulys šaunamas iš `p_src + r_src * raydir`,
     tad grąžinamas atstumas matuojamas NUO TEN, ne nuo src. Iš z=10 iki dėžės
     viršaus (z=3) yra 7 mm, minus r. */
  const down = M.beamHit(t.mesh, [0, 0, 10], [0, 0, -1], r, r);
  assert.ok(Math.abs(down - (7 - r)) < 0.05, `žemyn: ${down}, laukta ${7 - r}`);
  // Iš šono: nuo x=10 iki sienos x=4 yra 6 mm, minus r.
  const side = M.beamHit(t.mesh, [10, 0, 1.5], [-1, 0, 0], r, r);
  assert.ok(Math.abs(side - (6 - r)) < 0.05, `į šoną: ${side}, laukta ${6 - r}`);
});

test('beamHit: virš tuštumos nieko nekliūva', async () => {
  const pos = new Float32Array(box(4, 4, 3, 0));
  const t = await M.buildSupportTree(pos, M.CFG);
  const r = M.CFG.head_back_radius_mm;
  // Pro šalį, greta dėžės — turi būti begalybė, ne nulis.
  const free = M.beamHit(t.mesh, [15, 15, 10], [0, 0, -1], r, r);
  assert.ok(!(free < Infinity), `pro šalį turi būti INF, gauta ${free}`);
});

/* ------------------------------------------------------------ formulės */

test('zigzago žingsnis leidžiasi, ne kyla', () => {
  /* DefaultSupportTree.cpp:215-216:
       bridge_distance = pillar_dist / cos(-bridge_slope)
       zstep           = pillar_dist * tan(-bridge_slope)
     Abu su MINUSU. Praleidus jį jungtys kilo aukštyn ir kabojo ore. */
  const d = 4;
  const zstep = d * Math.tan(-M.CFG.bridge_slope);
  assert.ok(zstep < 0, `zstep turi būti neigiamas, gauta ${zstep}`);
  assert.ok(Math.abs(zstep + d) < 1e-9, '45° kampu nueita ir nusileista vienodai');
});

test('galvutė eina PAGAL normalę ir bent bridge_slope žemyn', () => {
  /* DefaultSupportTree.cpp:444,462: polar = max(polar, PI - bridge_slope),
     nn = spheric_to_dir(polar, azimuth). Kabančio paviršiaus normalė jau rodo
     žemyn — apvertus kryptį galvutės eina į modelį. */
  const maxDown = -Math.cos(M.CFG.bridge_slope);
  assert.ok(maxDown < 0, 'leistina kryptis turi būti žemyn');
  assert.ok(Math.abs(maxDown + Math.SQRT1_2) < 1e-9, '45° -> -0.7071');
});

test('galvutės skersmuo prie detalės = head_front_diameter', () => {
  /* Profilis: support_head_front_diameter = 0.5 -> spindulys 0.25. */
  const b = [{ ax: 0, ay: 0, z0: 0, bx: 0, by: 0, z1: 10, bridge: true }];
  const tip = M.braceDiscs(b, 10 - 1e-6)[0].r;
  assert.ok(Math.abs(tip - M.CFG.head_front_radius_mm) < 0.01,
    `smaigalys ${tip}, laukta ${M.CFG.head_front_radius_mm}`);
  // O kūnas — stulpo storio.
  const body = M.braceDiscs(b, 1)[0].r;
  assert.ok(Math.abs(body - M.CFG.pillar_radius_mm) < 0.01,
    `kūnas ${body}, laukta ${M.CFG.pillar_radius_mm}`);
});

test('pėda prie plokštės = support_base_diameter', () => {
  /* Profilis: support_base_diameter 3 -> spindulys 1.5, per base_height 1 mm. */
  const p = [{ x: 0, y: 0, top: 10, bottom: 0, rTop: M.CFG.pillar_radius_mm }];
  const foot = M.pillarDiscs(p, 0)[0].r;
  assert.ok(Math.abs(foot - M.CFG.base_radius_mm) < 0.01,
    `pėda ${foot}, laukta ${M.CFG.base_radius_mm}`);
  const shaft = M.pillarDiscs(p, M.CFG.base_height_mm + 0.5)[0].r;
  assert.ok(Math.abs(shaft - M.CFG.pillar_radius_mm) < 0.01,
    `kūnas ${shaft}, laukta ${M.CFG.pillar_radius_mm}`);
});

/* ------------------------------------------------------- invariantai */

test('nė vienas stulpas neprasideda ore', async () => {
  const pos = new Float32Array(box(10, 10, 4, 0, box(6, 6, 4, 8)));
  const t = await M.buildSupportTree(pos, M.CFG);
  /* Zonduojama iš `bottom + eps` — iš TUŠTUMOS virš atramos taško. Iš
     `bottom - 1e-3` spindulys startuoja medžiagos VIDUJE ir grąžina kūno
     storį, ne nulį: stulpas, stovintis tiksliai ant dėžės (0,001 mm nuo
     paviršiaus), atrodydavo kabantis 4 mm ore (išmatuota 08-13). */
  const eps = 1e-3, tol = eps + 0.05;               // vienas sluoksnis atsargos
  const bad = [];
  for (const p of t.pillars) {
    if (p.bottom <= 1e-6) continue;                 // ant plokštės — gerai
    if (p.partial) continue;                        // remiasi į tiltą, ne į medžiagą
    const hr = t.mesh.rayHit([p.x, p.y, p.bottom + eps], [0, 0, -1]);
    if (hr.inside || hr.dist <= tol) continue;      // įleista į medžiagą / guli ant jos
    bad.push({ x: +p.x.toFixed(2), y: +p.y.toFixed(2),
               bottom: +p.bottom.toFixed(2), gap: +hr.dist.toFixed(2) });
  }
  assert.equal(bad.length, 0, 'kabantys stulpai: ' + JSON.stringify(bad.slice(0, 5)));
  assert.ok(t.pillars.length > 0, 'testas be stulpų nieko netikrina');
});

test('zondas neatleidžia: pakeltas stulpas vis tiek kabantis', async () => {
  /* Sargas pačiam sargui. Jei zondo taisymas išsigimtų į „visada gerai",
     šis testas kris: dirbtinai pakeltas stulpas privalo likti pagautas. */
  const pos = new Float32Array(box(10, 10, 4, 0));
  const t = await M.buildSupportTree(pos, M.CFG);
  const onSurface = { x: 0, y: 0, top: 6, bottom: 4 };      // ant dėžės viršaus
  const inAir     = { x: 0, y: 0, top: 6, bottom: 4.5 };    // 0,5 mm virš jos
  const offEdge   = { x: 9, y: 9, top: 6, bottom: 4 };      // greta dėžės — nieko po juo
  assert.equal(M.selfCheck({ pillars: [onSurface], braces: [] }, t.mesh, M.CFG), 0);
  assert.equal(M.selfCheck({ pillars: [inAir], braces: [] }, t.mesh, M.CFG), 1);
  assert.equal(M.selfCheck({ pillars: [offEdge], braces: [] }, t.mesh, M.CFG), 1);
});

test('skylė nėra nuokaba — į ją nesėjama', async () => {
  /* SupportPointGenerator.cpp:415 — `diff_ex` grąžina ExPolygons, kurių vidus
     yra kontūras MINUS skylės; skylės sėjamos tik kaip KRAŠTAS (cpp:475-477).
     Plokščiame Clipper kelių sąraše skylė nuo kontūro neatskiriama, ir even-odd
     prieš vieną kelią rodo „viduje" būtent skylės viduje. */
  /* Sluoksniai skaičiuojami nuo z = 0, tad figūra turi siekti plokštę:
     stulpelis kampe (už kiaurymės ribų) laiko plokštelę z = 8..10. */
  const stem = shift(box(3, 3, 8, 0), 8, 8);
  const pos = new Float32Array([...stem, ...plateWithHole(20, 13, 8, 2)]);
  const t = await M.buildSupportTree(pos, M.CFG);
  const b = 13 / 2;
  const deep = 0.3;                        // kraštas — teisėtas, gilus vidus — ne
  const inHole = p => Math.abs(p[0]) < b - deep && Math.abs(p[1]) < b - deep;
  assert.ok(t.heads.length > 0, 'plokštė be atramų — testas nieko netikrina');
  const bad = t.heads.filter(h => inHole(h.pos))
                     .map(h => [+h.pos[0].toFixed(2), +h.pos[1].toFixed(2)]);
  assert.equal(bad.length, 0,
    `smaigaliai kiauryme: ${bad.length}/${t.heads.length} ` + JSON.stringify(bad.slice(0, 5)));
  // Ir stulpai neturi stovėti kiauryme (jie eina iki plokštės, bet iš niekur).
  const badP = t.pillars.filter(p => inHole([p.x, p.y]));
  assert.equal(badP.length, 0, `stulpai kiauryme: ${badP.length}/${t.pillars.length}`);
});


test('plokščia nuokaba paremiama VISAME plote, ne tik pakraštyje', async () => {
  /* `create_peninsulas` (SPG.cpp:567) + `support_peninsulas` (SPG.cpp:316).
     Vieno sluoksnio nuokaba, išsikišusi toliau nei `peninsula_min_width`, yra
     „pussalis" ir remiama kaip PLOTAS, ne kaip kraštas.

     Testas gimė iš tikro radinio: viskas buvo tikrinta ant dviejų organinių
     modelių, o pirmas kronsteinas parodė, kad po plokšte dedam 8 atramas ten,
     kur etalonas deda 12–20. Ant glotnaus kūno mechanizmas tyli, tad be tokios
     figūros jo dingimo niekas nepastebėtų. */
  const prof = [[-13, 0], [-9, 0], [-9, 12], [9, 12], [9, 0],
                [13, 0], [13, 15], [-13, 15]];      // „П": dvi kojos ir plokštė
  const pos = new Float32Array(extrude(prof, 10));
  const t = await M.buildSupportTree(pos, M.CFG);

  const under = t.heads.filter(h => Math.abs(h.pos[2] - 12) < 0.3);
  assert.ok(under.length > 0, 'po plokšte nėra nė vienos galvutės');

  /* Skiriamasis požymis — ne kiekis, o VIETA. Plokštės laisvi kraštai yra
     ties y = ±5; sėjant tik kontūrą visos galvutės ten ir sėdi (išmatuota
     išjungus pussalius: y reikšmės buvo lygiai −5 ir 5, viduryje nulis).
     Pussaliai užpildo plotą, tad vidurio juostoje atsiranda atramų.
     Slenkstis čia netiktų — jis lūžtų nuo bet kokio teisėto tankio pokyčio. */
  const middle = under.filter(h => Math.abs(h.pos[1]) <= 2);
  assert.ok(middle.length > 0,
    `visos ${under.length} galvutės ant kraštų (y: ` +
    [...new Set(under.map(h => +h.pos[1].toFixed(1)))].sort((a, b) => a - b).join(' ') +
    ') — nuokabos vidurys neparemtas');

  // Ir jos turi kur nusileisti: stulpai stovi ant plokštės, ne ant detalės.
  assert.ok(t.pillars.some(p => p.bottom <= 1e-6),
    'nė vienas stulpas nepasiekė plokštės');
});

test('sala virš kūno gauna atramos taškų', async () => {
  /* prev turi būti ANKSTESNIS sluoksnis, net tuščias — kitaip virš tuštumos
     atsiradusi sala nėra skirtumas ir lieka be nieko. */
  const pos = new Float32Array(box(8, 8, 3, 0, []));
  box(6, 6, 3, 6, []);
  const both = new Float32Array([...box(8, 8, 3, 0, []), ...box(6, 6, 3, 6, [])]);
  const t = await M.buildSupportTree(both, M.CFG);
  assert.ok(t.log.sampled > 0, 'sala turi duoti bent vieną tašką');
});

test('savikontrolė pagauna kabantį stulpą', () => {
  /* Jei ji nieko nepagauna, ji netikrina — o mums svarbu, kad tyliai nemeluotų. */
  const fakeMesh = { rayHit: () => ({ dist: Infinity, inside: false }) };
  const hanging = M.selfCheck(
    { pillars: [{ x: 0, y: 0, top: 5, bottom: 2 }], braces: [] }, fakeMesh, M.CFG);
  assert.equal(hanging, 1, 'stulpas be atramos turi būti suskaičiuotas');
});
