# Feedback collector — tinymaker-stats worker papildymas

Feedback forma (gh-pages `/feedback/`, rodoma per tinymakerwifi.com/feedback/)
POST'ina į `https://tinymaker-stats.slibbinas.workers.dev/feedback`. Kad tai
veiktų, į **tinymaker-stats** worker'į (CF dashboard → Workers → tinymaker-stats)
reikia įdėti `/feedback` route ir pririšti KV.

## 1. KV namespace

Workers & Pages → KV → **Create namespace**: `tinymaker-feedback`.
Worker'io Settings → Bindings → **KV Namespace**: binding name `FEEDBACK`
→ pasirinkti `tinymaker-feedback`.

## 2. Kodas — įterpti į fetch handler'į PRIEŠ esamą /ping logiką

```js
// --- feedback collector ---------------------------------------------------
const FEEDBACK_ORIGIN = 'https://tinymakerwifi.com';
if (url.pathname === '/feedback') {
  const cors = {
    'Access-Control-Allow-Origin': FEEDBACK_ORIGIN,
    'Access-Control-Allow-Methods': 'POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
  };
  if (request.method === 'OPTIONS') return new Response(null, { headers: cors });
  if (request.method !== 'POST')
    return new Response('POST only', { status: 405, headers: cors });
  let body;
  try { body = await request.json(); } catch (e) {
    return new Response('bad json', { status: 400, headers: cors });
  }
  const msg = String(body.message || '').slice(0, 4000).trim();
  if (!msg) return new Response('empty', { status: 400, headers: cors });
  // 1 žinutė / 30 s / IP - paprastas anti-spam be state
  const ip = request.headers.get('CF-Connecting-IP') || 'x';
  const gate = await env.FEEDBACK.get('gate:' + ip);
  if (gate) return new Response('slow down', { status: 429, headers: cors });
  await env.FEEDBACK.put('gate:' + ip, '1', { expirationTtl: 30 });
  const key = 'fb:' + new Date().toISOString() + ':' + crypto.randomUUID().slice(0, 8);
  await env.FEEDBACK.put(key, JSON.stringify({
    message: msg,
    contact: String(body.contact || '').slice(0, 120),
    fw: String(body.fw || '').slice(0, 20),
    build: String(body.build || '').slice(0, 20),
    ua: String(body.ua || '').slice(0, 120),
    at: new Date().toISOString(),
  }));
  return new Response('{"ok":true}', {
    headers: { 'Content-Type': 'application/json', ...cors },
  });
}
// --- /feedback -------------------------------------------------------------
```

Pastaba: jei worker'is senos sintaksės (`addEventListener('fetch', ...)`),
`env.FEEDBACK` keisti į globalų `FEEDBACK`.

## 3. Skaitymas

Įrašai — KV narštyklėje (dashboard → KV → tinymaker-feedback), raktai
rikiuojasi chronologiškai (`fb:2026-07-15T...`). Norint sąrašo per URL,
galima pridėti (nebūtina):

```js
if (url.pathname === '/feedback/list' && url.searchParams.get('key') === 'PASIKEISK-SLAPTA') {
  const list = await env.FEEDBACK.list({ prefix: 'fb:', limit: 100 });
  const out = [];
  for (const k of list.keys) out.push(JSON.parse(await env.FEEDBACK.get(k.name)));
  return new Response(JSON.stringify(out, null, 1), { headers: { 'Content-Type': 'application/json' } });
}
```

Kol worker'io dalis neįdėta, forma rodo mandagų fallback'ą
(GitHub Issues + FB grupė) — niekas nelūžta.
