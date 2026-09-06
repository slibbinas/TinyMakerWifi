"""Paskelbia WASM sliceri i gh-pages su PRISEGTOMIS versijomis.

Kodel prisegti: pulto taisykle nuo 2026-08-12 - prisegamas VISAS rinkinys, nes
sena narsykles kesо kopija po naujo adapterio yra tylus gedimas (V tada ieskojo
pusdieni). Todel `slicer-wasm-3.0.0.js` viduje turi rodyti i
`slicer-core-3.0.0.js`, `slicer-wasm-worker-3.0.0.js` ir `sla-web-3.0.0.js/.wasm`,
o ne i neprisegtus vardus.

    python wasm/publish.py 3.0.1 [--ghp C:/PIO-build/ghp-wt] [--build C:/PIO-build/wasm-verify]

Skriptas TIK paruosia failus gh-pages worktree'e; commit ir push - rankomis,
kad butu matyti, kas skelbiama.
"""
import argparse
import gzip
import hashlib
import io
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def skaityk(p):
    return io.open(p, encoding='utf-8').read()


def verOk(v):
    """X.Y.Z ir nieko daugiau - tokia pat riba, kokia pulte tikrina sarasa."""
    dalys = v.split('.')
    return len(dalys) == 3 and all(d.isdigit() for d in dalys)


