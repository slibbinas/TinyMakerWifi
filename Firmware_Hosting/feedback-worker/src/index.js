// TinyMakerWifi feedback collector - a tiny standalone Cloudflare Worker.
//
//   GET  /feedback[/]            -> the form page (proxied from gh-pages)
//   POST /feedback               -> store a note (+ up to 3 photos) in KV
//   GET  /feedback/inbox?key=..  -> the maintainer's reading page (HTML)
//   GET  /feedback/list?key=..   -> the same notes as JSON  (LIST_KEY secret)
//   GET  /feedback/img?key=..&k=img:..  -> one stored photo
//   POST /feedback/mark?key=..&k=fb:..  -> triage: {tag, handled, verdict}
//   POST /feedback/del?key=..&k=fb:..   -> drop one note and its photos
//
// Triage lives on the record itself: the maintainer tags it (submitters
// mis-file their own notes), ticks it handled, and writes the agreed verdict
// so the decision stays glued to what prompted it.
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
      await env.FEEDBACK.put('fb:' + stamp + ':' + id, JSON.stringify({
        num,
        message: msg,
        contact: str(fields.contact, 120),
        fw: str(fields.fw, 20),
        build: str(fields.build, 20),
        ua: str(fields.ua, 120),
        photos: imgKeys,
        at: stamp,
      }));
      return new Response(JSON.stringify({ ok: true, id: num, photos: imgKeys.length }), {
        headers: { 'Content-Type': 'application/json', ...CORS },
      });
    }

    // trim(): a secret typed at a Windows prompt can arrive with a stray \r,
    // which would silently fail every key comparison.
    const listKey = String(env.LIST_KEY || '').trim();
    const keyOk = listKey && (url.searchParams.get('key') || '').trim() === listKey;

    const readAll = async () => {
      const list = await env.FEEDBACK.list({ prefix: 'fb:', limit: 100 });
      const out = [];
      for (const k of list.keys) {
        const v = await env.FEEDBACK.get(k.name);
        if (!v) continue;
        const rec = JSON.parse(v);
        rec.key = k.name;
        rec.photoUrls = (rec.photos || []).map(
          (p) => url.origin + '/feedback/img?key=' + encodeURIComponent(listKey) + '&k=' + encodeURIComponent(p));
        out.push(rec);
      }
      out.reverse();  // newest first
      return out;
    };

    if (path === '/feedback/inbox' && keyOk) {
      const notes = await readAll();
      return new Response(inboxPage(notes, listKey), {
        headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' },
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
      await env.FEEDBACK.put(k, JSON.stringify(rec));
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
      return new Response(JSON.stringify(await readAll(), null, 1), {
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

function inboxPage(notes, listKey) {
  const withPhotos = notes.filter((n) => (n.photos || []).length).length;
  const open = notes.filter((n) => !n.handled).length;
  const count = (t) => notes.filter((n) => n.tag === t).length;

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
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect x='8' y='40' width='48' height='9' rx='3' fill='%23e8720c'/><rect x='14' y='27' width='36' height='9' rx='3' fill='%23e8720c' opacity='.75'/><rect x='20' y='14' width='24' height='9' rx='3' fill='%23e8720c' opacity='.5'/><path d='M22 6 A14 14 0 0 1 42 6' fill='none' stroke='%234da3ff' stroke-width='5' stroke-linecap='round'/></svg>">
<style>
:root{color-scheme:dark;--bg:#141416;--card:#1d1d20;--line:#2c2c31;--text:#eee;--muted:#9a9aa2;--accent:#e8720c;--pill:#2a2a2e;--danger:#b34a38;--ok:#3f9f55}
@media(prefers-color-scheme:light){:root{color-scheme:light;--bg:#f2f2f4;--card:#fff;--line:#dfe1e5;--text:#1f2124;--muted:#5f6570;--pill:#eceef1;--ok:#2f8043}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15.5px/1.55 -apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.wrap{max-width:760px;margin:0 auto;padding:24px 14px 60px;display:flex;flex-direction:column;gap:14px}
header{display:flex;align-items:baseline;justify-content:space-between;gap:10px;flex-wrap:wrap;padding:2px}
h1{font-size:1.2rem;margin:0}h1 b{color:var(--accent)}
.counts{color:var(--muted);font-size:.82rem;font-variant-numeric:tabular-nums}
.filters{display:flex;gap:8px;flex-wrap:wrap}
.filters button{background:var(--pill);color:var(--text);border:1px solid var(--line);border-radius:999px;padding:5px 12px;font-size:.8rem;font-weight:600;cursor:pointer}
.filters button.on{background:var(--accent);border-color:var(--accent);color:#fff}
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
  <h1><b>Feedback</b> inbox</h1>
  <span class="counts">${open} open · ${notes.length} total${withPhotos ? ` · ${withPhotos} with photos` : ''}</span>
</header>
${notes.length ? `<div class="filters">
  <button class="on" data-f="open">New (${open})</button>
  ${TAGS.map(([v, label]) => `<button data-f="${v}">${label} (${count(v)})</button>`).join('')}
  <button data-f="all">All (${notes.length})</button>
</div>` : ''}
${notes.length ? cards : `<div class="empty"><div class="big">📭</div>Nothing yet. The form is at <a href="/feedback/">tinymakerwifi.com/feedback</a>.</div>`}
<div class="empty none" style="display:none"><div class="big">✅</div>Nothing here — try another filter.</div>
<footer>Private page · <a href="/feedback/list?key=${encodeURIComponent(listKey)}">raw JSON</a></footer>
</div>
<script>
var KEY=${JSON.stringify(listKey)};
var api=function(what,note,body){
  return fetch('/feedback/'+what+'?key='+encodeURIComponent(KEY)+'&k='+encodeURIComponent(note.dataset.k),
    {method:'POST',headers:body?{'Content-Type':'application/json'}:{},body:body?JSON.stringify(body):undefined})
    .then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);});
};
var applyFilter=function(){
  var f=(document.querySelector('.filters button.on')||{dataset:{f:'open'}}).dataset.f,shown=0;
  document.querySelectorAll('.note').forEach(function(n){
    var ok = f==='all' ? true
           : f==='open' ? !n.dataset.handled
           : n.dataset.tag===f;
    n.style.display=ok?'':'none';
    if(ok)shown++;
  });
  var none=document.querySelector('.empty.none');
  if(none)none.style.display=(shown||!document.querySelectorAll('.note').length)?'none':'';
};
document.querySelectorAll('.filters button').forEach(function(b){
  b.addEventListener('click',function(){
    document.querySelectorAll('.filters button').forEach(function(x){x.classList.remove('on');});
    b.classList.add('on');applyFilter();
  });
});
document.querySelectorAll('.note').forEach(function(n){
  n.querySelectorAll('.tag').forEach(function(b){
    b.addEventListener('click',function(){
      var next=b.classList.contains('on')?'':b.dataset.tag;   // click again to clear
      api('mark',n,{tag:next}).then(function(){
        n.querySelectorAll('.tag').forEach(function(x){x.classList.remove('on');});
        if(next)b.classList.add('on');
        n.dataset.tag=next;applyFilter();
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
      applyFilter();
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
    api('del',n).then(function(){n.remove();applyFilter();})
      .catch(function(){b.disabled=false;b.textContent='Delete failed';});
  });
});
applyFilter();
</script></body></html>`;
}
