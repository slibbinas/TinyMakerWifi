/*
 * pulto_nuotrauka.mjs - nufotografuoja pultą be žmogaus.
 *
 * Kam: UI pakeitimas laikomas patikrintu tik tada, kai jį pamatai nurenderintą.
 * `textContent` patikros meluoja - 2026-08-24 įspėjimas apie per didelę detalę
 * buvo įrašytas į neatvaizduojamą elementą, visi JS patikrinimai rodė „tekstas
 * yra", o kortelė ramiai rašė „Fits the build volume". Pagavo tik nuotrauka.
 *
 * Priklausomybių nėra: Node 18+ turi globalų fetch, Node 22+ - globalų WebSocket.
 * Chrome varomas headless režimu ir valdomas per CDP.
 *
 * Du režimai:
 *   gyvas    - fotografuoja TIKRĄ printerį (numatytasis)
 *   --stendas - pultas sulipdomas iš repo (assemble_dashboard.py) ir paduodamas
 *               iš vietinio serverio, o /api/* proxy'inama į printerį. Taip
 *               pakeitimas matomas PRIEŠ liejimą, su gyvais duomenimis.
 *
 * Pavyzdžiai:
 *   node scripts/dev/pulto_nuotrauka.mjs --out C:/tmp/pultas.png
 *   node scripts/dev/pulto_nuotrauka.mjs --stendas --clip "#slicerCard,#sdSection" \
 *        --click "#slicerToggle" --out C:/tmp/kortele.png
 *   node scripts/dev/pulto_nuotrauka.mjs --js "document.getElementById('slicerInfo').textContent"
 *   node scripts/dev/pulto_nuotrauka.mjs --url "http://tinymaker.local/?theme=light" --out C:/tmp/sviesi.png
 */
import { spawn, execFileSync } from 'child_process';
import http from 'http';
import fs from 'fs';
import os from 'os';
import path from 'path';

/* ---------- argumentai ---------- */
const A = process.argv.slice(2);
const imk = (v, num) => { const i = A.indexOf(v); return i < 0 ? num : A[i + 1]; };
const yra = v => A.includes(v);

const CFG = {
  url:      imk('--url', 'http://tinymaker.local/'),
  out:      imk('--out', null),
  clip:     imk('--clip', null),          // "#a,#b" - stačiakampis apglėbia visus
  click:    imk('--click', null),         // "#id" arba "#a,#b" - paspaudžiama iš eilės
  js:       imk('--js', null),            // išraiška; rezultatas spausdinamas
  laukti:   Number(imk('--wait', 8000)),  // kiek laukti po įkėlimo (ms)
  plotis:   Number(imk('--w', 1000)),
  aukstis:  Number(imk('--h', 1400)),
  mastelis: Number(imk('--scale', 2)),
  stendas:  yra('--stendas'),
  tylus:    yra('--tylus'),
};
const sako = (...m) => { if (!CFG.tylus) console.log(...m); };
const miegok = ms => new Promise(r => setTimeout(r, ms));

/* ---------- Chrome ---------- */
const CHROME_KELIAI = [
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
  process.env.CHROME_PATH,
].filter(Boolean);
const CHROME = CHROME_KELIAI.find(p => fs.existsSync(p));
if (!CHROME) { console.error('Chrome nerastas. Nurodyk per CHROME_PATH.'); process.exit(1); }

const PORT = 9400 + Math.floor(process.uptime() * 7) % 90;   // kad du paleidimai nesimuštų
const WEB  = PORT + 500;
const PROF = fs.mkdtempSync(path.join(os.tmpdir(), 'pultas-'));
const REPO = path.resolve(path.dirname(new URL(import.meta.url).pathname.slice(1)), '..', '..');

/* ---------- stendas: pultas iš repo + /api proxy į printerį ---------- */
let srv = null, adresas = CFG.url;
if (CFG.stendas) {
  const dash = path.join(PROF, 'dashboard.html');
  execFileSync('python', [path.join(REPO, 'scripts', 'assemble_dashboard.py'), '-o', dash],
               { cwd: REPO, stdio: 'ignore' });
  const printeris = new URL(CFG.url).origin;
  srv = http.createServer(async (q, r) => {
    if (q.url === '/' || q.url.startsWith('/?')) {
      r.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      return r.end(fs.readFileSync(dash));
    }
    try {                                     // visa kita - iš tikro printerio
      const at = await fetch(printeris + q.url);
      const b = Buffer.from(await at.arrayBuffer());
      r.writeHead(at.status, { 'Content-Type': at.headers.get('content-type') || 'application/octet-stream' });
      r.end(b);
    } catch (e) { r.writeHead(502); r.end('proxy: ' + e.message); }
  });
  await new Promise(r => srv.listen(WEB, '127.0.0.1', r));
  adresas = `http://127.0.0.1:${WEB}/` + (new URL(CFG.url).search || '');
  sako('stendas:', adresas, '(API iš ' + printeris + ')');
}

