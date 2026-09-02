/*
 * Darbininkas pulto sliceriui (`slicer-wasm.js`).
 *
 * Skiriasi nuo `worker.js` (demo) tuo, kad modelis ateina TRIKAMPIAIS - pultas
 * ji jau pasuko ir pastate, tad WASM pusėje nieko nebecentruojam. Zinutes turi
 * `id`, kad kelis kvietimus butu galima skirti vieną nuo kito.
 */
/* Baze paduoda uzkrovejas (zr. `slicer-wasm.js`): sis darbininkas gali buti
   pakrautas is blob URL, o tada reliatyvus kelias vestu i niekur. */
const BAZE = self.SLA_BAZE || './';
importScripts(BAZE + 'sla-web-3.3.0.js');

let M = null, sliceMeshFn = null, sl1Fn = null, previewFn = null, rotFn = null, vardas = 'spaudinys';
let setParamsFn = null;
let dabartinisId = 0;

/*
 * #116: ar kas nors spausdintusi ORE?
 *
 * Nulis atramu turi DU atsakymus, ir juos butina atskirti: detale ju
 * nereikalauja (viskas remiasi i plokste arba i pacia save) arba reikalauja, o
 * variklis nepastate. Atsakymas imamas is perziuros kaukiu: pikselis, kuris
 * sluoksnyje yra, o po juo nera nei modelio, nei atramos, kabo ore.
 *
 * ⚠️ Praplatinimas. Perziura retinta, tad tarp dvieju kadru nuozulni siena
 * „paauga". Apacia praplecia tiek, kiek paaugtu 45 laipsniu siena - tas pats
 * kriterijus, kaip variklio `support_critical_angle`: stačiau nei 45 laiko save.
 *
 * Skaiciuojam TIK kai atramu nulis: didele nuokaba, laikoma stulpu, kaukese vis
 * tiek atrodo „ore" (puodelis 25 mm su atramomis duoda 12 176 tokiu pikseliu, ir
 * viskas su juo gerai).
 */
const PLOKSTE_X_MM = 40.8;        // toks pat, kaip variklio PLOKSTE_X_MM
const ORE_RIBA_PX = 20;           // maziau = trianguliacijos dulkes
/* Virs sitos ribos antro ejimo NEBANDOM. Antras pjaustymas kainuoja tiek pat,
   kiek pirmas, o atminties poreikis auga tiesiskai su trikampiais (ismatuota
   2026-08-24: 300 tukst. = 157 MB ir 13 s, 490 tukst. = 226 MB ir 22 s). Sunkiam
   modeliui tai butu antras zingsnis link tos pacios sienos, kuri narsykleje
   baigiasi „Aborted()". Geriau pasakyti, kad nepavyko, nei nukristi bandant. */
const AUTO_PAKELTI_MAX_TRI = 600000;

function oreLiktu(buferis, sluoksniuViso, sluoksnisMm) {
  const dv = new DataView(buferis);
  const n = dv.getUint32(0, true), w = dv.getUint32(4, true), h = dv.getUint32(8, true);
  if (n < 2 || !w || !h) return null;
  const baitai = new Uint8Array(buferis);
  const kadras = w * h;
  const mmPx = PLOKSTE_X_MM / w;
  const zingsnis = Math.max(1, (sluoksniuViso || n) / n);
  const auga = Math.max(1, Math.ceil(zingsnis * (sluoksnisMm || 0.05) / mmPx));

  const apacia = new Uint8Array(kadras), platus = new Uint8Array(kadras);
  let viso = 0, blogiausias = 0, kur = 0;
  for (let i = 1; i < n; i++) {
    const oPrev = 16 + (i - 1) * 2 * kadras, oSis = 16 + i * 2 * kadras;
    for (let q = 0; q < kadras; q++)
      apacia[q] = (baitai[oPrev + q] || baitai[oPrev + kadras + q]) ? 1 : 0;
    for (let k = 0; k < auga; k++) {
      platus.set(apacia);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
        if (!platus[y * w + x]) continue;
        if (x) apacia[y * w + x - 1] = 1;
        if (y) apacia[(y - 1) * w + x] = 1;
        if (x + 1 < w) apacia[y * w + x + 1] = 1;
        if (y + 1 < h) apacia[(y + 1) * w + x] = 1;
      }
    }
    let kabo = 0;
    for (let q = 0; q < kadras; q++) if (baitai[oSis + q] && !apacia[q]) kabo++;
    viso += kabo;
    if (kabo > blogiausias) { blogiausias = kabo; kur = i; }
  }
  if (viso <= ORE_RIBA_PX) return null;
  return { px: viso, mm2: viso * mmPx * mmPx,
           sluoksnis: Math.round(kur * (sluoksniuViso || n) / n) };
}

