/* TinyMakerWiFi — SLA slicer, antras algoritmas.
 *
 * Rašomas pagal PrusaSlicer libslic3r ŠALTINIUS, ne pagal atmintį. Kiekvienas
 * etapas turi nuorodą į failą ir eilutę, iš kurios paimta logika:
 *
 *   src/libslic3r/SLA/DefaultSupportTree.hpp/.cpp   — etapų grandinė
 *   src/libslic3r/SLA/SupportTreeUtils.hpp          — spindulių pluoštas (Beam)
 *   src/libslic3r/SLA/SupportPointGenerator.hpp     — kur sėjami taškai
 *
 * Skiriasi nuo `slicer.js` iš esmės: ten supportai augo iš rastrinių euristikų,
 * čia — iš tos pačios etapų grandinės, kurią vykdo PrusaSlicer, su tikra
 * kolizijų patikra spinduliais. Abu moduliai gyvena greta ir stende
 * perjungiami, kad tą patį modelį būtų galima pamatyti abiem (V 08-13).
 *
 * Bendra infrastruktūra (STL skaitymas, orientavimas, pjaustymas, rastrizacija,
 * ZIP) imama iš `slicer.js` — tai ne algoritmas, o įrankiai; dubliuoti juos
 * reikštų dvi vietas tai pačiai klaidai.
 */
import {
  PLATE, PIXEL_MM, RES, LAYER_MM, SUP,
  parseSTL, bounds, surfaceArea, detailBudget, makeTransform, place, fitCheck,
  autoOrient, toSceneMesh, sliceAt, layerMask, zipStore,
  pillarDiscs, braceDiscs, supportMesh,
  slice as sliceBase,
} from './slicer.js';

export {
  PLATE, PIXEL_MM, RES, LAYER_MM, SUP,
  parseSTL, bounds, surfaceArea, detailBudget, makeTransform, place, fitCheck,
  autoOrient, toSceneMesh, sliceAt, layerMask, zipStore,
  supportMesh,
};
export { pillarDiscs2 as pillarDiscs, braceDiscs2 as braceDiscs };

export const VERSION = '2.0.1-dev';

/* ------------------------------------------------------------------ config */
/* Vardai palikti tokie patys kaip PrusaSlicer'io nustatymuose, kad būtų
   matyti, iš kur kiekvienas skaičius. Reikšmės — iš V profilio
   (TinyMaker + „Universal 0.05 - Light Supports"), ne iš numatytųjų. */
export const CFG = {
  head_front_radius_mm: 0.25,   // support_head_front_diameter 0.5
  head_back_radius_mm:  0.5,    // support_pillar_diameter 1
  head_fallback_radius_mm: 0.3, // 60 % — support_small_pillar_diameter_percent
  head_penetration_mm:  0.3,    // support_head_penetration
  head_width_mm:        3.0,    // support_head_width
  pillar_radius_mm:     0.5,    // support_pillar_diameter 1
  base_radius_mm:       1.5,    // support_base_diameter 3
  base_height_mm:       1.0,    // support_base_height
  /* SupportTree.hpp:110 — KOMPILIAVIMO METO konstanta 0.5, NE profilio
     `support_base_safety_distance`. Tas profilio skaičius yra stulpo PĖDOS
     atstumas nuo detalės (`pillar_base_safety_distance_mm`), visai kitas
     dalykas. Paėmus 1.0 galvutės žiedas išeina 1,67× per platus. */
  safety_distance_mm:   0.5,
  pillar_base_safety_distance_mm: 1.0,  // support_base_safety_distance
  max_bridge_length_mm: 10.0,   // support_max_bridge_length
  max_pillar_link_distance_mm: 10.0,  // support_max_pillar_link_distance
  max_bridges_on_pillar: 3,     // support_max_bridges_on_pillar
  bridge_slope:         Math.PI / 4,  // 45°, kaip jo numatytasis
  normal_cutoff_angle:  150 * Math.PI / 180,  // SupportTree.hpp:105 — 150°, ne 90°
  removing_delta_mm:    5.0,    // SampleConfig.hpp:31
  ground_facing_only:   false,  // support_buildplate_only = 0
  object_elevation_mm:  0,      // pad_around_object = 1 -> nekeliam
  /* Taškų sėja. PrusaSlicer'io density_relative = 100 %; mūsų pikselis
     0.1275 mm, tad tankį išreiškiam atstumu tarp taškų. */
  support_points_density: 1.0,     // support_points_density_relative 100 %
  /* Nuokabos krašto diskretizavimo žingsnis — `discretize_overhang_step`
     (SampleConfig.hpp:18). Tai NE tankis: tankį lemia įtakos spindulys žemiau. */
  discretize_overhang_step_mm: 2.0,
  /* `create_default_support_curve()` (SupportPointGenerator.cpp:1453).
     [atstumas sluoksnyje XY, aukščio skirtumas Z] milimetrais. Ką tik pastatyta
     atrama „dengia" 3,2 mm spindulį, o kylant aukštyn tas spindulys AUGA iki
     6 mm ties 40 mm. Būtent tai, o ne pastovus žingsnis, ir valdo tankį —
     todėl ant glotnaus kūno atramos retėja, o ant šviežios nuokabos tankėja.
     Senasis pastovus 3 mm tinklelis niekada neaugo, ir dėl to narvas išeidavo
     2–3× tankesnis nei PrusaSlicer'io (išmatuota 08-12). */
  support_curve: [[3.2, 0], [4.0, 3.9], [5.0, 15.0], [6.0, 40.0]],
  /* SampleConfig.hpp:47-58 — salų sėjos atstumai: kontūras 5*3/4, vidus 5,
     plonos dalies nugarkaulis 5. */
  island_outline_step_mm: 3.75,
  island_inner_step_mm:   5.0,
  island_thin_step_mm:    5.0,
  /* SampleConfig.hpp:20-24 — vieno sluoksnio nuokaba tampa „pussaliu", jei
     išsikiša toliau nei `peninsula_min_width`; kas arčiau nei
     `peninsula_self_supported_width` — laikosi pati. */
  peninsula_min_width_mm: 2.0,
  peninsula_self_supported_width_mm: 1.5,
  /* `minimal_bounding_sphere_radius` (SampleConfig.hpp:35): mažesnės dalys
     išmetamos dar prieš sėją — jų neįmanoma atspausdinti kitaip nei rutuliuku
     nuo galvutės. */
  minimal_part_radius_mm: 0.2,
  critical_angle:        Math.PI / 4,  // support_critical_angle 45
  /* Klasteriai: du taškai jungiasi į vieną stulpą, jei XY atstumas mažesnis
     nei 2 × base_radius IR 3D atstumas mažesnis nei max_bridge_length
     (DefaultSupportTree.cpp:565-571). */
  cluster_size:          3,     // = max_bridges_on_pillar
  pillar_cascade_neighbors: 3,  // kiek kaimynų vienas stulpas jungia
  pillar_connection_mode: 'zigzag',   // support_pillar_connection_mode
  /* SLA/Pad.hpp + V profilis: pad_wall_height 0, pad_wall_thickness 0.15,
     pad_brim_size 1.6. full_height = wall_height + wall_thickness. */
  pad_thickness_mm:      0.15,
  pad_brim_mm:           1.6,
  pad_layers:            3,     // 0.15 mm / 0.05
};

const DOWN = [0, 0, -1];
const INF = Infinity;

/* ------------------------------------------------------------ vektoriukai */
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1],
                         a[2] * b[0] - a[0] * b[2],
                         a[0] * b[1] - a[1] * b[0]];
const len = a => Math.hypot(a[0], a[1], a[2]);
const norm = a => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
const dist2d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const dist3d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);

/* ------------------------------------------------------- erdvinis indeksas */
/* AABBMesh atitikmuo: tinklelis XY plokštumoje, kiekviename langelyje —
   trikampių, kurių gabaritas jį kerta, sąrašas. Spindulys eina per langelius
   ir tikrina tik juos. Tikras BVH būtų greitesnis, bet čia užtenka: modelis
   telpa į 40×30 mm, o langelių tinklelis jį suskaido į šimtus dalių. */
class MeshIndex {
  constructor(pos, cell = 2.0) {
    this.pos = pos;
    this.cell = cell;
    this.map = new Map();
    const b = bounds(pos);
    this.min = b.min;
    this.nx = Math.max(1, Math.ceil(b.size[0] / cell) + 1);
    this.ny = Math.max(1, Math.ceil(b.size[1] / cell) + 1);
    for (let t = 0; t + 8 < pos.length; t += 9) {
      const x0 = Math.min(pos[t], pos[t + 3], pos[t + 6]);
      const x1 = Math.max(pos[t], pos[t + 3], pos[t + 6]);
      const y0 = Math.min(pos[t + 1], pos[t + 4], pos[t + 7]);
      const y1 = Math.max(pos[t + 1], pos[t + 4], pos[t + 7]);
      const i0 = this.cx(x0), i1 = this.cx(x1);
      const j0 = this.cy(y0), j1 = this.cy(y1);
      for (let j = j0; j <= j1; j++)
        for (let i = i0; i <= i1; i++) {
          const k = j * this.nx + i;
          let l = this.map.get(k);
          if (!l) { l = []; this.map.set(k, l); }
          l.push(t);
        }
    }
  }
  cx(x) { return Math.max(0, Math.min(this.nx - 1, Math.floor((x - this.min[0]) / this.cell))); }
  cy(y) { return Math.max(0, Math.min(this.ny - 1, Math.floor((y - this.min[1]) / this.cell))); }

