/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/BranchingTree/{BranchingTree,PointCloud}.{hpp,cpp}
 *              ir SLA/SupportTreeUtils.hpp (find_merge_pt)
 *
 * Tai ANTRAS medzio tipas - `support_tree_type = branching`. V profilyje
 * naudojamas `default`, bet V prase turėti abu, kad butu galima perjungti.
 */
import { add, sub, mul, norm, normalized, distance3, EPSILON } from './support-tree-utils.js';

export const PtType = { LEAF: 'leaf', MESH: 'mesh', BED: 'bed', JUNCTION: 'junction', NONE: 'none' };
export const ID_NONE = -1;

/** `Properties` (BT.hpp:24-70). */
export const branchingProperties = (over = {}) => ({
  maxSlope: Math.PI / 4,
  groundLevel: 0,
  samplingRadius: 0.5,
  maxBranchLength: 10,
  ...over,
});

/** `Node` (BT.hpp:74-89). `weight` yra visu is jos iseinanciu saku ilgiu suma. */
export const bnode = (pos, Rmin = 0) => ({
  id: ID_NONE, left: ID_NONE, right: ID_NONE, pos, Rmin, weight: 0,
});
export const isOccupied = n => n.left !== ID_NONE && n.right !== ID_NONE;

/**
 * `find_merge_pt` (SupportTreeUtils.hpp:875-944).
 *
 * Kur susilieja dvi sakos, atejusios is A ir B, jei abi gali kristi ne
 * staciau nei `criticalAngle`.
 *
 * Uzdavinys sumazinamas i 2D: vertikali pjuvio plokstuma, kurios X asis yra
 * AB XY kryptis, o Y asis - Z. Tada ieskoma dvieju spinduliu sankirtos.
 *
 * ⚠️ Dirbama su SINUSO KVADRATU su zenklu (`b.y * abs(b.y) / b_sqn`), o ne su
 * kampu. Taip isvengiama `atan2`/`asin` ir isliekamas zenklas; saturacija
 * daroma `min` su `-sin(crit)^2`. Perrasant „per kampus" rezultatas skirtusi
 * ties beveik horizontaliomis sakomis.
 */
export function findMergePt(A, B, criticalAngle) {
  const diff = [B[0] - A[0], B[1] - A[1], 0];
  const L = Math.hypot(diff[0], diff[1]);
  const dir = L > 0 ? [diff[0] / L, diff[1] / L, 0] : [0, 0, 0];

  /* tr2D: eilute 0 = dir, eilute 1 = (0,0,1). */
  const BA = sub(B, A);
  const b = [dir[0] * BA[0] + dir[1] * BA[1] + dir[2] * BA[2], BA[2]];

  const bSqn = b[0] * b[0] + b[1] * b[1];
  let sin2sigA = bSqn > EPSILON ? (b[1] * Math.abs(b[1])) / bSqn : 0;
  let sin2sigB = -sin2sigA;

  const sincrit = Math.sin(criticalAngle);
  const sin2crit = -sincrit * sincrit;
  sin2sigA = Math.min(sin2sigA, sin2crit);
  sin2sigB = Math.min(sin2sigB, sin2crit);

  const sin2a = Math.abs(sin2sigA), sin2b = Math.abs(sin2sigB);
  const cos2a = 1 - sin2a, cos2b = 1 - sin2b;
  const cs = (v, s) => (s < 0 ? -Math.abs(v) : Math.abs(v));

  const Da = [cs(Math.sqrt(cos2a), b[0]), cs(Math.sqrt(sin2a), sin2sigA)];
  const Db = [-cs(Math.sqrt(cos2b), b[0]), cs(Math.sqrt(sin2b), sin2sigB)];

  const den = Da[0] * Db[1] - Da[1] * Db[0];
  if (Math.abs(den) < 1e-12) return null;
  const t1 = (Db[1] * b[0] - b[1] * Db[0]) / den;
  if (t1 < 0) return null;

  const mp = [t1 * Da[0], t1 * Da[1]];
  /* tr2D transponuota: grazinam is 2D i 3D. */
  return [A[0] + dir[0] * mp[0], A[1] + dir[1] * mp[0], A[2] + mp[1]];
}

