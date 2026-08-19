/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/SupportPointGenerator.{hpp,cpp}
 */
import { scaled, unscaled, diffEx, intersectionEx, offsetEx, unionEx,
         exsToPaths, exArea, bbox, ApplySafetyOffset, DEFAULT_MITER_LIMIT } from './geometry.js';

/* SPG.cpp:1453-1464 - V profilyje ta pati kreive naudoja ir musu senasis kelias. */
export const createDefaultSupportCurve = () => [[3.2, 0], [4, 3.9], [5, 15], [6, 40]];

/** `SupportPointGeneratorConfig` (SPG.hpp:26-52). */
export const generatorConfig = (over = {}) => ({
  densityRelative: 1.0,          // support_points_density_relative / 100
  headDiameter: 0.4,             // [mm]
  supportCurve: createDefaultSupportCurve(),
  maxAllowedDistanceSq: scaled(1) * scaled(1),
  ...over,
});

/** `PrepareGeneratorDataConfig` (SPG.hpp:165-186). */
export const prepareConfig = (over = {}) => ({
  discretizeOverhangStep: 2.0,        // [mm]
  peninsulaWidth: scaled(2.0),        // peninsula_width [scaled mm]
  peninsulaSelfSupportedWidth: scaled(1.5),  // peninsula_self_supported_width
  minimalBoundingSphereRadius: 0.2,   // [mm]
  removingDelta: scaled(0.3),
  ...over,
});

/**
 * `intersection_line_circle` (SPG.cpp:174-206).
 * Kur atkarpa p1->p2 kerta apskritima (cnt, r2). Imamas MAZESNIS saknis (t1);
 * didesnis - tik atsarga, originalo komentaras sako „should not be in use".
 */
export function intersectionLineCircle(p1, p2, cnt, r2) {
  const dx = p2.X - p1.X, dy = p2.Y - p1.Y;
  const fx = p1.X - cnt.X, fy = p1.Y - cnt.Y;
  const a = dx * dx + dy * dy;
  const b = 2 * (fx * dx + fy * dy);
  const c = fx * fx + fy * fy - r2;
  let disc = b * b - 4 * a * c;
  if (disc < 0) return null;
  disc = Math.sqrt(disc);
  const t1 = (-b - disc) / (2 * a);
  if (t1 >= 0 && t1 <= 1) return { X: p1.X + t1 * dx, Y: p1.Y + t1 * dy };
  const t2 = (-b + disc) / (2 * a);
  if (t2 >= 0 && t2 <= 1 && t1 !== t2) return { X: p1.X + t2 * dx, Y: p1.Y + t2 * dy };
  return null;
}

/**
 * `sample` (SPG.cpp:361-385).
 *
 * Tolygus atkarpu sekos deliojimas: einama per taskus ir, kai atstumas nuo
 * paskutinio pademeto virsija `dist`, i ta vieta idedamas naujas taskas
 * TIKSLIAI ant linijos (per apskritimo sankirta), o ne ties virsune.
 *
 * ⚠️ `while`, ne `if`: viena ilga atkarpa gali duoti kelis taskus is eiles.
 */
export function sample(pts, from, to, dist2) {
  const r = [];
  if (to - from <= 0) return r;
  r.push(pts[from]);
  let prevPt = null;
  for (let i = from; i + 1 < to; i++) {
    const pt = pts[i + 1];
    let last = r[r.length - 1];
    let d2 = (last.X - pt.X) ** 2 + (last.Y - pt.Y) ** 2;
    while (d2 > dist2) {
      if (prevPt === null) prevPt = pts[i];
      const np = intersectionLineCircle(prevPt, pt, r[r.length - 1], dist2);
      if (!np) break;
      r.push(np);
      last = np;
      d2 = (last.X - pt.X) ** 2 + (last.Y - pt.Y) ** 2;
      prevPt = np;
    }
    prevPt = null;
  }
  return r;
}