  /** Möller–Trumbore. Grąžina atstumą iki trikampio arba Infinity.
   *  `inside` sako, ar pataikyta į paviršių iš vidaus (normalė su spinduliu
   *  vienakryptė) — libslic3r tuo remiasi permesdamas spindulį (Beam logika). */
  triHit(t, o, d) {
    const p = this.pos;
    const ax = p[t], ay = p[t + 1], az = p[t + 2];
    const e1 = [p[t + 3] - ax, p[t + 4] - ay, p[t + 5] - az];
    const e2 = [p[t + 6] - ax, p[t + 7] - ay, p[t + 8] - az];
    const h = cross(d, e2);
    const a = dot(e1, h);
    if (Math.abs(a) < 1e-12) return null;
    const f = 1 / a;
    const s = [o[0] - ax, o[1] - ay, o[2] - az];
    const u = f * dot(s, h);
    if (u < 0 || u > 1) return null;
    const q = cross(s, e1);
    const v = f * dot(d, q);
    if (v < 0 || u + v > 1) return null;
    const dist = f * dot(e2, q);
    if (dist <= 1e-9) return null;
    /* a = -dot(spindulys, normalė): a > 0 reiškia pataikymą iš IŠORĖS,
       tad „iš vidaus" yra a < 0. Apverstas ženklas griovė visą kolizijų
       patikrą — beamHit grąžindavo 0 visur, kur kliūtis toliau nei 2r+sd,
       ir tiltai buvo atmetami be priežasties (auditas 08-13). */
    return { dist, inside: a < 0 };
  }

  /** Vieno spindulio metimas — AABBMesh::query_ray_hit atitikmuo. */
  rayHit(o, d) {
    let best = { dist: INF, inside: false };
    /* Einam per XY langelius palei spindulį; žingsnis — pusė langelio.
       Pakartotinius langelius atmetam lygindami su ankstesniu, ne per Set:
       spindulys eina tiesiai, tad kartojasi tik gretimi žingsniai, o Set
       kiekvienam spinduliui kainavo daugiau nei pats tikrinimas. */
    /* Tikras DDA: einam per KIEKVIENĄ langelį, kurį spindulys kerta. Žingsnis
       „pusė langelio" praleisdavo langelius įstrižai einantiems spinduliams —
       0,5 % atsakymų buvo per toli, t. y. „laisva" ten, kur laisva nebuvo
       (auditas 08-13, palyginta su brute force). */
    let ix = this.cx(o[0]), iy = this.cy(o[1]);
    const stepX = d[0] > 0 ? 1 : -1, stepY = d[1] > 0 ? 1 : -1;
    const c = this.cell;
    const bx = this.min[0] + (ix + (d[0] > 0 ? 1 : 0)) * c;
    const by = this.min[1] + (iy + (d[1] > 0 ? 1 : 0)) * c;
    let tMaxX = Math.abs(d[0]) > 1e-12 ? (bx - o[0]) / d[0] : INF;
    let tMaxY = Math.abs(d[1]) > 1e-12 ? (by - o[1]) / d[1] : INF;
    const tDeltaX = Math.abs(d[0]) > 1e-12 ? c / Math.abs(d[0]) : INF;
    const tDeltaY = Math.abs(d[1]) > 1e-12 ? c / Math.abs(d[1]) : INF;
    let travelled = 0;
    const far = 200;                       // toliau nei plokštės įstrižainė
    for (let guard = 0; guard < 4096; guard++) {
      const list = this.map.get(iy * this.nx + ix);
      if (list)
        for (const t of list) {
          const h = this.triHit(t, o, d);
          if (h && h.dist < best.dist) best = h;
        }
      // Toliau eiti nėra ko, kai pataikymas arčiau nei jau nueitas kelias.
      if (best.dist < travelled) break;
      if (tMaxX < tMaxY) { travelled = tMaxX; ix += stepX; tMaxX += tDeltaX; }
      else               { travelled = tMaxY; iy += stepY; tMaxY += tDeltaY; }
      if (travelled > far) break;
      if (ix < 0 || ix >= this.nx || iy < 0 || iy >= this.ny) {
        // Išėjus iš tinklelio vertikalus spindulys vis tiek lieka savo langelyje.
        if (tDeltaX === INF && tDeltaY === INF) break;
        if (travelled > far) break;
        if (ix < -1 || ix > this.nx || iy < -1 || iy > this.ny) break;
      }
    }
    return best;
  }
}

/* --------------------------------------------------------- spindulių žiedas */
/* PointRing (SupportTreeUtils.hpp): aštuoni taškai ant apskritimo, statmeno
   krypčiai. Sukam bet kurį statmeną vektorių apie ašį. */
function ringBasis(dir) {
  const d = norm(dir);
  const helper = Math.abs(d[2]) < 0.9 ? [0, 0, 1] : [1, 0, 0];
  const a = norm(cross(d, helper));
  const b = norm(cross(d, a));
  return [a, b];
}
function ringPoint(centre, a, b, r, i, n) {
  const t = 2 * Math.PI * i / n;
  return add(centre, add(mul(a, r * Math.cos(t)), mul(b, r * Math.sin(t))));
}

const BEAM_SAMPLES = 8;   // SupportTreeUtils.hpp: Beam_<Samples = 8>

/** beam_mesh_hit (SupportTreeUtils.hpp:150-194): aštuoni spinduliai palei kūgio
 *  paviršių, rezultatas — mažiausias pataikymas. `sd` yra saugos atstumas. */
/** Kaip `beamHit`, tik grąžina ir pataikymo TAŠKĄ. Jo reikia apverstai
 *  galvutei: kai ašis ir pluoštas nesutampa, originalas remiasi į pluošto
 *  pataikymo vietą, o ji yra šalia ašies (SupportTreeUtils.hpp: hit.position()
 *  = spindulio pradžia + kryptis × atstumas). */
export function beamHitFull(mesh, src, dir, r1, r2, sd = 0) {
  const d = norm(dir);
  const dst = add(src, d);
  const [a, b] = ringBasis(d);
  let best = INF, bestPos = null;
  for (let i = 0; i < BEAM_SAMPLES; i++) {
    const ps = ringPoint(src, a, b, r1 + sd, i, BEAM_SAMPLES);
    const pd = ringPoint(dst, a, b, r2 + sd, i, BEAM_SAMPLES);
    const rd = norm(sub(pd, ps));
    let hr = mesh.rayHit(add(ps, mul(rd, r1)), rd);
    if (hr.inside && hr.dist < INF) {
      // Pataikyta iš vidaus — permetam iš išorės, kaip daro originalas.
      if (hr.dist > 2 * r1 + sd) { best = 0; bestPos = null; continue; }
      /* hr.dist matuojamas nuo TAŠKO, iš kurio šauta (ps + rd*r1), tad
         permetant reikia to paties poslinkio — kitaip naujas spindulys
         atsiduria prieš paviršių ir pataiko į jį patį (rezultatas visada
         išeidavo ≈ r1). */
      const q = add(ps, mul(rd, r1 + hr.dist + 1e-6));
      hr = mesh.rayHit(q, rd);
    }
    if (hr.dist < best) {
      best = hr.dist;
      bestPos = Number.isFinite(hr.dist)
        ? add(add(ps, mul(rd, r1)), mul(rd, hr.dist)) : null;
    }
  }
  return { dist: best, pos: bestPos };
}

/** Tik atstumas — taip jis naudojamas beveik visur. */
export function beamHit(mesh, src, dir, r1, r2, sd = 0) {
  return beamHitFull(mesh, src, dir, r1, r2, sd).dist;
}

/** Saugos atstumai. `safety_distance(r)` — galvutei (SupportTree.hpp:95),
 *  `bridgeSafety(r)` — tiltams ir `classify` (DefaultSupportTree.hpp:165).
 *  Abu perkrovimai patys skaičiuoja atstumą; perdavus nulį tikrinama be jokios
 *  atsargos, o tai ne tas pats. */
export function safetyDistance(r, cfg = CFG) {
  return Math.min(cfg.safety_distance_mm,
                  r * cfg.safety_distance_mm / cfg.head_back_radius_mm);
}
export function bridgeSafety(r, cfg = CFG) {
  return r * cfg.safety_distance_mm / cfg.head_back_radius_mm;
}

const PINHEAD_SAMPLES = 16;   // „8 is almost ok … 16 is necessary"

/** `pinhead_mesh_hit` (SupportTreeUtils.hpp:196-280).
 *
 *  Tai NE `beam_mesh_hit` su kitais parametrais, nors ilgai buvo taip parašyta.
 *  Savas kūnas: 16 spindulių; smaigalio žiedas ties PAČIU tašku spinduliu
 *  `rPin + sd`, nugarėlės — ties `s + (rPin + width + rBack) * dir` spinduliu
 *  `rBack + sd`; spindulys leidžiamas iš `ps + sd * n`, pasistūmėjus nuo
 *  lietimosi taško; „iš vidaus" riba yra `rPin + sd`, o permetama su
 *  `dist + 2*sd` poslinkiu. */
export function pinheadHit(mesh, s, dir, rPin, rBack, width, sd = 0) {
  const d = norm(dir);
  const spin = s;
  const sback = add(s, mul(d, rPin + width + rBack));
  const rpin = rPin + sd, rback = rBack + sd;
  const [a, b] = ringBasis(d);
  let best = INF;
  for (let i = 0; i < PINHEAD_SAMPLES; i++) {
    const ps = ringPoint(spin, a, b, rpin, i, PINHEAD_SAMPLES);
    const pd = ringPoint(sback, a, b, rback, i, PINHEAD_SAMPLES);
    const n = norm(sub(pd, ps));
    let hr = mesh.rayHit(add(ps, mul(n, sd)), n);
    if (hr.inside && hr.dist < INF) {
      if (hr.dist > rpin) { best = 0; continue; }
      hr = mesh.rayHit(add(ps, mul(n, hr.dist + 2 * sd)), n);
    }
    if (hr.dist < best) best = hr.dist;
  }
  return best;
}

/* ------------------------------------------------------------ taškų sėja */
/* SupportPointGenerator: taškai sėjami ant to, kas kabo. Kampo riba —
   support_critical_angle (45°). Retinimas — vienodu atstumu, kad taškai
   nesusigrūstų (originale Poisson tipo atranka ant kontūro). */
