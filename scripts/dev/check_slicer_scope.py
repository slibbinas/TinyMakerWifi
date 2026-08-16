# -*- coding: utf-8 -*-
"""Slicerio saka liecia tik slicerio failus.

Riba tarp dvieju darbu eina ne per sakas, o per failus: algoritmas gyvena
`web/lib/`, o pultas ir firmware - `web/dashboard.html` ir `src/`. Pultas
slicerio kodo savyje neturi, jis ji PARSISIUNCIA, tad vienintelis mudvieju
susilietimas yra viena eilute su failo vardu.

Kol sitos ribos nesilaikem, `experimental2` pakeite `dashboard.html` 871
eilute, ir tie patys pakeitimai atsirado dviejose vietose (V 08-17). Cia ta
sutartis paverciama tikrinama dalyku: apsirikti tiesiog nepavyks.

    python scripts/dev/check_slicer_scope.py            # staged failai
    python scripts/dev/check_slicer_scope.py --range A..B

Grazina 1, jei rastas svetimas failas. Samoningai peszengti - `--no-verify`.
"""
import subprocess
import sys

# Svetima teritorija: firmware ir pultas. Viskas kita (web/lib, scripts, docs)
# yra musu, todel sarasas rasomas kaip draudimas, ne kaip leidimas - naujas
# musu failas neturi reikalauti sito skripto pataisos.
FORBIDDEN = ("src/", "web/dashboard.html", "include/", "platformio.ini")

BRANCH = "experimental2"


def staged():
    out = subprocess.check_output(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        text=True)
    return [l.strip() for l in out.splitlines() if l.strip()]


def in_range(rng):
    out = subprocess.check_output(
        ["git", "diff", "--name-only", "--diff-filter=ACMR", rng], text=True)
    return [l.strip() for l in out.splitlines() if l.strip()]


def branch():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"], text=True).strip()
    except subprocess.CalledProcessError:
        return ""


def main():
    rng = None
    if len(sys.argv) > 2 and sys.argv[1] == "--range":
        rng = sys.argv[2]

    # Hook'u dalinasi visi to paties repo worktree'ai, tad firmware sakoje sis
    # patikrinimas turi TYLETI - kitaip uzblokuotu tiketina darba.
    if rng is None and branch() != BRANCH:
        return 0

    files = in_range(rng) if rng else staged()
    bad = [f for f in files if any(f.startswith(p) for p in FORBIDDEN)]
    if not bad:
        return 0

    print("")
    print("  Slicerio saka lieciu tik slicerio failus, o cia yra svetimu:")
    print("")
    for f in bad:
        print("      " + f)
    print("")
    print("  Pultas ir firmware - kitos sesijos darbas. Jei algoritmui reikia,")
    print("  kad pultas rodytu ka nors nauja, tai prasymas jiems, ne musu")
    print("  taisymas: kitaip tie patys pakeitimai atsiranda dviejose vietose")
    print("  ir paskui juos reikia sulieti rankomis.")
    print("")
    print("  Tikrai reikia (pvz. bendras sakos suliejimas) - git commit --no-verify")
    print("")
    return 1


if __name__ == "__main__":
    sys.exit(main())
