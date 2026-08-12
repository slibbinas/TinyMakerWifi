# -*- coding: utf-8 -*-
"""Suskaičiuoja ATRAMŲ TAŠKUS iš gatavų sluoksnių — ir mūsų, ir PrusaSlicer'io.

Atramos galvutės smaigalys yra ta vieta, kur supporto dėmė baigiasi: sluoksnyje
ji dar yra, o aukščiau jos nebėra. Tad einam sluoksniais iš apačios į viršų,
kiekvienoje randam supportų dėmes (sluoksnis MINUS detalė) ir pažymim tas,
kurios aukščiau nebeturi tęsinio. Kiekviena tokia dėmė = vienas atramos taškas.

Taip gaunam jo taškų sąrašą nesikapstant po 3MF ir nespėliojant.

    python tips.py <sluoksniai.zip|sl1> <placed.stl> [pirmo_sluoksnio_storis]
"""
import io
import sys
import zipfile

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

src, stl = sys.argv[1], sys.argv[2]
first = float(sys.argv[3]) if len(sys.argv) > 3 else 0.05

z = zipfile.ZipFile(src)
names = sorted(n for n in z.namelist() if n.lower().endswith('.png'))
mesh = slicing.place(slicing.load(stl))
print(f'{src}: {len(names)} sluoksniu · pirmas {first} mm')


def layer_z(k):
    """Sluoksnio vidurio aukstis. Prusa pirma sluoksni daro storesni."""
    return first / 2 if k == 0 else first + (k - 0.5) * 0.05


def masks(k):
    a = np.asarray(Image.open(io.BytesIO(z.read(names[k]))).convert('L')) > 127
    part = raster.rasterize(slicing.section(mesh, layer_z(k))) > 127
    # detales krastas gali buti pilkas - praplecia, kad jo nelaikytume supportu
    part = ndimage.binary_dilation(part, iterations=2)
    return a & ~part


prev_sup = None
tips = []
for k in range(len(names) - 1, -1, -1):          # is virsaus zemyn
    sup = masks(k)
    lab, n = ndimage.label(sup)
    if n:
        above = prev_sup if prev_sup is not None else np.zeros_like(sup)
        # dėmė turi tęsinį aukščiau, jei bent vienas jos pikselis ten irgi yra
        has_above = ndimage.sum(above, lab, index=np.arange(1, n + 1)) > 0
        sizes = ndimage.sum(sup, lab, index=np.arange(1, n + 1))
        cent = ndimage.center_of_mass(sup, lab, np.arange(1, n + 1))
        for i in range(n):
            if has_above[i] or sizes[i] < 2:
                continue
            tips.append((layer_z(k), cent[i][1], cent[i][0], sizes[i]))
    prev_sup = ndimage.binary_dilation(sup, iterations=1)
    if k % 200 == 0:
        print(f'  {k}/{len(names)}', end='\r', flush=True)

print(f'\natramos tasku (galvuciu smaigaliu): {len(tips)}')
hist = {}
for zz, _, _, _ in tips:
    hist[int(zz // 10) * 10] = hist.get(int(zz // 10) * 10, 0) + 1
print('pagal auksti (10 mm juostos):', dict(sorted(hist.items())))
areas = np.array([t[3] for t in tips])
print('demes dydis px: mediana %.0f · p90 %.0f' % (np.median(areas), np.percentile(areas, 90)))
