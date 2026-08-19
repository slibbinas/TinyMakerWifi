/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/SLA/DefaultSupportTree.{hpp,cpp}
 */
import {
  add, mul, sub, norm, normalized, distance3, EPSILON,
  Ball, Beam, beamMeshHit, pinheadMeshHit, dirToSpheric, sphericToDir, optimize,
} from './support-tree-utils.js';
import {
  head as mkHead, headJunction, junctionPoint, fullWidth, invalidateHead, headIsValid,
  pillar as mkPillar, pillarStartpoint, pillarEndpoint, junction, bridge as mkBridge,
  emptyTree, DOWN, ID_UNSET,
} from './types.js';
import { safetyDistance, groundLevel } from './config.js';
import { clusterByDistance, clusterByPredicate, clusterCentroid } from './clustering.js';
import { getNormal } from './normals.js';
import { deepsearchGroundConnection, buildGroundConnection, defaultWideningModel,
         DEFAULT_WIDENING_BEAM_SAMPLES } from './ground-route.js';

const to2d = p => [p[0], p[1]];
const dist2d = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);

export class DefaultSupportTree {
  /**
   * @param sm     {cfg, zoffset, padCfg}
   * @param mesh   AABBMesh
   * @param points sejos taskai: [{pos:[x,y,z], headFrontRadius}]
   */
  constructor(sm, mesh, points) {
    this.sm = sm;
    this.mesh = mesh;
    this.points = points;
    this.tree = emptyTree();
    this.heads = [];            // visi, ir negaliojantys (indeksas = tasko nr.)
    this.iheads = [];           // galiojanciu indeksai
    this.iheadsOnModel = [];
    this.headToGroundScans = new Map();
    this.pillarClusters = [];
    /* `m_pillar_index` - stulpu galu erdvinis indeksas. Originale R-tree; cia
       masyvas su tiesine paieska, nes stulpu desimtys, ne tukstanciai. */
    this.pillarIndex = [];
  }

  /* -------------------------------------------------- kolizijos apvalkalai */

  /** `pinhead_mesh_intersect` (DST.hpp:134-143).
   *  ⚠️ Saugos atstumas cia `r_back * safety / head_back_radius` BE `min` -
   *  tai NE tas pats, kas `cfg.safety_distance(r)`. */
  pinheadMeshIntersect(s, dir, rPin, rBack, width) {
    const cfg = this.sm.cfg;
    const sd = rBack * cfg.safetyDistanceMm / cfg.headBackRadiusMm;
    return pinheadMeshHit(this.mesh, s, dir, rPin, rBack, width, sd);
  }

  /** `bridge_mesh_intersect` - pluostas isilgai strypo. */
  bridgeMeshIntersect(s, dir, r, safetyD) {
    const sd = safetyD === undefined ? safetyDistance(this.sm.cfg, r) : safetyD;
    return beamMeshHit(this.mesh, new Beam(s, dir, r), sd, DEFAULT_WIDENING_BEAM_SAMPLES);
  }

  /* -------------------------------------------------------- add_pinheads */

