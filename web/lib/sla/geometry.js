/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * This file is part of a derivative work of PrusaSlicer and is therefore
 * licensed under the GNU Affero General Public License v3 or later.
 * See LICENSE-AGPL.md in this directory. The rest of TinyMakerWiFi (firmware,
 * dashboard) stays MIT; only this directory carries AGPL.
 *
 * Portuota is: src/libslic3r/ClipperUtils.{hpp,cpp}
 */

/* ------------------------------------------------------------------ mastelis
 *
 * ⚠️ SVARBIAUSIA SIO FAILO EILUTE.
 *
 * libslic3r: SCALING_FACTOR = 0.000001, t. y. `scale_(mm) = mm * 1e6`, ir
 * vienas Clipper vienetas yra NANOMETRAS.
 *
 * Musu senasis `slicer2.js` naudoja SCALE = 1000 (vienetas = mikrometras), ir
 * del to viena originalo savoka atkartoti buvo FIZISKAI neimanoma:
 * `ClipperSafetyOffset = 10` vienetu tame mastelyje virsta 0,01, o Clipper
 * dirba sveikais skaiciais - t. y. nuliu. Todel `ApplySafetyOffset::Yes`,
 * kuri originalas naudoja `diff_ex`/`intersection_ex` iskvietimuose, pas mus
 * tiesiog nieko nedarydavo.
 *
 * Portas eina su originalo masteliu. Skaiciai lieka int64 riboje: plokste
 * 40,8 mm -> 4,08e7 vienetu, o Clipper riba yra ~4,6e18.
 */
export const SCALING_FACTOR = 0.000001;
export const scaled = mm => Math.round(mm / SCALING_FACTOR);
export const unscaled = u => u * SCALING_FACTOR;

/* ClipperUtils.hpp:47. Komentaras originale sako „10um", bet reiksme yra
   10 VIENETU, t. y. 10 nm - komentaras senas, is laiku kai mastelis buvo
   kitoks. Imam reiksme, ne komentara. */
export const CLIPPER_SAFETY_OFFSET = 10;

/* ClipperUtils.hpp:49-50 */
export const DEFAULT_MITER_LIMIT = 3.0;

export const ApplySafetyOffset = { No: 0, Yes: 1 };

/* ------------------------------------------------------------------ tipai
 *
 * ExPolygon = { contour: Path, holes: Path[] }, kaip libslic3r. Path yra
 * ClipperLib kelias - masyvas {X, Y} su sveikais skaiciais.
 */
export const exPolygon = (contour, holes = []) => ({ contour, holes });

/** Visi ExPolygon keliai vienu masyvu - Clipper'iui paduoti. */
export const exToPaths = ex => (ex.holes.length ? [ex.contour, ...ex.holes] : [ex.contour]);
export const exsToPaths = exs => { const o = []; for (const e of exs) { o.push(e.contour); for (const h of e.holes) o.push(h); } return o; };

/* ------------------------------------------------------- PolyTree -> ExPolygons
 *
 * Pazodinis `PolyTreeToExPolygons` (ClipperUtils.cpp:211-245) atitikmuo.
 *
 * Esme, kuria lengva praleisti: kontūrai, gulintys SKYLES viduje, tampa
 * ATSKIRAIS ExPolygon'ais, ne tos pacios dalimi. Todel rekursija eina per
 * `Childs[i]->Childs[j]` - anuko lygi, ne vaiko.
 */
export function polyTreeToExPolygons(CL, polytree) {
  const out = [];
  /* C++ `polynode.Contour` yra laukas, o clipper.js jis yra METODAS
     (`PolyNode.prototype.Contour()`, clipper.js:1964). Skirtumas tylus:
     laukas grazintu funkcija, plotas iseitu nulis, ir atrodytu, kad
     geometrija tuscia. */
  const rec = node => {
    const ex = exPolygon(node.Contour(), []);
    out.push(ex);
    const kids = node.Childs();
    for (let i = 0; i < kids.length; i++) {
      ex.holes.push(kids[i].Contour());
      const grand = kids[i].Childs();
      for (let j = 0; j < grand.length; j++) rec(grand[j]);
    }
  };
  for (const c of polytree.Childs()) rec(c);
  return out;
}

/* ------------------------------------------------------------------ ofsetas */

/** `raw_offset` (ClipperUtils.cpp): ClipperOffset be jokio valymo po jo. */
function rawOffset(CL, paths, delta, joinType, miterLimit) {
  const co = new CL.ClipperOffset(miterLimit, 0);
  co.AddPaths(paths, joinType, CL.EndType.etClosedPolygon);
  const out = new CL.Paths();
  co.Execute(out, delta);
  return out;
}

