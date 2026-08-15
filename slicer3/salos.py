# -*- coding: utf-8 -*-
"""Neparemtos SALOS -> taskai pultui (raudoni rutuliukai).

Ta pati taisykle kaip `fizika.py` 3-iame kriterijuje: sio sluoksnio deme, po
kuria nera nieko, didesne uz kontakta ir toliau nei kontakto skersmuo nuo bet
ko, kas apacioje jau sukietinta (arciau ji kietedama tiesiog prilimpa).

Skirtumas nuo `fizika.py`: cia mums reikia ne KIEK, o KUR - tad rasom kiekvienos
salos svorio centra milimetrais.

    python salos.py            -> C:/PIO-build/3d/<modelis>_<kas>_salos.json
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
MODELIAI = {
    'biustas': ('woman-prusa.sl1', 'one-biowoman.zip', 0.3, 'woman-placed.stl', True),
    'evil': ('evil-prusa.sl1', 'one-evil.zip', 0.3, 'evil-placed.stl', True),
    'kronsteinas': ('bracket-up-prusa.sl1', 'one-kronsteinas.zip', 0.3, 'bracket2.stl', False),
    'puodelis': ('cup-prusa.sl1', 'one-puodelis.zip', 0.3, 'cup.stl', False),
    'ertme-testas': ('nera.sl1', 'one-ertme-testas.zip', 0.05, 'ertme-testas.stl', False),
    'kaklelis-testas': ('nera.sl1', 'one-kaklelis-testas.zip', 0.05, 'kaklelis-testas.stl', False),
}


def salos(path, first):
    prev = None
    rasta = []
    for z, img in fizika.layers_of(path, first):
        if prev is not None and img.any() and prev.any():
            lab, n = ndimage.label(img)
            if n:
                laiko = set(np.unique(lab[prev]))
                sz = ndimage.sum(img, lab, np.arange(1, n + 1))
                cand = [k for k in range(1, n + 1)
                        if k not in laiko and sz[k - 1] >= fizika.HEAD_PX]
                if cand:
                    dd = ndimage.distance_transform_edt(~prev) * fizika.PX
                    for k in cand:
                        if float(dd[lab == k].min()) <= 2 * fizika.HEAD_R_MM:
                            continue
                        ys, xs = np.where(lab == k)
                        rasta.append({'x': round(float(xs.mean()) * fizika.PX, 2),
                                      'y': round(float(ys.mean()) * fizika.PX, 2),
                                      'z': round(float(z), 2),
                                      'dydis': int(sz[k - 1])})
        prev = img
    return rasta


os.makedirs(OUT, exist_ok=True)
for vardas, (ref, ours, first, stl, ori) in MODELIAI.items():
    for kas, path, f in (('prusa', ref, first), ('musu', ours, 0.05)):
        r = salos(os.path.join(LAB, path), f)
        p = os.path.join(OUT, '%s_%s_salos.json' % (vardas, kas))
        io.open(p, 'w', encoding='utf-8').write(json.dumps(r))
        print('%-12s %-6s salu %d%s' % (vardas, kas, len(r),
              ('  z: ' + ', '.join('%.1f' % s['z'] for s in r[:6])) if r else ''))
