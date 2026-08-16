// Slicerio stendas: tas pats pultas, bet ekrane tik du blokai — peržiūra ir
// STL/slice. Testinis modelis įkeliamas ir suslicinamas iškart, tad kiekvienas
// bandymas prasideda ties tuo, kas įdomu, o ne ties „Choose STL" (V 08-13).
(function () {
  'use strict';

  var MODELS = [
    { name: 'biomechanical+woman.stl', label: 'biowoman' },
    { name: 'ScreamingEvil.stl', label: 'ScreamingEvil' }
  ];

  // Kurį algoritmą krauti. Pultas modulį ima per window.loadModule('slicer'…),
  // tad perimam būtent ten — taip abu algoritmai eina per tą patį kelią kaip
  // gyvai, be jokių apeidinėjimų.
  var ALT = 'slicer2';
  /* Pagal nutylėjimą — NAUJAS algoritmas: jis jau paskelbtas naudotojams
     (gh-pages `lib/slicer.js` = slicer2 turinys), tad stendas turi rodyti tai,
     ką žmogus gauna, o senasis lieka palyginimui (08-17). */
  var useAlt = localStorage.getItem('labAlt') !== '0';
  (function hookLoader() {
    var real = window.loadModule;
    Object.defineProperty(window, 'loadModule', {
      configurable: true,
      get: function () {
        return function (name, ver, cdnUrl) {
          if (name === 'slicer' && useAlt) name = ALT;
          return real ? real(name, ver, cdnUrl)
                      : import('/lib/' + name + '.js?v=' + ver).catch(function () { return null; });
        };
      },
      set: function (v) { real = v; },
    });
  })();

  /* Slepiam per CSS, o ne per inline style: pultas kai kuriuos blokus parodo
     vėliau pats (pradžios vediklis mirktelėdavo ir dingdavo), o !important
     taisyklė galioja ir tada. Logotipas paliekamas — jis netrukdo. */
  function hideChrome() {
    var css = document.createElement('style');
    css.textContent = [
      /* Sekcijos guli #homeView viduje, ne tiesiai <main> — todėl jokio
         tiesioginio vaiko selektoriaus, o visos, kad ir kaip giliai. Lieka
         tik peržiūra ir slicerio blokas; printerio būsena ir SD tvarkyklė
         stende nereikalingos (V 08-13). */
      'section:not(#printPreviewCard):not(#slicerCard){display:none!important}',
      'nav,footer,.head,.hint{display:none!important}',
      '#gsCard{display:none!important}',                     // pradžios vediklis
      'main > .toolbar{display:none!important}',             // Dashboard/Settings/Statistics
      '#connectView,#configView,#statsView,#updateView{display:none!important}',
    ].join('\n');
    document.head.appendChild(css);
  }

  function bar() {
    var b = document.createElement('div');
    b.style.cssText = 'display:flex;gap:8px;align-items:center;flex-wrap:wrap;' +
      'padding:10px 12px;margin:0 0 10px;border:1px solid var(--line,#3a3a3f);' +
      'border-radius:10px;background:var(--card,#2a2a2e)';
    var t = document.createElement('span');
    t.textContent = 'Stendas:';
    t.style.cssText = 'color:var(--muted,#aaa);font-size:.85rem';
    b.appendChild(t);
    MODELS.forEach(function (m) {
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'button secondary';
      btn.textContent = m.label;
      btn.style.cssText = 'width:auto;flex:0 0 auto';
      btn.onclick = function () { load(m.name, true); };
      b.appendChild(btn);
    });
    var disk = document.createElement('button');
    disk.type = 'button';
    disk.className = 'button secondary';
    disk.style.cssText = 'width:auto;flex:0 0 auto';
    disk.textContent = 'iš disko…';
    disk.onclick = function () { FILE.click(); };
    b.appendChild(disk);

    /* Kuris algoritmas dirba — matosi VISADA (užrašas), o mygtukas tik keičia.
       Anksčiau mygtuko tekstas bandė būti ir viena, ir kita, ir buvo neaišku,
       ar tai būsena, ar veiksmas (V 08-13). */
    var badge = document.createElement('span');
    badge.id = 'labAlgo';
    badge.style.cssText = 'font-size:.85rem;font-weight:600;padding:3px 9px;' +
      'border-radius:999px;margin-left:14px;' +
      (useAlt ? 'background:#2f6fbf;color:#fff' : 'background:#3a3a3f;color:#ddd');
    badge.textContent = algoName();
    b.appendChild(badge);

    var alt = document.createElement('button');
    alt.type = 'button';
    alt.className = 'button secondary';
    alt.style.cssText = 'width:auto;flex:0 0 auto';
    alt.textContent = 'keisk algoritmą';
    alt.onclick = function () {
      localStorage.setItem('labAlt', useAlt ? '0' : '1');
      location.reload();
    };
    b.appendChild(alt);

    var note = document.createElement('span');
    note.id = 'labNote';
    note.style.cssText = 'color:var(--muted,#aaa);font-size:.85rem;margin-left:auto';
    b.appendChild(note);
    var host = document.querySelector('main') || document.body;
    host.insertBefore(b, host.firstChild);
  }

  function algoName() {
    return useAlt ? 'NAUJAS (libslic3r)' : 'dabartinis';
  }

  // Prie kiekvienos eigos žinutės — kuris algoritmas dirba. Kitaip po kelių
  // perjungimų nebeaišku, kieno rezultatą matai.
  function say(s) {
    var n = document.getElementById('labNote');
    if (n) n.textContent = s + '  ·  ' + algoName();
  }

  // Paduoda STL taip, tarsi jį būtų pasirinkęs žmogus — per tą patį <input>,
  // kad veiktų visa esama grandinė, o ne apeitas jos gabalas.
  function feed(f, slice, tries) {
    var me = ++SEQ;   // kiekvienas naujas įkėlimas nutildo ankstesnio laukimą,
                      // kitaip po pakartojimo Slice būtų paspaustas du kartus.
    var inp = document.getElementById('slicerFile');
    var dt = new DataTransfer();
    dt.items.add(f);
    inp.files = dt.files;
    inp.dispatchEvent(new Event('change', { bubbles: true }));
    say(f.name + ' — įkelta');
    if (!slice) return;
    /* Pultas STL priima tik pasikrovus slicerio moduliui; iki tol `change`
       nueina į niekur ir stendas lieka ties „Choose an STL file". Vietoj
       ilgesnės pauzės — pakartojam patį įkėlimą, kol mygtukai atsirakina
       (V 08-17: po perkrovimo su nauju moduliu nesulaukdavo Slice). */
    var left = tries === undefined ? 6 : tries;
    setTimeout(function () {
      if (me !== SEQ) return;
      var fit = document.getElementById('slicerAutoFit');
      var go = document.getElementById('slicerGo');
      var dead = (!fit || fit.disabled) && (!go || go.disabled);
      if (dead && left > 0) { say('kartojam įkėlimą…'); feed(f, slice, left - 1); }
    }, 1500);
    // Slice mygtukas atsiranda tik apdorojus STL; laukiam, kol atsirakins.
    var t0 = Date.now();
    var fitted = false;   // vienam įkėlimui, ne vienam mygtukui: kraunant kitą
                          // modelį „Fit" turi suveikti iš naujo (08-17).
    (function wait() {
      if (me !== SEQ) return;
      /* Testiniai modeliai už plokštę didesni, o Slice lieka užrakintas, kol
         daiktas netelpa — tad sumažinam PIRMIAU, nepriklausomai nuo Slice
         būsenos. Anksčiau laukiau Slice ir tik tada spaudžiau „Fit", ir
         stendas įstrigdavo ties „nesulaukiau Slice mygtuko" (08-13). */
      var fit = document.getElementById('slicerAutoFit');
      if (fit && !fit.disabled && !fitted) {
        fitted = true;
        fit.click();
        setTimeout(wait, 400);
        return;
      }
      var go = document.getElementById('slicerGo');
      if (go && !go.disabled) { say('slicinam…'); go.click(); return; }
      if (Date.now() - t0 > 20000) { say('nesulaukiau Slice mygtuko'); return; }
      setTimeout(wait, 200);
    })();
  }

  function load(file, slice) {
    say('kraunam ' + file + '…');
    fetch('/models/' + encodeURIComponent(file)).then(function (r) {
      if (!r.ok) throw new Error(r.status + ' ' + file);
      return r.arrayBuffer();
    }).then(function (buf) {
      feed(new File([buf], file, { type: 'model/stl' }), slice);
    }).catch(function (e) { say('klaida: ' + e.message); });
  }

  /* Bet koks STL iš disko: mygtuku arba numetus ant lango. Stendo modeliai yra
     tik greitieji mygtukai, o ne visas sąrašas — V turi savų modelių aplankų
     (08-17). Numetimas priimamas visame lange, kad nereikėtų taikytis. */
  function diskInput() {
    var inp = document.createElement('input');
    inp.type = 'file';
    inp.accept = '.stl';
    inp.style.display = 'none';
    inp.onchange = function () { if (inp.files[0]) feed(inp.files[0], true); };
    document.body.appendChild(inp);
    return inp;
  }

  function dropZone() {
    var veil = document.createElement('div');
    veil.style.cssText = 'position:fixed;inset:0;z-index:9999;display:none;' +
      'align-items:center;justify-content:center;font-size:1.4rem;font-weight:600;' +
      'color:#fff;background:rgba(20,60,120,.55);border:3px dashed #7ab6ff;' +
      'pointer-events:none';
    veil.textContent = 'paleisk STL čia';
    document.body.appendChild(veil);
    var depth = 0;   // dragenter/dragleave kyla ir vaikams; be skaitiklio
                     // šydas mirksi vos pelei perėjus kitą elementą.
    window.addEventListener('dragenter', function (e) {
      e.preventDefault(); depth++; veil.style.display = 'flex';
    });
    window.addEventListener('dragover', function (e) { e.preventDefault(); });
    window.addEventListener('dragleave', function () {
      if (--depth <= 0) { depth = 0; veil.style.display = 'none'; }
    });
    window.addEventListener('drop', function (e) {
      e.preventDefault(); depth = 0; veil.style.display = 'none';
      var f = e.dataTransfer && e.dataTransfer.files[0];
      if (!f) return;
      if (!/\.stl$/i.test(f.name)) { say('ne STL: ' + f.name); return; }
      feed(f, true);
    });
  }

  var FILE = null;
  var SEQ = 0;

  function start() {
    hideChrome();
    FILE = diskInput();
    dropZone();
    bar();
    var tg = document.getElementById('slicerToggle');
    var body = document.getElementById('slicerBody');
    if (tg && body && body.style.display === 'none') tg.click();
    load(MODELS[0].name, true);
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', function () { setTimeout(start, 1200); });
  else setTimeout(start, 1200);
})();
