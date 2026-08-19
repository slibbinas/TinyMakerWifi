/*
 * sVi Slicer - optimizatorius, portuotas is NLopt.
 * Copyright (C) 2007-2020 Massachusetts Institute of Technology (Steven G. Johnson)
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * NLopt platinamas pagal MIT licencija; sis portas eina kartu su `web/lib/sla/`
 * pagal AGPL-3.0-or-later (MIT su ja suderinama).
 *
 * Portuota is: src/algs/mlsl/mlsl.c
 *   MLSL = Multi-Level Single-Linkage (Kan & Timmer)
 */
import { sbplxMinimize } from './nlopt-subplex.js';
import { NLOPT, stopCriteria } from './nlopt-neldermead.js';

/* mlsl.c:274-275 */
const MLSL_SIGMA = 2.0;
const MLSL_GAMMA = 0.3;
const K2PI = 6.2831853071795864769252867665590057683943388;

/**
 * `gam` (mlsl.c:227-237): Gamma(1 + n/2)^(1/n) per Stirlingo aproksimacija.
 * Originalo komentaras sako, kad tikslumo uztenka (klaida <2 % kai n=3) ir kad
 * taip isvengiama perpildymo.
 */
const gam = n => {
  const z = n / 2;
  return Math.sqrt(Math.pow(K2PI * z, 1 / n) * z) * Math.exp(-0.5);
};

const dist2 = (a, b) => {
  let s = 0;
  for (let i = 0; i < a.length; i++) s += (a[i] - b[i]) ** 2;
  return s;
};

/**
 * `is_potential_minimizer` (mlsl.c:196-221).
 *
 * Taskas verta lokalios paieskos TIK jei:
 *   - is jo dar nebuvo ieskota,
 *   - artimiausias KITAS taskas toliau nei R,
 *   - artimiausias jau rastas minimumas toliau nei dlm*R,
 *   - jis nera per arti RIBU.
 *
 * Butent tai ir yra „single-linkage": paieska nekartojama ten, kur jau buvo
 * ieskota, ir del to globalus biudzetas (pas mus 5000) isnaudojamas skirtingoms
 * srities dalims, o ne tam paciam sleniui.
 */
function isPotentialMinimizer(p, dptMin, dlmMin, dboundMin, lb, ub) {
  if (p.minimized) return false;
  if (p.closestPtD <= dptMin * dptMin) return false;
  if (p.closestLmD <= dlmMin * dlmMin) return false;
  for (let i = 0; i < p.x.length; i++)
    if ((p.x[i] - lb[i] <= dboundMin || ub[i] - p.x[i] <= dboundMin) &&
        ub[i] - lb[i] > dboundMin) return false;
  return true;
}

/**
 * `mlsl_minimize` (mlsl.c:277-437).
 *
 * @param localMaxeval lokalios paieskos biudzetas (originale is `local_opt`)
 * @param rng          [0,1) generatorius - determinizmui (originale seed(0))
 */