/**
 * `contain_point` (SPG.cpp:389-397).
 *
 * ⚠️ ORIGINALO KEISTENYBE, portuota kaip yra. Po `lower_bound` daromas `++it`,
 * ir tada lyginama su ieskomu tasku - t. y. `true` grazinama tik tada, kai
 * taskas sarase yra DU KARTUS. Atrodo kaip klaida (natūralu butų lyginti
 * `*it`), bet `prev_points` sudaromas is keliu figuru, ir bendros virsunes ten
 * tikrai kartojasi. Istaisius pasikeistu, kurie konturo taskai laikomi
 * „sausuma", tad paliekam ir zymim - kaip ir Clustering bei ConcaveHull atvejus.
 */
export function containPoint(p, sortedPoints) {
  let lo = 0, hi = sortedPoints.length;
  while (lo < hi) {
    const m = (lo + hi) >> 1;
    const q = sortedPoints[m];
    if (q.X < p.X || (q.X === p.X && q.Y < p.Y)) lo = m + 1; else hi = m;
  }
  if (lo >= sortedPoints.length) return false;
  lo++;                                    // originalo `++it`
  if (lo >= sortedPoints.length) return false;
  return sortedPoints[lo].X === p.X && sortedPoints[lo].Y === p.Y;
}

const toPoints = exs => {
  const o = [];
  for (const e of exs) { for (const p of e.contour) o.push(p); for (const h of e.holes) for (const p of h) o.push(p); }
  return o;
};
const cmpPt = (a, b) => (a.X - b.X) || (a.Y - b.Y);

/**
 * `sample_overhangs` (SPG.cpp:409-480).
 *
 * Nuokaba = dabartine dalis MINUS visos ja laikancios apatines dalys. Kontūras
 * einamas ir skaidomas i atkarpas: taskai, kurie sutampa su apatiniu sluoksniu
 * („sausuma"), praleidziami, o likusios atkarpos sejamos tolygiai.
 *
 * ⚠️ `diff_ex(..., ApplySafetyOffset::Yes)` - butent cia. Musu sename
 * masteliyje (1e-3) tas ofsetas virsdavo nuliu ir nedarydavo NIEKO; portas eina
 * su originalo masteliu (1e-6), tad jis veikia.
 */
export function sampleOverhangs(CL, part, prevShapes, dist2) {
  if (!prevShapes.length) return [];
  const overhangs = diffEx(CL, exsToPaths([part.shape]), exsToPaths(prevShapes),
                           ApplySafetyOffset.Yes);
  if (!overhangs.length) return [];

  const prevPoints = toPoints(prevShapes).slice().sort(cmpPt);
  const samples = [];

  const sampleOne = polygon => {
    const pts = polygon;
    let firstBad = -1, startIt = -1;
    for (let i = 0; i < pts.length; i++) {
      if (containPoint(pts[i], prevPoints)) {
        if (firstBad === -1) firstBad = i;
        if (startIt !== -1) { samples.push(...sample(pts, startIt, i, dist2)); startIt = -1; }
      } else if (startIt === -1) startIt = i;
    }
    /* Uodegos apdorojimas - keturi atvejai, kaip originale. */
    if (startIt === -1) {
      if (firstBad !== 0 && firstBad > 0) samples.push(...sample(pts, 0, firstBad, dist2));
    } else if (firstBad === 0) {
      samples.push(...sample(pts, startIt, pts.length, dist2));
    } else if (startIt === 0) {
      const pts2 = pts.concat([pts[0]]);
      samples.push(...sample(pts2, 0, pts2.length, dist2));
    } else {
      const pts2 = pts.slice(startIt).concat(pts.slice(0, firstBad < 0 ? 0 : firstBad));
      samples.push(...sample(pts2, 0, pts2.length, dist2));
    }
  };

  for (const ov of overhangs) { sampleOne(ov.contour); for (const h of ov.holes) sampleOne(h); }
  return samples;
}

/**
 * `prepare_generator_data` (SPG.cpp:963-1077).
 *
 * Sluoksniai -> dalys -> rysiai tarp sluoksniu -> nuokabu diskretizavimas.
 * Rysiai daromi TIKRA sankirta, ne gabaritais: gabaritai persidengia beveik
 * visada, ir tada viskas susisietu su viskuo.
 */
