# libslic3r SLA naršyklėje (WebAssembly)

Čia gyvena receptas, kaip **tikrą PrusaSlicer atramų ir rafto variklį**
sukompiliuoti į WebAssembly ir paleisti naršyklėje - be serverio, be diegimo,
viskas vartotojo kompiuteryje.

Kodėl to reikia: mūsų pačių rašomas JS portas (`web/lib/sla/`) buvo įstrigęs
ties dviem dalykais - salų sėja (reikalauja Voronoi, ~8000 eilučių) ir greičiu
(biustas 285 s). Sukompiliavus originalą abu klausimai atkrenta savaime.

## Kaip paleisti

```bash
bash wasm/build.sh          # viskas nuo nulio (pirmą kartą ~40 min)
bash wasm/build.sh link     # tik perlinkuoti, kai pakeistas bridge.cpp
node /c/PIO-build/wasm-build/sla.js modelis.stl 0.05
```

Reikia `git`, `python` su `pip` ir `bash`. Visa kita (Emscripten, cmake, ninja,
PrusaSlicer šaltiniai, Eigen, CGAL, NLopt, qhull) skriptas parsisiunčia pats.

## Kas čia yra

| failas | kam |
|---|---|
| `build.sh` | visas receptas nuo tuščios mašinos iki `sla.wasm` |
| `bridge.cpp` | tiltas: kviečia SLA grandinę ta pačia tvarka kaip `SLAPrint`, su mūsų profilio parametrais |
| `sources.txt` | kurie `libslic3r` failai reikalingi SLA keliui (FDM dalis atmesta) |
| `shims/` | pakaitalai bibliotekoms, kurių naršyklėje nereikia (žr. žemiau) |

## Pakaitalai (`shims/`) - ką jie keičia ir kodėl tai saugu

Visi jie liečia **aplinką, ne algoritmą**. Nė vienas nekeičia to, kaip
skaičiuojamos atramos.

| pakaitalas | kodėl |
|---|---|
| `oneapi/tbb/*`, `tbb/*` | Intel TBB = lygiagretumas. WASM naršyklėje sukasi viena gija, tad `parallel_for` virsta paprastu ciklu - lygiai tuo, ką daro paties libslic3r `ExecutionSeq`. Rezultatas tas pats, tik deterministiškesnis |
| `boost/log/*` | žurnalo rašymas. Tai kompiliuojama boost dalis, kurios Emscripten portas neturi; eilutės kode lieka, tik niekur nekeliauja |
| `boost/thread*` | tik užraktams - pakeista `std` atitikmenimis |
| `boost/beast/core.hpp` | libslic3r jį traukia TIK dėl base64; pilnas beast tempia `boost::asio`, kuriam Emscripten nėra nei Windows, nei POSIX |
| `LibBGCode/*`, `core/*` | binarinis G-code - FDM dalykas. Tipai tikri pagal formą, skaitymas visada grąžina klaidą, tad tas kelias niekada nepasileidžia |

## Patikrinta prieš etaloną (2026-08-19)

Tie patys keturi modeliai, tas pats profilis (`slicer-lab/prusa-full.ini`),
sluoksnis 0,05 mm:

| modelis | PrusaSlicer CLI | WASM | skirtumas | WASM laikas |
|---|---|---|---|---|
| puodelis | 2,4249 ml | 2,4081 ml | −0,7 % | 0,3 s |
| kronšteinas | 1,2209 ml | 1,2280 ml | +0,6 % | 0,05 s |
| evil (490k trikampių) | 16,2504 ml | 16,4300 ml | +1,1 % | 16,0 s |
| biustas (300k trikampių) | 18,3998 ml | 18,5751 ml | +1,0 % | 23,6 s |

Likutinis skirtumas paaiškinamas: PrusaSlicer dervą skaičiuoja iš rastrizuotų
sluoksnių (pikselių pustonai), `bridge.cpp` - tiesiai iš geometrijos.

Modulis: **3,0 MB**.

## Atramų tipas: `regular` ir `tree`

Tiltas priima trečią argumentą - kokio tipo atramas statyti:

```bash
node sla.js modelis.stl 0.05            # regular (numatytasis)
node sla.js modelis.stl 0.05 tree       # medžio (branching)
```

Tai ne tas pats algoritmas su kitu vardu: PrusaSlicer kiekvienam tipui turi
**atskirą parametrų rinkinį** (`support_*` ir `branchingsupport_*`), ir abu
paimti iš to paties profilio. Medžio variante galvutė plonesnė (0,4 vs 0,5 mm),
tiltas trumpesnis (5 vs 10 mm), stulpas platėjantis, jungimas dinaminis.

Puodelis, tas pats profilis:

| | PrusaSlicer | mūsų WASM |
|---|---|---|
| regular | 2,4249 ml | 2,4081 ml |
| tree | 2,2629 ml | 2,2995 ml |

**Ištirta (2026-08-19): tuščias padas su `tree` nėra klaida.**

`remove_redundant_parts` (Pad.cpp:307-316) palieka tik tas pado dalis, po
kuriomis yra atrama. Medžio atramos dažnai remiasi į patį modelį ir plokštės
nesiekia - tada pado po jomis ir nereikia.

| modelis | `regular` padas | `tree` padas |
|---|---|---|
| puodelis | 54,3 mm³ | 0 (atramos prasideda 1,44 mm) |
| biustas | 61,4 mm³ | 36,1 mm³ |

PrusaSlicer tam pačiam puodeliui elgiasi vienodai: su `tree` jo pirmi sluoksniai
15212 → 15400 → 15544 px (tik modelio kontūras), su `regular` - 37428 px.

## Ką reikia turėti galvoje

1. **CGAL apvalinimo tikrinimas išjungtas** (`-DCGAL_DISABLE_ROUNDING_MATH_CHECK`).
   WASM neturi apvalinimo režimo valdymo, o CGAL intervalų aritmetika juo
   remiasi. Keturiuose modeliuose rezultatas sutapo, bet tai neįrodo, kad taip
   bus visada - jei salų sėja kada nors duos keistą rezultatą, tikrinti pirma čia.
2. **Vienagijis.** Gijos naršyklėje reikalautų `SharedArrayBuffer` ir COOP/COEP
   antraščių, kurių gh-pages nustatyti negalima. Aukščiau esantis greitis - be jų.
3. **Licencija.** `libslic3r` yra AGPL-3.0-or-later, tad ir šis modulis yra
   išvestinis kūrinys su ta pačia licencija. `bridge.cpp` - taip pat. Likusi
   projekto dalis (firmware, pultas) lieka MIT ir yra atskirai.
