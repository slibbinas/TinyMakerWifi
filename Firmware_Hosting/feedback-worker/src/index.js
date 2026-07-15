// TinyMakerWifi feedback collector - a tiny standalone Cloudflare Worker.
// POST /feedback  {message, contact?, fw?, build?, ua?}  -> stored in KV.
// Deployed as `tinymaker-feedback`; the form at tinymakerwifi.com/feedback/
// posts here. Reading: CF dashboard KV browser, or GET /list?key=<LIST_KEY>
// (set LIST_KEY as a Worker secret: wrangler secret put LIST_KEY).

const CORS = {
  'Access-Control-Allow-Origin': 'https://tinymakerwifi.com',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === '/feedback') {
      if (request.method === 'OPTIONS') return new Response(null, { headers: CORS });
      if (request.method !== 'POST')
        return new Response('POST only', { status: 405, headers: CORS });
      let body;
      try { body = await request.json(); } catch (e) {
        return new Response('bad json', { status: 400, headers: CORS });
      }
      const msg = String(body.message || '').slice(0, 4000).trim();
      if (!msg) return new Response('empty', { status: 400, headers: CORS });
      // 1 message / 60 s / IP - crude anti-spam (KV's minimum TTL is 60 s)
      const ip = request.headers.get('CF-Connecting-IP') || 'x';
      if (await env.FEEDBACK.get('gate:' + ip))
        return new Response('slow down', { status: 429, headers: CORS });
      await env.FEEDBACK.put('gate:' + ip, '1', { expirationTtl: 60 });
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
        headers: { 'Content-Type': 'application/json', ...CORS },
      });
    }

    if (url.pathname === '/list' && env.LIST_KEY && url.searchParams.get('key') === env.LIST_KEY) {
      const list = await env.FEEDBACK.list({ prefix: 'fb:', limit: 100 });
      const out = [];
      for (const k of list.keys) {
        const v = await env.FEEDBACK.get(k.name);
        if (v) out.push(JSON.parse(v));
      }
      return new Response(JSON.stringify(out, null, 1), {
        headers: { 'Content-Type': 'application/json' },
      });
    }

    return new Response('TinyMakerWifi feedback collector', { status: 200 });
  },
};