export function samplePoints(pos, cfg = CFG) {
  const cosLimit = -Math.cos(cfg.critical_angle);
  const step = cfg.point_spacing_mm / Math.max(0.01, cfg.support_points_density);
  const cells = new Map();
  const out = [];
  const put = (x, y, z, n) => {
    if (z < cfg.base_height_mm) return;     // prie pat plokštės laikosi pats
    const k = [Math.floor(x / step), Math.floor(y / step), Math.floor(z / step)].join(',');
    if (cells.has(k)) return;
    cells.set(k, 1);
    out.push({ pos: [x, y, z], normal: n });
  };
  for (let t = 0; t + 8 < pos.length; t += 9) {
    const ax = pos[t], ay = pos[t + 1], az = pos[t + 2];
    const u = [pos[t + 3] - ax, pos[t + 4] - ay, pos[t + 5] - az];
    const v = [pos[t + 6] - ax, pos[t + 7] - ay, pos[t + 8] - az];
    const n = cross(u, v);
    const nl = len(n);
    if (nl < 1e-12) continue;
    const nz = n[2] / nl;
    if (nz >= cosLimit) continue;           // kabo per mažai
    const nn = [n[0] / nl, n[1] / nl, nz];
    put(ax + (u[0] + v[0]) / 3, ay + (u[1] + v[1]) / 3, az + (u[2] + v[2]) / 3, nn);
    const nu = Math.min(24, Math.floor(len(u) / step));
    const nv = Math.min(24, Math.floor(len(v) / step));
    for (let iu = 0; iu <= nu; iu++)
      for (let iv = 0; iv <= nv; iv++) {
        if (!nu && !nv) break;
        const cu = nu ? iu / nu : 0, cv = nv ? iv / nv : 0;
        if (cu + cv > 1) continue;
        put(ax + u[0] * cu + v[0] * cv, ay + u[1] * cu + v[1] * cv,
            az + u[2] * cu + v[2] * cv, nn);
      }
  }
  return out;
}

/* ------------------------------------------------------ klasterizacija */
/* DefaultSupportTree.cpp:565-574. Du taškai vienam stulpui, jei XY atstumas
   mažesnis nei 2 × base_radius IR 3D atstumas mažesnis nei max_bridge_length.
   Klasterio dydis ribojamas max_bridges_on_pillar. */
function clusterHeads(heads, cfg) {
  const used = new Array(heads.length).fill(false);
  const out = [];
  for (let i = 0; i < heads.length; i++) {
    if (used[i]) continue;
    const cl = [i]; used[i] = true;
    for (let j = i + 1; j < heads.length && cl.length <= cfg.cluster_size; j++) {
      if (used[j]) continue;
      const a = heads[i].junction, b = heads[j].junction;
      if (dist2d(a, b) < 2 * cfg.base_radius_mm &&
          dist3d(a, b) < cfg.max_bridge_length_mm) { cl.push(j); used[j] = true; }
    }
    out.push(cl);
  }
  return out;
}

/** Klasterio centras pagal XY atstumą — cluster_centroid. */
function centroidOf(cl, heads) {
  let best = cl[0], bestSum = INF;
  for (const a of cl) {
    let s = 0;
    for (const b of cl) s += dist2d(heads[a].junction, heads[b].junction);
    if (s < bestSum) { bestSum = s; best = a; }
  }
  return best;
}

/* ------------------------------------------------------------- grandinė */
/** Etapai tokia pat tvarka, kaip DefaultSupportTree::execute():
 *  add_pinheads -> classify -> routing_to_ground -> routing_to_model ->
 *  interconnect_pillars -> merge_result. */
