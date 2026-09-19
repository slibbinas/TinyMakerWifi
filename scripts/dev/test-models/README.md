# Testiniai modeliai

Trys mažyčiai spausdinimo darbai, skirti **firmware eigai tikrinti**, o ne detalėms
gaminti: startui, pauzei, stabdymui, pabaigai, skaitikliams. Su jais nereikia leisti
50 min spaudinio tam, kad pamatytum, kaip elgiasi vienas pranešimas.

**Vardas sako trukmę** - tiek jis realiai ir užtrunka, tad rinkiesi ne spėliodamas:

| Modelis | Sluoksnių | Realiai | Printerio spėjimas |
|---|---|---|---|
| `Test2min.zip` | 4 | ~2 min 20 s | 1m 38s |
| `Test4min.zip` | 9 | **4 min 20 s** (išmatuota) | 3m 03s |
| `Test10min.zip` | 28 | ~10 min | 8m 26s |

⚠️ **Printerio spėjimas nuosekliai per mažas.** Trumpiems modeliams paklaida didžiausia
(+42%), nes juose dominuoja ilgos bazinių sluoksnių ekspozicijos; ilgesniems ji mažėja
(173 sluoksnių Ziedui buvo +14%). Vardai remiasi tikra trukme, ne tuo spėjimu.

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
curl -F "file=@Test4min.zip" -F "source=test" http://<printerio-ip>/upload
```

Atsakymas `{"ok":true,"queued":true,"name":"Test4min"}` reiškia, kad archyvas priimtas ir
išpakuojamas. Kelis modelius kelti **po vieną**: kol vyksta išpakavimas, printeris atsako
`{"error":"printer busy"}`.

## Ko čia nėra ir kodėl

**Peržiūros paveikslėlio.** Išpakuotojas iš archyvo ima tik sluoksnius (skaičius prieš
`.png`, žr. `src/Import.ino`), o `POST /api/files/model/preview` per `curl` atmeta
(`preview upload failed`). Todėl sąraše modelis iš pradžių būna be piktogramos - ji
atsiranda pati, kai modelį kartą atidarai pulte.

Kilmė: 2026-09-01, V prašymu, kad kiekvienam bandymui nereikėtų ilgo spaudinio.
Pirmieji vardai buvo `ShortTest`, `TestasLong` ir `TestasMini`; pakeisti tą patį vakarą,
nes iš jų nesimatė, kuris kiek trunka.
