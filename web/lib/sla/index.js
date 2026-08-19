/*
 * sVi Slicer - SLA support generation ported from PrusaSlicer (libslic3r).
 * Copyright (C) 2016-2024 Prusa Research s.r.o. - original C++ implementation
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * AGPL-3.0-or-later. See LICENSE-AGPL.md in this directory.
 *
 * Visa grandine viename kvietime - ta pacia tvarka, kaip SLAPrint:
 *   pjaustymas -> sejos paruosimas -> tasku generavimas -> medis -> padas
 */
import { AABBMesh } from './mesh.js';
import { sliceMeshEx } from './mesh-slicer.js';
import { prepareGeneratorData, generateSupportPoints, generatorConfig, prepareConfig } from './support-point-generator.js';
import { DefaultSupportTree } from './default-support-tree.js';
import { defaultConfig } from './config.js';
import { padConfig, padContour, padBlueprint } from './pad.js';
import { exsToPaths } from './geometry.js';

/**
 * @param CL   ClipperLib
 * @param pos  trikampiai (Float32Array, 9 skaiciai vienam)
 * @param opts { layerHeight, cfg, padCfg, genCfg, prepCfg, onProgress }
 */
export async function buildSupports(CL, pos, opts = {}) {
  const layerHeight = opts.layerHeight || 0.05;
  const cfg = opts.cfg || defaultConfig();
  const pcfg = opts.padCfg || padConfig();
  const gcfg = opts.genCfg || generatorConfig();
  const prcfg = opts.prepCfg || prepareConfig();
  const say = opts.onProgress || (() => {});
  const log = {};
  const t = () => Number(process?.hrtime?.bigint?.() ?? 0n) / 1e6;

  const mesh = new AABBMesh(pos);
  const gnd = mesh.min[2];

  /* 1. Pjaustymas. Sluoksnio VIDURYS, ne riba - kitaip plokstuma eitu tiksliai
        per virsunes, ir riba „auksciausia briauna priklauso" tapt u loterija. */
  let t0 = t();
  const zs = [];
  for (let z = gnd + layerHeight / 2; z < mesh.max[2]; z += layerHeight) zs.push(z);
  const slices = sliceMeshEx(CL, pos, zs);
  log.slice = Math.round(t() - t0); say('slice', 1);

  /* 2. Sejos paruosimas */
  t0 = t();
  const data = prepareGeneratorData(CL, slices, zs, prcfg);
  log.prepare = Math.round(t() - t0); say('prepare', 1);

  /* 3. Tasku generavimas */
  t0 = t();
  const gen = generateSupportPoints(CL, data, gcfg);
  log.points = Math.round(t() - t0); say('points', 1);

  /* 4. Medis */
  t0 = t();
  const sm = { cfg, zoffset: gnd, padCfg: { wallThicknessMm: pcfg.wallThicknessMm } };
  const pts = gen.points.map(p => ({ pos: p.pos, headFrontRadius: p.headFrontRadius }));
  const tree = pts.length ? DefaultSupportTree.execute(sm, mesh, pts) : null;
  log.tree = Math.round(t() - t0); say('tree', 1);

  /* 5. Padas. Atramu siluetas - stulpu pedu vietos; modelio - is pjaustymo. */
  t0 = t();
  let pad = [];
  if (tree) {
    const suppBp = [];
    const RING = 16;
    for (const p of tree.pillars) {
      const c = p.endpt, r = Math.max(p.rEnd, cfg.baseRadiusMm);
      const ring = [];
      for (let i = 0; i < RING; i++) {
        const a = i / RING * 2 * Math.PI;
        ring.push({ X: Math.round((c[0] + r * Math.cos(a)) * 1e6), Y: Math.round((c[1] + r * Math.sin(a)) * 1e6) });
      }
      suppBp.push({ contour: ring, holes: [] });
    }
    const modelBp = padBlueprint(CL, pos, gnd, pcfg.wallThicknessMm + layerHeight, layerHeight);
    pad = padContour(CL, suppBp, modelBp, pcfg);
  }
  log.pad = Math.round(t() - t0); say('pad', 1);

  return { mesh, slices, zs, data, points: gen.points, skippedIslands: gen.skippedIslands, tree, pad, log };
}

export { AABBMesh, sliceMeshEx, prepareGeneratorData, generateSupportPoints,
         DefaultSupportTree, defaultConfig, padConfig, generatorConfig, prepareConfig };
