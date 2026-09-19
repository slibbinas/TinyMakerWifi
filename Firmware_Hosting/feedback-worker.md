# Feedback collector — DEPLOYED

Feedback forma (`tinymakerwifi.com/feedback/`, šaltinis `docs/feedback/`)
POST'ina į **atskirą** worker'į:

- **Worker:** `tinymaker-feedback` → https://tinymaker-feedback.slibbinas.workers.dev
- **Šaltinis:** [feedback-worker/](feedback-worker/) šiame repo (`wrangler.jsonc` + `src/index.js`)
- **KV:** namespace `tinymaker-feedback` (binding `FEEDBACK`)
- **Deploy:** `cd Firmware_Hosting/feedback-worker && npx wrangler deploy`
  (wrangler OAuth — `npx wrangler login`)
- Įdiegta 2026-07-15 (versija a72a1317). `tinymaker-stats` worker'is NELIESTAS —
  feedback gyvena atskirai, kad stats/ping niekada nenukentėtų nuo formos.

## Puslapiai ir KV panel'ai (tas pats worker'is servina daugiau nei feedback)

Tas pats `tinymaker-feedback` worker'is servina ir statiškus puslapius iš KV
raktų (`panel:*`). HTML įkeliamas iš PC su `wrangler`, NE iš repo.

| Route | KV raktas | Prieiga | Įkėlimas |
|---|---|---|---|
| `/tests` | `panel:tests` | viešas | `wrangler kv key put panel:tests --path <failas.html>` |
| `/plan` | `panel:plan` | secret **`LIST_KEY`** per `?key=`; blogas → **404** (nematomas) | `wrangler kv key put panel:plan --path planas.html` |
| `/team` | `panel:team` | KV raktas **`key:team`** per `?key=` (NE LIST_KEY); blogas → **404** | `wrangler kv key put panel:team --path <failas.html>` |

Proxy į gh-pages (be KV): `/demo`, `/manual`, `/roadmap`.

### `/tests/state` — testų pulto žymos serveryje (2026-08-27)

Pultas lieka viešas ir be rakto veikia kaip anksčiau (žymos tik toje naršyklėje).
Su asmeniniu raktu nuorodoje (`/tests?k=...`) žymos keliauja į serverių, tad
telefonas prie printerio ir kompiuteris ant stalo rodo tą pačią būseną, o
naršyklės išvalymas nebenužudo testavimo sesijos.

| | |
|---|---|
| Raktas prieigai | KV `key:tests` (kaip `key:team`); blogas ar joks → **404** |
| Būsena | KV `tests:state`, vienas JSON: `{"T-19":{"v":"pass","n":"...","t":<ms>}}` |
| `GET /tests/state?k=` | grąžina būseną |
| `POST /tests/state?k=` | prisiunčia **pilnuą to įrenginio kopiją**, grąžina sulietą būseną |

**Susiliejimas pagal eilutės laiką, ne pagal atsiuntimo eilę.** Abu įrenginiai
siunčia pilnas kopijas, tad vakar paliktas atidarytas langas kitaip nutrintų
tai, kas ka tik pažymėta telefone. Taisyklė gyvena atskirai — `src/state.mjs`
`mergeState()` — ir yra **unit-testuota**:

```
cd Firmware_Hosting/feedback-worker
node --test test/state.test.mjs
```

⚠️ Windows'e `node --test test/` (katalogas) nesuveikia — `MODULE_NOT_FOUND`;
paduok patį failą, kaip aukščiau.


**SEO / AI-discovery route'ai** (inline turinys `src/index.js`, ne KV — apex neturi
CNAME, tad worker juos servina pats): `/robots.txt`, `/sitemap.xml`, `/llms.txt`.
Turinį keisti tiesiai `index.js` + `wrangler deploy`.

**Gating pastaba:** `LIST_KEY` = Worker secret (`wrangler secret put LIST_KEY`);
`key:team` = KV raktas (vienintelis toks — `wrangler kv key put key:team <secret>`).
Blogas/nesamas raktas šiuose route'uose grąžina 404 (nematomą), ne 403.

**Maintainer feedback route'ai** (visi po `LIST_KEY`): `/feedback/inbox`,
`/feedback/csv`, `/feedback/list`, `/feedback/img`, `POST /feedback/mark`,
`POST /feedback/del`. Vieši: `/feedback/status?t=<token>`, `/feedback/recent`.