/* ---------- paleidžiam ir prisijungiam ---------- */
const chrome = spawn(CHROME, ['--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars',
  '--no-default-browser-check', `--user-data-dir=${PROF}`, `--remote-debugging-port=${PORT}`,
  `--window-size=${CFG.plotis},${CFG.aukstis}`, adresas], { stdio: 'ignore' });

let taikinys = null;
for (let i = 0; i < 40 && !taikinys; i++) {
  await miegok(500);
  try {
    const t = await fetch(`http://127.0.0.1:${PORT}/json`).then(r => r.json());
    taikinys = t.find(x => x.type === 'page' && x.url !== 'about:blank');
  } catch (e) { /* dar kyla */ }
}
if (!taikinys) { console.error('Chrome neatsiliepė per 20 s'); chrome.kill(); process.exit(1); }

const ws = new WebSocket(taikinys.webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener('open', r));
let nr = 0; const laukia = new Map();
ws.addEventListener('message', ev => {
  const m = JSON.parse(ev.data);
  if (m.id && laukia.has(m.id)) { laukia.get(m.id)(m); laukia.delete(m.id); }
});
const cmd = (metodas, p = {}) => new Promise((res, rej) => {
  const n = ++nr;
  laukia.set(n, x => x.error ? rej(new Error(JSON.stringify(x.error))) : res(x.result));
  ws.send(JSON.stringify({ id: n, method: metodas, params: p }));
});
const js = async (kodas, pazadas = false) => {
  const r = await cmd('Runtime.evaluate',
    { expression: kodas, awaitPromise: pazadas, returnByValue: true, timeout: 120000 });
  if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 300));
  return r.result && r.result.value;
};

const baigti = async kodas => {
  try { ws.close(); } catch (e) {}
  chrome.kill(); if (srv) srv.close();
  await miegok(200); try { fs.rmSync(PROF, { recursive: true, force: true }); } catch (e) {}
  process.exit(kodas);
};

try {
  await cmd('Page.enable'); await cmd('Runtime.enable');
  await miegok(CFG.laukti);

  /* Gidas užstoja korteles - jei jis atidarytas, uždarom. */
  await js(`(()=>{const b=[...document.querySelectorAll('button')]
      .find(x=>/Hide this guide/i.test(x.textContent)); if(b)b.click(); return 1;})()`);

  if (CFG.click) {
    for (const sel of CFG.click.split(',')) {
      await js(`(()=>{const e=document.querySelector('${sel.trim()}'); if(e)e.click(); return !!e;})()`);
      await miegok(2500);
    }
  }

  if (CFG.js) sako(await js(CFG.js, true));

  if (CFG.out) {
    let clip;
    if (CFG.clip) {
      /* Stačiakampis skaičiuojamas PUSLAPYJE, ne spėjamas pikseliais. */
      const sels = JSON.stringify(CFG.clip.split(',').map(s => s.trim()));
      clip = await js(`(()=>{const r=${sels}.map(s=>document.querySelector(s))
          .filter(Boolean).map(e=>e.getBoundingClientRect());
        if(!r.length) return null;
        const x0=Math.min(...r.map(a=>a.x))+scrollX-14, y0=Math.min(...r.map(a=>a.y))+scrollY-14;
        const x1=Math.max(...r.map(a=>a.x+a.width))+scrollX+14;
        const y1=Math.max(...r.map(a=>a.y+a.height))+scrollY+14;
        return {x:x0,y:y0,width:x1-x0,height:y1-y0};})()`);
      if (!clip) { console.error('--clip: nė vienas selektorius nerastas'); await baigti(1); }
      clip.scale = CFG.mastelis;
    }
    const { data } = await cmd('Page.captureScreenshot',
      { format: 'png', captureBeyondViewport: true, ...(clip ? { clip } : {}) });
    fs.writeFileSync(CFG.out, Buffer.from(data, 'base64'));
    sako('✔', CFG.out, '(' + Math.round(fs.statSync(CFG.out).size / 1024) + ' KB)');
  }
  await baigti(0);
} catch (e) {
  console.error('KLAIDA:', e.message);
  await baigti(1);
}
