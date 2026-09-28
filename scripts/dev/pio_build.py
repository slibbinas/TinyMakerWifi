# -*- coding: utf-8 -*-
"""pio_build.py - kas guli C:/PIO-build, kieno tai ir ka galima valyti.

C:/PIO-build naudoja kelios sesijos ir keli projektai (TinyMakerWiFi, Curing, 3D
modeliai). 2026-09-26 revizijoje pusei katalogu savininka teko speti, o saknyje
gulejo 66 pavieniai darbiniai failai. Nuo tada kiekvienas virsutinio lygio
katalogas turi zyme `.naudoja.json`, o pavieniai failai saknyje nededami (laikini
failai - i sesijos scratchpad).

Zyme (irasoma ranka arba `naudoju` komanda):

    {
      "paskirtis": "kam sitas katalogas",
      "tipas": "laikinas | irankis | build | worktree | archyvas",
      "sritis": "Printeris | Sliceris | Connect Live | Curing | 3D modeliai | ...",
      "naudojo": {"<sesijos vardas>": "YYYY-MM-DD"}
    }

    python scripts/dev/pio_build.py naudoju <katalogas> --sesija X --sritis Y --tipas T --paskirtis "..."
    python scripts/dev/pio_build.py ataskaita [--dienos 30]

Ataskaita nieko netrina ir nekelia: ji tik parodo katalogus be zymes, laikinus
katalogus, nenaudotus ilgiau nei --dienos, ir pavienius failus saknyje. Valo
sesija tik V leidus - perkelia i C:/PIO-BAK/<data>/ (karantinas, zr. atminti
pio-bak-karantinas).
"""
import datetime
import io
import json
import os
import sys

ROOT = r"C:\PIO-build"
ZYME = ".naudoja.json"
TIPAI = ("laikinas", "irankis", "build", "worktree", "archyvas")


def say(text):
    sys.stdout.buffer.write((text + "\n").encode("utf-8", "replace"))


def arg(name, default=""):
    if name in sys.argv:
        i = sys.argv.index(name)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return default


def skaityk(d):
    try:
        return json.load(io.open(os.path.join(d, ZYME), encoding="utf-8"))
    except (OSError, ValueError):
        return None


def naudoju():
    if len(sys.argv) < 3:
        say("naudoju <katalogas> --sesija X [--sritis Y --tipas T --paskirtis ...]")
        return 2
    kelias = os.path.abspath(sys.argv[2])
    rel = os.path.relpath(kelias, ROOT)
    if rel.startswith(".."):
        say("!! %s nera C:/PIO-build viduje" % kelias)
        return 2
    top = os.path.join(ROOT, rel.split(os.sep)[0])  # zyme tik virsutiniame lygyje
    if not os.path.isdir(top):
        say("!! %s nera katalogas" % top)
        return 2
    sesija = arg("--sesija")
    if not sesija:
        say("!! reikia --sesija")
        return 2
    z = skaityk(top) or {"paskirtis": "", "tipas": "", "sritis": "", "naudojo": {}}
    for key in ("paskirtis", "tipas", "sritis"):
        v = arg("--" + key)
        if v:
            z[key] = v
    if z.get("tipas") and z["tipas"] not in TIPAI:
        say("!! tipas turi buti vienas is: %s" % ", ".join(TIPAI))
        return 2
    z.setdefault("naudojo", {})[sesija] = datetime.date.today().isoformat()
    with io.open(os.path.join(top, ZYME), "w", encoding="utf-8") as f:
        f.write(json.dumps(z, ensure_ascii=False, indent=2) + "\n")
    say("zyme: %s -> %s, %s, %s" % (os.path.basename(top), z["sritis"] or "?", z["tipas"] or "?",
                                     ", ".join("%s %s" % kv for kv in sorted(z["naudojo"].items()))))
    return 0


def ataskaita():
    dienos = int(arg("--dienos", "30"))
    siandien = datetime.date.today()
    eil, be_zymes, seni, failai = [], [], [], []
    for n in sorted(os.listdir(ROOT), key=str.lower):
        p = os.path.join(ROOT, n)
        if os.path.isfile(p):
            failai.append(n)
            continue
        z = skaityk(p)
        if not z:
            be_zymes.append(n)
            eil.append((n, "?", "?", "-", "BE ZYMES"))
            continue
        datos = sorted(z.get("naudojo", {}).values())
        pask = datos[-1] if datos else ""
        busena = ""
        if z.get("tipas") == "laikinas" and pask:
            amzius = (siandien - datetime.date.fromisoformat(pask)).days
            if amzius > dienos:
                busena = "laikinas, nenaudotas %d d." % amzius
                seni.append(n)
        kas = ", ".join(sorted(z.get("naudojo", {})))
        eil.append((n, z.get("sritis") or "?", z.get("tipas") or "?", "%s (%s)" % (pask or "-", kas), busena))

    say("")
    say("  C:/PIO-build - %d katalogu, %d be zymes, %d seni laikini, %d pavieniai failai saknyje"
        % (len(eil), len(be_zymes), len(seni), len(failai)))
    say("")
    w = max([len(e[0]) for e in eil] + [8])
    for n, sr, tp, pask, b in eil:
        say("  %-*s  %-14s %-9s %-40s %s" % (w, n, sr, tp, pask, b))
    if failai:
        say("")
        say("  Pavieniai failai saknyje (turetu buti sesijos scratchpad'e):")
        for f in failai:
            say("    " + f)
    say("")
    say("  Nieko netrinta ir nekelta. Valyti - tik V leidus, i C:/PIO-BAK/<data>/.")
    return 0


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "ataskaita"
    if cmd == "naudoju":
        return naudoju()
    if cmd == "ataskaita":
        return ataskaita()
    say(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
