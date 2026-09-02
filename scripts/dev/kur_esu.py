# -*- coding: utf-8 -*-
"""kur_esu.py - kurioje srityje ir sakoje si sesija dirba.

Sesijos pradzioje (zr. CLAUDE.md „Sesijos pradzia") reikia prisistatyti: kuri
sritis, kuri saka, koks katalogas. Saku vardai keiciasi - 2026-08-23 pervadinom
du is karto - tad atsakymas imamas is git, ne is atminties.

    python scripts/dev/kur_esu.py --sesija <sesijos-vardas>

Isvestis tycia trumpa: viena eilute prisistatymui + kontekstas po ja.

DU dalykai, kuriu skriptas NEGALI zinoti, tad ir nesideda zinantis:

1. Sritis cia yra SPEJIMAS is sakos ir katalogo. Skriptas nemato, kam V atidare
   langa. 2026-08-27 sesija, atidaryta slicerio darbui, atsistojo ant sakos
   0.17/slicer-merge, gavo "SRITIS: Printeris" ir patikejo - nes ankstesne
   versija sriti spausdino kaip fakta, be jokio "gal". Todel dabar salia visada
   eina pagrindas ir prasymas patvirtinti su V.

2. Ar tame paciame darbo medyje jau sedi kita sesija. Tam yra zyme: su --sesija
   skriptas irašo .claude/uzimta.json (git jo nemato, zr. .gitignore), ir kita
   sesija, atsistojusi tame paciame kataloge, gauna perspejima. Tas pats
   2026-08-27: dvi sesijos vienoje sakoje ir viename medyje.

Kanono patikra (sritys.json) tyli trimis atvejais, nes ju nei vienas nera
pasenes irasas: laikina sesijos saka, antra tos pacios srities saka (slcr/lab
salia slcr/dev) ir bendra main/experimental. Garsus ispejimas paliktas tikram
pervadinimui - kitaip jis degtu kaskart, o degantis visada nebematomas.
"""
import io
import json
import os
import subprocess
import sys
import time

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


# Kiek laiko svetimos sesijos zyme dar laikoma gyva. Ilgiau pasedejusi zyme
# greiciausiai likusi nuo uzdarytos sesijos, ir triuksmauti del jos nebeverta.
ZYME_GYVA_H = 8


def zymes_kelias(root):
    return os.path.join(root, ".claude", "uzimta.json")


def zyme_skaityk(root):
    try:
        return json.load(io.open(zymes_kelias(root), encoding="utf-8"))
    except Exception:
        return None


def zyme_rasyk(root, sesija, branch):
    try:
        d = os.path.dirname(zymes_kelias(root))
        if not os.path.isdir(d):
            os.makedirs(d)
        with io.open(zymes_kelias(root), "w", encoding="utf-8") as f:
            f.write(json.dumps({
                "sesija": sesija,
                "saka": branch,
                "laikas": time.strftime("%Y-%m-%d %H:%M"),
                "epoch": int(time.time()),
            }, ensure_ascii=False, indent=2))
    except Exception:
        pass  # zyme yra patogumas, ne butinybe - del jos skriptas negriuva


def amzius(zyme):
    """Zymes amzius valandomis (float) arba None, jei nesuprantama."""
    try:
        return (time.time() - float(zyme.get("epoch", 0))) / 3600.0
    except Exception:
        return None


def git(*args):
    try:
        out = subprocess.check_output(("git",) + args, stderr=subprocess.STDOUT)
        return out.decode("utf-8", "replace").strip()
    except Exception:
        return ""


def say(text):
    sys.stdout.buffer.write((text + "\n").encode("utf-8", "replace"))


def saka_yra_nutolusi(branch):
    """Ar tokia saka guli GitHub'e.

    Imam vietine nutolusiu saku kopija (refs/remotes/origin/...), ne `ls-remote` -
    veikia ir be tinklo, ir nekainuoja sekundes kiekvienam sesijos startui.
    Sviezumas cia nesvarbus: klausiam tik „ar tai nuolatine saka, ar sesijos
    laikinoji", ne „ar sutampa commit'ai".
    """
    if not branch or branch.startswith("("):
        return False
    return bool(git("rev-parse", "--verify", "--quiet",
                    "refs/remotes/origin/%s" % branch))


