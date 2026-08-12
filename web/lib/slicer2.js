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
  safety_distance_mm:   1.0,    // support_base_safety_distance
  max_bridge_length_mm: 10.0,   // support_max_bridge_length
  max_pillar_link_distance_mm: 10.0,  // support_max_pillar_link_distance
  max_bridges_on_pillar: 3,     // support_max_bridges_on_pillar
  bridge_slope:         Math.PI / 4,  // 45°, kaip jo numatytasis
  normal_cutoff_angle:  Math.PI / 2,  // pjovimo riba galvutės krypčiai
  ground_facing_only:   false,  // support_buildplate_only = 0
  object_elevation_mm:  0,      // pad_around_object = 1 -> nekeliam
  /* Taškų sėja. PrusaSlicer'io density_relative = 100 %; mūsų pikselis
     0.1275 mm, tad tankį išreiškiam atstumu tarp taškų. */
  support_points_density: 1.0,
  point_spacing_mm:      3.0,   // TODO: imti is SupportPointGenerator support_curve, ne derinti
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
export function beamHit(mesh, src, dir, r1, r2, sd = 0) {
  const d = norm(dir);
  const dst = add(src, d);
  const [a, b] = ringBasis(d);
  let best = INF;
  for (let i = 0; i < BEAM_SAMPLES; i++) {
    const ps = ringPoint(src, a, b, r1 + sd, i, BEAM_SAMPLES);
    const pd = ringPoint(dst, a, b, r2 + sd, i, BEAM_SAMPLES);
    const rd = norm(sub(pd, ps));
    let hr = mesh.rayHit(add(ps, mul(rd, r1)), rd);
    if (hr.inside && hr.dist < INF) {
      // Pataikyta iš vidaus — permetam iš išorės, kaip daro originalas.
      if (hr.dist > 2 * r1 + sd) { best = 0; continue; }
      /* hr.dist matuojamas nuo TAŠKO, iš kurio šauta (ps + rd*r1), tad
         permetant reikia to paties poslinkio — kitaip naujas spindulys
         atsiduria prieš paviršių ir pataiko į jį patį (rezultatas visada
         išeidavo ≈ r1). */
      const q = add(ps, mul(rd, r1 + hr.dist + 1e-6));
      hr = mesh.rayHit(q, rd);
    }
    if (hr.dist < best) best = hr.dist;
  }
  return best;
}

/** pinhead_mesh_hit — ar galvutė telpa neliesdama modelio. Tas pats pluoštas,
 *  tik nuo smaigalio iki nugarėlės, per `width`. */