export async function buildSupportTree(pos, cfg = CFG, onProgress) {
  /* Etapų laikai — kad optimizuotume tai, kas iš tikrųjų lėta, o ne tai, kas
     atrodo lėta (pirmas spėjimas buvo krypties paieška, o kainavo visai kas
     kita). */
  const clock = typeof performance !== 'undefined' ? () => performance.now()
                                                   : () => Number(process.hrtime.bigint() / 1000000n);
  let t0 = clock();
  const lap = k => { const t = clock(); log.ms[k] = Math.round(t - t0); t0 = t; };

  const log = { ms: {} };
  const mesh = new MeshIndex(pos);
  lap('index');
  /* Taškai iš SLUOKSNIŲ, kaip SupportPointGenerator; sena sėja pagal mesh
     veidų kampą lieka faile palyginimui, bet grandinėje nebenaudojama. */
  const pts = await samplePointsFromLayers(pos, cfg, onProgress);
  log.sampled = pts.length;
  lap('sample');

  /* --- 1 · add_pinheads (DefaultSupportTree.cpp:385) --------------------- */
  const heads = [];
  for (const p of pts) {
    /* Galvutė eina PAGAL paviršiaus normalę (originale nn = prisotinta
       normalė, DefaultSupportTree.cpp:462), o kabančio paviršiaus normalė jau
       rodo žemyn. Prisotinimas: polar = max(polar, PI - bridge_slope) reiškia,
       kad kryptis turi būti bent bridge_slope žemiau horizontalės. */
    let dir = norm(p.normal);
    const maxDown = -Math.cos(cfg.bridge_slope);   // -0.707 prie 45°
    if (dir[2] > maxDown) {
      // per mažas polinkis žemyn — pakreipiam iki leistinos ribos
      const h = Math.hypot(dir[0], dir[1]) || 1;
      const s = Math.sqrt(Math.max(0, 1 - maxDown * maxDown));
      dir = [dir[0] / h * s, dir[1] / h * s, maxDown];
    }
    let rBack = cfg.head_back_radius_mm;
    let width = cfg.head_width_mm;
    /* Laisvo kelio reikalavimas w (DefaultSupportTree.cpp:449-456):
         lmin = head_width; if (back_r < head_back_radius) { lmin = 0; }
         w = lmin + 2*back_r + 2*head_front_radius - penetration
       Prie plonos galvutės lmin krenta į NULĮ, tad w = 0,80 mm vietoj 4,20 —
       būtent tam ji ir yra, ankštoms vietoms. */
    const lmin = r => (r < cfg.head_back_radius_mm ? 0 : cfg.head_width_mm);
    const need = r => lmin(r) + 2 * r + 2 * cfg.head_front_radius_mm - cfg.head_penetration_mm;
    // Galvutė statoma nuo paviršiaus taško kryptimi dir.
    let hit = pinheadHit(mesh, p.pos, dir, cfg.head_front_radius_mm, rBack,
                         need(rBack), safetyDistance(rBack, cfg));
    /* Nepavykus originalas NEmeta taško, o ieško kitos krypties, kuri
       nesikirstų su modeliu ir būtų kuo arčiau normalės
       (DefaultSupportTree.cpp:467-499; ten tam naudojamas NLopt genetinis
       optimizatorius). Tikslo funkcija ir rėžiai čia tie patys —
       maksimizuojamas pinhead_mesh_hit atstumas, polar leidžiamas nuo
       PI-bridge_slope iki PI, azimutas visas ratas. Skiriasi tik paieškos
       būdas: tvarkinga tinklelio apžvalga vietoj genetinės, nes ji
       determinuota ir nereikalauja bibliotekos (V: įrankis gali skirtis,
       matematika ne). */
    if (hit < need(rBack)) {
      const base = Math.atan2(dir[1], dir[0]);
      let bestDir = dir, bestHit = hit;
      const want = need(rBack);
      /* Kandidatai atrenkami VIENU spinduliu palei ašį — aštuonių spindulių
         pluoštas kiekvienai iš 48 krypčių suėsdavo 32 s iš 39 (išmatuota
         etapų laikmačiais 08-13). Laimėtojas patikrinamas pilnu pluoštu, tad
         priimamas atsakymas lieka toks pat griežtas. */
      const probes = [];
      for (let a = 0; a < 12; a++) {
        const az = base + (a / 12) * 2 * Math.PI;
        for (let k = 0; k <= 3; k++) {
          const polar = Math.PI - (k / 3) * cfg.bridge_slope;
          const st = Math.sin(polar), ct = Math.cos(polar);
          const d2 = [st * Math.cos(az), st * Math.sin(az), ct];
          const h2 = mesh.rayHit(add(p.pos, mul(d2, cfg.head_front_radius_mm)), d2).dist;
          probes.push({ d: d2, h: h2 });
        }
      }
      /* Vienas spindulys tik ATRENKA; sprendžia pilnas pluoštas. Tikrinam tris
         geriausius, nes pirmasis dažnai krenta pluošte — tikrinant tik jį
         galvučių likdavo 189 vietoj 373, o tikrinant visus 48 pilnai
         skaičiavimas užtrukdavo 39 s vietoj 7. */
      probes.sort((x, y) => y.h - x.h);
      for (const pr of probes.slice(0, 3)) {
        const full = pinheadHit(mesh, p.pos, pr.d, cfg.head_front_radius_mm, rBack,
                                want, safetyDistance(rBack, cfg));
        if (full > bestHit) { bestHit = full; bestDir = pr.d; }
        if (bestHit > want) break;
      }
      dir = bestDir; hit = bestHit;
    }
    if (hit < need(rBack) && rBack > cfg.head_fallback_radius_mm) {
      rBack = cfg.head_fallback_radius_mm;
      width = lmin(rBack);                         // plona galvutė gali būti 0 ilgio
      hit = pinheadHit(mesh, p.pos, dir, cfg.head_front_radius_mm, rBack,
                       need(rBack), safetyDistance(rBack, cfg));
    }
    if (!(hit > need(rBack))) continue;            // netelpa — taško atsisakom
    const junction = add(p.pos, mul(dir, width));
    if (junction[2] < cfg.base_height_mm) continue;
    heads.push({ pos: p.pos, dir, rBack, width, junction, pillar: -1, onModel: false });
  }
  log.heads = heads.length;
  lap('pinheads');
  /* Pultas laukia (padaryta, iš viso) — vardinis etapas jam duodavo NaN%. */
  if (onProgress) onProgress(pts.length, pts.length);

  /* --- 2 · classify (DefaultSupportTree.cpp:528) ------------------------- */
  const ground = [], onModel = [];
  for (let i = 0; i < heads.length; i++) {
    const h = heads[i];
    /* Spindulys ŽEMYN nuo jungties taško: jei niekas nekliūva — pilnas stulpas.
       Originale (DefaultSupportTree.cpp:547) čia saugos atstumas NEperduodamas —
       klausiama tik „ar kelias laisvas", ne „ar laisvas su atsarga". Pridėjus jį
       pluoštas užkabindavo pačią detalę ir 80 iš 83 taškų klaidingai virsdavo
       atramomis ant modelio. */
    /* `bridge_mesh_intersect(headjp, DOWN, r)` (cpp:547) yra 3 argumentų
       perkrova, kuri saugos atstumą PASISKAIČIUOJA pati — tai ne „be atsargos". */
    const scan = beamHitFull(mesh, h.junction, DOWN, h.rBack, h.rBack,
                             bridgeSafety(h.rBack, cfg));
    if (!(scan.dist < INF)) ground.push(i);
    else if (cfg.ground_facing_only) continue;
    else {
      h.onModel = true;
      h.groundHit = scan.dist;
      h.groundHitPos = scan.pos;      // `m_head_to_ground_scans[i] = hit`
      onModel.push(i);
    }
  }
  const clusters = clusterHeads(ground.map(i => heads[i]), cfg)
    .map(cl => cl.map(k => ground[k]));
  log.ground = ground.length;
  log.onModel = onModel.length;
  log.clusters = clusters.length;
  lap('classify');

  /* --- 3 · routing_to_ground (DefaultSupportTree.cpp:577) ---------------- */
  const pillars = [], bridges = [];
  const addPillar = (h, id) => {
    const p = { x: h.junction[0], y: h.junction[1],
                top: h.junction[2], bottom: 0, rTop: h.rBack, rBase: cfg.base_radius_mm,
                head: id, bridges: 0 };
    pillars.push(p);
    h.pillar = pillars.length - 1;
    return pillars.length - 1;
  };
  /** connect_to_nearpillar (DefaultSupportTree.cpp:282-363), eilutė po eilutės.
   *  Grąžina true, jei galvutė prikabinta prie nurodyto stulpo tiltu. */
  const connectToNearpillar = (h, pid) => {
    const pil = pillars[pid];
    if (pil.bridges > cfg.max_bridges_on_pillar) return false;
    const headjp = h.junction;
    const nearU = [pil.x, pil.y, pil.top];      // startpoint
    const nearL = [pil.x, pil.y, pil.bottom];   // endpoint
    const r = h.rBack;
    const d2d = dist2d(headjp, nearU);
    const d3d = dist3d(headjp, nearU);
    const hdiff = nearU[2] - headjp[2];
    const slope = Math.atan2(hdiff, d2d);
    let bridgestart = headjp.slice();
    let bridgeend = nearU.slice();
    const maxLen = r * cfg.max_bridge_length_mm / cfg.head_back_radius_mm;
    const maxSlope = cfg.bridge_slope;
    let zdiff = 0;

    if (d3d > maxLen || slope > -maxSlope) {
      // Tiesiai į stulpo viršūnę netinka — ieškom prisilietimo taško žemiau.
      let Zdown = headjp[2] + d2d * Math.tan(-maxSlope);
      const touch = [nearU[0], nearU[1], Zdown];
      const D = dist3d(headjp, touch);
      zdiff = Zdown - nearU[2];
      if (zdiff > 0) {
        Zdown -= zdiff;
        bridgestart[2] -= zdiff;
        // Po galvute reikia dalinio stulpelio — patikrinam, ar ten laisva.
        if (beamHit(mesh, headjp, DOWN, r, r, bridgeSafety(r, cfg)) < zdiff) return false;
      }
      if (Zdown <= nearU[2] && Zdown >= nearL[2] && D < maxLen) bridgeend[2] = Zdown;
      else return false;
    }
    // Empirinė riba: prie pat plokštės tiltas nekabinamas.
    if (bridgeend[2] < 4 * cfg.head_back_radius_mm) return false;
    const need = dist3d(bridgestart, bridgeend);
    if (beamHit(mesh, bridgestart, norm(sub(bridgeend, bridgestart)), r, r,
                bridgeSafety(r, cfg)) < need)
      return false;
    if (pil.bridges >= cfg.max_bridges_on_pillar) return false;
    if (zdiff > 0) {
      // Dalinis stulpelis po galvute + tiltas nuo jo.
      pillars.push({ x: headjp[0], y: headjp[1], top: headjp[2],
                     bottom: bridgestart[2], rTop: r, rBase: r,
                     head: h.id, bridges: 0, partial: true });
    }
    bridges.push({ a: bridgestart.slice(), b: bridgeend.slice(), r, head: true });
    pil.bridges++;
    h.pillar = pid;
    return true;
  };

  /** search_pillar_and_connect (cpp:723): artimiausias stulpas pagal XY; jei
   *  prikabinti nepavyko, jis išbraukiamas ir ieškoma toliau. */
  const searchPillarAndConnect = h => {
    const tried = new Set();
    for (;;) {
      let best = -1, bestD = INF;
      for (let k = 0; k < pillars.length; k++) {
        if (tried.has(k) || pillars[k].partial) continue;
        const d = Math.hypot(pillars[k].x - h.junction[0], pillars[k].y - h.junction[1]);
        if (d < bestD) { bestD = d; best = k; }
      }
      if (best < 0) return false;
      if (connectToNearpillar(h, best)) return true;
      tried.add(best);
    }
  };

  for (const cl of clusters) {
    const cIdx = centroidOf(cl, heads);
    addPillar(heads[cIdx], cIdx);
    for (const i of cl) {
      if (i === cIdx) continue;
      const h = heads[i];
      h.id = i;
      /* Originalo tvarka (cpp:639-644): centrinis stulpas -> bet kuris kitas
         -> savo stulpas. */
      if (connectToNearpillar(h, heads[cIdx].pillar)) continue;
      if (searchPillarAndConnect(h)) continue;
      addPillar(h, i);
    }
  }

  /** `connect_to_ground` -> `deepsearch_ground_connection`
   *  (SupportTreeUtils.hpp:600-700). TRŪKSTAMA VIDURINĖ PAKOPA: originale
   *  `routing_to_model` bando stulpą šalia, **kelią į plokštę**, ir tik tada
   *  remiasi į patį modelį (cpp:773-783). Be jos kiekviena „ant modelio"
   *  galvutė numeta stulpelį ant detalės, ir narvas išeina perpus retesnis.
   *
   *  Ieškoma tilto krypties (polar rėžiuose [PI - bridge_slope, PI]) ir ilgio
   *  [0, max_bridge_length] taip, kad iš nusileidimo taško vertikalus stulpas
   *  pasiektų plokštę neužkliuvęs. Originale tai NLopt MLSL; čia tinklelis, po
   *  jo — tas pats ilgio trumpinimas žingsniu r. */
  const connectToGround = h => {
    const src = h.junction, r = h.rBack, sd = bridgeSafety(r, cfg);
    const gnd = cfg.object_elevation_mm;
    if (src[2] <= gnd + cfg.base_height_mm) return false;
    /* Kryptys paruošiamos vieną kartą, o ilgis auga VISOMS iš karto: pirmas
       radinys tada ir yra trumpiausias tiltas, ir paieška nutrūksta. Anksčiau
       kiekviena kryptis buvo perrenkama iki galo — maršrutizacija truko 8,7 s. */
    /* VISOS 48 kryptys. Bandžiau atrinkti aštuonias pagal ilgiausią laisvą
       spindulį — klaida: svarbu ne kiek laisva ta kryptimi, o ar iš
       NUSILEIDIMO TAŠKO yra laisvas kelias žemyn, o to vienas spindulys palei
       tiltą nematuoja. Su atranka dėžučių testas liko be stulpų, o tikrame
       modelyje jų sumažėjo 30 -> 26. Greitį duoda ne siauresnė paieška, o
       pigus atmetimas žemiau. */
    const dirs = [];
    for (let k = 0; k < 4; k++) {
      const polar = Math.PI - (k / 3) * cfg.bridge_slope;
      const st = Math.sin(polar), ct = Math.cos(polar);
      for (let a = 0; a < 12; a++) {
        const az = (a / 12) * 2 * Math.PI;
        const n = [st * Math.cos(az), st * Math.sin(az), ct];
        // lmax — vienu spinduliu; tiltą vis tiek patikrins pluoštas žemiau.
        const free = mesh.rayHit(add(src, mul(n, r)), n).dist;
        dirs.push({ n, lmax: Math.min(cfg.max_bridge_length_mm,
                    Number.isFinite(free) ? free : cfg.max_bridge_length_mm) });
      }
    }
    let best = null;
    const step = Math.max(r, 1e-3);
    for (let l = 0; l <= cfg.max_bridge_length_mm && !best; l += step)
      for (const d of dirs) {
        if (l > d.lmax) continue;
        const p = add(src, mul(d.n, l));
        if (p[2] <= gnd + cfg.base_height_mm) continue;
        /* Pigus atmetimas: viena ašis. Beveik visi kandidatai krenta čia, ir
           brangaus pluošto jiems nebereikia. */
        if (mesh.rayHit(p, DOWN).dist < p[2] - gnd) continue;
        // Tikras sprendimas — pluoštas su saugos atstumu.
        if (beamHit(mesh, p, DOWN, r, r, sd) < p[2] - gnd) continue;
        if (beamHit(mesh, src, d.n, r, r, sd) < l) continue;   // ir pats tiltas
        best = { l, p };
        break;
      }
    if (!best) return false;
    pillars.push({ x: best.p[0], y: best.p[1], top: best.p[2], bottom: gnd,
                   rTop: r, rBase: cfg.base_radius_mm, head: h.id, bridges: 0 });
    if (best.l > 1e-6) bridges.push({ a: src.slice(), b: best.p.slice(), r });
    h.pillar = pillars.length - 1;
    return true;
  };

  /* --- 4 · routing_to_model (DefaultSupportTree.cpp:760-789) ------------- */
  /* Tvarka originale griežta: pirma ieškom stulpo šalia, tada kelio į plokštę,
     ir tik kaip PASKUTINĖ išeitis remiamės į patį modelį. Praleidus dvi
     pirmąsias pakopas 149 iš 182 atramų iškart atsidurdavo ant detalės
     (išmatuota 08-13). */
  for (const i of onModel) {
    const h = heads[i];
    h.id = i;
    if (searchPillarAndConnect(h)) continue;
    if (connectToGround(h)) continue;              // vidurinė pakopa (cpp:778)
    /* connect_to_model_body (cpp:670-706). Originale atramos taškas imamas iš
       DVIEJŲ matavimų: pluošto skeno iš classify (`hit`) ir spindulio palei
       AŠĮ (`center_hit = m_sm.emesh.query_ray_hit(hjp, DOWN)`), o galutinis —
           hitdiff = center_hit.distance() - hit.distance();
           hitp = |hitdiff| < 2*head.r_back_mm ? center_hit.position()
                                              : hit.position();
       Mūsų stulpas vertikalus, tad remtis galima TIK tuo, kas po ašimi.
       Pluošto žiedas (r = 0,5 mm) mato ir tai, ko po ašimi nėra: prie žemesnio
       kūno krašto jis užkabina briauną, ir stulpas atsidurdavo ore (išmatuota
       08-13: 3 iš 3 stulpų kabojo 4 mm virš nieko). Pluoštas lieka tik
       klausimui „ar kelias laisvas" — būtent tam jis ir naudojamas classify. */
    if (!Number.isFinite(h.groundHit)) continue;     // !hit.is_hit() -> return false
    const centre = mesh.rayHit(h.junction, DOWN);
    /* `hitp` — kur atsiremia apversta galvutė (cpp:699-701):
         hitdiff = center_hit.distance() - hit.distance();
         hitp = |hitdiff| < 2*r_back ? center_hit.position() : hit.position();
       Kai ašis ir pluoštas nesutampa, atrama krypsta į pluošto pataikymo tašką
       — anksčiau tokių galvučių tiesiog atsisakydavom, nes pasvirusių nepiešėm. */
    const hitdiff = centre.dist - h.groundHit;
    const onAxis = Math.abs(hitdiff) < 2 * h.rBack && Number.isFinite(centre.dist);
    const hitp = onAxis
      ? [h.junction[0], h.junction[1], h.junction[2] - centre.dist]
      : h.groundHitPos;
    if (!hitp) continue;                    // nėra kur atsiremti
    /* Spindulys eina iš pačios jungties, tad atstumas jau tikras — pluošto
       poslinkio (+r_back, SupportTreeUtils.hpp:179) čia nebėra ką kompensuoti. */
    /* `endp.z = hjp.z - hit.distance() + h` (cpp:696) — nuo PLUOŠTO atstumo,
       kaip originale, o ne nuo ašies. */
    const surface = h.junction[2] - h.groundHit;
    /* Stulpas NEVAROMAS į paviršių: jis baigiasi `hh` aukščiau, o likusį tarpą
       uždengia APVERSTA galvutė (`add_anchor`, cpp:684-706), smailėjanti iki
       head_front_radius. Tai ne grožis — storas stulpas, atremtas į detalę,
       nulūždamas palieka 1 mm žymę, o galvutė nusilaužia švariai.
         zangle = max(asin(dir.z), PI/4);  dir = DOWN -> PI/4
         hh = min(hit.distance() - r_back, sin(zangle) * fullwidth) */
    const fullwidth = 2 * cfg.head_front_radius_mm + h.width +
                      2 * h.rBack - cfg.head_penetration_mm;
    let hh = Math.min(h.groundHit - h.rBack, Math.SQRT1_2 * fullwidth);
    if (h.rBack < cfg.head_back_radius_mm) hh = Math.max(hh, 0);
    else if (hh <= 0) continue;
    const bottom = Math.max(0, surface + hh);
    if (h.junction[2] - bottom < cfg.base_height_mm) continue;   // galvutė atmetama
    pillars.push({ x: h.junction[0], y: h.junction[1], top: h.junction[2],
                   bottom, rTop: h.rBack, rBase: h.rBack, head: i, bridges: 0,
                   onModel: true, anchored: hh > 1e-6 });
    if (hh > 1e-6)
      // Atkarpa nuo stulpo galo iki `hitp` — vertikali, kai ašis sutampa, ir
      // PASVIRUSI, kai ne (`taildir = (endp - hitp).normalized()`, cpp:706).
      bridges.push({ a: [h.junction[0], h.junction[1], bottom],
                     b: hitp.slice(), r: h.rBack, anchor: true });
    h.pillar = pillars.length - 1;
  }

  lap('routing');

  /* --- 5 · interconnect_pillars (DefaultSupportTree.cpp:189, 792) -------- */
  const links = [];
  const zmin = cfg.base_height_mm;
  /* DefaultSupportTree.cpp:815-851 `cascadefn`: kiekvienas stulpas jungiasi tik
     su ARTIMIAUSIAIS kaimynais ir tik tol, kol turi mažiau nei
     pillar_cascade_neighbors jungčių; kiekviena pora jungiama vieną kartą
     (`pairs` aibė). Jungiant visas poras iš eilės narvas išeidavo dvigubai
     tankesnis nei PrusaSlicer'io (auditas + matavimas 08-13). */
  for (const p of pillars) p.links = 0;
  const donePairs = new Set();
  const order = pillars.map((p, i) => i);
  for (const i of order) {
    const A = pillars[i];
    if (A.links >= cfg.pillar_cascade_neighbors) continue;
    const maxD = cfg.max_pillar_link_distance_mm *
                 (A.rTop || cfg.pillar_radius_mm) / cfg.head_back_radius_mm;
    const near = [];
    for (let j = 0; j < pillars.length; j++) {
      if (j === i) continue;
      const d = Math.hypot(A.x - pillars[j].x, A.y - pillars[j].y);
      if (d < maxD) near.push({ j, d });
    }
    near.sort((a, b) => a.d - b.d);
    for (const { j, d } of near) {
      if (A.links >= cfg.pillar_cascade_neighbors) break;
      const key = i < j ? i + ':' + j : j + ':' + i;
      if (donePairs.has(key)) continue;
      const B = pillars[j];
      if (d < 2 * cfg.head_back_radius_mm) continue;
      const bridgeDistance = d / Math.cos(-cfg.bridge_slope);
      const zstep = d * Math.tan(-cfg.bridge_slope);
      let sUp = A.top, sLo = B.top;
      let eUp = Math.max(A.bottom, zmin), eLo = Math.max(B.bottom, zmin);
      let ax = A.x, ay = A.y, bx = B.x, by = B.y;
      if (sUp - eUp < 0 || sLo - eLo < 0) continue;
      if (sUp < sLo) { [sUp, sLo] = [sLo, sUp]; [ax, bx] = [bx, ax]; [ay, by] = [by, ay]; }
      if (eUp < eLo) [eUp, eLo] = [eLo, eUp];
      let startz = (sLo - zstep < sUp) ? sLo - zstep : sLo;
      if (sLo - eUp < Math.abs(zstep)) {
        startz = Math.min(sUp, sLo - zstep);
        const endz = Math.max(eUp + zstep, eLo);
        const avail = startz - endz;
        const rounds = Math.floor(avail / Math.abs(zstep));
        startz -= 0.5 * (avail - rounds * Math.abs(zstep));
      }
      let a = [ax, ay, startz], b = [bx, by, startz + zstep];
      let made = false, guard = 0;
      while (b[2] >= eUp && guard++ < 200) {
        if (beamHit(mesh, a, norm(sub(b, a)), cfg.head_front_radius_mm,
                    cfg.head_front_radius_mm, cfg.safety_distance_mm) >= bridgeDistance) {
          links.push({ a: a.slice(), b: b.slice(), r: cfg.pillar_radius_mm });
          made = true;
        }
        const t = a; a = b; b = [t[0], t[1], a[2] + zstep];
      }
      donePairs.add(key);
      if (made) { A.links++; B.links = (B.links || 0) + 1; }
    }
  }

  /* --- 6 · merge_result -------------------------------------------------- */
  lap('interconnect');
  /* Padas — po viskuo, kas stovi ant plokštės (SLA/Pad.hpp). */
  const pad = await buildPad(pos, pillars, cfg);
  log.pillars = pillars.length;
  log.bridges = bridges.length;
  log.links = links.length;
  return { pillars, bridges, links, heads, mesh, pad, log };
}

