/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/SupportTree.hpp (struct SupportTreeConfig)
 * Reiksmes: V profilis „TinyMaker + Universal 0.05 - Light Supports"
 *           (slicer-lab/prusa-full.ini) - tas pats, su kuriuo lyginam.
 */

export const SupportTreeType = { Default: 'default', Branching: 'branching' };
export const PillarConnectionMode = { zigzag: 'zigzag', cross: 'cross', dynamic: 'dynamic' };

/* Profilio reiksmes duodamos DIAMETRAIS, o kodas dirba SPINDULIAIS - originale
   tas vertimas ivyksta SLAPrint konfige, ne cia. Todel raso m kaip yra ir
   verciam vienoje vietoje, kad nesusimaisytu. */
const D = {
  head_front_diameter: 0.5,      // support_head_front_diameter
  head_penetration: 0.3,         // support_head_penetration
  head_width: 3,                 // support_head_width
  pillar_diameter: 1,            // support_pillar_diameter
  small_pillar_diameter_percent: 60,
  base_diameter: 3,              // support_base_diameter
  base_height: 1,
  base_safety_distance: 1,
  critical_angle: 45,
  max_bridge_length: 10,
  max_pillar_link_distance: 10,
  max_bridges_on_pillar: 3,
  max_weight_on_model: 10,
  object_elevation: 5,
  pillar_widening_factor: 0,
  points_density_relative: 100,
  buildplate_only: 0,
  pillar_connection_mode: 'zigzag',
  tree_type: 'default',
  pad_around_object: true,
};

export function defaultConfig(over = {}) {
  const backR = D.pillar_diameter / 2;
  const cfg = {
    enabled: true,
    treeType: D.tree_type,

    headFrontRadiusMm: D.head_front_diameter / 2,
    headPenetrationMm: D.head_penetration,
    headBackRadiusMm: backR,
    headFallbackRadiusMm: backR * D.small_pillar_diameter_percent / 100,
    headWidthMm: D.head_width,

    pillarConnectionMode: D.pillar_connection_mode,
    groundFacingOnly: !!D.buildplate_only,
    pillarWideningFactor: D.pillar_widening_factor,

    baseRadiusMm: D.base_diameter / 2,
    baseHeightMm: D.base_height,

    /* `bridge_slope` originale yra PI/4 konstanta, o profilio
       `support_critical_angle` (45°) yra ta pati reiksme kitu vardu. */
    bridgeSlope: D.critical_angle * Math.PI / 180,
    maxBridgeLengthMm: D.max_bridge_length,
    maxPillarLinkDistanceMm: D.max_pillar_link_distance,

    /* ⚠️ `pad_around_object = 1` reiskia, kad padas apgaubia detale, ir tada
       modelis NEKELIAMAS - elevation lieka nulis, nors profilyje irasyta 5.
       Sumaisius tai, visos atramos pakiltu 5 mm i ora. */
    objectElevationMm: D.pad_around_object ? 0 : D.object_elevation,
    pillarBaseSafetyDistanceMm: D.base_safety_distance,

    maxBridgesOnPillar: D.max_bridges_on_pillar,
    maxWeightOnModelSupport: D.max_weight_on_model,

    /* Kompiliavimo meto konstantos (SupportTree.hpp:105-118) - profilyje ju
       nera, bet jos lemia elgesi. */
    normalCutoffAngle: 150.0 * Math.PI / 180.0,
    safetyDistanceMm: 0.5,
    maxSoloPillarHeightMm: 15.0,
    maxDualPillarHeightMm: 35.0,
    pillarCascadeNeighbors: 3,
    optimizerMaxIterations: 100,   // krypties paieskai; zr. maximizeUntil

    ...over,
  };
  return cfg;
}

/** `head_fullwidth()` (ST.hpp:88-91). */
export const headFullwidth = cfg =>
  2 * cfg.headFrontRadiusMm + cfg.headWidthMm + 2 * cfg.headBackRadiusMm - cfg.headPenetrationMm;

/**
 * `safety_distance(r)` (ST.hpp:94-97): plonesniems strypams saugos tarpas
 * mazinamas proporcingai. Tai NE tas pats, kas pastovus tarpas - stulpas,
 * plonesnis uz galvutes rutuli, gali prieiti arciau.
 */
export const safetyDistance = (cfg, r) =>
  r === undefined ? cfg.safetyDistanceMm
                  : Math.min(cfg.safetyDistanceMm, r * cfg.safetyDistanceMm / cfg.headBackRadiusMm);

/** `ground_level(sm)` - plokstes lygis su elevation ir zoffset. */
export const groundLevel = sm => (sm.zoffset || 0) + (sm.cfg.objectElevationMm || 0);