export function mlslMinimize(n, f, lb, ub, x0, stop, localMaxeval = 100, Nsamples = 4, rng = null) {
  let ret = NLOPT.SUCCESS;
  const N = Nsamples || 4;
  const gamma = MLSL_GAMMA;

  /* `R_prefactor` (mlsl.c:315-317) - i ji ieina VISU asiu ilgiai. */
  let Rprefactor = Math.sqrt(2 / K2PI) * Math.pow(gam(n) * MLSL_SIGMA, 1 / n);
  for (let i = 0; i < n; i++) Rprefactor *= Math.pow(ub[i] - lb[i], 1 / n);

  const dlm = 1.0, dbound = 1e-6;

  /* Determinizmas: originale `solver.seed(0)` - be jo tas pats modelis duotu
     skirtingas atramas. Sobolio seka nenaudojam (`lds = 0` atitikmuo). */
  let rnd = 12345 >>> 0;
  const urand = rng || (() => ((rnd = (rnd * 1664525 + 1013904223) >>> 0) / 4294967296));

  const pts = [];   // {x, f, minimized, closestPtD, closestLmD}
  const lms = [];   // {x, f}

  const naujasTaskas = xv => ({
    x: xv, f: f(xv), minimized: false,
    closestPtD: Infinity, closestLmD: Infinity,
  });

  const p0 = naujasTaskas(x0.slice());
  stop.nevals++;
  pts.push(p0);
  if (stop.maxeval > 0 && stop.nevals >= stop.maxeval) ret = NLOPT.MAXEVAL_REACHED;
  else if (p0.f < stop.minfMax) ret = NLOPT.MINF_MAX_REACHED;

  const geriausias = () => {
    let b = pts[0];
    for (const p of pts) if (p.f < b.f) b = p;
    for (const l of lms) if (l.f < b.f) b = l;
    return b;
  };

  while (ret === NLOPT.SUCCESS) {
    /* Semimo faze: N atsitiktiniu tasku. */
    for (let i = 0; i < N && ret === NLOPT.SUCCESS; i++) {
      const xv = new Array(n);
      for (let j = 0; j < n; j++) xv[j] = lb[j] + urand() * (ub[j] - lb[j]);
      const p = naujasTaskas(xv);
      stop.nevals++;
      pts.push(p);

      if (stop.maxeval > 0 && stop.nevals >= stop.maxeval) ret = NLOPT.MAXEVAL_REACHED;
      else if (p.f < stop.minfMax) ret = NLOPT.MINF_MAX_REACHED;
      else {
        /* `find_closest_pt` - artimiausias tarp GERESNIU uz ji tasku. */
        for (const q of pts) {
          if (q === p) continue;
          if (q.f <= p.f) {
            const d = dist2(q.x, p.x);
            if (d < p.closestPtD) p.closestPtD = d;
          } else {
            /* `pts_update_newpt`: naujas taskas gali tapti artimiausiu KITIEMS. */
            const d = dist2(q.x, p.x);
            if (d < q.closestPtD) q.closestPtD = d;
          }
        }
        for (const l of lms) {
          const d = dist2(l.x, p.x);
          if (d < p.closestLmD) p.closestLmD = d;
        }
      }
    }
    if (ret !== NLOPT.SUCCESS) break;

    /* Atstumo slenkstis R (mlsl.c:378-380) - mazeja augant tasku skaiciui. */
    const R = Rprefactor * Math.pow(Math.log(pts.length) / pts.length, 1 / n);

    /* Lokalios paieskos faze: einam per GERIAUSIUS gamma*N tasku. */
    const rusiuoti = pts.slice().sort((a, b) => a.f - b.f);
    let kiek = Math.ceil(gamma * pts.length) + 0.5 | 0;

    for (const p of rusiuoti) {
      if (kiek-- <= 0 || ret !== NLOPT.SUCCESS) break;
      if (!isPotentialMinimizer(p, R, dlm * R, dbound * R, lb, ub)) continue;

      if (stop.maxeval > 0 && stop.nevals >= stop.maxeval) { ret = NLOPT.MAXEVAL_REACHED; break; }

      const liko = stop.maxeval > 0 ? stop.maxeval - stop.nevals : localMaxeval;
      const locStop = stopCriteria({
        maxeval: Math.min(localMaxeval, liko), minfMax: stop.minfMax,
        ftolRel: stop.ftolRel, ftolAbs: stop.ftolAbs,
        xtolRel: stop.xtolRel, xtolAbs: stop.xtolAbs,
      });
      const step = lb.map((l, i) => (ub[i] - l) * 0.1);
      const r = sbplxMinimize(n, f, lb, ub, p.x.slice(), step, locStop);
      stop.nevals += locStop.nevals;
      p.minimized = true;

      const lm = { x: r.x.slice(), f: r.minf };
      lms.push(lm);

      if (lm.f < stop.minfMax) { ret = NLOPT.MINF_MAX_REACHED; break; }
      if (stop.maxeval > 0 && stop.nevals >= stop.maxeval) { ret = NLOPT.MAXEVAL_REACHED; break; }

      /* `pts_update_newlm` - naujas minimumas priartina kitus taskus. */
      for (const q of pts) {
        const d = dist2(lm.x, q.x);
        if (d < q.closestLmD) q.closestLmD = d;
      }
    }
  }

  const b = geriausias();
  return { ret, x: b.x.slice(), minf: b.f };
}

export { stopCriteria, NLOPT };
