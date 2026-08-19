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

  /* --------------------------------------------------- pagalbiniai statymui */

  /** `bridge_mesh_distance` - tik atstumas. */
  bridgeMeshDistance(s, dir, r, sd) { return this.bridgeMeshIntersect(s, dir, r, sd).dist; }

  addPillarToTree(p) { p.id = this.tree.pillars.length; this.tree.pillars.push(p); return p.id; }

  /** `connect_to_ground` (DST.cpp:648-668) per `deepsearch_ground_connection`. */
  connectToGround(h) {
    const src = junction(junctionPoint(h), h.rBackMm, -h.id);
    const conn = deepsearchGroundConnection(this.mesh, this.sm, src,
                                            defaultWideningModel(this.sm), h.dir);
    const pid = buildGroundConnection(this.tree, this.sm, conn);
    if (pid >= 0) {
      this.pillarIndex.push({ pos: pillarEndpoint(this.tree.pillars[pid]), id: pid });
      h.pillarId = pid;
    }
    return pid >= 0;
  }

  /** `create_ground_pillar` (DST.cpp:365-383). */
  createGroundPillar(hjp, sourcedir, headId) {
    const conn = deepsearchGroundConnection(this.mesh, this.sm, hjp,
                                            defaultWideningModel(this.sm), sourcedir);
    const pid = buildGroundConnection(this.tree, this.sm, conn);
    if (pid >= 0) this.pillarIndex.push({ pos: pillarEndpoint(this.tree.pillars[pid]), id: pid });
    return pid >= 0;
  }

  /**
   * `connect_to_nearpillar` (DST.cpp:282-363).
   *
   * Bando nuvesti tilta nuo galvutes i JAU ESANTI stulpa. Jei tiesioginis
   * kelias per status arba per ilgas, ieskoma zemesnio lietimosi tasko ant to
   * stulpo, ir tada po galvute reikia DALINIO stulpelio (`zdiff > 0`).
   */
  connectToNearpillar(h, nearpillarId) {
    const cfg = this.sm.cfg;
    const np = this.tree.pillars[nearpillarId];
    if (!np) return false;
    if (np.bridges > cfg.maxBridgesOnPillar) return false;

    const headjp = junctionPoint(h);
    const nearjpU = pillarStartpoint(np);
    const nearjpL = pillarEndpoint(np);

    const r = h.rBackMm;
    const d2d = dist2d(to2d(headjp), to2d(nearjpU));
    const d3d = distance3(headjp, nearjpU);
    const hdiff = nearjpU[2] - headjp[2];
    const slope = Math.atan2(hdiff, d2d);

    let bridgestart = headjp.slice();
    let bridgeend = nearjpU.slice();
    const maxLen = r * cfg.maxBridgeLengthMm / cfg.headBackRadiusMm;
    const maxSlope = cfg.bridgeSlope;
    let zdiff = 0;

    if (d3d > maxLen || slope > -maxSlope) {
      let Zdown = headjp[2] + d2d * Math.tan(-maxSlope);
      const touchjp = [bridgeend[0], bridgeend[1], Zdown];
      const D = distance3(headjp, touchjp);
      zdiff = Zdown - nearjpU[2];

      if (zdiff > 0) {
        Zdown -= zdiff;
        bridgestart[2] -= zdiff;
        touchjp[2] = Zdown;
        /* Po galvute reikia dalinio stulpelio - bet tik jei ten yra vietos. */
        const t = this.bridgeMeshDistance(headjp, DOWN, r);
        if (t < zdiff) return false;
      }

      if (Zdown <= nearjpU[2] && Zdown >= nearjpL[2] && D < maxLen) bridgeend[2] = Zdown;
      else return false;
    }

    /* Empirine riba: tiltas neleidziamas per zemai prie plokstes. */
    const minz = groundLevel(this.sm) + 4 * h.rBackMm;
    if (bridgeend[2] < minz) return false;

    const t = this.bridgeMeshDistance(bridgestart, normalized(sub(bridgeend, bridgestart)), r);
    if (t < distance3(bridgestart, bridgeend)) return false;

    if (np.bridges < cfg.maxBridgesOnPillar) {
      if (zdiff > 0) {
        const p = mkPillar([headjp[0], headjp[1], bridgestart[2]], headjp[2] - bridgestart[2], r);
        p.startsFromHead = true; p.startJunctionId = h.id;
        this.addPillarToTree(p);
        this.tree.junctions.push(junction(bridgestart, r));
        this.tree.bridges.push(mkBridge(bridgestart, bridgeend, r));
      } else {
        this.tree.bridges.push(mkBridge(headjp, bridgeend, r));
      }
      np.bridges++;
      return true;
    }
    return false;
  }

  /** `search_pillar_and_connect` (DST.cpp:723-760). */
  searchPillarAndConnect(source) {
    const liko = this.pillarIndex.slice();
    const querypt = junctionPoint(source);
    const gnd = groundLevel(this.sm);

    while (liko.length) {
      const qp = [querypt[0], querypt[1], gnd];
      let bi = 0, bd = Infinity;
      for (let i = 0; i < liko.length; i++) {
        const d = distance3(liko[i].pos, qp);
        if (d < bd) { bd = d; bi = i; }
      }
      const ne = liko[bi];
      const np = this.tree.pillars[ne.id];
      if (np && this.connectToNearpillar(source, ne.id) && np.rStart >= source.rBackMm)
        return true;
      liko.splice(bi, 1);
    }
    return false;
  }

  /* -------------------------------------------------------- routing_to_ground */

  /**
   * `routing_to_ground` (DST.cpp:577-647).
   *
   * Kiekvienam klasteriui: centrine galvute gauna TIKRA stulpa, o likusios
   * prisikabina prie jo tiltais. Nepavykus - ieskoma bet kurio kito stulpo, o
   * jei ir to nera, statomas savas.
   */
  routingToGround() {
    const clCentroids = [];

    for (const cl of this.pillarClusters) {
      if (!cl.length) continue;
      const lcid = clusterCentroid(cl, i => this.points[i].pos,
                                   (p1, p2) => dist2d(p1, p2));
      const hid = cl[lcid];
      clCentroids.push(hid);

      const h = this.heads[hid];
      if (!this.createGroundPillar(headJunction(h), h.dir, h.id)) {
        /* Stulpo pastatyti nepavyko - galvute keliauja i „ant modelio" grupe. */
        this.iheadsOnModel.push(h.id);
      }
    }

    let ci = 0;
    for (const cl of this.pillarClusters) {
      if (!cl.length) continue;
      const cidx = clCentroids[ci++];
      if (cidx === undefined) continue;

      /* Artimiausias stulpas prie centrines galvutes jungties. */
      const qp = junctionPoint(this.heads[cidx]);
      let best = null, bd = Infinity;
      for (const e of this.pillarIndex) {
        const d = distance3(e.pos, qp);
        if (d < bd) { bd = d; best = e; }
      }
      if (!best) continue;

      for (const c of cl) {
        if (c === cidx) continue;
        const sidehead = this.heads[c];
        if (!this.connectToNearpillar(sidehead, best.id) &&
            !this.searchPillarAndConnect(sidehead)) {
          this.createGroundPillar(headJunction(sidehead), sidehead.dir, sidehead.id);
        }
      }
    }
  }
}
