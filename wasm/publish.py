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
import io
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def skaityk(p):
    return io.open(p, encoding='utf-8').read()


def rasyk(p, t):
    io.open(p, 'w', encoding='utf-8', newline='\n').write(t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('versija')
    ap.add_argument('--ghp', default='C:/PIO-build/ghp-wt')
    ap.add_argument('--build', default='C:/PIO-build/wasm-verify')
    a = ap.parse_args()
    V = a.versija
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

    print('paruosta %s:' % lib)
    for f in sorted(os.listdir(lib)):
        if V in f or f.startswith(('slicer-wasm', 'sla-web')):
            print('   ', f)
    print('\nToliau rankomis:  cd %s && git add lib && git commit && git push' % a.ghp)


if __name__ == '__main__':
    main()
