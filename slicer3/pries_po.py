# -*- coding: utf-8 -*-
"""Pries / po: kiek spaudinys liecia ekrano krasta ir kiek jame medziagos.

Lyginam TUOS PACIUS modelius su senu ir nauju moduliu (`_senas_*.zip` pries,
`one-*.zip` po), plius PrusaSlicer kaip etalona.

    python pries_po.py
"""
import io
import zipfile

import numpy as np
from PIL import Image

LAB = 'C:/PIO-build/slicer-lab'
PX = 0.1275
LAYER = 0.05
EIL = [('biustas', 'woman-prusa.sl1', '_senas_biowoman.zip', 'one-biowoman.zip'),
       ('evil', 'evil-prusa.sl1', '_senas_evil.zip', 'one-evil.zip'),
       ('kronsteinas', 'bracket-up-prusa.sl1', '_senas_kronsteinas.zip', 'one-kronsteinas.zip'),
       ('puodelis', 'cup-prusa.sl1', '_senas_puodelis.zip', 'one-puodelis.zip')]


def matuok(path):
    zf = zipfile.ZipFile(LAB + '/' + path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    kr_sl = kr_px = 0
    turis = 0.0
    for n in names:
        a = np.asarray(Image.open(io.BytesIO(zf.read(n))).convert('L')) > 127
        if not a.any():
            continue
        turis += a.sum() * PX * PX * LAYER
        e = int(a[0].sum() + a[-1].sum() + a[:, 0].sum() + a[:, -1].sum())
        if e:
            kr_sl += 1
            kr_px += e
    return kr_sl, kr_px, turis


print('%-12s %-8s %7s %9s %9s' % ('modelis', 'kieno', 'kr.sl', 'kr.tasku', 'turis mm3'))
for vardas, ref, senas, naujas in EIL:
    for kas, f in (('Prusa', ref), ('pries', senas), ('PO', naujas)):
        try:
            ks, kp, v = matuok(f)
        except Exception as e:
            print('%-12s %-8s  (%s)' % (vardas, kas, e))
            continue
        print('%-12s %-8s %7d %9d %9.0f' % (vardas, kas, ks, kp, v))
    print()
