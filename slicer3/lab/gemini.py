# -*- coding: utf-8 -*-
"""Dokumentas isoriniam modeliui (Gemini) — musu SLA supportu algoritmo perziura.

Sudeda i viena savarankiska HTML: tikslas, kriterijai, dabartiniai skaiciai,
renderiai, algoritmo etapai, apribojimai, neissprestos vietos ir gatavas promptas.
Is naršyklės spausdinant (Ctrl+P) gaunasi PDF.

    python gemini.py
"""
import base64
import io
import os

LAB = 'C:/PIO-build/slicer-lab'
SRC = [d for d in ('peržiūra', 'perziura') if os.path.isdir(os.path.join(LAB, d))]
SRC = os.path.join(LAB, SRC[0]) if SRC else os.path.join(LAB, 'perziura')
OUT = os.path.join(LAB, 'gemini-brief.html')


def img(name, cap):
    p = os.path.join(SRC, name + '.png')
    if not os.path.exists(p):
        return ''
    b = base64.b64encode(io.open(p, 'rb').read()).decode()
    return ('<figure><img src="data:image/png;base64,%s"><figcaption>%s</figcaption>'
            '</figure>' % (b, cap))


PROMPT = """Esi SLA/MSLA 3D spausdinimo ir skaičiavimo geometrijos ekspertas.
Žemiau — mano atramų (support) generavimo algoritmo aprašymas, kriterijai,
išmatuoti rezultatai ir palyginimas su PrusaSlicer 2.9.6 (etalonas). Prašau:

1. Nurodyk KONKREČIAS klaidas ar spragas algoritme — ne bendrus patarimus.
   Kiekvienai: kur būtent, kodėl tai klaida, ir kaip pasireikš spaudinyje.
2. Įvertink, ar mano fiziniai kriterijai (2 skyrius) yra teisingi ir pilni.
   Ko trūksta? Kuris kriterijus matuoja ne tai, ką galvoju?
3. Trys neišspręstos vietos surašytos 6 skyriuje — pasiūlyk sprendimą
   kiekvienai, su formule ar konkrečia taisykle, ne su „reikėtų patikrinti".
4. Ar mano sprendimas remti retai (PrusaSlicer deda daug smulkių kontaktų ant
   paviršiaus, aš beveik nededu) yra fiziškai pagrįstas 0,05 mm sluoksniui ir
   kietai dervai? Kokia riba, nuo kurios nuokaba tikrai nukars?
5. Jei matai, kad kuri nors mano išvada klaidinga — pasakyk tiesiai.

Atsakyk lietuviškai, konkrečiai, su skaičiais."""

TXT = {}

TXT['ka_darom'] = """
<p>Rašau <b>atramų generatorių MSLA (dervos) spausdintuvui</b>. Jis veikia
naršyklėje (JavaScript) ir siunčia sluoksnių PNG rinkinį į savo gamybos
spausdintuvą su ESP32 valdikliu. Etalonas, su kuriuo lyginuosi —
<b>PrusaSlicer 2.9.6</b> (jo <code>libslic3r</code> SLA modulis), nes jo
rezultatai realiai spausdinasi.</p>
<p>Iki šiol algoritmą rašiau pagal PrusaSlicer šaltinius (etapas po etapo), bet
tikslas ne kopija, o <b>geras spaudinys</b>: kuo mažiau atramų, kad tik detalė
pavyktų ir gražiai atsiluptų.</p>
"""

TXT['kriterijai'] = """
<table>
<tr><th>#</th><th>Kriterijus</th><th>Kaip matuoju</th><th>Riba</th></tr>
<tr><td colspan="4" class="grp">1 grupė — privaloma (bet kuris ne nulis = rizika)</td></tr>
<tr><td>1</td><td>Nė viena atrama neprasideda ore</td><td>spindulys žemyn nuo stulpo apačios</td><td>0</td></tr>
<tr><td>2</td><td>Atramos nekerta detalės</td><td>pluošto ir tinklo sankirta</td><td>0</td></tr>
<tr><td>3</td><td>Kiekviena „sala" paremta</td><td>sluoksnio dėmė, po kuria nieko nėra</td><td>0</td></tr>
<tr><td>4</td><td>Danga</td><td>blogiausias atstumas nuo naujo ploto iki to, kas jį laiko</td><td>≤ 3 mm</td></tr>
<tr><td colspan="4" class="grp">2 grupė — kad išsilaikytų ir atsiluptų</td></tr>
<tr><td>5</td><td>Kaklelis</td><td>sluoksnio plotas / persidengimas su apačia</td><td>kaip etalono</td></tr>
<tr><td>6</td><td>Kontakto skersmuo</td><td>konfigas</td><td>0,4 mm</td></tr>
<tr><td>7</td><td>Tarpas iki detalės</td><td>konfigas (kad tilptų replės)</td><td>≥ 1,0 mm</td></tr>
<tr><td>8</td><td>Vienišo stulpo aukštis</td><td>stulpas be jungčių</td><td>≤ 15 mm</td></tr>
<tr><td>9</td><td>Sukibimas su platforma</td><td>atramų plotas ant plokštės</td><td>≥ 260 mm²</td></tr>
<tr><td colspan="4" class="grp">3 grupė — minimizuojam</td></tr>
<tr><td>10</td><td>Dervos atramoms</td><td>mm³ ir % nuo modelio</td><td>kuo mažiau</td></tr>
<tr><td>11</td><td>Žymių kiekis</td><td>kontaktų su modeliu</td><td>kuo mažiau</td></tr>
</table>
<p class="small">Sprendimai, kuriuos priėmiau pats: kontaktas ⌀0,4 (derva kieta ir
tvirta — plonesnis antgalis laiko, o žymė mažesnė); tarpas 1 mm (kad tilptų
replės); dangos riba 3 mm.</p>
"""

