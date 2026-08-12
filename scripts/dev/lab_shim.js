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
  var useAlt = localStorage.getItem('labAlt') === '1';
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

  // Įkelia STL taip, tarsi jį būtų pasirinkęs žmogus — per tą patį <input>,
  // kad veiktų visa esama grandinė, o ne apeitas jos gabalas.
  function load(file, slice) {
    say('kraunam ' + file + '…');
    fetch('/models/' + encodeURIComponent(file)).then(function (r) {
      if (!r.ok) throw new Error(r.status + ' ' + file);
      return r.arrayBuffer();
    }).then(function (buf) {
      var inp = document.getElementById('slicerFile');
      var dt = new DataTransfer();
      dt.items.add(new File([buf], file, { type: 'model/stl' }));
      inp.files = dt.files;
      inp.dispatchEvent(new Event('change', { bubbles: true }));
      say(file + ' — įkelta');
      if (!slice) return;
      // Slice mygtukas atsiranda tik apdorojus STL; laukiam, kol atsirakins.
      var t0 = Date.now();
      (function wait() {
        /* Testiniai modeliai už plokštę didesni, o Slice lieka užrakintas, kol
           daiktas netelpa — tad sumažinam PIRMIAU, nepriklausomai nuo Slice
           būsenos. Anksčiau laukiau Slice ir tik tada spaudžiau „Fit", ir
           stendas įstrigdavo ties „nesulaukiau Slice mygtuko" (08-13). */
        var fit = document.getElementById('slicerAutoFit');
        if (fit && !fit.disabled && !fit.dataset.labDone) {
          fit.dataset.labDone = '1';
          fit.click();
          setTimeout(wait, 400);
          return;
        }
        var go = document.getElementById('slicerGo');
        if (go && !go.disabled) { say('slicinam…'); go.click(); return; }
        if (Date.now() - t0 > 20000) { say('nesulaukiau Slice mygtuko'); return; }
        setTimeout(wait, 200);
      })();
    }).catch(function (e) { say('klaida: ' + e.message); });
  }

  function start() {
    hideChrome();
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
