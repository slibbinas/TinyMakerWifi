# -*- coding: utf-8 -*-
"""Is `log/history.json` padaro `/log` puslapi: balu eiga, lenteles ir vaizdai.

Puslapis savarankiskas (paveiksleliai — data URI), tad ji galima ir issiusti, ir
atsidaryti per serveri: http://localhost:8080/log/
"""
import base64
import io
import json
import os

LAB = 'C:/PIO-build/slicer-lab'
HIST = os.path.join(LAB, 'log', 'history.json')
OUT = os.path.join(LAB, 'log', 'index.html')
MODELS = ['biowoman', 'evil', 'kronsteinas', 'puodelis']


def img(tag, name, w=520):
    p = os.path.join(LAB, 'log', 'img', tag, name + '.png')
    if not os.path.exists(p):
        return ''
    b = base64.b64encode(io.open(p, 'rb').read()).decode()
    return ('<img loading="lazy" style="width:%dpx;max-width:100%%;border-radius:6px;'
            'border:1px solid var(--line)" src="data:image/png;base64,%s">' % (w, b))


def bar(v, best=100.0):
    w = max(0, min(100, v))
    return ('<span class="bar"><span style="width:%.0f%%"></span></span>'
            '<b>%.1f</b>' % (w, v))


hist = json.load(io.open(HIST, encoding='utf-8'))
rows = []
# Delta skaiciuojam nuo paskutines ISLAIKYTOS busenos: A/B bandymai (isjungtas
# gabalas, kad matytusi ka jis dave) nera eigos zingsniai ir eiles neveda.
last_kept = None
for i, e in enumerate(hist):
    exp = e.get('kind') == 'bandymas'
    prev = last_kept
    d = '' if prev is None else ('%+.1f' % (e['score'] - prev))
    cls = '' if not d else ('up' if e['score'] >= prev else 'down')
    if exp:
        cls = 'exp'
        if d:
            d = '(%s)' % d
    else:
        last_kept = e['score']
    per = ' · '.join('%s <b>%.0f</b>' % (m, e['models'][m]['score'])
                     for m in MODELS if m in e['models'])
    g = e.get('geometry', {})
    ok = '✓' if (g.get('air') == 0 and g.get('crossing') == 0) else '⚠'
    tag = e['tag'] if not exp else         '%s <span class="chip">bandymas</span>' % e['tag']
    rows.append(
        '<tr class="%s"><td>%d</td><td class="small">%s</td><td><code>%s</code></td>'
        '<td>%s</td><td>%s</td><td class="%s">%s</td>'
        '<td class="small">%s</td><td>%s</td></tr>'
        % ('dim' if exp else '', i + 1, e.get('when', '—'), e['commit'], tag,
           bar(e['score']), cls, d, per, ok))

last = hist[-1]
detail = []
for m in MODELS:
    if m not in last['models']:
        continue
    d = last['models'][m]
    hdr = ''.join('<th>%s</th>' % z for z in d['z'])
    ref = ''.join('<td>%d</td>' % v for v in d['ref'])
    our = ''.join('<td class="%s">%d</td>'
                  % ('' if abs(o - r) <= max(1, r * 0.15) else 'off', o)
                  for o, r in zip(d['ours'], d['ref']))
    detail.append(
        '<div class="card"><h3>%s <span class="score">%.0f</span></h3>'
        '<table class="mini"><tr><th>z mm</th>%s</tr>'
        '<tr><td>PrusaSlicer</td>%s</tr><tr><td>mūsų</td>%s</tr></table>%s</div>'
        % (m, d['score'], hdr, ref, our, img(last['tag'], m)))

spark = ' '.join('%.0f' % e['score'] for e in hist)