/** `safety_offset` (ClipperUtils.cpp:341-345) - isplecia CLIP puse per 10 vienetu. */
export const safetyOffset = (CL, paths) =>
  rawOffset(CL, paths, CLIPPER_SAFETY_OFFSET, CL.JoinType.jtMiter, DEFAULT_MITER_LIMIT);

/**
 * `offset_ex` (ClipperUtils.cpp:455-456): ofsetas -> PolyTree -> ExPolygons.
 * Pastaba: ClipperOffset viduje jau daro union, tad papildomo nereikia.
 */
export function offsetEx(CL, paths, delta, joinType, miterLimit) {
  const co = new CL.ClipperOffset(miterLimit === undefined ? DEFAULT_MITER_LIMIT : miterLimit, 0);
  co.AddPaths(paths, joinType === undefined ? CL.JoinType.jtMiter : joinType, CL.EndType.etClosedPolygon);
  const tree = new CL.PolyTree();
  co.Execute(tree, delta);
  return polyTreeToExPolygons(CL, tree);
}

/* ------------------------------------------------------------------ booleans
 *
 * `clipper_do` (ClipperUtils.cpp:348-377). Saugos ofsetas taikomas TIK clip
 * pusei, ir tik ne-union operacijoms (originale tai assert'as).
 */
function clipperDo(CL, clipType, subject, clip, fillType, doSafety) {
  const c = new CL.Clipper();
  c.AddPaths(subject, CL.PolyType.ptSubject, true);
  const cl = doSafety === ApplySafetyOffset.Yes ? safetyOffset(CL, clip) : clip;
  if (cl.length) c.AddPaths(cl, CL.PolyType.ptClip, true);
  const tree = new CL.PolyTree();
  c.Execute(clipType, tree, fillType, fillType);
  return tree;
}

const NONZERO = CL => CL.PolyFillType.pftNonZero;

export const diffEx = (CL, subject, clip, doSafety = ApplySafetyOffset.No) =>
  polyTreeToExPolygons(CL, clipperDo(CL, CL.ClipType.ctDifference, subject, clip, NONZERO(CL), doSafety));

export const intersectionEx = (CL, subject, clip, doSafety = ApplySafetyOffset.No) =>
  polyTreeToExPolygons(CL, clipperDo(CL, CL.ClipType.ctIntersection, subject, clip, NONZERO(CL), doSafety));

/** `clipper_union` (ClipperUtils.cpp:380-392) - pftNonZero pagal nutylejima. */
export function unionEx(CL, subject, fillType) {
  const c = new CL.Clipper();
  c.AddPaths(subject, CL.PolyType.ptSubject, true);
  const tree = new CL.PolyTree();
  const ft = fillType === undefined ? CL.PolyFillType.pftNonZero : fillType;
  c.Execute(CL.ClipType.ctUnion, tree, ft, ft);
  return polyTreeToExPolygons(CL, tree);
}

/** `union_safety_offset_ex` (ClipperUtils.hpp:380) = offset_ex(+10). */
export const unionSafetyOffsetEx = (CL, paths) =>
  offsetEx(CL, paths, CLIPPER_SAFETY_OFFSET, CL.JoinType.jtMiter, DEFAULT_MITER_LIMIT);

/* ------------------------------------------------------------------ matai */

/** Kelio plotas (Clipper vienetais kvadratu). Skyles gaunasi neigiamos. */
export const pathArea = (CL, path) => CL.Clipper.Area(path);

/** ExPolygon plotas: konturas minus skyles. */
export function exArea(CL, ex) {
  let a = Math.abs(CL.Clipper.Area(ex.contour));
  for (const h of ex.holes) a -= Math.abs(CL.Clipper.Area(h));
  return a;
}

/** Ar taskas ExPolygon viduje (konturas taip, skyles ne). */
export function exContains(CL, ex, x, y) {
  const p = new CL.IntPoint(x, y);
  if (CL.Clipper.PointInPolygon(p, ex.contour) === 0) return false;
  for (const h of ex.holes) if (CL.Clipper.PointInPolygon(p, h) !== 0) return false;
  return true;
}

/** Gabaritai [minX, minY, maxX, maxY] Clipper vienetais. */
export function bbox(paths) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of paths) for (const q of p) {
    if (q.X < x0) x0 = q.X; if (q.X > x1) x1 = q.X;
    if (q.Y < y0) y0 = q.Y; if (q.Y > y1) y1 = q.Y;
  }
  return [x0, y0, x1, y1];
}
