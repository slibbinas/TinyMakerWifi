/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/TriangleMeshSlicer.cpp
 *   slice_facet, slice_make_lines, chain_lines_by_triangle_connectivity,
 *   make_loops, make_expolygons, slice_mesh, slice_mesh_ex
 *
 * Is 2659 originalo eiluciu cia yra tik SLA reikalinga dalis - FDM „slabs",
 * projekcijos ir trianguliacija nereikalingos.
 */
import { scaled, unionEx, offsetEx, polyTreeToExPolygons, DEFAULT_MITER_LIMIT } from './geometry.js';

const GENERAL = 0, TOP = 1, BOTTOM = 2, HORIZONTAL = 3;
const SLICING = 0, CUTTING = 1, NOSLICE = 2;

/**
 * `slice_facet` (TMS.cpp:223-404).
 *
 * Vieno trikampio sankirta su plokstuma. Grazina `null` arba atkarpa
 * `{ax, ay, bx, by, edgeAId, edgeBId, aId, bId, edgeType}`.
 *
 * ⚠️ Trys dalykai, kuriuos butina islaikyti:
 *
 * 1. Virsunes pereinamos pradedant nuo ZEMIAUSIOS (`idxVertexLowest`). Tai
 *    duoda nuosekli atkarpu kryptį - „isore visada desineje". Be sito
 *    konturai iseina su atsitiktine orientacija ir skyles nesiskiria nuo
 *    isores.
 *
 * 2. Kai DVI virsunes guli ant plokstumos, atkarpa priimama tik jei trecioji
 *    yra ZEMIAU (`third_below`), ir tada a/b sukeiciami. Taisykle originale
 *    ivardyta: zemiausia trikampio briauna jam „nepriklauso", o auksciausia -
 *    priklauso. Be sito gretimi trikampiai duotu ta pacia briauna du kartus.
 *
 * 3. Gale `a = points[1]`, `b = points[0]` - APVERSTA tvarka, ne 0/1.
 */
function sliceFacet(sliceZ, v, idxVertexLowest, horizontal) {
  const points = [];
  let pointOnLayer = -1;

  for (let j = 0; j < 3; j++) {
    const k = (idxVertexLowest + j) % 3;
    const l = (k + 1) % 3;
    let a = v[k], b = v[l], aId = k, bId = l;
    const edgeId = k;

    if (a[2] === sliceZ && b[2] === sliceZ) {
      /* Briauna guli plokstumoje. */
      const v0 = v[0], v1 = v[1], v2 = v[2];
      if (horizontal) {
        const cr = (v1[0] - v0[0]) * (v2[1] - v1[1]) - (v1[1] - v0[1]) * (v2[0] - v1[0]);
        if (cr < 0) { const t = a; a = b; b = t; const ti = aId; aId = bId; bId = ti; }
        return { ax: a[0], ay: a[1], bx: b[0], by: b[1], aId, bId,
                 edgeAId: -1, edgeBId: -1, edgeType: HORIZONTAL, cutting: true };
      }
      const thirdBelow = v0[2] < sliceZ || v1[2] < sliceZ || v2[2] < sliceZ;
      let edgeType;
      if (thirdBelow) {
        edgeType = TOP;
        const t = a; a = b; b = t; const ti = aId; aId = bId; bId = ti;
      } else edgeType = BOTTOM;
      return { ax: a[0], ay: a[1], bx: b[0], by: b[1], aId, bId,
               edgeAId: -1, edgeBId: -1, edgeType, cutting: !thirdBelow };
    }

    if (a[2] === sliceZ) {
      if (pointOnLayer === -1 || points[pointOnLayer].pointId !== aId) {
        pointOnLayer = points.length;
        points.push({ x: a[0], y: a[1], pointId: aId, edgeId: -1 });
      }
    } else if (b[2] === sliceZ) {
      if (pointOnLayer === -1 || points[pointOnLayer].pointId !== bId) {
        pointOnLayer = points.length;
        points.push({ x: b[0], y: b[1], pointId: bId, edgeId: -1 });
      }
    } else if ((a[2] < sliceZ && b[2] > sliceZ) || (b[2] < sliceZ && a[2] > sliceZ)) {
      /* Briauna rusiuojama, kad atsakymas butu vienodas is abieju trikampiu. */
      if (aId > bId) { const t = a; a = b; b = t; const ti = aId; aId = bId; bId = ti; }
      const t = (sliceZ - a[2]) / (b[2] - a[2]);
      let px, py;
      if (t <= 0) { px = a[0]; py = a[1]; }
      else if (t >= 1) { px = b[0]; py = b[1]; }
      else {
        px = Math.floor(a[0] * (1 - t) + b[0] * t + 0.5);
        py = Math.floor(a[1] * (1 - t) + b[1] * t + 0.5);
      }
      points.push({ x: px, y: py, pointId: -1, edgeId });
    }
  }

  if (points.length === 2) {
    /* ⚠️ a is points[1], b is points[0] - butent tokia tvarka. */
    return {
      ax: points[1].x, ay: points[1].y, bx: points[0].x, by: points[0].y,
      aId: points[1].pointId, bId: points[0].pointId,
      edgeAId: points[1].edgeId, edgeBId: points[0].edgeId,
      edgeType: GENERAL, cutting: false,
    };
  }
  return null;
}

