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

## Anti-spam

1 žinutė / 30 s / IP (KV gate raktas su TTL). CORS užrakintas į
`https://tinymakerwifi.com`.