def main():
    sesija = ""
    if "--sesija" in sys.argv:
        i = sys.argv.index("--sesija")
        if i + 1 < len(sys.argv):
            sesija = sys.argv[i + 1]

    branch = git("branch", "--show-current") or "(detached HEAD)"
    root = git("rev-parse", "--show-toplevel") or os.getcwd()
    # Worktree kataloge basename yra worktree vardas, ne repo - imam is remote.
    origin = git("remote", "get-url", "origin")
    repo = os.path.basename(origin).replace(".git", "") if origin else os.path.basename(root)
    head = git("log", "-1", "--format=%h %s")
    upstream = git("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{u}")
    dirty = git("status", "--porcelain")

    sritis, pastaba, pagrindas = None, "", ""
    for pref, name in BY_PREFIX.items():
        if branch.startswith(pref):
            sritis = name
            pagrindas = 'saka prasideda "%s"' % pref
            break
    if sritis is None and branch in LEGACY:
        sritis, pastaba = LEGACY[branch]
        pagrindas = "senas sakos vardas"
    if sritis is None:
        low = root.replace("\\", "/").lower()
        for key, name in BY_PATH.items():
            if key in low:
                sritis = name
                pagrindas = 'kataloge yra "%s"' % key
                break
    if sritis is None and branch in ("main", "experimental") or branch.startswith("0.17/"):
        sritis = "Printeris"
        pagrindas = pagrindas or 'saka "%s" - atsarginis spejimas, ne taisykle' % branch

    say("")
    if sritis:
        say("  SRITIS:   %s   (spejimas: %s)%s"
            % (sritis, pagrindas or "nezinia is ko", ("  [%s]" % pastaba) if pastaba else ""))
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

    # Palyginam su uzfiksuotu kanonu: pasenes irasas turi issiduoti PATS.
    reg = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sritys.json")
    if os.path.isfile(reg):
        try:
            data = json.load(io.open(reg, encoding="utf-8"))
        except Exception as e:
            say("  (!) sritys.json neperskaitomas: %s" % e)
            data = None
        if data:
            rec = None
            for it in data.get("sritys", []):
                if sritis and it.get("sritis", "").lower().startswith(sritis.split()[0].lower()):
                    rec = it
                    break
            if rec:
                # sritys.json laiko NAMU saka. Sesija gali teisetai sedeti kitoje:
                # laikinoje savo worktree sakoje, antroje tos pacios srities sakoje
                # (slcr/lab salia slcr/dev) arba bendroje main/experimental. Nei
                # vienas is tu atveju nera pasenes kanonas, o ispejimas, kuris dega
                # visada, per savaite tampa nematomas - ir tada pradings tikras
                # pervadinimas, del kurio si patikra apskritai atsirado (08-23).
                namu = rec.get("saka")
                pref = rec.get("priesdelis") or ""
                bendros = data.get("pagrindines", {})
                if namu == branch:
                    pass
                elif branch.startswith("claude/") or not saka_yra_nutolusi(branch):
                    # Priezastis sakoma ta, kuri tikrai patikrinta: "claude/" saka
                    # atpazistama is vardo ir GitHub'e ji kaip tik daznai guli
                    # (debesu sesijos), tad apie GitHub'a cia netvirtinam nieko.
                    kodel = ("sesijos darbo saka" if branch.startswith("claude/")
                             else "GitHub'e jos nera")
                    say("  laikina:  darbo saka '%s' (%s); namu saka - '%s', "
                        "kanonas sutampa" % (branch, kodel, namu))
                elif pref and branch.startswith(pref):
                    say("  antra saka: dirbama '%s', tos pacios srities namu saka - "
                        "'%s'. Ne pasenimas" % (branch, namu))
                elif branch in bendros:
                    say("  bendra saka: dirbama '%s' (%s); srities namu saka - '%s'"
                        % (branch, bendros[branch], namu))
                else:
                    say("  (!) SRITYS.JSON PASENES: uzfiksuota saka '%s', realiai '%s' "
                        "(pervadinta?)." % (namu, branch))
                    say("      Atnaujink scripts/dev/sritys.json tame paciame commit'e.")
                if rec.get("pastaba"):
                    say("  pastaba:  %s" % rec["pastaba"])
                say("")
            elif sritis:
                say("  (!) sritys.json neturi irašo sriciai '%s' - pridek." % sritis)
                say("")

    # Sritis yra spejimas, ir taip ir turi atrodyti - kitaip sesija ja perskaito
    # kaip atsakyma ir nebeklausia (taip ir nutiko 2026-08-27).
    say("  (!) SRITIS - SPEJIMAS. Skriptas mato tik saka ir kataloga; kam V atidare")
    say("      si langa, jis nemato. Paklausk V, ar sritis teisinga, PRIES pirma")
    say("      pakeitima (CLAUDE.md 'Sesijos pradzia').")
    say("")

    # Uzimtumo zyme: ar siame medyje jau sedi kita sesija.
    sena = zyme_skaityk(root)
    if sena and sena.get("sesija") and sena.get("sesija") != sesija:
        val = amzius(sena)
        if val is None or val < ZYME_GYVA_H:
            if val is None:
                kada = sena.get("laikas", "?")
            elif val < 1:
                kada = "pries %d min" % int(val * 60)
            else:
                kada = "pries %.1f val" % val
            say("  (!!) SI DARBO MEDI JAU PASIEME KITA SESIJA")
            say("       sesija:  %s" % sena["sesija"])
            say("       saka:    %s   (%s, %s)" % (sena.get("saka", "?"),
                                                   sena.get("laikas", "?"), kada))
            say("       Du redaktoriai viename medyje susipjauna: vieno pakeitimai")
            say("       nukrenta i kito commit'a. Imk sau atskira worktree arba")
            say("       susitark su V, kuri sesija cia lieka.")
            say("")
    if sesija:
        zyme_rasyk(root, sesija, branch)
    else:
        say("  (i) Zyme neirasyta: paleisk su --sesija <savo-vardas>, kad kita sesija")
        say("      matytu, jog sis medis uzimtas.")
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
