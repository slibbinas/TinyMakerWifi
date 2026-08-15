# -*- coding: utf-8 -*-
"""Ar atramos remiasi i platformos krasta?

V pastebejimas (2026-08-15): PrusaSlicer, kai modelis platformai per didelis,
sonuose kabancias atramas tiesiog NUKERTA ties krastu. Musu sliceris atramas
generuoja pats, tad jas sudeda taip, kad tilptu. Jei taip - Prusos „sutaupyta"
derva toje vietoje nera algoritmo nuopelnas, ir palyginimas ten neteisingas.

Matuojam tris dalykus kiekvienam failui:
  * kiek sluoksniu paliecia paveikslelio krasta (t. y. platformos riba);
  * kiek pikseliu ant krasto is viso;
  * detales (STL) plotis ir gylis, palyginti su platforma.

    python krastas.py
"""
import io
import zipfile

import numpy as np
from PIL import Image

LAB = 'C:/PIO-build/slicer-lab'
PX = 0.1275
MODELIAI = {
    'biustas': ('woman-prusa.sl1', 'one-biowoman.zip'),
    'evil': ('evil-prusa.sl1', 'one-evil.zip'),
    'kronsteinas': ('bracket-up-prusa.sl1', 'one-kronsteinas.zip'),
    'puodelis': ('cup-prusa.sl1', 'one-puodelis.zip'),
}


def tirti(path):
    zf = zipfile.ZipFile(LAB + '/' + path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    krastas_sl, krastas_px, plotis, gylis = 0, 0, 0, 0
    for n in names:
        a = np.asarray(Image.open(io.BytesIO(zf.read(n))).convert('L')) > 127
        if not a.any():
            continue
        e = int(a[0].sum() + a[-1].sum() + a[:, 0].sum() + a[:, -1].sum())
        if e:
            krastas_sl += 1
            krastas_px += e
        ys, xs = np.where(a)
        plotis = max(plotis, xs.max() - xs.min() + 1)
        gylis = max(gylis, ys.max() - ys.min() + 1)
    return len(names), krastas_sl, krastas_px, plotis * PX, gylis * PX


print('%-12s %-6s %7s %7s %8s %8s %8s' %
      ('modelis', 'kieno', 'sluoks', 'krast.', 'kr.taSku', 'plotis', 'gylis'))
for vardas, (ref, ours) in MODELIAI.items():
    for kas, p in (('prusa', ref), ('musu', ours)):
        n, ks, kp, w, h = tirti(p)
        print('%-12s %-6s %7d %7d %8d %7.1f%s %7.1f%s' %
              (vardas, kas, n, ks, kp, w, '!' if w > 40.0 else ' ',
               h, '!' if h > 30.0 else ' '))
