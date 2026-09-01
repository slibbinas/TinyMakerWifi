# Testiniai modeliai

Trys mažyčiai spausdinimo darbai, skirti **firmware eigai tikrinti**, o ne detalėms
gaminti: startui, pauzei, stabdymui, pabaigai, skaitikliams. Su jais nereikia leisti
50 min Ziedo tam, kad pamatytum, kaip elgiasi vienas pranešimas.

| Modelis | Sluoksnių | Printerio spėjimas | Realiai |
|---|---|---|---|
| `TestasMini.zip` | 4 | 1m 38s | **~2m 20s** |
| `ShortTest.zip` | 9 | 3m 03s | **4m 20s** (išmatuota) |
| `TestasLong.zip` | 28 | 8m 26s | ~10 min |

⚠️ **Printerio spėjimas nuosekliai per mažas.** Trumpiems modeliams paklaida didžiausia
(+42%), nes juose dominuoja ilgos bazinių sluoksnių ekspozicijos; ilgesniems ji mažėja
(Ziedui, 173 sluoksniai, buvo +14%). Planuojant laiką imti su atsarga.

## Iš ko jie padaryti

Tai **tikri Ziedo apatiniai sluoksniai**, nuskaityti iš paties printerio:

```
GET /api/files/layer?name=Ziedas&i=<N>&source=1
```

Tas endpoint'as atiduoda sluoksnio PNG tokį, koks jis guli SD kortelėje - **320×240**,
nes tokia ir yra šio spausdintuvo kaukės ekrano skiriamoji geba. Todėl formato spėlioti
nereikėjo: jis toks pat, kokį duoda sliceris. Spausdinasi žiedo apačia, plokščias
žiedelis 0,2-1,4 mm aukščio.

## Kaip įkelti atgal

Vienas failas, viena komanda (tas pats kelias, kuriuo siunčia PrusaSlicer - pulto kilmės
jam nereikia):

```bash
curl -F "file=@ShortTest.zip" -F "source=test" http://<printerio-ip>/upload
```

Atsakymas `{"ok":true,"queued":true,"name":"ShortTest"}` reiškia, kad archyvas priimtas ir
išpakuojamas. Kelis modelius kelti **po vieną**: kol vyksta išpakavimas, printeris atsako
`{"error":"printer busy"}`.

## Ko čia nėra ir kodėl

**Peržiūros paveikslėlio.** Išpakuotojas iš archyvo ima tik sluoksnius (skaičius prieš
`.png`, žr. `src/Import.ino`), o `POST /api/files/model/preview` per `curl` atmeta
(`preview upload failed`). Todėl sąraše modelis iš pradžių būna be piktogramos - ji
atsiranda pati, kai modelį kartą atidarai pulte.

Kilmė: 2026-09-01, V prašymu, kad kiekvienam bandymui nereikėtų ilgo spaudinio.
