# -*- coding: utf-8 -*-
"""Fizikos pultas — matuoja NE panasuma i PrusaSlicer, o sutartus kriterijus.

Kriteriju sarasas suderintas su V (2026-08-13):

  1 grupe (privaloma, riba 0 arba tiksli):
    1 niekas nekabo ore .................. tikrina verify.mjs (musu medyje)
    2 atramos nekerta detales ............ verify.mjs
    3 kiekviena sala paremta ............. cia: „salu be atramos"
    4 danga .............................. cia: blogiausias atstumas <= 3 mm
  2 grupe (kad issilaikytu ir grazIai atsiluptu):
    5 laikymo atsarga .................... cia: plotas / persidengimas su apacia
    6 kontakto skersmuo .................. konfige (0,4 mm)
    7 tarpas iki detales ................. konfige (1,0 mm)
    8 stulpu stotingumas ................. medyje (vienisas <= 15 mm)
    9 sukibimas su platforma ............. cia: atramu plotas ant ploksces
  3 grupe (minimizuojam):
    10 dervos atramoms ................... cia: mm3 ir % nuo modelio
    11 zymiu kiekis ...................... cia: kontaktu skaicius

Matuojama vienodai musu ZIP ir PrusaSlicer .sl1 failams — modelio pjuvis abiem
imamas is to paties STL, tad skirtumas rodo algoritma, ne matavima.

    python fizika.py [modelis ...]
"""
import io
import sys
import zipfile

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, 'C:/PIO-build/s3-wt/slicer3')
from sla3 import raster, slicing  # noqa: E402

import score  # noqa: E402

PX = 40.8 / 320.0          # mm/px — patikrinta: puodelio siena 8.99 vs 9.00
LAYER = 0.05
GAP_LIMIT = 3.0            # kriterijus 4
BASE_MM = 1.2              # pagrindo/pado zona — ten platejimas ne nuokaba
HEAD_R_MM = 0.2            # kontakto spindulys (⌀0,4)
HEAD_PX = int(round(np.pi * (HEAD_R_MM / PX) ** 2))   # jo plotas pikseliais


def layers_of(path, first):
    zf = zipfile.ZipFile(path)
    names = sorted(n for n in zf.namelist() if n.lower().endswith('.png'))
    for i, n in enumerate(names):
        z = first + i * LAYER if first > 0.05 else (i + 1) * LAYER
        yield z, np.asarray(Image.open(io.BytesIO(zf.read(n))).convert('L')) > 127