export function prepareGeneratorData(CL, slices, heights, cfg = prepareConfig()) {
  const layers = slices.map((islands, i) => ({
    printZ: heights[i],
    parts: islands.map(island => ({
      shape: island, extendShape: [], shapeExtent: bbox(exsToPaths([island])),
      samples: [], prevParts: [], nextParts: [], peninsulas: [],
    })),
  }));

  /* Dalys siejamos su apatinemis TIKRA sankirta. */
  for (let li = 1; li < layers.length; li++) {
    const above = layers[li].parts, below = layers[li - 1].parts;
    for (const a of above) for (const b of below) {
      const [ax0, ay0, ax1, ay1] = a.shapeExtent, [bx0, by0, bx1, by1] = b.shapeExtent;
      if (ax1 < bx0 || ax0 > bx1 || ay1 < by0 || ay0 > by1) continue;
      const inter = intersectionEx(CL, exsToPaths([a.shape]), exsToPaths([b.shape]));
      if (!inter.length) continue;
      a.prevParts.push(b);
      b.nextParts.push(a);
    }
  }

  /* Nuokabu diskretizavimas. Salos (be prevParts) praleidziamos - jos eina
     kitu keliu (uniform_support_island). */
  const dist = scaled(cfg.discretizeOverhangStep);
  const dist2 = dist * dist;
  for (let li = 1; li < layers.length; li++)
    for (const part of layers[li].parts) {
      if (!part.prevParts.length) continue;
      part.samples = sampleOverhangs(CL, part, part.prevParts.map(p => p.shape), dist2);
    }

  /* Pusiasaliai (SPG.cpp:1055-1066). */
  for (let li = 1; li < layers.length; li++)
    for (const part of layers[li].parts) {
      if (!part.prevParts.length) continue;
      createPeninsulas(CL, part, part.prevParts.map(p => p.shape), cfg);
    }

  /* `extend_shape` - naudojamas atmetant nereikalingus taskus. */
  for (let li = 1; li < layers.length; li++)
    for (const part of layers[li].parts)
      part.extendShape = offsetEx(CL, exsToPaths([part.shape]), cfg.removingDelta,
                                  CL.JoinType.jtSquare, DEFAULT_MITER_LIMIT);

  return { slices, layers };
}

/* ------------------------------------------------------------ pusiasaliai */

const toLines = exs => {
  const o = [];
  const push = ring => { for (let i = 0; i < ring.length; i++) o.push([ring[i], ring[(i + 1) % ring.length]]); };
  for (const e of exs) { push(e.contour); for (const h of e.holes) push(h); }
  return o;
};

/** Kampas su TEIGIAMA X kryptimi (SPG.cpp:585-590). */
const lineAngle = l => {
  let dx = l[1].X - l[0].X, dy = l[1].Y - l[0].Y;
  if (dx < 0) { dx = -dx; dy = -dy; }
  return Math.atan2(dy, dx);
};

/** Statmenas atstumas nuo tasko iki tieses per l. */
const perpDistance = (l, p) => {
  const dx = l[1].X - l[0].X, dy = l[1].Y - l[0].Y;
  const L = Math.hypot(dx, dy);
  if (L < 1e-12) return Math.hypot(p.X - l[0].X, p.Y - l[0].Y);
  return Math.abs((p.X - l[0].X) * dy - (p.Y - l[0].Y) * dx) / L;
};

/**
 * `create_peninsulas` (SPG.cpp:567-681).
 *
 * Pusiasalis - dalis, kuri isikisusi UZ apatinio sluoksnio ribu. Skiriamos dvi
 * ribos: `peninsula_min_width` (ar isikisimas apskritai vertas demesio) ir
 * `peninsula_self_supported_width` (nuo kur laikoma, kad plotis pats save
 * islaiko).
 *
 * Svarbiausia dalis - kiekvienai pusiasalio kraStinei nustatyti, ar ji yra
 * KRANTAS (nuokabos briauna), ar SANDURA su sausuma. Tai daroma lyginant su
 * apatinio sluoksnio linijomis pagal KAMPĄ (surusiuotą) ir statmenĄ atstumĄ,
 * o ne pagal tapatybe - nes po ofseto virsuniu tapatybes nebelieka.
 *
 * ⚠️ Kampu paieska „apsisuka" per ±PI/2 (`is_over`): linija, kurios kampas
 * arti ribos, turi buti lyginama ir su kitu sarasо galu.
 */
