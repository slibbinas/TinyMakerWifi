# -*- coding: utf-8 -*-
"""release_doctor.py — po-release „ar tikrai viskas paskelbta" patikra.

`release.py` sudeda firmware (bins/version.txt/versions.txt/gh-pages) ir tikrina
manualo versiją. Bet periferiją — demo, manual publish, mockai, experimental sync,
issue'ų uždarymas — daro žmogus per release-checklist RANKOMIS, iš atminties. 0.16.2
metu taip prakrito: demo liko 0.16.0, du issue'ai liko atviri.

Šis skriptas paverčia „atsimink 8 žingsnius" į „paleisk 1 komandą": patikrina,
ar viskas, kas turi rodyti naują versiją, ją tikrai rodo — LOKALIAI ir GYVAI.
Read-only, nieko nekeičia. Exit 0 = viskas žalia, 1 = kažkas pasenę.

    python scripts/dev/release_doctor.py            # versija iš platformio.ini
    python scripts/dev/release_doctor.py 0.16.2     # aiški versija
    python scripts/dev/release_doctor.py --stable   # tikisi, kad version.txt == versija
                                                    # (promotinta stable, ne beta)

Susiję memory: release-checklist (D žingsnis), sanity-check (#5), check_docs_drift.
"""
import io, os, re, subprocess, sys, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
SITE = "https://tinymakerwifi.com"

RED, GRN, YEL, DIM, OFF = "\033[31m", "\033[32m", "\033[33m", "\033[2m", "\033[0m"
fails, warns = [], []


def line(status, name, detail):
    tag = {"OK": GRN + "OK  " + OFF, "FAIL": RED + "FAIL" + OFF,
           "??": YEL + "??  " + OFF}[status]
    print("    %s  %-16s %s" % (tag, name, detail))


def ok(name, detail):
    line("OK", name, detail)


def fail(name, detail):
    line("FAIL", name, detail)
    fails.append(name)


def warn(name, detail):
    line("??", name, detail)
    warns.append(name)


def read(path):
    with io.open(os.path.join(REPO, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def git(*args):
    r = subprocess.run(["git", "-C", REPO, *args], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout or "").strip(), (r.stderr or "").strip()


def gh(*args):
    try:
        r = subprocess.run(["gh", *args], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=REPO)
        return r.returncode, (r.stdout or "").strip()
    except FileNotFoundError:
        return 127, ""


def fetch(path):
    """Gyvas puslapis per worker (su cache-bust). None jei nepavyko."""
    url = SITE + path + ("&" if "?" in path else "?") + "cb=doctor"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "release-doctor"})
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.read().decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001
        return None


def ghpages(path):
    """PUSHED gh-pages failas (git show), None jei nėra."""
    rc, out, _ = git("show", "origin/gh-pages:" + path)
    return out if rc == 0 else None


def detect_version():
    for a in sys.argv[1:]:
        if re.match(r"^\d+\.\d+\.\d+$", a):
            return a
    ini = read("platformio.ini")
    vs = set(re.findall(r'FIRMWARE_VERSION=\\?"?(\d+\.\d+\.\d+)"?', ini))
    if len(vs) != 1:
        print("negaliu nustatyti versijos iš platformio.ini:", vs or "nerasta")
        sys.exit(2)
    return vs.pop()


def main():
    v = detect_version()
    stable = "--stable" in sys.argv
    print("\nrelease_doctor %s (%s)\n" % (v, "stable/promoted" if stable else "beta"))
    git("fetch", "-q", "origin", "gh-pages", "main", "experimental")

    print("  LOKALIAI")
    man = read("docs/manual/index.html")
    other = set(re.findall(r"User Manual · v(\d+\.\d+\.\d+)", man)) - {v}
    ok("manual (local)", "abu spots v%s" % v) if not other else \
        fail("manual (local)", "dar rodo v%s" % ", v".join(sorted(other)))
    ch = read("CHANGELOG.md")
    ok("CHANGELOG", "turi [%s] įrašą" % v) if ("[%s]" % v) in ch else \
        fail("CHANGELOG", "NĖRA [%s] įrašo" % v)

    print("  GYVAI (gh-pages / worker)")
    demo = fetch("/demo/")
    if demo is None:
        warn("demo", "nepavyko parsisiųsti (tinklas?)")
    elif v in demo:
        ok("demo", "rodo %s" % v)
    else:
        seen = sorted(set(re.findall(r"0\.\d+\.\d+", demo)))
        fail("demo", "NErodo %s (matau: %s) — perregeneruok build_demo.py + push" %
             (v, ", ".join(seen) or "?"))
    lman = fetch("/manual/")
    if lman is None:
        warn("manual (live)", "nepavyko parsisiųsti")
    elif ("v" + v) in lman or ("· v%s" % v) in lman:
        ok("manual (live)", "v%s" % v)
    else:
        fail("manual (live)", "NErodo v%s — publikuok manual į gh-pages" % v)
    vers = ghpages("versions.txt")
    if vers is None:
        warn("versions.txt", "nerasta gh-pages")
    elif v in vers:
        ok("versions.txt", "picker turi %s" % v)
    else:
        fail("versions.txt", "picker NEturi %s" % v)
    _raw = (ghpages("version.txt") or "").strip()  # version.txt = versija + firmware URL
    vtxt = _raw.splitlines()[0].strip() if _raw else ""
    if stable:
        ok("version.txt", "self-update == %s" % v) if vtxt == v else \
            fail("version.txt", "self-update = %s, o turi būt %s (--stable)" % (vtxt, v))
    else:
        warn("version.txt", "self-update = %s (beta — taip ir turi būt iki promote)" % vtxt)
    tests = fetch("/tests")
    if tests is None:
        warn("tests panel", "nepavyko parsisiųsti")
    elif v in tests:
        ok("tests panel", "%s" % v)
    else:
        warn("tests panel", "NErodo %s — sugen. tests-X-Y-Z + KV panel:tests" % v)

    print("  GIT")
    rc, out, _ = git("log", "--oneline", "origin/experimental..origin/main")
    ok("experimental", "neatsilikęs nuo main") if not out else \
        fail("experimental", "atsilikęs %d commit'ais — sync main→experimental" %
             len(out.splitlines()))

    print("  ISSUE'AI")
    rc, out = gh("issue", "list", "--state", "open", "--search", "[%s]" % v,
                 "--json", "number,title", "-q",
                 ".[] | \"#\\(.number) \\(.title)\"")
    if rc == 127:
        warn("open [%s]" % v, "gh CLI neprieinamas — patikrink rankomis")
    elif not out:
        ok("open [%s]" % v, "nėra atvirų")
    else:
        warn("open [%s]" % v, "peržiūrėk (padaryta bet atvira?): %s" %
             " | ".join(out.splitlines()))

    print()
    if fails:
        print("%s!! %d PASENĘ: %s — sutvarkyk ir kartok.%s" %
              (RED, len(fails), ", ".join(fails), OFF))
        sys.exit(1)
    if warns:
        print("%sViskas žalia (%d perspėjimai peržiūrai — žr. ??).%s" % (YEL, len(warns), OFF))
    else:
        print("%sViskas žalia — release baigtas.%s" % (GRN, OFF))
    sys.exit(0)


if __name__ == "__main__":
    main()
