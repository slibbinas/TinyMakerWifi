# -*- coding: utf-8 -*-
"""Kaklelio santykis: kiek algoritmas ji numusa.

Santykis (`fizika.py` 5 kriterijus) = sluoksnio plotas / persidengimas su
ankstesniu. Skaiciuojam DU kartus:
  * TIK MODELIS (is STL pjuviu) - kiek butu be jokiu atramu;
  * SPAUDINYS (is musu .zip) - kiek liko su atramomis.
Skirtumas ir yra atramu nauda, isreiksta skaiciumi.

    python kaklelis.py [modelis.stl] [spaudinys.zip]
"""
import io
import sys
import zipfile

import numpy as np
from PIL import Image

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

import fizika  # noqa: E402

STL = sys.argv[1] if len(sys.argv) > 1 else 'kaklelis-testas.stl'
ZIP = sys.argv[2] if len(sys.argv) > 2 else 'one-kaklelis-testas.zip'


def santykis(kaukes):
    """Blogiausias plotas/persidengimas per visus sluoksnius."""
    blog, kur = 0.0, 0.0
    prev = None
    for z, m in kaukes:
        if prev is not None and m.any():
            att = float((m & prev).sum())
            if att > 0:
                s = m.sum() / att
                if s > blog:
                    blog, kur = s, z
        prev = m
    return blog, kur


mesh = slicing.load(STL)
b = mesh.bounds
modelis = []
z = b[0][2] + fizika.LAYER / 2
while z < b[1][2]:
    sec = slicing.section(mesh, z)
    m = raster.rasterize(sec) > 127 if sec else np.zeros((240, 320), bool)
    modelis.append((z, m))
    z += fizika.LAYER

zf = zipfile.ZipFile(ZIP)
names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
spaudinys = [(i * fizika.LAYER,
              np.asarray(Image.open(io.BytesIO(zf.read(n))).convert('L')) > 127)
             for i, n in enumerate(names)]

a, za = santykis(modelis)
c, zc = santykis(spaudinys)
print('%s' % STL)
print('  tik modelis (be atramu): %6.1f kartu  ties z = %.2f mm' % (a, za))
print('  spaudinys su atramomis:  %6.1f kartu  ties z = %.2f mm' % (c, zc))
if a > 0:
    print('  atramos numuse:          %6.1f %%' % ((1 - c / a) * 100))
