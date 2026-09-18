# -*- coding: utf-8 -*-
"""Testinio modelio pritaikymas TinyMaker'iui: trikampių mažinimas ir per smulkių dalių išmetimas.

Paleidžiama atskira Python aplinka su mesh bibliotekomis (trimesh, fast-simplification),
ne PlatformIO penv:

    C:/Users/SViktoras/Tools/meshenv/Scripts/python.exe scripts/dev/resin-lab/adapt.py IN.stl OUT.stl

Kodėl mažinti: ekrano pikselis 0,1275 mm, sluoksnis 0,05 mm. Smulkesni už ~0,02 mm
trikampiai spaudinyje nieko nekeičia, bet 15 MB failas lėtina naršyklės pjaustyklę
ir įkėlimą į printerį. Kiekviena atskira dalis mažinama atskirai ir tikrinama, kad
nuokrypis nuo originalo neviršytų --tol (numatyta 0,02 mm) ir dalis liktų uždara.

Kodėl mesti dalis: atskiras kūnas, kurio siauriausias matmuo XY plokštumoje mažesnis
už --min (numatyta 2 pikseliai = 0,255 mm), šiame ekrane neišsispausdina - jis tik
suklaidintų vertinant testą. Išmetamos tik ATSKIROS dalys; plyšiai ir skylės pagrindo
viduje lieka (jos parodo, kur ekranas nebeatskiria).
"""
import argparse, sys
import numpy as np
import trimesh
import fast_simplification
from scipy.spatial import cKDTree

PX = 0.1275


def _point_tri_dist(p, a, b, c):
    """Tikslus atstumas nuo taškų p iki trikampių (a, b, c) - vektorizuotas Ericson algoritmas."""
    ab, ac, ap = b - a, c - a, p - a
    d1 = (ab * ap).sum(1); d2 = (ac * ap).sum(1)
    bp = p - b; d3 = (ab * bp).sum(1); d4 = (ac * bp).sum(1)
    cp = p - c; d5 = (ab * cp).sum(1); d6 = (ac * cp).sum(1)
    va = d3 * d6 - d5 * d4; vb = d5 * d2 - d1 * d6; vc = d1 * d4 - d3 * d2
    den = va + vb + vc
    den[den == 0] = 1e-30
    v = vb / den; w = vc / den
    q = a + ab * v[:, None] + ac * w[:, None]
    # kraštiniai atvejai: viršūnės ir briaunos
    m = (d1 <= 0) & (d2 <= 0); q[m] = a[m]
    m = (d3 >= 0) & (d4 <= d3); q[m] = b[m]
    m = (d6 >= 0) & (d5 <= d6); q[m] = c[m]
    m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
    t = d1[m] / np.where(d1[m] - d3[m] == 0, 1e-30, d1[m] - d3[m]); q[m] = a[m] + ab[m] * t[:, None]
    m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
    t = d2[m] / np.where(d2[m] - d6[m] == 0, 1e-30, d2[m] - d6[m]); q[m] = a[m] + ac[m] * t[:, None]
    m = (va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0)
    t = (d4[m] - d3[m]) / np.where((d4[m] - d3[m]) + (d5[m] - d6[m]) == 0, 1e-30, (d4[m] - d3[m]) + (d5[m] - d6[m]))
    q[m] = b[m] + (c[m] - b[m]) * t[:, None]
    return np.linalg.norm(p - q, axis=1)


def deviation(orig, new, n=20000, k=12):
    """Kiek originalo paviršius nutolo nuo naujo: 99,5 procentilis atstumų nuo
    originalo taškų iki artimiausio NAUJO trikampio (tikras atstumas iki paviršiaus,
    kandidatai - k artimiausių trikampių pagal centrą)."""
    pts, _ = trimesh.sample.sample_surface(orig, n)
    pts = np.vstack([pts, orig.vertices[np.random.default_rng(0).choice(len(orig.vertices), min(n, len(orig.vertices)), replace=False)]])
    tri = new.triangles
    _, idx = cKDTree(tri.mean(1)).query(pts, k=min(k, len(tri)))
    best = np.full(len(pts), np.inf)
    for j in range(idx.shape[1]):
        t = tri[idx[:, j]]
        best = np.minimum(best, _point_tri_dist(pts, t[:, 0], t[:, 1], t[:, 2]))
    return float(np.percentile(best, 99.5))


def simplify_part(p, tol):
    """Didžiausias sumažinimas, kuris dar tenkina tol ir palieka dalį uždarą."""
    if len(p.faces) < 400:
        return p, 0.0
    best, best_dev = p, 0.0
    for r in (0.95, 0.9, 0.8, 0.65, 0.5, 0.3):
        v, f = fast_simplification.simplify(p.vertices.astype(np.float32), p.faces.astype(np.int32), target_reduction=r)
        q = trimesh.Trimesh(v, f, process=True)
        if not q.is_watertight or q.volume <= 0:
            continue
        dv = deviation(p, q)
        if dv <= tol and abs(q.volume - p.volume) <= max(0.02 * p.volume, 0.01):
            return q, dv
    return best, best_dev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp"); ap.add_argument("out")
    ap.add_argument("--tol", type=float, default=0.02, help="leistinas nuokrypis, mm")
    ap.add_argument("--min", type=float, default=2 * PX, help="mažiausias atskiros dalies XY plotis, mm")
    a = ap.parse_args()

    m = trimesh.load(a.inp, force="mesh", process=True)
    parts = m.split(only_watertight=False)
    kept, dropped = [], []
    for p in parts:
        ext = p.extents
        if min(ext[0], ext[1]) < a.min:
            dropped.append(p); continue
        q, dv = simplify_part(p, a.tol)
        kept.append(q)
        if len(q.faces) != len(p.faces):
            print("  dalis %5.2f x %5.2f x %5.2f mm: %7d -> %6d trikamp., nuokrypis %.3f mm"
                  % (ext[0], ext[1], ext[2], len(p.faces), len(q.faces), dv))
    for p in dropped:
        e = p.extents
        print("  IŠMESTA %5.2f x %5.2f x %5.2f mm (siauriau nei %.3f mm)" % (e[0], e[1], e[2], a.min))
    out = trimesh.util.concatenate(kept)
    out.export(a.out)
    print("%s: %d dalių, %d -> %d trikamp.; išmesta %d; gabaritai %s mm; uždaras: %s"
          % (a.out, len(kept), len(m.faces), len(out.faces), len(dropped),
             np.round(out.extents, 2).tolist(), all(k.is_watertight for k in kept)))


if __name__ == "__main__":
    sys.exit(main())
