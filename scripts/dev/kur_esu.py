# -*- coding: utf-8 -*-
"""kur_esu.py - kurioje srityje ir sakoje si sesija dirba.

Sesijos pradzioje (zr. CLAUDE.md „Sesijos pradzia") reikia prisistatyti: kuri
sritis, kuri saka, koks katalogas. Saku vardai keiciasi - 2026-08-23 pervadinom
du is karto - tad atsakymas imamas is git, ne is atminties.

    python scripts/dev/kur_esu.py

Isvestis tycia trumpa: viena eilute prisistatymui + kontekstas po ja.
"""
import io
import os
import subprocess
import sys

# Sritis atpazistama pagal sakos priesdeli; kelias - atsarginis kelias tiems
# atvejams, kai saka dar nepervadinta pagal nauja sistema.
BY_PREFIX = {
    "prnt/": "Printeris",
    "slcr/": "Sliceris",
    "cliv/": "Connect Live",
    "cure/": "Curing stotele",
}
BY_PATH = {
    "tinymakercuring": "Curing stotele",
    "exp2-wt": "Sliceris",
    "s3-wt": "Sliceris (laboratorija)",
    "ghp-wt": "Leidyba (gh-pages)",
}
# Senos, dar nepervadintos sakos - kad sesija nesiblaskytu.
LEGACY = {
    "feature/connect-live": ("Connect Live", "turi buti pervadinta i cliv/gateway (issue 114)"),
    "experimental2": ("Sliceris", "pervadinta i slcr/dev 2026-08-23"),
    "slicer3": ("Sliceris", "pervadinta i slcr/lab 2026-08-23"),
}


def git(*args):
    try:
        out = subprocess.check_output(("git",) + args, stderr=subprocess.STDOUT)
        return out.decode("utf-8", "replace").strip()
    except Exception:
        return ""


def say(text):
    sys.stdout.buffer.write((text + "\n").encode("utf-8", "replace"))


def main():
    branch = git("branch", "--show-current") or "(detached HEAD)"
    root = git("rev-parse", "--show-toplevel") or os.getcwd()
    # Worktree kataloge basename yra worktree vardas, ne repo - imam is remote.
    origin = git("remote", "get-url", "origin")
    repo = os.path.basename(origin).replace(".git", "") if origin else os.path.basename(root)
    head = git("log", "-1", "--format=%h %s")
    upstream = git("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
    dirty = git("status", "--porcelain")

    sritis, pastaba = None, ""
    for pref, name in BY_PREFIX.items():
        if branch.startswith(pref):
            sritis = name
            break
    if sritis is None and branch in LEGACY:
        sritis, pastaba = LEGACY[branch]
    if sritis is None:
        low = root.replace("\\", "/").lower()
        for key, name in BY_PATH.items():
            if key in low:
                sritis = name
                break
    if sritis is None and branch in ("main", "experimental") or branch.startswith("0.17/"):
        sritis = "Printeris"

    say("")
    if sritis:
        say("  SRITIS:   %s%s" % (sritis, ("  (%s)" % pastaba) if pastaba else ""))
    else:
        say("  SRITIS:   NEAISKU - klausk V ir pasiulyk meniu (zr. CLAUDE.md)")
    say("  SAKA:     %s" % branch)
    say("  KATALOGAS: %s" % root)
    say("  REPO:     %s" % repo)
    say("")
    if upstream:
        ab = git("rev-list", "--left-right", "--count", "%s...%s" % (branch, upstream))
        say("  nutolusi: %s   (priekyje/atsilieka: %s)" % (upstream, ab.replace("\t", " / ") or "?"))
    else:
        say("  nutolusi: NERA - sios sakos GitHub'e dar nera, darbas tik siame diske")
    say("  HEAD:     %s" % head)
    say("  medis:    %s" % ("svarus" if not dirty else "NESVARUS (%d failai)" % len(dirty.splitlines())))
    say("")

    wt = git("worktree", "list")
    if wt:
        say("  Kitos sio repo darbo vietos:")
        for line in wt.splitlines():
            mark = " <-- cia" if line.startswith(root.replace("/", "\\")) or root in line.replace("\\", "/") else ""
            say("    %s%s" % (line, mark))
        say("")
    say("  Curing gyvena ATSKIROJE repo: Documents/PlatformIO/Projects/TinyMakerCuring")
    say("")


if __name__ == "__main__":
    main()