HTML = """<meta charset="utf-8">
<title>slicer · atitikimo žurnalas</title>
<style>
:root{--bg:#faf9f7;--fg:#1a1a1a;--muted:#6b6b6b;--line:#e2e0dc;--card:#fff;
      --ok:#2f7d32;--bad:#b3261e;--accent:#c85a1e}
:root:not([data-theme=light]) @media (prefers-color-scheme:dark){}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){
  --bg:#17181a;--fg:#e8e6e3;--muted:#9a9a9a;--line:#2e3033;--card:#1e2023;
  --ok:#6fbf73;--bad:#e5786d;--accent:#e08a4f}}
:root[data-theme=dark]{--bg:#17181a;--fg:#e8e6e3;--muted:#9a9a9a;--line:#2e3033;
  --card:#1e2023;--ok:#6fbf73;--bad:#e5786d;--accent:#e08a4f}
body{background:var(--bg);color:var(--fg);font:15px/1.55 -apple-system,Segoe UI,Roboto,sans-serif;
     margin:0;padding:28px}
.wrap{max-width:1100px;margin:0 auto}
h1{font-size:22px;margin:0 0 4px} h2{font-size:17px;margin:28px 0 10px}
h3{font-size:15px;margin:0 0 8px;display:flex;justify-content:space-between;align-items:center}
p.lede{color:var(--muted);margin:0 0 18px}
table{border-collapse:collapse;width:100%;font-size:14px}
th,td{text-align:left;padding:7px 10px;border-bottom:1px solid var(--line)}
th{color:var(--muted);font-weight:600;font-size:12px;text-transform:uppercase;letter-spacing:.04em}
code{font:12px ui-monospace,Consolas,monospace;color:var(--muted)}
.bar{display:inline-block;width:90px;height:7px;border-radius:4px;background:var(--line);
     margin-right:8px;vertical-align:middle;overflow:hidden}
.bar span{display:block;height:100%;background:var(--accent)}
.up{color:var(--ok);font-weight:600} .down{color:var(--bad);font-weight:600}
.exp{color:var(--muted)} tr.dim{opacity:.72}
.chip{border:1px solid var(--line);border-radius:20px;padding:0 7px;font-size:11px;
      color:var(--muted);margin-left:6px;white-space:nowrap}
.small{color:var(--muted);font-size:12.5px}
.cards{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(520px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px}
.score{background:var(--accent);color:#fff;border-radius:20px;padding:1px 10px;font-size:12px}
table.mini{width:auto;margin-bottom:12px} table.mini td,table.mini th{padding:4px 9px}
td.off{color:var(--bad);font-weight:600}
.note{color:var(--muted);font-size:13px;margin-top:26px;border-top:1px solid var(--line);padding-top:14px}
.scroll{overflow-x:auto}
</style>
<div class="wrap">
<h1>Kiek mūsų sliceris sutampa su PrusaSlicer</h1>
<p class="lede">Balas: kiekvienam modeliui ir aukščiui imamas santykinis nuokrypis
nuo etalono, iš jo — vidurkis. 100 reikštų sutapimą aukštis į aukštį.
Geometrija (stulpų ore, kertančių detalę) tikrinama atskirai ir privalo būti nulis —
kitaip balas neturi prasmės.</p>

<h2>Eiga · {{SPARK}}</h2>
<div class="scroll"><table>
<tr><th>#</th><th>kada</th><th>commit</th><th>žymė</th><th>balas</th><th>Δ</th><th>pagal modelį</th><th>geom.</th></tr>
{{ROWS}}
</table></div>

<h2>Paskutinė būsena · {{TAG}} <span class="small">({{WHEN}})</span></h2>
<div class="cards">{{DETAIL}}</div>

<p class="note"><b>bandymas</b> — eilutė, kur gabalas buvo laikinai išjungtas, kad
matytųsi, ką jis duoda; toks matavimas eilės neveda, todėl jo Δ skliaustuose ir
skaičiuojamas nuo paskutinės išlaikytos būsenos.
Raudonai pažymėti aukščiai, kur nuokrypis didesnis nei 15 %.
Skaičius — supportų dėmių sluoksnyje tuo aukščiu; matuojama vienodai abiem
failams, su veidrodžio ir sluoksnio poslinkio pataisa. Šaltinis:
<code>slicer-lab/score.py</code> → <code>log/history.json</code>.</p>
</div>
"""
for _k, _v in (('{{ROWS}}', '\n'.join(rows)),
               ('{{DETAIL}}', '\n'.join(detail)),
               ('{{TAG}}', last['tag']),
               ('{{WHEN}}', last.get('when', '—')),
               ('{{SPARK}}', spark)):
    HTML = HTML.replace(_k, _v)

io.open(OUT, 'w', encoding='utf-8').write(HTML)
print('irasyta', OUT, len(HTML), 'baitu ·', len(hist), 'irasai')
