# -*- coding: utf-8 -*-
"""Kokio storio PADAS (raftas) is tikruju?

Gemini teigia, kad 0,3 mm padas per plonas mentelei, ir reikia 1,0-1,5 mm.
Musu sprendimas remesi tuo, kad taikom „Prusos" storį. Tad pirma pamatuojam,
kiek sluoksniu Prusos pade is TIKRUJU - is jo paties failo, ne is nuostatu.

Pado pozymis: apacioje plotas didelis ir beveik nekintantis, o pasibaigus padui
plotas staiga krenta (lieka tik stulpeliai).

    python padas.py
"""
import io
import zipfile

import numpy as np
from PIL import Image

LAB = 'C:/PIO-build/slicer-lab'
PX2 = 0.1275 ** 2
FAILAI = [('biustas', 'woman-prusa.sl1'), ('biustas', 'one-biowoman.zip'),
          ('evil', 'evil-prusa.sl1'), ('evil', 'one-evil.zip'),
          ('kronsteinas', 'bracket-up-prusa.sl1'), ('kronsteinas', 'one-kronsteinas.zip'),
          ('puodelis', 'cup-prusa.sl1'), ('puodelis', 'one-puodelis.zip')]


def plotai(path, n=40):
    zf = zipfile.ZipFile(LAB + '/' + path)
    names = sorted(x for x in zf.namelist() if x.lower().endswith('.png'))
    out = []
    for nm in names[:n]:
        a = np.asarray(Image.open(io.BytesIO(zf.read(nm))).convert('L')) > 127
        out.append(int(a.sum()))
    return out


for vardas, f in FAILAI:
    p = plotai(f)
    # pado pabaiga: pirmas sluoksnis, kurio plotas < puse pirmojo
    riba = p[0] * 0.5
    k = next((i for i, v in enumerate(p) if v < riba), len(p))
    print('%-12s %-22s padas %2d sluoksniu = %.2f mm  (plotas %.1f mm2 -> %.1f)'
          % (vardas, f, k, k * 0.05, p[0] * PX2, p[min(k, len(p) - 1)] * PX2))
    print('             pirmi plotai (mm2): %s'
          % ' '.join('%.0f' % (v * PX2) for v in p[:14]))