TXT['skaiciai'] = """
<table>
<tr><th>Modelis</th><th></th><th>Salų</th><th>Danga mm</th><th>Kaklelis</th>
    <th>Plokštė mm²</th><th>Derva mm³</th><th>Žymių</th></tr>
<tr><td rowspan="2">Biustas 55 mm</td><td>PrusaSlicer</td><td>1</td><td>1,18</td><td>2,8</td><td>264</td><td>981</td><td>318</td></tr>
<tr><td><b>Mano</b></td><td>2</td><td>1,28</td><td>2,7</td><td>281</td><td>1060</td><td>275</td></tr>
<tr><td rowspan="2">Evil (figūrėlė)</td><td>PrusaSlicer</td><td>0</td><td>0,93</td><td>1,0</td><td>284</td><td>588</td><td>148</td></tr>
<tr><td><b>Mano</b></td><td>0</td><td>0,90</td><td>1,0</td><td>307</td><td>613</td><td>143</td></tr>
<tr><td rowspan="2">Kronšteinas</td><td>PrusaSlicer</td><td>0</td><td>2,91</td><td>3,2</td><td>353</td><td>988</td><td>6</td></tr>
<tr><td><b>Mano</b></td><td>0</td><td>2,91</td><td>3,2</td><td>351</td><td>393</td><td>15</td></tr>
<tr><td rowspan="2">Puodelis</td><td>PrusaSlicer</td><td>0</td><td>2,48</td><td>2,9</td><td>347</td><td>151</td><td>29</td></tr>
<tr><td><b>Mano</b></td><td>0</td><td>2,97</td><td>2,9</td><td>318</td><td>189</td><td>16</td></tr>
</table>
<p class="small">Matuojama vienodai abiem: modelio pjūvis abiem imamas iš to
paties STL, tad skirtumas rodo algoritmą, ne matavimą. Danga ir kaklelis —
<b>blogiausias</b> viso spaudinio atvejis, ne vidurkis.</p>
"""

TXT['algoritmas'] = """
<ol>
<li><b>Sluoksniavimas.</b> Modelis pjaustomas kas 0,05 mm; sluoksnis — Clipper
    daugiakampiai. Ekranas 40,8 × 30,6 mm, 320 × 240 px (0,1275 mm/px).</li>
<li><b>Nuokabos.</b> Kiekvienam sluoksniui: naujas plotas = šis sluoksnis MINUS
    po juo esantys. Ruožai, siauresni nei sluoksnio postūmis prie 45°
    (0,05 mm), laikomi savilaikiais ir metami.</li>
<li><b>Taškų sėja.</b> Nuokabos kontūras einamas kas 2 mm; praleidžiamos
    atkarpos, sutampančios su apatiniu sluoksniu. Atskirai: <i>salos</i> (dalis
    be nieko po ja) ir <i>pusiasaliai</i> (išsikiša &gt;2 mm už apatinio) —
    jiems sėjamas ir vidus: taškas dedamas ten, kur iki artimiausios atramos ar
    jau sukietintos medžiagos toliausiai, kol niekur nelieka &gt;3 mm.</li>
<li><b>Tankio filtras.</b> Taškas praleidžiamas, jei patenka į jau esančio
    taško „įtakos spindulį". Spindulys AUGA su aukščio skirtumu pagal kreivę
    (3,2 mm prie 0 → 4,0 prie 3,9 → 5,0 prie 15 → 6,0 prie 40 mm).</li>
<li><b>Galvutės.</b> Kryptis — iš tinklo normalės taške (artimiausio trikampio;
    ant briaunos vidurkinami du). Polius prisotinamas iki 45° žemyn. Jei
    galvutė netelpa, ieškoma kitos krypties; nepavykus — plonesnė (⌀0,6 → 0,4
    kotas).</li>
<li><b>Klasifikacija.</b> Spindulys žemyn nuo galvutės jungties: laisva → stulpas
    į plokštę; kliūva → jungiama prie kaimyninio stulpo, kelio į plokštę arba
    inkaruojama į patį modelį (apversta galvutė).</li>
<li><b>Stulpų jungimas.</b> Kaskada: kiekvienas stulpas jungiasi su artimiausiais,
    kol turi 3 jungtis; pora jungiama vieną kartą; zigzagas 45° laipteliais.
    Riba tarp stulpų — 10 mm XY.</li>
<li><b>Pagalbiniai stulpai.</b> Vienišam aukštam (&gt;15 mm be jungties) šalia
    statomas naujas ir sujungiamas, taip pat tiltu per viršų.</li>
<li><b>Nupjovimas.</b> Joks stulpas nekyla aukščiau savo aukščiausio sujungimo.</li>
<li><b>Padas.</b> Žiedas aplink detalę su 1 mm tarpu, 0,15 mm storio; detalė
    pirmu sluoksniu lipa tiesiai prie plokštės.</li>
</ol>
"""

