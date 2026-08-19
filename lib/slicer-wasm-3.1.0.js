/*
 * Pulto sliceris ant WASM variklio.
 *
 * Pultui atrodo TAIP PAT, kaip senasis `slicer.js`: tie patys funkciju vardai,
 * tas pats `slice()` atsakymas. Skiriasi tik vidus - atramas, rafta ir
 * sluoksnius skaiciuoja tikras PrusaSlicer variklis (libslic3r), sukompiliuotas
 * i WebAssembly ir sukamas atskiroje gijoje.
 *
 * Pagalbines funkcijas (STL skaitymas, pasukimas, telpa/netelpa, scenos
 * tinklas) imam is bazes - jos ne algoritmas, o irankiai, ir jos lieka.
 *
 * Copyright (C) 2026 Viktoras Sidlauskas · AGPL-3.0-or-later (naudoja libslic3r)
 */
import * as BAZE from './slicer-core-3.1.0.js';

export const VERSION = '3.1.0-wasm';

/* Ka pultas ima tiesiogiai - perduodam nepakeista. */
export const {
  parseSTL, autoOrient, place, bounds, fitCheck, toSceneMesh, detailBudget,
  zipStore, setFitMargin, PLATE, RES, LAYER_MM, SUP,
  /* Sie trys - pulto piesimui. Jie ne algoritmas, o irankiai, tad ateina is
     bazes nepakeisti (printerio sesija pastebejo, kad ju truko). */
  pillarDiscs, braceDiscs, supportMesh,
  /* Posukis ant plokstes ir talpinimas - reikalingas ir `autoOrientPro` viduje. */
  fitOnPlate,
} = BAZE;

/* ------------------------------------------------------------- pastatymas */

/**
 * „Autofit" kruopstusis kelias: PAKRYPIMA parenka PrusaSlicer Rotfinder
 * (`find_least_supports_rotation`, tikras jo kodas WASM viduje), o POSUKI ant
 * plokstes, talpinima ir masteli - musu puse. Prusa apie vertikale nesuka
 * visai, o musu ploksté pailga, tad butent ten musu nauda.
 *
 * Kodel ne vietoj `autoOrient`: sis skaiciavimas trunka 5-12 s dideliems
 * modeliams, tad ikeliant faila ir toliau naudojamas greitasis `autoOrient`, o
 * sis - tik paspaudus mygtuka. Matavimas, kodel apskritai: kreivai atsiustam
 * failui Prusos pakrypimas laimejo 4 modeliuose is 5 (08-20).
 *
 * @param pos        trikampiai, dar NEPASTATYTI (kaip is `parseSTL`)
 * @param onProgress (done, total, phase) - tokia pat forma, kaip `slice()`
 * @returns { tr, size, fit } - lygiai tas pats, ka grazina `autoOrient`
 */
export async function autoOrientPro(pos, onProgress) {
  const kopija = new Float32Array(pos);            // savininkyste keliauja i gija
  let r;
  try {
    r = await paklausk(
      { tipas: 'pakrypimas', pos: kopija.buffer, kauke: 1 },
      [kopija.buffer],
      (z) => { if (onProgress) onProgress(z.proc || 0, 100, z.etapas); });
  } catch (e) {
    /* Variklis neatsake - grazinam greitaji atsakyma, o ne klaida: zmogui
       geriau pastatytas modelis nei pranesimas, kad nepavyko. */
    return { ...BAZE.autoOrient(pos), atsargin: String((e && e.message) || e) };
  }
  const k = r.kampai && r.kampai.maziausiai_atramu;
  if (!k) return BAZE.autoOrient(pos);

  const tr = Object.assign(BAZE.makeTransform(), {
    rxDeg: k.rx * 180 / Math.PI,
    ryDeg: k.ry * 180 / Math.PI,
  });
  const best = fitOnPlate(pos, tr);
  if (onProgress) onProgress(100, 100, 'baigta');
  return best;
}

/* --------------------------------------------------------------- darbininkas */

let W = null, kitasId = 1;
const laukia = new Map();

/* Kur gyvena sis modulis - is ten imami ir darbininkas bei WASM variklis.
   ⚠️ NE `BAZE`: tas vardas jau uzimtas importo virsuje (`slicer-core.js`), ir
   dublikatas luzta tik pakrovimo metu - lokaliai nepastebejau, pagavo pirmas
   bandymas is gh-pages. */
const ADRESAS = new URL('./', import.meta.url).href;

