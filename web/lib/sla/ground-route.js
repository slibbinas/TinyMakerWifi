/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/SupportTreeUtils.hpp
 *   check_ground_route, deepsearch_ground_connection, build_ground_connection
 */
import { add, mul, sub, norm, normalized, EPSILON, Ball, Beam, beamMeshHit } from './support-tree-utils.js';
import { junction, pillar, pedestal, diffBridgeFromJunctions, DOWN } from './types.js';
import { safetyDistance, groundLevel } from './config.js';

/* Plėtimo strategijos (`WideningFn`). Originalas leidzia keleta; V profilyje
   `support_pillar_widening_factor = 0`, tad strypas nestoreja - bet funkcijos
   forma islaikoma, kad velesnis perjungimas butu vienos vietos keitimas. */
export const constantWidening = r => () => r;
export const widenBy = factor => (ball, dir, length) => ball.R + factor * length;

/**
 * `check_ground_route` (STU.hpp:521-592).
 *
 * Grazina TASKA, iki kurio kelias i plokste yra laisvas. Jei jis sutampa su
 * numatytu galu - kelias praeina; jei arciau - ten kliutis.
 *
 * ⚠️ Viduje yra saka BUTENT musu atvejui: kai `objectElevationMm` yra nulis
 * (o pas mus jis nulis, nes pad_around_object), tikrinama, ar stulpo peda
 * nepatenka i tarpa tarp pado ir modelio. Be jos stulpai lystu i ta plysi.
 */
export function checkGroundRoute(mesh, sm, source, dir, bridgeLen, wideningfn, full = true) {
  const cfg = sm.cfg;
  const sd = safetyDistance(cfg, source.r);
  const gndlvl = groundLevel(sm);

  /* Jei tiltas nusileistu zemiau ploksstes, nukerpam ji ties ja. */
  const t = (gndlvl - source.pos[2]) / dir[2];
  if (t > 0 || dir[2] < 0) bridgeLen = Math.min(t, bridgeLen);

  const bridgeEnd = add(source.pos, mul(dir, bridgeLen));
  const downL = bridgeEnd[2] - gndlvl;
  const bridgeR = wideningfn(Ball(source.pos, source.r), dir, bridgeLen);
  let brhitDist = 0;

  if (bridgeLen > EPSILON && full) {
    /* Nulinio ilgio tiltui pluostas negalioja - todel salyga. */
    const bb = Beam.fromBalls(Ball(source.pos, source.r), Ball(bridgeEnd, bridgeR));
    brhitDist = beamMeshHit(mesh, bb, sd).dist;
  } else {
    brhitDist = bridgeLen;
  }

  if (brhitDist < bridgeLen) return add(source.pos, mul(dir, brhitDist));

  if (downL > 0) {
    const gp = [bridgeEnd[0], bridgeEnd[1], gndlvl];
    const endRadius = wideningfn(Ball(bridgeEnd, bridgeR), DOWN, bridgeEnd[2] - gndlvl);
    const gndbeam = Beam.fromBalls(Ball(bridgeEnd, bridgeR), Ball(gp, endRadius));
    const gndhit = beamMeshHit(mesh, gndbeam, sd);
    let gndHitD = Math.min(gndhit.dist, downL + EPSILON);

    if (source.r >= cfg.headBackRadiusMm && gndhit.dist > downL &&
        cfg.objectElevationMm < EPSILON) {
      /* Nulinio pakelimo rezimas: neleidziam stulpui nusileisti i plysi tarp
         pado ir modelio kuno. */
      const gap = Math.sqrt(mesh.squaredDistance(gp));
      const baseR = Math.max(cfg.baseRadiusMm, endRadius);
      const minGap = cfg.pillarBaseSafetyDistanceMm + baseR;
      if (gap < minGap) gndHitD = downL - minGap + gap;
    }
    return [bridgeEnd[0], bridgeEnd[1], bridgeEnd[2] - gndHitD];
  }

  return bridgeEnd;
}

/**
 * `build_ground_connection` (STU.hpp:460-500): is rasto kelio pastato tikra
 * geometrija - tarpines jungtis, stulpa ir peda.
 */
export function buildGroundConnection(tree, sm, conn) {
  if (!conn || !conn.pillarBase || !conn.path.length) return -1;

  for (let i = 0; i + 1 < conn.path.length; i++) {
    tree.diffbridges.push(diffBridgeFromJunctions(conn.path[i], conn.path[i + 1]));
    tree.junctions.push(conn.path[i + 1]);
  }

  const last = conn.path[conn.path.length - 1];
  const gp = [last.pos[0], last.pos[1], groundLevel(sm)];
  let h = last.pos[2] - gp[2];

  /* Plonas strypas remiasi ne i plokste, o i PADA - todel jis pratesiamas per
     pado sienos storį. */
  if (conn.pillarBase.rTop < sm.cfg.headBackRadiusMm) {
    const wall = (sm.padCfg && sm.padCfg.wallThicknessMm) || 0;
    h += wall;
    gp[2] -= wall;
  }

  const p = pillar(gp, h, last.r, conn.pillarBase.rTop);
  p.id = tree.pillars.length;
  tree.pillars.push(p);

  if (conn.pillarBase.rTop >= sm.cfg.headBackRadiusMm) {
    tree.pedestals.push(pedestal(gp, conn.pillarBase.height,
                                 conn.pillarBase.rBottom, conn.pillarBase.rTop));
  }
  return p.id;
}

/**
 * Tiesus kelias zemyn - `deepsearch_ground_connection` supaprastinta pradzia.
 * Pilna versija dar ieskotu APLINKKELIO su vienu tiltu; ji ateis kartu su
 * `routing_to_ground`, kad nebutu portuojama „is akies".
 */
export function straightGroundConnection(mesh, sm, source, wideningfn) {
  const gndlvl = groundLevel(sm);
  const endp = checkGroundRoute(mesh, sm, source, DOWN, 0, wideningfn, false);
  const reached = Math.abs(endp[2] - gndlvl) < EPSILON;
  if (!reached) return null;
  const rTop = wideningfn(Ball(source.pos, source.r), DOWN, source.pos[2] - gndlvl);
  return {
    path: [junction(source.pos, source.r, source.id)],
    pillarBase: {
      pos: [source.pos[0], source.pos[1], gndlvl],
      height: sm.cfg.baseHeightMm,
      rBottom: sm.cfg.baseRadiusMm,
      rTop,
    },
  };
}
