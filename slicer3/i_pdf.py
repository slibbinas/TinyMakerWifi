# -*- coding: utf-8 -*-
"""Audito ataskaita (.md) -> PDF su ideTais paveiksleliais.

Gemini Drive integracija patikimiausiai skaito PDF, o Markdown'e paveiksleliu
neideSi. Todel kiekvienam raStui i aplanka dedam abu: .md (kad butu redaguojama
ir palyginama) ir .pdf (kad auditorius matytu vaizdus).

    python i_pdf.py 001_Cld_Gem_2026-08-15_ataskaita-1
"""
import base64
import html
import io
import os
import re
import subprocess
import sys

DRIVE = 'C:/Users/SViktoras/My Drive/Slicer'
VIEWS = 'C:/PIO-build/slicer-lab/views'
CHROME = 'C:/Program Files/Google/Chrome/Application/chrome.exe'

PAV = [('evil', 'Evil — PrusaSlicer nusėja visą paviršių smulkiais kontaktais '
                '(taškuoti raštai ant veido); mūsų pusėje paviršius švarus'),
       ('biowoman', 'Biustas — mūsų atramos stovi toliau nuo kūno (1 mm tarpas)'),
       ('kronsteinas', 'Kronšteinas — didelė plokščia nuokaba viršuje'),
       ('puodelis', 'Puodelis — staigi atbraila ties 9 mm')]
KAMPAI = [('35', 'iš priekio-kairės'), ('155', 'iš užpakalio-kairės'),
          ('275', 'iš dešinės')]


def md2html(md):
    out, in_tbl, in_code = [], False, False
    for ln in md.split('\n'):
        if ln.startswith('```'):
            in_code = not in_code
            out.append('<pre>' if in_code else '</pre>')
            continue
        if in_code:
            out.append(html.escape(ln))
            continue
        t = html.escape(ln)
        t = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', t)
        t = re.sub(r'`(.+?)`', r'<code>\1</code>', t)
        if re.match(r'^\|', ln):
            cells = [c.strip() for c in t.strip('|').split('|')]
            if all(re.match(r'^:?-+:?$', c) for c in cells):
                continue
            if not in_tbl:
                out.append('<table>')
                in_tbl = True
                out.append('<tr>' + ''.join('<th>%s</th>' % c for c in cells) + '</tr>')
            else:
                out.append('<tr>' + ''.join('<td>%s</td>' % c for c in cells) + '</tr>')
            continue
        if in_tbl:
            out.append('</table>')
            in_tbl = False
        if ln.startswith('### '):
            out.append('<h3>%s</h3>' % t[4:])
        elif ln.startswith('## '):
            out.append('<h2>%s</h2>' % t[3:])
        elif ln.startswith('# '):
            out.append('<h1>%s</h1>' % t[2:])
        elif ln.startswith('---'):
            out.append('<hr>')
        elif re.match(r'^\d+\. ', ln):
            out.append('<p class="li">%s</p>' % t)
        elif ln.startswith('- '):
            out.append('<p class="li">• %s</p>' % t[2:])
        elif ln.strip() == '':
            out.append('')
        else:
            out.append('<p>%s</p>' % t)
    if in_tbl:
        out.append('</table>')
    return '\n'.join(out)


