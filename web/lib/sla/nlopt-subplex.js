/*
 * sVi Slicer - optimizatorius, portuotas is NLopt.
 * Copyright (C) 2007-2020 Massachusetts Institute of Technology (Steven G. Johnson)
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * NLopt platinamas pagal MIT licencija; sis portas eina kartu su `web/lib/sla/`
 * pagal AGPL-3.0-or-later (MIT su ja suderinama).
 *
 * Portuota is: src/algs/neldermead/sbplx.c (Subplex, T. Rowan 1990)
 */
import { nldrmdMinimize, NLOPT, stopCriteria } from './nlopt-neldermead.js';

/* sbplx.c:31-32 */
const PSI = 0.25, OMEGA = 0.1;
const NSMIN = 2, NSMAX = 5;

/**
 * `sbplx_minimize` (sbplx.c:66-236).
 *
 * Subplex yra Nelder-Mead, taikomas ne visai erdvei, o POMAISIAMS: kintamieji
 * rusiuojami pagal tai, kiek jie pajudejo praeitame rate (|dx|), skaidomi i
 * grupes po 2-5, ir kiekvienai grupei paleidziamas atskiras Nelder-Mead.
 *
 * Butent tai ir yra priezastis, kodel jis iveikia sleni, kurio asis istriza:
 * grupe judinama KARTU, o ne po viena asi.
 *
 * ⚠️ Zingsniu perskaiciavimas gale (sbplx.c:210-229) turi du dalykus, kuriuos
 * lengva praleisti:
 *   - jei buvo tik VIENA grupe, mastelis yra fiksuotas `psi` (0,25), o ne
 *     santykis;
 *   - naujas zingsnis paveldi dx ZENKLA (`copysign`), o jei dx nulinis -
 *     zingsnis APVERCIAMAS. Be to paieska sukastu i ta pacia puse.
 */
