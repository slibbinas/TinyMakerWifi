// End-to-end check of the parts that decide whether a print keeps running:
// the signature both ways, the replay guard, and the command queue.
//
// Run: node test/beat.test.mjs   (no dependencies, no wrangler needed)

import worker from '../src/index.js';
import assert from 'node:assert';

const enc = new TextEncoder();
async function hmacHex(key, msg) {
  const k = await crypto.subtle.importKey('raw', enc.encode(key),
    { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const s = await crypto.subtle.sign('HMAC', k, enc.encode(msg));
  return [...new Uint8Array(s)].map(b => b.toString(16).padStart(2, '0')).join('');
}

// Minimal KV stand-in: get/put/delete over a Map is all the worker uses.
function fakeKv() {
  const m = new Map();
  return {
    m,
    async get(k) { return m.has(k) ? m.get(k) : null; },
    async put(k, v) { m.set(k, v); },
    async delete(k) { m.delete(k); },
  };
}

const env = { GATEWAY: fakeKv(), ADMIN_KEY: 'test-admin' };
const call = (url, init) => worker.fetch(new Request('http://x' + url, init), env);

let passed = 0;
async function check(name, fn) {
  try { await fn(); passed++; console.log('  ok   ' + name); }
  catch (e) { console.error('  FAIL ' + name + '\n       ' + e.message); process.exitCode = 1; }
}

// --- pairing ---------------------------------------------------------------
const codeRes = await call('/gw/admin/new?key=test-admin&name=Bench', { method: 'POST' });
const { code } = await codeRes.json();

const claimRes = await call('/gw/v1/claim', {
  method: 'POST', body: JSON.stringify({ code, device: 'AABBCCDDEEFF', name: 'Bench' }),
});
const claim = await claimRes.json();
const { deviceKey, publicId, viewKey } = claim;

await check('claim returns a key, id and view key', () => {
  assert.ok(claim.ok && deviceKey.length === 64 && publicId.length === 16 && viewKey.length === 16);
});

await check('a claim code is single-use', async () => {
  const again = await call('/gw/v1/claim', {
    method: 'POST', body: JSON.stringify({ code, device: 'OTHER' }),
  });
  assert.strictEqual(again.status, 404);
});

// --- beat ------------------------------------------------------------------
async function beat(seq, frame, key = deviceKey, device = 'AABBCCDDEEFF') {
  const body = JSON.stringify(frame);
  return call('/gw/v1/beat', {
    method: 'POST', body,
    headers: {
      'X-TM-Device': device,
      'X-TM-Seq': String(seq),
      'X-TM-Sig': await hmacHex(key, `${seq}.${body}`),
    },
  });
}

const printing = { v: 1, st: 'Curing', by: 1, ly: 42, lt: 480, rs: 5400, ml: 12.4, mo: 'skull', fw: '0.18.0' };

await check('a correctly signed beat is accepted', async () => {
  const r = await beat(100, printing);
  assert.strictEqual(r.status, 200);
});

await check('the reply is signed over the same seq', async () => {
  const r = await beat(101, printing);
  const text = await r.text();
  assert.strictEqual(await hmacHex(deviceKey, `101.${text}`), r.headers.get('X-TM-RSig'));
});

await check('a wrong key is rejected', async () => {
  const r = await beat(102, printing, 'f'.repeat(64));
  assert.strictEqual(r.status, 401);
});

await check('a tampered body is rejected', async () => {
  const body = JSON.stringify(printing);
  const r = await call('/gw/v1/beat', {
    method: 'POST',
    body: JSON.stringify({ ...printing, ly: 1 }),          // signature covers the original
    headers: {
      'X-TM-Device': 'AABBCCDDEEFF', 'X-TM-Seq': '103',
      'X-TM-Sig': await hmacHex(deviceKey, `103.${body}`),
    },
  });
  assert.strictEqual(r.status, 401);
});

await check('a replayed seq is rejected', async () => {
  const r = await beat(101, printing);                      // 101 already used
  assert.strictEqual(r.status, 401);
});

await check('an unknown device is rejected', async () => {
  const r = await beat(200, printing, deviceKey, 'NOSUCHDEVICE');
  assert.strictEqual(r.status, 409);
});

// --- commands --------------------------------------------------------------
await check('the phone page needs the view key', async () => {
  assert.strictEqual((await call(`/gw/p/${publicId}`)).status, 404);
  assert.strictEqual((await call(`/gw/p/${publicId}?k=wrong`)).status, 404);
  assert.strictEqual((await call(`/gw/p/${publicId}?k=${viewKey}`)).status, 200);
});

await check('a queued command reaches the next beat', async () => {
  await call(`/gw/p/${publicId}/cmd?k=${viewKey}&cmd=pause`, { method: 'POST' });
  const r = await beat(300, printing);
  const j = await r.json();
  assert.strictEqual(j.cmds.length, 1);
  assert.strictEqual(j.cmds[0].cmd, 'pause');
});

await check('an acked command is not sent twice', async () => {
  const r1 = await beat(301, printing);
  const id = (await r1.json()).cmds[0].id;                  // still pending, not acked yet
  const r2 = await beat(302, { ...printing, ack: [id] });
  assert.strictEqual((await r2.json()).cmds.length, 0);
});

await check('only known commands can be queued', async () => {
  const r = await call(`/gw/p/${publicId}/cmd?k=${viewKey}&cmd=selfdestruct`, { method: 'POST' });
  assert.strictEqual(r.status, 400);
});

await check('the printing page shows layer progress', async () => {
  const html = await (await call(`/gw/p/${publicId}?k=${viewKey}`)).text();
  assert.ok(html.includes('42 / 480'), 'layer counter missing');
  assert.ok(html.includes('skull'), 'model name missing');
});

// Web control off on the printer: it acks every command anyway, so a live
// button here would report a stop that never happened.
await check('web control off greys out the buttons', async () => {
  await beat(310, { ...printing, wc: 0 });
  const off = await (await call(`/gw/p/${publicId}?k=${viewKey}`)).text();
  assert.ok(off.includes('data-cmd="stop" class="danger" disabled'), 'stop still live');
  assert.ok(off.includes('Web control is switched off'), 'no explanation shown');

  await beat(311, printing);                       // switched back on
  const on = await (await call(`/gw/p/${publicId}?k=${viewKey}`)).text();
  assert.ok(!on.includes('data-cmd="stop" class="danger" disabled'), 'buttons stayed disabled');
});

console.log(`\n${passed} checks passed`);