def pav_sekcija(base=None):
    """Jei salia rasto guli `<base>_pav_1.png`, `_pav_2.png`... - imam JUOS (parasai
    skaitomi is `<base>_pav.txt`, po viena eiluteje). Kitaip - senasis rinkinys."""
    if base:
        savi = []
        for n in range(1, 20):
            f = os.path.join(DRIVE, '%s_pav_%d.png' % (base, n))
            if os.path.exists(f):
                savi.append(f)
        if savi:
            cap_f = os.path.join(DRIVE, base + '_pav.txt')
            caps = (io.open(cap_f, encoding='utf-8').read().strip().split('\n')
                    if os.path.exists(cap_f) else [])
            out = ['<h2>Priedas · Vaizdai</h2>']
            for i, f in enumerate(savi):
                b = base64.b64encode(io.open(f, 'rb').read()).decode()
                c = caps[i] if i < len(caps) else os.path.basename(f)
                out.append('<figure><img src="data:image/png;base64,%s">'
                           '<figcaption>%s</figcaption></figure>' % (b, html.escape(c)))
            return '\n'.join(out)
    # Nera savu paveiksleliu -> priedo NEDEDAM. Anksciau tokiu atveju i kiekviena
    # rasta ilipdavo senas `views/` rinkinys, kuris jau seniai nerodo tiesos.
    if base:
        return ''
    blocks = ['<h2>Priedas · Vaizdai</h2>'
              '<p>Kairėje visada PrusaSlicer, dešinėje mūsų. Renderis daromas iš '
              'tikrų sluoksnių (to paties failo, kuris eitų į spausdintuvą), ne iš '
              'modelio. Oranžinė — detalė, pilka — atramos.</p>']
    for name, cap in PAV:
        blocks.append('<h3>%s</h3><p>%s</p>' % (name.capitalize(), cap))
        for ang, txt in KAMPAI:
            f = os.path.join(VIEWS, '%s-%s.png' % (name, ang))
            if not os.path.exists(f):
                continue
            b = base64.b64encode(io.open(f, 'rb').read()).decode()
            blocks.append('<figure><img src="data:image/png;base64,%s">'
                          '<figcaption>%s, %s</figcaption></figure>' % (b, name, txt))
    return '\n'.join(blocks)


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else '001_Cld_Gem_2026-08-15_ataskaita-1'
    # Kontekstas gyvena PACIAME raste: auditoriui turi pakakti „patikrink Nr. 18",
    # be jokio atskiro prompt'o (V, 2026-08-19).
    import gem_blokas
    if base.count('_Cld_Gem_') and gem_blokas.uztikrinti(DRIVE, base):
        print('  (iraSytas standartinis blokas)')
    md = io.open(os.path.join(DRIVE, base + '.md'), encoding='utf-8').read()
    doc = """<meta charset="utf-8"><title>%s</title>
<style>
body{font:12pt/1.5 Georgia,serif;color:#111;max-width:900px;margin:0 auto;padding:24px}
h1{font-size:20pt;margin:0 0 8px} h2{font-size:15pt;margin:22px 0 6px;
  border-bottom:2px solid #111;padding-bottom:3px} h3{font-size:12.5pt;margin:16px 0 4px}
p{margin:6px 0} p.li{margin:3px 0 3px 18px}
table{border-collapse:collapse;width:100%%;font:10.5pt sans-serif;margin:8px 0}
th,td{border:1px solid #bbb;padding:4px 7px;text-align:left} th{background:#f0eee9}
pre{background:#f5f3ef;border:1px solid #ddd;padding:8px;font:10pt monospace;
  white-space:pre-wrap} code{font:10.5pt monospace;background:#f0eee9;padding:0 3px}
hr{border:0;border-top:1px solid #ccc;margin:16px 0}
figure{margin:10px 0;page-break-inside:avoid} figure img{width:100%%;border:1px solid #ccc}
figcaption{font-size:9.5pt;color:#555;margin-top:3px}
@page{margin:14mm}
</style>
%s
%s
""" % (base, md2html(md), pav_sekcija(base))
    tmp = os.path.join(os.environ.get('TEMP', '.'), base + '.html')
    io.open(tmp, 'w', encoding='utf-8').write(doc)
    pdf = os.path.join(DRIVE, base + '.pdf')
    subprocess.run([CHROME, '--headless', '--disable-gpu', '--no-pdf-header-footer',
                    '--print-to-pdf=' + pdf, 'file:///' + tmp.replace('\\', '/')],
                   capture_output=True, timeout=300)
    print('PDF:', pdf, os.path.getsize(pdf) // 1024, 'KB' if os.path.exists(pdf) else 'NEPAVYKO')


if __name__ == '__main__':
    main()