/* ------------------------------------------------- suderinamas paviršius */
/* Kad pultas ir stendas galėtų įkelti šį modulį nieko nekeisdami, grąžinam
   tokios pat formos rezultatą kaip `slicer.js`: stulpeliai su x/y/cx/cy/top/
   bottom ir jungtys su ax/ay/z0/bx/by/z1. */
export async function findOverhangs(pos, layers, onProgress) {
  const t = await buildSupportTree(pos, CFG, onProgress);
  const pillars = t.pillars.map(p => ({
    x: p.x, y: p.y, cx: p.x, cy: p.y, top: p.top, bottom: p.bottom,
    tower: !p.onModel,
    anchored: !!p.anchored,
    /* `partial` keliaujam kartu — be jo savikontrolė reikalaudavo medžiagos po
       stulpeliu, kuris remiasi į tiltą, ir visada degdavo raudonai. */
    partial: !!p.partial,
  }));
  /* Galvutės (heads) — atkarpa nuo jungties taško iki paties paviršiaus,
     smailėjanti į head_front_radius. Be jos stulpas baigiasi head_width_mm
     atstumu nuo detalės ir nieko nelaiko (matyta renderyje, 08-13).
     Piešiama kaip „bridge", nes piešėjas būtent tiltams daro smaigalį. */
  const heads = t.heads.filter(h => h.pillar >= 0).map(h => ({
    a: h.junction.slice(), b: h.pos.slice(), headTip: true,
  }));
  /* Piešėjas eina iš apačios į viršų, tad žemesnis galas turi būti pirmas.
     Tiltas nuo galvutės į stulpą leidžiasi žemyn — pirmiau jį tiesiog
     išmesdavau, ir dėl to galvutės kabodavo atskirai nuo narvo (08-13).
     Smaigalį daro tik GALVUTĖ (`bridge`); tiltai ir jungtys — vienodo storio,
     kaip ir originale. */
  const braces = [...t.bridges, ...t.links, ...heads].map(c => {
    const up = c.b[2] >= c.a[2];
    const lo = up ? c.a : c.b, hi = up ? c.b : c.a;
    return {
      ax: lo[0], ay: lo[1], z0: lo[2],
      bx: hi[0], by: hi[1], z1: hi[2],
      bridge: c.headTip === true,
      anchor: c.anchor === true,
    };
  });
  return {
    pillars, braces, companions: [],
    towers: t.pillars.filter(p => !p.onModel).length,
    bridges: t.bridges.length,
    pad: t.pad,
    padMm2: t.pad ? t.pad.reduce((s2, v) => s2 + v, 0) * PIXEL_MM * PIXEL_MM : 0,
    /* Savikontrolė tikra, ne kietai įrašytas nulis. */
    islands: selfCheck({ pillars, braces }, t.mesh, CFG), firstIsland: 0,
    onModel: t.pillars.filter(p => p.onModel).length,
    dropped: false,
    log: t.log,
  };
}