export function pinheadHit(mesh, s, dir, rPin, rBack, width, sd = 0) {
  const d = norm(dir);
  const start = add(s, mul(d, rPin));
  /* Kūgis prasiskleidžia nuo rPin iki rBack per VISĄ galvutės ilgį. Anksčiau
     `dst = src + d` su normalizuotu d reiškė 1 mm, tad prie 0.25→0.5 kūgis
     buvo tris kartus per status ir galvutės krisdavo be reikalo (auditas). */
  const w = Math.max(1e-6, width);
  return beamHit(mesh, start, d, rPin, rPin + (rBack - rPin) / w, sd);
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
    // Laisvo kelio reikalavimas w (DefaultSupportTree.cpp:456).
    const need = r => width + 2 * r + 2 * cfg.head_front_radius_mm - cfg.head_penetration_mm;
    // Galvutė statoma nuo paviršiaus taško kryptimi dir.
    let hit = pinheadHit(mesh, p.pos, dir, cfg.head_front_radius_mm, rBack, need(rBack));
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
        const full = pinheadHit(mesh, p.pos, pr.d, cfg.head_front_radius_mm, rBack, want);
        if (full > bestHit) { bestHit = full; bestDir = pr.d; }
        if (bestHit > want) break;
      }
      dir = bestDir; hit = bestHit;
    }
    if (hit < need(rBack) && rBack > cfg.head_fallback_radius_mm) {
      rBack = cfg.head_fallback_radius_mm;
      hit = pinheadHit(mesh, p.pos, dir, cfg.head_front_radius_mm, rBack, need(rBack));
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
    const hit = beamHit(mesh, h.junction, DOWN, h.rBack, h.rBack);
    if (!(hit < INF)) ground.push(i);
    else if (cfg.ground_facing_only) continue;
    else { h.onModel = true; h.groundHit = hit; onModel.push(i); }
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
        if (beamHit(mesh, headjp, DOWN, r, r) < zdiff) return false;
      }
      if (Zdown <= nearU[2] && Zdown >= nearL[2] && D < maxLen) bridgeend[2] = Zdown;
      else return false;
    }
    // Empirinė riba: prie pat plokštės tiltas nekabinamas.
    if (bridgeend[2] < 4 * cfg.head_back_radius_mm) return false;
    const need = dist3d(bridgestart, bridgeend);
    if (beamHit(mesh, bridgestart, norm(sub(bridgeend, bridgestart)), r, r) < need)
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

  /* --- 4 · routing_to_model (DefaultSupportTree.cpp:760-789) ------------- */
  /* Tvarka originale griežta: pirma ieškom stulpo šalia, tada kelio į plokštę,
     ir tik kaip PASKUTINĖ išeitis remiamės į patį modelį. Praleidus dvi
     pirmąsias pakopas 149 iš 182 atramų iškart atsidurdavo ant detalės
     (išmatuota 08-13). */
  for (const i of onModel) {
    const h = heads[i];
    h.id = i;
    if (searchPillarAndConnect(h)) continue;
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
    /* NUOKRYPIS NUO ORIGINALO, sąmoningas: kai abu matavimai nesutampa
       (|hitdiff| >= 2·r_back — kaip tik briaunos atvejis), originalas stato
       PASVIRUSIĄ atramą į pluošto pataikymo tašką. Pasvirusių atramų dar
       nepiešiam, tad galvutės atsisakom: geriau be atramos nei atrama ore. */
    if (!Number.isFinite(centre.dist)) continue;
    /* Spindulys eina iš pačios jungties, tad atstumas jau tikras — pluošto
       poslinkio (+r_back, SupportTreeUtils.hpp:179) čia nebėra ką kompensuoti. */
    const bottom = Math.max(0, h.junction[2] - centre.dist);
    if (h.junction[2] - bottom < cfg.base_height_mm) continue;   // galvutė atmetama
    pillars.push({ x: h.junction[0], y: h.junction[1], top: h.junction[2],
                   bottom, rTop: h.rBack, rBase: h.rBack, head: i, bridges: 0,
                   onModel: true });
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
export async function samplePointsFromLayers(pos, cfg = CFG, onProgress) {
  const CL = (await import('./clipper.js')).default;
  const b = bounds(pos);
  const layers = Math.max(1, Math.ceil(b.size[2] / LAYER_MM));
  const step = Math.max(0.5, cfg.point_spacing_mm / Math.max(0.01, cfg.support_points_density));
  const out = [];
  const near = new Set();     // NearPoints atitikmuo
  let prev = [];
  const seg = [];
  for (let i = 0; i < layers; i++) {
    const z = (i + 0.5) * LAYER_MM;
    sliceAt(pos, z, seg);
    const cur = stitch(seg);
    if (cur.length && i > 0) {
      const c = new CL.Clipper();
      c.AddPaths(cur, CL.PolyType.ptSubject, true);
      if (prev.length) c.AddPaths(prev, CL.PolyType.ptClip, true);
      /* PolyTree, ne plokščias kelių sąrašas. Originale `diff_ex` grąžina
         ExPolygons — kiekviena su kontūru IR savo skylėmis
         (SupportPointGenerator.cpp:415), ir vidus yra kontūras MINUS skylės.
         Plokščiame sąraše skylės kilpa nuo kontūro neatskiriama: even-odd
         prieš TĄ VIENĄ kelią skylės viduje duoda „inside = true", ir atramos
         sėjamos į tuštumą — 20×20 plokštėje su 13×13 kiauryme 25 iš 48
         smaigalių kabojo ore (išmatuota 08-13). `Math.abs(Area)` nuo to
         negelbsti: abs nunulina būtent tą ženklą, kuris skylę ir skiria. */
      const tree = new CL.PolyTree();
      c.Execute(CL.ClipType.ctDifference, tree,
                CL.PolyFillType.pftNonZero, CL.PolyFillType.pftNonZero);
      /* Medį išskleidžiam į ExPolygon atitikmenis: lyginis gylis — kontūras,
         nelyginis — skylė; skylės vaikai yra savarankiškos salos joje. */
      const expolys = [];
      const stack = tree.Childs().slice();
      while (stack.length) {
        const n = stack.pop();
        if (n.IsHole()) { for (const ch of n.Childs()) stack.push(ch); continue; }
        const holeNodes = n.Childs();
        expolys.push({ contour: n.Contour(), holes: holeNodes.map(h => h.Contour()) });
        for (const h of holeNodes) stack.push(h);
      }
      /* Mažas skirtumas — tik kontūro drebėjimas, ne nuokaba. Riba: vieno
         pikselio juostelė aplink kontūrą. Plotas skaičiuojamas visai
         ExPolygon'ai: kontūras minus jo skylės. */
      const minArea = (PIXEL_MM * PIXEL_MM) * SCALE * SCALE * 4;
      /** even-odd prieš VISĄ rinkinį (kontūras + skylės): taškas skylėje kerta
       *  abi ribas, tad lieka lauke. */
      const inExPoly = (paths, gx, gy) => {
        let inside = false;
        for (const path of paths)
          for (let k = 0, j = path.length - 1; k < path.length; j = k++) {
            const kx = path[k].X / SCALE, ky = path[k].Y / SCALE;
            const jx = path[j].X / SCALE, jy = path[j].Y / SCALE;
            if ((ky > gy) !== (jy > gy) &&
                gx < (jx - kx) * (gy - ky) / (jy - ky) + kx) inside = !inside;
          }
        return inside;
      };
      const put = (px, py) => {
        /* NearPoints (SPG.cpp): naujo taško nededam, jei netoliese jau yra
           paremta vieta. Be šito kontūrų sėja davė 18 644 taškus vietoj
           poros tūkstančių — kiekvienas sluoksnis kartojo tą patį kraštą. */
        const key = Math.round(px / step) + ',' + Math.round(py / step) +
                    ',' + Math.round(z / step);
        if (near.has(key)) return;
        near.add(key);
        out.push({ pos: [px, py, z], normal: [0, 0, -1] });
      };
      for (const ex of expolys) {
        let area = Math.abs(CL.Clipper.Area(ex.contour));
        for (const h of ex.holes) area -= Math.abs(CL.Clipper.Area(h));
        if (area < minArea) continue;
        const all = [ex.contour, ...ex.holes];
        /* Vidaus užpildas — MŪSŲ priedas, ne originalo: dabartinis
           `sample_overhangs` sėja tik perimetrus (kontūrą ir kiekvieną skylę,
           cpp:473-477). Plokščia 6×8 mm nuokaba tada gaudavo taškus tik ant
           krašto, o vidurys likdavo be nieko (auditas 08-13). Paliekam, bet
           vadinam savo vardu — šaltinyje `sample_expolygon` funkcijos nėra. */
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
        for (const q of ex.contour) {
          const qx = q.X / SCALE, qy = q.Y / SCALE;
          if (qx < x0) x0 = qx; if (qx > x1) x1 = qx;
          if (qy < y0) y0 = qy; if (qy > y1) y1 = qy;
        }
        for (let gy = Math.ceil(y0 / step) * step; gy <= y1; gy += step)
          for (let gx = Math.ceil(x0 / step) * step; gx <= x1; gx += step)
            if (inExPoly(all, gx, gy)) put(gx, gy);
        /* Taškai ant kontūro vienodu žingsniu (sample(), cpp:361) — ir ant
           kontūro, IR ant kiekvienos skylės: skylės kraštas irgi yra nuokabos
           kraštas (cpp:475-477). */
        for (const path of all) {
          let carry = 0;
          for (let k = 0; k < path.length; k++) {
            const a = path[k], n = path[(k + 1) % path.length];
            const ax = a.X / SCALE, ay = a.Y / SCALE;
            const nx = n.X / SCALE, ny = n.Y / SCALE;
            const L = Math.hypot(nx - ax, ny - ay);
            for (let t = carry; t < L; t += step) {
              const u = t / L;
              put(ax + (nx - ax) * u, ay + (ny - ay) * u);
            }
            carry = L ? (Math.ceil((L - carry) / step) * step + carry - L) : 0;
          }
        }
      }
    }
    /* prev VISADA yra ankstesnis sluoksnis, net jei jis tuščias: palikus
       seną, virš tuštumos atsiradusi sala nebūdavo skirtumas ir negaudavo
       nė vieno taško — spausdintųsi ore (auditas 08-13). */
    prev = cur;
    if (onProgress && (i % 32 === 0)) onProgress(i + 1, layers);
  }
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
    if (c.bridge) {
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
