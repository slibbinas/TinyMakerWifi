# slicer3 — Python laboratorija

Trečias, **nepriklausomas** SLA pjaustymo kelias. Nei vienos eilutės bendros su
`web/lib/slicer.js` ar `slicer2.js`; vienintelis sąlyčio taškas — **išvesties
formatas** (ZIP su `00001.png`), kad tas pats matuoklis ir tas pats piešėjas
galėtų palyginti visus tris.

## Kam jis

Ne tam, kad keliautų vartotojui. Pultas ir toliau pjausto **naršyklėje** (JS),
nes ten vartotojas įmeta STL. Python čia yra **etaloninė realizacija ir
stendas**: geometriją rašo `trimesh` ir `shapely`, ne mes.

Priežastis konkreti: visos trys 2026-08-12 taisytos `slicer2` klaidos buvo
naminėje geometrijoje (savas point-in-polygon prieš plokščią kelių sąrašą,
zondas iš medžiagos vidaus, spindulių pluoštas vietoj ašies), o ne algoritme.
Su `shapely`/`trimesh` tokios klaidos neįmanomos.

## Kaip paleisti

```
.venv/Scripts/python.exe slice_stl.py model.stl isvestis.zip
.venv/Scripts/python.exe -m unittest discover -s tests -v
```

Jungikliai: `--no-center` (nestumdyti XY), `--no-mirror` (žr. žemiau).

## Būsena

**Antras žingsnis padarytas:** supportai — visa `DefaultSupportTree` grandinė
(`add_pinheads` → `classify` → `routing_to_ground` → `routing_to_model` →
`interconnect_pillars`) + padas. Kolizijos per `trimesh`/embree paketais:
**20 000 spindulių ≈ 7 ms**, visas medis — **0,5 s** (JS versijoje 7–49 s).

### Tankio mechanizmas — tai, ko JS versija neturėjo

Tankio nevaldo pastovus žingsnis. `sample_overhangs` smulkiai diskretizuoja
nuokabos kraštą (`discretize_overhang_step` = 2 mm), o `prepare_supports_for_layer`
tada atmeta kandidatus, patenkančius į jau esančių atramų **įtakos spindulį**,
kuris AUGA kylant aukštyn (`support_curve`, SPG.cpp:1453):

| Z virš atramos | spindulys XY |
|---|---|
| 0 mm | 3,2 mm |
| 3,9 mm | 4,0 mm |
| 15 mm | 5,0 mm |
| 40 mm | 6,0 mm |

Įtaka keliauja **tik per susijusias sluoksnių dalis** (`create_near_points`),
ne per visą XY plokštumą — taikant ją globaliai kelios apatinės atramos
„uždengia" viską aukščiau ir modelis virš 10 mm negauna nieko (išmatuota:
33 taškai vietoj 98).

### Kaip atrodo prieš etaloną (biowoman)

n · Ø p50 · tarpas iki detalės p50:

| z | PrusaSlicer | JS slicer2 | slicer3 |
|---|---|---|---|
| 3 | 14 · 1,01 · 2,21 | 13 · 1,04 · 0,93 | 6 · 1,04 · 0,98 |
| 10 | 27 · 1,02 · 9,01 | 70 · 1,11 · 3,53 | 15 · 1,03 · 3,44 |
| 15 | 30 · 1,19 · 7,27 | 86 · 1,00 · 3,06 | 19 · 1,03 · 5,44 |
| 20 | 47 · 0,63 · 0,89 | 46 · 1,03 · 2,00 | 16 · 1,01 · 3,95 |
| 30 | 31 · 1,01 · 2,76 | 53 · 1,02 · 1,06 | 19 · 1,04 · 1,36 |
| 40 | 15 · 0,25 · 0,17 | 26 · 0,75 · 0,26 | 4 · 1,02 · 0,26 |

Storis pataikytas (1,03 mm per visą aukštį). Stulpų ore — 0. **Kiekis dabar
per mažas** — maždaug pusė etalono; JS klydo į kitą pusę.

### Kodėl per mažai (išmatuota, ne spėta)

98 atramos taškai → tik 52 galvutės. Priežastis geometrinė: galvutei reikia
4,20 mm laisvo kelio, o mediana yra 2,76 mm; **23 taškai iš 98 turi medžiagą
iškart po savimi** (<0,5 mm). Tiesiai žemyn telpa 37, krypties paieška ir
plonesnė galvutė atgauna iki 52, likę atmetami.

Originale tokios vietos gauna TRUMPESNĘ galvutę: `head.fullwidth()` ir `w`
skaičiavimas `connect_to_model_body` leidžia plotį iki 0 mini stulpams. Mūsų
`head_fallback_radius` mažina tik storį, ne ilgį. **Tai kitas darbas.**

Salų (`support_island`) sėja supaprastinta: krantas + centras, o ne originalo
atskiras algoritmas. 62 iš 98 taškų pažymėti kaip salos — verta patikrinti,
ar tai tiesa, ar dalių siejimo (`intersects`) trūkumas.

---

**Pirmas žingsnis (baigtas):** STL → sluoksniai → kaukės → ZIP, be supportų.

Priėmimo kriterijai — tie patys skaičiai, kuriais pasitikim JS pusėje:

| Tikrinama | Laukta | Gauta |
|---|---|---|
| kubas 10×10×10, pjūvis Z=5 | 100,000000 mm² | **100,000000** |
| tas pats su 4×4 kanalu | 84,000000 mm² | **84,000000** |
| rastro plotas prieš geometrinį | ±0,5 mm² | +0,18 / +0,05 |
| skylė lieka viena figūra su skyle | 1 kontūras, 1 skylė | taip |
| `display_mirror_x` pritaikytas | dešinė pusė | taip |

biowoman (300 000 trikampių, 1132 sluoksniai): **33 s**.

Supportai — antras žingsnis, ant jau įrodytos santechnikos.

## ⚠️ Atviras klausimas: veidrodis

Išmatuota 2026-08-12: **JS sliceris `display_mirror_x` NETAIKO, o slicer3 ir
PrusaSlicer — taiko.** Įrodymas: išjungus veidrodį slicer3 kaukės sutampa su JS
(20 546 iš 24 344 pikselių, likutis — JS supportai), įjungus — nesutampa (5 034).
Posūkis atmestas: skirtumas yra grynas X veidrodis.

Firmware priima ABU kelius — ir mūsų ZIP, ir PrusaSlicer SL1. Abu vienu metu
teisingi būti negali, nebent firmware juos piešia skirtingai. **Kuris teisus —
neišspręsta; tai firmware sesijos klausimas.** Patikra: atspausdinti aiškiai
nesimetrišką smulkmeną (raidę „F") iš abiejų kelių ir pažiūrėti.

Kol neaišku, slicer3 laikosi profilio (veidrodis įjungtas), o `--no-mirror`
leidžia lyginti su JS.

## Priklausomybės

`trimesh` · `shapely` · `numpy` · `pillow` · `scipy` + `networkx` (trimesh
grafams) · `rtree` (poligonų medis) · `mapbox_earcut` (trianguliacija testuose).
Python 3.14, `slicer3/.venv`.