/** Sluoksniai, peržiūra ir ZIP — ta pati funkcija kaip pirmajame algoritme,
 *  tik supportus jai duodam savo. Be šito pultas kviesdavo bazinį `slice()`,
 *  tas pasidarydavo SENUS supportus, ir ekrane matėsi trys stulpeliai vietoj
 *  viso narvo (V 08-13: „naujas algoritmas nupiešia tik tris suportus"). */
export function slice(pos, opts, onProgress) {
  return sliceBase(pos, { ...(opts || {}), findSupports: findOverhangs,
                          discsFor: (sup, z) => discsFor(sup, z, CFG),
                          padLayers: CFG.pad_layers }, onProgress);
}

/* ------------------------------------------------------------------- padas */
/* SLA/Pad.hpp: PadConfig { wall_thickness_mm, wall_height_mm, brim_size_mm,
   wall_slope = pi/4 }, full_height = wall_height + wall_thickness. V profilyje
   pad_wall_height = 0, pad_wall_thickness = 0.15, pad_brim_size = 1.6,
   pad_around_object = 1 — tad padas plonas ir apjuosia tai, kas ant plokštės.

   2D dalis (sujungimas ir apvado offset) daroma Clipper'iu, kaip nurodyta:
   savo poligonų matematikos nerašom. Clipper dirba sveikais skaičiais, tad
   milimetrai keliami SCALE kartų. */
const SCALE = 1000;

/** Atkarpos -> uždari keliai. sliceAt grąžina jas orientuotas (medžiaga
 *  kairėje), tad jungiam paprastai: kiekvienos galas yra kitos pradžia. */
function stitch(seg, eps = 1e-4) {
  const key = (x, y) => Math.round(x / eps) + ',' + Math.round(y / eps);
  const start = new Map();
  for (let i = 0; i < seg.length; i += 4) {
    const k = key(seg[i], seg[i + 1]);
    let l = start.get(k);
    if (!l) { l = []; start.set(k, l); }
    l.push(i);
  }
  const used = new Uint8Array(seg.length / 4);
  const paths = [];
  for (let i = 0; i < seg.length; i += 4) {
    if (used[i / 4]) continue;
    const path = [];
    let cur = i, guard = 0;
    while (cur !== undefined && !used[cur / 4] && guard++ < 1e6) {
      used[cur / 4] = 1;
      path.push({ X: Math.round(seg[cur] * SCALE), Y: Math.round(seg[cur + 1] * SCALE) });
      const l = start.get(key(seg[cur + 2], seg[cur + 3]));
      cur = l && l.find(j => !used[j / 4]);
    }
    if (path.length >= 3) paths.push(path);
  }
  return paths;
}

/** Apskritimas kaip Clipper kelias. */
function circlePath(cx, cy, r, n = 24) {
  const p = [];
  for (let i = 0; i < n; i++) {
    const a = 2 * Math.PI * i / n;
    p.push({ X: Math.round((cx + r * Math.cos(a)) * SCALE),
             Y: Math.round((cy + r * Math.sin(a)) * SCALE) });
  }
  return p;
}

/** Pado kaukė: viskas, kas stovi ant plokštės, sujungiama ir išplečiama
 *  brim_size. Grąžina Uint8Array (RES.w × RES.h), tokį patį, kokio tikisi
 *  sluoksnių piešėjas. */
export async function buildPad(pos, pillars, cfg = CFG) {
  const CL = (await import('./clipper.js')).default;
  const seg = [];
  sliceAt(pos, cfg.pad_thickness_mm * 0.5, seg);
  const paths = stitch(seg);
  for (const p of pillars)
    if (p.bottom <= 1e-6) paths.push(circlePath(p.x, p.y, cfg.base_radius_mm));
  if (!paths.length) return null;

  // Sujungiam viską į vieną figūrą (non-zero, kaip libslic3r).
  const c = new CL.Clipper();
  c.AddPaths(paths, CL.PolyType.ptSubject, true);
  const united = new CL.Paths();
  c.Execute(CL.ClipType.ctUnion, united, CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);

  // Apvadas: offset brim_size_mm.
  const co = new CL.ClipperOffset();
  co.AddPaths(united, CL.JoinType.jtRound, CL.EndType.etClosedPolygon);
  const grown = new CL.Paths();
  co.Execute(grown, cfg.pad_brim_mm * SCALE);

  // Į kaukę: skenavimo eilutės per gautus poligonus.
  const W = RES.w, H = RES.h;
  const sx = W / PLATE.x, sy = H / PLATE.y;
  const mask = new Uint8Array(W * H);
  const xs = [];
  for (let row = 0; row < H; row++) {
    const yMm = (row + 0.5) / sy - PLATE.y / 2;
    xs.length = 0;
    for (const path of grown) {
      for (let i = 0; i < path.length; i++) {
        const a = path[i], b = path[(i + 1) % path.length];
        const ay = a.Y / SCALE, by = b.Y / SCALE;
        if ((ay > yMm) === (by > yMm)) continue;
        const t = (yMm - ay) / (by - ay);
        xs.push((a.X / SCALE + (b.X / SCALE - a.X / SCALE) * t + PLATE.x / 2) * sx);
      }
    }
    if (xs.length < 2) continue;
    xs.sort((p, q) => p - q);
    for (let k = 0; k + 1 < xs.length; k += 2) {
      const x0 = Math.max(0, Math.round(xs[k])), x1 = Math.min(W - 1, Math.round(xs[k + 1]));
      for (let x = x0; x <= x1; x++) mask[row * W + x] = 1;
    }
  }
  return mask;
}

/* ------------------------------------------------- taškai iš SLUOKSNIŲ */
/* SupportPointGenerator.cpp:409 `sample_overhangs`:
     overhangs = diff_ex(shape, prev_shapes)
   — nuokaba yra ne veido kampas, o SLUOKSNIO IR ANKSTESNIO SLUOKSNIO
   SKIRTUMAS, ir taškai sėjami ant to skirtumo KONTŪRO vienodu žingsniu.

   Būtent to trūko pirmojoje sėjoje: ėmus mesh veidus pagal normalę, 40 iš 261
   galvutės atsidurdavo viršutinėje modelio dalyje, kur niekas nekaba
   (V 08-13: „pankas"). Sluoksniuose ten figūra tik mažėja, tad skirtumo nėra
   ir taškų neatsiranda. */
/** Atramos taško įtakos spindulys, kai esam `dz` mm virš jo.
 *  `prepare_supports_for_layer` (SPG.cpp:495-543): tiesinė interpoliacija
 *  kreivėje; density mažina spindulį per sqrt(r² / density). */
function influenceRadius(dz, cfg) {
  const c = cfg.support_curve;
  let r;
  if (dz <= c[0][1]) r = c[0][0];
  else if (dz >= c[c.length - 1][1]) r = c[c.length - 1][0];
  else {
    r = c[c.length - 1][0];
    for (let k = 0; k + 1 < c.length; k++)
      if (dz >= c[k][1] && dz <= c[k + 1][1]) {
        const t = (dz - c[k][1]) / ((c[k + 1][1] - c[k][1]) || 1);
        r = c[k][0] + t * (c[k + 1][0] - c[k][0]);
        break;
      }
  }
  const d = cfg.support_points_density;
  return Math.abs(d - 1) > 1e-4 ? Math.sqrt(r * r / d) : r;
}

/** Clipper rezultatas -> ExPolygon atitikmenys (kontūras + jo skylės). */
function toExPolys(CL, tree) {
  const out = [];
  const stack = tree.Childs().slice();
  while (stack.length) {
    const n = stack.pop();
    if (n.IsHole()) { for (const ch of n.Childs()) stack.push(ch); continue; }
    const holes = n.Childs();
    out.push([n.Contour(), ...holes.map(h => h.Contour())]);
    for (const h of holes) stack.push(h);
  }
  return out;
}

function pathsBBox(paths) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of paths) for (const q of p) {
    if (q.X < x0) x0 = q.X; if (q.X > x1) x1 = q.X;
    if (q.Y < y0) y0 = q.Y; if (q.Y > y1) y1 = q.Y;
  }
  return [x0, y0, x1, y1];
}

/** Kilpa -> taškai vienodu žingsniu (`sample()`, SPG.cpp:361). */
function walkRing(path, step, into) {
  const n = path.length;
  let total = 0;
  for (let k = 0; k < n; k++) {
    const a = path[k], b = path[(k + 1) % n];
    total += Math.hypot((b.X - a.X) / SCALE, (b.Y - a.Y) / SCALE);
  }
  if (total < step) { into.push([path[0].X / SCALE, path[0].Y / SCALE]); return; }
  const count = Math.max(1, Math.floor(total / step));
  const want = total / count;
  let acc = 0, next = 0;
  for (let k = 0; k < n; k++) {
    const a = path[k], b = path[(k + 1) % n];
    const ax = a.X / SCALE, ay = a.Y / SCALE;
    const L = Math.hypot(b.X / SCALE - ax, b.Y / SCALE - ay);
    while (next <= acc + L && into.length < 1e5) {
      const u = L ? (next - acc) / L : 0;
      into.push([ax + (b.X / SCALE - ax) * u, ay + (b.Y / SCALE - ay) * u]);
      next += want;
    }
    acc += L;
  }
}

/** Ar taškas ExPolygon viduje (even-odd prieš visą rinkinį). */
function pointInPaths(paths, x, y) {
  let inside = false;
  for (const p of paths)
    for (let k = 0, j = p.length - 1, n = p.length; k < n; j = k++) {
      const kx = p[k].X / SCALE, ky = p[k].Y / SCALE;
      const jx = p[j].X / SCALE, jy = p[j].Y / SCALE;
      if ((ky > y) !== (jy > y) && x < (jx - kx) * (y - ky) / (jy - ky) + kx)
        inside = !inside;
    }
  return inside;
}

