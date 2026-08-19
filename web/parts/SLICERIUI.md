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

---

# 2 prašymas (08-19, po 3.0.1 prijungimo): modelis rastre guli kampe

3.0.1 prijungtas, kaukė ir `geometrija()` veikia - ačiū. Bet **`.sl1` rastras
išeina blogas**, ir tai matosi ne akimis, o išmatavus patį failą.

## Ką radom

Stende (tikras pultas, Ziedas.stl, 21,7 × 21,5 × 8,6 mm, `regular`) paėmėm
vidurinį sluoksnį - **ne mūsų piešinį, o patį PNG iš `slice()` grąžinto `files[]`**:

| | rasta | tikėtasi |
|---|---|---|
| rastras | 320 × 240 | 320 × 240 ✓ |
| objekto centras | **273, 194** | 160, 120 |
| ribos | 232…**319** × 157…**239** (remiasi į kraštą) | ~75…245 × ~35…205 |
| matomas plotis | ~88 px | ~170 px (7,84 px/mm) |

Kaukė (`supportSlices`) sutampa su PNG, tad pultas piešia teisingai - taip
atrodo pats variklio failas. Matomas **ketvirtis** objekto, nukirstas rastro
krašte.

## Kodėl taip, mūsų spėjimas

Skaičiai sutampa su viena prielaida: mes paduodam koordinates, kuriose **stalo
nulis yra centre** (modelis −10,8…+10,8 mm; taip grąžina `place()`, taip buvo ir
senajame modulyje), o variklis jas skaito taip, tarsi **nulis būtų stalo
kampas**. Tada į rastrą patenka tik teigiamas ketvirtis, o `display_mirror_x`
(ir y ašies kryptis) nuverčia jį į apatinį dešinį kampą:

    ketvirčio centras ≈ (7 mm, 7 mm) → (55, 55) px
    po veidrodžio       → (320−55, 240−55) = (265, 185) px     rasta: (273, 194)

Jūsų žinutėje buvo: „paduodant trikampiais variklis nieko nebecentruoja" -
panašu, kad būtent čia ir prasilenkėm: mes tikėjomės, kad koordinačių prasmė
lieka tokia pat, kaip senojo modulio.

## Ko prašom

Nuspręskit, kurioje pusėje kelti - abu variantai vienos eilutės:

- **(a) adapteryje**: prieš paduodant variklį, pridėti pusę stalo
  (`+PLATE.x/2, +PLATE.y/2`), t. y. adapteris ir toliau kalba ta pačia kalba,
  kaip senasis modulis. Mums tai patogiau: pultas paduoda `place()` rezultatą ir
  nieko nežino apie variklio vidų.
- **(b) pulte**: paduodam kampinėse koordinatėse. Padarysim, jei pasakysit, kad
  taip teisingiau - tik tada tai turi būti parašyta, nes `place()` grąžina
  centruotai ir tas pats masyvas eina į 3D vaizdą.

⚠️ Kol tai neišspręsta, **iš pulto slicinti negalima** - printeris gautų
ketvirtį modelio. Pas mus sliceris ant printerio įjungtas, tad tai skubu.

## Patikra, kai pataisysit

Mums užtenka vienos eilutės iš jūsų pusės, bet mes vis tiek pamatuosim tą patį:
vidurinio sluoksnio PNG objekto centras turi būti ~(160, 120), o plotis ~170 px
21,7 mm modeliui. Tada pultą perjungiam ir vedam per geležies vartus.

## Smulkmena, ne prašymas

`geometrija()` veikia puikiai - 3D vaizde atramos dabar tikros. Ačiū už tai, kad
dalys ateina `place()` koordinatėmis: tas kelias sutapo iš karto.
