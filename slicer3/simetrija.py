# -*- coding: utf-8 -*-
"""Simetrijos matas + „vos telpa" patikra.

V pastaba (2026-08-19): kai modelis padetas beveik per visa plokste, viena
puse atrodo kitaip nei kita - soninės atramos nupjaunamos. Iki siol tai
vertinom akimis; cia paverciam skaiciumi.

SIMETRIJA. Imam SIMETRISKA modeli (puodelis, grybas), centruota. Kiekvienam
sluoksniui lyginam ji su savo veidrodiniu atspindziu:

    asimetrija = |kauke XOR veidrodis| / |kauke|

Simetriskam modeliui su simetriskomis atramomis tai turi buti apie nuli. Jei
algoritmas puses traktuoja skirtingai (ar viena nupjauta krasto), skaicius
sokteli.

Papildomai - kiek spaudinys liecia ekrano krasta (t. y. kiek nupjauta).

    python simetrija.py [telpa_*.zip ...]
"""
import io
import os
import re
import sys
import zipfile

import numpy as np
from PIL import Image

LAB = 'C:/PIO-build/slicer-lab'
PX = 0.1275


def matuok(path):
    zf = zipfile.ZipFile(path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    blog = 0.0
    blog_z = 0.0
    suma = 0.0
    n_sl = 0
    kr_sl = kr_px = 0
    for i, n in enumerate(names):
        a = np.asarray(Image.open(io.BytesIO(zf.read(n))).convert('L')) > 127
        if not a.any():
            continue
        # veidrodis apie modelio centra: imam paties sluoksnio svorio centra,
        # kad matuotume ne padeti, o FORMOS asimetrija
        ys, xs = np.where(a)
        cx = int(round(xs.mean()))
        w = min(cx, a.shape[1] - 1 - cx)
        if w < 3:
            continue
        kair = a[:, cx - w:cx]
        desi = a[:, cx + 1:cx + 1 + w][:, ::-1]
        if kair.shape != desi.shape:
            continue
        skirt = np.logical_xor(kair, desi).sum()
        viso = a.sum()
        if viso:
            r = skirt / viso
            suma += r
            n_sl += 1
            if r > blog:
                blog, blog_z = r, (i + 1) * 0.05
        e = int(a[0].sum() + a[-1].sum() + a[:, 0].sum() + a[:, -1].sum())
        if e:
            kr_sl += 1
            kr_px += e
    vid = suma / n_sl if n_sl else 0.0
    return vid, blog, blog_z, kr_sl, kr_px


failai = sys.argv[1:] or sorted(
    f for f in os.listdir(LAB) if re.match(r'^telpa_.*\.zip$', f))
print('%-28s %10s %10s %9s %8s %9s'
      % ('variantas', 'asim.vid', 'asim.blog', 'ties z', 'kr.sl', 'kr.tasku'))
for f in failai:
    v, b, z, ks, kp = matuok(os.path.join(LAB, f))
    zyme = ' KRASTAS' if kp else ''
    print('%-28s %9.1f%% %9.1f%% %9.2f %8d %9d%s'
          % (f.replace('telpa_', '').replace('.zip', ''), v * 100, b * 100, z, ks, kp, zyme))
