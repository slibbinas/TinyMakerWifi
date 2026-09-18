# Resin Lab kaip printerio modulis (plane `RL-module`, 1.1)

Šis failas - viskas, ko reikia darbui pakelti nuo nulio: kodėl, ką V nusprendė, kas jau
išmatuota, kaip turi atrodyti ir kokia eile daryti. Surašyta 2026-09-19, kai idėja kilo
ir buvo patikrinta ant V kompiuterio.

## Kodėl

Resin Lab šiandien yra programa kompiuteryje: `install.ps1` ją įdiegia į
`%USERPROFILE%\Tools\TinyMaker`, darbalaukio nuoroda paleidžia Python serverį
(`localhost:8893`). Veikia gerai, bet tik tame kompiuteryje, kur įdiegta, ir kiekvieną
pataisymą reikia įdiegti iš naujo.

V nori, kad įrankis būtų „visada su tavim kaip sliceris": atidarai printerio pultą - ir
Resin Lab ten, be jokio diegimo, visada tos versijos, kokią printeris turi.

## V sprendimai (2026-09-19)

- **Įrankis kraunamas iš printerio** kaip modulis, taip pat kaip sliceris.
- **Duomenys lieka kompiuteryje** (Google Drive `My Drive\3Dprinter\30 Dervos\Testai`), NE
  printerio kortelėje: printeris lėtas, o nuotraukos didelės. Printeris saugo tik patį
  modulį.
- **Plane - 1.1**, po 1.0.0 paleidimo darbų.

## Kas jau išmatuota (2026-09-19, V kompiuteris, Chrome 153)

Kad puslapis rašytų tiesiai į kompiuterio aplanką, reikia File System Access API
(`showDirectoryPicker`). Jis veikia **tik saugiame kontekste** (HTTPS arba localhost) -
MDN: „available only in secure contexts". Printerio pultas eina per `http://`, tad
pagal nutylėjimą jo nėra.

| Matavimas `http://192.168.1.138/` | Be nustatymo | Su nustatymu |
|---|---|---|
| `window.isSecureContext` | `false` | `true` |
| `typeof showDirectoryPicker` | `undefined` | `function` |
| Paspaudus atsidaro aplanko langas | - | taip: „Select a folder this site can view" |

Nustatymas: `chrome://flags/#unsafely-treat-insecure-origin-as-secure` → laukelyje
`http://192.168.1.138` → Enabled → Relaunch. Įjungtas V Chrome'e 2026-09-19.

