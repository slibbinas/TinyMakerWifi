# -*- coding: utf-8 -*-
"""Uzdaros ertmes („siurbtukai") -> langeliai pultui (pusiau permatoma geltona).

Ta pati taisykle kaip `fizika.suction` (13 kriterijus): statom surezinta
tinkleli, tustuma jungiam 3D rysiu, ir tos dalys, kurios NEPASIEKIA turio
krasto, yra uzdaros. Skirtumas: cia mums reikia ne KIEK, o KUR.

Ertme priklauso nuo MODELIO, ne nuo slicerio, tad failas vienas modeliui.

    python siurbtukai.py            -> C:/PIO-build/3d/<modelis>_ertmes.json
"""
import io
import json
import os
import sys

import numpy as np
from scipy import ndimage

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

import fizika  # noqa: E402

OUT = 'C:/PIO-build/3d'
LAB = 'C:/PIO-build/slicer-lab'
STEP, DOWN = 4, 2          # tie patys, kaip fizika.suction
MODELIAI = {
    'biustas': ('woman-placed.stl', True),
    'evil': ('evil-placed.stl', True),
    'kronsteinas': ('bracket2.stl', False),
    'puodelis': ('cup.stl', False),
    'ertme-testas': ('ertme-testas.stl', False),
}


def ertmes(stl, oriented):
    mesh = slicing.place(slicing.load(stl)) if oriented else slicing.load(stl)
    b = mesh.bounds
    zs = np.arange(b[0][2] + fizika.LAYER / 2, b[1][2], fizika.LAYER * STEP)
    stack = []
    for z in zs:
        sec = slicing.section(mesh, z)
        m = raster.rasterize(sec) > 127 if sec else None
        if m is None:
            m = np.zeros((240, 320), bool)
        stack.append(m[::DOWN, ::DOWN])
    if not stack:
        return [], 0.0, 0
    vol = np.stack(stack)
    lab, n = ndimage.label(~vol)
    if not n:
        return [], 0.0, 0
    free = set(np.unique(np.concatenate([
        lab[0].ravel(), lab[-1].ravel(),
        lab[:, 0].ravel(), lab[:, -1].ravel(),
        lab[:, :, 0].ravel(), lab[:, :, -1].ravel()])))
    cellA = (fizika.PX * DOWN) ** 2 * (fizika.LAYER * STEP)
    taskai, viso, kiek = [], 0.0, 0
    for k in range(1, n + 1):
        if k in free:
            continue
        kur = np.argwhere(lab == k)
        v = len(kur) * cellA
        if v < 1.0:                 # < 1 mm3 - dulke, kaip ir fizika.py
            continue
        kiek += 1
        viso += v
        for zi, yi, xi in kur:
            taskai.append([round(float((xi + 0.5) * fizika.PX * DOWN), 2),
                           round(float((yi + 0.5) * fizika.PX * DOWN), 2),
                           round(float(zs[zi]), 2)])
    return taskai, viso, kiek


os.makedirs(OUT, exist_ok=True)
for vardas, (stl, ori) in MODELIAI.items():
    t, v, n = ertmes(os.path.join(LAB, stl), ori)
    d = {'langelis': [round(fizika.PX * DOWN, 4), round(fizika.PX * DOWN, 4),
                      round(fizika.LAYER * STEP, 4)],
         'turis': round(v, 1), 'kiek': n, 'taskai': t}
    io.open(os.path.join(OUT, '%s_ertmes.json' % vardas), 'w',
            encoding='utf-8').write(json.dumps(d))
    print('%-13s ertmiu %d, turis %7.1f mm3, langeliu %5d'
          % (vardas, n, v, len(t)))
