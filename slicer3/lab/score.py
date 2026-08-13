# -*- coding: utf-8 -*-
"""Vienas skaicius, kiek esam arti PrusaSlicer'io — ir istorija, kad matytusi
   ar artejam.

Kodel to reikejo: kiekviena pataisa vienam modeliui padeda, kitam kenkia, ir be
bendro mato sprendziama „atrodo geriau". Cia matuojama VISKAS vienu paleidimu ir
irasoma i istorija.

Matas:
  kiekvienam modeliui ir aukščiui  err = |musu - etalono| / max(etalono, 1)
  modelio balas = 100 * (1 - vidutinis err), apacia nukertama ties 0
  bendras balas = modeliu balu vidurkis
Plius GEOMETRIJA: stulpu ore ir kertanciu detale — abu privalo buti 0, kitaip
balas neturi prasmes.

    python score.py "zyme" ["pastaba"]
"""
import io
import json
import os
import subprocess
import sys
import zipfile

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

LAB = 'C:/PIO-build/slicer-lab'
MOD = 'C:/PIO-build/exp2-wt/web/lib/slicer2.js'
HIST = os.path.join(LAB, 'log', 'history.json')

# modelis: (stl, etalonas, pirmo sluoksnio storis, auksciai, ar orientuoti)
MODELS = {
    'biowoman':  ('woman-placed.stl', 'woman-prusa.sl1',   0.3, [3, 10, 15, 20, 30, 40], True),
    'evil':      ('evil-placed.stl',  'evil-prusa.sl1',    0.3, [3, 10, 15, 20, 30],     True),
    'kronsteinas': ('bracket2.stl',   'bracket-up-prusa.sl1', 0.3, [2, 5, 8, 11],        False),
    'puodelis':  ('cup.stl',          'cup-prusa.sl1',     0.3, [2, 5, 8],               False),
}


def slice_ours(stl, out, oriented):
    tool = 'ourslices.mjs' if oriented else 'rawslices.mjs'
    env = dict(os.environ, SLICER=MOD)
    subprocess.run(['node', tool, stl, out], cwd=LAB, env=env,
                   capture_output=True, check=True)


def blobs(stl, path, first, z, oriented):
    mesh = slicing.place(slicing.load(os.path.join(LAB, stl))) if oriented \
        else slicing.load(os.path.join(LAB, stl))
    zf = zipfile.ZipFile(os.path.join(LAB, path))
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    k = int(round((z - first) / 0.05 + 0.5)) if first > 0.05 else int(round(z / 0.05 - 0.5))
    k = min(max(k, 0), len(names) - 1)
    a = np.asarray(Image.open(io.BytesIO(zf.read(names[k]))).convert('L')) > 127
    part = raster.rasterize(slicing.section(mesh, z)) > 127
    best = None
    for pm in (part, np.fliplr(part)):
        sup = a & ~ndimage.binary_dilation(pm, iterations=2)
        lab, n = ndimage.label(sup)
        sz = ndimage.sum(sup, lab, np.arange(1, n + 1)) if n else np.empty(0)
        v = int((sz >= 2).sum())
        if best is None or v < best:
            best = v
    return best


def geometry(stl, oriented):
    """verify.mjs — ar niekas nekabo ore ir nekerta detales."""
    out = subprocess.run(['node', 'verify.mjs', stl, MOD], cwd=LAB,
                         capture_output=True, text=True).stdout
    air = cross = None
    for line in out.splitlines():
        if 'apacia ore' in line:
            air = int(line.split(':')[1].split('/')[0])
        if 'kerta detale' in line:
            cross = sum(int(p.split('/')[0].split()[-1]) for p in line.split('·'))
    return air, cross


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'be-zymes'
    note = sys.argv[2] if len(sys.argv) > 2 else ''
    head = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'],
                          cwd='C:/PIO-build/exp2-wt', capture_output=True,
                          text=True).stdout.strip()

    entry = {'tag': tag, 'note': note, 'commit': head, 'models': {}}
    scores = []
    for name, (stl, ref, first, zs, oriented) in MODELS.items():
        ours = f'score-{name}.zip'
        slice_ours(stl, ours, oriented)
        ref_v = [blobs(stl, ref, first, z, oriented) for z in zs]
        our_v = [blobs(stl, ours, 0.05, z, oriented) for z in zs]
        errs = [abs(o - r) / max(r, 1) for o, r in zip(our_v, ref_v)]
        score = max(0.0, 100.0 * (1 - float(np.mean(errs))))
        scores.append(score)
        entry['models'][name] = {'z': zs, 'ref': ref_v, 'ours': our_v,
                                 'score': round(score, 1)}
        print('%-12s etalonas %s' % (name, ref_v))
        print('%-12s mūsų     %s · balas %.1f' % ('', our_v, score))

    # Renderiai — kad log'e matytusi ne tik skaiciai
    imgdir = os.path.join(LAB, 'log', 'img', tag)
    os.makedirs(imgdir, exist_ok=True)
    for name, (stl, ref, first, zs, oriented) in MODELS.items():
        try:
            subprocess.run(['node', 'isostack.mjs', f'score-{name}.zip',
                            f'log/img/{tag}/{name}-ours.png', '35', stl],
                           cwd=LAB, capture_output=True, timeout=600)
            subprocess.run(['node', 'isostack.mjs', ref,
                            f'log/img/{tag}/{name}-ref.png', '35', stl],
                           cwd=LAB, capture_output=True, timeout=600)
            subprocess.run(['node', 'montage.mjs',
                            f'log/img/{tag}/{name}-ref.png',
                            f'log/img/{tag}/{name}-ours.png',
                            f'log/img/{tag}/{name}.png',
                            'PRUSASLICER', 'MUSU'],
                           cwd=LAB, capture_output=True, timeout=600)
            for suf in ('-ours.png', '-ref.png'):
                f = os.path.join(imgdir, name + suf)
                if os.path.exists(f):
                    os.remove(f)
        except Exception as e:
            print('renderis nepavyko:', name, e)

    air, cross = geometry('woman-placed.stl', True)
    entry['geometry'] = {'air': air, 'crossing': cross}
    entry['score'] = round(float(np.mean(scores)), 1)
    print('\nBENDRAS BALAS: %.1f · stulpų ore %s · kerta detalę %s'
          % (entry['score'], air, cross))

    os.makedirs(os.path.dirname(HIST), exist_ok=True)
    hist = json.load(io.open(HIST, encoding='utf-8')) if os.path.exists(HIST) else []
    hist.append(entry)
    io.open(HIST, 'w', encoding='utf-8').write(
        json.dumps(hist, ensure_ascii=False, indent=1))
    print('istorijoje irasu:', len(hist))


if __name__ == '__main__':
    main()
