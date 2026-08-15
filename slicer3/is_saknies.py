# -*- coding: utf-8 -*-
"""Gemini failus is Google Drive SAKNIES perkelia i musu `Slicer` aplanka.

Gemini i konkretu aplanka rasyti NEGALI - visada padeda i saknį (V, 2026-08-15).
Tad tvarkome mes: sis skriptas suranda `*_Gem_Cld_*` failus saknyje, perkelia
i `Slicer/` ir, jei tai Google Docs nuoroda (.gdoc), atspausdina `doc_id` -
turini tada pasiimu per Drive jungti ir issaugau salia kaip .md.

    python is_saknies.py            # parodo, ka darytu
    python is_saknies.py --daryk    # perkelia
"""
import io
import json
import os
import shutil
import sys

SAKNIS = 'C:/Users/SViktoras/My Drive'
APLANKAS = os.path.join(SAKNIS, 'Slicer')
DARYK = '--daryk' in sys.argv


def doc_id(path):
    """.gdoc yra tik nuoroda i Google Docs - viduje JSON su `doc_id`."""
    try:
        return json.load(io.open(path, encoding='utf-8')).get('doc_id')
    except Exception:
        return None


rasta = 0
for f in sorted(os.listdir(SAKNIS)):
    p = os.path.join(SAKNIS, f)
    if not os.path.isfile(p) or '_Gem_Cld_' not in f:
        continue
    rasta += 1
    d = doc_id(p) if f.endswith('.gdoc') else None
    print('%-52s %s' % (f, ('doc_id ' + d) if d else ''))
    if DARYK:
        tikslas = os.path.join(APLANKAS, f)
        if os.path.exists(tikslas):
            print('   jau yra aplanke - praleidziam')
            continue
        shutil.move(p, tikslas)
        print('   -> Slicer/')

if not rasta:
    print('saknyje nauju Gemini failu nera')
elif not DARYK:
    print('\n(bandomasis paleidimas; perkelti - `python is_saknies.py --daryk`)')
