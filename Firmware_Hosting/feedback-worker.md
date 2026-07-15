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
