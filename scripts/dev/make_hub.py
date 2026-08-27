# -*- coding: utf-8 -*-
"""Bendras pultas: viskas, kas leidžiasi iš localhost, vienoje vietoje.

Generuojamas, o ne rašomas ranka: datos ir „kiek seniai liesta" turi būti tikri,
kitaip po savaitės pultas meluoja. Paleisti:

    ~/.platformio/penv/Scripts/python.exe scripts/dev/make_hub.py

Rezultatas - scripts/dev/index.html, tad `http://localhost:8899/` atidaro pultą.
Archyvuoti = perkelti failą į scripts/dev/archyvas/ (jis lieka pasiekiamas, tik
atskiroje, suskleistoje sekcijoje).
"""
import io, os, re, time, html, json

HERE = os.path.dirname(os.path.abspath(__file__))
ARCH = os.path.join(HERE, "archyvas")
OUT = os.path.join(HERE, "index.html")

# Asmeniniai raktai gyvena SALIA, o ne cia: sis failas commit'inamas i vieša
# repo, o scripts/dev/local-links.json yra .gitignore. Nera failo - pultas
# rodo paprasta nuoroda ir pasako, kur raktas guli.
def local_links():
    try:
        return json.load(io.open(os.path.join(HERE, "local-links.json"),
                                 encoding="utf-8"))
    except (OSError, ValueError):
        return {}

# Ką kiekvienas failas yra. Nėra sąraše = pultas jį parodys kaip „be aprašymo",
# ir tai pats savaime signalas: arba aprašyk, arba archyvuok.
CATALOG = {
    "testai-0-16.html": ("Geležies testai · 0.17 dervų profiliai",
        "Punktai su ✓/✗ ir pastabomis; apačioje ataskaita kopijavimui.", "testai"),
    "busenos.html": ("Būsenų žemėlapis",
        "Kur printeris gali būti, kaip pereina ir kas kurioje būsenoje uždrausta.", "testai"),
    "scenarijai.html": ("Naudojimo scenarijai",
        "Kaip žmogus iš tikrųjų naudoja pultą: prielaidos, tikslas, žingsniai, rezultatas.", "testai"),
    "resin-publish.html": ("Dervų bibliotekos tvarkymas",
        "Naujas profilis, taisymas, kopija, laikinas sustabdymas - be rankinio JSON.", "irankiai"),
    "demo.html": ("Pulto demo",
        "Visas dashboard be printerio (suklastoti duomenys). Generuoja build_demo.py.", "stendai"),
    "voxel-preview-lab.html": ("3D peržiūros stendas",
        "Modelio piešimas be printerio: modelis, režimas, progresas, visi variantai greta.", "stendai"),
    "nav-prototype.html": ("Navigacijos prototipas",
        "Senas pulto navigacijos eskizas iš 0.15 laikų.", "stendai"),
}

# Kas gyvena kitur (kitas portas ar kitas serveris) - nuorodos su paleidimo komanda.
KITUR = [
    ("3D pultas su žymėmis", "http://localhost:8080/3d/pultas.html",
     "Mūsų ir Prusos pjūviai greta; pažymėjus sritį gaunamos mm koordinatės.",
     "python pultas3d.py"),
    ("Slicerio stendas", "http://localhost:8897/lab/lab.html",
     "Tikras pultas su netikru printeriu ir jau įkeltu modeliu - slicerio pataisoms.",
     "python -m http.server 8897  (iš C:/PIO-build)"),
    ("Printerio pultas", "http://tinymaker.local/",
     "Tikras printeris: būsena, modeliai, dervos, nustatymai.", None),
]

def kitur_all():
    """KITUR + testų pultas su asmeniniu raktu, jei raktas yra vietiniame faile.

    Su raktu žymos saugomos serveryje, tad telefonas prie printerio ir kompiuteris
    ant stalo rodo tą patį; be rakto pultas veikia, bet žymos lieka toje naršyklėje.
    """
    k = local_links().get("tests_key", "")
    if k:
        url = "https://tinymakerwifi.com/tests?k=" + k
        desc = ("0.17 testų pultas su TAVO raktu: žymos saugomos serveryje ir "
                "keliauja tarp telefono ir kompiuterio. Nedalink šios nuorodos - "
                "kas turi raktą, tas gali žymes keisti.")
    else:
        url = "https://tinymakerwifi.com/tests"
        desc = ("0.17 testų pultas be rakto: veikia, bet žymos lieka tik šioje "
                "naršyklėje. Raktas guli memory key-links; įdėk jį į "
                "scripts/dev/local-links.json kaip {\"tests_key\": \"...\"}.")
    return KITUR + [("Testų pultas (www)", url, desc, None)]

AGE_FRESH, AGE_OLD = 7, 30      # dienos

