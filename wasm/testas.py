"""Regresijos testas: ar WASM sliceris vis dar duoda tai, ka desktop PrusaSlicer.

Kodel reikia: slicerio viduje keiciasi daug (koordinates, pirmas sluoksnis,
kompensacijos, rasterizacija), ir kiekviena karta tikrinti ranka - brangu bei
nepatikima. Sis testas pjausto tuos pacius modelius, gamina .sl1 ir lygina ji su
PrusaSlicer etalonu SLUOKSNIS PO SLUOKSNIO.

Matas - kiek pikseliu apsvieciama. Tai tas pats, ka mato derva: jei du sliceriai
duoda ta pati spaudini, kreives sutampa.

    python wasm/testas.py                    # visi modeliai
    python wasm/testas.py --modelis cup      # vienas
    python wasm/testas.py --atnaujink        # perkurti PrusaSlicer etalonus

Grazina 0, jei viskas ribose, ir 1, jei ne - tinka CI.
"""
import argparse
import io
import json
import os
import struct
import subprocess
import sys
import zipfile
import zlib

LAB = os.environ.get('SLICER_LAB', 'C:/PIO-build/slicer-lab')
BUILD = os.environ.get('WASM_BUILD', 'C:/PIO-build/wasm-verify')
PRUSA = os.environ.get('PRUSA_CLI',
                       'C:/Program Files/Prusa3D/PrusaSlicer/prusa-slicer-console.exe')

# Ribos. Ne is dangaus: siandien (2026-08-19) blogiausias bendras nuokrypis buvo
# 0,94 %, blogiausias atskiras sluoksnis - 4,0 %. Ribos padetos su atsarga, kad
# gaudytu REGRESIJA, o ne triuksma.
RIBA_BENDRA = 3.0        # %
RIBA_SLUOKSNIO = 8.0     # %
RIBA_SLUOKSNIU_SK = 3    # kiek sluoksniu leidziama skirtis

# Blogiausio sluoksnio ieskom TIK ten, kur yra ka lyginti. Pacioje virsuneje
# sluoksnis buna keliu pikseliu, ir vieno pikselio eilute duoda +600 % - tai
# matavimo triuksmas, ne regresija (pagavo pats testas, pirma karta paleistas).
MAZAS_SLUOKSNIS = 200    # px

MODELIAI = [
    # (vardas, STL, etalonas)
    ('puodelis',    'cup.stl',           'ref/cup.sl1'),
    ('kronsteinas', 'bracket-placed.stl', 'ref/bracket-placed.sl1'),
    ('evil',        'evil-placed.stl',   'ref/evil-placed.sl1'),
    ('biustas',     'woman-placed.stl',  'ref/woman-placed.sl1'),
]


def png_pikseliai(z, vardas):
    """Kiek nenuliniu pikseliu PNG'e (be isorines bibliotekos)."""
    d = z.read(vardas)
    pos, w, h, idat = 8, 0, 0, b''
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        if typ == b'IHDR':
            w, h = struct.unpack('>II', d[pos + 8:pos + 16])
        elif typ == b'IDAT':
            idat += d[pos + 8:pos + 8 + ln]
        pos += 12 + ln
    raw = zlib.decompress(idat)
    stride = len(raw) // h - 1
    n, prev = 0, bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(1, stride):
                line[i] = (line[i] + line[i - 1]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - 1] if i else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - 1] if i else 0
                c = prev[i - 1] if i else 0
                p = a + prev[i] - c
                pa, pb, pc = abs(p - a), abs(p - prev[i]), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (prev[i] if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        n += sum(1 for v in line if v)
        prev = line
    return n


def sluoksniai(kelias):
    z = zipfile.ZipFile(kelias)
    return z, sorted(n for n in z.namelist() if n.lower().endswith('.png'))


def palygink(etalonas, musu):
    za, pa = sluoksniai(etalonas)
    zb, pb = sluoksniai(musu)
    n = min(len(pa), len(pb))
    # Lyginam nuo VIRSAUS: apacioje skiriasi rafto pradzia, o virsus abiejuose
    # yra tas pats modelio galas.
    suma_a = suma_b = 0
    blogiausias = (0, 0.0)
    for i in range(n):
        na = png_pikseliai(za, pa[len(pa) - 1 - i])
        nb = png_pikseliai(zb, pb[len(pb) - 1 - i])
        suma_a += na
        suma_b += nb
        if na >= MAZAS_SLUOKSNIS:
            sk = (nb - na) / na * 100
            if abs(sk) > abs(blogiausias[1]):
                blogiausias = (i, sk)
    bendra = (suma_b - suma_a) / suma_a * 100 if suma_a else 0.0
    return {
        'sluoksniu_etalono': len(pa), 'sluoksniu_musu': len(pb),
        'bendra_proc': bendra, 'blogiausias_proc': blogiausias[1],
        'blogiausias_nuo_virsaus': blogiausias[0],
    }


def daryk_etalona(stl, isvestis):
    ini = os.path.join(LAB, 'prusa-full.ini')
    subprocess.check_call([PRUSA, '--export-sla', '--load', ini,
                           '--output', isvestis, os.path.join(LAB, stl)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modelis', help='tik sitas (pvz. puodelis)')
    ap.add_argument('--atnaujink', action='store_true', help='perkurti etalonus')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()

    sla = os.path.join(BUILD, 'sla.js')
    if not os.path.isfile(sla):
        sys.exit('nerandu %s - pirma `bash wasm/build.sh link`' % sla)

    rezultatai, blogai = [], 0
    for vardas, stl, etalonas in MODELIAI:
        if a.modelis and a.modelis != vardas:
            continue
        etalonas_p = os.path.join(LAB, etalonas)
        if a.atnaujink or not os.path.isfile(etalonas_p):
            print('  %s: kuriamas etalonas...' % vardas)
            daryk_etalona(stl, etalonas_p)

        musu = os.path.join(BUILD, 'test-%s.sl1' % vardas)
        subprocess.check_call(['node', sla, os.path.join(LAB, stl), '0.05',
                               'regular', musu],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        r = palygink(etalonas_p, musu)
        r['modelis'] = vardas
        sk_sluoksniu = abs(r['sluoksniu_musu'] - r['sluoksniu_etalono'])
        r['ok'] = (abs(r['bendra_proc']) <= RIBA_BENDRA
                   and abs(r['blogiausias_proc']) <= RIBA_SLUOKSNIO
                   and sk_sluoksniu <= RIBA_SLUOKSNIU_SK)
        if not r['ok']:
            blogai += 1
        rezultatai.append(r)

    if a.json:
        print(json.dumps(rezultatai, ensure_ascii=False, indent=1))
    else:
        print('\n%-12s %-16s %-12s %-14s' % ('modelis', 'sluoksniu', 'bendra', 'blogiausias'))
        print('-' * 58)
        for r in rezultatai:
            print('%-12s %5d / %-8d %+7.2f %%    %+7.2f %%   %s'
                  % (r['modelis'], r['sluoksniu_musu'], r['sluoksniu_etalono'],
                     r['bendra_proc'], r['blogiausias_proc'],
                     'gerai' if r['ok'] else 'BLOGAI'))
        print('\nribos: bendra +-%.1f %% · sluoksnio +-%.1f %% · sluoksniu skaicius +-%d'
              % (RIBA_BENDRA, RIBA_SLUOKSNIO, RIBA_SLUOKSNIU_SK))
        print('verdiktas: %s' % ('VISKAS RIBOSE' if not blogai else '%d MODELIS(-IAI) UZ RIBU' % blogai))

    return 1 if blogai else 0


if __name__ == '__main__':
    sys.exit(main())
