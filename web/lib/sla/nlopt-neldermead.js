/*
 * sVi Slicer - optimizatorius, portuotas is NLopt.
 * Copyright (C) 2007-2020 Massachusetts Institute of Technology (Steven G. Johnson)
 * Copyright (C) 2026 Viktoras Sidlauskas - JavaScript port
 *
 * NLopt platinamas pagal MIT licencija; sis portas platinamas kartu su
 * `web/lib/sla/` katalogu pagal AGPL-3.0-or-later (MIT su ja suderinama).
 *
 * Portuota is: src/algs/neldermead/nldrmd.c
 *
 * Kodel reikia: PrusaSlicer atramu medis tris vietas sprendzia optimizatoriumi
 * (galvutes kryptis, kelias i plokste, inkaras). Musu pakaitalas ejo asimis, o
 * Subplex deformuoja simpleksa - auditorius (050_Gem_Cld) patvirtino, kad
 * siaurose vietose, kur sprendinys „diagonaliai" tarp pavirsiu, tai duoda
 * skirtinga rezultata.
 */

/* nldrmd.c:35 */
const ALPHA = 1, BETA = 0.5, GAMMA = 2, DELTA = 0.5;

export const NLOPT = {
  SUCCESS: 1, MINF_MAX_REACHED: 2, FTOL_REACHED: 3, XTOL_REACHED: 4,
  MAXEVAL_REACHED: 5, FAILURE: -1,
};

/** `close` (nldrmd.c:47) - lygu su plaukiojancio kablelio tikslumu. */
const artimi = (a, b) => Math.abs(a - b) <= 1e-13 * (Math.abs(a) + Math.abs(b));

/**
 * `reflectpt` (nldrmd.c:63-77): xnew = c + scale*(c - xold), prispaustas prie
 * ribu. Grazina false, jei naujas taskas sutapo su `c` arba `xold` - tada
 * simpleksas issigimė ir paieska baigiama.
 *
 * ⚠️ Prispaudimas prie ribu („pinning", Richardson & Kuester 1973) gali
 * suplokstinti simpleksa i zemesnio matmens hiperplokstuma. Originalo
 * komentaras tai ivardija ir sako, kad gelbsti restartas - butent ji daro
 * subplex.
 */
function reflectpt(n, xnew, c, scale, xold, lb, ub) {
  let equalc = true, equalold = true;
  for (let i = 0; i < n; i++) {
    let newx = c[i] + scale * (c[i] - xold[i]);
    if (newx < lb[i]) newx = lb[i];
    if (newx > ub[i]) newx = ub[i];
    equalc = equalc && artimi(newx, c[i]);
    equalold = equalold && artimi(newx, xold[i]);
    xnew[i] = newx;
  }
  return !(equalc || equalold);
}

/**
 * `nldrmd_minimize_` (nldrmd.c:106-287).
 *
 * MINIMIZUOJA. `psi > 0` pakeicia iprastus tolerancijos testus salyga, kad
 * simplekso skersmuo turi sumazeti `psi` kartu - to reikia, kai ji kviecia
 * subplex.
 *
 * Originale simpleksas laikomas raudonai-juodame medyje; cia (n = 3) uztenka
 * masyvo su rusiavimu - tvarka ta pati, tik struktura pigesnė.
 *
 * @param stop {maxeval, minfMax, ftolRel, ftolAbs, xtolRel, xtolAbs, nevals}
 * @returns {ret, x, minf, fdiff}
 */
