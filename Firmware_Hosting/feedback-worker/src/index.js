import { mergeState } from './state.mjs';

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
//   GET  /feedback/status?t=<token>     -> PUBLIC: the submitter's own note's
//                                          status (ticket - 0-7)
//   GET  /feedback/recent               -> PUBLIC: last 5 handled notes,
//                                          number/tag/fw only (0-7)
//
// Triage lives on the record itself: the maintainer tags it (submitters
// mis-file their own notes), ticks it handled, and writes the agreed verdict
// so the decision stays glued to what prompted it.
//
// Verdict visibility rule (0-7, agreed 2026-07-22): notes carrying a ticket
// token (everything submitted after this shipped) show their verdict to the
// holder of the token - so write verdicts knowing the submitter reads them.
// Older verdicts were written as private and are never exposed; the public
// recent-fixed list carries no verdict text at all.
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

// Only for /slicerbug: the printer's dashboard is served from a LAN address
// (http://tinymaker.local, http://192.168.x.x), so no fixed origin can be named.
// The public form keeps the strict CORS above - this is a separate, rate-limited
// door that accepts one kind of body and hands back nothing worth stealing.
const WIDE_CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
};

const FORM_ORIGIN = 'https://slibbinas.github.io/TinyMakerWifi/feedback/';
const GHPAGES = 'https://slibbinas.github.io/TinyMakerWifi';
const EXT_STORE = 'https://chromewebstore.google.com/detail/tinymaker-status/bhfjpfjkhopfnfpapgahmaipdgmajlde';
// The inboxes open from the owner's links.json in the printer dashboard - a new tab with the
// printer (LAN) as referrer, which script may close. Close shows only there (V 2026-09-26).
const TM_CLOSE = String.raw`<p class="tmclose" hidden style="text-align:center;margin:18px 0 0;font-size:.9rem"><a href="#" onclick="window.close();return false;">Close</a></p><script>(function(){try{var h=document.referrer?new URL(document.referrer).hostname:'';var lan=/^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.|127\.)/.test(h)||/\.local$/.test(h);if(!(lan&&history.length<=1))return;document.querySelectorAll('.tmclose').forEach(function(e){e.hidden=false;});var a=document.querySelector('.tmcrumb a');if(a){var o=new URL(document.referrer).origin+'/';a.textContent='Printer';a.href=o;a.title='Back to the printer dashboard';a.onclick=function(){window.close();setTimeout(function(){location.href=o;},300);return false;};}}catch(e){}})();</script>`;
const MAX_PHOTOS = 3;
const MAX_PHOTO_BYTES = 2 * 1024 * 1024;   // the form sends ~300 KB; this is the hard stop

// A photo is a RASTER photo. "image/*" was too generous: an SVG is a document,
// and the inbox opens attachments in a tab whose URL carries LIST_KEY - so a
// script inside an "image" would run on tinymakerwifi.com and could read that
// key straight out of location.search. The type is declared by whoever uploads,
// so it is checked on the way in AND again on the way out (/feedback/img).
const SAFE_IMG = ['image/png', 'image/jpeg', 'image/webp', 'image/gif'];
const safeImgType = (t) => (SAFE_IMG.includes(String(t || '').toLowerCase().split(';')[0].trim()));

// The two ways a /r/ link can fail to lead anywhere. Both say which resin was
// asked for: these URLs are read off a printer screen and typed by hand, and
// "not found" without the slug tells the person nothing they can act on.
const rPage = (status, title, body) => new Response(
  `<!doctype html><meta charset="utf-8"><title>${title}</title>` +
  '<meta name="viewport" content="width=device-width,initial-scale=1">' +
  '<style>body{margin:0;padding:48px 20px;background:#f7f7f8;color:#1d1f24;' +
  'font:16px/1.6 -apple-system,"Segoe UI",Roboto,sans-serif}' +
  'main{max-width:34rem;margin:0 auto}h1{font-size:1.3rem;margin:0 0 12px}' +
  'a{color:#f07a1a}</style>' +
  `<main><h1>${title}</h1><p>${body}</p>` +
  '<p><a href="https://tinymakerwifi.com/">tinymakerwifi.com</a></p></main>',
  { status, headers: { 'Content-Type': 'text/html;charset=utf-8', 'Cache-Control': 'no-store' } });

const rGone = (slug) => rPage(404, 'No shop link for this resin',
  `Nothing is published for <code>${String(slug).replace(/[<&]/g, '')}</code>. ` +
  'The resin may have been renamed, or its link was never set - the profile ' +
  'itself is unaffected and keeps working.');

const rDown = () => rPage(503, 'The link list is unreachable',
  'The shop links are published alongside the resin profiles and that file did ' +
  'not answer just now. This is temporary; try again in a minute.');

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
// A bug hunt is bursty, but ten reports is already a long evening - and this is
// the one door with no human check, so its worst day has to stay small: 10 x 1 MB
// is ~10 MB, against a 1 GB namespace.
const MAX_SLICERBUG_PER_DAY = 10;
const MAX_SLICERBUG_PHOTO_BYTES = 1024 * 1024;
const MAX_CRASH_PER_DAY = 200;             // crash pings are rare + firmware-deduped; cap guards a runaway fleet
const CRASH_TTL = 60 * 60 * 24 * 90;       // crash records self-clean after 90 days

