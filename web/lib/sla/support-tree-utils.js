/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/SupportTreeUtils.hpp
 *
 * Cia gyvena KOLIZIJU matematika - ja remiasi visas medis. Butent siu dvieju
 * patikru (pinhead<->mesh ir bridge<->mesh) truko pirmajame musu algoritme, ir
 * del to tiltai eidavo kiaurai detale.
 */

/* --------------------------------------------------------------- vektoriai */
export const v3 = (x, y, z) => [x, y, z];
export const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const mul = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
export const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a, b) => [a[1] * b[2] - a[2] * b[1],
                                a[2] * b[0] - a[0] * b[2],
                                a[0] * b[1] - a[1] * b[0]];
export const norm = a => Math.hypot(a[0], a[1], a[2]);
export const normalized = a => { const L = norm(a) || 1; return [a[0] / L, a[1] / L, a[2] / L]; };
export const distance3 = (a, b) => norm(sub(a, b));

/** `dirv` (STU.hpp:95): vienetinis vektorius nuo pradzios i pabaiga. */
export const dirv = (startp, endp) => normalized(sub(endp, startp));

export const EPSILON = 1e-9;

/* --------------------------------------------------------------- Hit
 *
 * `AABBMesh::hit_result` atitikmuo. `inside` reiskia, kad pataikyta i pavirsiu
 * IS VIDAUS (normale su spinduliu vienakrypte) - originalas tuo remiasi
 * permesdamas spinduli.
 */
export const Hit = (dist = Infinity, inside = false) => ({ dist, inside });

/** `min_hit` (STU.hpp:101): artimiausias is rinkinio. */
export function minHit(hits) {
  let best = hits[0];
  for (let i = 1; i < hits.length; i++) if (hits[i].dist < best.dist) best = hits[i];
  return best;
}

/* --------------------------------------------------------------- PointRing
 *
 * `PointRing<N>` (STU.hpp:36-92). Ziedas tasku aplink asi `n`: `get(0)` yra
 * pats centras, `get(1..N-1)` - taskai apskritime.
 *
 * ⚠️ Kampai: `linspace_array<N-1>(0, 2*PI)` (MTUtils.hpp:111) dalija is N-1,
 * NE is N-2, ir 2*PI NEITRAUKIA - todel taskai nesidubliuoja. Standartinis
 * linspace cia duotu kitus kampus.
 */
export class PointRing {
  constructor(n, N = 8) {
    this.N = N;
    const cnt = N - 1;
    const stride = (2 * Math.PI) / cnt;
    this.phis = new Array(cnt);
    for (let i = 0; i < cnt; i++) this.phis[i] = i * stride;

    /* Kai kryptis sutampa su viena is pasaulio asiu, du jos komponentai yra
       nuliai - dalyba is nulio. Originalas tada ima elementu poslinki. */
    const isOne = v => Math.abs(Math.abs(v) - 1) < 1e-20;
    if (isOne(n[0]) || isOne(n[1]) || isOne(n[2])) {
      this.a = [n[2], n[0], n[1]];
      this.b = [n[1], n[2], n[0]];
    } else {
      const a = [0, 1, 0];
      a[2] = -(n[1] * a[1]) / n[2];
      this.a = normalized(a);
      this.b = cross(this.a, n);
    }
  }

  get(idx, src, r) {
    if (idx === 0) return src;
    const phi = this.phis[idx - 1];
    const rc = r * Math.cos(phi), rs = r * Math.sin(phi);
    return [src[0] + rc * this.a[0] + rs * this.b[0],
            src[1] + rc * this.a[1] + rs * this.b[1],
            src[2] + rc * this.a[2] + rs * this.b[2]];
  }
}

/* --------------------------------------------------------------- Beam
 *
 * `Beam_` (STU.hpp:120-146): spinduliu pluostas kugio pavirsiumi. `r2` yra
 * spindulys VIENO VIENETO atstumu nuo `src` kryptimi `dir` - ne pabaigoje.
 */
export class Beam {
  constructor(src, dir, r1, r2) {
    this.src = src; this.dir = dir; this.r1 = r1;
    this.r2 = r2 === undefined ? r1 : r2;
  }

  /** `Beam_(const Ball&, const Ball&)` - is dvieju rutuliu. */
  static fromBalls(srcBall, dstBall) {
    const b = new Beam(srcBall.p, dirv(srcBall.p, dstBall.p), srcBall.R, srcBall.R);
    const d = distance3(srcBall.p, dstBall.p);
    if (d > EPSILON) b.r2 = srcBall.R + (dstBall.R - srcBall.R) / d;
    return b;
  }
}

export const Ball = (p, R) => ({ p, R });

