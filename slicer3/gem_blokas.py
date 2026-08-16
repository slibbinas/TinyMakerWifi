# -*- coding: utf-8 -*-
"""Standartinis blokas kiekvienam raštui auditoriui.

V pastaba (2026-08-19): jam turi pakakti pasakyti „patikrink atsakymą Nr. 18",
ir jis privalo pats žinoti, kas tai, kur ieškoti ir kur dėti atsakymą. Todėl
kontekstas gyvena PAČIAME rašte, ne atskirame prompt'e.

Kviečiama iš `i_pdf.py`: jei rašte bloko dar nėra, jis įrašomas į .md failą
(idempotentiškai - antrą kartą nieko nekeičia).
"""
import io
import os
import re

ZYME = '<!-- gem-blokas -->'

VIRSUS = """%s
> **Kas tai.** Tu esi šio projekto išorinis auditorius. Čia - SLA (dervinio) 3D
> spausdintuvo **slicerio** (atramų generavimo) auditas. Raštai guli Google
> Drive aplanke **„Slicer"**, numeruoti iš eilės: `NNN_Cld_Gem_...` - nuo
> Claude tau, `NNN_Gem_Cld_...` - nuo tavęs.
>
> **Geležis, kuriai viskas daroma:** LCD 40,8 × 30,6 mm, 320 × 240 pikselių
> (vienas pikselis **0,1275 mm**), sluoksnis 0,05 mm, **kieta aliuminio**
> platforma, valdiklis ESP32 be PSRAM, sliceris sukasi naršyklėje gryname JS.
> Derva apibūdinama **klase** (kieta, trapi, nelanksti), ne prekės ženklu -
> ribos turi remtis savybėmis, kad tiktų ir kitiems vartotojams.
>
> **Ko prašom:** patikrinamų teiginių - skaičiaus, formulės ar šaltinio. Ne
> „reikėtų patikrinti", o „turi būti taip, nes X". Kiekvieną tavo teiginį
> tikrinam matavimu ir grąžinam verdiktą.
"""

APACIA = """
---

## Kaip atsakyti

1. Atsakymą įkelk į **Google Drive ŠAKNĮ** (ne į aplanką - į aplanką neturi
   teisių), pavadinimu:

       %s

2. Atsakyme, prašau, **cituok punkto numerį**, į kurį atsakai, ir pasakyk
   verdiktą aiškiai: *sutinku / nesutinku / nuomonė*. Jei nesutinki - su kuo
   konkrečiai ir kokiu pagrindu.

3. Jei kuris nors tavo ankstesnis verdiktas dėl šio rašto duomenų nebegalioja -
   parašyk tai atskirai, net jei neklausiam.
"""


def kitas_vardas(base):
    """Iš `018_Cld_Gem_2026-08-19_tema` -> `019_Gem_Cld_2026-08-19_atsakymas.md`."""
    m = re.match(r'^(\d+)_Cld_Gem_(\d{4}-\d{2}-\d{2})', base)
    if not m:
        return 'NNN_Gem_Cld_data_atsakymas.md'
    return '%03d_Gem_Cld_%s_atsakymas.md' % (int(m.group(1)) + 1, m.group(2))


def uztikrinti(drive, base):
    """Įrašo bloką į .md, jei jo dar nėra. Grąžina True, jei ką nors keitė."""
    p = os.path.join(drive, base + '.md')
    s = io.open(p, encoding='utf-8').read()
    if ZYME in s:
        return False
    eil = s.split('\n')
    # antraštė lieka pirma, blokas - iškart po jos
    i = 1 if eil and eil[0].startswith('# ') else 0
    naujas = (eil[:i] + [''] + (VIRSUS % ZYME).split('\n') +
              eil[i:] + (APACIA % kitas_vardas(base)).split('\n'))
    io.open(p, 'w', encoding='utf-8').write('\n'.join(naujas))
    return True
