# -*- coding: utf-8 -*-
"""Testinis modelis KAKLELIO kriterijui - „grybas".

Auditoriaus (Gemini, rastas 009) pastaba: kriterijus, kurio teigiamo atvejo
nera ne viename darbiniame modelyje, yra nepatikrintas kriterijus. Siurbtukams
toki modeli jau turim (`ertme-testas`), o kakleliui - ne: keturiu darbiniu
modeliu santykiai yra 1,0-3,2, t. y. visi „geri".

Kaklelio kriterijus (`fizika.py`, 5) = sluoksnio PLOTAS / persidengimas su
ankstesniu sluoksniu. Jis rodo, kiek kartu didesnis plesiamas plotas uz ji
laikanti kakleli.

Grybas: kotelis 2 mm skersmens, virs jo kepure 12 mm skersmens. Ties kepures pradzia
    plotas/persidengimas = (pi*6^2) / (pi*1^2) = 36
BE atramu. Su GEROMIS atramomis santykis turi nukristi - jos prisideda prie
persidengimo. Butent tai ir yra matavimas: kiek algoritmas ta 36 numusa.

    python mk_kaklelis.py            -> kaklelis-testas.stl
"""
import io
import math
import struct

OUT = 'C:/PIO-build/slicer-lab/kaklelis-testas.stl'
N = 96                      # kampiniu segmentu
# Profilis (spindulys, aukstis), nuo asies apacioje ir vel i asi virsuje.
PROFILIS = [(0.0, 0.0), (1.0, 0.0), (1.0, 6.0), (6.0, 6.0), (6.0, 7.5), (0.0, 7.5)]


def suktinis(prof, n):
    """Sukinys apie Z asi -> trikampiu sarasas."""
    tri = []
    for k in range(len(prof) - 1):
        r0, z0 = prof[k]
        r1, z1 = prof[k + 1]
        for i in range(n):
            a0 = 2 * math.pi * i / n
            a1 = 2 * math.pi * (i + 1) / n
            p00 = (r0 * math.cos(a0), r0 * math.sin(a0), z0)
            p01 = (r0 * math.cos(a1), r0 * math.sin(a1), z0)
            p10 = (r1 * math.cos(a0), r1 * math.sin(a0), z1)
            p11 = (r1 * math.cos(a1), r1 * math.sin(a1), z1)
            if r0 == 0:                      # apatinis dangtelis - vienas trikampis
                tri.append((p00, p11, p10))
            elif r1 == 0:                    # virsutinis dangtelis
                tri.append((p00, p01, p10))
            else:
                tri.append((p00, p01, p11))
                tri.append((p00, p11, p10))
    return tri


def normale(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    ux, uy, uz = bx - ax, by - ay, bz - az
    vx, vy, vz = cx - ax, cy - ay, cz - az
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    d = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return nx / d, ny / d, nz / d


tri = suktinis(PROFILIS, N)
buf = bytearray(b'\0' * 80)
buf += struct.pack('<I', len(tri))
for t in tri:
    buf += struct.pack('<3f', *normale(t))
    for p in t:
        buf += struct.pack('<3f', *p)
    buf += struct.pack('<H', 0)
io.open(OUT, 'wb').write(bytes(buf))

kepure = math.pi * 6 ** 2
kotelis = math.pi * 1 ** 2
print('irasyta %s: %d trikampiu' % (OUT, len(tri)))
print('kotelis 2 mm skersmens (%.2f mm2), kepure 12 mm skersmens (%.1f mm2), aukstis 7,5 mm'
      % (kotelis, kepure))
print('laukiamas kaklelio santykis BE atramu: %.1f' % (kepure / kotelis))
