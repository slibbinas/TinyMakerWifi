# -*- coding: utf-8 -*-
"""Sulipdo web/dashboard.html is jo paties ir web/parts/* gabalu.

Kodel to reikia: prie pulto dirba DVI sesijos - viena prie printerio, kita prie
slicerio, - ir abi rase i ta pati `dashboard.html`. Nuo 08-17 slicerio dalys guli
atskiruose failuose po `web/parts/`, o pulte ju vietoje stovi zyme:

    <!--#include parts/slicer-card.html-->     (HTML kontekste)
    /*#include parts/slicer.css*/              (CSS/JS kontekste)

Zyme pakeiciama failo turiniu PAZODZIUI - jokiu tarpu, jokiu naujos eilutes
taisykliu. Todel sulipdytas rezultatas yra baitas i baita toks pat, koks buvo
vientisas failas, ir tai galima IRODYTI (zr. --check).

Naršyklė mato viena faila kaip ir anksciau; skirtumas tik tas, kad jis
sulipdomas build metu. Flash'ui kaina nuline.

Naudojimas:
    python scripts/assemble_dashboard.py            # i stdout
    python scripts/assemble_dashboard.py -o out.html
    python scripts/assemble_dashboard.py --check ref.html   # ar sutampa baitais

Is kodo:
    from assemble_dashboard import assemble
    html = assemble(project_dir)
"""
import io
import os
import re
import sys

MARKER = re.compile(r'(?:<!--|/\*)#include\s+([A-Za-z0-9_./-]+)\s*(?:-->|\*/)')


def _read(path):
    # newline='': jokio \n <-> \r\n vertimo, kad baitai liktu tokie, kokie yra
    return io.open(path, encoding='utf-8', newline='').read()


def assemble(proj):
    """Grazina pilna dashboard.html teksta su iterptais gabalais."""
    web = os.path.join(proj, 'web')
    src = _read(os.path.join(web, 'dashboard.html'))
    missing = []

    def sub(m):
        rel = m.group(1)
        path = os.path.join(web, rel.replace('/', os.sep))
        if not os.path.exists(path):
            missing.append(rel)
            return m.group(0)
        return _read(path)

    out = MARKER.sub(sub, src)
    if missing:
        raise SystemExit('[assemble_dashboard] truksta gabalu: ' + ', '.join(missing))
    # Igneztas #include igneztame gabale - nepalaikoma tycia: viena lygis, kad
    # butu akivaizdu, kas is kur ateina.
    left = MARKER.search(out)
    if left:
        raise SystemExit('[assemble_dashboard] gabale liko #include: ' + left.group(0))
    return out


if __name__ == '__main__':
    proj = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    html = assemble(proj)
    args = sys.argv[1:]
    if args and args[0] == '--check':
        ref = _read(args[1])
        if ref == html:
            print('[assemble_dashboard] SUTAMPA baitas i baita (%d simboliu)' % len(html))
        else:
            print('[assemble_dashboard] NESUTAMPA: %d vs %d simboliu' % (len(ref), len(html)))
            for i in range(min(len(ref), len(html))):
                if ref[i] != html[i]:
                    print('  pirmas skirtumas ties %d:' % i)
                    print('   buvo: ' + repr(ref[i:i + 80]))
                    print('   dabar: ' + repr(html[i:i + 80]))
                    break
            sys.exit(1)
    elif args and args[0] == '-o':
        io.open(args[1], 'w', encoding='utf-8', newline='').write(html)
        print('[assemble_dashboard] %s (%d simboliu)' % (args[1], len(html)))
    else:
        sys.stdout.write(html)