const str = (v, n) => String(v || '').slice(0, n);
const hexStr = (v, n) => String(v || '').toLowerCase().replace(/[^0-9a-f]/g, '').slice(0, n);

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

    // --- SEO / AI discovery: robots.txt, sitemap.xml, llms.txt at the apex ---
    // The apex root is Cloudflare-served (not this worker) and there is no CNAME,
    // so these files have no gh-pages home; the worker owns them via dedicated
    // routes (see wrangler.jsonc). Kept inline here as the single source of truth.
    if (request.method === 'GET' && path === '/robots.txt') {
      const body = 'User-agent: *\nAllow: /\nDisallow: /api/\n\nSitemap: https://tinymakerwifi.com/sitemap.xml\n';
      return new Response(body, {
        headers: { 'Content-Type': 'text/plain;charset=utf-8', 'Cache-Control': 'public, max-age=86400' },
      });
    }
    if (request.method === 'GET' && path === '/sitemap.xml') {
      const pages = ['/', '/manual/', '/roadmap/', '/demo/'];
      const body = '<?xml version="1.0" encoding="UTF-8"?>\n'
        + '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
        + pages.map(p => `  <url><loc>https://tinymakerwifi.com${p}</loc></url>`).join('\n')
        + '\n</urlset>\n';
      return new Response(body, {
        headers: { 'Content-Type': 'application/xml;charset=utf-8', 'Cache-Control': 'public, max-age=86400' },
      });
    }
    if (request.method === 'GET' && path === '/llms.txt') {
      const body = `# TinyMakerWifi

> Open-source firmware that adds WiFi, wireless model upload from PrusaSlicer, a web dashboard, OTA self-updates, resin tracking and phone notifications to the palm-sized TinyMaker MSLA (resin) 3D printer (ESP32-WROOM-32E).

## Docs
- [User manual](https://tinymakerwifi.com/manual/): WiFi setup, wireless printing from PrusaSlicer, the web dashboard, OTA updates, exposure/resin settings, troubleshooting
- [Roadmap](https://tinymakerwifi.com/roadmap/): what is coming and what shipped
- [Live demo](https://tinymakerwifi.com/demo/): the real dashboard driving a simulated printer
- [Source, releases & firmware downloads](https://github.com/slibbinas/TinyMakerWifi)
`;
      return new Response(body, {
        headers: { 'Content-Type': 'text/plain;charset=utf-8', 'Cache-Control': 'public, max-age=86400' },
      });
    }

    // --- /r/<slug>: the Buy link that survives being installed --------------
    //
    // The firmware takes a buy link only from our own domain
    // (resinBuyUrlAllowed, src/ResinProfile.ino), so a manufacturer URL written
    // into a profile was dropped the moment the profile reached the printer:
    // the link showed in the library list and then vanished from the installed
    // profile. Every profile now carries /r/<slug> instead, and the real shop
    // URL lives outside the firmware - which is the point: a partner can change
    // without a firmware release, and the printer never learns who it is.
    //
    // The table is NOT in this file. It is resin/links.json, published next to
    // the profiles, so a resin and its shop link ship in the same commit. A
    // table in here would let a profile go out with a /r/ URL that leads
    // nowhere until somebody remembers to deploy the worker - the exact drift
    // the manifest is written in one piece to avoid.
    if (path.startsWith('/r/')) {
      if (request.method !== 'GET' && request.method !== 'HEAD')
        return new Response('method not allowed', { status: 405 });
      const slug = path.slice(3);
      if (!/^[a-z0-9][a-z0-9-]{0,40}$/.test(slug)) return rGone(slug);

      let links = null;
      try {
        const r = await fetch(GHPAGES + '/resin/links.json', { cf: { cacheTtl: 300 } });
        if (r.ok) links = await r.json();
      } catch (e) { /* falls through to rDown() */ }
      if (!links || typeof links !== 'object') return rDown();

      // A plain string is the normal case. The object form exists because a
      // shop is not one shop: the same resin is bought elsewhere depending on
      // where the buyer is, and that is a property of the link, not of the
      // resin - so it stays here rather than turning into two profiles.
      const entry = links[slug];
      const country = (request.cf && request.cf.country) || '';
      const target = typeof entry === 'string'
        ? entry
        : (entry && ((entry.geo && entry.geo[country]) || entry.url)) || '';
      if (!/^https:\/\//.test(target)) return rGone(slug);

      // 302, not 301: the destination is a partner link, and a browser that
      // cached it permanently would keep sending people to a partner we left.
      return new Response(null, {
        status: 302,
        headers: { 'Location': target, 'Cache-Control': 'public, max-age=300' },
      });
    }

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
    // Test-panel marks, kept server-side. The panel itself stays public and
    // localStorage-only for anyone who opens it; WRITING marks needs the
    // 'key:tests' value from KV, which lives only in the tester's own link
    // (/tests?k=...). No key -> 404, so the path stays invisible. Without this
    // the marks lived in one browser: a phone tick was unreadable on the
    // computer, and clearing site data threw a whole test session away.
    //
    // State is one JSON blob: { "T-19": {"v":"pass","n":"...","t":<ms>}, ... }.
    // Both devices send FULL snapshots, so the merge is per row by its own
    // timestamp - a stale snapshot from a tab left open yesterday must not
    // undo a mark made on the phone a minute ago.
    if (path === '/tests/state') {
      const want = String((await env.FEEDBACK.get('key:tests')) || '').trim();
      const got = (url.searchParams.get('k') || '').trim();
      if (!want || got !== want) return new Response('Not found', { status: 404 });

      const asJson = (obj) => new Response(JSON.stringify(obj), {
        headers: { 'Content-Type': 'application/json;charset=utf-8', 'Cache-Control': 'no-store' },
      });
      const readState = async () => {
        try {
          const v = JSON.parse((await env.FEEDBACK.get('tests:state')) || '{}');
          return (v && typeof v === 'object' && !Array.isArray(v)) ? v : {};
        } catch (e) { return {}; }
      };

      if (request.method === 'GET') return asJson(await readState());

      if (request.method === 'POST') {
        const body = await request.text();
        if (body.length > 262144) return new Response('Too large', { status: 413 });
        let incoming;
        try { incoming = JSON.parse(body); } catch (e) { return new Response('Bad JSON', { status: 400 }); }
        if (!incoming || typeof incoming !== 'object' || Array.isArray(incoming)) {
          return new Response('Bad JSON', { status: 400 });
        }
        // The merge rule itself lives in state.mjs and is unit-tested there
        // (test/state.test.mjs) - it is the part that can silently lose a
        // testing session, so it must not be checkable only by hand.
        const merged = mergeState(await readState(), incoming);
        if (Object.keys(merged).length > 400) return new Response('Too many rows', { status: 413 });
        await env.FEEDBACK.put('tests:state', JSON.stringify(merged));
        return asJson(merged);
      }
      return new Response('Method not allowed', { status: 405 });
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

    // eInkWeather vakarinio klausimo drabuziu deriniu paveikslai (KV raktas oi:<code>).
    // Telegram sendPhoto atsisiunciamas per URL, tad irenginiui nereikia PNG kodavimo.
    // Deriniai is anksto sugeneruoti (gencombos.py -> wrangler kv bulk put oi_bulk.json).
    if (request.method === 'GET' && path.startsWith('/oi/')) {
      const code = path.slice(4).replace(/\.png$/, '');
      const png = await env.FEEDBACK.get('oi:' + code, 'arrayBuffer');
      if (!png) return new Response('not found', { status: 404 });
      return new Response(png, {
        headers: { 'Content-Type': 'image/png', 'Cache-Control': 'public, max-age=86400' },
      });
    }

    // The demo and the manual live on gh-pages, but the apex is not a GitHub
    // Pages site (no CNAME - Cloudflare serves it), so only the paths this
    // worker owns exist there: tinymakerwifi.com/demo/ and /manual/ were 404s
    // while the github.io ones worked. Every link we hand out should be on our
    // own domain rather than spelling out someone else's hosting - and the
    // trailing images/CSS under those paths have to come along, so this proxies
    // the subtree, not just the page. /extension/ is the Chrome toolbar extension's
    // page (the dashboard's "Browser extension" link points at it).
    //
    // The extension is in the Chrome Web Store since 2026-09-25 and the manual zip
    // install is gone (V). The zip links still sit in the 09-21 FB post and older
    // READMEs, so they lead to the store instead of a 404.
    if (/^\/extension\/tinymaker-chrome[^/]*\.zip$/.test(path)) {
      return Response.redirect(EXT_STORE, 301);
    }
    //
    // /resin/ carries both: the printer fetches manifest.json and the profiles
    // from it, and index.html is the same data written for a person - the page
    // a resin maker is handed, since they have no TinyMaker to open a dashboard
    // on. One folder, because it is one subject.
    if (request.method === 'GET' &&
        /^\/(demo|manual|roadmap|extension|resin)(\/|$)/.test(path)) {
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
        if (!safeImgType(p.type))
          return new Response('photos only (png, jpeg, webp, gif)', { status: 415, headers: CORS });
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
      // 0-7 ticket: an unguessable token the submitter keeps. It buys one
      // extra KV write per note (tok: -> fb: pointer) - accounted for in the
      // daily-cap math, which already assumed ~3 writes plus photos.
      const tok = crypto.randomUUID();
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
        tok,
      };
      await env.FEEDBACK.put('fb:' + stamp + ':' + id, JSON.stringify(rec),
                             { metadata: metaOf(rec) });
      await env.FEEDBACK.put('tok:' + tok, 'fb:' + stamp + ':' + id);
      return new Response(JSON.stringify({ ok: true, id: num, photos: imgKeys.length, token: tok }), {
        headers: { 'Content-Type': 'application/json', ...CORS },
      });
    }

    // Slicer defect markers from the printer's own dashboard (0.17).
    //
    // Its own route rather than /feedback, for two reasons that are not style:
    // the dashboard is served over http from the printer's LAN address, so the
    // public form's CORS (locked to tinymakerwifi.com) would block it; and there
    // is no Turnstile widget on a page that may have no internet at all. Both
    // guards stay exactly as they are for the public form - this door is a
    // different door, with its own lock: one report per minute per IP and a
    // daily cap well under the form's.
    //
    // The record lands in the SAME inbox in the SAME shape, pre-tagged 'bug', so
    // "what is new in feedback" picks it up with no new machinery anywhere.
    if (path === '/slicerbug' && request.method === 'OPTIONS')
      return new Response(null, { headers: WIDE_CORS });

    if (path === '/slicerbug' && request.method === 'POST') {
      let fields = {}, photo = null;
      const ct = request.headers.get('Content-Type') || '';
      try {
        if (ct.includes('multipart/form-data')) {
          const fd = await request.formData();
          for (const [k, v] of fd.entries()) {
            if (k === 'photo' && typeof v === 'object' && v.size) { if (!photo) photo = v; }
            else fields[k] = String(v);
          }
        } else {
          fields = await request.json();
        }
      } catch (e) {
        return new Response('bad body', { status: 400, headers: WIDE_CORS });
      }

      const msg = str(fields.message, 8000).trim();   // a marker report is longer than a note
      if (!msg) return new Response('empty', { status: 400, headers: WIDE_CORS });
      if (photo) {
        // Tighter than the public form on purpose: this door has no Turnstile,
        // so its worst case has to be small. A 3D snapshot is tens of KB.
        if (photo.size > MAX_SLICERBUG_PHOTO_BYTES)
          return new Response('photo too large', { status: 413, headers: WIDE_CORS });
        if (!safeImgType(photo.type))
          return new Response('photos only (png, jpeg, webp, gif)', { status: 415, headers: WIDE_CORS });
      }

      const ip = request.headers.get('CF-Connecting-IP') || 'x';
      if (await env.FEEDBACK.get('sbgate:' + ip))
        return new Response('one report a minute - the last one arrived', { status: 429, headers: WIDE_CORS });

      // Two counters: its own (a bug hunt is bursty) and the shared daily one,
      // because the thing actually being protected is the KV write budget.
      const day = new Date().toISOString().slice(0, 10);
      const sbKey = `sbday:${day}`, allKey = `day:${day}`;
      const [sbUsed, allUsed] = await Promise.all([
        env.FEEDBACK.get(sbKey).then((v) => Number(v) || 0),
        env.FEEDBACK.get(allKey).then((v) => Number(v) || 0),
      ]);
      if (sbUsed >= MAX_SLICERBUG_PER_DAY)
        return new Response('that is a lot of markers for one day - use Copy and open a GitHub issue', { status: 429, headers: WIDE_CORS });
      if (allUsed >= MAX_PER_DAY)
        return new Response('the inbox is over its daily limit - try tomorrow', { status: 503, headers: WIDE_CORS });

      await env.FEEDBACK.put('sbgate:' + ip, '1', { expirationTtl: 60 });
      await env.FEEDBACK.put(sbKey, String(sbUsed + 1), { expirationTtl: DAY_TTL });
      await env.FEEDBACK.put(allKey, String(allUsed + 1), { expirationTtl: DAY_TTL });

      const stamp = new Date().toISOString();
      const id = crypto.randomUUID().slice(0, 8);
      const num = (Number(await env.FEEDBACK.get('seq')) || 0) + 1;
      await env.FEEDBACK.put('seq', String(num));
      const imgKeys = [];
      if (photo) {
        const k = 'img:' + stamp + ':' + id + ':0';
        await env.FEEDBACK.put(k, await photo.arrayBuffer(),
                               { metadata: { ct: photo.type, size: photo.size } });
        imgKeys.push(k);
      }
      const rec = {
        num,
        message: msg,
        contact: '',
        fw: str(fields.fw, 20),
        build: str(fields.build, 20),
        ua: str(fields.ua, 120),
        src: 'slicer',
        // Pre-tagged: this door only opens for one kind of thing, so making a
        // human classify it later would be busywork.
        tag: 'bug',
        handled: false,
        photos: imgKeys,
        at: stamp,
      };
      await env.FEEDBACK.put('fb:' + stamp + ':' + id, JSON.stringify(rec),
                             { metadata: metaOf(rec) });
      // Deliberately no case number in the answer. This route replies to ANY
      // origin, and `num` is the project-wide counter shared with /feedback -
      // any web page could otherwise poll it to watch how much mail we get.
      return new Response(JSON.stringify({ ok: true, photos: imgKeys.length }), {
        headers: { 'Content-Type': 'application/json', ...WIDE_CORS },
      });
    }

    // Firmware crash telemetry (GitHub #70): anonymous, opt-out on the device.
    // NOT a browser form -> no Turnstile; the firmware de-dupes per crash event,
    // and here a per-device 60 s gate + a daily cap + a bounded JSON parse keep a
    // crashlooping or hostile device from flooding KV. No IP, no personal data -
    // just a hashed device id + the ESP reset reason (+ optional layer/epoch).
    if (path === '/crash' && request.method === 'POST') {
      let f = {};
      try { f = await request.json(); } catch (e) {
        return new Response('bad body', { status: 400 });
      }
      const id = str(f.id, 64);
      const reason = str(f.reason, 48);
      if (!id || !reason) return new Response('bad', { status: 400 });

      const gate = 'cgate:' + id;                 // 1 crash / 60 s / device
      if (await env.FEEDBACK.get(gate))
        return new Response('slow down', { status: 429 });
      const day = new Date().toISOString().slice(0, 10);
      const allKey = `cday:${day}`;
      const allUsed = Number(await env.FEEDBACK.get(allKey)) || 0;
      if (allUsed >= MAX_CRASH_PER_DAY)
        return new Response('over daily limit', { status: 503 });
      await env.FEEDBACK.put(gate, '1', { expirationTtl: 60 });
      await env.FEEDBACK.put(allKey, String(allUsed + 1), { expirationTtl: DAY_TTL });

      const stamp = new Date().toISOString();
      const rec = {
        id,
        version: str(f.version, 20),
        reason,
        layer: Math.max(0, Math.min(60000, Number(f.layer) || 0)),
        epoch: Math.max(0, Number(f.epoch) || 0),
        at: stamp,
        // 0.18.1 coredump backtrace. All optional - older firmware omits them.
        task: str(f.task, 16),
        pc: str(f.pc, 8),
        cause: Math.max(0, Number(f.cause) || 0),
        vaddr: str(f.vaddr, 8),
        bt: str(f.bt, 160),
        // 0.18.3: first 16 hex of the firmware.elf sha of the build that crashed
        // (from the coredump) and of the build reporting it - see builds.json.
        elf: hexStr(f.elf, 64),
        run: hexStr(f.run, 64),
        // 0.18.4 stack evidence: A0/A1 (return address, stack pointer) from the
        // coredump and the lowest address + size of the reporting boot's loopTask stack.
        a0: hexStr(f.a0, 8),
        sp: hexStr(f.sp, 8),
        stk: hexStr(f.stk, 8),
        stksz: Math.max(0, Math.min(1 << 20, Number(f.stksz) || 0)),
        btbad: f.btbad ? 1 : 0,
      };
      await env.FEEDBACK.put('crash:' + stamp + ':' + id.slice(0, 8),
                             JSON.stringify(rec),
                             { metadata: { reason: rec.reason, version: rec.version },
                               expirationTtl: CRASH_TTL });
      return new Response(JSON.stringify({ ok: true }),
                         { headers: { 'Content-Type': 'application/json' } });
    }

    // 0-7 PUBLIC ticket status: the token IS the authorisation - unguessable
    // (uuid), and a wrong one answers 404 with no timing-relevant difference.
    // Only status fields go back out - never the message or the contact (a
    // leaked link should not leak what was written), and the verdict only for
    // token-era notes (see the visibility rule up top).
    if (path === '/feedback/status' && request.method === 'GET') {
      const t = (url.searchParams.get('t') || '').trim();
      // ACAO:* here is deliberate (unlike the key-gated routes): read-only
      // public-by-token data, and the form's gh-pages fallback origin needs it.
      const noStore = { 'Content-Type': 'application/json', 'Cache-Control': 'no-store',
                        'Access-Control-Allow-Origin': '*' };
      const miss = () => new Response('{"error":"unknown ticket"}', { status: 404, headers: noStore });
      if (!/^[0-9a-f-]{36}$/.test(t)) return miss();
      const fbKey = await env.FEEDBACK.get('tok:' + t);
      const v = fbKey && await env.FEEDBACK.get(fbKey);
      if (!v) return miss();
      const rec = JSON.parse(v);
      return new Response(JSON.stringify({
        ok: true, num: rec.num, at: rec.at, fw: rec.fw || '',
        tag: rec.tag || '', handled: !!rec.handled,
        verdict: rec.tok ? (rec.verdict || '') : '', verdictAt: rec.tok ? (rec.verdictAt || '') : '',
      }), { headers: noStore });
    }

    // 0-7 PUBLIC recently-handled list: metadata only (one KV list call, no
    // record reads, no verdict text), capped at 5 and edge-cached so the form
    // page can show it on every load without touching the read budget.
    if (path === '/feedback/recent' && request.method === 'GET') {
      const rows = [];
      let cursor;
      do {
        const page = await env.FEEDBACK.list({ prefix: 'fb:', limit: 1000, cursor });
        for (const k of page.keys) rows.push({ key: k.name, m: k.metadata || {} });
        cursor = page.list_complete ? null : page.cursor;
      } while (cursor);
      rows.reverse();
      const out = [];
      for (const e of rows) {
        if (!e.m.handled) continue;
        out.push({ num: e.m.n || 0, tag: e.m.tag || '', fw: e.m.fw || '', at: e.key.slice(3, 13) });
        if (out.length >= 5) break;
      }
      return new Response(JSON.stringify(out), {
        headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=300',
                   'Access-Control-Allow-Origin': '*' },
      });
    }

    // trim(): a secret typed at a Windows prompt can arrive with a stray \r,
    // which would silently fail every key comparison.
    const listKey = String(env.LIST_KEY || '').trim();
    const keyOk = listKey && (url.searchParams.get('key') || '').trim() === listKey;

    // Private internal plan (planas.html), pushed to panel:plan KV. Gated by the
    // same admin LIST_KEY; 404 (not 403) without it so the path stays invisible.
    // NOT a public roadmap - holds internal strategy; only V opens it by URL.
    if (request.method === 'GET' && path === '/plan') {
      if (!keyOk) return new Response('Not found', { status: 404 });
      const html = await env.FEEDBACK.get('panel:plan');
      if (!html) return new Response('No plan uploaded yet', { status: 404 });
      return new Response(html, {
        headers: { 'Content-Type': 'text/html;charset=utf-8', 'Cache-Control': 'no-cache' },
      });
    }

    // Ops "farm map" (how-we-work reference: rules by trigger, farm-map.json).
    // Same LIST_KEY as /plan so one key opens both, and plan<->map links carry it.
    // Stable reference (separate from the changing plan); 404 without the key.
    if (request.method === 'GET' && path === '/map') {
      if (!keyOk) return new Response('Not found', { status: 404 });
      const html = await env.FEEDBACK.get('panel:map');
      if (!html) return new Response('No map uploaded yet', { status: 404 });
      return new Response(html, {
        headers: { 'Content-Type': 'text/html;charset=utf-8', 'Cache-Control': 'no-cache' },
      });
    }

    // Team roadmap (the contributor-facing projection) from panel:team KV.
    // Gated by its OWN key in KV ('key:team') - deliberately NOT LIST_KEY,
    // because the team may hold this one while LIST_KEY also unlocks /plan
    // and the feedback admin. 404 without it so the path stays invisible.
    if (request.method === 'GET' && path === '/team') {
      const want = String((await env.FEEDBACK.get('key:team')) || '').trim();
      const got = (url.searchParams.get('key') || '').trim();
      if (!want || got !== want) return new Response('Not found', { status: 404 });
      const html = await env.FEEDBACK.get('panel:team');
      if (!html) return new Response('No team roadmap uploaded yet', { status: 404 });
      return new Response(html, {
        headers: { 'Content-Type': 'text/html;charset=utf-8', 'Cache-Control': 'no-cache' },
      });
    }

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

    // Telemetry heartbeat as data (for a device that shows crash status and must
    // tell "quiet" from "telemetry down"). now lets the caller compute the age
    // without a synced clock; heartbeat is null until the weekly cron first runs.
    if (path === '/crash/status' && keyOk) {
      const hb = await env.FEEDBACK.get('hb:last');
      return new Response(JSON.stringify({ heartbeat: hb || null, now: new Date().toISOString() }),
                         { headers: { 'Content-Type': 'application/json' } });
    }

    // Crash telemetry admin view (same LIST_KEY as the feedback inbox).
    if ((path === '/crash/inbox' || path === '/crash/list') && keyOk) {
      const names = [];
      let ccur;
      do {
        const page = await env.FEEDBACK.list({ prefix: 'crash:', limit: 1000, cursor: ccur });
        for (const k of page.keys) names.push(k.name);
        ccur = page.list_complete ? null : page.cursor;
      } while (ccur);
      names.reverse();                              // newest first
      const recs = [];
      for (const k of names.slice(0, 500)) {
        const v = await env.FEEDBACK.get(k);
        if (v) recs.push(JSON.parse(v));
      }
      if (path === '/crash/list')
        return new Response(JSON.stringify(recs, null, 1),
                           { headers: { 'Content-Type': 'application/json' } });
      const hb = await env.FEEDBACK.get('hb:last');
      // builds.json (release.py): firmware.elf sha of every release -> version.
      let builds = {};
      try {
        const b = await fetch(GHPAGES + '/builds.json', { cf: { cacheTtl: 300 } });
        if (b.ok) builds = await b.json();
      } catch (e) { /* page still renders, just without the Build column filled */ }
      return new Response(crashInboxPage(recs, hb, builds),
                         { headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' } });
    }

    // Fleet stats page (key-gated, next to the crash view). V opens it by URL with
    // ?key=; its client JS carries the key to /fleet/history below.
    if (request.method === 'GET' && (path === '/fleet' || path === '/fleet/')) {
      if (!keyOk) return new Response('Not found', { status: 404 });
      return new Response(fleetPage(), {
        headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' },
      });
    }

    // Fleet version history (key-gated, same LIST_KEY as the crash view). Daily
    // vh:<date> snapshots of the public /stats aggregate. ?range=D|W|M returns
    // {now, ref} (both with .date) for a since-X delta; no range returns the
    // snapshot list (newest first) for the page's graphs. The RLCD device reads
    // this with the key, or keeps its own NVS trend.
    if (path === '/fleet/history' && keyOk) {
      const range = (url.searchParams.get('range') || '').toUpperCase();
      const dates = [];
      let vcur;
      do {
        const page = await env.FEEDBACK.list({ prefix: 'vh:', limit: 1000, cursor: vcur });
        for (const k of page.keys) dates.push(k.name.slice(3));
        vcur = page.list_complete ? null : page.cursor;
      } while (vcur);
      dates.sort();                                    // oldest -> newest
      const jhead = { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' };
      const read = async (dt) => { const v = await env.FEEDBACK.get('vh:' + dt); return v ? JSON.parse(v) : null; };
      if (range === 'D' || range === 'W' || range === 'M') {
        if (!dates.length) return new Response('{"now":null,"ref":null}', { headers: jhead });
        const now = await read(dates[dates.length - 1]);
        const back = range === 'D' ? 1 : range === 'W' ? 7 : 30;
        const target = new Date(Date.parse(now.date + 'T00:00:00Z') - back * 86400000).toISOString().slice(0, 10);
        let refDate = dates[0];                         // oldest we have, if nothing older than target
        for (const dt of dates) { if (dt <= target) refDate = dt; else break; }
        return new Response(JSON.stringify({ now, ref: await read(refDate) }), { headers: jhead });
      }
      const pick = dates.slice(-120);
      const snaps = [];
      for (const dt of pick) { const s = await read(dt); if (s) snaps.push(s); }
      snaps.reverse();                                  // newest first
      return new Response(JSON.stringify({ snapshots: snaps }), { headers: jhead });
    }

    // Release metadata for the fleet page ONLY (date + stable/beta per version,
    // latest of each, and the set of real shipped versions to flag ghosts like a
    // device reporting a nonexistent 0.18.13). Kept SEPARATE from /fleet/history
    // on purpose: that endpoint the RLCD device reads stays byte-for-byte unchanged.
    // Both fetches are fail-safe and edge-cached; on failure the page just omits
    // dates/pills/ghost-split. GitHub lists newest-first, so the first non-prerelease
    // is the latest stable and the first prerelease is the latest beta.
    if (path === '/fleet/releases' && keyOk) {
      const out = { map: {}, latestStable: null, latestBeta: null, real: [] };
      try {
        const rr = await fetch('https://api.github.com/repos/slibbinas/TinyMakerWifi/releases?per_page=100',
          { headers: { 'User-Agent': 'tinymaker-fleet', 'Accept': 'application/vnd.github+json' }, cf: { cacheTtl: 3600 } });
        if (rr.ok) {
          for (const r of await rr.json()) {
            const v = String(r.tag_name || '').replace(/^v/, '');
            if (!v) continue;
            out.map[v] = { date: String(r.published_at || '').slice(0, 10), beta: !!r.prerelease };
            if (r.prerelease) { if (!out.latestBeta) out.latestBeta = v; }
            else if (!out.latestStable) out.latestStable = v;
          }
        }
      } catch (e) { /* page falls back to no dates/pills */ }
      try {
        const br = await fetch(GHPAGES + '/builds.json', { cf: { cacheTtl: 3600 } });
        if (br.ok) out.real = [...new Set(Object.values(await br.json()))];
      } catch (e) { /* ghost-split just won't apply */ }
      return new Response(JSON.stringify(out),
        { headers: { 'Content-Type': 'application/json', 'Cache-Control': 'public, max-age=3600' } });
    }

    // Manual snapshot now (bootstrap / test), so the history does not wait for the
    // first daily cron.
    if (path === '/fleet/snap' && keyOk) {
      await snapshotFleet(env, Date.now());
      return new Response('{"ok":true}', { headers: { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' } });
    }

    if (path === '/feedback/img' && keyOk) {
      const k = url.searchParams.get('k') || '';
      if (!k.startsWith('img:')) return new Response('bad key', { status: 400 });
      const { value, metadata } = await env.FEEDBACK.getWithMetadata(k, { type: 'arrayBuffer' });
      if (!value) return new Response('not found', { status: 404 });
      // Second lock on the same door: anything stored before the intake check
      // existed, or stored with a lying type, is served as a download and never
      // as a document. nosniff stops the browser from "helpfully" deciding.
      const ct = metadata && metadata.ct;
      return new Response(value, {
        headers: {
          'Content-Type': safeImgType(ct) ? ct : 'application/octet-stream',
          'X-Content-Type-Options': 'nosniff',
          'Content-Disposition': 'inline',
          'Content-Security-Policy': "default-src 'none'; sandbox",
        },
      });
    }

    return new Response('TinyMakerWifi feedback collector', { status: 200 });
  },

  // Weekly heartbeat (cron in wrangler.jsonc). Stamps hb:last so /crash/inbox can
  // show "telemetry alive" - an empty crash list then means "no crashes", not
  // "the pipeline is dead". Proves the worker + KV run on schedule.
  async scheduled(event, env, ctx) {
    ctx.waitUntil((async () => {
      await env.FEEDBACK.put('hb:last', new Date(event.scheduledTime).toISOString());
      await snapshotFleet(env, event.scheduledTime);   // daily version snapshot (idempotent per day)
    })());
  },
};

// ------------------------------------------------------------- fleet snapshot
// Daily snapshot of the public install-stats aggregate, stored under
// vh:<YYYY-MM-DD> so /fleet/history can show how the version split moves over
// days/weeks/months. The install-stats worker itself keeps no history (only the
// current per-device record), so the trend has to be captured here, going
// forward - there is no past to backfill. Idempotent: overwrites today's key,
// so running on both the weekly and daily cron is harmless. Best-effort: a
// failed fetch skips today rather than storing a hole.
const STATS_URL = 'https://tinymaker-stats.slibbinas.workers.dev/stats';
async function snapshotFleet(env, ms) {
  let d;
  try {
    const r = await fetch(STATS_URL, { cf: { cacheTtl: 0 } });
    if (!r.ok) return;
    d = await r.json();
  } catch (e) { return; }
  if (!d || typeof d.printers !== 'number' || !d.by_version || typeof d.by_version !== 'object') return;
  const date = new Date(ms).toISOString().slice(0, 10);
  await env.FEEDBACK.put('vh:' + date, JSON.stringify({ date, printers: d.printers, by_version: d.by_version }),
                         { metadata: { p: d.printers } });
  // Keep ~400 daily snapshots (each tiny); drop the oldest beyond that.
  try {
    const keys = [];
    let cur;
    do {
      const page = await env.FEEDBACK.list({ prefix: 'vh:', limit: 1000, cursor: cur });
      for (const k of page.keys) keys.push(k.name);
      cur = page.list_complete ? null : page.cursor;
    } while (cur);
    keys.sort();                                     // vh:YYYY-MM-DD sorts chronologically
    for (const old of keys.slice(0, Math.max(0, keys.length - 400))) await env.FEEDBACK.delete(old);
  } catch (e) { /* pruning is best-effort */ }
}

// ---------------------------------------------------------------- fleet page
// Key-gated version-history dashboard (next to the crash view). All data comes
// from /fleet/history (same key, read from the URL); nothing sensitive is baked
// into the HTML, so this is a static shell filled in the browser.
const fleetPage = () => `<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex,nofollow"><title>TinyMaker printer fleet</title>
<script>(function(){try{var q=new URLSearchParams(location.search).get('theme');var t=(q==='light'||q==='dark')?q:localStorage.getItem('tmTheme');if(t==='light'||t==='dark')document.documentElement.setAttribute('data-theme',t);}catch(e){}})()</script>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/><rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/><rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/><path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/></svg>">
<style>
:root{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--accent:#e8720c;--pill:#2a2a2e;--up:#2fbf4f;--dn:#e05555}
@media(prefers-color-scheme:light){:root{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--up:#2f8f4f;--dn:#c93b45}}
:root[data-theme=light]{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--up:#2f8f4f;--dn:#c93b45}
:root[data-theme=dark]{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--pill:#2a2a2e;--up:#2fbf4f;--dn:#e05555}
*{box-sizing:border-box}body{font:14px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:var(--bg);color:var(--text);padding:20px}
.wrap{max-width:860px;margin:0 auto;border:1px solid var(--line);border-radius:10px;background:var(--card);padding:18px 22px;position:relative}
h1{font-size:19px;color:var(--accent);margin:0 0 4px;display:flex;align-items:center;gap:8px}h1 .mark{width:22px;height:22px;flex:none}
.sub{color:var(--muted);font-size:13px;margin-bottom:14px}
.tmcrumb{margin:2px 0 16px;font-size:12px;color:var(--muted)}.tmcrumb a{color:var(--accent);text-decoration:none}
.themeSw{position:absolute;top:14px;right:18px;display:inline-flex;gap:1px;align-items:center}
.themeSw button{background:none;border:0;padding:5px;margin:0;cursor:pointer;color:var(--muted);line-height:0;border-radius:7px}
.themeSw button svg{width:17px;height:17px;display:block;stroke:currentColor;fill:none;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.themeSw button:hover{color:var(--text)}.themeSw button[aria-pressed=true]{color:var(--accent)}
.cards{display:flex;gap:10px;flex-wrap:wrap;margin:0 0 18px}
.kpi{background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:12px 14px;min-width:110px;flex:1}
.kpi .n{font-size:24px;font-weight:700}.kpi .l{color:var(--muted);font-size:12px}
.card{background:var(--bg);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin-bottom:16px}.card h2{font-size:15px;margin:0 0 10px}
table{width:100%;border-collapse:collapse}th,td{text-align:right;padding:6px 8px;border-bottom:1px solid var(--line);font-variant-numeric:tabular-nums;font-size:13px}
th:first-child,td:first-child{text-align:left}th{color:var(--muted);font-weight:600;font-size:12px}
.up{color:var(--up)}.dn{color:var(--dn)}.z{color:var(--muted)}
.bar{height:9px;border-radius:99px;background:var(--line);overflow:hidden}.bar span{display:block;height:100%;background:var(--accent)}
button{border:1px solid var(--line);background:var(--bg);color:var(--text);border-radius:8px;padding:7px 12px;font:inherit;cursor:pointer}button.pri{background:var(--accent);color:#fff;border-color:var(--accent)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.msg{color:var(--muted);font-size:13px}.err{color:var(--dn)}
a{color:var(--accent)}svg{display:block;max-width:100%}
.foot{color:var(--muted);font-size:12px;margin-top:20px;display:flex;justify-content:space-between;gap:8px;flex-wrap:wrap}
</style></head>
<body><div class="wrap"><p class="tmcrumb"><a href="https://tinymakerwifi.com/">TinyMakerWifi</a> &rsaquo; Printer fleet</p><div class="themeSw" role="group" aria-label="Theme"><button data-m="system" title="System" aria-label="System theme"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8M12 17v4"/></svg></button><button data-m="light" title="Light" aria-label="Light theme"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4.2"/><path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M19.1 4.9l-1.8 1.8M6.7 17.3l-1.8 1.8"/></svg></button><button data-m="dark" title="Dark" aria-label="Dark theme"><svg viewBox="0 0 24 24"><path d="M21 12.8A8.5 8.5 0 1 1 11.2 3a6.6 6.6 0 0 0 9.8 9.8z"/></svg></button></div><h1><svg class="mark" viewBox="0 0 64 64" aria-hidden="true"><rect x="8" y="40" width="48" height="9" rx="3" fill="#e8720c"/><rect x="14" y="27" width="36" height="9" rx="3" fill="#e8720c" opacity=".75"/><rect x="20" y="14" width="24" height="9" rx="3" fill="#e8720c" opacity=".5"/><path d="M22 6 A14 14 0 0 1 42 6" fill="none" stroke="#4da3ff" stroke-width="5" stroke-linecap="round"/></svg>Printer fleet</h1>
  <div class="sub">Anonymous install stats over time - daily snapshots of the public aggregate. <span id="asof"></span></div>
  <div class="cards" id="kpis"></div>
  <div class="card"><h2>Versions</h2><div id="vertbl"><div class="msg">Loading...</div></div></div>
  <div class="card"><h2>Printers over time</h2><div id="chart"><div class="msg">Loading...</div></div></div>
  <div class="row" style="margin-bottom:8px">
    <button class="pri" id="snap">Snapshot now</button>
    <button id="reload">Reload</button>
    <span class="msg" id="snapmsg"></span>
  </div>
  <div class="foot"><a id="crashlink" href="#">Crash telemetry</a><span id="cnt"></span></div>
</div>
<script>
const KEY=new URLSearchParams(location.search).get('key')||'';
const $=id=>document.getElementById(id);
const esc=s=>{const d=document.createElement('div');d.textContent=s==null?'':String(s);return d.innerHTML;};
const api=p=>fetch(p+(p.includes('?')?'&':'?')+'key='+encodeURIComponent(KEY),{cache:'no-store'}).then(r=>r.json());
function refAt(snaps,days){ // snaps newest-first; latest date minus 'days', closest on/before
  if(!snaps.length)return null;
  const t=new Date(Date.parse(snaps[0].date+'T00:00:00Z')-days*864e5).toISOString().slice(0,10);
  let ref=snaps[snaps.length-1];
  for(const s of snaps){ if(s.date<=t){ref=s;break;} }
  return ref;
}
function delta(now,ref){ // signed html
  if(now==null||ref==null)return '<span class="z">-</span>';
  const d=now-ref; if(d===0)return '<span class="z">0</span>';
  return '<span class="'+(d>0?'up':'dn')+'">'+(d>0?'+':'')+d+'</span>';
}
// Sortable version table. Default: newest version first. Click a header to sort by
// that column (click again flips direction). Unknown/empty values sort to the end.
let _vt=null,_sort={key:'ver',dir:'desc'};
function svCmp(a,b){const pa=String(a).split('.').map(x=>parseInt(x,10)||0),pb=String(b).split('.').map(x=>parseInt(x,10)||0);for(let i=0;i<Math.max(pa.length,pb.length);i++){const d=(pa[i]||0)-(pb[i]||0);if(d)return d;}return 0;}
function drawVerTable(){
  if(!_vt)return;
  const bv=_vt.bv,relMap=_vt.relMap,rel=_vt.rel,rD=_vt.rD,rW=_vt.rW,rM=_vt.rM;
  const gv=(s,v)=>s&&s.by_version?(s.by_version[v]!=null?s.by_version[v]:0):null;
  const realSet=rel?new Set([...Object.keys(relMap),...(rel.real||[])]):null;
  const allVers=Object.keys(bv);
  const vers=realSet?allVers.filter(v=>realSet.has(v)):allVers.slice(); // real releases
  const ghosts=realSet?allVers.filter(v=>!realSet.has(v)):[];           // e.g. a bogus 0.18.13
  const max=Math.max(1,...vers.map(v=>bv[v]));
  const dnum=(v,ref)=>ref==null?null:(bv[v]-ref);
  const k=_sort.key,dir=_sort.dir,sgn=dir==='desc'?-1:1;
  vers.sort((a,b)=>{
    if(k==='ver')return (dir==='desc'?-1:1)*svCmp(a,b);
    let x,y;
    if(k==='rel'){x=(relMap[a]&&relMap[a].date)||'';y=(relMap[b]&&relMap[b].date)||'';}
    else if(k==='now'){x=bv[a];y=bv[b];}
    else if(k==='d'){x=dnum(a,gv(rD,a));y=dnum(b,gv(rD,b));}
    else if(k==='w'){x=dnum(a,gv(rW,a));y=dnum(b,gv(rW,b));}
    else if(k==='m'){x=dnum(a,gv(rM,a));y=dnum(b,gv(rM,b));}
    else return 0;
    const an=(x==null||x===''),bn=(y==null||y==='');
    if(an&&bn)return -svCmp(a,b); if(an)return 1; if(bn)return -1;   // empties last
    return sgn*(x<y?-1:x>y?1:0);
  });
  const pill=(txt,bg)=>' <span style="display:inline-block;padding:1px 7px;border-radius:999px;font-size:11px;font-weight:700;background:'+bg+';color:#fff;vertical-align:middle">'+txt+'</span>';
  const arr=c=>k===c?(dir==='desc'?' ▾':' ▴'):'';
  const th=c=>{const lbl={ver:'Version',rel:'Released',now:'Now',d:'Day',w:'Week',m:'Month'}[c];return '<th data-k="'+c+'" style="cursor:pointer;user-select:none">'+lbl+arr(c)+'</th>';};
  let h='<table><thead><tr>'+th('ver')+th('rel')+th('now')+th('d')+th('w')+th('m')+'<th style="width:120px"></th></tr></thead><tbody>';
  for(const v of vers){
    let tag='';
    if(rel&&v===rel.latestStable)tag+=pill('Stable','var(--ok,#2e9b4e)');
    if(rel&&v===rel.latestBeta)tag+=pill('Beta','var(--accent)');
    const dt=(relMap[v]&&relMap[v].date)||'';
    h+='<tr><td>'+esc(v)+tag+'</td><td style="color:var(--muted);white-space:nowrap">'+esc(dt)+'</td><td>'+bv[v]+'</td><td>'+delta(bv[v],gv(rD,v))+'</td><td>'+delta(bv[v],gv(rW,v))+'</td><td>'+delta(bv[v],gv(rM,v))+
       '</td><td><div class="bar"><span style="width:'+Math.round(bv[v]/max*100)+'%"></span></div></td></tr>';
  }
  h+='</tbody></table>';
  if(ghosts.length){h+='<div class="msg" style="margin-top:8px">Unrecognized '+(ghosts.length>1?'versions':'version')+
     ' (not a published release - self-built or a version-read bug): '+ghosts.map(v=>esc(v)+' ('+bv[v]+')').join(', ')+'</div>';}
  const box=$('vertbl');box.innerHTML=h;
  box.querySelectorAll('th[data-k]').forEach(el=>el.onclick=()=>{const c=el.getAttribute('data-k');if(_sort.key===c)_sort.dir=_sort.dir==='desc'?'asc':'desc';else{_sort.key=c;_sort.dir='desc';}drawVerTable();});
  $('cnt').textContent=vers.length+' versions';
}
function render(hist,live,rel){
  const snaps=hist.snapshots||[]; // newest first
  const cur=live&&typeof live.printers==='number'?live:(snaps[0]||null);
  if(!cur){ $('vertbl').innerHTML='<div class="msg">No snapshots yet. Hit <b>Snapshot now</b> to seed the first one; a daily snapshot runs at 06:30 UTC.</div>'; $('chart').innerHTML=''; $('kpis').innerHTML=''; return; }
  const rD=refAt(snaps,1),rW=refAt(snaps,7),rM=refAt(snaps,30);
  // KPIs
  $('kpis').innerHTML=
    '<div class="kpi"><div class="n">'+cur.printers+'</div><div class="l">printers seen</div></div>'+
    '<div class="kpi"><div class="n">'+delta(cur.printers,rD&&rD.printers)+'</div><div class="l">day</div></div>'+
    '<div class="kpi"><div class="n">'+delta(cur.printers,rW&&rW.printers)+'</div><div class="l">week</div></div>'+
    '<div class="kpi"><div class="n">'+delta(cur.printers,rM&&rM.printers)+'</div><div class="l">month</div></div>';
  $('asof').textContent='As of '+(cur.date||'live')+(snaps.length?' | '+snaps.length+' daily snapshots':'');
  // version table - sortable (default newest version first); drawn by drawVerTable()
  _vt={bv:cur.by_version||{},relMap:(rel&&rel.map)||{},rel:rel,rD:rD,rW:rW,rM:rM};
  drawVerTable();
  // printers-over-time line
  if(snaps.length<2){ $('chart').innerHTML='<div class="msg">Chart needs at least two daily snapshots - it fills in over the coming days.</div>'; return; }
  const pts=snaps.slice().reverse(); // oldest first
  const W=800,H=180,pad=28;
  const xs=pts.map((_,i)=>pad+(W-2*pad)*(pts.length===1?0:i/(pts.length-1)));
  const mn=Math.min(...pts.map(p=>p.printers)),mx=Math.max(...pts.map(p=>p.printers));
  const yy=v=>H-pad-(H-2*pad)*(mx===mn?0.5:(v-mn)/(mx-mn));
  const path=pts.map((p,i)=>(i?'L':'M')+xs[i].toFixed(1)+' '+yy(p.printers).toFixed(1)).join(' ');
  let svg='<svg viewBox="0 0 '+W+' '+H+'" width="'+W+'" height="'+H+'">';
  svg+='<line x1="'+pad+'" y1="'+(H-pad)+'" x2="'+(W-pad)+'" y2="'+(H-pad)+'" stroke="var(--line)"/>';
  svg+='<path d="'+path+'" fill="none" stroke="var(--accent)" stroke-width="2"/>';
  pts.forEach((p,i)=>{svg+='<circle cx="'+xs[i].toFixed(1)+'" cy="'+yy(p.printers).toFixed(1)+'" r="2.5" fill="var(--accent)"/>';});
  svg+='<text x="'+pad+'" y="14" fill="var(--muted)" font-size="11">'+mx+'</text>';
  svg+='<text x="'+pad+'" y="'+(H-pad+14)+'" fill="var(--muted)" font-size="11">'+esc(pts[0].date)+'</text>';
  svg+='<text x="'+(W-pad)+'" y="'+(H-pad+14)+'" fill="var(--muted)" font-size="11" text-anchor="end">'+esc(pts[pts.length-1].date)+'</text>';
  svg+='</svg>';
  $('chart').innerHTML=svg;
}
async function load(){
  if(!KEY){ $('vertbl').innerHTML='<div class="err">Missing ?key=</div>'; return; }
  try{
    const hist=await api('/fleet/history');
    let live=null; try{ live=await fetch('https://tinymaker-stats.slibbinas.workers.dev/stats',{cache:'no-store'}).then(r=>r.json()); }catch(e){}
    let rel=null; try{ rel=await api('/fleet/releases'); }catch(e){}
    render(hist,live,rel);
  }catch(e){ $('vertbl').innerHTML='<div class="err">Load failed: '+esc(e.message||e)+'</div>'; }
}
$('reload').onclick=load;
$('snap').onclick=async()=>{ $('snapmsg').textContent='Snapshotting...'; try{ const r=await api('/fleet/snap'); $('snapmsg').textContent=r&&r.ok?'Snapshot saved.':'Failed.'; load(); }catch(e){ $('snapmsg').textContent='Failed: '+(e.message||e); } };
(function(){var sw=document.querySelector('.themeSw');if(sw){function mode(){var d=document.documentElement.getAttribute('data-theme');return d==='light'||d==='dark'?d:'system';}function mark(){var m=mode();sw.querySelectorAll('button').forEach(function(b){b.setAttribute('aria-pressed',String(b.dataset.m===m));});}sw.addEventListener('click',function(e){var b=e.target.closest('button[data-m]');if(!b)return;var m=b.dataset.m;if(m==='system'){document.documentElement.removeAttribute('data-theme');try{localStorage.removeItem('tmTheme');}catch(_){}}else{document.documentElement.setAttribute('data-theme',m);try{localStorage.setItem('tmTheme',m);}catch(_){}}mark();});mark();}var cl=document.getElementById('crashlink');if(cl)cl.href='/crash/inbox?key='+encodeURIComponent(KEY);})();
load();
</script>${TM_CLOSE}</body></html>`;

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

// Crash telemetry inbox (GitHub #70): read behind LIST_KEY. Anonymous rows -
// hashed device id + ESP reset reason (+ optional print layer). esc() on every
// field: reason/version come off the wire from firmware.
const crashInboxPage = (recs, hb, builds = {}) => {
  // Which build: the 16-hex elf sha prefix looked up in builds.json (every release
  // since the sha is in the image). No match = not a release; all zeros = unknown.
  const buildOf = (s) => {
    if (!s || s.length < 16) return '';          // missing or damaged - try the other sha
    if (/^0+$/.test(s)) return 'unknown';
    const hit = Object.keys(builds && typeof builds === 'object' ? builds : {}).find((k) => k.startsWith(s));
    return hit ? `v${builds[hit]} \u2713` : 'self-built \u26A0';
  };
  // A coredump from another build than the reporter's = an old crash that sat in
  // flash until the first 0.18.1+ boot read it (older firmware never did). The
  // reset reason then belongs to THIS boot (usually the restart after the update),
  // not to the crash - 2026-09-27/28 two "software restart" rows carried 0.14.3
  // and 0.16.0 panics. Show them as their own reason so they are not taken for
  // fresh crashes of the running version.
  const hex16 = (s) => (s && s.length >= 16 && !/^0+$/.test(s) ? s.slice(0, 16) : '');
  const earlier = (r) => !!(hex16(r.elf) && hex16(r.run) && hex16(r.elf) !== hex16(r.run));
  const reasonOf = (r) => (earlier(r) ? 'crash (earlier)' : r.reason);
  const counts = {};
  for (const r of recs) counts[reasonOf(r)] = (counts[reasonOf(r)] || 0) + 1;
  const order = Object.entries(counts).sort((a, b) => b[1] - a[1]);
  const rIdx = {};
  order.forEach(([k], i) => { rIdx[k] = i; });
  // Pills double as a reason filter (client-side, see the script at the bottom):
  // clicking one shows only that reason, clicking it again (or "All") restores.
  const summary = recs.length
    ? `<span class="pill act" data-r="all">All: <b>${recs.length}</b></span> `
      + order.map(([k, n], i) => `<span class="pill" data-r="${i}">${esc(k)}: <b>${n}</b></span>`).join(' ')
    : '<span class="sub">no reports</span>';
  // Weekly heartbeat health line: fresh (<=10d) green, stale red, none amber.
  let hbLine;
  if (hb) {
    const days = (Date.now() - new Date(hb).getTime()) / 86400000;
    const fresh = days <= 10;
    hbLine = `<div style="font-size:12px;margin:2px 0 12px;color:${fresh ? '#2fbf4f' : '#e24b4a'}">&#9679; Telemetry heartbeat: ${when(hb)}${fresh ? ' (weekly, healthy)' : ' &mdash; STALE, check the worker cron'}</div>`;
  } else {
    hbLine = `<div style="font-size:12px;margin:2px 0 12px;color:#e8a020">&#9679; Telemetry heartbeat: none yet (weekly cron)</div>`;
  }
  const rows = recs.map((r) => {
    // 0.18.1+ coredump: pc / cause / faulting address / task, with the full
    // backtrace in the title for addr2line. The summary's pc is the frame PC - 3
    // (IDF core dump), so the faulting instruction is pc + 3 - 2026-09-25's
    // 0x400803bd was _DoubleExceptionVector (0x400803c0).
    const hex8 = (n) => (n >>> 0).toString(16).padStart(8, '0');
    const pcHit = r.pc ? hex8(parseInt(r.pc, 16) + 3) : '';
    // SP against the loopTask stack is only meaningful for a loopTask crash of the
    // build that reports it (the stack lands at the same address every boot of one
    // build, not across builds).
    let stackLine = '';
    if (r.sp && r.stk && r.stk !== '00000000') {
      stackLine = `\na0 0x${r.a0} · sp 0x${r.sp}`;
      if (r.task === 'loopTask' && !earlier(r)) {
        const lo = parseInt(r.stk, 16), sp = parseInt(r.sp, 16);
        // ESP32 internal DRAM is 0x3FFAE000..0x40000000; anything else is no stack.
        stackLine += (sp < 0x3FFAE000 || sp >= 0x40000000) ? ' · not a RAM address (corrupted stack pointer)'
          : sp < lo ? ` · ${lo - sp} B BELOW the loop stack (overflow)`
          : sp >= lo + r.stksz ? ' · outside the loop stack (corrupted)'
          : ` · ${sp - lo} B left of ${r.stksz}`;
      }
    }
    const crashCell = r.pc && r.pc !== '00000000'
      ? `<span class="mono" title="${esc(`cause ${r.cause} · vaddr 0x${r.vaddr} · task ${r.task} · fault pc 0x${pcHit}\nbt ${r.bt}${r.btbad ? ' (corrupted)' : ''}${stackLine}`)}">0x${esc(r.pc)}${r.bt ? ' &hellip;' : ''}</span>`
      : '';
    // The crashed build when there is a coredump (it can be older than the
    // reporter), else the reporting build; both shas in the tooltip.
    const b = buildOf(r.elf) || buildOf(r.run);
    const buildCell = b ? `<span title="${esc(`crashed: ${r.elf || '-'}\nreporting: ${r.run || '-'}`)}">${esc(b)}</span>` : '';
    const reasonCell = earlier(r)
      ? `<span title="${esc(`found in flash after an update; this boot: ${r.reason}`)}">crash (earlier)</span>`
      : esc(r.reason);
    return `<tr data-r="${rIdx[reasonOf(r)]}"><td>${when(r.at)}</td><td>${reasonCell}</td><td>${esc(r.version)}</td><td>${buildCell}</td>` +
      `<td>${r.layer ? esc(String(r.layer)) : ''}</td>` +
      `<td class="mono">${esc(String(r.id).slice(0, 8))}</td><td>${crashCell}</td></tr>`;
  }).join('');
  return `<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex,nofollow"><title>TinyMaker crash telemetry</title>
<script>(function(){try{var q=new URLSearchParams(location.search).get('theme');var t=(q==='light'||q==='dark')?q:localStorage.getItem('tmTheme');if(t==='light'||t==='dark')document.documentElement.setAttribute('data-theme',t);}catch(e){}})()</script>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/><rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/><rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/><path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/></svg>">
<style>
:root{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--accent:#e8720c;--pill:#2a2a2e;--mono:#84bcf8}
@media(prefers-color-scheme:light){:root{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--mono:#155fb0}}
:root[data-theme=light]{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--mono:#155fb0}
:root[data-theme=dark]{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--pill:#2a2a2e;--mono:#84bcf8}
body{font:14px/1.5 -apple-system,Segoe UI,Roboto,sans-serif;margin:0;background:var(--bg);color:var(--text);padding:20px}
h1{font-size:19px;color:var(--accent);margin:0 0 4px;display:flex;align-items:center;gap:8px}h1 .mark{width:22px;height:22px;flex:none}.sub{color:var(--muted);font-size:13px;margin-bottom:14px}
.pill{display:inline-block;background:var(--pill);border:1px solid var(--line);color:var(--text);border-radius:12px;padding:2px 10px;margin:2px;font-size:12px;cursor:pointer;user-select:none}.pill.act{background:var(--accent);border-color:var(--accent);color:#fff;font-weight:600}
table{border-collapse:collapse;width:100%;margin-top:14px}th,td{text-align:left;padding:7px 10px;border-bottom:1px solid var(--line);font-size:13px}
th{color:var(--muted);font-weight:600;font-size:12px}.mono{font-family:ui-monospace,monospace;color:var(--mono)}tr:hover td{background:var(--pill)}
.wrap{max-width:860px;margin:0 auto;border:1px solid var(--line);border-radius:10px;background:var(--card);padding:18px 22px;position:relative}
.themeSw{position:absolute;top:14px;right:18px;display:inline-flex;gap:1px;align-items:center}
.tmcrumb{margin:2px 0 16px;font-size:12px;color:var(--muted)}.tmcrumb a{color:var(--accent);text-decoration:none}
.themeSw button{background:none;border:0;padding:5px;margin:0;cursor:pointer;color:var(--muted);line-height:0;border-radius:7px}
.themeSw button svg{width:17px;height:17px;display:block;stroke:currentColor;fill:none;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.themeSw button:hover{color:var(--text)}
.themeSw button[aria-pressed=true]{color:var(--accent)}</style></head>
<body><div class="wrap"><p class="tmcrumb"><a href="https://tinymakerwifi.com/">TinyMakerWifi</a> &rsaquo; Crash telemetry</p><div class="themeSw" role="group" aria-label="Theme"><button data-m="system" title="System" aria-label="System theme"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8M12 17v4"/></svg></button><button data-m="light" title="Light" aria-label="Light theme"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4.2"/><path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M19.1 4.9l-1.8 1.8M6.7 17.3l-1.8 1.8"/></svg></button><button data-m="dark" title="Dark" aria-label="Dark theme"><svg viewBox="0 0 24 24"><path d="M21 12.8A8.5 8.5 0 1 1 11.2 3a6.6 6.6 0 0 0 9.8 9.8z"/></svg></button></div><h1><svg class="mark" viewBox="0 0 64 64" aria-hidden="true"><rect x="8" y="40" width="48" height="9" rx="3" fill="#e8720c"/><rect x="14" y="27" width="36" height="9" rx="3" fill="#e8720c" opacity=".75"/><rect x="20" y="14" width="24" height="9" rx="3" fill="#e8720c" opacity=".5"/><path d="M22 6 A14 14 0 0 1 42 6" fill="none" stroke="#4da3ff" stroke-width="5" stroke-linecap="round"/></svg>Crash telemetry</h1><div class="sub">${recs.length} report(s) &middot; anonymous (hashed device id + ESP reset reason). Newest first, 90-day retention.</div>
${hbLine}
<div>${summary}</div>
<table><tr><th>When</th><th>Reason</th><th title="Firmware running when the report was sent">Running</th><th title="Build that crashed (from the coredump); without a coredump, the reporting build">Crashed on</th><th>Layer</th><th>Device</th><th>Crash (pc)</th></tr>${rows}</table></div>
<script>
(function(){var P=document.querySelectorAll('.pill[data-r]'),R=document.querySelectorAll('tr[data-r]');
function sel(v){P.forEach(function(p){p.classList.toggle('act',p.dataset.r===v);});R.forEach(function(t){t.style.display=(v==='all'||t.dataset.r===v)?'':'none';});}
P.forEach(function(p){p.addEventListener('click',function(){sel(p.classList.contains('act')&&p.dataset.r!=='all'?'all':p.dataset.r);});});})();
(function(){var sw=document.querySelector('.themeSw');if(!sw)return;function mode(){var d=document.documentElement.getAttribute('data-theme');return d==='light'||d==='dark'?d:'system';}function mark(){var m=mode();sw.querySelectorAll('button').forEach(function(b){b.setAttribute('aria-pressed',String(b.dataset.m===m));});}sw.addEventListener('click',function(e){var b=e.target.closest('button[data-m]');if(!b)return;var m=b.dataset.m;if(m==='system'){document.documentElement.removeAttribute('data-theme');try{localStorage.removeItem('tmTheme');}catch(_){}}else{document.documentElement.setAttribute('data-theme',m);try{localStorage.setItem('tmTheme',m);}catch(_){}}mark();});mark();})();
</script>
${TM_CLOSE}</body></html>`;
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
        <label>Verdict — what we agreed${n.tok ? ' <span class="vat" title="This note carries a ticket: its submitter reads this verdict through their status link">· visible to its submitter</span>' : ''}${n.verdictAt ? ` <span class="vat">${when(n.verdictAt)}</span>` : ''}</label>
        <textarea rows="2" placeholder="e.g. Real bug, fixed in 0.15.1 · Duplicate of the resin estimate note · Backlog #31, after 1.0.0">${esc(n.verdict)}</textarea>
        <button class="save" disabled>Save</button>
      </div>
      <div class="meta">
        ${n.num ? `<a class="num" href="#n${esc(n.num)}" title="Link to this case">#${esc(n.num)}</a>` : ''}
        <time>${when(n.at)}</time>
        ${n.fw ? `<span class="pill">fw ${esc(n.fw)}${n.build ? ` <em>${esc(n.build)}</em>` : ''}</span>` : ''}
        ${n.src === 'printer' ? '<span class="pill src" title="Sent from a printer dashboard, not the open site">🖨 from a printer</span>' : ''}
        ${n.src === 'slicer' ? '<span class="pill src" title="Marked on the model in the dashboard preview - coordinates and the exact build are in the note">⌖ slicer markers</span>' : ''}
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
<script>(function(){try{var q=new URLSearchParams(location.search).get('theme');
  var t=(q==='light'||q==='dark')?q:localStorage.getItem('tmTheme');
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
.tmtop{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:2px}
.tmcrumb{margin:0;font-size:12px;color:var(--muted)}.tmcrumb a{color:var(--accent);text-decoration:none}
h1{font-size:1.2rem;margin:0;display:flex;align-items:center;gap:8px}h1 b{color:var(--accent)}
h1 .mark{width:24px;height:24px;flex:none}
.counts{color:var(--muted);font-size:.82rem;font-variant-numeric:tabular-nums}
.themeSw{display:inline-flex;gap:1px;align-items:center;flex:none}
.themeSw button{background:none;border:0;padding:5px;margin:0;cursor:pointer;color:var(--muted);line-height:0;border-radius:7px}
.themeSw button svg{width:17px;height:17px;display:block;stroke:currentColor;fill:none;stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
.themeSw button:hover{color:var(--text)}
.themeSw button[aria-pressed=true]{color:var(--accent)}
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
<div class="tmtop"><p class="tmcrumb"><a href="https://tinymakerwifi.com/">TinyMakerWifi</a> &rsaquo; Feedback inbox</p>
  <div style="display:flex;align-items:center;gap:12px">
    <span class="counts">${open} open · ${all.length} total</span>
    <div class="themeSw" role="group" aria-label="Theme"><button data-m="system" title="System" aria-label="System theme"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8M12 17v4"/></svg></button><button data-m="light" title="Light" aria-label="Light theme"><svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="4.2"/><path d="M12 2v2.5M12 19.5V22M2 12h2.5M19.5 12H22M4.9 4.9l1.8 1.8M17.3 17.3l1.8 1.8M19.1 4.9l-1.8 1.8M6.7 17.3l-1.8 1.8"/></svg></button><button data-m="dark" title="Dark" aria-label="Dark theme"><svg viewBox="0 0 24 24"><path d="M21 12.8A8.5 8.5 0 1 1 11.2 3a6.6 6.6 0 0 0 9.8 9.8z"/></svg></button></div>
  </div>
</div>
<header>
  <h1><svg class="mark" viewBox="0 0 64 64" aria-hidden="true"><rect x="8" y="40" width="48" height="9" rx="3" fill="#e8720c"/><rect x="14" y="27" width="36" height="9" rx="3" fill="#e8720c" opacity=".75"/><rect x="20" y="14" width="24" height="9" rx="3" fill="#e8720c" opacity=".5"/><path d="M22 6 A14 14 0 0 1 42 6" fill="none" stroke="#4da3ff" stroke-width="5" stroke-linecap="round"/></svg><b>TinyMakerWifi</b> feedback</h1>
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
// Theme switcher: system / light / dark. 'system' clears the override so the
// OS preference (prefers-color-scheme) shows through; light/dark persist.
(function(){var sw=document.querySelector('.themeSw');if(!sw)return;
  function mode(){var d=document.documentElement.getAttribute('data-theme');return d==='light'||d==='dark'?d:'system';}
  function mark(){var m=mode();sw.querySelectorAll('button').forEach(function(b){b.setAttribute('aria-pressed',String(b.dataset.m===m));});}
  sw.addEventListener('click',function(e){var b=e.target.closest('button[data-m]');if(!b)return;var m=b.dataset.m;
    if(m==='system'){document.documentElement.removeAttribute('data-theme');try{localStorage.removeItem('tmTheme');}catch(_){}}
    else{document.documentElement.setAttribute('data-theme',m);try{localStorage.setItem('tmTheme',m);}catch(_){}}
    mark();});
  mark();})();
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
</script>${TM_CLOSE}</body></html>`;
}
