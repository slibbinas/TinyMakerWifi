# Matavimo virtuvė

Įrankiai, kuriais lyginam su PrusaSlicer. Laikomi repo, nes vieną kartą jau buvo
dingę kartu su sesijos scratchpad'u. Modeliai ir etalonai (dideli) lieka
`C:/PIO-build/slicer-lab/`.

| Įrankis | Ką daro |
|---|---|
| `ourslices.mjs <stl> <out.zip>` | mūsų sluoksniai į ZIP (per `SLICER=…` pasirenkamas modulis) |
| `measure.mjs <zip\|sl1> <stl> <aukščiai>` | n, Ø p10/p50/p90, tarpas iki detalės |
| `blobs.py <stl> <failas:pirmo_sluoksnio_storis> …` | dėmių skaičius vienodu matu keliems failams |
| `tips.py <zip\|sl1> <stl> [pirmas]` | atramų taškai iš gatavų sluoksnių |
| `isostack.mjs <zip\|sl1> <out.png> [kampas] [stl]` | izometrinis renderis iš sluoksnių |
| `montage.mjs a.png b.png out.png [antr1] [antr2]` | du renderiai greta |
| `verify.mjs <stl> [modulis]` | ar apačios remiasi, ar niekas nekerta detalės |

## ⚠️ Ką matuoklis gali sumeluoti

**Sluoksnių poslinkis.** PrusaSlicer pirmą sluoksnį daro storesnį
(`initial_layer_height = 0.3`), tad jo sluoksnis nr. k nėra mūsų nr. k — visa
krūva pastumta. Be pataisos modelio kaukė imama 0,3 mm per žemai, nesutapimo
kraštas skaičiuojamas kaip supportai, ir **etalono skaičiai išeina dvigubai
didesni** (ties z=20: 48 vietoj 24). `measure.mjs` poslinkį dabar randa pats;
`blobs.py` jam paduodamas pirmojo sluoksnio storis.

**Veidrodis.** `display_mirror_x = 1`: PrusaSlicer ir slicer3 jį taiko, mūsų JS
sliceris — ne. Abu įrankiai kryptį nustato patys, bet jei rašai naują —
nepamiršk.

**Antialiasingo kraštas.** Be bent 1 px atlaidos nuo detalės kraštelio dešimtys
pilkų pikselių virsta „atramomis" (be atlaidos: 71 dėmė vietoj 24).