  /**
   * `add_pinheads` (DST.cpp:385-527).
   *
   * Trys dalykai is eiles, kuriuos svarbu islaikyti:
   *  1. per arti esantys taskai suklijuojami POROMIS (`cluster(pts, 0.1, 2)`),
   *     ir is kiekvieno klasterio imamas TIK PIRMAS;
   *  2. galvute NEDEDAMA, jei normale per stati (`polar < PI - normal_cutoff`);
   *  3. netilpus - back_r mazinamas iki `head_fallback_radius_mm` ir bandoma
   *     DAR KARTA (rekursija), o ne numetama.
   */
  addPinheads() {
    const cfg = this.sm.cfg;
    const D_SP = 0.1;

    const visiIdx = this.points.map((_, i) => i);
    const aliases = clusterByDistance(visiIdx, i => this.points[i].pos, D_SP, 2);
    const filtered = aliases.map(a => a[0]);

    /* Galvutes kuriamos VISIEMS taskams (ir tiems, kurie iskrito per
       suklijavima), bet id gauna tik prasejusieji - taip originale. */
    this.heads = this.points.map(p => {
      const h = mkHead(NaN, p.headFrontRadius !== undefined ? p.headFrontRadius : cfg.headFrontRadiusMm,
                       0, cfg.headPenetrationMm, [0, 0, 0], p.pos.slice());
      return h;
    });

    const filterfn = (fidx, nrml, backR) => {
      let [polar, azimuth] = dirToSpheric(nrml);

      if (polar < Math.PI - cfg.normalCutoffAngle) return;      // per stati
      polar = Math.max(polar, Math.PI - cfg.bridgeSlope);       // iki 3pi/4

      const hp = this.points[fidx].pos;
      let lmin = cfg.headWidthMm, lmax = lmin;
      if (backR < cfg.headBackRadiusMm) { lmin = 0; lmax = cfg.headPenetrationMm; }

      const w = lmin + 2 * backR + 2 * cfg.headFrontRadiusMm - cfg.headPenetrationMm;
      const pinR = this.heads[fidx].rPinMm;
      let nn = normalized(sphericToDir(polar, azimuth));

      let t = this.pinheadMeshIntersect(hp, nn, pinR, backR, w);

      if (t.dist < w) {
        /* ⚠️ Originale cia `AlgNLoptGenetic` (ESCH). NLopt dar neportuotas -
           stovi `optimize()` pakaitalas. Tikslo funkcija ir reziai tikslus. */
        const or_ = optimize(
          ([plr, azm, l]) => {
            const d = normalized(sphericToDir(plr, azm));
            return this.pinheadMeshIntersect(hp, d, pinR, backR, l).dist;
          },
          [polar, azimuth, (lmin + lmax) / 2],
          [[Math.PI - cfg.bridgeSlope, Math.PI], [-Math.PI, Math.PI], [lmin, lmax]],
          { stopScore: w, maxIter: cfg.optimizerMaxIterations });

        if (or_.score > w) {
          polar = or_.x[0]; azimuth = or_.x[1];
          nn = normalized(sphericToDir(polar, azimuth));
          lmin = or_.x[2];
          t = { dist: or_.score, inside: false };
        }
      }

      if (t.dist > w && hp[2] + w * nn[2] >= groundLevel(this.sm)) {
        const h = this.heads[fidx];
        h.id = fidx;
        h.dir = nn;
        h.widthMm = lmin;
        h.rBackMm = backR;
      } else if (backR > cfg.headFallbackRadiusMm) {
        filterfn(fidx, nrml, cfg.headFallbackRadiusMm);
      }
    };

    for (const fidx of filtered) {
      const n = getNormal(this.mesh, this.points[fidx].pos, this.sm.cfg.headFrontRadiusMm);
      filterfn(fidx, n, cfg.headBackRadiusMm);
    }

    for (let i = 0; i < this.heads.length; i++)
      if (headIsValid(this.heads[i])) {
        this.tree.heads.push(this.heads[i]);
        this.iheads.push(i);
      }
  }

  /* ------------------------------------------------------------ classify */

  /**
   * `classify` (DST.cpp:528-575).
   *
   * Padalija galvutes i tas, kurios pasiekia plokste TIESIAI zemyn, ir tas,
   * kurios remsis i modeli. Skiriamasis pozymis grieztas: `isinf(hit.dist)`,
   * t. y. pluostas zemyn NIEKO nesutinka.
   *
   * Tada plokste pasiekiancios grupuojamos i klasterius: viename klasteryje
   * bus VIENAS tikras stulpas, o likusios prie jo prisikabins tiltais.
   */
  classify() {
    const cfg = this.sm.cfg;
    const groundHeads = [];

    for (const i of this.iheads) {
      const h = this.heads[i];
      const r = h.rBackMm;
      const headjp = junctionPoint(h);
      const hit = this.bridgeMeshIntersect(headjp, DOWN, r);

      if (!isFinite(hit.dist)) groundHeads.push(i);
      else if (cfg.groundFacingOnly) invalidateHead(h);
      else this.iheadsOnModel.push(i);

      this.headToGroundScans.set(i, hit);
    }

    const pointfn = i => junctionPoint(this.heads[i]);
    /* Predikatas: XY atstumas maziau nei DU pedu spinduliai (kad pedos
       nesikirstu) IR 3D atstumas telpa i tilto ilgi. */
    const predicate = (e1, e2) => {
      const d2d = dist2d(to2d(e1[0]), to2d(e2[0]));
      const d3d = distance3(e1[0], e2[0]);
      return d2d < 2 * cfg.baseRadiusMm && d3d < cfg.maxBridgeLengthMm;
    };

    this.pillarClusters = clusterByPredicate(groundHeads, pointfn, predicate,
                                             cfg.maxBridgesOnPillar);
  }
}
