# -*- coding: utf-8 -*-
"""Slicerio modulių publikavimas į gh-pages, prisegtais vardais.

Iki šiol tai buvo rankinis kopijavimas, ir jame tyliai dingdavo svarbiausia
dalis: failo vardas gaudavo versiją, o importas jo viduje likdavo be jos.
Tada prisegtas vardas nieko nebereiškia - bazė po juo keičiasi toliau, tik
dabar po tvarkingai atrodančiu vardu (V 08-17).

Kas parašoma į <ghp>/lib/:

  slicer-<VER>.js        algoritmas, importai PRISEGTI prie versijų
  slicer-core-<VER>.js   bazė
  clipper-<CVER>.js      Clipper, savo paties versija (ne mūsų)
  slicer.js              tas pats turinys, importai neprisegti - kad senas
  slicer-core.js         pultas, prašantis „slicer.js?v=…", matytų tą patį,
  clipper.js             o ne kitą realybę

Senasis `slicer-0.9.0.js` nekeičiamas: jis savarankiškas, be importų.

    python scripts/dev/publish_slicer.py [ghp-wt kelias]
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "web", "lib"))
GHP = sys.argv[1] if len(sys.argv) > 1 else "C:/PIO-build/ghp-wt"
OUT = os.path.join(GHP, "lib")


def read(name):
    return io.open(os.path.join(SRC, name), encoding="utf-8").read()


def write(name, text):
    path = os.path.join(OUT, name)
    io.open(path, "w", encoding="utf-8", newline="\n").write(text)
    return len(text.encode("utf-8"))


algo = read("slicer2.js")
core = read("slicer.js")
clip = read("clipper.js")

m = re.search(r"export const VERSION = '([^']+)'", algo)
if not m:
    sys.exit("slicer2.js: nerandu VERSION")
VER = m.group(1)

mc = re.search(r"export const VERSION = '([^']+)'", core)
if not mc:
    sys.exit("slicer.js: nerandu VERSION")
if mc.group(1) != VER:
    sys.exit("baze sako %s, algoritmas %s - rinkinys leidziamas kartu, "
             "numeris turi buti tas pats" % (mc.group(1), VER))

mv = re.search(r"ClipperLib\.version = '(\d+\.\d+\.\d+)", clip)
if not mv:
    sys.exit("clipper.js: nerandu versijos")
CVER = mv.group(1)          # 6.4.2 - Clipper'io, ne musu

if "-dev" in VER:
    sys.exit("VERSION = %s: juodrascio nepublikuojam prisegtu vardu" % VER)

CORE_PIN = "slicer-core-%s.js" % VER
CLIP_PIN = "clipper-%s.js" % CVER

# Prisegta grandine: KIEKVIENAS importas rodo i versijuota faila.
pinned = (algo.replace("from './slicer.js'", "from './%s'" % CORE_PIN)
              .replace("import('./clipper.js')", "import('./%s')" % CLIP_PIN))
# Neprisegta - senam pultui, kuris ima „slicer.js?v=…" ir tikisi tokiu vardu.
plain = algo.replace("from './slicer.js'", "from './slicer-core.js'")

wrote = []
wrote.append((CORE_PIN, write(CORE_PIN, core)))
wrote.append((CLIP_PIN, write(CLIP_PIN, clip)))
wrote.append(("slicer-%s.js" % VER, write("slicer-%s.js" % VER, pinned)))
wrote.append(("slicer-core.js", write("slicer-core.js", core)))
wrote.append(("clipper.js", write("clipper.js", clip)))
wrote.append(("slicer.js", write("slicer.js", plain)))

# Patikra, o ne pasitikejimas: butent si eilute ir buvo klausimas, del kurio
# visa tai daroma. Tikrinam PARASYTA faila, ne atmintyje turima teksta.
done = io.open(os.path.join(OUT, "slicer-%s.js" % VER), encoding="utf-8").read()
bad = re.findall(r"(?:from|import\()\s*'\./([A-Za-z0-9_.-]+\.js)'", done)
loose = [b for b in bad if b not in (CORE_PIN, CLIP_PIN)]
if loose:
    sys.exit("PRISEGIMAS NEPAVYKO: slicer-%s.js vis dar importuoja %s"
             % (VER, ", ".join(loose)))

for name, n in wrote:
    print("  %-24s %8d B" % (name, n))
print("rinkinys %s (clipper %s) - importai prisegti: %s" % (VER, CVER, ", ".join(bad)))
