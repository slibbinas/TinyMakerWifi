/*
 * SLA slicerio darbininkas (Web Worker).
 *
 * Kodel reikia: pjaustymas yra blokuojantis - biustui tai ~20 s. Pagrindineje
 * gijoje tiek laiko langas butu uzsales (nei mygtuko paspausi, nei progreso
 * pamatysi). Cia jis sukasi atskirai, o i pagrindine gija keliauja tik zinutes.
 *
 * Zinutes I darbininka:
 *   {tipas:'pjaustyk', stl:ArrayBuffer, sluoksnis:0.05, medis:false, vardas:'x'}
 *   {tipas:'sl1'}                      - pagamina archyva is paskutinio rezultato
 *
 * Zinutes IS darbininko:
 *   {tipas:'paruostas'}                - variklis pakrautas
 *   {tipas:'eiga', etapas:'...', proc:55}
 *   {tipas:'atsakymas', duomenys:{...}}
 *   {tipas:'sl1', baitai:ArrayBuffer, vardas:'x.sl1', info:{...}}
 *   {tipas:'klaida', tekstas:'...'}
 */
importScripts('sla-web.js');

let M = null, sliceFn = null, sl1Fn = null, vardas = 'spaudinys';

/* Tilto `praneskEiga` kviecia butent sita - todel ji globali. */
self.slaProgress = function (etapas, proc) {
  self.postMessage({ tipas: 'eiga', etapas: etapas, proc: proc });
};

createSLA().then(function (m) {
  M = m;
  sliceFn = m.cwrap('sla_slice', 'string', ['string', 'number', 'number']);
  sl1Fn = m.cwrap('sla_export_sl1', 'string', ['string', 'string']);
  self.postMessage({ tipas: 'paruostas' });
}).catch(function (e) {
  self.postMessage({ tipas: 'klaida', tekstas: 'variklio pakrauti nepavyko: ' + e });
});

self.onmessage = function (ev) {
  const z = ev.data || {};
  if (!M) { self.postMessage({ tipas: 'klaida', tekstas: 'variklis dar nepasiruoses' }); return; }

  try {
    if (z.tipas === 'pjaustyk') {
      vardas = (z.vardas || 'spaudinys').replace(/\.stl$/i, '').replace(/[^A-Za-z0-9_-]/g, '_').slice(0, 24) || 'spaudinys';
      M.FS.writeFile('/model.stl', new Uint8Array(z.stl));
      const t0 = Date.now();
      const atsakymas = sliceFn('/model.stl', z.sluoksnis || 0.05, z.medis ? 1 : 0);
      const d = JSON.parse(atsakymas);
      d.truko_ms = Date.now() - t0;
      self.postMessage({ tipas: 'atsakymas', duomenys: d });

    } else if (z.tipas === 'sl1') {
      const info = JSON.parse(sl1Fn('/isvestis.sl1', vardas));
      if (info.klaida) { self.postMessage({ tipas: 'klaida', tekstas: info.klaida }); return; }
      const baitai = M.FS.readFile('/isvestis.sl1');
      /* Perduodam savininkyste, kad 4 MB nebutu kopijuojami. */
      self.postMessage({ tipas: 'sl1', baitai: baitai.buffer, vardas: vardas + '.sl1', info: info },
                       [baitai.buffer]);
      try { M.FS.unlink('/isvestis.sl1'); } catch (e) {}
    }
  } catch (e) {
    self.postMessage({ tipas: 'klaida', tekstas: String(e && e.message || e) });
  }
};
