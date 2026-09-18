# -*- coding: utf-8 -*-
"""Testinių modelių generavimas: *.scad -> dvejetainis *.stl + *.png peržiūra.

    ~/.platformio/penv/Scripts/python.exe scripts/dev/resin-lab/models/build.py

OpenSCAD - nešiojama versija C:/Users/SViktoras/Tools/OpenSCAD (kitur - env
OPENSCAD). Dvejetainis STL todėl, kad OpenSCAD 2021.01 rašo tik tekstinį, o jis
~3 kartus didesnis. Pabaigoje patikrinama, ar kiekvienas modelis telpa ant
plokštės (PrusaSlicer/TinyMaker.ini: 40,8 × 30,6 mm, aukštis 60 mm).
"""
import glob, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.environ.get("OPENSCAD", r"C:/Users/SViktoras/Tools/OpenSCAD/openscad.com")
PLATE = (40.8, 30.6, 60.0)


def ascii_to_binary(path):
    tris, norm, verts = [], None, []
    for line in open(path, encoding="ascii"):
        m = re.match(r"\s*facet normal\s+(\S+)\s+(\S+)\s+(\S+)", line)
        if m:
            norm, verts = tuple(map(float, m.groups())), []
            continue
        m = re.match(r"\s*vertex\s+(\S+)\s+(\S+)\s+(\S+)", line)
        if m:
            verts.append(tuple(map(float, m.groups())))
            if len(verts) == 3:
                tris.append((norm, verts))
    with open(path, "wb") as f:
        f.write(b"TinyMakerWiFi resin-lab test model".ljust(80, b" "))
        f.write(struct.pack("<I", len(tris)))
        for n, v in tris:
            f.write(struct.pack("<12fH", *n, *v[0], *v[1], *v[2], 0))
    pts = [p for _, v in tris for p in v]
    return [max(p[i] for p in pts) - min(p[i] for p in pts) for i in range(3)], len(tris)


def main():
    bad = 0
    for scad in sorted(glob.glob(os.path.join(HERE, "*.scad"))):
        base = scad[:-5]
        subprocess.run([OS, "-o", base + ".stl", scad], check=True, capture_output=True)
        subprocess.run([OS, "-o", base + ".png", "--imgsize=480,360", "--viewall", "--autocenter",
                        "--colorscheme=Tomorrow", scad], check=True, capture_output=True)
        (dx, dy, dz), n = ascii_to_binary(base + ".stl")
        fits = max(dx, dy) <= PLATE[0] and min(dx, dy) <= PLATE[1] and dz <= PLATE[2]
        bad += not fits
        print("%-10s %5.1f x %5.1f x %5.1f mm  %5d tri  %s" % (
            os.path.basename(base), dx, dy, dz, n, "telpa" if fits else "NETELPA"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
