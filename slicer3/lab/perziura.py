# -*- coding: utf-8 -*-
"""3D perziuros pultas: PrusaSlicer ir musu atramos salia, vizualiai patikrinti
pries spausdinant.

    python perziura.py
"""
import base64
import io
import os

LAB = 'C:/PIO-build/slicer-lab'
SRC = [d for d in ('peržiūra', 'perziura') if os.path.isdir(os.path.join(LAB, d))]
SRC = os.path.join(LAB, SRC[0]) if SRC else os.path.join(LAB, 'perziura')
OUT = os.path.join(LAB, 'perziura.html')

MODELS = [
    ('biowoman', 'Biustas', '55 mm, organinis, daug smulkiu nuokabu',
     'Salu 2 pries 1 · danga 1,28 pries 1,18 mm · zymiu <b>275 pries 318</b> · '
     'dervos 1060 pries 981 mm3 · pridetas pagalbinis stulpas vienisam'),
    ('evil', 'Evil', 'figurele su iskysusiais elementais',
     'Salu <b>0</b> · danga <b>0,90 pries 0,93</b> mm · zymiu 142 pries 148 · '
     'dervos <b>613</b> pries 588 mm3 · pagalbinis stulpas pridetas'),
    ('kronsteinas', 'Kronsteinas', 'techninis, didele plokscia nuokaba virsuje',
     'Salu 0 · danga <b>2,91 = tiek pat</b> · dervos <b>393 pries 988 mm3</b> · '
     'zymiu 15 pries 6'),
    ('puodelis', 'Puodelis', 'staigi atbraila ties 9 mm',
     'Salu 0 · danga 2,97 pries 2,48 mm · zymiu <b>16 pries 29</b> · '
     'dervos <b>189</b> pries 151 mm3 (raftas suplonintas)'),
]


def img(name):
    p = os.path.join(SRC, name + '.png')
    if not os.path.exists(p):
        return '<p class="small">(renderis nerastas: %s)</p>' % name
    b = base64.b64encode(io.open(p, 'rb').read()).decode()
    return ('<img loading="lazy" src="data:image/png;base64,%s" '
            'style="width:100%%;border-radius:8px;border:1px solid var(--line)">' % b)


cards = []
for key, title, what, nums in MODELS:
    cards.append(
        '<div class="card"><h3>%s <span class="small">%s</span></h3>%s'
        '<p class="small nums">%s</p></div>' % (title, what, img(key), nums))

HTML = """<meta charset="utf-8">
<title>Atramų peržiūra</title>
<style>
:root{--bg:#faf9f7;--fg:#1a1a1a;--muted:#6b6b6b;--line:#e2e0dc;--card:#fff;--accent:#c85a1e}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){
  --bg:#17181a;--fg:#e8e6e3;--muted:#9a9a9a;--line:#2e3033;--card:#1e2023;--accent:#e08a4f}}
:root[data-theme=dark]{--bg:#17181a;--fg:#e8e6e3;--muted:#9a9a9a;--line:#2e3033;
  --card:#1e2023;--accent:#e08a4f}
body{background:var(--bg);color:var(--fg);margin:0;padding:24px;
  font:15px/1.55 -apple-system,Segoe UI,Roboto,sans-serif}
.wrap{max-width:1180px;margin:0 auto}
h1{font-size:21px;margin:0 0 6px}
h3{font-size:15px;margin:0 0 10px;display:flex;gap:10px;align-items:baseline}
p.lede{color:var(--muted);margin:0 0 4px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;
      padding:14px 16px;margin-top:16px}
.small{color:var(--muted);font-size:12.5px;font-weight:400}
.nums{margin:10px 0 0}
ul{margin:8px 0 0;padding-left:20px}
li{margin:4px 0}
</style>
<div class="wrap">
<h1>Atramos iš arti: PrusaSlicer ir mūsų</h1>
<p class="lede">Tas pats modelis, ta pati orientacija, tas pats piešimo įrankis —
skiriasi tik algoritmas. Kairėje visada PrusaSlicer, dešinėje mūsų.</p>

<div class="card" style="margin-top:14px">
<b>Į ką verta žiūrėti</b>
<ul>
<li><b>Ar kas nors nekabo.</b> Atrama, prasidedanti ore, — brokas. Skaičiais tai
    jau tikrinta (visur nulis), bet akis pagauna tai, ko matas nemato.</li>
<li><b>Kur atramos liečia detalę.</b> Ar jos nesikabina ten, kur bus matoma
    plokštuma; ar pasiekia įdubas.</li>
<li><b>Narvo pobūdis.</b> Jo atramos eina arčiau kūno ir įsipina; mūsų stovi
    toliau ir sudaro išorinį narvą — tai tavo 1 mm prasilenkimo tarpo pasekmė.</li>
<li><b>Tankis.</b> Kur mūsų per tiršta ar per reta lyginant su juo.</li>
</ul>
</div>

%s

<div class="card"><b>Pastaba apie spalvas</b>
<p class="small" style="margin-top:6px">Oranžinė — detalė, pilka — atramos.
Kronšteino kairėje (PrusaSlicer) spalvos vietomis susikeitusios: jo failas
centruotas ant plokštės, o modelio kaukė imama iš žalio STL, tad jos nesutampa.
Tai piešėjo, ne slicer'io dalykas — geometrija abiejose pusėse teisinga.
Dešinė (mūsų) pusė visur nuspalvinta teisingai.</p></div>

<p class="small" style="margin-top:20px">Renderis daromas iš tikrų sluoksnių
(to paties ZIP/SL1, kuris eitų į spausdintuvą), ne iš modelio — tad matai būtent
tai, kas užsipoliarizuos dervoje. Šaltinis: <code>slicer-lab/perziura.py</code>.</p>
</div>
""" % '\n'.join(cards)

io.open(OUT, 'w', encoding='utf-8').write(HTML)
print('irasyta', OUT, len(HTML), 'baitu')