/* --------------------------------------------------- beam_mesh_hit
 *
 * `beam_mesh_hit` (STU.hpp:150-194). Meta RayCount spinduliu ziedu ir grazina
 * artimiausia pataikyma. Vieno spindulio neuztenka: plonas strypas gali
 * prasprusti pro asi, o pluostas pagauna ir tai, ka jis realiai uzkabintu.
 *
 * `mesh` turi tureti `rayHit(o, d) -> {dist, inside}`.
 */
export function beamMeshHit(mesh, beam, sd, rayCount = 8) {
  const src = beam.src;
  const dst = add(src, beam.dir);
  const rSrc = beam.r1, rDst = beam.r2;
  const dir = normalized(sub(dst, src));
  const ring = new PointRing(dir, rayCount);

  const hits = new Array(rayCount);
  for (let i = 0; i < rayCount; i++) {
    const pSrc = ring.get(i, src, rSrc + sd);
    const pDst = ring.get(i, dst, rDst + sd);
    const raydir = normalized(sub(pDst, pSrc));
    const hr = mesh.rayHit(add(pSrc, mul(raydir, rSrc)), raydir);

    if (hr.inside) {
      /* Pataikyta is vidaus. Jei toli - laikom, kad kelias uzblokuotas nuo pat
         pradzios (0.0); jei arti - permetam spinduli is isores. */
      if (hr.dist > 2 * rSrc + sd) hits[i] = Hit(0.0);
      else {
        const q = add(pSrc, mul(raydir, hr.dist + EPSILON));
        hits[i] = mesh.rayHit(q, raydir);
      }
    } else hits[i] = hr;
  }
  return minHit(hits);
}

/* --------------------------------------------------- pinhead_mesh_hit
 *
 * `pinhead_mesh_hit` (STU.hpp:196-278). Tikrina, ar GALVUTE telpa: meta
 * spindulius nuo pin ziedo i back ziedą, t. y. isilgai jos sonines
 * plokstumos, ir grazina artimiausia kliuti.
 *
 * ⚠️ SAMPLES = 16, ne 8. Originalo komentaras tiesiai sako, kodel: „8 is
 * almost ok, but to prevent rare cases of collision, 16 is necessary, which
 * makes the algorithm run about 60% longer". Tai samoningas pasirinkimas
 * kokybes naudai, ir portas ji perima - kitaip retkarciais praleistume
 * kolizija.
 *
 * Skirtumas nuo `beam_mesh_hit`: ten riba yra `2*r_src + sd`, cia - `rpin`.
 */
export function pinheadMeshHit(mesh, s, dir, rPin, rBack, width, sd) {
  const SAMPLES = 16;
  const rpin = rPin + sd, rback = rBack + sd;
  const spin = s;
  const sback = add(s, mul(dir, rPin + width + rBack));
  const ring = new PointRing(dir, SAMPLES);

  const hits = new Array(SAMPLES);
  for (let i = 0; i < SAMPLES; i++) {
    const ps = ring.get(i, spin, rpin);
    const p = ring.get(i, sback, rback);
    const n = normalized(sub(p, ps));
    const q = mesh.rayHit(add(ps, mul(n, sd)), n);

    if (q.inside) {
      /* Viduje ir toliau nei pin ziedas - reiskia taskas jau buvo detales
         viduje arba vietos aplink ji visai nera. Nulis priverčia funkcija
         grazinti bloga rezultata (zr. minHit gale). */
      if (q.dist > rpin) hits[i] = Hit(0.0);
      else {
        /* Permetam is isores. Poslinkis 2*sd, nes pradinis spindulys irgi
           turejo sd poslinki. */
        hits[i] = mesh.rayHit(add(ps, mul(n, q.dist + 2 * sd)), n);
      }
    } else hits[i] = q;
  }
  return minHit(hits);
}

/** `pinhead_mesh_hit(ex, mesh, head, sd)` (STU.hpp:283-289) - is Head objekto. */
export const pinheadMeshHitHead = (mesh, head, sd) =>
  pinheadMeshHit(mesh, head.pos, head.dir, head.rPinMm, head.rBackMm, head.widthMm, sd);

/* ------------------------------------------------- sferines koordinates
 * `Geometry.hpp:545-559`. Polar matuojamas nuo +Z: zemyn nukreipta normale
 * duoda polar ~PI, todel visos ribos originale uzrasytos kaip „PI - kazkas".
 */
export const dirToSpheric = n => [Math.acos(n[2]), Math.atan2(n[1], n[0])];
export const sphericToDir = (polar, azimuth) => [
  Math.cos(azimuth) * Math.sin(polar),
  Math.sin(azimuth) * Math.sin(polar),
  Math.cos(polar),
];

