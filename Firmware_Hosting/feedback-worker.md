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
- Pasirinktinai: nustačius secret'ą `LIST_KEY` (`npx wrangler secret put LIST_KEY`),
  veikia `GET /list?key=<LIST_KEY>` — JSON sąrašas (100 naujausių).

## Anti-spam

1 žinutė / 30 s / IP (KV gate raktas su TTL). CORS užrakintas į
`https://tinymakerwifi.com`.
