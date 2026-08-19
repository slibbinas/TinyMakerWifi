/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/ConcaveHull.{hpp,cpp}
 */
import { scaled, unionEx, exsToPaths, offsetEx, polyTreeToExPolygons,
         DEFAULT_MITER_LIMIT } from './geometry.js';

/**
 * `ConcaveHull::centroid` (CH.cpp:18-42).
 *
 * ⚠️ Tai NE masiu centras. Tai GABARITU vidurys: imamas min/max ir dalijama
 * pusiau. Skiriasi nuo tikro centroido, ir butent taip originale.
 */
export function centroid(pp) {
  if (!pp.length) return { X: 0, Y: 0 };
  if (pp.length === 1) return { X: pp[0].X, Y: pp[0].Y };
  if (pp.length === 2) return { X: (pp[0].X + pp[1].X) / 2 | 0, Y: (pp[0].Y + pp[1].Y) / 2 | 0 };
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  for (const p of pp) {
    if (p.X < x0) x0 = p.X; if (p.X > x1) x1 = p.X;
    if (p.Y < y0) y0 = p.Y; if (p.Y > y1) y1 = p.Y;
  }
  return { X: x0 + ((x1 - x0) / 2 | 0), Y: y0 + ((y1 - y0) / 2 | 0) };
}

const dist2 = (a, b) => Math.hypot(a.X - b.X, a.Y - b.Y);

/**
 * `ConcaveHull` (CH.cpp:112-126).
 *
 * Salos sujungiamos ne apvalkalu, o TRIKAMPIAIS („connector rectangles"),
 * vedamais nuo bendro centro i kiekvienos salos centra - bet tik tada, kai
 * artimiausia kaimyne yra ARCIAU nei `mergedist`. Toliau esancios salos lieka
 * atskiros, ir padas po jomis irgi.
 */
export class ConcaveHull {
  constructor(CL, polys, mergedist) {
    this.CL = CL;
    this.polys = [];
    if (!polys.length) return;

    this.polys = polys.map(p => p.slice());
    this.mergePolygons();
    if (this.polys.length === 1) return;

    const centroids = this.polys.map(p => centroid(p));
    this.addConnectorRectangles(centroids, scaled(mergedist));
    this.mergePolygons();
  }

  /** `merge_polygons` - union ir tik ISORINIAI konturai. */
  mergePolygons() {
    if (!this.polys.length) return;
    const ex = unionEx(this.CL, this.polys);
    this.polys = ex.map(e => e.contour);
  }

  /** `add_connector_rectangles` (CH.cpp:58-110). */
  addConnectorRectangles(centroids, maxDist) {
    const cc = centroid(centroids);

    for (let idx = 0; idx < centroids.length; idx++) {
      const c = centroids[idx];
      const dx = c.X - cc.X, dy = c.Y - cc.Y;
      const l = Math.hypot(dx, dy);
      if (!l) continue;
      const nx = dx / l, ny = dy / l;

      /* Artimiausia KITA sala. Originale `nearest(ct, 2)` - du artimiausi,
         is kuriu vienas yra jis pats. */
      let dist = maxDist;
      let bd = Infinity, bi = -1;
      for (let j = 0; j < centroids.length; j++) {
        if (j === idx) continue;
        const d = dist2(centroids[j], c);
        if (d < bd) { bd = d; bi = j; }
      }
      if (bi >= 0) dist = bd;

      /* ⚠️ Originale cia `return`, ne `continue` - t. y. radus VIENA sala,
         kurios kaimyne per toli, ciklas nutraukiamas VISAI ir likusios salos
         jungciu nebegauna. Atrodo kaip klaida, bet portuojam kaip yra. */
      if (dist >= maxDist) return;

      const n = { X: scaled(nx), Y: scaled(ny) };
      const tri = [
        { X: cc.X, Y: cc.Y },
        { X: c.X + n.Y, Y: c.Y - n.X },
        { X: c.X - n.Y, Y: c.Y + n.X },
      ];
      /* `offset(r, scaled(1.))` - trikampis praplecziamas 1 mm. */
      const grown = offsetEx(this.CL, [tri], scaled(1), this.CL.JoinType.jtMiter, DEFAULT_MITER_LIMIT);
      for (const e of grown) this.polys.push(e.contour);
    }
  }

  toExPolygons() { return this.polys.map(p => ({ contour: p, holes: [] })); }
}

/**
 * `offset_waffle_style` (CH.cpp:141-151).
 *
 * `closing(polys, 2*delta, delta, jtRound)` - isplecia per 2*delta ir traukia
 * atgal per delta, t. y. GRYNAS pletimas per delta su uzpildytomis siauromis
 * tarpuertmemis. Tada ismetami laikrodzio kryptimi einantys keliai (skyles).
 */
export function offsetWaffleStyle(CL, hull, delta) {
  const polys = hull.polygons ? hull.polygons() : hull.polys;
  if (!polys.length) return [];
  const arcTolerance = scaled(0.01);

  const co1 = new CL.ClipperOffset(DEFAULT_MITER_LIMIT, arcTolerance);
  co1.AddPaths(polys, CL.JoinType.jtRound, CL.EndType.etClosedPolygon);
  const isplestas = new CL.Paths();
  co1.Execute(isplestas, 2 * delta);

  const co2 = new CL.ClipperOffset(DEFAULT_MITER_LIMIT, arcTolerance);
  co2.AddPaths(isplestas, CL.JoinType.jtRound, CL.EndType.etClosedPolygon);
  const tree = new CL.PolyTree();
  co2.Execute(tree, -delta);

  /* Laikrodzio kryptimi einantys keliai yra skyles - jie ismetami. */
  const ex = polyTreeToExPolygons(CL, tree);
  return ex.filter(e => CL.Clipper.Orientation(e.contour)).map(e => e.contour);
}

export const offsetWaffleStyleEx = (CL, hull, delta) =>
  offsetWaffleStyle(CL, hull, delta).map(p => ({ contour: p, holes: [] }));