def build_ver(path):
    """Versijos/build'o žymė iš paties failo, jei jis tokią turi."""
    try:
        s = io.open(path, encoding="utf-8", errors="ignore").read(4000)
    except OSError:
        return ""
    m = re.search(r"[Bb]uild <b>([^<]{4,24})</b>", s)
    if m:
        return m.group(1).strip()
    m = re.search(r"build <code>([^<]{4,24})</code>", s)
    return m.group(1).strip() if m else ""

def rows(folder):
    out = []
    if not os.path.isdir(folder):
        return out
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".html") or name == "index.html":
            continue
        p = os.path.join(folder, name)
        st = os.stat(p)
        title, purpose, group = CATALOG.get(name, (name, "", "stendai"))
        out.append({
            "file": name, "title": title, "purpose": purpose, "group": group,
            "mtime": st.st_mtime, "kb": round(st.st_size / 1024),
            "ver": build_ver(p),
            "days": int((time.time() - st.st_mtime) / 86400),
        })
    return out

def card(r, archived=False):
    if r["days"] <= AGE_FRESH: pill, cls = "šviežias", "p-ok"
    elif r["days"] <= AGE_OLD: pill, cls = "%d d." % r["days"], "p-warn"
    else: pill, cls = "%d d. neliesta" % r["days"], "p-old"
    if archived: pill, cls = "archyve", "p-mut"
    href = ("archyvas/" if archived else "") + r["file"]
    meta = time.strftime("%Y-%m-%d %H:%M", time.localtime(r["mtime"]))
    if r["ver"]: meta += " · " + html.escape(r["ver"])
    meta += " · %d KB" % r["kb"]
    return ("""<a class="c" href="{href}" target="_blank" rel="noopener">
  <div class="ct"><span class="cn">{title}</span><span class="pill {cls}">{pill}</span></div>
  <div class="cp">{purpose}</div>
  <div class="cm">{meta}</div>
</a>""").format(href=html.escape(href), title=html.escape(r["title"]), cls=cls, pill=pill,
                purpose=html.escape(r["purpose"] or "Be aprašymo - arba aprašyk, arba archyvuok."),
                meta=meta)

MEM = os.path.join(os.path.expanduser("~"), ".claude", "projects",
                   "C--Users-SViktoras-Documents-PlatformIO-Projects-TinyMakerWiFi", "memory")
LOCAL = os.path.join(HERE, "local")
PLANAI = [
    ("planas.html", "Planas (vidinis)",
     "Visas darbu planas: busenos, sprintai, idejos. Kopija atnaujinama kas paleidima."),
    ("team-roadmap.html", "Komandos roadmap",
     "Ka mato Brianas ir Tanneris: be vidines virtuves. Kopija is memory."),
    ("disko-zemelapis.html", "Disko zemelapis",
     "Kur diske guli printeris, sliceris ir curing: keliai, worktree'ai, build."),
    ("pultas-0-17.html", "0.17 testu pultas",
     "105 punktai is registro, sugrupuoti pagal tai, ko reikia rankoje. Generuoja _mk_panel.py."),
]

def copy_plans():
    """Planu kopijos i local/, kad tas pats serveris juos atiduotu."""
    out = []
    if not os.path.isdir(MEM):
        return out
    if not os.path.isdir(LOCAL):
        os.makedirs(LOCAL)
    for name, title, purpose in PLANAI:
        src = os.path.join(MEM, name)
        if not os.path.isfile(src):
            continue
        try:
            data = io.open(src, encoding="utf-8", errors="replace").read()
            io.open(os.path.join(LOCAL, name), "w", encoding="utf-8", newline="").write(data)
        except OSError:
            continue
        st = os.stat(src)          # data ir dydis - is ORIGINALO, ne kopijos
        out.append({"file": "local/" + name, "title": title, "purpose": purpose,
                    "mtime": st.st_mtime, "kb": round(st.st_size / 1024),
                    "ver": "", "days": int((time.time() - st.st_mtime) / 86400),
                    "group": "planai"})
    return out


