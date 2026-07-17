// TinyMakerWifi feedback collector - a tiny standalone Cloudflare Worker.
//
//   GET  /feedback[/]            -> the form page (proxied from gh-pages)
//   POST /feedback               -> store a note (+ up to 3 photos) in KV
//   GET  /feedback/inbox?key=..  -> the maintainer's reading page (HTML)
//   GET  /feedback/list?key=..   -> the same notes as JSON  (LIST_KEY secret)
//   GET  /feedback/csv?key=..    -> every note as a spreadsheet
//   GET  /feedback/img?key=..&k=img:..  -> one stored photo
//   POST /feedback/mark?key=..&k=fb:..  -> triage: {tag, handled, verdict}
//   POST /feedback/del?key=..&k=fb:..   -> drop one note and its photos
//
// Triage lives on the record itself: the maintainer tags it (submitters
// mis-file their own notes), ticks it handled, and writes the agreed verdict
// so the decision stays glued to what prompted it.
//
// Every record also carries {n, fw, tag, handled, ph} in its KV *metadata*.
// KV list() hands metadata back for free, so the stats block, the filter
// counts and the version list cost one list call - only the notes actually
// on screen are fetched in full.
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
const GHPAGES = 'https://slibbinas.github.io/TinyMakerWifi';
const MAX_PHOTOS = 3;
const MAX_PHOTO_BYTES = 2 * 1024 * 1024;   // the form sends ~300 KB; this is the hard stop

const PAGE = 25;                            // notes fetched in full per view

// Flood limits. The real risk is not money (KV never charges) but silence:
// the free plan stops accepting writes at ~1000/day, and every note costs
// ~3 of them plus one per photo. One bored person at one note a minute would
// burn the day's budget in about seven hours and real feedback would then
// fail with nobody noticing. These caps keep the worst case at ~60 notes
// (~500 writes) while sitting far above any honest volume this project sees.
const MAX_PER_IP_DAY = 5;
const MAX_PER_DAY = 60;
const DAY_TTL = 172800;                     // counters self-clean after 48 h

const str = (v, n) => String(v || '').slice(0, n);

