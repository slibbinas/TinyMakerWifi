# -*- coding: utf-8 -*-
"""check_docs_drift.py — apsauga nuo tos nakties (2026-08-03) bėdos.

Gh-pages viešieji puslapiai (landing/manual/feedback) turi rankines pataisas
(temų toggle/handover, teisingi paveikslėlių dims/CSS). Repo `docs/` ir gh-pages
gali prasilenkti — o wholesale `cp repo->gh-pages` tada perrašo naujesnę live
versiją senesne repo (dingsta toggle, iškraipomi paveikslėliai).

Šis skriptas palygina repo `docs/<page>/index.html` su PUSHED gh-pages
(`origin/gh-pages:<page>/index.html`) ir flag'ina drift'ą. Whitespace/CRLF
normalizuojama — lyginam turinį, ne eilučių galūnes.

Paleisti (sesijos pradžioj / prieš docs deploy / sanity-check #5):
    python scripts/dev/check_docs_drift.py
Exit 0 = sutampa, 1 = drift (tada spręst: kuris teisingas, suderint SURGICAL).

Susiję memory: docs-deploy-gh-pages-ne-repo, sanity-check (#5).
"""
import io, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
# repo docs/<page> -> gh-pages <page>
PAGES = {"landing": "landing", "manual": "manual", "feedback": "feedback"}


def norm(s):
    """Turinio parašas, atsparus CRLF/whitespace skirtumams."""
    return re.sub(r"\s+", " ", s.replace("\r\n", "\n")).strip()


def repo_doc(page):
    p = os.path.join(REPO, "docs", page, "index.html")
    with io.open(p, encoding="utf-8", errors="replace") as f:
        return f.read()


def ghpages_doc(page):
    """PUSHED gh-pages versija (ne lokalus worktree — kad tikrintume live)."""
    subprocess.run(["git", "fetch", "origin", "gh-pages"], cwd=REPO,
                   capture_output=True)
    r = subprocess.run(["git", "show", f"origin/gh-pages:{page}/index.html"],
                       cwd=REPO, capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        return None
    return r.stdout


def main():
    drift = []
    for repo_page, ghp_page in PAGES.items():
        rc = repo_doc(repo_page)
        gc = ghpages_doc(ghp_page)
        if gc is None:
            print("  ?     %-9s — nerasta gh-pages" % repo_page); continue
        if norm(rc) == norm(gc):
            print("  OK    %-9s — repo == live gh-pages" % repo_page)
        else:
            drift.append(repo_page)
            print("  DRIFT %-9s — repo != live gh-pages (kažkas redaguota "
                  "vienoj pusėj be kitos)" % repo_page)
    print("\nroadmap = gh-pages-only (memory/team-roadmap.html), ne repo docs — "
          "netikrinama čia.")
    if drift:
        print("\n!! DRIFT: %s — NEdaryk wholesale cp; spręsk kuris teisingas, "
              "suderink SURGICAL. Žr. [[docs-deploy-gh-pages-ne-repo]]."
              % ", ".join(drift))
        sys.exit(1)
    print("\nViskas suderinta — deploy saugus.")


if __name__ == "__main__":
    main()