def find_shift(stl, path, first, oriented):
    """Kiek failo sluoksniai pastumti musu modelio kaukes atzvilgiu.

    Kronsteino atveju PrusaSlicer sluoksniai dengia modelio kauke tik 48%
    (musu - 96%): jis detale centruoja ant plokstes, o mes pjaustom STL kaip yra.
    Be sios pataisos PUSE modelio sieneliu patenka i „atramu" plota ir dervos
    skaicius isputa dvigubai (rasta 08-15, V/Gemini audito metu).
    """
    mesh = slicing.place(slicing.load(stl)) if oriented else slicing.load(stl)
    # Sluoksnis paieskai imamas is VIDURIO, ne pirmas: apacioje yra tik padas,
    # ir pagal ji lygiuojant biusto skaiciai isejo nesamone - 4255 mm3 ir 1867
    # zymes (08-15). Imam kelis ir renkames geriausia sutapima.
    layers = [(z, img) for z, img in layers_of(path, first) if img.sum() > 2000]
    if not layers:
        return (0, 0, False, 1.0)
    best = (0, 0, False, -1.0)
    for z, img in [layers[len(layers) // 2], layers[len(layers) // 3]]:
        part = raster.rasterize(slicing.section(mesh, z)) > 127
        if part.sum() < 500:
            continue
        for flip in (False, True):
            pm = np.fliplr(part) if flip else part
            for dy in range(-30, 31, 2):
                for dx in range(-30, 31, 2):
                    ov = (img & np.roll(np.roll(pm, dy, 0), dx, 1)).sum() / pm.sum()
                    if ov > best[3]:
                        best = (dx, dy, flip, ov)
    # Tikslinam po 1 px: kronsteino sieneles vos 12 px storio, tad 2 px paklaida
    # kainuoja ~15% sutapimo ir isputa „atramu" plota (08-15).
    bx, by, bf, bo = best
    part = raster.rasterize(slicing.section(mesh, layers[len(layers) // 2][0])) > 127
    img = layers[len(layers) // 2][1]
    pm = np.fliplr(part) if bf else part
    for dy in range(by - 3, by + 4):
        for dx in range(bx - 3, bx + 4):
            ov = (img & np.roll(np.roll(pm, dy, 0), dx, 1)).sum() / pm.sum()
            if ov > bo:
                bx, by, bo = dx, dy, ov
    return (bx, by, bf, bo)


def suction(stl, oriented, step=4, down=2):
    """13 kriterijus: uzdaros ertmes („siurbtukai"). V sprendimas 08-15.

    Uzdara ertme keliant plokste sukuria vakuuma - derva i ja neistrykSta taip
    greitai, kaip pleciasi turis, ir modelis gali buti isplestas is atramu.

    Tikrinama ERDVEJE, ne sluoksnyje. Pirmas bandymas buvo 2D (tustuma, kurios
    kontura uzdaro medziaga tame paciame sluoksnyje) ir jis melavo: biuste rado
    416 mm2 „ertme", nors modelis neturi NE VIENOS vidines skyles - tai buvo
    tarpas tarp plauku sruogu ir veido, 2D atrodantis uzdaras, o erdveje atviras
    (08-15).

    Dabar: statomas surezintas tinklelis (kas `step` sluoksniu, `down` kartu
    smulkesnis), tustuma jungiama 3D rysiu, ir tos jos dalys, kurios NEPASIEKIA
    turio krasto, yra uzdaros. Grazina ju turi mm3 ir skaiciu.
    """
    mesh = slicing.place(slicing.load(stl)) if oriented else slicing.load(stl)
    b = mesh.bounds
    zs = np.arange(b[0][2] + LAYER / 2, b[1][2], LAYER * step)
    stack = []
    for z in zs:
        sec = slicing.section(mesh, z)
        m = raster.rasterize(sec) > 127 if sec else None
        if m is None:
            m = np.zeros((240, 320), bool)
        stack.append(m[::down, ::down])
    if not stack:
        return 0.0, 0
    vol = np.stack(stack)                      # [z, y, x]
    void = ~vol
    lab, n = ndimage.label(void)               # 6-rysys pakanka: tarpas turi
    if not n:                                  # buti tikrai uzdaras
        return 0.0, 0
    free = set(np.unique(np.concatenate([
        lab[0].ravel(), lab[-1].ravel(),
        lab[:, 0].ravel(), lab[:, -1].ravel(),
        lab[:, :, 0].ravel(), lab[:, :, -1].ravel()])))
    cell = (PX * down) ** 2 * (LAYER * step)
    worst, count = 0.0, 0
    for k in range(1, n + 1):
        if k in free:
            continue
        v = (lab == k).sum() * cell
        if v < 1.0:                            # < 1 mm3 - dulke
            continue
        count += 1
        worst = max(worst, v)
    return worst, count


def measure(stl, path, first, oriented, shift=None):
    mesh = slicing.place(slicing.load(stl)) if oriented else slicing.load(stl)
    r = dict(vol=0.0, model=0.0, plate=0.0, contacts=0, islands=0,
             gap=0.0, gap_z=0.0, load=0.0, load_z=0.0, layers=0)
    prev_sup = prev_part = prev_img = None
    for i, (z, img) in enumerate(layers_of(path, first)):
        part = raster.rasterize(slicing.section(mesh, z)) > 127
        if shift is not None:
            dx0, dy0, fl0 = shift
            if fl0:
                part = np.fliplr(part)
            part = np.roll(np.roll(part, dy0, 0), dx0, 1)
        if shift is None and part.sum() and img.sum() and \
           (img & part).sum() < (img & np.fliplr(part)).sum():
            part = np.fliplr(part)
        sup = img & ~ndimage.binary_dilation(part, iterations=2)
        a = PX * PX
        r['vol'] += sup.sum() * a * LAYER
        r['model'] += part.sum() * a * LAYER
        r['layers'] += 1
        if i == 0:
            r['plate'] = sup.sum() * a

        if prev_img is not None:
            """3 ir 4 kriterijus skaiciuojam is GRYNU sluoksniu (modelis+atramos),
            nes fiziskai nauja sluoksni laiko VISKAS, kas po juo sukietinta.
            Anksciau cia buvo atimtas modelis, ir tada atrama, laikanti nuokaba,
            dingdavo is kaukes (ji juk yra po pacia detale) — pultas rode
            „11,35 mm nepadengta" ir PrusaSlicer'iui, kuris spausdinasi (08-13)."""
            # 3 — sala: sio sluoksnio deme, po kuria nera NIEKO
            lab, n = ndimage.label(img)
            if n:
                held = set(np.unique(lab[prev_img]))
                sz = ndimage.sum(img, lab, np.arange(1, n + 1))
                cand = [k for k in range(1, n + 1)
                        if k not in held and sz[k - 1] >= HEAD_PX]
                if cand:
                    # Sala skaiciuojama tik jei ji TIKRAI atskira: arciau nei
                    # kontakto skersmuo nuo apacioje sukietintos medziagos ji
                    # kietedama tiesiog prilimpa prie kaimyno. PrusaSlicer'io
                    # 6 „salos" biuste visos buvo 0,26-0,77 mm atstumu ir
                    # 10-17 px dydzio — statoko slaito krastai, ne broka (08-13).
                    dd = ndimage.distance_transform_edt(~prev_img) * PX
                    for k in cand:
                        if float(dd[lab == k].min()) > 2 * HEAD_R_MM:
                            r['islands'] += 1
            # 4 — danga: kiek toli naujas plotas nuo to, kas ji laiko.
            # Dvi isimtys, abi fizines:
            #  - PAGRINDAS (z < BASE_MM): ten padas tycia platejа i sonus, tai
            #    ne nuokaba (be sios isimties pultas rode 11,4 mm ir Prusai);
            #  - lopineliai, mazesni uz kontakta (< HEAD_PX), — ju paremti
            #    neimanoma, tai trianguliacijos dulkes.
            over = img & ~prev_img
            if z >= BASE_MM and over.sum() > HEAD_PX and prev_img.sum():
                lab_o, n_o = ndimage.label(over)
                if n_o:
                    sz = ndimage.sum(over, lab_o, np.arange(1, n_o + 1))
                    keep = np.isin(lab_o, np.nonzero(sz >= HEAD_PX)[0] + 1)
                    if keep.any():
                        d = ndimage.distance_transform_edt(~prev_img) * PX
                        worst = float(d[keep].max())
                        if worst > r['gap']:
                            r['gap'], r['gap_z'] = worst, z
            """5 — laikymo atsarga. Plevele plesia proporcingai sluoksnio
            plotui, o laiko ji tai, kuo sluoksnis prisikabines prie apacios:
            PERSIDENGIMAS su ankstesniu sluoksniu (ir modelio, ir atramu).
            Santykis plotas/persidengimas rodo, kiek karta didesnis plesiamas
            plotas uz ji laikanti kaklelis.

            Pirmiau cia buvo plotas / STULPU SKAICIUS, ir tai rode nesamone:
            blogiausia vieta iseidavo modelio virsuje, kur likes vienas stulpas,
            nors ten sluoksni laiko pats kunas po juo (08-14)."""
            att = float((img & prev_img).sum())
            if img.sum() and att > 0:
                load = img.sum() / att
                if load > r['load']:
                    r['load'], r['load_z'] = load, z
            # 11 — kontaktas: atramos deme, kurios virsuje nebeliko
            lab2, n2 = ndimage.label(prev_sup) if prev_sup is not None else (None, 0)
            if n2:
                alive = set(np.unique(lab2[sup]))
                r['contacts'] += sum(1 for k in range(1, n2 + 1) if k not in alive)
        prev_sup, prev_part, prev_img = sup, part, img
    return r


def main():
    names = sys.argv[1:] or list(score.MODELS)
    hdr = ('modelis', 'kas', 'salu', 'danga', 'kaklelis', 'plokste',
           'derva', '% nuo', 'zymiu')
    print('%-11s %-11s %5s %8s %9s %8s %8s %7s %6s' % hdr)
    print('%-11s %-11s %5s %8s %9s %8s %8s %7s %6s' %
          ('', '', 'vnt', 'mm', 'kartai', 'mm2', 'mm3', 'modelio', 'vnt'))
    for name in names:
        stl, ref, first, zs, oriented = score.MODELS[name]
        # 13 — uzdaros ertmes: modelio savybe, tad matuojama karta, ne kiekvienam
        cav_v, cav_n = suction(stl, oriented)
        if cav_n:
            print('   ! %s: %d uzdaros ertmes, didziausia %.0f mm3 — vakuumo rizika'
                  % (name, cav_n, cav_v))
        ours = 'one-%s.zip' % name
        score.slice_ours(stl, ours, oriented)
        for who, p, f in (('PrusaSlicer', ref, first), ('musu', ours, 0.05)):
            dx, dy, flip, ov = find_shift(stl, p, f, oriented)
            if ov < 0.90:
                print('   ! %s: kauke sutampa tik %.0f%%' % (who, 100 * ov))
            m = measure(stl, p, f, oriented, (dx, dy, flip))
            flag = '' if m['gap'] <= GAP_LIMIT else ' !'
            print('%-11s %-11s %5d %6.2f%-2s %9.1f %8.1f %8.1f %6.0f%% %6d' %
                  (name if who != 'musu' else '', who, m['islands'], m['gap'],
                   flag, m['load'], m['plate'], m['vol'],
                   100 * m['vol'] / max(m['model'], 1e-9), m['contacts']))


if __name__ == '__main__':
    main()