export function createPeninsulas(CL, part, prevShapes, cfg) {
  if (!prevShapes.length) return;
  const belowPaths = exsToPaths(prevShapes);
  const belowExpanded = offsetEx(CL, belowPaths, cfg.peninsulaWidth, CL.JoinType.jtSquare, DEFAULT_MITER_LIMIT);
  const overPeninsula = diffEx(CL, exsToPaths([part.shape]), exsToPaths(belowExpanded));
  if (!overPeninsula.length) return;          // tik smulkios nuokabos

  const selfSup = cfg.peninsulaSelfSupportedWidth === undefined
    ? Math.round(cfg.peninsulaWidth * 0.75) : cfg.peninsulaSelfSupportedWidth;
  const belowSelfSupported = offsetEx(CL, belowPaths, selfSup, CL.JoinType.jtSquare, DEFAULT_MITER_LIMIT);
  const peninsulasShape = diffEx(CL, exsToPaths([part.shape]), exsToPaths(belowSelfSupported));
  if (!peninsulasShape.length) return;

  const belowLines = toLines(belowSelfSupported);
  const belowAngle = belowLines.map(lineAngle);
  const idx = belowLines.map((_, i) => i).sort((a, b) => belowAngle[a] - belowAngle[b]);

  const ANGLE_EPS = 1e-3;
  const PARALEL_EPS = scaled(1e-2);           // 10 um

  const existBelowe = l => {
    if (!belowLines.length) return false;
    const angle = lineAngle(l);
    let lowAngle = angle - ANGLE_EPS, hiAngle = angle + ANGLE_EPS;
    let isOver = false;
    if (lowAngle <= -Math.PI / 2) { lowAngle += Math.PI; isOver = true; }
    if (hiAngle >= Math.PI / 2) { hiAngle -= Math.PI; isOver = true; }

    const dxa = l[0].X - l[1].X, dya = l[0].Y - l[1].Y;
    const mj = Math.abs(dxa) < Math.abs(dya) ? 'Y' : 'X';
    let low = l[0][mj], high = l[1][mj];
    if (low > high) { const t = low; low = high; high = t; }

    /* lower_bound pagal kampa */
    let lo = 0, hi = idx.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (belowAngle[idx[m]] < lowAngle) lo = m + 1; else hi = m; }
    let it = lo;
    if (it >= idx.length) { if (isOver) { it = 0; isOver = false; } else return false; }

    while (isOver || belowAngle[idx[it]] < hiAngle) {
      const l2 = belowLines[idx[it]];
      const l2low = Math.min(l2[0][mj], l2[1][mj]);
      const l2high = Math.max(l2[0][mj], l2[1][mj]);
      const ta = l2[0], tb = l2[1];
      const same = (ta.X === l[0].X && ta.Y === l[0].Y && tb.X === l[1].X && tb.Y === l[1].Y) ||
                   (ta.X === l[1].X && ta.Y === l[1].Y && tb.X === l[0].X && tb.Y === l[0].Y);
      if (l2high >= low && l2low <= high && (same || perpDistance(l, l2[0]) < PARALEL_EPS))
        return true;
      it++;
      if (it >= idx.length) { if (isOver) { it = 0; isOver = false; } else break; }
    }
    return false;
  };

  for (const pen of peninsulasShape) {
    /* Pusiasalis turi buti pakankamai platus - persidengti su `over_peninsula`. */
    const inter = intersectionEx(CL, exsToPaths([pen]), exsToPaths(overPeninsula));
    if (!inter.length) continue;
    const lines = toLines([pen]);
    const isOutline = lines.map(l => !existBelowe(l));
    part.peninsulas.push({ unsupportedArea: pen, isOutline, lines });
  }
}
