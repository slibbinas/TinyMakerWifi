# -*- coding: utf-8 -*-
"""Pasuktu variantu MATAVIMAS is pjuviu (antra pakopa).

Tos pacios taisykles kaip `fizika.py`, tik viskas skaiciuojama is paties
spaudinio, be etalono:
  * SALOS - sluoksnio deme, po kuria nieko nera, ne mazesne uz kontakta ir
    toliau nei kontakto skersmuo nuo bet ko, kas apacioje jau sukietinta;
  * DANGA - kaip toli naujas plotas nuo to, kas ji laiko;
  * DERVA - viso spaudinio turis (modelis + atramos).

    python pasukimai2.py
"""
import io
import os
import re
import zipfile

import numpy as np
from PIL import Image
from scipy import ndimage

import fizika

LAB = 'C:/PIO-build/slicer-lab'


def matuok(path):
    zf = zipfile.ZipFile(path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    prev = None
    salos = 0
    danga = 0.0
    danga_z = 0.0
    turis = 0.0
    for i, n in enumerate(names):
        img = np.asarray(Image.open(io.BytesIO(zf.read(n))).convert('L')) > 127
        z = (i + 1) * fizika.LAYER
        turis += img.sum() * fizika.PX * fizika.PX * fizika.LAYER
        if prev is not None and img.any() and prev.any():
            lab, k = ndimage.label(img)
            if k:
                laiko = set(np.unique(lab[prev]))
                sz = ndimage.sum(img, lab, np.arange(1, k + 1))
                cand = [j for j in range(1, k + 1)
                        if j not in laiko and sz[j - 1] >= fizika.HEAD_PX]
                if cand:
                    dd = ndimage.distance_transform_edt(~prev) * fizika.PX
                    for j in cand:
                        if float(dd[lab == j].min()) > 2 * fizika.HEAD_R_MM:
                            salos += 1
            over = img & ~prev
            if z >= fizika.BASE_MM and over.sum() > fizika.HEAD_PX:
                lo, no = ndimage.label(over)
                if no:
                    sz = ndimage.sum(over, lo, np.arange(1, no + 1))
                    keep = np.isin(lo, np.nonzero(sz >= fizika.HEAD_PX)[0] + 1)
                    if keep.any():
                        d = ndimage.distance_transform_edt(~prev) * fizika.PX
                        w = float(d[keep].max())
                        if w > danga:
                            danga, danga_z = w, z
        prev = img
    return salos, danga, danga_z, turis, len(names)


failai = sorted(f for f in os.listdir(LAB) if re.match(r'^pas_.*_\d+\.zip$', f))
print('%-22s %6s %7s %9s %10s %8s' %
      ('variantas', 'salu', 'danga', 'ties z mm', 'derva mm3', 'sluoks'))
blogu = 0
for f in failai:
    s, d, dz, t, n = matuok(os.path.join(LAB, f))
    zyme = ''
    if s:
        zyme += ' SALOS'
        blogu += 1
    if d > 3.0:
        zyme += ' DANGA>3'
        blogu += 1
    print('%-22s %6d %7.2f %9.2f %10.0f %8d%s'
          % (f.replace('pas_', '').replace('.zip', ''), s, d, dz, t, n, zyme))
print('\nblogu: %d' % blogu)
