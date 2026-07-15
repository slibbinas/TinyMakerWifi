// TinyMakerWifi feedback collector - a tiny standalone Cloudflare Worker.
//
//   GET  /feedback[/]           -> the form page (proxied from gh-pages)
//   POST /feedback              -> store a note (+ up to 3 photos) in KV
//   GET  /feedback/list?key=..  -> newest notes as JSON   (LIST_KEY secret)
//   GET  /feedback/img?key=..&k=img:..  -> one stored photo
//
// Photos live in KV, NOT R2: the Workers free plan simply stops accepting
// writes past its 1 GB / 1k-writes-a-day limits, while R2 would auto-charge
// the card on file past 10 GB. The form downscales every photo to <=1600 px
// JPEG (~200-400 KB), so 1 GB is thousands of them.
//
// Deploy: cd Firmware_Hosting/feedback-worker && npx wrangler deploy

const CORS = {
  'Access-Control-Allow-Origin': 'https://tinymakerwifi.com',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

const FORM_ORIGIN = 'https://slibbinas.github.io/TinyMakerWifi/feedback/';
const MAX_PHOTOS = 3;
const MAX_PHOTO_BYTES = 2 * 1024 * 1024;   // the form sends ~300 KB; this is the hard stop

const str = (v, n) => String(v || '').slice(0, n);

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname.replace(/\/$/, '') || '/';

    if (request.method === 'GET' && path === '/feedback') {
      const r = await fetch(FORM_ORIGIN, { cf: { cacheTtl: 300 } });
      return new Response(r.body, {
        status: r.status,
        headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'max-age=300' },
      });
    }

    if (path === '/feedback' && request.method === 'OPTIONS')
      return new Response(null, { headers: CORS });

    if (path === '/feedback' && request.method === 'POST') {
      let fields = {}, photos = [];
      const ct = request.headers.get('Content-Type') || '';
      try {
        if (ct.includes('multipart/form-data')) {
          const fd = await request.formData();
          for (const [k, v] of fd.entries()) {
            if (k === 'photo' && typeof v === 'object' && v.size) photos.push(v);
            else fields[k] = String(v);
          }
        } else {
          fields = await request.json();
        }
      } catch (e) {
        return new Response('bad body', { status: 400, headers: CORS });
      }

      const msg = str(fields.message, 4000).trim();
      if (!msg) return new Response('empty', { status: 400, headers: CORS });
      photos = photos.slice(0, MAX_PHOTOS);
      for (const p of photos) {
        if (p.size > MAX_PHOTO_BYTES)
          return new Response('photo too large', { status: 413, headers: CORS });
        if (!String(p.type || '').startsWith('image/'))
          return new Response('photos only', { status: 415, headers: CORS });
      }

      // 1 message / 60 s / IP - crude anti-spam (KV's minimum TTL is 60 s)
      const ip = request.headers.get('CF-Connecting-IP') || 'x';
      if (await env.FEEDBACK.get('gate:' + ip))
        return new Response('slow down', { status: 429, headers: CORS });
      await env.FEEDBACK.put('gate:' + ip, '1', { expirationTtl: 60 });

      const stamp = new Date().toISOString();
      const id = crypto.randomUUID().slice(0, 8);
      const imgKeys = [];
      for (let i = 0; i < photos.length; i++) {
        const k = 'img:' + stamp + ':' + id + ':' + i;
        await env.FEEDBACK.put(k, await photos[i].arrayBuffer(), {
          metadata: { ct: photos[i].type, size: photos[i].size },
        });
        imgKeys.push(k);
      }
      await env.FEEDBACK.put('fb:' + stamp + ':' + id, JSON.stringify({
        message: msg,
        contact: str(fields.contact, 120),
        fw: str(fields.fw, 20),
        build: str(fields.build, 20),
        ua: str(fields.ua, 120),
        photos: imgKeys,
        at: stamp,
      }));
      return new Response(JSON.stringify({ ok: true, photos: imgKeys.length }), {
        headers: { 'Content-Type': 'application/json', ...CORS },
      });
    }

    const keyOk = env.LIST_KEY && url.searchParams.get('key') === env.LIST_KEY;

    if (path === '/feedback/list' && keyOk) {
      const list = await env.FEEDBACK.list({ prefix: 'fb:', limit: 100 });
      const out = [];
      for (const k of list.keys) {
        const v = await env.FEEDBACK.get(k.name);
        if (!v) continue;
        const rec = JSON.parse(v);
        rec.photoUrls = (rec.photos || []).map(
          (p) => url.origin + '/feedback/img?key=' + encodeURIComponent(env.LIST_KEY) + '&k=' + encodeURIComponent(p));
        out.push(rec);
      }
      out.reverse();  // newest first
      return new Response(JSON.stringify(out, null, 1), {
        headers: { 'Content-Type': 'application/json' },
      });
    }

    if (path === '/feedback/img' && keyOk) {
      const k = url.searchParams.get('k') || '';
      if (!k.startsWith('img:')) return new Response('bad key', { status: 400 });
      const { value, metadata } = await env.FEEDBACK.getWithMetadata(k, { type: 'arrayBuffer' });
      if (!value) return new Response('not found', { status: 404 });
      return new Response(value, {
        headers: { 'Content-Type': (metadata && metadata.ct) || 'image/jpeg' },
      });
    }

    return new Response('TinyMakerWifi feedback collector', { status: 200 });
  },
};