export function nldrmdMinimize(n, f, lb, ub, x0, minf0, xstep, stop, psi = 0) {
  const x = x0.slice();
  let minf = minf0;
  let fdiff = Infinity;
  let ret = NLOPT.SUCCESS;
  let initDiam = 0;

  /* pts[i] = { f, x[] } - simplekso virsune su funkcijos reiksme. */
  const pts = new Array(n + 1);
  const c = new Array(n).fill(0);
  const xcur = new Array(n).fill(0);

  const CHECK_EVAL = (xc, fc) => {
    stop.nevals++;
    if (fc <= minf) {
      minf = fc;
      for (let k = 0; k < n; k++) x[k] = xc[k];
      if (minf < stop.minfMax) return NLOPT.MINF_MAX_REACHED;
    }
    if (stop.maxeval > 0 && stop.nevals >= stop.maxeval) return NLOPT.MAXEVAL_REACHED;
    return 0;
  };

  pts[0] = { f: minf0, x: x0.slice() };
  if (minf0 < stop.minfMax) return { ret: NLOPT.MINF_MAX_REACHED, x, minf, fdiff };

  /* Simplekso statymas is `xstep`, su ribu tvarkymu (nldrmd.c:135-165). */
  for (let i = 0; i < n; i++) {
    const pt = x0.slice();
    pt[i] += xstep[i];
    if (pt[i] > ub[i]) {
      if (ub[i] - x0[i] > Math.abs(xstep[i]) * 0.1) pt[i] = ub[i];
      else pt[i] = x0[i] - Math.abs(xstep[i]);       // riba per arti - i kita puse
    }
    if (pt[i] < lb[i]) {
      if (x0[i] - lb[i] > Math.abs(xstep[i]) * 0.1) pt[i] = lb[i];
      else {
        pt[i] = x0[i] + Math.abs(xstep[i]);
        if (pt[i] > ub[i])                            // einam i tolimesne riba
          pt[i] = 0.5 * ((ub[i] - x0[i] > x0[i] - lb[i] ? ub[i] : lb[i]) + x0[i]);
      }
    }
    if (artimi(pt[i], x0[i])) return { ret: NLOPT.FAILURE, x, minf, fdiff };
    const fv = f(pt);
    pts[i + 1] = { f: fv, x: pt };
    const r = CHECK_EVAL(pt, fv);
    if (r) return { ret: r, x, minf, fdiff };
  }

  for (;;) {                                          // `restart:`
    for (;;) {
      pts.sort((a, b) => a.f - b.f);
      const low = pts[0], high = pts[n];
      const fl = low.f, xl = low.x;
      let fh = high.f; const xh = high.x;
      fdiff = fh - fl;

      if (initDiam === 0)
        for (let i = 0; i < n; i++) initDiam += Math.abs(xl[i] - xh[i]);

      if (psi <= 0) {
        /* `nlopt_stop_ftol` */
        const d = Math.abs(fh - fl);
        if (d <= stop.ftolAbs || d <= stop.ftolRel * (Math.abs(fh) + Math.abs(fl)) / 2)
          return { ret: NLOPT.FTOL_REACHED, x, minf, fdiff };
      }

      /* Centroidas be BLOGIAUSIO tasko. */
      c.fill(0);
      for (let i = 0; i <= n; i++) {
        if (pts[i] === high) continue;
        for (let j = 0; j < n; j++) c[j] += pts[i].x[j];
      }
      for (let i = 0; i < n; i++) c[i] /= n;

      xcur.fill(0);
      for (let i = 0; i <= n; i++)
        for (let j = 0; j < n; j++) {
          const dx = Math.abs(pts[i].x[j] - c[j]);
          if (dx > xcur[j]) xcur[j] = dx;
        }
      for (let i = 0; i < n; i++) xcur[i] += c[i];

      if (psi > 0) {
        let diam = 0;
        for (let i = 0; i < n; i++) diam += Math.abs(xl[i] - xh[i]);
        if (diam < psi * initDiam) return { ret: NLOPT.XTOL_REACHED, x, minf, fdiff };
      } else {
        let ok = true;
        for (let i = 0; i < n; i++) {
          const dx = Math.abs(xcur[i] - c[i]);
          if (dx > stop.xtolAbs && dx > stop.xtolRel * Math.abs(c[i])) { ok = false; break; }
        }
        if (ok) return { ret: NLOPT.XTOL_REACHED, x, minf, fdiff };
      }

      /* Atspindys */
      if (!reflectpt(n, xcur, c, ALPHA, xh, lb, ub))
        return { ret: NLOPT.XTOL_REACHED, x, minf, fdiff };
      const fr = f(xcur);
      let r = CHECK_EVAL(xcur, fr);
      if (r) return { ret: r, x, minf, fdiff };

      if (fr < fl) {
        /* Naujas geriausias - PLECIAM simpleksa. */
        if (!reflectpt(n, xh, c, GAMMA, xh, lb, ub))
          return { ret: NLOPT.XTOL_REACHED, x, minf, fdiff };
        fh = f(xh);
        r = CHECK_EVAL(xh, fh);
        if (r) return { ret: r, x, minf, fdiff };
        if (fh >= fr) { fh = fr; for (let k = 0; k < n; k++) xh[k] = xcur[k]; }
      } else if (fr < pts[n - 1].f) {
        /* Geriau uz antra blogiausia - priimam. */
        for (let k = 0; k < n; k++) xh[k] = xcur[k];
        fh = fr;
      } else {
        /* Naujas blogiausias - TRAUKIAM. */
        if (!reflectpt(n, xcur, c, fh <= fr ? -BETA : BETA, xh, lb, ub))
          return { ret: NLOPT.XTOL_REACHED, x, minf, fdiff };
        const fc = f(xcur);
        r = CHECK_EVAL(xcur, fc);
        if (r) return { ret: r, x, minf, fdiff };

        if (fc < fr && fc < fh) {
          for (let k = 0; k < n; k++) xh[k] = xcur[k];
          fh = fc;
        } else {
          /* Traukimas nepavyko - SUSPAUDZIAM visa simpleksa link geriausio. */
          for (let i = 0; i <= n; i++) {
            const pt = pts[i];
            if (pt === low) continue;
            if (!reflectpt(n, pt.x, xl, -DELTA, pt.x, lb, ub))
              return { ret: NLOPT.XTOL_REACHED, x, minf, fdiff };
            pt.f = f(pt.x);
            r = CHECK_EVAL(pt.x, pt.f);
            if (r) return { ret: r, x, minf, fdiff };
          }
          break;                                      // `goto restart`
        }
      }
      high.f = fh;
    }
  }
}

/** Numatytieji stabdymo kriterijai. */
export const stopCriteria = (over = {}) => ({
  maxeval: 0, minfMax: -Infinity,
  ftolRel: 0, ftolAbs: 0, xtolRel: 0, xtolAbs: 0,
  nevals: 0, ...over,
});
