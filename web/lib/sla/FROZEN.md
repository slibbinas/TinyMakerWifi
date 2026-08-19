# ❄️ UŽŠALDYTA 2026-08-19 - čia nebedirbama

Šis katalogas yra PrusaSlicer SLA algoritmo **portas į JavaScript** (26
commit'ai). **V sprendimas 2026-08-19: jis metamas.**

## Kodėl

Tą pačią dieną paaiškėjo, kad `libslic3r` **sukompiliuojamas į WebAssembly**
(receptas - [`wasm/`](../../../wasm/)). Sukompiliuotas originalas:

- duoda tą patį, ką desktop PrusaSlicer - **sutapimas per 1 %** keturiuose modeliuose;
- yra **12× greitesnis** už šį portą (biustas 23 s vietoj 285 s);
- turi **tikrą salų sėją** (`SupportIslands` + Voronoi), kurios šiame porte nėra
  ir kuri būtų kainavusi dar ~8000 eilučių.

Rašyti ranka tai, ką galima sukompiliuoti, nebeturi prasmės.

## Ką tai reiškia praktiškai

- **Jokių pataisų čia.** Jei kas nors „neveikia" - atsakymas yra WASM, ne pataisa.
- **Neportuoti Voronoi**, `igl::AABB` medžio ar likusių `SupportIslands`.
- Katalogas dar neištrintas tik todėl, kad WASM dar nepasileido naršyklėje
  (pirmieji iš trijų vartų). Praėjus jiems - trinamas.

## Kas iš jo lieka vertingo

Trys šios dienos radiniai buvo tikri nukrypimai nuo originalo, ir jie surašyti
commit'uose `d71e092` ir `0628733` - lanko tolerancija apvaliam ofsetui,
gardelės langelis ir kampinis jungimas pjūvių užvėrime. Jie svarbūs ne kaip
kodas, o kaip **metodo pavyzdys**: kiekvienas rastas profiliu, ne spėjimu, ir
kiekvienas apvertė prielaidą.

Detalės - atmintyje `slicer-porto-testas-08-19` ir `slicer-wasm-libslic3r`.