/* Vienas pilnas ejimas: pjaustymas, `.sl1` ir perziuros kaukes. Isskirta i
   funkcija, nes #116 automatika ta pati kelia gali praeiti du kartus. */
function pjaustymas(pos, sluoksnis, medis, pakelta, perziuros) {
  const ptr = M._malloc(pos.byteLength);
  M.HEAPF32.set(pos, ptr >> 2);
  let atsakymas;
  try {
    atsakymas = sliceMeshFn(ptr, pos.length / 9, sluoksnis, medis ? 1 : 0, pakelta ? 1 : 0);
  } finally {
    M._free(ptr);
  }
  const d = JSON.parse(atsakymas);
  if (d.klaida) return { klaida: d.klaida };

  const info = JSON.parse(sl1Fn('/isvestis.sl1', vardas));
  if (info.klaida) return { klaida: info.klaida };
  const sl1 = M.FS.readFile('/isvestis.sl1');
  try { M.FS.unlink('/isvestis.sl1'); } catch (e) {}

  let preview = null, previewInfo = null;
  try {
    previewInfo = JSON.parse(previewFn('/preview.bin', perziuros));
    if (!previewInfo.klaida) {
      const pv = M.FS.readFile('/preview.bin');
      preview = pv.buffer;
      try { M.FS.unlink('/preview.bin'); } catch (e) {}
    }
  } catch (e) { previewInfo = { klaida: String(e && e.message || e) }; }

  return { d: d, info: info, sl1: sl1, preview: preview, previewInfo: previewInfo };
}

/* Tilto `praneskEiga` kviecia butent sita. */
self.slaProgress = function (etapas, proc) {
  self.postMessage({ tipas: 'eiga', id: dabartinisId, etapas: etapas, proc: proc });
};

/* `locateFile` butinas del tos pacios priezasties: be jo Emscripten ieskotu
   `sla-web.wasm` salia blob URL, o ne salia modulio. */
const paruostas = createSLA({ locateFile: function (p) { return BAZE + p.replace('sla-web.wasm', 'sla-web-3.3.0.wasm'); } }).then(function (m) {
  M = m;
  /* Penki argumentai, ne keturi: paskutinis - pakeltas padas (#116). */
  sliceMeshFn = m.cwrap('sla_slice_mesh', 'string',
                        ['number', 'number', 'number', 'number', 'number']);
  sl1Fn = m.cwrap('sla_export_sl1', 'string', ['string', 'string']);
  /* SL-params: nekviesta - modulis elgiasi kaip iki 3.3.0. */
  setParamsFn = m.cwrap('sla_set_params', null,
                        ['number', 'number', 'number', 'number']);
  previewFn = m.cwrap('sla_preview', 'string', ['string', 'number']);
  /* Taip pat visi penki: `tikslumas` ir `max_trikampiu` anksciau likdavo
     nepaduoti, ir viskas laikesi ant C pusės sargu (SL-args). */
  rotFn = m.cwrap('sla_rotfind', 'string',
                  ['number', 'number', 'number', 'number', 'number']);
  return m;
});

/*
 * SL-params. Nuliai reiskia „palik numatytaji", tad pultas gali paduoti tik
 * tuos laukus, kuriuos zmogus lietė, o nepaliestas jungiklis nieko nekeicia.
 * Kvieciam PRIES kiekviena pjaustyma, ne karta uzsikrovus: modulis gyvas visa
 * sesija, ir kitas modelis kitaip nepaveldetu naujo pasirinkimo.
 */
function paduokParametrus(p) {
  if (!setParamsFn) return;
  p = p || {};
  setParamsFn(Number(p.tankis) > 0 ? Number(p.tankis) : 0,
              Number(p.smaigalys) > 0 ? Number(p.smaigalys) : 0,
              Number(p.raftoSluoksniai) > 0 ? Number(p.raftoSluoksniai) : 0,
              p.glotninimas === false ? 0 : 1);
}

