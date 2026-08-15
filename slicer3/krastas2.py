# -*- coding: utf-8 -*-
"""Kas remiasi i platformos krasta - DETALE ar ATRAMOS?

Pirmas matavimas (`krastas.py`) parode, kad spaudinys krasta lieCia. Bet is jo
nematyti, ar nukirsta pati detale, ar tik jos atramos. Cia atskiriam: modelio
pjuvis imamas is STL (su ta pacia lygiuote kaip visur), o atramos = spaudinys
minus modelis.

    python krastas2.py biustas evil
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
ZINGSNIS = 10
MODELIAI = {
    'biustas': ('woman-prusa.sl1', 'one-biowoman.zip', 0.3, 'woman-placed.stl', True),
    'evil': ('evil-prusa.sl1', 'one-evil.zip', 0.3, 'evil-placed.stl', True),
    'kronsteinas': ('bracket-up-prusa.sl1', 'one-kronsteinas.zip', 0.3, 'bracket2.stl', False),
    'puodelis': ('cup-prusa.sl1', 'one-puodelis.zip', 0.3, 'cup.stl', False),
}


def kr(a):
    return int(a[0].sum() + a[-1].sum() + a[:, 0].sum() + a[:, -1].sum())


def tirti(path, first, stl, ori):
    zf = zipfile.ZipFile(LAB + '/' + path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    mesh = slicing.place(slicing.load(stl)) if ori else slicing.load(stl)
    dx, dy, flip, _ = fizika.find_shift(stl, path, first, ori)
    det_sl = det_px = atr_sl = atr_px = 0
    for k in range(0, len(names), ZINGSNIS):
        img = np.asarray(Image.open(io.BytesIO(zf.read(names[k]))).convert('L')) > 127
        if not img.any():
            continue
        z = (first + k * 0.05 if first > 0.05 else (k + 1) * 0.05) - 0.025
        part = raster.rasterize(slicing.section(mesh, z)) > 127
        if flip:
            part = np.fliplr(part)
        part = np.roll(np.roll(part, dy, 0), dx, 1)
        det = img & ndimage.binary_dilation(part, iterations=2)
        atr = img & ~ndimage.binary_dilation(part, iterations=2)
        a, b = kr(det), kr(atr)
        if a:
            det_sl += 1
            det_px += a
        if b:
            atr_sl += 1
            atr_px += b
    return len(names) // ZINGSNIS, det_sl, det_px, atr_sl, atr_px


print('%-12s %-6s %6s %8s %8s %8s %8s' %
      ('modelis', 'kieno', 'tirta', 'det.sl', 'det.px', 'atr.sl', 'atr.px'))
for v in (sys.argv[1:] or list(MODELIAI)):
    ref, ours, first, stl, ori = MODELIAI[v]
    for kas, p, f in (('prusa', ref, first), ('musu', ours, 0.05)):
        n, ds, dp, asl, ap = tirti(p, f, LAB + '/' + stl, ori)
        print('%-12s %-6s %6d %8d %8d %8d %8d' % (v, kas, n, ds, dp, asl, ap))
