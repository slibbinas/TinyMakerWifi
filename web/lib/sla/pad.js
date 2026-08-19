/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/Pad.{hpp,cpp}
 *
 * Portuojama tik 2D KONTURO grandine. Originalo `create_pad` gamina 3D mesh
 * (sieneles, trianguliacija), o musu kelyje geometrija piesiama tiesiai i
 * sluoksnius - 3D dalies tiesiog nera kur naudoti.
 */
import { scaled, unionEx, diffEx, offsetEx, exsToPaths, exArea,
         ApplySafetyOffset, DEFAULT_MITER_LIMIT } from './geometry.js';
import { ConcaveHull, offsetWaffleStyleEx } from './concave-hull.js';
import { sliceMeshEx } from './mesh-slicer.js';

/** `PadConfig` (Pad.hpp:44-90). Reiksmes is V profilio. */
export function padConfig(over = {}) {
  const cfg = {
    wallThicknessMm: 0.15,      // pad_wall_thickness
    wallHeightMm: 0,            // pad_wall_height
    maxMergeDistMm: 50,         // pad_max_merge_distance
    wallSlope: 90 * Math.PI / 180,   // pad_wall_slope
    brimSizeMm: 1.6,            // pad_brim_size
    embedObject: {
      objectGapMm: 1.0,             // pad_object_gap
      stickStrideMm: 10,            // pad_object_connector_stride
      stickWidthMm: 0.5,            // pad_object_connector_width
      stickPenetrationMm: 0.3,      // pad_object_connector_penetration
      enabled: true,                // pad_around_object
      everywhere: false,            // pad_around_object_everywhere
    },
    ...over,
  };
  return cfg;
}

/* Pad.hpp:75-88. Su musu `wall_slope = 90 grad` tangentas yra begalybe, tad
   `wingDistance` ir `bottomOffset` iseina NULIAI - pado sienos nera, lieka tik
   0,15 mm storio plokstele su apvadu. */
export const bottomOffset = c => (c.wallThicknessMm + c.wallHeightMm) / Math.tan(c.wallSlope);
export const wingDistance = c => c.wallHeightMm / Math.tan(c.wallSlope);
export const fullHeight = c => c.wallHeightMm + c.wallThicknessMm;
export const requiredElevation = c => c.wallThicknessMm;

/** `get_waffle_offset` (Pad.cpp:141-144). */
export const waffleOffset = c => scaled(c.brimSizeMm + wingDistance(c));
/** `get_merge_distance` (Pad.cpp:146-149). */
export const mergeDistance = c => 2 * (1.8 * c.wallThicknessMm) + c.maxMergeDistMm;

/**
 * `breakstick_holes` (Pad.cpp:60-127).
 *
 * Per kontura kas `stride` iterpiami stacziakampiai „kaisciai", kurie iLENDA i
 * detale per `penetration + padding`. Del ju padas laiko detale, bet
 * atsiskiria - lieciasi tik siauromis juostelemis, ne visu perimetru.
 *
 * ⚠️ `t` NENUNULINAMAS tarp kraStiniu: gale daroma `t = t - nrm`, tad zingsnis
 * tesiasi per visa perimetra vientisai. Nunulinus kaisciai susigrustu ties
 * kiekviena virsune.
 */
export function breakstickHoles(pts, padding, stride, stickWidth, penetration) {
  const EPS = 1e-9;
  if (stride <= EPS || stickWidth <= EPS || padding <= EPS) return pts;

  const out = [];
  const sbottom = scaled(stickWidth);
  const sright = scaled(penetration + padding);
  const sstride = scaled(stride);
  let t = 0;

  for (let i = pts.length - 1, j = 0; j < pts.length; i = j, j++) {
    const a = pts[i], b = pts[j];
    let dx = b.X - a.X, dy = b.Y - a.Y;
    const nrm = Math.hypot(dx, dy);
    if (nrm < EPS) { out.push({ X: a.X, Y: a.Y }); continue; }
    dx /= nrm; dy /= nrm;
    const px = -dy, py = dx;                   // statmuo

    out.push({ X: a.X, Y: a.Y });

    /* Aplenkiam pradzios taska - kaisciu ant sanduru nedarom. */
    while (t < sbottom) t += sbottom;
    const tend = nrm - sbottom;

    while (t < tend) {
      const p1 = { X: (a.X + t * dx) | 0, Y: (a.Y + t * dy) | 0 };
      const p2 = { X: (p1.X + sright * px) | 0, Y: (p1.Y + sright * py) | 0 };
      const p3 = { X: (p2.X + sbottom * dx) | 0, Y: (p2.Y + sbottom * dy) | 0 };
      const p4 = { X: (p3.X - sright * px) | 0, Y: (p3.Y - sright * py) | 0 };
      out.push(p1, p2, p3, p4);
      t += sstride;
    }
    t = t - nrm;
    out.push({ X: b.X, Y: b.Y });
  }
  return out;
}

