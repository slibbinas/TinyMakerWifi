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
import { add, mul, sub, norm, normalized, EPSILON, Ball, Beam, beamMeshHit,
         sphericToDir, dirToSpheric, optimize } from './support-tree-utils.js';
import { junction, pillar, pedestal, diffBridgeFromJunctions, DOWN } from './types.js';
import { safetyDistance, groundLevel } from './config.js';

/* Pletimo strategijos (`WideningFn`).
 *
 * `DefaultWideningModel` (STU.hpp:751-760) - butent ji naudoja numatytasis
 * `deepsearch_ground_connection`:
 *
 *   w = WIDENING_SCALE * pillar_widening_factor * len
 *   r = max(src.R, head_back_radius_mm) + w
 *
 * ⚠️ Du dalykai, kuriuos lengva praleisti:
 *   - spindulys niekada nebuna mazesnis uz `head_back_radius_mm`, net jei
 *     saltinis plonesnis (`max(src.R, ...)`);
 *   - siam modeliui pluostas naudoja 16 spinduliu, ne 8
 *     (`BeamSamples<DefaultWideningModel> = 16`, STU.hpp:763-765).
 *
 * V profilyje `pillar_widening_factor = 0`, tad `w` isnyksta ir strypas
 * nestoreja - bet `max(...)` lieka ir veikia.
 */
export const WIDENING_SCALE = 0.02;
export const DEFAULT_WIDENING_BEAM_SAMPLES = 16;

export const defaultWideningModel = sm => (src, dir, len) => {
  const w = WIDENING_SCALE * sm.cfg.pillarWideningFactor * len;
  return Math.max(src.R, sm.cfg.headBackRadiusMm) + w;
};

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
export function checkGroundRoute(mesh, sm, source, dir, bridgeLen, wideningfn, full = true,
                                 raySamples = DEFAULT_WIDENING_BEAM_SAMPLES) {
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
    brhitDist = beamMeshHit(mesh, bb, sd, raySamples).dist;
  } else {
    brhitDist = bridgeLen;
  }

  if (brhitDist < bridgeLen) return add(source.pos, mul(dir, brhitDist));

  if (downL > 0) {
    const gp = [bridgeEnd[0], bridgeEnd[1], gndlvl];
    const endRadius = wideningfn(Ball(bridgeEnd, bridgeR), DOWN, bridgeEnd[2] - gndlvl);
    const gndbeam = Beam.fromBalls(Ball(bridgeEnd, bridgeR), Ball(gp, endRadius));
    const gndhit = beamMeshHit(mesh, gndbeam, sd, raySamples);
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
 * `deepsearch_ground_connection` (STU.hpp:598-720) - PAZODZIUI.
 *
 * Ieskoma tilto krypties ir ilgio taip, kad is jo galo vertikalus stulpas
 * pasiektu plokste. Optimizuojama MINIMIZUOJANT kolizijos Z auksti: kuo
 * zemiau, tuo geriau, o pasiekus plokstes lygi paieska nutraukiama.
 *
 * ⚠️ OPTIMIZATORIUS. Originale cia `AlgNLoptMLSL_Subplx` su GLOBALIU 5000
 * iteraciju biudzetu ir lokaliu 100. Tai ne smulkmena: butent tas gylis leidzia
 * rasti APLINKKELI aplink detale, o be jo tokiose vietose atrama tiesiog
 * nesusidaro. NLopt dar neportuotas (sutarta 08-19: pirma medis ir padas,
 * paskui NLopt, paskui seja), tad kol kas cia stovi `optimize()` pakaitalas is
 * `support-tree-utils.js`. VISKAS KITA sioje funkcijoje yra tikslus portas.
 */
export function deepsearchGroundConnection(mesh, sm, source, wideningfn, initDir = DOWN) {
  const MaxIterationsGlobal = 5000;
  const MaxIterationsLocal = 100;
  const gndlvl = groundLevel(sm);

  /* `z_fn` (STU.hpp:637-646): grazina kolizijos tasko Z, jei kelias uzstotas,
     arba plokstes lygi, jei praeina. */
  const zFn = ([plr, azm, bridgeLen]) => {
    const n = sphericToDir(plr, azm);
    return checkGroundRoute(mesh, sm, source, n, bridgeLen, wideningfn, true)[2];
  };

  let [plrInit, azmInit] = dirToSpheric(initDir);
  plrInit = Math.max(plrInit, Math.PI - sm.cfg.bridgeSlope);

  const bounds = [
    [Math.PI - sm.cfg.bridgeSlope, Math.PI],   // polar
    [-Math.PI, Math.PI],                       // azimuth
    [0, sm.cfg.maxBridgeLengthMm],             // tilto ilgis
  ];

  const oresult = optimize(zFn, [plrInit, azmInit, 0], bounds, {
    minimize: true, stopScore: gndlvl, maxIter: MaxIterationsGlobal,
  });

  let [plr, azm, bridgeL] = oresult.x;
  const n = sphericToDir(plr, azm);

  const t = (gndlvl - source.pos[2]) / n[2];
  bridgeL = Math.min(t, bridgeL);

  /* Brute-force trumpinimas (STU.hpp:686-698). Originalo pastaba paaiskina,
     kodel tai NE optimizatoriaus salyga: kaip apribojimas jis pakankamai
     tikslaus sprendinio nerastu greitai, ir stop_score nustotu veikti. */
  let l = 0;
  const lMax = bridgeL;
  let zlvl = Infinity;
  while (zlvl > gndlvl && l <= lMax) {
    zlvl = checkGroundRoute(mesh, sm, source, n, l, wideningfn, false)[2];
    if (zlvl <= gndlvl) bridgeL = l;
    l += source.r;
  }

  const bridgeEnd = add(source.pos, mul(n, bridgeL));
  const gp = [bridgeEnd[0], bridgeEnd[1], gndlvl];
  const bridgeR = wideningfn(Ball(source.pos, source.r), n, bridgeL);
  const downL = bridgeEnd[2] - gndlvl;
  const endRadius = wideningfn(Ball(bridgeEnd, bridgeR), DOWN, downL);
  const baseR = Math.max(sm.cfg.baseRadiusMm, endRadius);

  /* Kelias grazinamas net ir nepavykus - su geriausiu rastu rezultatu. */
  const conn = { path: [junction(source.pos, source.r, source.id)], pillarBase: null };
  if (bridgeL > EPSILON) conn.path.push(junction(bridgeEnd, bridgeR));

  /* Pastatas galioja TIK jei paieska pavyko - tai ir yra `operator bool()`. */
  if (zFn([plr, azm, bridgeL]) <= gndlvl)
    conn.pillarBase = { pos: gp, height: sm.cfg.baseHeightMm, rBottom: baseR, rTop: endRadius };

  return conn;
}

/**
 * `deepsearch_ground_connection` su ISKALNO ZINOMU galo spinduliu
 * (STU.hpp:723-745): strypas plateja tolygiai per visa kelia iki plokstes.
 */
export function deepsearchGroundConnectionEndR(mesh, sm, source, endRadius, initDir = DOWN) {
  const gndlvl = groundLevel(sm);
  const wfn = (src, dir, len) => {
    if (len < EPSILON) return src.R;
    const dst = add(src.p, mul(dir, len));
    const widening = endRadius - src.R;
    const zlen = dst[2] - gndlvl;
    const fullLen = len + zlen;
    return src.R + widening * (len / (fullLen || 1));
  };
  return deepsearchGroundConnection(mesh, sm, source, wfn, initDir);
}
