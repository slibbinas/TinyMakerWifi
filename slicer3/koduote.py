# -*- coding: utf-8 -*-
"""Koduotes patikra: ar failas neturi sugadintos UTF-8 (mojibake) zymiu.

    python koduote.py failas [failas...]

Grazina 1, jei rado. Taip pat perspeja apie ilgus bruksnius (V taisykle).
"""
import io
import sys

BLOGI = ['Ã', 'Å', 'â', '�', '']
BLOGI_VARDAI = {'Ã': 'Ã', 'Å': 'Å', 'â': 'â€',
                '�': 'pakaitos simbolis', '': 'valdymo baitas'}


def tikrinti(path):
    rado = 0
    try:
        txt = io.open(path, encoding='utf-8').read()
    except UnicodeDecodeError as e:
        print('%s: NE UTF-8 (%s)' % (path, e))
        return 1
    for n, line in enumerate(txt.split('\n'), 1):
        for b in BLOGI:
            if b in line:
                print('%s:%d sugadinta koduote (%s): %s'
                      % (path, n, BLOGI_VARDAI[b], line.strip()[:90]))
                rado += 1
                break
    ilgi = txt.count('—')
    if ilgi:
        print('%s: ilgu bruksniu (—): %d - keisti i paprasta -' % (path, ilgi))
    return rado


if __name__ == '__main__':
    bad = sum(tikrinti(p) for p in sys.argv[1:])
    print('svaru' if not bad else 'RADO %d vietu' % bad)
    sys.exit(1 if bad else 0)