TXT['apribojimai'] = """
<ul>
<li><b>Spausdintuvas:</b> MSLA, LCD 40,8 × 30,6 mm, 320 × 240 px. Sluoksnis
    0,05 mm. Valdiklis ESP32 (4 MB flash, be PSRAM) — jis tik rodo ir
    spausdina, skaičiavimas vyksta naršyklėje.</li>
<li><b>Derva:</b> SUNLU, kieta ir tvirta (ne lanksti).</li>
<li><b>Skaičiavimas:</b> JavaScript naršyklėje, be WASM ir be gijų. Vienam
    modeliui (~100 tūkst. trikampių, ~1000 sluoksnių) turi užtrukti sekundes,
    ne minutes. Naudoju Clipper (2D), savo AABB tinklelį spinduliams.</li>
<li><b>Geometrija:</b> STL gali būti su prasta trianguliacija; sluoksnių kontūrai
    su aukščiu truputį „sukasi", tad tikslių viršūnių sutapimo tikrinti negaliu —
    naudoju toleranciją.</li>
<li><b>Ko negaliu:</b> keisti spausdintuvo, sluoksnio storio, dervos; naudoti
    sunkių bibliotekų (CGAL, OpenVDB).</li>
</ul>
"""

TXT['problemos'] = """
<ol>
<li><b>Retas paviršiaus rėmimas.</b> PrusaSlicer ant organinių modelių deda
    daugybę smulkių kontaktų tiesiai ant paviršiaus (žr. „Evil" paveikslėlį —
    taškuoti raštai ant veido). Aš jų beveik nededu: mano dangos taisyklė (3 mm)
    tokių vietų „nemato", nes jos formaliai laikosi pačios. Nežinau, ar tai
    privalumas (mažiau žymių) ar rizika (nukars smulkios klostės).</li>
<li><b>Perteklinės atramos prie pagrindo (svarbiausia).</b> Ties „Evil" pagrindu
    turiu daug daugiau atramų nei etalonas, ir dalis jų — trumpi 1,35 mm kelmeliai,
    kurie baigiasi ties z≈3,5:
    <table><tr><th>aukštis</th><th>z=2</th><th>z=3</th><th>z=4</th><th>z=5</th><th>z=8</th></tr>
    <tr><td>PrusaSlicer</td><td>13</td><td>15</td><td>16</td><td>19</td><td>14</td></tr>
    <tr><td><b>mano</b></td><td>22</td><td>29</td><td>20</td><td>24</td><td>19</td></tr></table>
    Tarp z=3 ir z=4 mano skaičius krenta 29 → 20, t. y. ~9 atramos ten baigiasi;
    PrusaSlicer'io eina 15 → 16, nesibaigia nė viena. Išmatavau tą nuokabą:
    naujo ploto juosta ten yra <b>ne platesnė nei 0,128 mm</b> (tai vieno
    pikselio riba mano rastre, tikroji dar siauresnė) — t. y. beveik vertikali
    siena. Mano savilaikio filtras meta tik tai, kas siauriau nei
    <code>sluoksnis / tan(45°) = 0,05 mm</code>, tad tokia juosta prasprūsta.
    <b>Klausimas: kokia teisinga riba?</b> Ar Prusa čia naudoja `diff_ex` su
    `ApplySafetyOffset`, ar visai kitą kriterijų?</li>
<li><b>Derva.</b> Biustui sunaudoju 1060 mm³ prieš etalono 981, puodeliui 189
    prieš 151 — nors žymių visur turiu mažiau. Vadinasi mano atramos ilgesnės
    arba storesnės, o ne tankesnės. Iš dalies tai mano paties 1 mm tarpo kaina
    (išmatuota: +7 % dervos), bet ne visa.</li>
</ol>
"""