/** Mažiausias atstumas nuo taško iki kelių rinkinio kraštinių (mm). */
function distToPaths(paths, x, y) {
  let best = Infinity;
  for (const p of paths) {
    for (let k = 0, n = p.length; k < n; k++) {
      const a = p[k], b = p[(k + 1) % n];
      const ax = a.X / SCALE, ay = a.Y / SCALE;
      const bx = b.X / SCALE, by = b.Y / SCALE;
      const dx = bx - ax, dy = by - ay;
      const L2 = dx * dx + dy * dy;
      let t = L2 ? ((x - ax) * dx + (y - ay) * dy) / L2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      const d = Math.hypot(x - (ax + dx * t), y - (ay + dy * t));
      if (d < best) { best = d; if (best < 1e-7) return best; }
    }
  }
  return best;
}

/* ------------------------------------------------- taškai iš SLUOKSNIŲ */
/* SupportPointGenerator.cpp:409 `sample_overhangs`:
     overhangs = diff_ex(shape, prev_shapes)
   — nuokaba yra ne veido kampas, o SLUOKSNIO IR ANKSTESNIO SLUOKSNIO
   SKIRTUMAS, ir taškai sėjami ant to skirtumo KONTŪRO.

   Tankio NEVALDO pastovus žingsnis. Kraštas smulkiai diskretizuojamas
   (`discretize_overhang_step` = 2 mm), o kandidatas tampa atrama TIK jei jo
   neuždengia jau esančių atramų įtakos spindulys, kuris AUGA kylant aukštyn
   (`support_curve`). Įtaka keliauja tik per SUSIJUSIAS sluoksnio dalis
   (`create_near_points`, SPG.cpp:210), ne per visą XY plokštumą — taikant ją
   globaliai kelios apatinės atramos „uždengia" viską aukščiau.

   Perkelta iš slicer3 (Python laboratorijos), kur mechanizmas buvo išbandytas
   pirmas: JS versijos pastovus 3 mm tinklelis niekada neaugo, ir dėl to narvas
   išeidavo 2–3× tankesnis nei etalono (išmatuota 2026-08-12). */