function darbininkas() {
  if (W) return W;
  /*
   * ⚠️ Darbininko NEGALIMA kurti tiesiai is kito domeno: pultas sukasi ant
   * printerio (http://tinymaker.local), o modulis guli gh-pages, ir narsykle
   * toki `new Worker(https://...)` atmeta (SecurityError).
   *
   * Apeinam standartiskai: pasidarom mazyti vietini darbininka, kuris pats
   * per `importScripts` parsisiunčia tikraji - tam kito domeno riba negalioja.
   */
  const uzkrovejas =
    'self.SLA_VERSIJA="3.1.0";self.SLA_BAZE=' + JSON.stringify(ADRESAS) + ';' +
    'importScripts(' + JSON.stringify(ADRESAS + 'slicer-wasm-worker-3.1.0.js') + ');';
  const url = URL.createObjectURL(new Blob([uzkrovejas], { type: 'text/javascript' }));
  W = new Worker(url);
  URL.revokeObjectURL(url);
  W.onmessage = (ev) => {
    const z = ev.data || {};
    const p = laukia.get(z.id);
    if (z.tipas === 'eiga') { if (p && p.eiga) p.eiga(z); return; }
    if (!p) return;
    laukia.delete(z.id);
    if (z.tipas === 'klaida') p.blogai(new Error(z.tekstas));
    else p.gerai(z);
  };
  return W;
}

function paklausk(zinute, perduoti, eiga) {
  const id = kitasId++;
  return new Promise((gerai, blogai) => {
    laukia.set(id, { gerai, blogai, eiga });
    darbininkas().postMessage({ ...zinute, id }, perduoti || []);
  });
}

/* ------------------------------------------------------------------ pjaustymas */

/* Etapu svoriai juostai. Tikri skaiciai is matavimu: biuste seja uzima apie
   pusę laiko, medis - treciadali, sluoksniai - likusi. */
/* ⚠️ „baigta" ateina is grandines PABAIGOS, bet po jos dar gaminami sluoksniai
   (.sl1). Todel jai skirta 0,78, o ne 1,0 - kitaip juosta nusoka i galą ir
   grizta atgal (taip ir nutiko pirmame bandyme). Vienetą deda pats adapteris,
   kai viskas tikrai baigta. */
const ETAPO_DALIS = {
  'pjaustomas modelis': 0.05,
  'ieskoma, kur reikia atramu': 0.30,
  'sejami atramu taskai': 0.45,
  'statomos atramos': 0.55,
  'dedamas raftas': 0.72,
  'baigta': 0.78,
  'gaminami sluoksniai': 0.80,
};

/**
 * Suslicina jau pastatyta modeli.
 *
 * @param pos        trikampiai (Float32Array, 9 skaiciai vienam) - PO `place()`
 * @param opts       { supportType:'regular'|'tree', layerHeight, name }
 * @param onProgress (done, total, phase) - kaip senajame modulyje
 * @returns { blob, files, layers, rawMl, supports, preview }
 */
export async function slice(pos, opts, onProgress) {
  const o = opts || {};
  const medis = o.supportType === 'tree' || o.tree === true;
  const sluoksnis = o.layerHeight || LAYER_MM;

  const kopija = new Float32Array(pos);          // savininkyste keliauja i gija
  const r = await paklausk(
    { tipas: 'pjaustyk', pos: kopija.buffer, sluoksnis, medis,
      vardas: o.name || 'spaudinys' },
    [kopija.buffer],
    (z) => {
      if (!onProgress) return;
      const dalis = ETAPO_DALIS[z.etapas] !== undefined
        ? ETAPO_DALIS[z.etapas] : (z.proc || 0) / 100;
      /* Pultas laukia (done, total, phase); duodam sluoksniais, kad juosta
         atrodytu taip pat, kaip su senuoju moduliu. */
      const viso = 1000;
      /* „gaminami sluoksniai" turi savo procentą - ji reikia itraukti i
         likusia atkarpa (0,80..1,00), o ne rodyti atskirai nuo nulio. */
      const galutine = z.etapas === 'gaminami sluoksniai'
        ? 0.80 + 0.20 * ((z.proc || 0) / 100) : dalis;
      onProgress(Math.round(galutine * viso), viso,
                 galutine < 0.78 ? 'scan' : 'draw', z.etapas);
    });

  const d = r.duomenys;

  /* .sl1 yra ZIP - is jo pasiimam PNG sluoksnius perziurai. Naršykle moka
     iskleisti pati (`DecompressionStream`), tad savos bibliotekos nereikia. */
  const files = await isZip(new Uint8Array(r.sl1));

  /* Perziura: variklis atiduoda DVI kaukiu serijas - modelio ir atramu. Antroji
     leidzia pultui nudazyti atramas kita spalva TIKSLIAI, o ne apytiksliais
     diskais per `pillarDiscs` (V 08-13 pazymejo atskyrima kaip butina). */
  const p = r.preview ? isKaukiu(r.preview, r.previewInfo) : null;

  return {
    blob: new Blob([r.sl1], { type: 'application/zip' }),
    files,
    /* Sluoksniu skaicius imamas is PACIO FAILO, ne is vidinio pjaustymo: jie
       skiriasi, nes .sl1 pirmas sluoksnis storesnis (0,3 mm) ir pradedamas nuo
       plokstes. Naudotojui rodomas skaicius turi sutapti su tuo, ka gaus
       printeris (Terry: vidinis 340, faile 334). */
    layers: (r.sl1info && r.sl1info.sluoksniu) || d.sluoksniu,
    rawMl: d.turis.viso_ml,
    supports: {
      pillars: d.tasku,                  // kiek atramu liecia detale
      onModel: 0,
      braces: 0,
      hanging: 0,
      raft: d.turis.padas > 0,
      islands: 0,
      firstIsland: 0,
      list: [], braceList: [],           // WASM piesia pats - pultui piesti nereikia
      tipas: d.tipas,
      atramuMl: d.turis.atramos / 1000,
      raftoMl: d.turis.padas / 1000,
    },
    /* Senojo modulio formatas - pultas jo ir laukia. `supportSlices` yra
       PRIEDAS: to paties dydzio kaukes, tik atramu ir rafto. */
    preview: p && { slices: p.model, gw: p.w, gh: p.h, modelH: p.aukstis,
                    supportSlices: p.atramos },
    wasm: d,
  };
}