**Scope:** tas pats worker'is hostina ir atskirą **eInkWeather** projektą
(`/orai` ← `panel:orai`, `/oi/<code>` ← `oi:<code>` PNG'ai) — ne TinyMaker, čia
nedokumentuojama.

Pilnas route'ų/KV šaltinis: `feedback-worker/src/index.js`.

## Skaitymas

- CF dashboard → Storage & databases → Workers KV → `tinymaker-feedback`
  (raktai `fb:<ISO data>:<id>`, rikiuojasi chronologiškai), arba
- `npx wrangler kv key list --namespace-id 4eb7904e55fc490e854f85f631e6c35e`
- **`https://tinymakerwifi.com/feedback/list?key=<LIST_KEY>`** — 100 naujausių
  atsiliepimų JSON su `photoUrls` nuorodomis į nuotraukas. `LIST_KEY` secret'as
  NUSTATYTAS 2026-07-15 (reikšmę žino vartotojas; CF secret'ų atgal neparodo —
  pamiršus paleisti `npx wrangler secret put LIST_KEY` iš naujo). Blogas/nesamas
  raktas grąžina paprastą tekstą, ne duomenis.
- Nuotraukos: `GET /feedback/img?key=<LIST_KEY>&k=img:...` (tas pats vartas).

## Turnstile — įjungimas (kai prireiks; kodas jau paruoštas)

Worker'is Turnstile tikrina TIK jei nustatyti abu raktai — kitaip praleidžia.
Įjungti be jokio kodo pakeitimo:

1. CF dashboard → **Turnstile** → Add widget: vardas `tinymaker-feedback`,
   domenas `tinymakerwifi.com`, režimas **Managed**.
2. Gausi **Site Key** (viešas) ir **Secret Key**.
3. Iš `Firmware_Hosting/feedback-worker`:
   ```
   npx.cmd wrangler secret put TURNSTILE_SITEKEY   → įklijuoji Site Key
   npx.cmd wrangler secret put TURNSTILE_SECRET    → įklijuoji Secret Key
   ```
4. Viskas. Widget'as įsiterpia į formą pats (`<!--turnstile-->` vietoje), o
   POST pradeda tikrinti token'ą. Išjungti — `wrangler secret delete`.

## Anti-spam

- **60 s vartai / IP** (burst) + **paros lubos: 5 / IP, 60 iš viso**
  (skaitikliai `day:<data>[:<ip>]`, TTL 48 h).
- Kodėl lubos: KV nemokamas planas nustoja rašyti ties ~1000/parą, o vienas
  įrašas kainuoja ~3 rašymus (+1 už nuotrauką). Be lubų vienas žmogus po
  įrašą per minutę biudžetą sudegintų per ~7 val., ir **tikri atsiliepimai
  imtų tyliai nebeįsirašyti**. Su lubomis blogiausias atvejis ~500 rašymų.
- Atmestos užklausos tik SKAITO — todėl daužymas kainuoja skaitymus
  (100k/parą), ne brangius rašymus.
- CORS užrakintas į `https://tinymakerwifi.com`; forma ir POST — tas pats
  origin'as, tad CORS praktiškai net nedalyvauja.
- Testuojant lubas atmintinai išvalyti `day:*` ir `gate:*` raktus — kitaip
  paliksi savo paties IP užrakintą parai.

## `POST /slicerbug` — defektų žymės iš pulto (0.17)

Pulte (su `?dev=1`) galima bakstelėti į modelį 3D peržiūroje ir pažymėti, kur
blogai; „Send" atsiunčia žymes su milimetrais, slicerio versija, pulto ETag ir
firmware build'u, plius 3D kadrą kaip nuotrauką.

**Kodėl atskiras kelias, o ne `/feedback`:** pultas servinamas iš printerio per
`http://tinymaker.local` (arba LAN IP), tad viešos formos CORS
(`tinymakerwifi.com`) jį atmestų, o Turnstile widget'o puslapyje, kuris gali
neturėti interneto, nėra iš kur gauti. Viešos formos apsaugos **nekeičiamos** —
tai kitos durys su savo spyna.

| | |
|---|---|
| CORS | `*` **tik šiam keliui** (`WIDE_CORS` konstanta) |
| Turnstile | nėra |
| Limitai | 60 s / IP (`sbgate:<ip>`) · 20 per parą (`sbday:<data>`) · plius bendros 60/parą lubos |
| Kūnas | multipart arba JSON: `message` (≤8000), `fw`, `build`, `ua`, viena `photo` (≤2 MB, `image/*`) |
| Įrašas | ta pati `fb:` dėžutė, `src:'slicer'`, `tag:'bug'` iš karto — inbox'e matosi su ⌖ ženkleliu, „kas naujo feedback'e" pagauna savaime |

Kritimas be tinklo lieka pulte: „Copy" (iškarpinė) ir „JSON" (atsisiuntimas).