/**
 * `PointCloud` (PointCloud.{hpp,cpp}) - supaprastinta iki to, ka naudoja
 * `build_tree`. Mazgu tipas nustatomas pagal indekso ruoza, kaip originale:
 * lovos taskai, mesh taskai, lapai, jungtys.
 */
export class PointCloud {
  constructor(bedpoints, meshpoints, leafs, props) {
    this.props = props;
    this.bedpoints = bedpoints;
    this.meshpoints = meshpoints;
    this.leafs = leafs;
    this.junctions = [];
    this.MESHPTS_BEGIN = bedpoints.length;
    this.LEAFS_BEGIN = this.MESHPTS_BEGIN + meshpoints.length;
    this.JUNCTIONS_BEGIN = this.LEAFS_BEGIN + leafs.length;
    this.reachable = new Array(this.JUNCTIONS_BEGIN).fill(true);
    this.queueIdx = new Map();
  }

  nodeType(id) {
    if (id < this.MESHPTS_BEGIN && this.bedpoints.length) return PtType.BED;
    if (id < this.LEAFS_BEGIN && this.meshpoints.length) return PtType.MESH;
    if (id < this.JUNCTIONS_BEGIN && this.leafs.length) return PtType.LEAF;
    if (id >= this.JUNCTIONS_BEGIN && this.junctions.length) return PtType.JUNCTION;
    return PtType.NONE;
  }

  get(id) {
    switch (this.nodeType(id)) {
      case PtType.BED: return this.bedpoints[id];
      case PtType.MESH: return this.meshpoints[id - this.MESHPTS_BEGIN];
      case PtType.LEAF: return this.leafs[id - this.LEAFS_BEGIN];
      case PtType.JUNCTION: return this.junctions[id - this.JUNCTIONS_BEGIN];
      default: return null;
    }
  }

  markUnreachable(id) { if (id < this.reachable.length) this.reachable[id] = false; }
  nextJunctionId() { return this.JUNCTIONS_BEGIN + this.junctions.length; }

  insertJunction(node) {
    node.id = this.nextJunctionId();
    this.junctions.push(node);
    this.reachable.push(true);
    return node.id;
  }

  /**
   * `foreach_reachable` - K artimiausiu pasiekiamu mazgu.
   *
   * ⚠️ Atstumas VERTINAMAS DVIEM budais: `dst_euql` (paprastas 3D) ir
   * `dst_branching`, kuris atmeta mazgus, i kuriuos sakа nepasiektu neperzengusi
   * kritinio kampo. Rusiuojama pagal `dst_branching`, o `dst_euql` naudojamas
   * paieskos spinduliui.
   */
  foreachReachable(pos, fn, K, prevDistMax) {
    const kand = [];
    const total = this.JUNCTIONS_BEGIN + this.junctions.length;
    for (let id = 0; id < total; id++) {
      if (!this.reachable[id]) continue;
      const n = this.get(id);
      if (!n) continue;
      /* Saka gali eiti tik ZEMYN. */
      if (n.pos[2] > pos[2] + EPSILON) continue;
      const de = distance3(n.pos, pos);
      if (de <= prevDistMax) continue;
      const mp = findMergePt(pos, n.pos, this.props.maxSlope);
      const db = mp ? distance3(pos, mp) + distance3(mp, n.pos) : de;
      kand.push({ id, dstBranching: db, dstEuql: de });
    }
    kand.sort((a, b) => a.dstEuql - b.dstEuql);
    for (const k of kand.slice(0, K)) fn(k.id, k.dstBranching, k.dstEuql);
  }

  startQueue() {
    /* `ZCompareFn` - eile pagal Z, auksciausi pirma (lapai jungiami is virsaus). */
    const q = this.leafs.map((_, i) => this.LEAFS_BEGIN + i);
    q.sort((a, b) => this.get(b).pos[2] - this.get(a).pos[2]);
    return q;
  }
}