/** Perziuros dvejetainis pavidalas -> masyvai, kuriuos piesia pultas. */
function isKaukiu(buferis, info) {
  const dv = new DataView(buferis);
  const n = dv.getUint32(0, true), w = dv.getUint32(4, true), h = dv.getUint32(8, true);
  const aukstis = dv.getFloat32(12, true);
  const baitai = new Uint8Array(buferis);
  const model = [], atramos = [];
  let o = 16;
  for (let i = 0; i < n; i++) {
    model.push(baitai.subarray(o, o + w * h)); o += w * h;
    atramos.push(baitai.subarray(o, o + w * h)); o += w * h;
  }
  return { model, atramos, w, h, aukstis, info };
}

/** Paskutinio pjaustymo geometrija (modelis / atramos / raftas) STL pavidalu. */
export async function geometrija() {
  const r = await paklausk({ tipas: 'geometrija' });
  return r.dalys;                        // {model, supports, pad} - Uint8Array
}

/* ------------------------------------------------------------------- ZIP */

/** Minimalus ZIP skaitytojas: grazina [{name, data:Uint8Array}]. */
async function isZip(baitai) {
  const dv = new DataView(baitai.buffer, baitai.byteOffset, baitai.byteLength);
  /* Ieskom centrines direktorijos gale (EOCD), kad nereiketu spelioti. */
  let eocd = -1;
  for (let i = baitai.length - 22; i >= 0 && i > baitai.length - 65558; i--)
    if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) return [];
  const kiek = dv.getUint16(eocd + 10, true);
  let p = dv.getUint32(eocd + 16, true);

  const out = [];
  for (let n = 0; n < kiek; n++) {
    if (dv.getUint32(p, true) !== 0x02014b50) break;
    const metodas = dv.getUint16(p + 10, true);
    const dydis = dv.getUint32(p + 20, true);
    const vardoIlgis = dv.getUint16(p + 28, true);
    const extraIlgis = dv.getUint16(p + 30, true);
    const komIlgis = dv.getUint16(p + 32, true);
    const vietinis = dv.getUint32(p + 42, true);
    const vardas = new TextDecoder().decode(baitai.subarray(p + 46, p + 46 + vardoIlgis));
    p += 46 + vardoIlgis + extraIlgis + komIlgis;

    const lVardas = dv.getUint16(vietinis + 26, true);
    const lExtra = dv.getUint16(vietinis + 28, true);
    const pradzia = vietinis + 30 + lVardas + lExtra;
    const zali = baitai.subarray(pradzia, pradzia + dydis);

    let data = zali;
    if (metodas === 8) {
      const srautas = new Blob([zali]).stream()
        .pipeThrough(new DecompressionStream('deflate-raw'));
      data = new Uint8Array(await new Response(srautas).arrayBuffer());
    }
    if (vardas.toLowerCase().endsWith('.png')) out.push({ name: vardas, data });
  }
  out.sort((a, b) => a.name.localeCompare(b.name));
  return out;
}
