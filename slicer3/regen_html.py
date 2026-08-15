# -*- coding: utf-8 -*-
"""Persuka TIK pulto HTML (funkcijos is dashboard.html + sablonas), duomenu
neperskaiciuoja. Naudinga, kai keiciasi tik pultas.

    python regen_html.py
"""
import io
import json
import os

SRC = io.open('mk_pultas3d.py', encoding='utf-8').read()
exec(SRC.split('# ------------------------------------------------- duomenys')[0]
     if '# ------------------------------------------------- duomenys' in SRC
     else SRC.split('# ---------------------------------------------------------------- duomenys')[0])

MODELIAI = ('biustas', 'evil', 'kronsteinas', 'puodelis', 'ertme-testas',
            'kaklelis-testas')
SARASAS = {}
for v in MODELIAI:
    for kas in ('prusa', 'musu'):
        o = {}
        for z in ('d', 'p'):
            f = '%s_%s_%s.json' % (v, kas, z)
            if os.path.exists(os.path.join('C:/PIO-build/3d', f)):
                o[z] = f
        if o:
            SARASAS['%s_%s' % (v, kas)] = o

HTML = io.open('pultas_tmpl.html', encoding='utf-8').read()
h = (HTML.replace('__CODE__', code)          # noqa: F821  (is mk_pultas3d.py)
         .replace('__GPU__', gpu)            # noqa: F821
         .replace('__DATA__', json.dumps(SARASAS)))
io.open('C:/PIO-build/3d/pultas.html', 'w', encoding='utf-8').write(h)
print('pultas.html', round(len(h) / 1e6, 2), 'MB,', len(SARASAS), 'vaizdai')
