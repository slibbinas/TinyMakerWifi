# -*- coding: utf-8 -*-
"""Supportu demiu skaicius duotuose auksciuose - VIENODU matu visiems failams.

Kodel atskirai nuo measure.mjs: du matuokliai davė 2x skirtingus skaicius tam
paciam failui (Prusa z=20: 47 pries 24). Skirtumas - ar detales kauke pries
atimant praplecama (cia praplecama 2 px, kad antialiasingo krastas nebutu
palaikytas supportu). Absoliutus skaicius nera tiesa; tiesa yra palyginimas,
kai visiems taikomas TAS PATS matas.

Veidrodis nustatomas automatiskai: mus JS sliceris display_mirror_x netaiko,
Prusa ir slicer3 taiko, tad be sito palyginimas butu siuksle.

    python blobs.py <placed.stl> <failas:pirmo_sluoksnio_storis> ...
"""
import io
import sys
import zipfile

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

import os
ZS = [float(v) for v in os.environ.get("ZS","3,10,15,20,30,40").split(",")]
mesh = slicing.place(slicing.load(sys.argv[1]))


def index(z, first):
    return int(round((z - first) / 0.05 + 0.5)) if first > 0.05 else int(round(z / 0.05 - 0.5))


def run(path, first):
    zf = zipfile.ZipFile(path)
    names = [n for n in sorted(zf.namelist()) if n.lower().endswith('.png')]
    img = lambda k: np.asarray(Image.open(io.BytesIO(zf.read(names[k]))).convert('L')) > 127
    direct = flip = 0
    for z in (10, 20, 30):
        a, p = img(index(z, first)), raster.rasterize(slicing.section(mesh, z)) > 127
        direct += (a & p).sum()
        flip += (a & np.fliplr(p)).sum()
    mir = flip > direct
    out = []
    for z in ZS:
        a = img(index(z, first))
        part = raster.rasterize(slicing.section(mesh, z)) > 127
        if mir:
            part = np.fliplr(part)
        sup = a & ~ndimage.binary_dilation(part, iterations=2)
        lab, n = ndimage.label(sup)
        sizes = ndimage.sum(sup, lab, np.arange(1, n + 1)) if n else np.empty(0)
        out.append(int((sizes >= 2).sum()))
    return mir, out


print('z mm:', ZS)
for arg in sys.argv[2:]:
    path, first = arg.rsplit(':', 1)
    mir, vals = run(path, float(first))
    print('%-22s veidrodis=%-5s %s' % (path, mir, vals))