/* ------------------------------------------------- krypties paieska
 *
 * Originalas cia leidzia NLopt `AlgNLoptMLSL_Subplx` - globalu multistart
 * optimizatoriu (STU.hpp:377-395) su DVIEM stabdikliais:
 *   `stop_score(w)`   - sustok, kai rezultatas jau geresnis uz reikalauja ma,
 *   `max_iterations(100)`.
 *
 * ⚠️ TAI IR YRA GREICIO RAKTAS. Musu ankstesne versija dare tinklelio apzvalga
 * per visus rezius ir skaiciavo VISUS variantus - del to viena si vieta
 * pabrangino pjaustyma nuo 3 s iki 49 s. Originalas nustoja ieskoti, kai tik
 * randa PAKANKAMA kryptį, o ne geriausia.
 *
 * NLopt JS neturim, tad imam ta pati princip a: multistart + Nelder-Mead, ir
 * ankstyvas nutraukimas ties `stopScore`. Tikslas ne atkartoti ta pati taska
 * (globalus optimizatorius ir originale nera deterministinis tarp versiju), o
 * atkartoti KRITERIJŲ: rasti bet kuria kryptį, kuria galvute telpa.
 */
export function optimize(rawFn, x0, bounds, opts = {}) {
  /* Originale kryptis nurodoma `to_max()` / `to_min()`. Minimizavima verciam i
     maksimizavima, kad viduje liktu viena kilpa - matematiskai tas pats. */
  const minimize = !!opts.minimize;
  const fn = minimize ? x => -rawFn(x) : rawFn;
  const stopScore = minimize ? -opts.stopScore : opts.stopScore;
  const maxIter = opts.maxIter === undefined ? 100 : opts.maxIter;
  const seed = opts.seed === undefined ? 0 : opts.seed;
  const r = maximizeUntil(fn, x0, bounds, stopScore, maxIter, seed + 1);
  return { x: r.x, score: minimize ? -r.score : r.score, iters: r.iters };
}

export function maximizeUntil(fn, x0, bounds, stopScore, maxIter = 100, seed = 1) {
  /* Determinizmas: originalas kviecia `solver.seed(0)`, tad ir mes einam nuo
     fiksuoto seed'o - kitaip tas pats modelis duotu skirtingas atramas. */
  let rnd = seed >>> 0;
  const rand = () => ((rnd = (rnd * 1664525 + 1013904223) >>> 0) / 4294967296);
  const clamp = x => x.map((v, i) => Math.min(bounds[i][1], Math.max(bounds[i][0], v)));

  let best = clamp(x0.slice()), bestVal = fn(best);
  if (bestVal > stopScore) return { x: best, score: bestVal, iters: 0 };

  let iters = 1;
  const N = x0.length;
  /* Biudzetas yra KIETAS: `max_iterations(100)` originale reiskia 100 tikslo
     funkcijos kvietimu, o kiekvienas jos kvietimas cia yra 16 spinduliu i
     mesh. Perzengus biudzeta viena si vieta ir pabrangino pjaustyma. */
  const call = x => { iters++; return fn(x); };
  const liko = () => maxIter - iters;

  /* Multistart: pirmas startas nuo turimo speji mo (normales), likusieji -
     atsitiktiniai. Biudzetas dalijamas, kad paskutiniam irgi liktu. */
  for (let start = 0; start < 6 && liko() > 0 && bestVal <= stopScore; start++) {
    let x = start === 0 ? best.slice()
                        : bounds.map(([lo, hi]) => lo + rand() * (hi - lo));
    let val = start === 0 ? bestVal : call(x);
    if (val > bestVal) { bestVal = val; best = x.slice(); }
    if (bestVal > stopScore) break;

    /* Koordinatinis nusileidimas mazejanciu zingsniu - Subplex dvasia be jo
       simplekso. Mums nereikia tikslaus optimumo, tik PERZENGTI riba. */
    let step = bounds.map(([lo, hi]) => (hi - lo) / 4);
    while (liko() > 0 && bestVal <= stopScore) {
      let pagerejo = false;
      for (let i = 0; i < N && liko() > 0 && bestVal <= stopScore; i++) {
        for (const sgn of [1, -1]) {
          if (liko() <= 0) break;
          const cand = x.slice();
          cand[i] = Math.min(bounds[i][1], Math.max(bounds[i][0], cand[i] + sgn * step[i]));
          const v = call(cand);
          if (v > val) {
            val = v; x = cand; pagerejo = true;
            if (v > bestVal) { bestVal = v; best = x.slice(); }
            break;                       // radom geryn - einam prie kito kintamojo
          }
        }
      }
      if (!pagerejo) {
        step = step.map(s => s / 2);
        /* Zingsnis nukrito zemiau prasmes - sis startas issisemė. */
        if (step.every((s, i) => s < (bounds[i][1] - bounds[i][0]) * 1e-3)) break;
      }
    }
  }
  return { x: best, score: bestVal, iters };
}