// The subset of a record that lives in KV metadata (see the header note).
const metaOf = (rec) => ({
  n: rec.num || 0,
  fw: rec.fw || '',
  tag: rec.tag || '',
  handled: !!rec.handled,
  ph: (rec.photos || []).length,
});

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    // One canonical host: www 301s to the apex (people type www out of habit,
    // and links they then share should all look the same).
    if (url.hostname.startsWith('www.')) {
      url.hostname = url.hostname.slice(4);
      return Response.redirect(url.toString(), 301);
    }
    const path = url.pathname.replace(/\/$/, '') || '/';

    // Test panel: the per-release physical-test checklist, PUBLIC by design -
    // it is linked from the firmware's pre-release banner and the feedback
    // form, turning every beta user into a structured tester (checklist ->
    // Copy report -> feedback form). Nothing secret lives in it, and panels
    // are written knowing they are public. HTML lives in KV; a new one goes
    // up per release (wrangler kv key put panel:tests). The PANEL_KEY secret
    // stays available for a future *internal* panel route if one is needed.
    if (path === '/testai') {   // the original Lithuanian path, kept as a redirect
      url.pathname = '/tests';
      return Response.redirect(url.toString(), 301);
    }
    if (request.method === 'GET' && path === '/tests') {
      const html = await env.FEEDBACK.get('panel:tests');
      if (!html) return new Response('No panel uploaded yet', { status: 404 });
      return new Response(html, {
        headers: { 'Content-Type': 'text/html;charset=utf-8', 'Cache-Control': 'no-cache' },
      });
    }

    // eInkWeather (oru stotele) interactive prototype - same KV-panel pattern as /tests.
    // Public by design: fake demo data only; linked from that project's README and its
    // Telegram bot's /demo command. Update: wrangler kv key put panel:orai --path prototipas.html
    if (request.method === 'GET' && path === '/orai') {
      const html = await env.FEEDBACK.get('panel:orai');
      if (!html) return new Response('No panel uploaded yet', { status: 404 });
      return new Response(html, {
        headers: { 'Content-Type': 'text/html;charset=utf-8', 'Cache-Control': 'no-cache' },
      });
    }

    // The demo and the manual live on gh-pages, but the apex is not a GitHub
    // Pages site (no CNAME - Cloudflare serves it), so only the paths this
    // worker owns exist there: tinymakerwifi.com/demo/ and /manual/ were 404s
    // while the github.io ones worked. Every link we hand out should be on our
    // own domain rather than spelling out someone else's hosting - and the
    // trailing images/CSS under those paths have to come along, so this proxies
    // the subtree, not just the page.
    if (request.method === 'GET' &&
        /^\/(demo|manual)(\/|$)/.test(path)) {
      const upstream = GHPAGES + path + (url.pathname.endsWith('/') || !path.includes('.') ? '/' : '');
      const r = await fetch(upstream.replace(/\/+$/, '/'), { cf: { cacheTtl: 300 } });
      return new Response(r.body, {
        status: r.status,
        headers: { 'Content-Type': r.headers.get('Content-Type') || 'text/html; charset=utf-8',
                   'Cache-Control': 'max-age=30' },
      });
    }

    if (request.method === 'GET' && path === '/feedback') {
      const r = await fetch(FORM_ORIGIN, { cf: { cacheTtl: 30 } });
      // The widget is injected here rather than baked into the page: the form
      // lives on gh-pages, and this keeps the site key (and whether the check
      // runs at all) a worker setting instead of a commit.
      if (env.TURNSTILE_SITEKEY) {
        const html = (await r.text()).replace('<!--turnstile-->',
          '<script src="https://challenges.cloudflare.com/turnstile/v0/api.js" async defer></script>' +
          // Flexible size renders reliably; the form CSS caps it at 300 px and
          // centres it. Left uncapped it stretched full-width on desktop and
          // clipped its own Cloudflare branding on the right (field report);
          // at ~300 px it shows in full, the same as on a phone.
          `<div class="cf-turnstile" data-sitekey="${env.TURNSTILE_SITEKEY}" data-size="flexible"></div>`);
        return new Response(html, {
          status: r.status,
          headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'max-age=30' },
        });
      }
      return new Response(r.body, {
        status: r.status,
        headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'max-age=30' },
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

      // Turnstile, when it is configured: stops scripted floods at the door
      // and costs a human nothing. Without the secret the check is skipped, so
      // the code can ship before the keys exist.
      if (env.TURNSTILE_SECRET) {
        const ver = await fetch('https://challenges.cloudflare.com/turnstile/v0/siteverify', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            secret: env.TURNSTILE_SECRET,
            response: str(fields['cf-turnstile-response'], 2048),
            remoteip: request.headers.get('CF-Connecting-IP') || undefined,
          }),
        }).then((r) => r.json()).catch(() => ({ success: false }));
        if (!ver.success)
          return new Response('bot check failed - reload and try again', { status: 403, headers: CORS });
      }

      // 1 message / 60 s / IP - burst guard (KV's minimum TTL is 60 s)
      const ip = request.headers.get('CF-Connecting-IP') || 'x';
      if (await env.FEEDBACK.get('gate:' + ip))
        return new Response('slow down', { status: 429, headers: CORS });

      // Daily caps: the slow-drip flood the 60 s gate happily lets through.
      // Rejections only read, so being hammered costs reads (100k/day), not
      // the scarce writes.
      const day = new Date().toISOString().slice(0, 10);
      const ipKey = `day:${day}:${ip}`, allKey = `day:${day}`;
      const [ipUsed, allUsed] = await Promise.all([
        env.FEEDBACK.get(ipKey).then((v) => Number(v) || 0),
        env.FEEDBACK.get(allKey).then((v) => Number(v) || 0),
      ]);
      if (ipUsed >= MAX_PER_IP_DAY)
        return new Response('that is a lot of feedback for one day - mail slibbinas@gmail.com instead', { status: 429, headers: CORS });
      if (allUsed >= MAX_PER_DAY)
        return new Response('the form is over its daily limit - try tomorrow, or open a GitHub issue', { status: 503, headers: CORS });

      await env.FEEDBACK.put('gate:' + ip, '1', { expirationTtl: 60 });
      await env.FEEDBACK.put(ipKey, String(ipUsed + 1), { expirationTtl: DAY_TTL });
      await env.FEEDBACK.put(allKey, String(allUsed + 1), { expirationTtl: DAY_TTL });

      const stamp = new Date().toISOString();
      const id = crypto.randomUUID().slice(0, 8);
      // Case number: a plain KV counter. Two notes sent inside KV's ~60 s
      // propagation window could in theory share a number - at this project's
      // volume that beats standing up a Durable Object for a human reference.
      const num = (Number(await env.FEEDBACK.get('seq')) || 0) + 1;
      await env.FEEDBACK.put('seq', String(num));
      const imgKeys = [];
      for (let i = 0; i < photos.length; i++) {
        const k = 'img:' + stamp + ':' + id + ':' + i;
        await env.FEEDBACK.put(k, await photos[i].arrayBuffer(), {
          metadata: { ct: photos[i].type, size: photos[i].size },
        });
        imgKeys.push(k);
      }
      const rec = {
        num,
        message: msg,
        contact: str(fields.contact, 120),
        fw: str(fields.fw, 20),
        build: str(fields.build, 20),
        ua: str(fields.ua, 120),
        // Came through a printer's dashboard link (it carries fw/build) rather
        // than off the open site. A signal for reading the note, never a gate:
        // the person whose printer will not boot is exactly who must get through.
        src: fields.fw ? 'printer' : 'site',
        photos: imgKeys,
        at: stamp,
      };
      await env.FEEDBACK.put('fb:' + stamp + ':' + id, JSON.stringify(rec),
                             { metadata: metaOf(rec) });
      return new Response(JSON.stringify({ ok: true, id: num, photos: imgKeys.length }), {
        headers: { 'Content-Type': 'application/json', ...CORS },
      });
    }

    // trim(): a secret typed at a Windows prompt can arrive with a stray \r,
    // which would silently fail every key comparison.
    const listKey = String(env.LIST_KEY || '').trim();
    const keyOk = listKey && (url.searchParams.get('key') || '').trim() === listKey;

    // Every fb: key with its metadata, newest first. One list call, no gets -
    // enough for stats, filter counts and the version list.
    const indexAll = async () => {
      const out = [];
      let cursor;
      do {
        const page = await env.FEEDBACK.list({ prefix: 'fb:', limit: 1000, cursor });
        for (const k of page.keys) out.push({ key: k.name, m: k.metadata || {} });
        cursor = page.list_complete ? null : page.cursor;
      } while (cursor);
      out.reverse();
      return out;
    };

    const hydrate = async (entries) => {
      const out = [];
      for (const e of entries) {
        const v = await env.FEEDBACK.get(e.key);
        if (!v) continue;
        const rec = JSON.parse(v);
        rec.key = e.key;
        rec.photoUrls = (rec.photos || []).map(
          (p) => url.origin + '/feedback/img?key=' + encodeURIComponent(listKey) + '&k=' + encodeURIComponent(p));
        out.push(rec);
      }
      return out;
    };

    if (path === '/feedback/inbox' && keyOk) {
      const index = await indexAll();
      // Filtering happens on metadata, so a filtered page still reads only
      // the notes it shows.
      const f = url.searchParams.get('f') || 'open';
      const fw = url.searchParams.get('fw') || '';
      const match = (m) =>
        (!fw || m.fw === fw) &&
        (f === 'all' ? true : f === 'open' ? !m.handled : m.tag === f);
      const hits = index.filter((e) => match(e.m));
      const from = Math.max(0, parseInt(url.searchParams.get('from')) || 0);
      const notes = await hydrate(hits.slice(from, from + PAGE));
      return new Response(
        inboxPage(notes, listKey, { index, hits: hits.length, from, f, fw }),
        { headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' } });
    }

    if (path === '/feedback/csv' && keyOk) {
      const rows = await hydrate(await indexAll());
      const cell = (v) => '"' + String(v == null ? '' : v).replace(/"/g, '""') + '"';
      const csv = ['num,at,fw,build,tag,handled,message,contact,verdict,photos']
        .concat(rows.map((r) => [r.num, r.at, r.fw, r.build, r.tag || '', r.handled ? 'yes' : 'no',
                                 r.message, r.contact, r.verdict || '', (r.photos || []).length].map(cell).join(',')))
        .join('\r\n');
      return new Response('﻿' + csv, {   // BOM: Excel opens UTF-8 correctly
        headers: {
          'Content-Type': 'text/csv; charset=utf-8',
          'Content-Disposition': 'attachment; filename="tinymaker-feedback.csv"',
        },
      });
    }

    if (path === '/feedback/mark' && keyOk && request.method === 'POST') {
      const k = url.searchParams.get('k') || '';
      if (!k.startsWith('fb:')) return new Response('bad key', { status: 400 });
      const v = await env.FEEDBACK.get(k);
      if (!v) return new Response('not found', { status: 404 });
      let patch = {};
      try { patch = await request.json(); } catch (e) {}
      const rec = JSON.parse(v);
      if ('tag' in patch) rec.tag = ['bug', 'feature', 'other'].includes(patch.tag) ? patch.tag : '';
      if ('handled' in patch) rec.handled = !!patch.handled;
      if ('verdict' in patch) {
        rec.verdict = str(patch.verdict, 2000).trim();
        rec.verdictAt = rec.verdict ? new Date().toISOString() : '';
      }
      await env.FEEDBACK.put(k, JSON.stringify(rec), { metadata: metaOf(rec) });
      return new Response('{"ok":true}', { headers: { 'Content-Type': 'application/json' } });
    }

    if (path === '/feedback/del' && keyOk && request.method === 'POST') {
      const k = url.searchParams.get('k') || '';
      if (!k.startsWith('fb:')) return new Response('bad key', { status: 400 });
      const v = await env.FEEDBACK.get(k);
      if (v) {
        const rec = JSON.parse(v);
        for (const p of rec.photos || []) await env.FEEDBACK.delete(p);
      }
      await env.FEEDBACK.delete(k);
      return new Response('{"ok":true}', { headers: { 'Content-Type': 'application/json' } });
    }

    if (path === '/feedback/list' && keyOk) {
      return new Response(JSON.stringify(await hydrate(await indexAll()), null, 1), {
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

// ---------------------------------------------------------------- inbox page
// Every field below is user-submitted, so esc() is not optional: a note is
// read here in a browser holding the LIST_KEY.
const esc = (s) => String(s == null ? '' : s)
  .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
  .replace(/"/g, '&quot;').replace(/'/g, '&#39;');

const when = (iso) => {
  const d = new Date(iso);
  if (isNaN(d)) return esc(iso);
  const p = (n) => (n < 10 ? '0' : '') + n;
  return `${d.getUTCFullYear()}-${p(d.getUTCMonth() + 1)}-${p(d.getUTCDate())} ${p(d.getUTCHours())}:${p(d.getUTCMinutes())} UTC`;
};

const contactLink = (c) => {
  const t = String(c || '').trim();
  if (!t) return '';
  const href = /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(t) ? 'mailto:' + t : null;
  return href
    ? `<a class="contact" href="${esc(href)}">${esc(t)}</a>`
    : `<span class="contact">${esc(t)}</span>`;
};

const TAGS = [['bug', 'Bug'], ['feature', 'Feature'], ['other', 'Other']];

function inboxPage(notes, listKey, view) {
  const { index, hits, from, f, fw } = view;
  const all = index.map((e) => e.m);
  const open = all.filter((m) => !m.handled).length;
  const count = (t) => all.filter((m) => m.tag === t).length;
  const q = (over) => {
    const p = new URLSearchParams({ key: listKey, f, ...(fw ? { fw } : {}), ...over });
    if (!p.get('from') || p.get('from') === '0') p.delete('from');
    return '/feedback/inbox?' + p;
  };

  // Release health: the beta question is "did 0.15.0 break something", and
  // that is only answerable per version.
  const versions = [...new Set(all.map((m) => m.fw).filter(Boolean))].sort().reverse();
  const statsRows = versions.map((v) => {
    const s = all.filter((m) => m.fw === v);
    return `<tr${fw === v ? ' class="on"' : ''}>
      <td><a href="${esc(q({ fw: v, from: 0 }))}">fw ${esc(v)}</a></td>
      <td>${s.length}</td>
      <td${s.filter((m) => m.tag === 'bug').length ? ' class="bug"' : ''}>${s.filter((m) => m.tag === 'bug').length}</td>
      <td>${s.filter((m) => m.tag === 'feature').length}</td>
      <td>${s.filter((m) => m.ph).length}</td>
      <td${s.filter((m) => !m.handled).length ? ' class="open"' : ''}>${s.filter((m) => !m.handled).length}</td>
    </tr>`;
  }).join('');
  const noFw = all.filter((m) => !m.fw).length;

  const cards = notes.map((n) => `
    <article class="note${n.handled ? ' done' : ''}" data-k="${esc(n.key)}"
             ${n.num ? `id="n${esc(n.num)}"` : ''}
             data-tag="${esc(n.tag || '')}" data-handled="${n.handled ? '1' : ''}">
      <div class="msg">${esc(n.message)}</div>
      ${(n.photoUrls || []).length ? `<div class="shots">${n.photoUrls.map((u) =>
        `<a href="${esc(u)}" target="_blank" rel="noopener"><img src="${esc(u)}" alt="attached photo" loading="lazy"></a>`).join('')}</div>` : ''}
      <div class="verdict${n.verdict ? '' : ' blank'}">
        <label>Verdict — what we agreed${n.verdictAt ? ` <span class="vat">${when(n.verdictAt)}</span>` : ''}</label>
        <textarea rows="2" placeholder="e.g. Real bug, fixed in 0.15.1 · Duplicate of the resin estimate note · Backlog #31, after 1.0.0">${esc(n.verdict)}</textarea>
        <button class="save" disabled>Save</button>
      </div>
      <div class="meta">
        ${n.num ? `<a class="num" href="#n${esc(n.num)}" title="Link to this case">#${esc(n.num)}</a>` : ''}
        <time>${when(n.at)}</time>
        ${n.fw ? `<span class="pill">fw ${esc(n.fw)}${n.build ? ` <em>${esc(n.build)}</em>` : ''}</span>` : ''}
        ${n.src === 'printer' ? '<span class="pill src" title="Sent from a printer dashboard, not the open site">🖨 from a printer</span>' : ''}
        ${contactLink(n.contact)}
        <span class="tags">${TAGS.map(([v, label]) =>
          `<button class="tag${n.tag === v ? ' on' : ''}" data-tag="${v}">${label}</button>`).join('')}</span>
        <button class="handle${n.handled ? ' on' : ''}">${n.handled ? '✓ Handled' : 'Mark handled'}</button>
        <button class="del" title="Delete this note and its photos">Delete</button>
      </div>
    </article>`).join('');

  return `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex,nofollow">
<title>Feedback inbox — TinyMakerWifi</title>
<script>(function(){try{var t=new URLSearchParams(location.search).get('theme');
  if(t==='light'||t==='dark')document.documentElement.setAttribute('data-theme',t);}catch(e){}})()</script>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/><rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/><rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/><path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/></svg>">
<style>
:root{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--accent:#e8720c;--pill:#2a2a2e;--danger:#b34a38;--ok:#3f9f55}
@media(prefers-color-scheme:light){:root{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--ok:#2f8043}}
/* ?theme= wins over the OS preference, both ways - the dashboard passes its
   own choice along, same as it does for the manual. */
:root[data-theme=light]{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--ok:#2f8043}
:root[data-theme=dark]{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--pill:#2a2a2e;--ok:#3f9f55}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15.5px/1.55 -apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.wrap{max-width:760px;margin:0 auto;padding:24px 14px 60px;display:flex;flex-direction:column;gap:14px}
header{display:flex;align-items:baseline;justify-content:space-between;gap:10px;flex-wrap:wrap;padding:2px}
h1{font-size:1.2rem;margin:0;display:flex;align-items:center;gap:8px}h1 b{color:var(--accent)}
h1 .mark{width:24px;height:24px;flex:none}
.counts{color:var(--muted);font-size:.82rem;font-variant-numeric:tabular-nums}
.filters{display:flex;gap:8px;flex-wrap:wrap}
.filters a{background:var(--pill);color:var(--text);border:1px solid var(--line);border-radius:999px;padding:5px 12px;font-size:.8rem;font-weight:600;text-decoration:none}
.filters a.on{background:var(--accent);border-color:var(--accent);color:#fff}
.filters a.fwOff{background:none;border-style:dashed;color:var(--muted);font-family:ui-monospace,Consolas,monospace}
.stats{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 16px}
.stats summary{cursor:pointer;font-size:.72rem;text-transform:uppercase;letter-spacing:.09em;color:var(--muted);font-weight:700}
.stats[open] summary{margin-bottom:10px}
.stats .scroll{overflow-x:auto}
.stats table{border-collapse:collapse;width:100%;font-size:.84rem;font-variant-numeric:tabular-nums}
.stats th{text-align:right;font-weight:600;color:var(--muted);font-size:.72rem;text-transform:uppercase;letter-spacing:.05em;padding:0 0 6px 14px}
.stats td{text-align:right;padding:5px 0 5px 14px;border-top:1px solid var(--line)}
.stats th:first-child,.stats td:first-child{text-align:left;padding-left:0}
.stats td:first-child a{color:var(--text);text-decoration:none;font-family:ui-monospace,Consolas,monospace}
.stats td:first-child a:hover{color:var(--accent)}
.stats tr.on td{color:var(--accent)}
.stats td.bug{color:var(--danger);font-weight:700}
.stats td.open{color:var(--accent);font-weight:700}
.stats .hint{margin:8px 0 0;font-size:.76rem;color:var(--muted)}
.pager{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:2px 4px}
.pager a{color:var(--accent);text-decoration:none;font-size:.84rem;font-weight:600}
.pager .range{color:var(--muted);font-size:.78rem;font-variant-numeric:tabular-nums}
.note{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px 18px;display:flex;flex-direction:column;gap:12px}
.note.done{opacity:.62}
.note.done .msg{color:var(--muted)}
.msg{white-space:pre-wrap;overflow-wrap:anywhere}
.verdict{border-left:3px solid var(--accent);padding:2px 0 2px 12px;display:flex;flex-direction:column;gap:6px}
.verdict.blank{border-left-color:var(--line)}
.verdict label{font-size:.72rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);font-weight:700}
.verdict .vat{text-transform:none;letter-spacing:0;font-weight:400;opacity:.7}
.verdict textarea{width:100%;background:var(--bg);color:var(--text);border:1px solid var(--line);border-radius:8px;padding:8px 10px;font:inherit;font-size:.88rem;resize:vertical}
.verdict textarea:focus{outline:none;border-color:var(--accent)}
.verdict .save{align-self:flex-start;background:var(--accent);border:0;color:#fff;border-radius:7px;padding:5px 14px;font-size:.76rem;font-weight:600;cursor:pointer}
.verdict .save:disabled{background:var(--pill);color:var(--muted);cursor:default}
.shots{display:flex;gap:8px;flex-wrap:wrap}
.shots img{width:132px;height:132px;object-fit:cover;border-radius:8px;border:1px solid var(--line);display:block}
.shots a:hover img{border-color:var(--accent)}
.meta{display:flex;align-items:center;gap:10px;flex-wrap:wrap;font-size:.8rem;color:var(--muted);border-top:1px solid var(--line);padding-top:10px}
.pill{background:var(--pill);border-radius:999px;padding:2px 9px;font-family:ui-monospace,Consolas,monospace;font-size:.74rem}
.pill em{opacity:.6;font-style:normal}
.pill.src{font-family:inherit;color:var(--ok);border:1px solid var(--ok);background:none}
.contact{color:#84bcf8;text-decoration:none}.contact:hover{text-decoration:underline}
.num{color:var(--accent);font-weight:700;font-family:ui-monospace,Consolas,monospace;text-decoration:none;font-size:.86rem}
.num:hover{text-decoration:underline}
.note:target{border-color:var(--accent)}
.tags{display:flex;gap:5px;margin-left:auto}
.meta button{background:none;border:1px solid var(--line);color:var(--muted);border-radius:7px;padding:3px 10px;font-size:.75rem;cursor:pointer}
.meta .tag:hover{border-color:var(--accent);color:var(--accent)}
.meta .tag.on{background:var(--accent);border-color:var(--accent);color:#fff;font-weight:600}
.meta .handle.on{border-color:var(--ok);color:var(--ok)}
.meta .handle:hover{border-color:var(--ok);color:var(--ok)}
.meta .del:hover{border-color:var(--danger);color:var(--danger)}
.empty{background:var(--card);border:1px dashed var(--line);border-radius:12px;padding:40px 20px;text-align:center;color:var(--muted)}
.empty .big{font-size:2rem;margin-bottom:6px}
footer{color:var(--muted);font-size:.76rem;text-align:center}
footer a{color:#84bcf8;text-decoration:none}
</style></head><body><div class="wrap">
<header>
  <h1><svg class="mark" viewBox="0 0 64 64" aria-hidden="true"><rect x="8" y="40" width="48" height="9" rx="3" fill="#e8720c"/><rect x="14" y="27" width="36" height="9" rx="3" fill="#e8720c" opacity=".75"/><rect x="20" y="14" width="24" height="9" rx="3" fill="#e8720c" opacity=".5"/><path d="M22 6 A14 14 0 0 1 42 6" fill="none" stroke="#4da3ff" stroke-width="5" stroke-linecap="round"/></svg><b>TinyMakerWifi</b> feedback</h1>
  <span class="counts">${open} open · ${all.length} total</span>
</header>
${all.length ? `<div class="filters">
  <a class="${f === 'open' ? 'on' : ''}" href="${esc(q({ f: 'open', from: 0 }))}">New (${open})</a>
  ${TAGS.map(([v, label]) => `<a class="${f === v ? 'on' : ''}" href="${esc(q({ f: v, from: 0 }))}">${label} (${count(v)})</a>`).join('')}
  <a class="${f === 'all' ? 'on' : ''}" href="${esc(q({ f: 'all', from: 0 }))}">All (${all.length})</a>
  ${fw ? `<a class="fwOff" href="${esc('/feedback/inbox?key=' + encodeURIComponent(listKey) + '&f=' + esc(f))}">fw ${esc(fw)} ✕</a>` : ''}
</div>` : ''}
${versions.length ? `<details class="stats"${versions.length > 1 ? ' open' : ''}>
  <summary>Per release${fw ? ` — filtering by fw ${esc(fw)}` : ''}</summary>
  <div class="scroll"><table>
    <thead><tr><th>Release</th><th>Notes</th><th>Bugs</th><th>Features</th><th>Photos</th><th>Open</th></tr></thead>
    <tbody>${statsRows}</tbody>
  </table></div>
  ${noFw ? `<p class="hint">${noFw} note${noFw === 1 ? '' : 's'} without a version (sent straight from the site).</p>` : ''}
</details>` : ''}
${notes.length ? cards : all.length
  ? `<div class="empty"><div class="big">✅</div>Nothing here — try another filter.</div>`
  : `<div class="empty"><div class="big">📭</div>Nothing yet. The form is at <a href="/feedback/">tinymakerwifi.com/feedback</a>.</div>`}
${hits > PAGE ? `<div class="pager">
  ${from > 0 ? `<a href="${esc(q({ from: Math.max(0, from - PAGE) }))}">← Newer</a>` : '<span></span>'}
  <span class="range">${from + 1}–${Math.min(from + PAGE, hits)} of ${hits}</span>
  ${from + PAGE < hits ? `<a href="${esc(q({ from: from + PAGE }))}">Older →</a>` : '<span></span>'}
</div>` : ''}
<footer>Private page · <a href="/feedback/csv?key=${encodeURIComponent(listKey)}">download CSV</a> · <a href="/feedback/list?key=${encodeURIComponent(listKey)}">raw JSON</a></footer>
</div>
<script>
var KEY=${JSON.stringify(listKey)};
var api=function(what,note,body){
  return fetch('/feedback/'+what+'?key='+encodeURIComponent(KEY)+'&k='+encodeURIComponent(note.dataset.k),
    {method:'POST',headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined})
    .then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);});
};
// Filters and paging are links: the server picks the page, so a filtered view
// only ever fetches the notes it shows, and the URL stays shareable.
document.querySelectorAll('.note').forEach(function(n){
  n.querySelectorAll('.tag').forEach(function(b){
    b.addEventListener('click',function(){
      var next=b.classList.contains('on')?'':b.dataset.tag;   // click again to clear
      api('mark',n,{tag:next}).then(function(){
        n.querySelectorAll('.tag').forEach(function(x){x.classList.remove('on');});
        if(next)b.classList.add('on');
        n.dataset.tag=next;
      }).catch(function(){b.textContent='failed';});
    });
  });
  var h=n.querySelector('.handle');
  h.addEventListener('click',function(){
    var next=!n.dataset.handled;
    api('mark',n,{handled:next}).then(function(){
      n.dataset.handled=next?'1':'';
      n.classList.toggle('done',next);
      h.classList.toggle('on',next);
      h.textContent=next?'✓ Handled':'Mark handled';
    }).catch(function(){h.textContent='failed';});
  });
  var ta=n.querySelector('.verdict textarea'),save=n.querySelector('.verdict .save'),was=ta.value;
  ta.addEventListener('input',function(){save.disabled=(ta.value===was);save.textContent='Save';});
  save.addEventListener('click',function(){
    save.disabled=true;save.textContent='Saving...';
    api('mark',n,{verdict:ta.value}).then(function(){
      was=ta.value;save.textContent='Saved';
      n.querySelector('.verdict').classList.toggle('blank',!ta.value.trim());
    }).catch(function(){save.disabled=false;save.textContent='Save failed';});
  });
  n.querySelector('.del').addEventListener('click',function(){
    if(!confirm('Delete this note and its photos?'))return;
    var b=n.querySelector('.del');b.disabled=true;b.textContent='Deleting...';
    api('del',n).then(function(){n.remove();})
      .catch(function(){b.disabled=false;b.textContent='Delete failed';});
  });
});
</script></body></html>`;
}