def main():
    live = rows(HERE)
    planai = copy_plans()
    arch = rows(ARCH)
    groups = [("Testai ir scenarijai", "testai"), ("Įrankiai", "irankiai"),
              ("Planai", "planai"), ("Stendai ir prototipai", "stendai")]
    body = []
    for label, key in groups:
        items = [r for r in (live + planai) if r["group"] == key]
        if not items: continue
        body.append("<h2>%s</h2>\n<div class=\"grid\">%s</div>" %
                    (label, "\n".join(card(r) for r in items)))

    kitur = "\n".join(
        """<a class="c" href="{u}" target="_blank" rel="noopener">
  <div class="ct"><span class="cn">{n}</span><span class="pill p-ext">{tag}</span></div>
  <div class="cp">{d}</div>
  <div class="cm">{cmd}</div>
</a>""".format(u=html.escape(u), n=html.escape(n), d=html.escape(d),
               tag=("kitas portas" if "localhost" in u else
                    "www" if u.startswith("https://") else "printeris"),
               cmd=("paleisti: <code>%s</code>" % html.escape(c)) if c else "visada įjungtas")
        for n, u, d, c in kitur_all())
    body.append("<h2>Kitur</h2>\n<div class=\"grid\">%s</div>" % kitur)

    if arch:
        body.append("""<details class="arch"><summary>Archyvas ({n})</summary>
<div class="grid">{cards}</div></details>""".format(
            n=len(arch), cards="\n".join(card(r, True) for r in arch)))

    stale = [r for r in live if r["days"] > AGE_OLD]
    note = ("<div class=\"rule\">Kandidatai į archyvą (neliesti daugiau nei %d d.): <b>%s</b>. "
            "Archyvuoti = perkelti į <code>scripts/dev/archyvas/</code> ir paleisti šį skriptą "
            "iš naujo.</div>" % (AGE_OLD, ", ".join(html.escape(r["file"]) for r in stale))) if stale else ""

    page = TEMPLATE.format(body="\n\n".join(body), note=note,
                           gen=time.strftime("%Y-%m-%d %H:%M"), n=len(live) + len(planai))
    io.open(OUT, "w", encoding="utf-8", newline="").write(page)
    print("pultas: %s (%d gyvi, %d archyve)" % (OUT, len(live), len(arch)))

TEMPLATE = """<meta charset="utf-8">
<title>TinyMaker ūkis</title>
<style>
:root{{color-scheme:light;
 --bg:#f7f7f8;--card:#fff;--line:#e3e5ea;--text:#1d1f24;--muted:#6b7280;
 --accent:#f07a1a;--code:#eef0f3;
 --ok:#1f9d4d;--okbg:#e8f6ee;--warn:#b97a10;--warnbg:#fdf3e0;
 --old:#d7454f;--oldbg:#fdeaea;--mut:#6b7280;--mutbg:#eef0f3}}
*{{box-sizing:border-box}}
body{{margin:0;background:var(--bg);color:var(--text);padding:28px 16px 70px;
 font:15px/1.55 -apple-system,"Segoe UI",Roboto,sans-serif}}
.wrap{{max-width:960px;margin:0 auto}}
h1{{font-size:1.35rem;margin:0 0 4px}}
h1 .dot{{color:var(--accent)}}
.sub{{color:var(--muted);font-size:.85rem;margin:0 0 18px}}
.rule{{background:var(--card);border:1px solid var(--line);border-left:4px solid var(--accent);
 border-radius:10px;padding:11px 15px;margin:0 0 20px;font-size:.87rem}}
h2{{font-size:.76rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);
 margin:26px 0 10px;font-weight:700}}
.grid{{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:11px}}
.c{{display:block;background:var(--card);border:1px solid var(--line);border-radius:12px;
 padding:13px 15px;text-decoration:none;color:inherit;transition:border-color .12s,transform .12s}}
.c:hover{{border-color:var(--accent);transform:translateY(-1px)}}
.ct{{display:flex;align-items:baseline;gap:9px;margin-bottom:5px}}
.cn{{font-weight:700;font-size:.95rem}}
.cp{{color:var(--text);font-size:.85rem;margin-bottom:7px}}
.cm{{color:var(--muted);font-size:.76rem}}
.pill{{margin-left:auto;flex:0 0 auto;padding:2px 9px;border-radius:999px;
 font-size:.7rem;font-weight:700;white-space:nowrap}}
.p-ok{{background:var(--okbg);color:var(--ok)}}
.p-warn{{background:var(--warnbg);color:var(--warn)}}
.p-old{{background:var(--oldbg);color:var(--old)}}
.p-mut,.p-ext{{background:var(--mutbg);color:var(--mut)}}
code{{background:var(--code);padding:1px 5px;border-radius:5px;font-size:.85em}}
.arch{{margin-top:26px}}
.arch summary{{cursor:pointer;font-size:.76rem;text-transform:uppercase;letter-spacing:.08em;
 color:var(--muted);font-weight:700;margin-bottom:10px}}
.foot{{margin-top:28px;font-size:.78rem;color:var(--muted)}}
</style>
<div class="wrap">
<h1>TinyMaker <span class="dot">·</span> ūkis</h1>
<p class="sub">Viskas, kas leidžiasi iš localhost. Sugeneruota {gen} · {n} gyvi puslapiai.</p>
{note}
{body}
<p class="foot">Pultas generuojamas: <code>python scripts/dev/make_hub.py</code> (serveris:
<code>python -m http.server 8899</code> iš <code>scripts/dev/</code>). Naują puslapį įrašyk į
<code>CATALOG</code> skripto viršuje - be aprašymo jis rodomas kaip nežinomas.</p>
</div>
"""

if __name__ == "__main__":
    main()