export function sbplxMinimize(n, f, lb, ub, x0, xstep0, stop) {
  const x = x0.slice();
  let minf = f(x);
  stop.nevals++;
  if (minf < stop.minfMax) return { ret: NLOPT.MINF_MAX_REACHED, x, minf };
  if (stop.maxeval > 0 && stop.nevals >= stop.maxeval)
    return { ret: NLOPT.MAXEVAL_REACHED, x, minf };

  const xstep = xstep0.slice();
  let xprev = x.slice();
  const dx = new Array(n).fill(0);
  let ret = NLOPT.SUCCESS;

  for (;;) {
    let normi = 0, normdx = 0, nsubs = 0, fdiffMax = 0;
    xprev = x.slice();

    /* Indeksai rusiuojami pagal |dx| MAZEJANCIA tvarka. */
    const p = Array.from({ length: n }, (_, i) => i);
    p.sort((a, b) => Math.abs(dx[b]) - Math.abs(dx[a]));

    for (let i = 0; i < n; i++) normdx += Math.abs(dx[i]);

    /* Pomaisiu paieska. „Goodness" pagal Rowan disertacija: ieskoma STAIGAUS
       kritimo vidutiniame |dx| - ten ir dedama riba tarp grupiu. */
    let i = 0, ns = NSMIN;
    for (i = 0; i + NSMIN < n; i += ns) {
      let nsGoodness = -Infinity, norm = normi;
      const nk = i + NSMAX > n ? n : i + NSMAX;
      for (let k = i; k < i + NSMIN - 1; k++) norm += Math.abs(dx[p[k]]);
      ns = NSMIN;
      for (let k = i + NSMIN - 1; k < nk; k++) {
        norm += Math.abs(dx[p[k]]);
        if (n - (k + 1) < NSMIN) continue;      // likusi dalis turi buti dalijama
        const goodness = (k + 1 < n)
          ? norm / (k + 1) - (normdx - norm) / (n - (k + 1))
          : normdx / n;
        if (goodness > nsGoodness) { nsGoodness = goodness; ns = (k + 1) - i; }
      }
      for (let k = i; k < i + ns; k++) normi += Math.abs(dx[p[k]]);

      const r = runSubspace(n, f, x, p, i, ns, lb, ub, xstep, minf, stop);
      minf = r.minf;
      if (r.fdiff > fdiffMax) fdiffMax = r.fdiff;
      if (r.ret === NLOPT.FAILURE) return { ret: NLOPT.XTOL_REACHED, x, minf };
      if (r.ret !== NLOPT.XTOL_REACHED) return { ret: r.ret, x, minf };
      nsubs++;
    }

    /* Paskutinis pomaisis - visa, kas liko. */
    ns = n - i;
    const r = runSubspace(n, f, x, p, i, ns, lb, ub, xstep, minf, stop);
    minf = r.minf;
    if (r.fdiff > fdiffMax) fdiffMax = r.fdiff;
    nsubs++;
    if (r.ret === NLOPT.FAILURE) return { ret: NLOPT.XTOL_REACHED, x, minf };
    if (r.ret !== NLOPT.XTOL_REACHED) return { ret: r.ret, x, minf };

    /* Baigimo testai */
    const fa = minf, fb = minf + fdiffMax;
    const d = Math.abs(fb - fa);
    if (d <= stop.ftolAbs || d <= stop.ftolRel * (Math.abs(fa) + Math.abs(fb)) / 2)
      return { ret: NLOPT.FTOL_REACHED, x, minf };

    let xok = true;
    for (let j = 0; j < n; j++) {
      const dd = Math.abs(x[j] - xprev[j]);
      if (dd > stop.xtolAbs && dd > stop.xtolRel * Math.abs(x[j])) { xok = false; break; }
    }
    if (xok) {
      /* Rowan pastaba: tikrinti reikia ir ZINGSNI, ne tik |x - xprev| - jei
         zingsnis dar didelis, vidinis Nelder-Mead galejo tiesiog nespeti. */
      let j = 0;
      for (; j < n; j++)
        if (Math.abs(xstep[j]) * PSI > stop.xtolAbs &&
            Math.abs(xstep[j]) * PSI > stop.xtolRel * Math.abs(x[j])) break;
      if (j === n) return { ret: NLOPT.XTOL_REACHED, x, minf };
    }

    for (let k = 0; k < n; k++) dx[k] = x[k] - xprev[k];

    /* Zingsniu perskaiciavimas */
    let scale;
    if (nsubs === 1) scale = PSI;
    else {
      let stepnorm = 0, dxnorm = 0;
      for (let k = 0; k < n; k++) { stepnorm += Math.abs(xstep[k]); dxnorm += Math.abs(dx[k]); }
      scale = stepnorm ? dxnorm / stepnorm : PSI;
      if (scale < OMEGA) scale = OMEGA;
      if (scale > 1 / OMEGA) scale = 1 / OMEGA;
    }
    for (let k = 0; k < n; k++) {
      xstep[k] = dx[k] === 0 ? -(xstep[k] * scale)
                             : Math.sign(dx[k]) * Math.abs(xstep[k] * scale);
    }
  }
}

/** `subspace_func` + vienas `nldrmd_minimize_` kvietimas (sbplx.c:53-64, 155). */
function runSubspace(n, f, x, p, is, ns, lb, ub, xstep, minf, stop) {
  const xs = new Array(ns), xsstep = new Array(ns), lbs = new Array(ns), ubs = new Array(ns);
  for (let k = 0; k < ns; k++) {
    xs[k] = x[p[is + k]];
    xsstep[k] = xstep[p[is + k]];
    lbs[k] = lb[p[is + k]];
    ubs[k] = ub[p[is + k]];
  }
  /* Pomaisio funkcija: keiciam TIK sios grupes kintamuosius, likusieji lieka
     tokie, kokie yra dabartiniame `x`. */
  const fsub = xsub => {
    for (let k = 0; k < ns; k++) x[p[is + k]] = xsub[k];
    return f(x);
  };

  const r = nldrmdMinimize(ns, fsub, lbs, ubs, xs, minf, xsstep, stop, PSI);
  for (let k = 0; k < ns; k++) x[p[is + k]] = r.x[k];
  return { ret: r.ret, minf: r.minf, fdiff: r.fdiff };
}

export { stopCriteria, NLOPT };
