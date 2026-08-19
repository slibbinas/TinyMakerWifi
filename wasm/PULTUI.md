# Prašymas pulto sesijai: prijungti WASM slicerį

Slicerio pusė paruošta ir išbandyta. Kad ji pasiektų naudotoją, pulte
(`web/dashboard.html`) reikia keturių smulkių pakeitimų - jie **surašyti į
[`pultas.patch`](pultas.patch)** ir čia paaiškinti žmogiškai.

Slicerio šaka pulto failų neliečia (repo sargas to neleidžia, ir teisingai:
kitaip tie patys pakeitimai atsirastų dviejose vietose), todėl paliekama kaip
prašymas.

```bash
git apply wasm/pultas.patch      # printerio šakoje
```

## Kas keičiasi (4 vietos, 24 pridėtos eilutės)

| # | Kas | Kodėl |
|---|---|---|
| 1 | **Atramų tipo jungiklis** slicerio kortelėje: `Supports · regular · tree` | V prašymas. `regular` numatytasis |
| 2 | Modulio adresas: `slicer.js` 0.9.0 → **`slicer-wasm.js` 3.0.0** | naujas variklis |
| 3 | `slice()` gauna `supportType` ir failo vardą | be to jungiklis nieko nekeistų |
| 4 | Tipo perjungimas panaikina ankstesnį rezultatą | kitaip „Save" išsaugotų tai, ko ekrane nebėra |

Pulto kodo logika **nesikeičia**: adapteris (`slicer-wasm.js`) turi tą pačią
API, kaip senasis modulis - tie patys `parseSTL`, `autoOrient`, `place`,
`bounds`, `fitCheck`, `toSceneMesh`, `detailBudget`, o `slice()` grąžina tokį
patį objektą (`blob`, `files`, `layers`, `rawMl`, `supports`).

## Ką dar reikia padaryti prieš tai

Modulis turi gulėti gh-pages šalia esamų:

```
lib/slicer-wasm.js          adapteris
lib/slicer-wasm-worker.js   darbininkas
lib/slicer-core.js          bazė (jau yra)
lib/sla-web.js              WASM įkroviklis
lib/sla-web.wasm            pats variklis (3 MB)
```

⚠️ **3 MB** - tiek naršyklė parsisiųs pirmą kartą (paskui ims iš savo talpyklos).
Per mobilų ryšį tai jaučiama; verta paskelbti su ilgu `Cache-Control`.

## Kaip patikrinta

Stende (`python scripts/dev/build_lab.py`), tikras pultas su netikru printeriu:

| | |
|---|---|
| modulis pasikrovė | `3.0.0-wasm` |
| puodelis, `regular` | 1,6 s · 300 sluoksnių · ~2,4 ml |
| puodelis, `tree` | 1,6 s · 300 sluoksnių · ~2,3 ml |
| eigos juosta | 2 → 9 → 14 → 85 → 92 → 98, atgal nešoka |
| `.sl1` prieš PrusaSlicer | puodelis −1,4 %, biustas +0,29 % |
| įkeltas į printerį | priėmė, parodė 300 sluoksnių ir 2,4 ml |

Detaliau - atmintyje `slicer-wasm-libslic3r` ir [README.md](README.md).
