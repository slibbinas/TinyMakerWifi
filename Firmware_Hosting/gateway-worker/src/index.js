// TinyMaker Live gateway - the server half of docs/gateway-spec.md.
//
//   POST /gw/v1/claim            -> a printer trades a claim code for its key
//   POST /gw/v1/beat             -> signed status frame in, queued commands out
//   GET  /gw/p/<id>?k=<viewKey>  -> the phone page: status + pause/resume/stop
//   POST /gw/p/<id>/cmd?k=..     -> queue one command for the next beat
//   POST /gw/admin/new?key=..    -> mint a claim code (ADMIN_KEY secret)
//
// The printer is always the client. Nothing here ever connects to a printer,
// so no inbound port, no NAT traversal, no certificate on the device.
//
// Why plain HTTP with signatures instead of TLS: the ESP32 cannot afford a
// handshake mid-print (no PSRAM, and the print-time preview cache budget
// assumes no TLS). Both directions carry an HMAC-SHA256 over the same seq, so
// a forged reply cannot stop a print and a captured one cannot be replayed.
//
// Deploy: cd Firmware_Hosting/gateway-worker && npx wrangler deploy

const JSON_HEADERS = { 'Content-Type': 'application/json' };

// KV write budget. The free plan stops writing at ~1000/day, so the cadence is
// what keeps this inside it: the reply tells the printer when to come back -
// two minutes while idle (~720/day), one while printing, where the layer
// counter actually moves. Every accepted beat writes, because `seq` is the
// replay guard and a guard that is only sometimes persisted is not a guard:
// skipping the write would let the same frame through again a second later.
const NEXT_IDLE_SECS = 120;
const NEXT_PRINTING_SECS = 60;

const enc = new TextEncoder();

function json(obj, status = 200, extra = {}) {
  return new Response(JSON.stringify(obj), { status, headers: { ...JSON_HEADERS, ...extra } });
}

function bad(status, error) {
  return json({ ok: false, error }, status);
}

