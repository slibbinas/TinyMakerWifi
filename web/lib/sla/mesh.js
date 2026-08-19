/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Portuota is: src/libslic3r/AABBMesh.{hpp,cpp} - jo VIESA SASAJA:
 *   query_ray_hit(from, dir) -> {distance, is_inside, normal, face}
 *   squared_distance(point)  -> artimiausio pavirsiaus atstumo kvadratas
 *
 * ⚠️ Kas cia yra portas ir kas ne:
 *
 * Originalas viduje naudoja AABB medi (igl::AABB). Cia vietoj jo - vienodo
 * zingsnio XY gardele su DDA. Tai SANDELIAVIMO budas, ne algoritmas: abu
 * atsako i ta pati klausima („artimiausias trikampis palei spinduli") ir turi
 * duoti TA PATI atsakyma. Portuojamas yra rezultatas ir jo semantika, o ne
 * medzio apejimo tvarka.
 *
 * Butent todel `is_inside` cia igyvendintas taip pat, kaip originale: pagal
 * trikampio normales ir spindulio krypties sutapima. Nuo jo priklauso, ar
 * `pinhead_mesh_hit` permes spinduli, ar palaikys kelia uzblokuotu.
 */

const INF = Infinity;

export class AABBMesh {
  /*
   * Langelis 0,5 mm, ne 2,0 (ISMATUOTA 2026-08-19, evil 490k trikampiu,
   * `node --cpu-prof`): su 2,0 gardele detalei ~30x30 mm iseina ~250 langeliu,
   * t. y. ~1900 trikampiu viename, ir `triHit` suvalgo 51 % viso laiko. Su 0,5
   * medzio etapas 188 -> 25 s, visa grandine 299 -> 103 s, o rezultatas
   * nesikeicia ne vienu skaiciumi (110 tasku / 37 stulpai / 18 tiltu).
   *
   * PLEISTRAS, ne portas: originale cia `igl::AABB` medis (kompromisas B1).
   * Gardeles greitis priklauso nuo detales gabarito, medzio - ne. Dideliam
   * modeliui reikes medzio.
   *
   * PATIKRINTA IR ATMESTA sename `slicer2.js` kelyje: ten tas pats smulkinimas
   * nedave NIEKO, nes ten butelio kaklelis - Clipper (71 %), o ray casting
   * < 0,8 %. Kaklelis matuojamas kiekvienam moduliui atskirai.
   *
   * @param pos Float32Array/Array su trikampiais: 9 skaiciai vienam.
   */
  constructor(pos, cell = 0.5) {
    this.pos = pos;
    this.cell = cell;
    this.map = new Map();

    let x0 = INF, y0 = INF, z0 = INF, x1 = -INF, y1 = -INF, z1 = -INF;
    for (let i = 0; i < pos.length; i += 3) {
      if (pos[i] < x0) x0 = pos[i]; if (pos[i] > x1) x1 = pos[i];
      if (pos[i + 1] < y0) y0 = pos[i + 1]; if (pos[i + 1] > y1) y1 = pos[i + 1];
      if (pos[i + 2] < z0) z0 = pos[i + 2]; if (pos[i + 2] > z1) z1 = pos[i + 2];
    }
    this.min = [x0, y0, z0];
    this.max = [x1, y1, z1];
    this.nx = Math.max(1, Math.ceil((x1 - x0) / cell) + 1);
    this.ny = Math.max(1, Math.ceil((y1 - y0) / cell) + 1);

    for (let t = 0; t + 8 < pos.length; t += 9) {
      const ax = Math.min(pos[t], pos[t + 3], pos[t + 6]);
      const bx = Math.max(pos[t], pos[t + 3], pos[t + 6]);
      const ay = Math.min(pos[t + 1], pos[t + 4], pos[t + 7]);
      const by = Math.max(pos[t + 1], pos[t + 4], pos[t + 7]);
      const i0 = this.cx(ax), i1 = this.cx(bx);
      const j0 = this.cy(ay), j1 = this.cy(by);
      for (let j = j0; j <= j1; j++)
        for (let i = i0; i <= i1; i++) {
          const k = j * this.nx + i;
          let l = this.map.get(k);
          if (!l) { l = []; this.map.set(k, l); }
          l.push(t);
        }
    }
  }

  cx(x) { return Math.max(0, Math.min(this.nx - 1, Math.floor((x - this.min[0]) / this.cell))); }
  cy(y) { return Math.max(0, Math.min(this.ny - 1, Math.floor((y - this.min[1]) / this.cell))); }

  /** Möller-Trumbore. `inside` - ar normale su spinduliu vienakrypte. */
  triHit(t, o, d) {
    const p = this.pos;
    const ax = p[t], ay = p[t + 1], az = p[t + 2];
    const e1x = p[t + 3] - ax, e1y = p[t + 4] - ay, e1z = p[t + 5] - az;
    const e2x = p[t + 6] - ax, e2y = p[t + 7] - ay, e2z = p[t + 8] - az;
    const hx = d[1] * e2z - d[2] * e2y;
    const hy = d[2] * e2x - d[0] * e2z;
    const hz = d[0] * e2y - d[1] * e2x;
    const a = e1x * hx + e1y * hy + e1z * hz;
    if (Math.abs(a) < 1e-12) return null;
    const f = 1 / a;
    const sx = o[0] - ax, sy = o[1] - ay, sz = o[2] - az;
    const u = f * (sx * hx + sy * hy + sz * hz);
    if (u < 0 || u > 1) return null;
    const qx = sy * e1z - sz * e1y;
    const qy = sz * e1x - sx * e1z;
    const qz = sx * e1y - sy * e1x;
    const v = f * (d[0] * qx + d[1] * qy + d[2] * qz);
    if (v < 0 || u + v > 1) return null;
    const dist = f * (e2x * qx + e2y * qy + e2z * qz);
    if (dist <= 1e-12) return null;
    return { dist, inside: a < 0 };
  }

  /** `query_ray_hit` (AABBMesh.cpp). Grazina {dist, inside}. */
  rayHit(o, d) {
    let best = { dist: INF, inside: false };
    let ix = this.cx(o[0]), iy = this.cy(o[1]);
    const stepX = d[0] > 0 ? 1 : -1, stepY = d[1] > 0 ? 1 : -1;
    const c = this.cell;
    const bx = this.min[0] + (ix + (d[0] > 0 ? 1 : 0)) * c;
    const by = this.min[1] + (iy + (d[1] > 0 ? 1 : 0)) * c;
    let tMaxX = Math.abs(d[0]) > 1e-12 ? (bx - o[0]) / d[0] : INF;
    let tMaxY = Math.abs(d[1]) > 1e-12 ? (by - o[1]) / d[1] : INF;
    const tDeltaX = Math.abs(d[0]) > 1e-12 ? c / Math.abs(d[0]) : INF;
    const tDeltaY = Math.abs(d[1]) > 1e-12 ? c / Math.abs(d[1]) : INF;
    let travelled = 0;
    const far = 400;
    for (let guard = 0; guard < 8192; guard++) {
      const list = this.map.get(iy * this.nx + ix);
      if (list) for (const t of list) {
        const h = this.triHit(t, o, d);
        if (h && h.dist < best.dist) best = h;
      }
      if (best.dist < travelled) break;
      if (tMaxX < tMaxY) { travelled = tMaxX; ix += stepX; tMaxX += tDeltaX; }
      else { travelled = tMaxY; iy += stepY; tMaxY += tDeltaY; }
      if (travelled > far) break;
      if (ix < 0 || ix >= this.nx || iy < 0 || iy >= this.ny) break;
    }
    return best;
  }

  /** Kvadratinis atstumas nuo tasko iki trikampio (Ericson, RTCD). */
  pointTriDist2(p, t) {
    const q = this.pos;
    const a = [q[t], q[t + 1], q[t + 2]];
    const b = [q[t + 3], q[t + 4], q[t + 5]];
    const c = [q[t + 6], q[t + 7], q[t + 8]];
    const sub = (u, v) => [u[0] - v[0], u[1] - v[1], u[2] - v[2]];
    const dot = (u, v) => u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    const ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return dot(ap, ap);
    const bp = sub(p, b), d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return dot(bp, bp);
    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
      const s = d1 / (d1 - d3);
      const w = [ap[0] - ab[0] * s, ap[1] - ab[1] * s, ap[2] - ab[2] * s];
      return dot(w, w);
    }
    const cp = sub(p, c), d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return dot(cp, cp);
    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      const s = d2 / (d2 - d6);
      const w = [ap[0] - ac[0] * s, ap[1] - ac[1] * s, ap[2] - ac[2] * s];
      return dot(w, w);
    }
    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
      const s = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      const cb = sub(c, b), pb = sub(p, b);
      const w = [pb[0] - cb[0] * s, pb[1] - cb[1] * s, pb[2] - cb[2] * s];
      return dot(w, w);
    }
    const den = 1 / (va + vb + vc);
    const vv = vb * den, ww = vc * den;
    const w = [ap[0] - ab[0] * vv - ac[0] * ww,
               ap[1] - ab[1] * vv - ac[1] * ww,
               ap[2] - ab[2] * vv - ac[2] * ww];
    return dot(w, w);
  }

  /** `squared_distance(p)` - naudoja `check_ground_route` nuliniam pakelimui.
   *  Su `want` grazina ir artimiausia trikampi bei taska ant jo (originale tai
   *  `squared_distance(p, faceid, closest)` isvesties parametrai). */
  squaredDistance(p, want = false) {
    let best = INF, bt = -1;
    for (let w = this.cell; w <= this.cell * 16; w *= 2) {
      const i0 = this.cx(p[0] - w), i1 = this.cx(p[0] + w);
      const j0 = this.cy(p[1] - w), j1 = this.cy(p[1] + w);
      for (let j = j0; j <= j1; j++)
        for (let i = i0; i <= i1; i++) {
          const l = this.map.get(j * this.nx + i);
          if (!l) continue;
          for (const t of l) {
            const d = this.pointTriDist2(p, t);
            if (d < best) { best = d; bt = t; }
          }
        }
      if (best < w * w) break;
    }
    return want ? { d2: best, t: bt, q: bt < 0 ? null : this.closestOnTri(p, bt) } : best;
  }

  /** Artimiausias taskas ant trikampio (reikia `get_normal` briaunos patikrai). */
  closestOnTri(p, t) {
    const q = this.pos;
    const a = [q[t], q[t + 1], q[t + 2]];
    const ab = [q[t + 3] - a[0], q[t + 4] - a[1], q[t + 5] - a[2]];
    const ac = [q[t + 6] - a[0], q[t + 7] - a[1], q[t + 8] - a[2]];
    const ap = [p[0] - a[0], p[1] - a[1], p[2] - a[2]];
    const dot = (u, v) => u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    const d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const bp = [p[0] - q[t + 3], p[1] - q[t + 4], p[2] - q[t + 5]];
    const d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return [q[t + 3], q[t + 4], q[t + 5]];
    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
      const s = d1 / (d1 - d3);
      return [a[0] + ab[0] * s, a[1] + ab[1] * s, a[2] + ab[2] * s];
    }
    const cp = [p[0] - q[t + 6], p[1] - q[t + 7], p[2] - q[t + 8]];
    const d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return [q[t + 6], q[t + 7], q[t + 8]];
    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      const s = d2 / (d2 - d6);
      return [a[0] + ac[0] * s, a[1] + ac[1] * s, a[2] + ac[2] * s];
    }
    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
      const s = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      return [q[t + 3] + (q[t + 6] - q[t + 3]) * s,
              q[t + 4] + (q[t + 7] - q[t + 4]) * s,
              q[t + 5] + (q[t + 8] - q[t + 5]) * s];
    }
    const den = 1 / (va + vb + vc);
    const vv = vb * den, ww = vc * den;
    return [a[0] + ab[0] * vv + ac[0] * ww,
            a[1] + ab[1] * vv + ac[1] * ww,
            a[2] + ab[2] * vv + ac[2] * ww];
  }

  /** `normal_by_face_id` - trikampio normale. */
  faceNormal(t) {
    const q = this.pos;
    const ux = q[t + 3] - q[t], uy = q[t + 4] - q[t + 1], uz = q[t + 5] - q[t + 2];
    const vx = q[t + 6] - q[t], vy = q[t + 7] - q[t + 1], vz = q[t + 8] - q[t + 2];
    const n = [uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx];
    const L = Math.hypot(n[0], n[1], n[2]) || 1;
    return [n[0] / L, n[1] / L, n[2] / L];
  }

  /** `vertex_face_index` - virsune -> ja naudojantys trikampiai. */
  vertexFaceIndex() {
    if (this._vfi) return this._vfi;
    const m = new Map();
    const key = (x, y, z) => `${x.toFixed(5)},${y.toFixed(5)},${z.toFixed(5)}`;
    const q = this.pos;
    for (let t = 0; t + 8 < q.length; t += 9)
      for (let k = 0; k < 3; k++) {
        const kk = key(q[t + k * 3], q[t + k * 3 + 1], q[t + k * 3 + 2]);
        let l = m.get(kk); if (!l) { l = []; m.set(kk, l); }
        l.push(t);
      }
    this._vfi = { map: m, key };
    return this._vfi;
  }

  /** `face_neighbor_index` - trikampis, dalijantis duota briauna. */
  faceAcrossEdge(t, e1, e2) {
    const { map, key } = this.vertexFaceIndex();
    const c1 = map.get(key(e1[0], e1[1], e1[2])) || [];
    for (const o of c1) {
      if (o === t) continue;
      const q = this.pos;
      let sutampa = 0;
      for (let k = 0; k < 3; k++) {
        const px = q[o + k * 3], py = q[o + k * 3 + 1], pz = q[o + k * 3 + 2];
        if ((Math.abs(px - e1[0]) < 1e-5 && Math.abs(py - e1[1]) < 1e-5 && Math.abs(pz - e1[2]) < 1e-5) ||
            (Math.abs(px - e2[0]) < 1e-5 && Math.abs(py - e2[1]) < 1e-5 && Math.abs(pz - e2[2]) < 1e-5)) sutampa++;
      }
      if (sutampa === 2) return o;
    }
    return -1;
  }
}
