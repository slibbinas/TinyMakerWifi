# -*- coding: utf-8 -*-
"""Ar PrusaSlicer deda kugines PEDAS po stulpais, kai jau yra padas?

Musu geometrija sako, kad puodelyje 14 pedu suvalgo 47,6 mm3 - penktadali visos
atramu dervos. Pedos aukstis 1 mm, spindulys 1,5 mm.

Matome tai is pjuviu: ATRAMU plotas pirmuose sluoksniuose virs pado. Jei pedos
yra, plotas laipsniskai mazeja per pirma milimetra; jei ne - iskart nukrenta iki
stulpu skerspjuvio.

    python pedos.py [modelis]
"""
import io
import sys
import zipfile

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

import fizika  # noqa: E402

LAB = 'C:/PIO-build/slicer-lab'
MODELIAI = {
    'puodelis': ('cup-prusa.sl1', 'one-puodelis.zip', 0.3, 'cup.stl', False),
    'kronsteinas': ('bracket-up-prusa.sl1', 'one-kronsteinas.zip', 0.3, 'bracket2.stl', False),
}
VARDAS = sys.argv[1] if len(sys.argv) > 1 else 'puodelis'
ref, ours, first, stl, ori = MODELIAI[VARDAS]


def profilis(path, f, n=10**9):
    """Atramu plotas mm2 pirmuose n sluoksniu (atmetus detale)."""
    zf = zipfile.ZipFile(LAB + '/' + path)
    names = sorted(x for x in zf.namelist() if x.lower().endswith('.png'))
    mesh = slicing.load(LAB + '/' + stl)
    dx, dy, flip, _ = fizika.find_shift(LAB + '/' + stl, path, f, ori)
    out = []
    for k in range(min(n, len(names))):
        img = np.asarray(Image.open(io.BytesIO(zf.read(names[k]))).convert('L')) > 127
        z = (f + k * 0.05 if f > 0.05 else (k + 1) * 0.05) - 0.025
        part = raster.rasterize(slicing.section(mesh, z)) > 127
        if flip:
            part = np.fliplr(part)
        part = np.roll(np.roll(part, dy, 0), dx, 1)
        sup = img & ~ndimage.binary_dilation(part, iterations=1)
        out.append(sup.sum() * fizika.PX * fizika.PX)
    return out


a = profilis(ref, first)
b = profilis(ours, 0.05)
# Sumuojam juostomis po 1 mm: turis = plotas * 0,05 mm
JUOSTA = 20
print('%s: atramu TURIS (mm3) juostomis po 1 mm' % VARDAS)
print('%-12s %12s %12s %10s' % ('aukstis mm', 'PrusaSlicer', 'musu', 'skirtumas'))
vp = vm = 0.0
for j in range(0, max(len(a), len(b)), JUOSTA):
    pa = sum(a[j:j + JUOSTA]) * 0.05
    pm = sum(b[j:j + JUOSTA]) * 0.05
    vp += pa; vm += pm
    if pa > 0.5 or pm > 0.5:
        print('%-12s %12.1f %12.1f %+10.1f'
              % ('%.1f-%.1f' % (j * 0.05, (j + JUOSTA) * 0.05), pa, pm, pm - pa))
print('%-12s %12.1f %12.1f %+10.1f' % ('VISO', vp, vm, vm - vp))