Leidimo atsiminimas: Chrome nuo 122 versijos siūlo „Allow on every visit", jei puslapis
aplanko rankeną (handle) pasideda į IndexedDB ir paskui kviečia `requestPermission()`
(developer.chrome.com, „persistent permissions for the File System Access API").

**Dar nepatikrinta:**
- ar veikia `http://tinymaker.local` (nustatymas priima kelis adresus per kablelį);
- ar tikrai galima RAŠYTI (tikrinta tik aplanko pasirinkimo langas, ne įrašymas) -
  rašymui Chrome klausia atskiro leidimo („Save changes");
- ar leidimas „Allow on every visit" veikia nesaugiam adresui su šiuo nustatymu.

## Apribojimai

- Tik **Chrome ir Edge kompiuteryje**. Telefone ir Safari aplanko prieigos nėra - ten
  modulis galėtų tik rodyti (skaityti iš printerio), ne rašyti.
- Nustatymas pririštas prie **adreso**: pasikeitus printerio IP, rašymas nutrūksta.
  Sprendimas - IP rezervacija maršrutizatoriuje arba `tinymaker.local` (nepatikrinta).
- Kiekvienam kompiuteriui ir naršyklei nustatymą reikia įjungti vieną kartą ranka
  (tai naršyklės saugumo nustatymas - jo nekeičia nei įrankis, nei Claude).
- Spausdinant printeris užimtas (bendra VSPI linija SD kortelei ir ekranui) - modulio
  krovimas iš kortelės ir printerio veiksmai tada ribojami taip pat, kaip slicerio.

## Kaip turi atrodyti

```
printerio SD /lib/resinlab-X.Y.Z.*.gz  --(pultas atsisiunčia)-->  naršyklė (Chrome, PC)
                                                                   |  File System Access
                                                                   v
                                          My Drive\3Dprinter\30 Dervos\Testai\<derva>\resin.json, photos\
                                          My Drive\3Dprinter\00_TinyMaker\00 STLs Tests\ (modeliai)
naršyklė --(ta pati kilmė, be tarpininko)--> printerio API (profilis lab-test, /upload, būsena)
naršyklė --(pulto sliceris, WASM)--> SL1/ZIP be atramų --> printerio /upload
```

**Duomenų formatas lieka lygiai toks pat** kaip dabar (`resin.json`, `photos/`,
`_modeliai.json`, `_deleted/`). Tada kompiuterio įrankis ir modulis veikia su tuo pačiu
aplanku, ir pereiti galima palaipsniui.

### Kas iš `server.py` kur persikelia

| Dabar (`scripts/dev/resin-lab/server.py`) | Modulyje |
|---|---|
| `/api/lab/config`, `/api/lab/settings`, `local.json` | Aplanko rankenos IndexedDB (dervų ir modelių aplankas); printerio adreso nebereikia - tai pats pultas |
| `/api/lab/catalog` | `catalog.json` įdedamas į modulį |
| `/api/lab/resins`, `/api/lab/resin/<slug>` GET/PUT, `resin-delete` | File System Access: skaityti/rašyti `resin.json`; trynimas = perkėlimas į `_deleted/` (kaip dabar) |
| `/api/lab/photo/...`, `photo-delete`, `/data/<slug>/photos/...` | Tas pats per aplanko rankeną; rodymas per `URL.createObjectURL` |
| `/api/lab/models`, `stl_info`, `/lib/<modelis>` | Modelių aplanko rankena; `stl_info` (gabaritai, trikampiai, tilpimas 40,8×30,6×60) perrašomas į JS |
| `/api/lab/modelmap` | `_modeliai.json` per aplanko rankeną |
| `/thumb/...` (OpenSCAD) | Nebėra OpenSCAD. Peržiūra - pulto three.js (jau yra) arba be peržiūros |
| `/api/lab/slice`, `/sliced/...` (PrusaSlicer) | **Pulto sliceris** (WASM) karpo be atramų ir be pado, sluoksnis 0,05/0,10. Reikia slicerio sesijos API - žr. žemiau |
| `/api/lab/printer`, `printer/current`, `printer/run`, `printer/start`, `printer/restore`, `printer/install` | Tiesiai printerio API iš to paties puslapio. Laikino profilio `lab-test` logika (įsiminti ankstesnį, grąžinti) perrašoma į JS; printeris laiko daugiausia 16 profilių |
| `/api/lab/job` (ilgi darbai) | Puslapio `async` darbai su eigos eilute |
| `/api/lab/open` (atidaryti aplanką Explorer'yje) | Nebėra - naršyklė to negali |
| Origin sarga ir `X-TinyMaker` antraštė | Nebereikia: puslapis yra paties printerio kilmės (`requestFromOwnUi` jį praleidžia) |

Puslapiai `index.html`, `vadovas.html`, `resin-publish.html`, `resin-fields.js` lieka
beveik tokie patys; keičiasi tik `api()` sluoksnis (vietoj `fetch('/api/lab/…')` - vietinės
funkcijos su aplanko rankena).

### Firmware dalis

Printeris jau moka laikyti ir paduoti slicerio modulį (`src/Network.ino`):
- `GET /api/lib/slicer` - kokia versija kortelėje (iš failų vardų);
- `POST /api/lib/slicer/check` - parsisiunčia `lib/slicer-X.Y.Z.sha256` iš gh-pages ir
  pasako, kurių failų kortelėje trūksta;
- `POST /api/lib/slicer` - įrašo vieną failą į `/lib/` gabalais, tikrina SHA-256;
- failai paduodami iš `/lib/<vardas>.gz`.

Du keliai Resin Lab moduliui:
1. **Apibendrinti `/api/lib/slicer`** į bet kurį modulį (`/api/lib/<modulis>`), tada
   Resin Lab keliauja tuo pačiu keliu kaip sliceris: gh-pages → pultas → kortelė, veikia ir
   be interneto. Firmware pakeitimas - vartai, auditas, testas ant geležies.
2. **Be firmware pakeitimo:** pultas krauna Resin Lab tiesiai iš gh-pages (`https://` scenarijus
   `http://` puslapyje leidžiamas). Reikia interneto kiekvieną kartą. Tinka prototipui.

Siūloma eilė: pirma 2 (prototipas be firmware), tada 1 (galutinis).
Flash šiandien 76,9 % - modulis gyvena kortelėje, tad flash kaina tik apibendrinimo kodas.

### Slicerio sesijos dalis

Karpymas pereina iš PrusaSlicer į pulto slicerį, o jis yra slicerio sesijos sritis
(`web/parts/slicer*.js`, `wasm/`). Reikia iš jų: funkcijos, kuri iš STL be atramų ir be
pado, su nurodytu sluoksniu, grąžina archyvą, kurį priima printerio `/upload`. Prieš
pradedant - žinutė slicerio sesijai (pirma juodraštis V).

Pastaba: printeris iš SL1 ima tik `layerHeight` (`Import.ino` config.ini), ekspoziciją - iš
profilio, tad sukarpytas failas nuo dervos nepriklauso (tą pačią taisyklę laiko ir dabartinis
įrankis).

## Darbų eilė

| Nr | Darbas | Kas tai duoda |
|---|---|---|
| 1 | Patikrinti rašymą: iš pulto puslapio įrašyti failą į pasirinktą aplanką ir perskaityti atgal; patikrinti „Allow on every visit" ir `tinymaker.local` | Įrodo, kad kelias veikia iki galo, prieš rašant kodą |
| 2 | `api()` sluoksnis per File System Access (dervos, nuotraukos, modeliai, `_modeliai.json`) ir STL matavimas JS'e | Įrankis veikia naršyklėje be Python serverio |
| 3 | Printerio veiksmai tiesiai iš puslapio (`lab-test` profilis, paleidimas, grąžinimas, įrašymas) | Spausdinimas iš įrankio be tarpininko |
| 4 | Karpymas per pulto slicerį (su slicerio sesija) | Nebereikia PrusaSlicer |
| 5 | Prototipas: pultas krauna modulį iš gh-pages (be firmware) | V gali naudoti ir vertinti |
| 6 | Firmware: `/api/lib/<modulis>` apibendrinimas, modulis kortelėje | Veikia be interneto, kaip sliceris |
| 7 | Vadovas, apžvalga, `install.ps1` likimas (palikti kaip atsarginį ar išimti) | - |

## Patikra

- Kiekvienam žingsniui - tas pats aplankas, kurį naudoja kompiuterio įrankis: abu turi
  matyti tuos pačius įrašus (formatas nesikeičia).
- Bandymai - su dervų duomenų kopija, ne su tikru `30 Dervos\Testai` (kaip dabar:
  `RESIN_LAB_DATA` scratch aplanke).
- Printerio veiksmai - pirma su `mockprinter.py` (įgūdis `tinymaker-resin-lab`), tikras
  printeris tik V leidus.
- Firmware dalis - vartai prieš liejimą (auditas, `pio run`), liejimas tik V žodžiu „liek".

## Susiję

- Įrankio kodas: `scripts/dev/resin-lab/` (`server.py`, `index.html`, `install.ps1`)
- Slicerio modulio kelias firmware: `src/Network.ino` (`/api/lib/slicer*`)
- Įgūdis `tinymaker-resin-lab` (spąstai), atmintis `dervu-testu-irankis`