self.onmessage = async function (ev) {
  const z = ev.data || {};
  dabartinisId = z.id;
  try {
    await paruostas;

    if (z.tipas === 'pjaustyk') {
      vardas = (z.vardas || 'spaudinys')
        .replace(/\.stl$/i, '').replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 24) || 'spaudinys';

      const pos = new Float32Array(z.pos);
      const sluoksnis = z.sluoksnis || 0.05;
      const perziuros = z.perziuros || 160;

      paduokParametrus(z.parametrai);

      /* Pirmas ejimas - taip, kaip visada: detale ant ploksces. */
      let r = pjaustymas(pos, sluoksnis, z.medis, z.pakelta ? 1 : 0, perziuros);
      if (r.klaida) { self.postMessage({ tipas: 'klaida', id: z.id, tekstas: r.klaida }); return; }

      /*
       * #116 automatika. Jei atramu nulis, o kazkas kabo - perpjaunam pakelta
       * detale. Kaina reali (+18…43 % dervos ir apie pusantro karto sluoksniu),
       * todel kelia TIK cia, o ne kiekvienam spaudiniui.
       *
       * Neuzsispiria: jei ir pakeltas variantas atramu neduoda, griztam prie
       * plokscio ir tik ispejam - blogesnio uz bloga nesiulom.
       */
      let auto = null;
      if (z.autoPakelti !== false && !z.pakelta && !r.d.atramu_trikampiu && r.preview) {
        const ore = oreLiktu(r.preview, (r.previewInfo || {}).sluoksniu_is_viso, sluoksnis);
        if (ore && pos.length / 9 > AUTO_PAKELTI_MAX_TRI) {
          /* Sunkus modelis: pasakom, kad kabo, bet antro ejimo nedarom. */
          auto = { pakelta: false, perDidelis: true, trikampiu: Math.round(pos.length / 9),
                   mm2: ore.mm2, sluoksnis: ore.sluoksnis };
        } else if (ore) {
          self.postMessage({ tipas: 'eiga', id: z.id,
                             etapas: 'atramoms nera vietos - keliam detale', proc: 60 });
          const plokscia = { ml: r.d.turis.viso_ml, sluoksniu: r.info.sluoksniu };
          const antras = pjaustymas(pos, sluoksnis, z.medis, 1, perziuros);
          if (!antras.klaida && antras.d.atramu_trikampiu) {
            r = antras;
            auto = { pakelta: true, mm2: ore.mm2, sluoksnis: ore.sluoksnis, plokscia: plokscia,
                     pakeltas: { ml: r.d.turis.viso_ml, sluoksniu: r.info.sluoksniu } };
          } else {
            auto = { pakelta: false, nepadejo: true, mm2: ore.mm2, sluoksnis: ore.sluoksnis };
          }
        }
      }

      const perduoti = r.preview ? [r.sl1.buffer, r.preview] : [r.sl1.buffer];
      self.postMessage({ tipas: 'atsakymas', id: z.id, duomenys: r.d, sl1info: r.info,
                         sl1: r.sl1.buffer, preview: r.preview, previewInfo: r.previewInfo,
                         auto: auto },
                       perduoti);

    } else if (z.tipas === 'pakrypimas') {
      /* PrusaSlicer Rotfinder: kuria puse modeli guldyti. Vienas blokuojantis
         kvietimas (dideliems modeliams 5-12 s), todel jis ir sukasi cia, o ne
         pulto gijoje - kitaip puslapis tiek laiko stovetu. */
      self.postMessage({ tipas: 'eiga', id: z.id, etapas: 'ieskoma geriausios padeties', proc: 5 });
      const pos = new Float32Array(z.pos);
      const ptr = M._malloc(pos.byteLength);
      M.HEAPF32.set(pos, ptr >> 2);
      let atsakymas;
      try {
        atsakymas = rotFn(ptr, pos.length / 9, z.kauke || 1,
                          z.tikslumas || 1.0, z.maxTrikampiu || 0);
      } finally {
        M._free(ptr);
      }
      const kampai = JSON.parse(atsakymas);
      if (kampai.klaida) { self.postMessage({ tipas: 'klaida', id: z.id, tekstas: kampai.klaida }); return; }
      self.postMessage({ tipas: 'atsakymas', id: z.id, kampai: kampai });

    } else if (z.tipas === 'geometrija') {
      const dalys = {};
      for (const [raktas, kelias] of [['model', '/out_model.stl'],
                                      ['supports', '/out_supports.stl'],
                                      ['pad', '/out_pad.stl']]) {
        try { dalys[raktas] = M.FS.readFile(kelias); } catch (e) { dalys[raktas] = null; }
      }
      const perduoti = Object.values(dalys).filter(Boolean).map(a => a.buffer);
      self.postMessage({ tipas: 'atsakymas', id: z.id, dalys: dalys }, perduoti);
    }
  } catch (e) {
    self.postMessage({ tipas: 'klaida', id: z.id, tekstas: String((e && e.message) || e) });
  }
};
