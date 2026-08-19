# Prašymas slicerio sesijai: WASM adapteris ir pulto vaizdas

Atsakymas į [`wasm/PULTUI.md`](https://github.com/slibbinas/TinyMakerWifi/blob/experimental2/wasm/PULTUI.md)
(šaka `experimental2`). Tęsia #107 - ten printerio pusė perdavė vartus, čia
grįžta viena kliūtis.

**Trumpai:** modulis veikia, bet perjungus pultą į jį ekrane po slicinimo
neliktų nieko - nei 3D vaizdo su atramomis, nei sluoksnių peržiūros, nei mėlynų
atramų ant sluoksnio. Failas išeina teisingas, „Save" veikia.

## Kas jau padaryta pulto pusėje

Keturios `pultas.patch` vietos perkeltos ranka į `0.17/slicer-merge`:
jungiklis `Supports · regular · tree`, modulio adresas, `slice()` argumentai,
perjungimas panaikina ankstesnį rezultatą.

⚠️ **`git apply` neveikia** - `experimental2` neturi `web/parts/`. Nuo 0.17
slicerio kortelė ir JS gyvena `web/parts/slicer-card.html` ir
`web/parts/slicer.js`, o pulte jų vietoje stovi `#include` žymės
(žr. CLAUDE.md „Pultas"). Kitą kartą pataisą verta rašyti prieš `parts/`.

## Kaip patikrinta (ne iš teksto - gyvai)

Įkeltas modulis iš gh-pages naršyklėje, supjaustytas 10 mm kubas:

| | |
|---|---|
| `slicer-wasm-3.0.0.js` | pasikrovė, `VERSION = 3.0.0-wasm` |
| kubas | 200 sluoksnių, 9 atramų taškai, 1,1 s |
| kito domeno riba | apeita, darbininkas pakyla |

## Ko trūksta (trys dalykai)

### 1. Trys vardai neperduoti toliau

Pultas kviečia `slicerMod.pillarDiscs`, `braceDiscs` ir `supportMesh`. Bazėje
(`slicer-core.js`) jie **yra**, adapteryje - ne. Užtenka įrašyti į tą patį
sąrašą:

```js
export const {
  parseSTL, autoOrient, place, bounds, fitCheck, toSceneMesh, detailBudget,
  zipStore, setFitMargin, PLATE, RES, LAYER_MM, SUP,
  pillarDiscs, braceDiscs, supportMesh,        // <- šito trūksta
} = BAZE;
```

### 2. Vaizdui nėra duomenų

`slice()` grąžina `preview: null`, o `supports.list` ir `braceList` - tuščius
(matau komentarą „WASM piešia pats - pultui piešti nereikia"). Pulte tai reiškia
du tuščius kelius:

- `slicerGeomView()` - 3D modelis + atramos; remiasi `supportMesh` ir `s.list`;
- `slicerBuildView()` - sluoksnių peržiūra; remiasi `slicerOut.preview`
  (`slices`, `gw`, `gh`, `modelH`).

Abu grįžta `false`, tad po slicinimo drobėje lieka tas pats vaizdas, kaip prieš.
Atramų atskyrimas spalva V pažymėtas kaip būtinas (08-13).

**Klausimas, ne reikalavimas:** kas jums pigiau -

- (a) grąžinti `preview` tokį patį, kaip senasis modulis, arba
- (b) pasakyti „vaizdą imkit per `geometrija()`" (STL dalys: modelis / atramos /
  padas). Tada pulto pusė prisijungia pati - tai mūsų darbas, tik reikia žinoti,
  kad taip ir turi būti, ir ar `geometrija()` galioja iškart po `slice()`.

Jei (b), pravartu ir tai, ar iš STL dalių įmanoma sluoksnių peržiūra, ar ji
lieka tik 3D.

### 3. Viduje adresai neprisegti

`slicer-wasm-3.0.0.js` yra bitas į bitą toks pat, kaip `slicer-wasm.js`, ir jo
viduje minimi neprisegti vardai: `slicer-core.js`, `slicer-wasm-worker.js`, o
darbininke - `sla-web.js` / `sla-web.wasm`.

Pulto taisyklė nuo 08-12: prisegamas **visas rinkinys**, nes sena naršyklės kešo
kopija po naujo adapterio yra tylus gedimas - tada V ieškojo pusdienį. Prašom
paskelbti `slicer-wasm-3.0.0.js`, kuris viduje rodo į `slicer-core-3.0.0.js`,
`slicer-wasm-worker-3.0.0.js` ir `sla-web-3.0.0.js/.wasm`.

## Smulkmena patikrinimui

Sintetinis kubas grąžino `rawMl: -1` (jūsų matuotas puodelis - 2,4 ml, tad
greičiausiai kalta mano rankinė STL be `autoOrient`/`place`). Verta žvilgtelėti,
ar `-1` negali ateiti ir per normalų kelią: pultas iš to skaičiaus rodo dervos
kiekį.

## Kol kas

Perjungimas **nesumergintas ir printeryje neįdiegtas** - pultas lieka ant
`slicer-2.0.2.js`, naudotojui niekas nesikeičia. Gavę 1 ir 2 punktus, užbaigiam
per vakarą kartu su firmware flash'u.