/**
 * `slice_make_lines` (TMS.cpp:424-457 + 459-483).
 *
 * Kiekvienam trikampiui randami visi sluoksniai, kuriuos jis kerta, ir
 * kiekvienam ju - atkarpa. Horizontalus trikampiai (min_z == max_z)
 * PRALEIDZIAMI: originalo komentaras sako, kad prie kiekvieno tokio privalo
 * buti vertikalus, kitaip detale butu nulinio turio.
 */
export function sliceMakeLines(pos, zs) {
  const lines = zs.map(() => []);
  const n = zs.length;

  for (let t = 0; t + 8 < pos.length; t += 9) {
    const v = [
      [scaled(pos[t]), scaled(pos[t + 1]), pos[t + 2]],
      [scaled(pos[t + 3]), scaled(pos[t + 4]), pos[t + 5]],
      [scaled(pos[t + 6]), scaled(pos[t + 7]), pos[t + 8]],
    ];
    const minZ = Math.min(v[0][2], v[1][2], v[2][2]);
    const maxZ = Math.max(v[0][2], v[1][2], v[2][2]);
    if (minZ === maxZ) continue;                       // horizontalus - praleidziam

    /* `lower_bound` / `upper_bound` per surusiuotus zs. */
    let lo = 0, hi = n;
    while (lo < hi) { const m = (lo + hi) >> 1; if (zs[m] < minZ) lo = m + 1; else hi = m; }
    let hi2 = n; let lo2 = lo;
    while (lo2 < hi2) { const m = (lo2 + hi2) >> 1; if (zs[m] <= maxZ) lo2 = m + 1; else hi2 = m; }

    const idxLowest = v[1][2] === minZ ? 1 : (v[2][2] === minZ ? 2 : 0);
    for (let i = lo; i < hi2; i++) {
      const il = sliceFacet(zs[i], v, idxLowest, false);
      if (il && !il.cutting) lines[i].push(il);
    }
  }
  return lines;
}

/**
 * Atkarpu lipdymas i uzdarus konturus.
 *
 * Originalas (`chain_lines_by_triangle_connectivity`) lipdo per TRIKAMPIU
 * KAIMYNYSTE - kiekviena atkarpa zino savo briaunos numeri, ir gretima
 * randama pagal ta pati numeri. Musu `edgeId` yra tik trikampio viduje, tad
 * kaimynyste statoma per TASKU sutapima, o tai originalo `chain_open_polylines_exact`
 * atitikmuo (TMS.cpp:1155-1297): galai jungiami, kai sutampa TIKSLIAI.
 *
 * ⚠️ Tai vienintele vieta visame porte, kur is trijų originalo lipdymo pakopu
 * imamos dvi: tiksli ir tarpu uzdarymas. Pirmoji (per briaunu numerius)
 * reikalautu trikampiu kaimynystes indekso is `its_face_edge_ids`, kurio musu
 * duomenyse nera. Rezultatas tas pats, kai modelis svarus; skiriasi tik
 * greitis ir elgesys su nesandariais.
 */
function chainLines(lines) {
  if (!lines.length) return [];
  const KEY = p => `${p[0]}_${p[1]}`;
  const galai = new Map();
  const naudota = new Array(lines.length).fill(false);

  lines.forEach((l, i) => {
    const k = KEY([l.ax, l.ay]);
    if (!galai.has(k)) galai.set(k, []);
    galai.get(k).push(i);
  });

  const loops = [];
  for (let i = 0; i < lines.length; i++) {
    if (naudota[i]) continue;
    const loop = [];
    let cur = i;
    for (;;) {
      naudota[cur] = true;
      const l = lines[cur];
      loop.push({ X: l.ax, Y: l.ay });
      const k = KEY([l.bx, l.by]);
      const kand = galai.get(k);
      let next = -1;
      if (kand) for (const c of kand) if (!naudota[c]) { next = c; break; }
      if (next === -1) break;
      cur = next;
      if (cur === i) break;
    }
    if (loop.length >= 3) loops.push(loop);
  }
  return loops;
}

/**
 * `make_expolygons` (TMS.cpp:1806-1893).
 *
 * Konturai -> ExPolygons. `closing_radius` uzdaro mikroskopines skyles:
 * isplecia ir traukia atgal. V profilyje `slice_gap_closing_radius = 0,005`.
 */
export function makeExPolygons(CL, loops, closingRadius = 0, extraOffset = 0) {
  if (!loops.length) return [];
  let ex = unionEx(CL, loops);
  const delta = scaled(closingRadius);
  if (delta > 0) {
    ex = offsetEx(CL, exPaths(ex), delta, CL.JoinType.jtRound, DEFAULT_MITER_LIMIT);
    ex = offsetEx(CL, exPaths(ex), -delta, CL.JoinType.jtRound, DEFAULT_MITER_LIMIT);
  }
  if (extraOffset) {
    ex = offsetEx(CL, exPaths(ex), scaled(extraOffset), CL.JoinType.jtMiter, DEFAULT_MITER_LIMIT);
  }
  return ex;
}

const exPaths = exs => { const o = []; for (const e of exs) { o.push(e.contour); for (const h of e.holes) o.push(h); } return o; };

/**
 * `slice_mesh_ex` (TMS.cpp:2130-2214). Trikampiai -> sluoksniu ExPolygons.
 * @param zs surusiuoti Z lygiai (mm)
 */
export function sliceMeshEx(CL, pos, zs, closingRadius = 0.005) {
  const lines = sliceMakeLines(pos, zs);
  return lines.map(ls => makeExPolygons(CL, chainLines(ls), closingRadius));
}