async function hmacHex(key, message) {
  const k = await crypto.subtle.importKey(
    'raw', enc.encode(key), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const sig = await crypto.subtle.sign('HMAC', k, enc.encode(message));
  return [...new Uint8Array(sig)].map(b => b.toString(16).padStart(2, '0')).join('');
}

// Length-independent compare so a wrong signature cannot be narrowed down by
// timing. Cheap; the frames are tiny.
function sigEquals(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string' || a.length !== b.length || !a.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function randomHex(bytes) {
  const b = new Uint8Array(bytes);
  crypto.getRandomValues(b);
  return [...b].map(x => x.toString(16).padStart(2, '0')).join('');
}

function esc(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g,
    c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function ago(ms) {
  if (!ms) return 'never';
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
  if (s < 60) return s + 's ago';
  if (s < 3600) return Math.round(s / 60) + ' min ago';
  return Math.round(s / 3600) + ' h ago';
}

function fmtDuration(secs) {
  secs = Math.max(0, Math.round(secs || 0));
  const h = Math.floor(secs / 3600), m = Math.round((secs % 3600) / 60);
  return h ? `${h} h ${m} min` : `${m} min`;
}

// ---------------------------------------------------------------- claim -----

async function handleAdminNew(request, env) {
  const url = new URL(request.url);
  if (!env.ADMIN_KEY || url.searchParams.get('key') !== env.ADMIN_KEY) return bad(404, 'not found');

  const name = (url.searchParams.get('name') || 'My printer').slice(0, 64);
  const code = randomHex(4).toUpperCase();
  await env.GATEWAY.put(`claim:${code}`, JSON.stringify({ name, createdAt: Date.now() }),
    { expirationTtl: 3600 });   // an unused code should not wait around forever
  return json({ ok: true, code, expiresInSecs: 3600 });
}

async function handleClaim(request, env) {
  let body;
  try { body = await request.json(); } catch { return bad(400, 'bad json'); }

  const code = String(body.code || '').trim().toUpperCase();
  const device = String(body.device || '').trim();
  if (!code || !device) return bad(400, 'code and device are required');

  const raw = await env.GATEWAY.get(`claim:${code}`);
  if (!raw) return bad(404, 'unknown or expired claim code');
  const claim = JSON.parse(raw);

  // One code, one printer.
  await env.GATEWAY.delete(`claim:${code}`);

  const publicId = randomHex(8);
  const deviceKey = randomHex(32);
  const viewKey = randomHex(8);
  const record = {
    publicId, deviceKey, viewKey, device,
    name: String(body.name || claim.name || 'My printer').slice(0, 64),
    claimedAt: Date.now(),
  };
  await env.GATEWAY.put(`dev:${device}`, JSON.stringify(record));
  await env.GATEWAY.put(`pub:${publicId}`, device);

  return json({ ok: true, deviceKey, publicId, viewKey, name: record.name });
}

// ----------------------------------------------------------------- beat -----

async function handleBeat(request, env) {
  const device = request.headers.get('X-TM-Device');
  const seqRaw = request.headers.get('X-TM-Seq');
  const sig = request.headers.get('X-TM-Sig');
  if (!device || !seqRaw || !sig) return bad(400, 'missing signature headers');

  const seq = Number(seqRaw);
  if (!Number.isFinite(seq) || seq < 0) return bad(400, 'bad seq');

  const raw = await env.GATEWAY.get(`dev:${device}`);
  if (!raw) return bad(409, 'unknown device');
  const dev = JSON.parse(raw);

  // Read the body as text, not json(): the signature covers the exact bytes,
  // and re-serialising a parsed object would not reproduce them.
  const body = await request.text();
  if (body.length > 2048) return bad(413, 'frame too large');

  const expect = await hmacHex(dev.deviceKey, `${seq}.${body}`);
  if (!sigEquals(expect, sig)) return bad(401, 'bad signature');

  const stateRaw = await env.GATEWAY.get(`st:${dev.publicId}`);
  const prev = stateRaw ? JSON.parse(stateRaw) : null;
  if (prev && seq <= prev.seq) return bad(401, 'replayed seq');

  let frame;
  try { frame = JSON.parse(body); } catch { return bad(400, 'bad json'); }

  // Acked commands are done; drop them before handing out what is left.
  const queueRaw = await env.GATEWAY.get(`cmd:${dev.publicId}`);
  let queue = queueRaw ? JSON.parse(queueRaw) : [];
  const acked = Array.isArray(frame.ack) ? frame.ack.map(String) : [];
  if (acked.length) {
    const before = queue.length;
    queue = queue.filter(c => !acked.includes(c.id));
    if (queue.length !== before) {
      await env.GATEWAY.put(`cmd:${dev.publicId}`, JSON.stringify(queue));
    }
  }

  const next = {
    seq,
    st: String(frame.st || '').slice(0, 40),
    by: frame.by ? 1 : 0,
    ly: Number(frame.ly) || 0,
    lt: Number(frame.lt) || 0,
    rs: Number(frame.rs) || 0,
    ml: Number(frame.ml) || 0,
    mo: String(frame.mo || '').slice(0, 80),
    fw: String(frame.fw || '').slice(0, 24),
    hp: Number(frame.hp) || 0,
    // Web control on the printer. 1 unless the printer said otherwise, so a
    // firmware too old to send the field keeps working buttons.
    wc: frame.wc === 0 || frame.wc === '0' ? 0 : 1,
    ts: Date.now(),
  };

  await env.GATEWAY.put(`st:${dev.publicId}`, JSON.stringify(next));

  const reply = JSON.stringify({
    ok: true,
    cmds: queue.slice(0, 4).map(c => ({ id: c.id, cmd: c.cmd })),
    next: next.by ? NEXT_PRINTING_SECS : NEXT_IDLE_SECS,
  });
  // The printer refuses an unsigned reply, and rightly so: this is the half
  // that can stop a running print.
  const rsig = await hmacHex(dev.deviceKey, `${seq}.${reply}`);
  return new Response(reply, { headers: { ...JSON_HEADERS, 'X-TM-RSig': rsig } });
}

// ------------------------------------------------------------ phone page ----

async function loadByPublicId(env, publicId, viewKey) {
  const device = await env.GATEWAY.get(`pub:${publicId}`);
  if (!device) return null;
  const raw = await env.GATEWAY.get(`dev:${device}`);
  if (!raw) return null;
  const dev = JSON.parse(raw);
  if (!viewKey || viewKey !== dev.viewKey) return null;
  return dev;
}

async function handleQueueCommand(request, env, publicId) {
  const url = new URL(request.url);
  const dev = await loadByPublicId(env, publicId, url.searchParams.get('k'));
  if (!dev) return bad(404, 'not found');

  const cmd = String(url.searchParams.get('cmd') || '');
  if (!['pause', 'resume', 'stop'].includes(cmd)) return bad(400, 'unknown command');

  const raw = await env.GATEWAY.get(`cmd:${publicId}`);
  const queue = raw ? JSON.parse(raw) : [];
  if (queue.length >= 8) return bad(429, 'too many pending commands');
  queue.push({ id: randomHex(3), cmd, ts: Date.now() });
  await env.GATEWAY.put(`cmd:${publicId}`, JSON.stringify(queue));

  return json({ ok: true, queued: cmd, pending: queue.length });
}

function page(dev, st, queue, viewKey) {
  const live = st && Date.now() - st.ts < 180000;
  // Buttons are offered only when the printer says it would act on them.
  // Queueing a command the printer will drop is worse than no button: it is
  // acknowledged either way, so the page would report a stop that never was.
  const remote = !st || st.wc !== 0;
  const pct = st && st.lt ? Math.round(100 * st.ly / st.lt) : 0;
  const body = `<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${esc(dev.name)}</title>
<style>
:root{--bg:#14161a;--card:#1b1e24;--line:#2a2e36;--fg:#e7e9ee;--dim:#9aa1ad;--accent:#e8720c}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,Segoe UI,Arial,sans-serif}
.wrap{max-width:560px;margin:0 auto;padding:20px 16px 40px}
h1{font-size:20px;margin:0 0 4px}
.sub{color:var(--dim);font-size:13px;margin:0 0 18px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:18px;margin-bottom:14px}
.state{font-size:26px;font-weight:700;margin:0 0 2px}
.off{color:var(--dim)}
.bar{height:10px;background:#0f1115;border-radius:6px;overflow:hidden;margin:14px 0 8px}
.bar span{display:block;height:100%;background:var(--accent)}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:12px 0 0;font-size:14px}
dt{color:var(--dim)}
dd{margin:0;text-align:right}
.btns{display:flex;gap:10px;flex-wrap:wrap}
button{flex:1;min-width:120px;padding:13px;border-radius:10px;border:1px solid var(--line);
background:#232830;color:var(--fg);font-size:15px;font-weight:600}
button.danger{border-color:#7a2b23;background:#2a1c1a;color:#ff9a8a}
button:disabled{opacity:.45}
.note{color:var(--dim);font-size:12px;margin-top:14px;line-height:1.6}
</style></head><body><div class="wrap">
<h1>${esc(dev.name)}</h1>
<p class="sub">TinyMaker Live &middot; ${live ? 'connected' : 'offline'} &middot; last beat ${esc(ago(st && st.ts))}</p>
<div class="card">
  <p class="state ${live ? '' : 'off'}">${esc(st ? st.st || 'Idle' : 'No data yet')}</p>
  ${st && st.by ? `<div class="bar"><span style="width:${pct}%"></span></div>
  <dl>
    <dt>Model</dt><dd>${esc(st.mo || '-')}</dd>
    <dt>Layer</dt><dd>${st.ly} / ${st.lt} (${pct}%)</dd>
    <dt>Remaining</dt><dd>${esc(fmtDuration(st.rs))}</dd>
    <dt>Resin used</dt><dd>${(st.ml || 0).toFixed(1)} ml</dd>
  </dl>` : '<p class="sub" style="margin:8px 0 0">Nothing printing right now.</p>'}
</div>
<div class="card">
  <div class="btns">
    <button data-cmd="pause"${remote ? '' : ' disabled'}>Pause</button>
    <button data-cmd="resume"${remote ? '' : ' disabled'}>Resume</button>
    <button data-cmd="stop" class="danger"${remote ? '' : ' disabled'}>Stop</button>
  </div>
  <p class="note" id="note">${!remote ? 'Web control is switched off on the printer, so it would ignore these. Turn it on in Settings → Network.'
    : queue.length ? `${queue.length} command(s) waiting for the next beat.`
    : 'A command is picked up on the printer’s next beat — up to a minute while printing.'}</p>
</div>
<p class="note">Starting a print remotely is deliberately not possible here: UV and motion with
nobody in the room is a decision for the printer itself.<br>This page refreshes every 30 s.</p>
</div>
<script>
const k=${JSON.stringify(viewKey)},id=${JSON.stringify(dev.publicId)};
document.querySelectorAll('button[data-cmd]:not([disabled])').forEach(b=>b.onclick=async()=>{
  const cmd=b.dataset.cmd;
  if(cmd==='stop'&&!confirm('Stop the print? This cannot be undone.'))return;
  document.querySelectorAll('button').forEach(x=>x.disabled=true);
  try{
    const r=await fetch('/gw/p/'+id+'/cmd?k='+encodeURIComponent(k)+'&cmd='+cmd,{method:'POST'});
    const j=await r.json();
    document.getElementById('note').textContent=j.ok
      ?(cmd+' queued — the printer picks it up on its next beat.')
      :('Could not queue: '+(j.error||'unknown error'));
  }catch(e){document.getElementById('note').textContent='Network error - try again.';}
  document.querySelectorAll('button[data-cmd]').forEach(x=>x.disabled=false);
});
setTimeout(()=>location.reload(),30000);
</script></body></html>`;
  return new Response(body, { headers: { 'Content-Type': 'text/html;charset=utf-8' } });
}

async function handlePage(request, env, publicId) {
  const url = new URL(request.url);
  const viewKey = url.searchParams.get('k');
  const dev = await loadByPublicId(env, publicId, viewKey);
  // A wrong key is a 404, not a 403: an unauthorised visitor should not learn
  // that this printer exists.
  if (!dev) return new Response('Not found', { status: 404 });

  const stRaw = await env.GATEWAY.get(`st:${publicId}`);
  const queueRaw = await env.GATEWAY.get(`cmd:${publicId}`);
  return page(dev, stRaw ? JSON.parse(stRaw) : null, queueRaw ? JSON.parse(queueRaw) : [], viewKey);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/^\/gw/, '') || '/';

    if (request.method === 'POST' && path === '/v1/beat')  return handleBeat(request, env);
    if (request.method === 'POST' && path === '/v1/claim') return handleClaim(request, env);
    if (request.method === 'POST' && path === '/admin/new') return handleAdminNew(request, env);

    const cmdMatch = path.match(/^\/p\/([0-9a-f]{16})\/cmd$/);
    if (request.method === 'POST' && cmdMatch) return handleQueueCommand(request, env, cmdMatch[1]);

    const pageMatch = path.match(/^\/p\/([0-9a-f]{16})$/);
    if (request.method === 'GET' && pageMatch) return handlePage(request, env, pageMatch[1]);

    return new Response('Not found', { status: 404 });
  },
};
