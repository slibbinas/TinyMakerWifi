/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/MeshNormals.cpp (get_normal)
 */
import { normalized } from './support-tree-utils.js';

/**
 * `get_normal` (MeshNormals.cpp:29-125).
 *
 * Ne tiesiog „artimiausio trikampio normale". Jei taskas gula ANT VIRSUNES
 * arba ANT BRIAUNOS, imamos visu ten susieinanciu trikampiu normales ir
 * sudedamos - kitaip ties briauna normale soktu, ir galvute pasvirtu i viena
 * pusę vien del to, kuris trikampis pasitaike arciau.
 *
 * ⚠️ Vidurkinama NE per visus kaimynus, o per DEDUBLIKUOTAS normales: dvi
 * vienodos (1e-3 tikslumu) skaitomos kaip viena. Be to plokscia siena, sudaryta
 * is daug trikampiu, nusvertu rezultata vien trikampiu kiekiu.
 */
export function getNormal(mesh, pickingPoint, eps = 0.05) {
  const r = mesh.squaredDistance(pickingPoint, true);
  if (!r || r.t < 0) return [0, 0, 0];

  const t = r.t, p = r.q;
  const q = mesh.pos;
  const p1 = [q[t], q[t + 1], q[t + 2]];
  const p2 = [q[t + 3], q[t + 4], q[t + 5]];
  const p3 = [q[t + 6], q[t + 7], q[t + 8]];

  const sq = (a, b) => (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2;
  const epsSq = eps * eps;

  /* Ar taskas ant briaunos: atstumas iki TIESES per abu galus. */
  const onEdge = (pt, e1, e2) => {
    const d = [e2[0] - e1[0], e2[1] - e1[1], e2[2] - e1[2]];
    const L2 = d[0] ** 2 + d[1] ** 2 + d[2] ** 2;
    if (L2 < 1e-18) return false;
    const w = [pt[0] - e1[0], pt[1] - e1[1], pt[2] - e1[2]];
    const cx = w[1] * d[2] - w[2] * d[1];
    const cy = w[2] * d[0] - w[0] * d[2];
    const cz = w[0] * d[1] - w[1] * d[0];
    return (cx * cx + cy * cy + cz * cz) / L2 < epsSq;
  };

  let vertex = null, edge = -1;
  if (sq(p, p1) < epsSq) vertex = p1;
  else if (sq(p, p2) < epsSq) vertex = p2;
  else if (sq(p, p3) < epsSq) vertex = p3;
  else if (onEdge(p, p1, p2)) edge = 0;
  else if (onEdge(p, p2, p3)) edge = 1;
  else if (onEdge(p, p1, p3)) edge = 2;

  const neigh = [];
  /* Dedublikavimas: 1e-3 kiekvienam komponentui, kaip originale (`eqfn`). */
  const push = n => {
    for (const m of neigh)
      if (Math.abs(m[0] - n[0]) < 1e-3 && Math.abs(m[1] - n[1]) < 1e-3 &&
          Math.abs(m[2] - n[2]) < 1e-3) return;
    neigh.push(n);
  };

  if (vertex) {
    const { map, key } = mesh.vertexFaceIndex();
    for (const f of map.get(key(vertex[0], vertex[1], vertex[2])) || [])
      push(mesh.faceNormal(f));
  } else if (edge >= 0) {
    const E = [[p1, p2], [p2, p3], [p1, p3]][edge];
    const f2 = mesh.faceAcrossEdge(t, E[0], E[1]);
    if (f2 >= 0) { push(mesh.faceNormal(t)); push(mesh.faceNormal(f2)); }
  }

  if (neigh.length) {
    const s = neigh.reduce((a, n) => [a[0] + n[0], a[1] + n[1], a[2] + n[2]], [0, 0, 0]);
    return normalized(s);
  }
  return mesh.faceNormal(t);
}
