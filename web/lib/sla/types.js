/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/SupportTreeTypes.hpp
 */
import { add, mul, sub, norm, normalized } from './support-tree-utils.js';

export const DOWN = [0, 0, -1];
export const ID_UNSET = -1;

/* ------------------------------------------------------------------ Junction
 * Rutulys, kuriuo baigiasi galvute arba jungiasi strypai. */
export const junction = (pos, r, id = ID_UNSET) => ({ pos, r, id });

/* ------------------------------------------------------------------ Head
 *
 * `Head` (STT.hpp:29-84). Galvute yra smaigalys (r_pin) + kotas (width) +
 * rutulys gale (r_back), o `penetration` yra tai, kiek smaigalys ILENDA i
 * detale.
 *
 * ⚠️ `junction()` formule: pos + (fullwidth() - r_back) * dir, kur
 * fullwidth() = 2*r_pin + width + 2*r_back - penetration. Nesutrumpinti:
 * `real_width` ir `fullwidth` skiriasi butent penetracija, ir sumaisius juos
 * jungties taskas atsiduria per giliai detaleje.
 */
export function head(rBigMm, rSmallMm, lengthMm, penetration, direction = DOWN, offset = [0, 0, 0]) {
  const h = {
    dir: direction,
    pos: offset,
    rBackMm: rBigMm,
    rPinMm: rSmallMm,
    widthMm: lengthMm,
    penetrationMm: penetration,
    pillarId: ID_UNSET,
    bridgeId: ID_UNSET,
    id: ID_UNSET,
  };
  return h;
}

export const realWidth = h => 2 * h.rPinMm + h.widthMm + 2 * h.rBackMm;
export const fullWidth = h => realWidth(h) - h.penetrationMm;
export const headJunction = h => junction(
  add(h.pos, mul(h.dir, fullWidth(h) - h.rBackMm)), h.rBackMm, -h.id);
export const junctionPoint = h => headJunction(h).pos;
export const headIsValid = h => h.id >= 0;
export const invalidateHead = h => { h.id = ID_UNSET; };

/* ------------------------------------------------------------------ Pillar
 *
 * `Pillar` (STT.hpp:88-121). Tiesus stulpas. Saugomas GALAS (apacia) ir
 * aukstis - pradzia isvedama. Todel stulpas visada vertikalus.
 */
export function pillar(endp, h, startRadius, endRadius = startRadius) {
  return {
    endpt: endp, height: h, rStart: startRadius, rEnd: endRadius,
    startsFromHead: false, startJunctionId: ID_UNSET,
    bridges: 0, links: 0, id: ID_UNSET,
  };
}
export const pillarStartpoint = p => [p.endpt[0], p.endpt[1], p.endpt[2] + p.height];
export const pillarEndpoint = p => p.endpt;

/* ------------------------------------------------------------------ Pedestal
 * `Pedestal` (STT.hpp:123-131) - pletejanti peda ant ploksteles. */
export const pedestal = (pos, height, rBottom, rTop) => ({ pos, height, rBottom, rTop, id: ID_UNSET });

/* ------------------------------------------------------------------ Anchor
 * `Anchor` (STT.hpp:135) - tai APVERSTA galvute: ja stulpas ar tiltas
 * prisitvirtina prie paties modelio kuno. */
export const anchor = head;

/* ------------------------------------------------------------------ Bridge */
export const bridge = (j1, j2, rMm = 0.8) => ({ startp: j1, endp: j2, r: rMm, id: ID_UNSET });
export const bridgeLength = b => norm(sub(b.endp, b.startp));
export const bridgeDir = b => normalized(sub(b.endp, b.startp));

/** `DiffBridge` (STT.hpp:152-162) - tiltas, kurio spindulys keiciasi. */
export const diffBridge = (ps, pe, rs, re) => ({ ...bridge(ps, pe, rs), endR: re });
export const diffBridgeFromJunctions = (js, je) => diffBridge(js.pos, je.pos, js.r, je.r);

/* ------------------------------------------------------- medzio rinkinys
 *
 * `SupportTreeOutput` (STT.hpp:164+). Cia tik duomenys - piesimas i sluoksnius
 * yra atskiras zingsnis, kaip ir musu esamame kelyje (3D geometrijos nestatom,
 * nes CSG luztu ant nesvariu STL).
 */
export const emptyTree = () => ({
  pillars: [], heads: [], junctions: [],
  bridges: [], crossbridges: [], diffbridges: [],
  pedestals: [], anchors: [],
});