export async function samplePointsFromLayers(pos, cfg = CFG, onProgress) {
  const CL = (await import('./clipper.js')).default;
  const b = bounds(pos);
  const layers = Math.max(1, Math.ceil(b.size[2] / LAYER_MM));
  const step = cfg.discretize_overhang_step_mm;
  const minR = cfg.minimal_part_radius_mm * SCALE;
  const out = [];                 // pasirinkti atramos taškai
  let prevParts = [];             // [{ paths, bbox, active:Set }]
  const seg = [];
  /* Laikmačiai: kartą jau „pagreitinau" ne tą vietą, tad daugiau nespėliojam. */
  const T = { slice: 0, tree: 0, below: 0, over: 0, land: 0, island: 0, pick: 0 };
  const now = () => (typeof performance !== 'undefined' ? performance.now() : Number(process.hrtime.bigint() / 1000000n));
  let tk;

  for (let i = 0; i < layers; i++) {
    const z = (i + 0.5) * LAYER_MM;
    tk = now(); sliceAt(pos, z, seg);
    const cur = stitch(seg); T.slice += now() - tk;
    const parts = [];
    if (cur.length) {
      tk = now();
      const tree = new CL.PolyTree();
      const c0 = new CL.Clipper();
      c0.AddPaths(cur, CL.PolyType.ptSubject, true);
      c0.Execute(CL.ClipType.ctUnion, tree,
                 CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
      const expolys = toExPolys(CL, tree); T.tree += now() - tk;
      for (const ex of expolys) {
        const bb = pathsBBox(ex);
        /* `get_small_parts` (SPG.cpp:1032): neatspausdinamos dalys išmetamos
           dar prieš sėją, kitaip kiekvienas mesh triukšmo taškelis virsta sala. */
        if (Math.max(bb[2] - bb[0], bb[3] - bb[1]) < 2 * minR) continue;

        /* Siejam su ankstesnio sluoksnio dalimis TIKRU persidengimu, ne
           gabaritais: gabaritai persidengia beveik visada, tad viskas
           susisieja su viskuo, salų nebelieka ir atramų kiekis krenta
           (išmatuota: 12 stulpų vietoj 23). Gabaritai — tik pigus sietas
           prieš tikrą patikrą. */
        tk = now();
        const below = prevParts.filter(pp => {
          if (bb[2] < pp.bbox[0] || bb[0] > pp.bbox[2] ||
              bb[3] < pp.bbox[1] || bb[1] > pp.bbox[3]) return false;
          /* Gretimi sluoksniai beveik sutampa, tad daugumą porų išsprendžia
             VIENAS taškas: jei vienos figūros viršūnė yra kitos viduje —
             persidengia, ir Clipper'io kviesti nereikia. Pilna sankirta lieka
             tik neaiškiems atvejams. Išmatuota: šis žingsnis buvo 7,8 s iš
             17,1 s visos sėjos. */
          const a0 = ex[0][0], b0 = pp.paths[0][0];
          if (pointInPaths(pp.paths, a0.X / SCALE, a0.Y / SCALE)) return true;
          if (pointInPaths(ex, b0.X / SCALE, b0.Y / SCALE)) return true;
          const ci = new CL.Clipper();
          ci.AddPaths(ex, CL.PolyType.ptSubject, true);
          ci.AddPaths(pp.paths, CL.PolyType.ptClip, true);
          const inter = new CL.Paths();
          ci.Execute(CL.ClipType.ctIntersection, inter,
                     CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
          return inter.length > 0;
        });
        T.below += now() - tk;
        const active = new Set();
        for (const pp of below) for (const k of pp.active) active.add(k);
        /* `remove_supports_out_of_part` (SPG.cpp:555): atrama nustoja dengti,
           kai ši dalis nuo jos nutolsta daugiau nei removing_delta. Be šito
           senos apatinės atramos blokuoja kandidatus per visą modelio aukštį. */
        for (const k of [...active]) {
          const s2 = out[k];
          if (!(s2.pos[0] >= bb[0] / SCALE - cfg.removing_delta_mm &&
                s2.pos[0] <= bb[2] / SCALE + cfg.removing_delta_mm &&
                s2.pos[1] >= bb[1] / SCALE - cfg.removing_delta_mm &&
                s2.pos[1] <= bb[3] / SCALE + cfg.removing_delta_mm) ||
              distToPaths(ex, s2.pos[0], s2.pos[1]) > cfg.removing_delta_mm &&
              !pointInPaths(ex, s2.pos[0], s2.pos[1]))
            active.delete(k);
        }

        if (z >= cfg.base_height_mm) {
          // Nuokaba = ši dalis MINUS po ja esančios dalys.
          const cand = [];      // per įtakos spindulio filtrą
          const free = [];      // BE filtro — salos ir pussaliai (SPG.cpp:300,316)
          let island = false;

          /* Salos/pussalio sėja: plonoms dalims — „nugarkaulis", storoms —
             kontūras plius retas vidaus tinklelis. Ta pati taisyklė abiem, tad
             gyvena vienoje vietoje. */
          const islandLike = (paths, pbb, out) => {
            const thin = cfg.island_outline_step_mm / 2;
            const cin = new CL.ClipperOffset();
            cin.AddPaths(paths, CL.JoinType.jtMiter, CL.EndType.etClosedPolygon);
            const shrunk = new CL.Paths();
            cin.Execute(shrunk, -thin * SCALE);
            if (shrunk.length === 0) {
              const deep = [];
              for (let gy = pbb[1] / SCALE; gy <= pbb[3] / SCALE; gy += 0.3)
                for (let gx = pbb[0] / SCALE; gx <= pbb[2] / SCALE; gx += 0.3)
                  if (pointInPaths(paths, gx, gy)) {
                    const d = distToPaths(paths, gx, gy);
                    if (d > 0.05) deep.push([d, gx, gy]);
                  }
              deep.sort((p1, p2) => p2[0] - p1[0]);
              const sp2 = cfg.island_thin_step_mm * cfg.island_thin_step_mm;
              for (const [, gx, gy] of deep)
                if (!out.some(c => (c[0] - gx) ** 2 + (c[1] - gy) ** 2 < sp2))
                  out.push([gx, gy]);
              if (!out.length) walkRing(paths[0], cfg.island_outline_step_mm, out);
            } else {
              for (const ring of paths) walkRing(ring, cfg.island_outline_step_mm, out);
              const st = cfg.island_inner_step_mm;
              for (let gy = Math.ceil(pbb[1] / SCALE / st) * st; gy <= pbb[3] / SCALE; gy += st)
                for (let gx = Math.ceil(pbb[0] / SCALE / st) * st; gx <= pbb[2] / SCALE; gx += st)
                  if (pointInPaths(shrunk, gx, gy)) out.push([gx, gy]);
            }
          };

          if (!below.length) {
            island = true;                        // sala: kabo visa
            islandLike(ex, bb, free);
          } else {
            tk = now();
            const clip = [];
            for (const pp of below) for (const p of pp.paths) clip.push(p);
            const c1 = new CL.Clipper();
            c1.AddPaths(ex, CL.PolyType.ptSubject, true);
            c1.AddPaths(clip, CL.PolyType.ptClip, true);
            const over = new CL.PolyTree();
            c1.Execute(CL.ClipType.ctDifference, over,
                       CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
            const raw = [];
            for (const oex of toExPolys(CL, over))
              for (const ring of oex) walkRing(ring, step, raw);
            /* Kraštas, sutampantis su ankstesniu sluoksniu, praleidžiamas
               (`contain_point(p, prev_points)`, cpp:429): tai jau paremta
               „sausuma", ne nuokabos krantas. */
            T.over += now() - tk;
            tk = now();
            for (const [x, y] of raw)
              if (distToPaths(clip, x, y) > 1e-6) cand.push([x, y]);
            T.land += now() - tk;

            /* `create_peninsulas` (SPG.cpp:567) + `support_peninsulas`
               (SPG.cpp:316). Vieno sluoksnio nuokaba, išsikišusi toliau nei
               `peninsula_min_width` (2 mm) už to, kas po ja, yra „pussalis" ir
               remiama ATSKIRAI — ne tik kraštas, o visas plotas, kaip sala.
               Savaime laikosi tik tai, kas arčiau nei
               `peninsula_self_supported_width` (1,5 mm) nuo „sausumos".

               Tai PRIDEDANTIS mechanizmas. Be jo plokščia nuokaba gauna tik
               kontūro taškus: kronsteine mūsų 8 prieš etalono 12–20
               (išmatuota 08-13). Ant glotnaus kūno jis netyli — ten sluoksnis
               retai išsikiša 2 mm per vieną žingsnį. */
            tk = now();
            const grow = (delta) => {
              const co = new CL.ClipperOffset();
              co.AddPaths(clip, CL.JoinType.jtMiter, CL.EndType.etClosedPolygon);
              const o = new CL.Paths();
              co.Execute(o, delta * SCALE);
              return o;
            };
            const diff = (subj, cl) => {
              const c = new CL.Clipper();
              c.AddPaths(subj, CL.PolyType.ptSubject, true);
              if (cl.length) c.AddPaths(cl, CL.PolyType.ptClip, true);
              const tr = new CL.PolyTree();
              c.Execute(CL.ClipType.ctDifference, tr,
                        CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
              return tr;
            };
            const overPen = toExPolys(CL, diff(ex, grow(cfg.peninsula_min_width_mm)));
            if (overPen.length) {
              const shapes = toExPolys(CL, diff(ex, grow(cfg.peninsula_self_supported_width_mm)));
              for (const pex of shapes) {
                // pakankamai platus? — turi persidengti su `overPen`
                const ci = new CL.Clipper();
                ci.AddPaths(pex, CL.PolyType.ptSubject, true);
                for (const oe of overPen) ci.AddPaths(oe, CL.PolyType.ptClip, true);
                const inter = new CL.Paths();
                ci.Execute(CL.ClipType.ctIntersection, inter,
                           CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
                if (!inter.length) continue;
                islandLike(pex, pathsBBox(pex), free);
              }
            }
            T.island += now() - tk;
          }
          // Atranka pagal augantį įtakos spindulį.
          tk = now();
          for (const [x, y] of cand) {
            let covered = false;
            for (const k of active) {
              const s = out[k];
              if (Math.hypot(s.pos[0] - x, s.pos[1] - y) <
                  influenceRadius(z - s.pos[2], cfg)) { covered = true; break; }
            }
            if (covered) continue;
            out.push({ pos: [x, y, z], normal: [0, 0, -1], island });
            active.add(out.length - 1);
          }
          /* Salos ir pussaliai — BE spindulio patikros: originale filtras yra
             tik `support_part_overhangs` (SPG.cpp:270), o `support_island` ir
             `support_peninsulas` savo taškus deda besąlygiškai. */
          for (const [x, y] of free) {
            out.push({ pos: [x, y, z], normal: [0, 0, -1], island: true });
            active.add(out.length - 1);
          }
          T.pick += now() - tk;
        }
        parts.push({ paths: ex, bbox: bb, active });
      }
    }
    /* prev VISADA ankstesnis sluoksnis, net tuščias: kitaip virš tuštumos
       atsiradusi sala nebūtų skirtumas ir liktų be nieko. */
    prevParts = parts;
    if (onProgress && (i % 32 === 0)) onProgress(i + 1, layers);
  }
  for (const k in T) T[k] = Math.round(T[k]);
  /* Naršyklėje `process` neegzistuoja, o `process?.env` nuo to neapsaugo —
     neapibrėžtas identifikatorius meta ReferenceError dar prieš optional
     chaining. Tikrinam per typeof. */
  if (typeof process !== 'undefined' && process.env && process.env.SLICER_TIMES)
    console.log('  sėjos vidus (ms):', JSON.stringify(T));
  return out;
}

/* ------------------------------------------------- savas piešimas (CFG) */
/* Iki šiol geometriją brėžė `slicer.js` funkcijos, o jos skaito `SUP` — todėl
   ekrane matėsi ne tie matmenys, kuriuos suskaičiavo šis modulis: padas 2,7×
   storesnis, jungtys 2× storesnės, koja siauresnė ir aukštesnė (auditas
   08-13). Čia viskas piešiama TAIS PAČIAIS skaičiais, kuriais skaičiuota. */

/** Stulpelio apskritimai duotam aukščiui. Pėda platėja per base_height_mm iki
 *  base_radius_mm (SLA/Pad: support_base_diameter 3, support_base_height 1). */
export function pillarDiscs2(pillars, z, cfg = CFG) {
  const out = [];
  for (const p of pillars) {
    if (z > p.top || z < p.bottom) continue;
    let r = p.rTop || cfg.pillar_radius_mm;
    const up = z - p.bottom;
    if (p.bottom <= 1e-6 && up < cfg.base_height_mm) {
      const t = up / cfg.base_height_mm;
      r = cfg.base_radius_mm + (r - cfg.base_radius_mm) * t;
    }
    out.push({ x: p.x, y: p.y, r });
  }
  return out;
}

/** Tiltų, jungčių ir galvučių apskritimai. Galvutė (headTip) siaurėja iki
 *  head_front_radius_mm per head_width_mm — support_head_front_diameter 0.5. */
export function braceDiscs2(braces, z, cfg = CFG) {
  const out = [];
  for (const c of braces) {
    if (z < c.z0 || z > c.z1) continue;
    const t = (z - c.z0) / ((c.z1 - c.z0) || 1);
    let r = cfg.pillar_radius_mm;
    if (c.anchor) {
      /* Apversta galvutė: platus galas viršuje prie stulpo, smaigalys apačioje,
         detalėje. Kūgis per visą atkarpą. */
      r = cfg.head_front_radius_mm +
          (cfg.pillar_radius_mm - cfg.head_front_radius_mm) * t;
    } else if (c.bridge) {
      const left = c.z1 - z;
      if (left < cfg.head_width_mm)
        r = cfg.head_front_radius_mm +
            (cfg.pillar_radius_mm - cfg.head_front_radius_mm) * (left / cfg.head_width_mm);
    }
    out.push({ x: c.ax + (c.bx - c.ax) * t, y: c.ay + (c.by - c.ay) * t, r });
  }
  return out;
}

/** Vieno sluoksnio diskai — tai, ką `slice()` paims per opts.discsFor. */
export function discsFor(sup, z, cfg = CFG) {
  const d = pillarDiscs2(sup.pillars, z, cfg);
  /* Jungtys prasideda virš pėdų — žemiau stulpas ir taip platus. Riba ta pati,
     kuria skaičiuota (base_height_mm), o ne svetima SUP.padMm. */
  if (sup.braces && sup.braces.length && z >= cfg.base_height_mm)
    for (const b of braceDiscs2(sup.braces, z, cfg)) d.push(b);
  return d;
}

/* --------------------------------------------------- savikontrolė (#4) */
/* Iki šiol naujam algoritmui ji buvo išjungta ir visada sakė „švaru" — pulto
   įspėjimas „would print hanging in the air" niekada neužsidegdavo (auditas
   08-13). Tikrinam tiesiogiai geometriją: kiekvienas stulpas turi remtis į
   plokštę arba į paviršių, o kiekviena gija — turėti bent vieną tvirtą galą. */
export function selfCheck(sup, mesh, cfg = CFG) {
  if (!mesh) return 0;
  /* Zondas leidžiamas iš `bottom + EPS`, t. y. iš TUŠTUMOS virš atramos taško.
     Anksčiau jis buvo leidžiamas iš `bottom - 1e-3` — MEDŽIAGOS VIDUJE, tad
     spindulys išeidavo pro apatinį paviršių ir grąžindavo kūno storį, ne nulį:
     visi 17 pranešimų buvo klaidingi (auditas 08-13). Dvi teisingos baigtys:
       hr.inside      — apačia įleista į medžiagą (spindulys pataiko iš vidaus),
       hr.dist <= tol — apačia guli ant paviršiaus. */
  const EPS = 1e-3;
  const tol = EPS + LAYER_MM;                          // vienas sluoksnis atsargos
  let hanging = 0;
  for (const p of sup.pillars) {
    if (p.bottom <= 1e-6) continue;                    // stovi ant plokštės
    /* `partial` stulpelis po galvute remiasi į TILTĄ, ne į medžiagą
       (connect_to_nearpillar, cpp:282-363) — medžiagos po juo ir neturi būti. */
    if (p.partial) continue;
    /* `anchored` stulpo apačioje medžiagos NĖRA ir neturi būti — po juo eina
       apversta galvutė iki paviršiaus; ją tikrina `braces` dalis. */
    if (p.anchored) continue;
    const hr = mesh.rayHit([p.x, p.y, p.bottom + EPS], DOWN);
    if (hr.inside || hr.dist <= tol) continue;
    hanging++;
  }
  /* Gijos tikrinamos atskirai ir savo matu: tiltas ar jungtis „kabo" tada, kai
     nė vienas galas neremiasi į stulpą. Anksčiau jos nebuvo tikrinamos išvis. */
  if (sup.braces) hanging += danglingBraces(sup.pillars, sup.braces).length;
  return hanging;
}

/** Gijos, kurių abu galai kabo. Tiltai ir galvutės, kurių viršus remiasi į
 *  detalę, NĖRA kabantys — senoji funkcija to neskyrė ir grybo scenoje rodė
 *  7 melagingus pavojus iš 75 (auditas 08-13). */
export function danglingBraces(pillars, braces) {
  const at = new Set();
  for (const p of pillars) {
    at.add(p.x.toFixed(2) + ',' + p.y.toFixed(2));
  }
  const out = [];
  for (const c of braces || []) {
    if (c.bridge) continue;                            // galvutė kimba į detalę
    const a = at.has(c.ax.toFixed(2) + ',' + c.ay.toFixed(2));
    const b = at.has(c.bx.toFixed(2) + ',' + c.by.toFixed(2));
    if (!a && !b) out.push(c);
  }
  return out;
}