const breakstickEx = (exs, ...args) => exs.map(e => ({
  contour: breakstickHoles(e.contour, ...args),
  holes: e.holes.map(h => breakstickHoles(h, ...args)),
}));

/**
 * `pad_blueprint` (Pad.cpp:478-516): modelio siluetas, gautas supjausciu ji per
 * pirmus `h` mm ir suliejus.
 */
export function padBlueprint(CL, pos, gnd, h = 0.1, layerh = 0.05) {
  const zs = [];
  for (let z = gnd; z < gnd + h; z += layerh) zs.push(z);
  if (!zs.length) zs.push(gnd);
  const sl = sliceMeshEx(CL, pos, zs);
  const visi = [];
  for (const ex of sl) for (const e of ex) { visi.push(e.contour); for (const hh of e.holes) visi.push(hh); }
  return visi.length ? unionEx(CL, visi) : [];
}

/**
 * `_AroundPadSkeleton` 2D dalis (Pad.cpp:250-281) - musu profilio atvejis
 * (`pad_around_object = 1`).
 *
 * Grandine: modelio siluetas praplecziamas per tarpa -> kartu su atramomis
 * paduodamas i ConcaveHull -> waffle ofsetas -> is jo ATIMAMAS modelis su
 * kaisciais -> ismetamos dalys, po kuriomis nera nei vienos atramos.
 */
export function padContour(CL, supportBlueprint, modelBlueprint, cfg) {
  const emb = cfg.embedObject;

  const modelBpOffs = modelBlueprint.length
    ? offsetEx(CL, exsToPaths(modelBlueprint), scaled(emb.objectGapMm), CL.JoinType.jtMiter, 1)
    : [];

  /* ConcaveHull ima tik ISORINIUS konturus - ir atramu, ir modelio. */
  const allin = [];
  for (const e of supportBlueprint) allin.push(e.contour);
  for (const e of modelBpOffs) allin.push(e.contour);
  if (!allin.length) return [];

  const cchull = new ConcaveHull(CL, allin, mergeDistance(cfg));
  const fullcvh = offsetWaffleStyleEx(CL, cchull, waffleOffset(cfg));

  const sticks = breakstickEx(modelBpOffs, emb.objectGapMm, emb.stickStrideMm,
                              emb.stickWidthMm, emb.stickPenetrationMm);

  const fullpad = sticks.length
    ? diffEx(CL, exsToPaths(fullcvh), exsToPaths(sticks), ApplySafetyOffset.No)
    : fullcvh;

  /* `remove_redundant_parts` (Pad.cpp:307-316): dalis lieka tik tada, kai po ja
     yra bent viena atrama. `everywhere = 0` reiskia, kad padas dedamas ne po
     visa detale, o tik ten, kur jos reikia. */
  if (emb.everywhere) return fullpad;
  return fullpad.filter(p => {
    for (const s of supportBlueprint) {
      const inter = diffEx(CL, [s.contour], [p.contour], ApplySafetyOffset.No);
      const liko = inter.reduce((a, e) => a + exArea(CL, e), 0);
      const visas = exArea(CL, { contour: s.contour, holes: [] });
      if (liko < visas - 1) return true;       // kazkas persidenge
    }
    return false;
  });
}

/** Pilna grandine: is medzio ir modelio - pado konturas. */
export function createPadContour(CL, pos, tree, cfg, gnd = 0, supportDiscs = null) {
  const modelBp = padBlueprint(CL, pos, gnd, requiredElevation(cfg) + 0.05, 0.05);
  const suppBp = supportDiscs || [];
  return padContour(CL, suppBp, modelBp, cfg);
}