def rasyk(p, t):
    io.open(p, 'w', encoding='utf-8', newline='\n').write(t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('versija')
    ap.add_argument('--ghp', default='C:/PIO-build/ghp-wt')
    ap.add_argument('--build', default='C:/PIO-build/wasm-verify')
    a = ap.parse_args()
    V = a.versija
    # Versija tikrinam PIRMA, dar nieko neirase: pultas versiju sarase priima
    # tik X.Y.Z, o sustojus viduryje kelyje liktu pusinis rinkinys su vardais,
    # kuriu niekas nebenaudos (ismatuota 09-06: krito po gz ir manifesto).
    if not verOk(V):
        sys.exit('versija %r nera X.Y.Z - pultas tokios versijos sarase nepriimtu' % V)
    lib = os.path.join(a.ghp, 'lib')
    if not os.path.isdir(lib):
        sys.exit('nerandu %s - ar tikrai gh-pages worktree?' % lib)

    # --- variklis (dvejetainis) ------------------------------------------
    for galas in ('js', 'wasm'):
        src = os.path.join(a.build, 'sla-web.' + galas)
        if not os.path.isfile(src):
            sys.exit('nerandu %s - pirma `bash wasm/build.sh link`' % src)
        shutil.copyfile(src, os.path.join(lib, 'sla-web-%s.%s' % (V, galas)))
        shutil.copyfile(src, os.path.join(lib, 'sla-web.' + galas))

    # --- baze -------------------------------------------------------------
    baze = os.path.join(lib, 'slicer-core.js')
    if os.path.isfile(baze):
        shutil.copyfile(baze, os.path.join(lib, 'slicer-core-%s.js' % V))

    # --- adapteris: prisegam bazę ir darbininka ---------------------------
    ad = skaityk(os.path.join(HERE, 'slicer-wasm.js'))
    rasyk(os.path.join(lib, 'slicer-wasm.js'), ad)          # neprisegtas - vietiniam darbui
    ad_v = (ad
            .replace("from './slicer-core.js'", "from './slicer-core-%s.js'" % V)
            .replace("'slicer-wasm-worker.js'", "'slicer-wasm-worker-%s.js'" % V)
            .replace("const BAZES_FAILAS = 'slicer-core.js'",
                     "const BAZES_FAILAS = 'slicer-core-%s.js'" % V)
            .replace("self.SLA_BAZE=", "self.SLA_VERSIJA=" + repr(V).replace("'", '"') + ";self.SLA_BAZE="))
    rasyk(os.path.join(lib, 'slicer-wasm-%s.js' % V), ad_v)

    # --- darbininkas: prisegam varikli -------------------------------------
    wk = skaityk(os.path.join(HERE, 'slicer-wasm-worker.js'))
    rasyk(os.path.join(lib, 'slicer-wasm-worker.js'), wk)
    wk_v = (wk
            .replace("importScripts(BAZE + 'sla-web.js');",
                     "importScripts(BAZE + 'sla-web-%s.js');" % V)
            .replace("return BAZE + p;",
                     "return BAZE + p.replace('sla-web.wasm', 'sla-web-%s.wasm');" % V))
    rasyk(os.path.join(lib, 'slicer-wasm-worker-%s.js' % V), wk_v)

    # --- patikra: ar prisegtame rinkinyje neliko neprisegtu vardu ---------
    blogi = []
    for f, tekstas in ((('slicer-wasm-%s.js' % V), ad_v),
                       (('slicer-wasm-worker-%s.js' % V), wk_v)):
        for vardas in ('slicer-core.js', 'slicer-wasm-worker.js', 'sla-web.js'):
            if ("'" + vardas) in tekstas or ('/' + vardas) in tekstas:
                blogi.append('%s -> %s' % (f, vardas))
    if blogi:
        sys.exit('LIKO NEPRISEGTU VARDU:\n  ' + '\n  '.join(blogi))

    # --- printerio rinkinys: suspausti failai + manifestas ----------------
    # Narsykle ima NESUSPAUSTUS failus is lib/, o PRINTERIS - tik gzip'intus, ir
    # tik tuos, kuriu SHA-256 sutampa su manifestu lib/slicer-<V>.sha256
    # (`Network.ino`, handleApiLibSlicerCheck). Iki 09-06 si puse buvo daroma
    # ranka ir todel pasimesdavo: leidziant 3.3.0 modulis narsykleje veike, o i
    # kortele neisidiege - pultas atsake „manifest not found". Klaida islisdavo
    # ne skelbiant, o tada, kai zmogus bando idiegti.
    RINKINYS = ['slicer-wasm-%s.js' % V,
                'slicer-core-%s.js' % V,
                'slicer-wasm-worker-%s.js' % V,
                'sla-web-%s.js' % V,
                'sla-web-%s.wasm' % V]
    eilutes = []
    for vardas in RINKINYS:
        kelias = os.path.join(lib, vardas)
        if not os.path.isfile(kelias):
            sys.exit('nerandu %s - rinkinys nepilnas, manifesto nerasau' % kelias)
        zali = io.open(kelias, 'rb').read()
        # mtime=0 BUTINAS: be jo gzip i antraste iraso laika, tad tie patys
        # ivesties baitai kiekviena karta duotu kita suma, ir manifestas nustotu
        # buti atkuriamas (to paties leidimo perleidimas atrodytu kaip kitas).
        gz = gzip.compress(zali, 9, mtime=0)
        # Isspaudziam ATGAL ir palyginam. Manifestas pasirasys butent siuos baitus,
        # o printeris tikrina suma tik uzbaigus siunta - suklydus 3,5 MB kelione
        # butu atmesta pacioje pabaigoje, jau prie gelezies.
        if gzip.decompress(gz) != zali:
            sys.exit('%s: isspaudus negaunami tie patys baitai' % vardas)
        gzvardas = vardas + '.gz'
        io.open(os.path.join(lib, gzvardas), 'wb').write(gz)
        eilutes.append('%s  %s' % (hashlib.sha256(gz).hexdigest(), gzvardas))

    # Firmware ribos - tikrinam CIA, kad nepaskelbtume rinkinio, kuri printeris
    # tyliai atmes. Reiksmes is `Network.ino`: SLICER_MAX_FILES,
    # SLICER_MANIFEST_MAX ir slicerNameOk().
    if len(eilutes) > 6:
        sys.exit('rinkinyje %d failai, o printeris priima daugiausia 6' % len(eilutes))
    for vardas in [e.split('  ')[1] for e in eilutes]:
        if not (8 <= len(vardas) <= 46):
            sys.exit('%s: vardo ilgis %d, printeris priima 8..46' % (vardas, len(vardas)))
        if not (vardas.startswith('slicer-') or vardas.startswith('sla-web-')):
            sys.exit('%s: printeris priima tik slicer-* arba sla-web-*' % vardas)
        if not vardas.endswith('.gz'):
            sys.exit('%s: printeris priima tik .gz' % vardas)
        if '..' in vardas or not all(
                c.isalnum() or c in '.-_' for c in vardas):
            sys.exit('%s: netinkami simboliai varde' % vardas)

    manifestas = '\n'.join(eilutes) + '\n'
    if len(manifestas) > 1024:
        sys.exit('manifestas %d B, o printeris priima iki 1024' % len(manifestas))
    rasyk(os.path.join(lib, 'slicer-%s.sha256' % V), manifestas)

    print('printerio rinkinys (%d failai, manifestas %d B):' % (len(eilutes), len(manifestas)))
    for e in eilutes:
        print('   ', e)
    print()

    # --- versiju sarasas: kad naujiena isvis pasimatytu Update skiltyje -----
    # Pultas ji skaito is gh-pages saknies (`dashboard.html`: GHP+'slicer-versions.txt'),
    # palieka tik eilutes pavidalo X.Y.Z ir laiko PIRMA eilute naujausia - nuo jos
    # priklauso „Install latest" ir uzrasas „A newer slicer module is available".
    # Iki 09-06 sis failas buvo pildomas ranka, tad paskelbta versija i kortele
    # isidiegdavo, o versiju sarase nepasirodydavo, kol kas nors jo neatidarydavo.
    sarasas_kelias = os.path.join(a.ghp, 'slicer-versions.txt')
    senos = []
    if os.path.isfile(sarasas_kelias):
        senos = [x.strip() for x in skaityk(sarasas_kelias).split(chr(10))]
    # Netinkamas eilutes ismetam TYLIAI: pultas jas ignoruotu bet kuriuo atveju,
    # tad laikyti jas faile reikstu tik apgauti ta, kas ji atsivers.
    visos = sorted({v for v in senos + [V] if verOk(v)},
                   key=lambda v: tuple(int(x) for x in v.split('.')),
                   reverse=True)
    rasyk(sarasas_kelias, chr(10).join(visos) + chr(10))
    print('versiju sarasas (%d, naujausia virsuje): %s' % (len(visos), ', '.join(visos[:5])))
    print()

    print('paruosta %s:' % lib)
    for f in sorted(os.listdir(lib)):
        if V in f or f.startswith(('slicer-wasm', 'sla-web')):
            print('   ', f)
    print('\nToliau rankomis:  cd %s && git add lib slicer-versions.txt && git commit && git push' % a.ghp)


if __name__ == '__main__':
    main()