/**
 * `build_tree` (BranchingTree.cpp:15-175).
 *
 * Kiekvienas lapas jungiamas su artimiausiu pasiekiamu mazgu. Sprendimas
 * priklauso nuo to, kas tas mazgas:
 *   BED      - tiltas i plokste (arba aplinkkelis, jei per toli);
 *   MESH     - tiltas i modeli;
 *   LEAF/JUNCTION - abi sakos susilieja NAUJAME mazge (`find_merge_pt`), ir
 *                   tas mazgas grizta i eile - taip auga medis.
 *
 * ⚠️ `K` didinamas ir `prev_dist_max` iSsaugomas, kai nepavyko - kita karta
 * ieskoma PLACIAU, praleidziant jau tikrintus. Be to nepasiekiami lapai butu
 * numetami po pirmo bandymo.
 */
export function buildTree(nodes, builder) {
  const initK = 5;
  const queue = nodes.startQueue();
  const props = nodes.props;

  let prevDistMax = 0;
  let K = initK;
  let routed = true;
  let nodeId = ID_NONE;

  while ((queue.length && builder.isValid()) || !routed) {
    if (routed) { nodeId = queue.shift(); if (nodeId === undefined) break; }

    const node = nodes.get(nodeId);
    if (!node) break;
    nodes.markUnreachable(nodeId);

    const distances = [];
    let dmax = 0;
    nodes.foreachReachable(node.pos, (id, db, de) => {
      distances.push({ id, dstBranching: db, dstEuql: de });
      dmax = Math.max(dmax, de);
    }, K, prevDistMax);
    distances.sort((a, b) => a.dstBranching - b.dstBranching);

    if (!distances.length) {
      builder.reportUnroutable(node);
      K = initK; prevDistMax = 0; routed = true;
      continue;
    }

    routed = false;
    for (const d of distances) {
      if (routed) break;
      const closestId = d.id;
      const closest = nodes.get(closestId);
      if (!closest) continue;
      const type = nodes.nodeType(closestId);
      let w = nodes.get(nodeId).weight + d.dstBranching;
      closest.Rmin = Math.max(node.Rmin, closest.Rmin);

      if (type === PtType.BED) {
        closest.weight = w;
        if (d.dstBranching > props.maxBranchLength) {
          const avo = builder.suggestAvoidance(node, props.maxBranchLength);
          if (!avo) continue;
          const nn = bnode(avo, node.Rmin);
          nn.weight = nodes.get(nodeId).weight + norm(sub(node.pos, avo));
          nn.left = node.id;
          if ((routed = builder.addBridge(node, nn))) queue.push(nodes.insertJunction(nn));
        } else if ((routed = builder.addGroundBridge(node, closest))) {
          closest.left = closest.right = nodeId;
          nodes.markUnreachable(closestId);
        }
      } else if (type === PtType.MESH) {
        closest.weight = w;
        if ((routed = builder.addMeshBridge(node, closest))) {
          closest.left = closest.right = nodeId;
          nodes.markUnreachable(closestId);
        }
      } else if (type === PtType.LEAF || type === PtType.JUNCTION) {
        const mergept = findMergePt(node.pos, closest.pos, props.maxSlope);
        if (!mergept) continue;
        const mdC = distance3(mergept, closest.pos);
        const mdN = distance3(mergept, node.pos);
        const Wsum = Math.max(nodes.get(nodeId).weight, nodes.get(closestId).weight);
        w = Wsum + Math.max(mdC, mdN);

        if (mdC > EPSILON && mdN > EPSILON) {
          const mn = bnode(mergept, closest.Rmin);
          mn.weight = w;
          mn.id = nodes.nextJunctionId();
          if ((routed = builder.addMerger(node, closest, mn))) {
            mn.left = nodeId; mn.right = closestId;
            queue.push(nodes.insertJunction(mn));
            const qi = queue.indexOf(closestId);
            if (qi >= 0) queue.splice(qi, 1);
            nodes.markUnreachable(closestId);
          }
        } else if (closest.pos[2] < node.pos[2] &&
                   (closest.left === ID_NONE || closest.right === ID_NONE)) {
          closest.weight = w;
          if ((routed = builder.addBridge(node, closest))) {
            if (closest.left === ID_NONE) closest.left = nodeId;
            else if (closest.right === ID_NONE) closest.right = nodeId;
          }
        }
      }
    }

    if (routed) { prevDistMax = 0; K = initK; }
    else { prevDistMax = dmax; K += initK; if (K > 100) { routed = true; K = initK; prevDistMax = 0; } }
  }
}
