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

**Pirmas žingsnis padarytas:** STL → sluoksniai → kaukės → ZIP, **be supportų**.

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
