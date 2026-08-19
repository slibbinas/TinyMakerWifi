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
importScripts(BAZE + 'sla-web-3.0.4.js');

let M = null, sliceMeshFn = null, sl1Fn = null, previewFn = null, vardas = 'spaudinys';
let dabartinisId = 0;

/* Tilto `praneskEiga` kviecia butent sita. */
self.slaProgress = function (etapas, proc) {
  self.postMessage({ tipas: 'eiga', id: dabartinisId, etapas: etapas, proc: proc });
};

/* `locateFile` butinas del tos pacios priezasties: be jo Emscripten ieskotu
   `sla-web.wasm` salia blob URL, o ne salia modulio. */
const paruostas = createSLA({ locateFile: function (p) { return BAZE + p.replace('sla-web.wasm', 'sla-web-3.0.4.wasm'); } }).then(function (m) {
  M = m;
  sliceMeshFn = m.cwrap('sla_slice_mesh', 'string', ['number', 'number', 'number', 'number']);
  sl1Fn = m.cwrap('sla_export_sl1', 'string', ['string', 'string']);
  previewFn = m.cwrap('sla_preview', 'string', ['string', 'number']);
  return m;
});

self.onmessage = async function (ev) {
  const z = ev.data || {};
  dabartinisId = z.id;
  try {
    await paruostas;

    if (z.tipas === 'pjaustyk') {
      vardas = (z.vardas || 'spaudinys')
        .replace(/\.stl$/i, '').replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 24) || 'spaudinys';

      const pos = new Float32Array(z.pos);
      const ntri = pos.length / 9;
      /* Trikampiai kopijuojami i modulio atminti; 300 tukst. trikampiu = 10 MB,
         tad po darbo butina atlaisvinti. */
      const ptr = M._malloc(pos.byteLength);
      M.HEAPF32.set(pos, ptr >> 2);
      let atsakymas;
      try {
        atsakymas = sliceMeshFn(ptr, ntri, z.sluoksnis || 0.05, z.medis ? 1 : 0);
      } finally {
        M._free(ptr);
      }
      const d = JSON.parse(atsakymas);
      if (d.klaida) { self.postMessage({ tipas: 'klaida', id: z.id, tekstas: d.klaida }); return; }

      /* Sluoksnius gaminam is karto: pultas juos rodo perziuroje, o issaugant
         tas pats archyvas keliauja i printeri - antro pjaustymo nereikia. */
      const info = JSON.parse(sl1Fn('/isvestis.sl1', vardas));
      if (info.klaida) { self.postMessage({ tipas: 'klaida', id: z.id, tekstas: info.klaida }); return; }
      const sl1 = M.FS.readFile('/isvestis.sl1');
      try { M.FS.unlink('/isvestis.sl1'); } catch (e) {}

      /* Perziuros kaukes - is karto po sluoksniu: pultas jas rodo tuoj pat, o
         antro pjaustymo nereikia. */
      let preview = null, previewInfo = null;
      try {
        previewInfo = JSON.parse(previewFn('/preview.bin', z.perziuros || 160));
        if (!previewInfo.klaida) {
          const pv = M.FS.readFile('/preview.bin');
          preview = pv.buffer;
          try { M.FS.unlink('/preview.bin'); } catch (e) {}
        }
      } catch (e) { previewInfo = { klaida: String(e && e.message || e) }; }

      const perduoti = preview ? [sl1.buffer, preview] : [sl1.buffer];
      self.postMessage({ tipas: 'atsakymas', id: z.id, duomenys: d, sl1info: info,
                         sl1: sl1.buffer, preview: preview, previewInfo: previewInfo },
                       perduoti);

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
