"""Palygina du .sl1 archyvus sluoksnis po sluoksnio.

Matas - kiek pikseliu apsvieciama (baltu/pilku) kiekviename sluoksnyje. Tai tas
pats, ka mato derva: jei du sliceriai duoda ta pati spaudini, sitos kreives
turi sutapti.

    python palygink_sl1.py a.sl1 b.sl1
"""
import struct
import sys
import zipfile
import zlib


def pikseliai(zf, vardas):
    """Grazina (nenuliniu pikseliu skaicius, ju suma) is PNG."""
    d = zf.read(vardas)
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
    n = suma = 0
    prev = bytearray(stride)
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
        for v in line:
            if v:
                n += 1
                suma += v
        prev = line
    return n, suma


def skaityk(kelias):
    z = zipfile.ZipFile(kelias)
    pngs = sorted(n for n in z.namelist() if n.lower().endswith('.png'))
    return z, pngs


def main():
    ka, kb = sys.argv[1], sys.argv[2]
    za, pa = skaityk(ka)
    zb, pb = skaityk(kb)
    print('%-28s %4d sluoksniu' % (ka.split('/')[-1], len(pa)))
    print('%-28s %4d sluoksniu' % (kb.split('/')[-1], len(pb)))

    # Lyginam nuo VIRSAUS: apacioje skiriasi rafto pradzia, o virsus - tas pats
    # modelio galas abiejuose.
    n = min(len(pa), len(pb))
    print('\n  nuo virsaus   %-22s %-22s  skirtumas' % (ka.split('/')[-1], kb.split('/')[-1]))
    zingsnis = max(1, n // 12)
    bendra_a = bendra_b = 0
    blogiausia = (0, 0.0)
    for i in range(0, n, zingsnis):
        na, sa = pikseliai(za, pa[len(pa) - 1 - i])
        nb, sb = pikseliai(zb, pb[len(pb) - 1 - i])
        sk = (nb - na) / na * 100 if na else 0.0
        if abs(sk) > abs(blogiausia[1]):
            blogiausia = (i, sk)
        print('  -%-12d %-22d %-22d  %+.1f %%' % (i, na, nb, sk))
    for i in range(n):
        na, _ = pikseliai(za, pa[len(pa) - 1 - i])
        nb, _ = pikseliai(zb, pb[len(pb) - 1 - i])
        bendra_a += na
        bendra_b += nb
    print('\n  VISU sluoksniu suma: %d vs %d  ->  %+.2f %%'
          % (bendra_a, bendra_b, (bendra_b - bendra_a) / bendra_a * 100))
    print('  blogiausias sluoksnis: -%d, %+.1f %%' % blogiausia)


if __name__ == '__main__':
    main()