HTML = """<meta charset="utf-8">
<title>SLA atramų algoritmo peržiūra</title>
<style>
body{background:#fff;color:#111;margin:0;padding:32px;
  font:15px/1.6 Georgia,'Times New Roman',serif;max-width:900px;margin:0 auto}
h1{font-size:25px;margin:0 0 6px} h2{font-size:19px;margin:32px 0 8px;
  border-bottom:2px solid #111;padding-bottom:4px}
h3{font-size:16px;margin:20px 0 6px}
p.lede{color:#555;margin:0 0 8px;font-style:italic}
table{border-collapse:collapse;width:100%;font-size:13.5px;margin:10px 0;
  font-family:-apple-system,Segoe UI,sans-serif}
th,td{border:1px solid #ccc;padding:5px 8px;text-align:left}
th{background:#f2f0ec;font-weight:600}
td.grp,.grp{background:#f7f5f2;font-weight:600}
figure{margin:14px 0}
figure img{width:100%;border:1px solid #ccc}
figcaption{font-size:12.5px;color:#555;margin-top:4px}
.small{font-size:13px;color:#555}
ol,ul{padding-left:22px} li{margin:5px 0}
pre.prompt{background:#f7f5f2;border:1px solid #ccc;border-left:4px solid #c85a1e;
  padding:14px;white-space:pre-wrap;font:13.5px/1.55 ui-monospace,Consolas,monospace}
code{font:13px ui-monospace,Consolas,monospace;background:#f2f0ec;padding:1px 4px}
@media print{body{padding:0} h2{page-break-after:avoid} figure{page-break-inside:avoid}}
</style>

<h1>SLA atramų generavimo algoritmas — peržiūrai</h1>
<p class="lede">Prašau rasti konkrečias klaidas. Promptas — dokumento gale.</p>

<h2>1. Ką darau</h2>
{{KA_DAROM}}

<h2>2. Ko siekiu (kriterijai)</h2>
{{KRITERIJAI}}

<h2>3. Ką gaunu dabar</h2>
{{SKAICIAI}}

<h2>4. Kaip atrodo (kairėje PrusaSlicer, dešinėje mano)</h2>
{{PAV}}

<h2>5. Algoritmo etapai</h2>
{{ALGORITMAS}}

<h2>6. Kas nepavyksta (čia labiausiai reikia pagalbos)</h2>
{{PROBLEMOS}}

<h2>7. Techniniai ir loginiai apribojimai</h2>
{{APRIBOJIMAI}}

<h2>8. Promptas</h2>
<pre class="prompt">{{PROMPT}}</pre>
"""
for _k, _v in list(TXT.items()):
    HTML = HTML.replace('{{%s}}' % _k.upper(), _v)
VIEWS = os.path.join(LAB, 'views')


def views(name, cap):
    out = ['<h3>%s</h3><p class="small">%s</p>' % (name.capitalize(), cap)]
    for ang, txt in (('35', 'is priekio-kaires'), ('155', 'is uzpakalio-kaires'),
                     ('275', 'is desines')):
        f = os.path.join(VIEWS, '%s-%s.png' % (name, ang))
        if not os.path.exists(f):
            continue
        b = base64.b64encode(io.open(f, 'rb').read()).decode()
        out.append('<figure><img src="data:image/png;base64,%s">'
                   '<figcaption>%s, %s (kaireje PrusaSlicer, desineje mano)</figcaption>'
                   '</figure>' % (b, name, txt))
    return chr(10).join(out)


HTML = HTML.replace('{{PAV}}', chr(10).join([
    views('evil', 'Kaireje matyti, kaip PrusaSlicer nuseja visa pavirsiu smulkiais '
                  'kontaktais (taskuoti rastai ant veido ir kuno); mano puseje '
                  'pavirsius svarus, remiama tik is isores.'),
    views('biowoman', 'Mano atramos stovi toliau nuo kuno (1 mm tarpas) ir sudaro '
                      'isorini narva; jo — arciau ir ispina.'),
    views('kronsteinas', 'Didele plokscia nuokaba virsuje. Dervos sunaudoju 2,5 karto '
                         'maziau prie tos pacios dangos. Kaireje spalvos vietomis '
                         'susikeitusios — piesejo, ne slicerio dalykas.'),
    views('puodelis', 'Staigi atbraila ties 9 mm. Rezultatai beveik sutampa.')]))
HTML = HTML.replace('{{PROMPT}}', PROMPT)

io.open(OUT, 'w', encoding='utf-8').write(HTML)
print('irasyta', OUT, len(HTML), 'baitu')
