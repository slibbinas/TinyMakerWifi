/* ==== Sliceris: STL -> sluoksniai -> kortele (0.17 SL-mod) ==============
   Perkelta is experimental2, kur si grandine buvo isbandyta ant gelezies.
   Modulis PRISEGTAS prie slicer-0.9.0.js: judantis slicer.js jau yra
   2.0.1-dev ir nebeeksportuoja parseSTL/autoOrient/fitCheck/bounds/
   toSceneMesh/supportMesh - butent to, ka sitas blokas kviecia. */
/* ---- Slicer (B etapas): STL -> matmenys -> telpa/netelpa ---------------- */
let slicerMod=null, slicerRaw=null, slicerTr=null, slicerBudget=0;
/* Kamera persistato tik naujam failui: kitaip kiekvienas perpiesimas ja
   pertaikydavo, ir mastelio pokytis ekrane dingdavo (V 08-12). */
let slicerHome=true;
let slicerFileName='';
/* Kol slicer'is piesia i perziuros kortele, ji yra JO. Be sios veliaveles
   busenos apklausa po sekundes padeda tuscia vietele ir vaizdas dingsta
   (V 08-12). Spaudinys vis tiek svarbesnis - zr. busyPrint saka. */
let slicerOwnsPreview=false;
const slicerSay=(id,t)=>{const e=$(id);if(e)e.textContent=t;};
const slicerBusyStop=()=>{
  if(statusData&&statusData.busy){msg('Not while printing.',true);return true;}
  return false;
};
/* Kelias nuoseklus, tad oranzinis tik tas zingsnis, kuris einamas: kitaip
   visi trys atrodo vienodai svarbus (V 08-12). */
/* Pjaustymo eiga: deklaruota CIA, o ne prie paties mygtuko, nes `slicerStep`
   ja skaito, o jis kviečiamas anksciau. */
let sliceRunning=false, sliceStopWanted=false;
/* Ar dabartinis modelis telpa. „Slice" tokio nepjauna (`fitCheck` sarga zemiau),
   tad juosta apie tai turi pasakyti PRIES paspaudima, o ne po jo (V 08-19:
   „neslicina bobos ir viskas" - biustas buvo +170 % per gilus, mygtukas atsakydavo
   raudona zinute, ir tai atrode kaip sugedes mygtukas). */
let slicerFits=true;
const slicerStep=()=>{
  const set=(id,on)=>{const b=$(id);if(b)b.classList.toggle('step',!!on);};
  const loaded=!!slicerRaw, sliced=!!(typeof slicerOut!=='undefined'&&slicerOut);
  set('slicerChoose',!loaded);
  set('slicerGo',loaded&&!sliced&&slicerFits);
  set('slicerSave',sliced);
  /* Netelpa - tai oranzinis zingsnis yra „Auto fit", ne „Slice": kelias veda
     per ji, ir juosta rodo butent ta mygtuka, kuris dabar ka nors pakeis. */
  {const tools=$('gl3dTools');
   const ft=tools&&tools.querySelector("[data-tool='fit']");
   if(ft)ft.classList.toggle('step',loaded&&!sliced&&!slicerFits);
   const go=$('slicerGo');
   if(go&&!sliceRunning){
     go.disabled=!loaded||sliced||!slicerFits;
     go.title=(loaded&&!slicerFits)
       ?'Does not fit yet - Auto fit, or turn it by hand'
       :'';}}
  /* Formos irankiai turi prasme tik IKI pjovimo. Po jo jie keicia tai, kas jau
     supjaustyta: rezultatas tyliai issimeta, o zmogus to neprase - jis tiesiog
     paspaude ta, kas buvo ekrane (V 08-19). Grazina „Discard". */
  {const tools=$('gl3dTools');
   if(tools)['fit','flat','flip','tilt','rot','scale'].forEach(k=>{
     const b=tools.querySelector("[data-tool='"+k+"']");
     if(b)b.style.display=sliced?'none':'';});
  }
};
const slicerButtons=on=>{
  ['slicerAutoFit','slicerFlat','slicerFlip','slicerRotX','slicerRotZ']
    .forEach(id=>{const b=$(id);if(b)b.disabled=!on;});
  /* Ta pati busena ir ant vaizdo esantiems - jie tik kita to paties veido puse. */
  const t=$('gl3dTools');
  if(t){t.querySelectorAll('button').forEach(b=>{b.disabled=!on;});
        t.style.display=on&&slicerOwnsPreview?'flex':'none';
    const pp=$('gl3dPop');
    if(pp&&pp.style.display!=='none'){
      if(t.style.display==='none')pp.style.display='none';   /* vaizdo nebera */
      else t.style.display='none';                            /* dar atidarytas */
    }}
  slicerStep();
};

/* Vienu metu reikalingas tik vienas is dvieju: SD sarasas atsako i klausima
   „ka spausdinti", slicer'is - „is ko padaryti". Todel atidarius viena, kitas
   susiskleidzia (V 08-12). Vietos rodiklis LIEKA matomas: kuriant modeli
   verta matyti, kiek kortelėje liko. */
const sdCollapse=on=>{
  ['uploadForm','filesFilter','filesList'].forEach(id=>{
    const e=$(id); if(e)e.style.display=on?'none':'';
  });
  const h=$('sdCollapsedHint'); if(h)h.style.display=on?'block':'none';
};
/* Uzdarymas - zenklas, ne zodis, ir TA PATI seima, kaip lango didinimas virs
   perziuros: tas pats remelis, dydis ir 13 px piesinys. Atidarymas lieka zodis -
   jo ieskai, tad jis turi kviesti; uzdarymo ieskoti nereikia, jis randamas ten,
   kur ka tik paspaudei (V 08-17). */
const ICON_SLICER_CLOSE="<svg viewBox='0 0 16 16' width='13' height='13' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><path d='M4.2 4.2l7.6 7.6M11.8 4.2l-7.6 7.6'/></svg>";
const slicerToggleUI=open=>{
  const t=$('slicerToggle'); if(!t)return;
  /* Busena jau tokia - iseinam. Si funkcija kvieciama ir is apklausos (1 Hz), o be
     sios eilutes ji kas sekunde is naujo kurdavo SVG. Ta pati „apklausa valdo
     elementa" schema, kuri jau buvo primirgejusi su #statusMsg. Klase - tiesos
     saltinis, tad neatitikus (kas ir yra perkartojimo prasme) taisymas praeina. */
  if(t.classList.contains('narrow')===open)return;
  t.classList.toggle('narrow',open);
  if(open)t.innerHTML=ICON_SLICER_CLOSE; else t.textContent='Open';
  t.title=open?'Close the slicer':'';
  t.setAttribute('aria-label',open?'Close the slicer':'Open the slicer');
};
window.slicerToggleUI=slicerToggleUI;   // refreshSlicerCard gyvena auksciau uz si bloka
/* Atidarymas ir uzdarymas - viena vieta. Ta pati seka reikalinga ne tik nuo
   paspaudimo: isjungus moduli printeryje atidaryta kortele turi uzsidaryti pati.
   Iki 08-17 seka gyveno TIK paspaudimo tvarkytuve, o busenos apklausa kviete
   `slicerOpen`, kurio niekas nebuvo apibrezes - `typeof` toki kvietima tyliai
   praryja, tad kortele likdavo atidaryta. Vienoda busena - tyliai iseinam: kitaip
   apklausa kas sekunde perpiestu perziura. */
const slicerOpen=open=>{
  const body=$('slicerBody'); if(!body)return;
  if((body.style.display!=='none')===open)return;
  body.style.display=open?'block':'none';
  slicerToggleUI(open);
  sdCollapse(open);
  /* Perziuroje galejo kaboti visai kitas failas - jis ne slicer'io, tad
     vaizdas isvalomas, kad neatrodytu ikeltas (V 08-12). */
  slicerReset(); slicerOwns(false); dashPreviewPlaceholder();
};
window.slicerOpen=slicerOpen;
$('slicerToggle').addEventListener('click',async()=>{
  const wasOpen=$('slicerBody').style.display!=='none';
  slicerOpen(!wasOpen);
  if(wasOpen||slicerMod)return;
  slicerSay('slicerInfo','Loading the slicer…');
  /* Kol kodas dar keiciasi, imam is musu gh-pages: SD kopija pririsama prie
     kontrolines sumos, tad kiekvienas pakeitimas reikstu perflasinima. */
  /* Versija abiejuose keliuose: be jos narsykle laiko sena kopija ir
     naujos funkcijos tiesiog neranda (taip ir nutiko, V 08-12). */
  /* 3.0.0 - tikras PrusaSlicer variklis (libslic3r), sukompiliuotas i
     WebAssembly ir sukamas atskiroje gijoje. Pultui keitesi TIK sis adresas:
     adapteris atiduoda ta pati API, kaip senasis modulis (parseSTL, autoOrient,
     place, bounds, fitCheck, toSceneMesh, detailBudget), o slice() grazina toki
     pati objekta.
     Prisegtas VISAS rinkinys: sis failas viduje rodo i slicer-core-<ver>.js,
     slicer-wasm-worker-<ver>.js ir sla-web-<ver>.js/.wasm, tad po juo niekas
     nebeplaukioja (3.0.0 dar plaukiojo).
     3.0.2 - rastro nulis: variklis dabar irgi skaiciuoja nuo ploksteles centro,
     kaip ir pultas (iki tol modelis nusesdavo i rastro kampa ir isspausdintum
     ketvirti). 3.0.3 - dramblio pedos kompensacija ir storesnis pirmas
     sluoksnis, kaip desktop PrusaSlicer. 3.0.5 - sluoksniai skaiciuojami nuo
     modelio apacios, tad po plokste nulindusi atrama nebevirsta pirmais
     sluoksniais. 3.0.6 - ta pati atrama nebematoma ir 3D vaizde, o `layers`
     imamas is paties failo (rodem 340, faile buvo 334). */
  const SV='3.0.6';
  slicerMod=await loadModule('slicer-wasm-'+SV,SV,
      'https://slibbinas.github.io/TinyMakerWifi/lib/slicer-wasm-'+SV+'.js');
  /* Piliuleje - `slicerMod.VERSION`, t. y. ka atsakė PATS uzsikroves modulis, o ne
     `SV` konstanta. Skirtumas ne teorinis: modulis ateina is tinklo, narsykle gali
     turėti sena kese, ir is pulto iki siol nebuvo kaip pasakyti, kuris algoritmas
     veikia - sugaista du kartus per diena (V 08-18). */
  {const e=$('slicerVer');
   if(e)e.textContent=(slicerMod&&slicerMod.VERSION)?slicerMod.VERSION:'';}
  slicerSay('slicerInfo',slicerMod?'Choose an STL file to begin.'
                                  :'The slicer module could not be loaded.');
});

/* Bet koks pakeitimas panaikina supjaustyta rezultata: kitaip „Save"
   siulytu issaugoti tai, ko ekrane jau nebera (V 08-12). */
const slicerInvalidate=()=>{
  if(typeof slicerOut==='undefined'||!slicerOut)return;
  slicerOut=null;
  show('printPreviewBarFill',true);slicerLayerUI(false);slicerSupportFacts(null);
  $('slicerSave').disabled=true;
  $('slicerDiscardLink').style.visibility='hidden';
  $('slicerProg').textContent='Settings changed - slice again to save.';
  slicerStep();
};
/* Spaudinys uz sliceri svarbesnis - ir cia, ne tik ties trimis mygtukais, kurie
   klausia `slicerBusyStop()`. Sukiojimas, mastelis ir sluoksnio slankiklis eina
   TIESIAI i ta pacia perziuros kortele, kurioje spausdinant sukasi spaudinys:
   `gl3dMesh` uzdengia ji, o `paintPreviewProgress` kas sekunde nuima atgal - is
   ko gaunasi 1 Hz mirgejimas, o kai gyvu sluoksniu dar nera (`liveN==0`), slicerio
   vaizdas lieka virsuje visam seansui. Slankiklis dar ir apkarpo spaudinio 3D per
   gl3dClip - butent ta veiksma pjuvio mygtukas atskirai draudzia spausdinant.
   Tyliai, be zinutes: sios funkcijos kvieciamos ir savaime (perpiesimas), tad
   snakas cia lystu ir be zmogaus paspaudimo (auditas 08-17). */
/* Mastelio valdikliai seka TIKRA masteli, o ne atvirksciai.
   Kodel atskira funkcija: slankiklio riba buvo 10 %, o didelis modelis telpa tik
   ties 2,7 % - tada valdiklis rode 10 (naršyklė reiksme apkerpa pati), ir vos ji
   paliesdavai, modelis vel isaugdavo iki netelpancio. Zmogui tai atrode „autofit
   sumazino, o vis tiek sako per didelis" (V 08-17). Todel apacia nusileidzia iki
   to, ko reikia SIAM modeliui, o ne iki is anksto isspausto skaiciaus. */
const slicerScaleUI=b=>{
  const pr=$('popScaleRange'),pp=$('popScalePct'),pm=$('popScaleMm');
  const pct=slicerTr.scale*100;
  /* Po kablelio - tik ten, kur be jo skaicius meluotu (2,7 % vs „3 %").
     Apvalinam ZEMYN, ne i artimiausia: 30,5 % pavirtes „31 %" vel netilptu, tad
     vien valdiklio bakstelejimas modeli issprogdintu uz plokstes ribu. Zemyn
     apvalintas skaicius blogiausiu atveju sumazina plauko storiu. */
  const shown=pct<10?Math.floor(pct*10)/10:Math.floor(pct);
  /* Apacia - puse dabartinio mastelio, bet ne aukstesne uz iprastus 10 % ir ne
     zemesne uz absoliucia riba: taip slankiklyje visada lieka vietos ir i viena,
     ir i kita puse, o iprastu dydziu modeliams elgsena nesikeicia. */
  const floor=Math.max(SCALE_MIN_PCT,Math.min(10,Math.floor(pct*5)/10));
  const step=pct<10?0.1:1;
  /* Kol PIRŠTAS ant slankiklio, jo ribos NEJUDINAM. Apacia yra puse dabartinio
     mastelio, tad tempiant kairen ji irgi slenka zemyn, ruozas ilgeja, ir slankiklis
     po pirstu bega desinen - iki 0,1 % reikedavo keliu tempimu (auditas 08-17).
     Reiksme irgi ne: ja ka tik pasake pats zmogus. */
  if(pr&&!scaleDragging){pr.min=floor; pr.step=step; pr.value=shown;}
  if(pp){pp.min=floor; pp.step=step; pp.value=shown;}
  if(pm)pm.value=b.size[2].toFixed(1);
};
/* Tempimo zyme. `pointerup` ir `change` - abu: pirmas pagauna pele/pirsta, antras
   klaviatura ir atveji, kai pointer'is paleidziamas uz lango ribu.
   `slicerLastBounds` - paskutiniai piesimo matmenys, kad atleidus slankikli
   valdiklius butu galima atstatyti NEPERSKAICIUOJANT viso modelio. */
let scaleDragging=false, slicerLastBounds=null;
const slicerRender=()=>{
  if(statusData&&statusData.busy)return;
  slicerInvalidate();
  const S=slicerMod;
  const placed=S.place(slicerRaw,slicerTr);
  const b=S.bounds(placed), f=S.fitCheck(b.size);
  {const n=slicerRaw.length/9;
   let vd,col;
   if(f.fits){vd='fits the build volume';col='var(--muted)';}
   /* Verdiktas mini ir autofita: anksciau siule tik pasukti ar sumazinti
      ranka, nors mygtukas daro abu iskart (V 08-12). */
   /* Mygtukas STOVI pranesime. Anksciau pranesimas buvo desineje, o mygtukas,
      kuri jis liepia spausti, - kitame korteles gale; akis eina per visa
      kortele ir atgal (V 08-13). */
   else{vd='too large - '+f.axis+' +'+Math.round((f.worst-1)*100)
        +'% · <button type="button" id="slicerFitHere" style="display:inline-flex;'
        +'width:auto;min-height:0;margin:0 0 0 4px;padding:2px 10px;border-radius:6px;'
        +'background:var(--accent);color:#fff;border:0;font-size:.85rem;cursor:pointer">'
        +'Fit it</button> or turn it by hand';col='var(--warncol)';}
   if(slicerBudget&&n>slicerBudget)vd+=' · more detail than the printer can show';
   /* Apatines ribos pjaustymas neturi, ir dabar, kai mastelis leidziasi iki 0,1 %,
      i viena sluoksni sumazinta detale supjaustoma bei issaugoma be nė zodzio
      (auditas 08-17). Nedraudziam - kartais mazyte detale ir yra tikslas, - bet
      pasakom, ka gausim: aukstis sluoksniais ir plotis printerio pikseliais
      (0,1275 mm, t. y. 40,8 mm / 320). */
   {const hMm=b.size[2], nL=Math.max(1,Math.round(hMm/0.05));
    const px=Math.min(b.size[0],b.size[1])/0.1275;
    if(hMm<1||px<8)
      vd+=' · very small: '+hMm.toFixed(2)+' mm tall ('+nL+' layer'+(nL===1?'':'s')
          +'), '+Math.max(1,Math.round(px))+' px across its smallest side';}
   /* Virsuje - tik verdiktas; matmenys ir trikampiai nusileido prie kitos
      to paties pobudzio pastabos apie sluoksnius (V 08-12). */
   $('slicerInfo').innerHTML='<span style="color:'+col+'">'+vd+'</span>';
   {const fh=$('slicerFitHere');
    if(fh)fh.addEventListener('click',()=>$('slicerAutoFit').click());}
   $('slicerDims').textContent=(slicerFileName?slicerFileName+'  ·  ':'')+
     b.size[0].toFixed(1)+' × '+b.size[1].toFixed(1)+' × '+b.size[2].toFixed(1)
     +' mm  ·  '+n.toLocaleString()+' triangles';}
  const fit=$('slicerFit');
  slicerFits=!!f.fits;
  if(f.fits){
    fit.textContent='Fits the build volume.';
    fit.style.color='var(--muted)';
    /* Masteli slėpti, kai jis pradeda tikti, - kaip tik atvirksciai, nei
       reikia: sumazinai iki telpancio ir pakoreguoti nebegali (V 08-12).
       Slepiamas tik ispejimas, ne valdiklis. */
  }else{
    fit.textContent='Too large - the '+f.axis+' is '+Math.round((f.worst-1)*100)
      +'% over. Turning it often solves this; scaling changes the part’s real size.';
    fit.style.color='var(--warncol)';
  }
  slicerLastBounds=b;
  slicerScaleUI(b);
  /* Detalumo biudzetas: virs jo printeris papildomu trikampiu parodyti nebegali. */
  const tri=slicerRaw.length/9;
  if(slicerBudget&&tri>slicerBudget)
    fit.textContent+='  This file holds more detail ('+tri.toLocaleString()+' triangles) than the '
      +'printer can show at this size (~'+slicerBudget.toLocaleString()+').';
  const home=slicerHome;
  if(window.gl3dMesh)gl3dMesh(S.toSceneMesh(placed),home);
  /* Kadruojam PATI MODELI, ne visa gamybos turi. `gl3dMesh` savo „home" atstuma
     skaiciuoja is modelio dydzio su atsarga (`size*2.6+28`), tad detale likdavo
     maza kazkur narvo viduryje - lygiai taip, kaip perziuroje atrodo vaizdas SU
     narvu. Ta pati perziura, narva nuemus, priartina prie paties daikto, ir
     sliceryje reikia to paties: cia narvo isvis nera ko ziureti, o svarbus tik
     daiktas (V 08-18). `gl3dFrameAll` skaiciuoja atstuma is lango proporciju,
     tad daiktas uzima kadra, o ne jo kampeli. */
  if(home&&window.gl3dFrameAll)gl3dFrameAll();
  slicerHome=false;
  slicerOwns(true);
  {const t=$('gl3dTools'); if(t)t.style.display='flex';}
  slicerStep();          // juostos turinys priklauso nuo to, ar jau supjaustyta
  $('printPreviewTitle').textContent='Slicer preview';
  show('printPreviewCard',true);
};

$('slicerFile').addEventListener('change',async e=>{
  const f=e.target.files&&e.target.files[0]; if(!f||!slicerMod)return;
  if(slicerBusyStop())return;
  slicerHome=true;                 // naujas failas - vaizda pastatom is naujo
  slicerSay('slicerInfo','Reading '+f.name+'…');slicerSay('slicerFit','');
  try{
    const buf=await f.arrayBuffer();
    const r=slicerMod.parseSTL(buf);
    /* Senas rezultatas nuvalomas PRIES nauja modeli: kitaip jo duomenys
       gali persideti ant naujo ir vaizdas atrodo istemptas (V 08-12). */
    slicerOut=null;
    slicerRaw=r.positions; slicerFileName=f.name;
    slicerBudget=slicerMod.detailBudget(slicerRaw);
    const best=slicerMod.autoOrient(slicerRaw);      // padedam ant plokstumos iskart
    slicerTr=best.tr;
    slicerButtons(true);
    /* Naujas failas - naujas siulymas: senas vardas likdavo ir modelis
       issisaugodavo ne tuo pavadinimu (V 08-12). */
      $('slicerName').value=f.name.replace(/\.stl$/i,'')
        .replace(/[^A-Za-z0-9_-]/g,'').slice(0,14);   // ilgesnis netelpa printerio ekrane
    $('slicerGo').disabled=false;
    slicerRender();
  }catch(err){slicerSay('slicerInfo',err.message);slicerButtons(false);}
});

/* Autofit = pasuka + sumazina TIK jei netelpa, ir apie tai pasako. Mazinimas
   keicia tikra detales dydi, tad tylus jis nebuna niekada (V 08-12). */
$('slicerAutoFit').addEventListener('click',()=>{
  if(!slicerRaw)return;
  const s0=slicerTr.scale;
  slicerTr=slicerMod.autoOrient(slicerRaw).tr; slicerTr.scale=s0;
  const b=slicerMod.bounds(slicerMod.place(slicerRaw,slicerTr));
  const f=slicerMod.fitCheck(b.size);
  if(!f.fits){
    slicerTr.scale*=f.scaleToFit;
    /* Be snacko: modelis vaizde pajuda, o korteles eilute pasako ir verdikta,
       ir masteli - pranesimas kartotu tai, kas jau matosi (V 08-17). */
  }
  /* Skundas „netelpa" gyvena savo 6 s, o Auto fit kaip tik ta priezasti pasalina:
     modelis jau telpa, o eilute dar kabo ir atrodo, kad mygtukas nesuveike (V 08-19).
     Nuimam TIK ta viena sakini ir tik kai tikrai tilpo - svetimo pranesimo neliecam. */
  {const e=$('statusMsg');
   if(/does not fit/i.test(e.textContent||'')&&
      slicerMod.fitCheck(slicerMod.bounds(slicerMod.place(slicerRaw,slicerTr)).size).fits)
     msg('',false);}
  /* Po autofito vaizdas kadruojamas is naujo: modelis ka tik pasisuko ir galbut
     sumazejo, tad senas zvilgsnis jam nebetinka - jis likdavo mazas lango viduryje
     (V 08-18). Rankiniai posukiai kameros ir toliau neliecia. */
  slicerHome=true;
  slicerRender();
});
$('slicerFlat').addEventListener('click',()=>{
  if(!slicerRaw)return; const s=slicerTr.scale;
  slicerTr=slicerMod.autoOrient(slicerRaw).tr; slicerTr.scale=s; slicerRender();});
$('slicerFlip').addEventListener('click',()=>{if(slicerRaw){slicerTr.rx+=2;slicerRender();}});
$('slicerRotX').addEventListener('click',()=>{if(slicerRaw){slicerTr.rx++;slicerRender();}});
$('slicerRotZ').addEventListener('click',()=>{if(slicerRaw){slicerTr.rz++;slicerRender();}});
/* Pjaustymas ir issaugojimas. Archyvas keliauja ESAMU ikelimo keliu - tuo
   paciu, kuriuo ateina PrusaSlicer siuntiniai; printeriui naujo kodo nereikia. */
let slicerOut=null;   // supjaustytas rezultatas, laukiantis sprendimo

/* Tikra sluoksnio kauke. 3D vaizdas glotnina pavirsiu, tad plona 0.4 mm supporto
   gija jame tiesiog dingsta - o butent ja ir reikia patikrinti. Cia rodoma ta
   pati PNG, kuri keliauja i archyva: nedidinta, be glotninimo. Nieko naujo
   neskaiciuojam - failai jau atmintyje (V 08-13). */
let slicerMaskOn=false, slicerView=0;
/* Einamas sluoksnis. Iki 08-17 ji laike paslepto lauko `value` - elementas buvo
   nematomas, bet kodas ji skaite kaip busena. Dabar busena yra busena, o vienintelis
   slankiklis (`#gl3dLayerRange`) tik ja rodo. */
let slicerLayerN=1;
function slicerMaskDraw(n){
  const box=$('slicerMask'), cv=$('slicerMaskCv');
  if(!box||!cv||!slicerMaskOn||!slicerOut||!slicerOut.files)return;
  const i=Math.max(1,Math.min(slicerOut.layers,n||1))-1;
  const f=slicerOut.files[i];
  if(!f||!f.data)return;
  const url=URL.createObjectURL(new Blob([f.data],{type:'image/png'}));
  const im=new Image();
  im.onload=()=>{
    const ctx=cv.getContext('2d');
    ctx.imageSmoothingEnabled=false;
    ctx.drawImage(im,0,0);
    /* Supportai perpiesiami KITA spalva. Sluoksnio nuotrauka juoda-balta -
       is jos neatskirsi, kur detale, o kur atrama; o butent tai ir reikia
       matyti (V 08-13, butina). Spalva tik ekrane - i archyva keliauja
       nepaliesta nuotrauka. */
    const pv=slicerOut.preview, s=slicerOut.supports;
    if(pv&&pv.supportSlices&&pv.supportSlices.length){
      /* Tikra atramu kauke: variklis grazina ta pati rastra, kaip sluoksni, tik
         su vienetais ten, kur atrama ar raftas. Anksciau cia buvo apytiksliai
         diskai, piesti is atramu saraso - jie sake „maždaug cia", o ne „stai".
         Kauke retesne uz sluoksnius (iki 160 per visa auksti), tad imam
         artimiausia pagal auksti. */
      const K=pv.supportSlices.length;
      const k=Math.min(K-1,Math.round(i/Math.max(1,slicerOut.layers-1)*(K-1)));
      const m=pv.supportSlices[k], gw=pv.gw||320, gh=pv.gh||240;
      if(m&&m.length>=gw*gh){
        const off=document.createElement('canvas');
        off.width=gw; off.height=gh;
        const id=off.getContext('2d').createImageData(gw,gh);
        for(let q=0,t=0;q<gw*gh;q++,t+=4){
          if(m[q]===1){id.data[t]=126;id.data[t+1]=166;id.data[t+2]=216;id.data[t+3]=255;}
        }
        off.getContext('2d').putImageData(id,0,0);
        ctx.drawImage(off,0,0,cv.width,cv.height);
      }
    }else if(s&&s.list&&s.list.length&&slicerMod&&slicerMod.pillarDiscs){
      /* Senasis modulis kaukes negrazina - jam lieka diskai. */
      const z=(i+0.5)*0.05, pad=slicerMod.SUP?slicerMod.SUP.padMm:1.5;
      let d=slicerMod.pillarDiscs(s.list,z);
      if(z>=pad&&s.braceList&&slicerMod.braceDiscs)
        d=d.concat(slicerMod.braceDiscs(s.braceList,z));
      const P=slicerMod.PLATE, sx=cv.width/P.x, sy=cv.height/P.y;
      ctx.fillStyle='#7ea6d8';
      for(const c of d){
        ctx.beginPath();
        ctx.arc((c.x+P.x/2)*sx,(c.y+P.y/2)*sy,Math.max(0.8,c.r*sx),0,6.2832);
        ctx.fill();
      }
    }
    URL.revokeObjectURL(url);
  };
  im.onerror=()=>URL.revokeObjectURL(url);
  im.src=url;
  box.style.display='flex';   // drobe centruojama: juodas staciakampis IR yra plokste
}
/* Plokscias vaizdas = pati plokste, tad langas jam pasidaro tokiu pat santykiu
   (320:240). Kitaip aplink juoda staciakampi likdavo tuscias kraštas: pirma ten
   prasisviesdavo 3D scena, paskui ji uzdaziau korteles spalva - ir abu variantai
   buvo apie ta pati, kad langas ir plokste ne to paties pavidalo (V 08-19).
   Aukstis ribojamas lango auksciu: geriau siauresnė plokste, nei blokas, kurio
   apacios nematyti. Ankstesnis aukstis grazinamas iseinant - jis gali buti
   nustatytas „lango didinimo" mygtuko. */
let slicerStageH=null;
function slicerFlatStage(on){
  const st=$('previewStage'); if(!st)return;
  if(on){
    if(slicerStageH===null)slicerStageH=st.style.height||'';
    const w=st.clientWidth||st.getBoundingClientRect().width;
    if(!w)return;
    let h=Math.round(w*240/320);
    h=Math.min(h,Math.round(innerHeight*0.78));
    st.style.height=h+'px';
  }else if(slicerStageH!==null){
    st.style.height=slicerStageH;
    slicerStageH=null;
  }
  /* Renderis savo dydi persiskaiciuoja per lango „resize" - be sio zenklo
     3D drobe liktu senojo aukscio ir vaizdas issitemptu. */
  try{window.dispatchEvent(new Event('resize'));}catch(e){}
}
function slicerMaskSet(on){
  slicerMaskOn=!!on&&!!slicerOut&&!!slicerOut.files;
  const b=$('gl3dMask'); if(b)b.classList.toggle('on',slicerMaskOn);
  const box=$('slicerMask'), cv=$('slicerMaskCv');
  if(!slicerMaskOn){
    if(box)box.style.display='none';
    /* Drobe isvaloma: kitaip kitas ijungimas trumpam parodytu sena sluoksni. */
    if(cv){const c=cv.getContext('2d'); c&&c.clearRect(0,0,cv.width,cv.height);}
    return;
  }
  /* Slankiklio prasme dviejuose rezimuose skiriasi: 3D jis sako „kiek aukscio
     rodyti" (gale - visas daiktas), kaukeje - „kuri sluoksni rodyti" (gale -
     pati virsune, dazniausiai juodas kvadratas). Todel ijungiant kauke ties
     pabaiga persistojam i vidury: kitaip mygtukas atrodo negyvas (V 08-13). */
  let n=slicerLayerN||slicerOut.layers;
  if(n>=slicerOut.layers)n=Math.max(1,Math.round(slicerOut.layers/2));
  slicerShowLayer(n);
}
/* Supportai atsiranda patys, tad kortelei lieka tik pasakyti, kas gavosi -
   jokiu nustatymu neatsiranda (V 08-12). */
const SUP_IDLE='Supports and a raft are added on their own, wherever the part hangs - nothing to set.';
function slicerSupportFacts(s){
  const a=$('slicerSupports'), b=$('slicerIslands');
  if(!a||!b)return;
  b.textContent='';
  if(s===null){a.textContent=SUP_IDLE;return;}              // dar nepjaustyta
  /* Modulis be supportu (senas, is narsykles keso). Tyleti negalima: zmogus
     matytu „pridedami patys" ir manytu, kad jie yra (auditor find, 08-13). */
  if(!s){a.textContent='This page is running an older slicer module, so NO supports were added. Reload with Ctrl+F5 and slice again.';return;}
  a.textContent=s.pillars
    ?'Supports: '+s.pillars+(s.pillars===1?' pillar':' pillars')
      +(s.onModel?' ('+s.onModel+' standing on the part itself)':'')
      +(s.raft?' · raft on':'')
    :'No supports needed - nothing on this part hangs in the air.';
  /* Supportai patys pasitikrina: suslicinus SU jais dar kartą ieškoma kabanciu
     vietu. Jei atsirado nauju - tai MUSU pacio klaida, ir apie ja butina
     pasakyti, o ne tyliai issaugoti (V 08-13). */
  b.textContent=s.hanging
    ?'⚠ '+s.hanging+' support'+(s.hanging===1?'':'s')+' would print hanging in the air - do not save this, tell the maintainer.'
    :s.islands
      ?s.islands+(s.islands===1?' spot starts':' spots start')+' in mid-air (the lowest at layer '
        +s.firstIsland+') - all held by supports.'
      :'';
  b.style.color=s.hanging?'#e8a020':'';
}
/* Sluoksnio valdikliai gimsta ir dingsta kartu: slankiklis, kaukes mygtukas ir
   pati kauke. Anksciau trys vietos slepe tik slankikli. */
function slicerLayerUI(on){
  const L=$('gl3dLayer'); if(L)L.style.display=on?'flex':'none';
  const b=$('gl3dMask'); if(b)b.style.display=on?'':'none';
  if(!on){slicerMaskSet(false); slicerFlatStage(false); slicerView=0; slicerLayerN=1;
          if(window.gl3dSupports)gl3dSupports(null);}
  else slicerSetView(slicerView);
}
const VIEW_TITLES=[
  '3D with supports - shapes, not pictures. Click for the printed layers.',
  'Printed layers - exactly what the printer builds. Click for one true layer.',
  'True layer - one picture, no smoothing. Click to go back to 3D.'];
function slicerSetView(v){
  slicerView=((v%3)+3)%3;
  const b=$('gl3dMask');
  if(b){b.classList.toggle('on',slicerView!==0); b.title=VIEW_TITLES[slicerView];}
  slicerMaskSet(slicerView===2);
  /* Tikro sluoksnio vaizde priartinimas ir narvas nieko nedaro: tai plokscia
     kauke, pikselis prie pikselio, ir tokia ji turi likti (tam ji ir yra).
     Mygtukai slepiami, o ne uzrakinami - valdiklis, kuris nieko nekeicia, meluoja
     labiau uz nesancio mygtuko nebuvima (V 08-19). */
  slicerFlatStage(slicerView===2);
  {const plokscia=slicerView===2;
   const cg=$('gl3dCage'); if(cg)cg.style.display=plokscia?'none':'';
   document.querySelectorAll('#gl3dZoom [data-zoom]')
     .forEach(b=>{b.style.display=plokscia?'none':'';});
   /* Sukimo ir stumdymo padai bei ju pagalba - irgi 3D reikalas. Plokscioje
      kaukeje jie ne tik nieko nedaro, bet ir gula ant paties vaizdo (V 08-19).
      Grazinam su tais paciais display, kokius duoda tiltas. */
   [['gl3dPad','grid'],['gl3dRot','grid'],['gl3dHelp','block']].forEach(([id,d])=>{
     const e=$(id); if(e)e.style.display=plokscia?'none':d;});}
  if(slicerView!==2)slicerBuildView();
  if(slicerOut)slicerShowLayer(slicerLayerN||slicerOut.layers);
}

/* Slankiklis valdo AUKSTI, ne rodo atskira pjuvi: tas pats pieseejas, kuris
   piesia spausdinimo progresa, tik frakcija ateina is slankiklio, o ne is
   printerio. Vienas algoritmas visur (V 08-12). */
/* Tinklelis sulipdomas VIENA karta (visas daiktas), o slankiklis tik judina
   pjovimo plokstuma. Anksciau iki 100 % piese vienas pieseejas, o ties 100 %
   perjungdavo i kita - vaizdas soktelėdavo (V 08-12). */
/* Trys vaizdai to paties rezultato, ir slankiklis veikia visuose:
   0 - tikra geometrija: modelis ir supportai kaip daiktai, ne kaip nuotrauku
       krūva. Cia matosi, kokios formos supportas ir kur jis liečia detale.
   1 - suslicinti sluoksniai: ka spausdintuvas TIKRAI padarys, su visais
       laipteliais.
   2 - viena tikra kauke: vieno sluoksnio nuotrauka be jokio glotninimo.
   (V 08-13: „matom 3D su suportais, 2d slicinta ir 3d slicinta".) */
/* Kelios STL dalys - vienas tinklas. `geometrija()` atiduoda atramas ir rafta
   atskirai, o vaizdui jie yra tas pats „ne detale". */
const slicerJoin=(bufs)=>{
  const parts=[];
  for(const b of bufs){
    if(!b||b.length<=84)continue;              // tuscias STL: vien antraste
    const r=slicerMod.parseSTL(b.buffer.slice(b.byteOffset,b.byteOffset+b.length));
    const pos=r&&(r.positions||r);
    if(pos&&pos.length)parts.push(pos);
  }
  if(!parts.length)return null;
  if(parts.length===1)return parts[0];
  let n=0;parts.forEach(a=>n+=a.length);
  const out=new Float32Array(n);
  let o=0;parts.forEach(a=>{out.set(a,o);o+=a.length;});
  return out;
};
const slicerGeomView=(home)=>{
  if(!slicerRaw||!slicerTr||!slicerMod)return false;
  if(!slicerMod.geometrija&&!slicerMod.supportMesh)return false;
  const placed=slicerMod.place(slicerRaw,slicerTr);
  if(window.gl3dMesh)gl3dMesh(slicerMod.toSceneMesh(placed),false);
  /* Sluoksniu vaizdas si lauka uzpildo pats, o geometrijos vaizdas iki siol
     ne - tad pjuvis matavosi pagal ANKSTESNI modeli. */
  {const H=slicerModelH(); if(H)slicesCache.modelH=H;}
  const s=slicerOut&&slicerOut.supports;
  if(slicerMod.geometrija&&slicerOut){
    /* WASM variklis atramu saraso nebeduoda - jos ateina kaip tikra geometrija
       (STL), tomis paciomis koordinatemis, kaip `place()` rezultatas. Tad
       vaizdas rodo ne musu spejima apie atramas, o tas pacias atramas, kurios
       ir isspausdinamos. */
    const mine=slicerOut;                      // per ta laika gali buti supjaustyta is naujo
    slicerMod.geometrija().then(g=>{
      if(slicerOut!==mine||!window.gl3dSupports)return;
      const pos=g?slicerJoin([g.supports,g.pad]):null;
      gl3dSupports(pos?slicerMod.toSceneMesh(pos):null);
      if(home&&window.gl3dFrameAll)gl3dFrameAll();
    }).catch(()=>{});
  }else if(window.gl3dSupports)
    gl3dSupports(s&&s.list&&s.list.length&&slicerMod.supportMesh
      ?slicerMod.toSceneMesh(slicerMod.supportMesh(s.list,s.braceList)):null);
  /* Ka tik suslicinta - vaizdas pastatomas taip, kad DAIKTAS SU SUPPORTAIS
     tilptu visas. Anksciau kamera likdavo ten, kur buvo, ir modelis atsirasdavo
     kazkur lango apacioje (V 08-13). Perjungiant rezimus kameros neliecia -
     zmogus ka tik pats prisitaike zvilgsni. */
  if(home&&window.gl3dFrameAll)gl3dFrameAll();
  return gl3dUp();
};
const slicerBuildView=(home)=>{
  if(!slicerOut)return false;
  /* Ta pati priezastis, kaip slicerRender: si funkcija perima perziuros kortele ir
     perrasoma globalu `slicesCache`, tad spausdinant ji ismestu gyva sluoksniu
     srauta ir jis butu traukiamas is naujo (auditas 08-17). */
  if(statusData&&statusData.busy)return false;
  if(slicerView===0){slicerOwns(true);
    if(window.key3dReset)key3dReset();
    return slicerGeomView(home);}
  if(window.gl3dSupports)gl3dSupports(null);
  if(!slicerOut.preview)return false;
  const p=slicerOut.preview;
  slicesCache={name:'‹slicer›:'+(slicerFileName||'?')+':'+slicerOut.layers+':'+p.modelH.toFixed(2),mode:'current',slices:p.slices,
               gw:p.gw,gh:p.gh,modelH:p.modelH,layers:slicerOut.layers};
  /* Naujas pjaustymas privalo persipiesti nuo nulio, nesvarbu, ka kesas
     mano: vien vardo uzteko, kad antras modelis pakliutu ant pirmojo
     geometrijos ir atrodytu istemptas (V 08-12). */
  if(window.key3dReset)key3dReset();
  lastPrevFrac=-1;
  slicerOwns(true);
  drawIso($('printPreviewCanvas'),1);   // visas daiktas + GPU
  return gl3dUp();
};
/* Modelio auksčio VIENAS saltinis: perziura, o jos nesant - sluoksniu skaicius.
   Ta pati reiksme turi guleti ir `slicesCache.modelH`, nes is jos `gl3dClip`
   dali paverčia milimetrais. */
const slicerModelH=()=>{
  if(!slicerOut)return 0;
  const pv=slicerOut.preview;
  return (pv&&pv.modelH)||slicerOut.layers*0.05;
};
const slicerShowLayer=n=>{
  if(!slicerOut)return;
  if(statusData&&statusData.busy)return;   // ta pati priezastis, kaip slicerRender
  slicerLayerN=n;
  /* `gl3dClip` laukia DALIES (0..1), ne milimetru: viduje ji dauginama is
     modelio auksčio. Cia keliavo milimetrai, tad plokstuma nuskrisdavo virs
     modelio ir slankiklis atrodydavo nieko nedarantis (V 08-19). */
  const H=slicerModelH();
  if(gl3dUp())gl3dClip(n>=slicerOut.layers||!H?null:Math.max(0,Math.min(1,n*0.05/H)));
  if(slicerMaskOn)slicerMaskDraw(n);
  /* Sluoksnio numeris PRIE paties slankiklio - taip daro visi slicer'iai. */
  {const N=$('gl3dLayerNo'); if(N)N.textContent=n+' / '+slicerOut.layers;}
  {const R=$('gl3dLayerRange'); if(R&&Number(R.value)!==n)R.value=n;}
};

/* Pjaustymas trunka minutemis, o iki 08-17 is jo nebuvo isejimo: mygtukai
   uzrakinti, ir apsigalvojus liko tik perkrauti puslapi (V klausimas). Modulis
   nutraukimo nemoka, bet jam to ir nereikia - jis kas kelis sluoksnius kviecia
   MUSU eigos funkcija, o is jos galima ismesti klaida. Ji niekur viduje nera
   gaudoma, tad `slice()` nutrūksta ties artimiausiu sluoksniu (paieskoje kas 32,
   piesime kas 8) ir isnyra cia, apacioje, kaip iprasta klaida. */
const SLICE_STOP='__slicer_stopped__';
$('slicerGo').addEventListener('click',async()=>{
  /* Tas pats mygtukas, kuris pradejo, ir sustabdo: kito ieskoti nereikia, o
     eilute lieka dvieju mygtuku ploCio. */
  if(sliceRunning){sliceStopWanted=true;
    $('slicerGo').disabled=true; $('slicerGo').textContent='Stopping…';
    /* Ir ant drobes: nutraukimas ivyksta tik ties artimiausiu eigos kvietimu, o
       tarp ju (antras patikros praejimas, ZIP surinkimas) juosta nejuda - be sio
       uzraso atrodytu, kad uzstrigo (auditas 08-17). */
    paintPreviewProgress($('printPreviewCanvas'),'Stopping…',null,true);
    return;}
  if(!slicerRaw||!slicerMod)return;
  if(slicerBusyStop())return;
  const placed=slicerMod.place(slicerRaw,slicerTr);
  const f=slicerMod.fitCheck(slicerMod.bounds(placed).size);
  if(!f.fits){msg('It does not fit yet - turn or scale it first.',true);return;}
  const go=$('slicerGo'), prog=$('slicerProg');
  sliceRunning=true; sliceStopWanted=false;
  go.textContent='Stop'; go.disabled=false;   // vienintelis gyvas mygtukas
  slicerButtons(false);
  try{
    prog.textContent='';
    paintPreviewProgress($('printPreviewCanvas'),'Slicing\u2026',0,true);
    const t0=performance.now();
    /* Du praejimai, viena juosta: pirma ieskoma, kur daiktas kabo (pirmas
       trecdalis), tada piesiami sluoksniai. Kitaip juosta nueitu iki galo ir
       pradetu is naujo - atrodytu, kad kazkas uzstrigo. */
    const supType=(document.querySelector('input[name=slicerSupType]:checked')||{}).value||'regular';
    const r=await slicerMod.slice(placed,{antialias:$('slicerAA').checked,
      supportType:supType,name:(slicerFileName||'print').replace(/\.stl$/i,'')},
      (done,total,phase)=>{
        if(sliceStopWanted)throw new Error(SLICE_STOP);
        const f=phase==='scan'?done/total*0.3
               :phase==='draw'?0.3+done/total*0.7
               :done/total;                      // senas modulis - viena faze
        const pct=Math.round(f*100);
        const what=phase==='scan'?'Looking for overhangs':'Slicing';
        /* VIENA vieta, ir ta vieta - vaizdas. Ta pati eilute stovejo ir korteleje,
           ir ant drobes; SD ikelimo atveju sis dubliavimas jau isnaikintas, tad ir
           cia elgiames vienodai (V 08-17). Sluoksniu skaicius keliauja kartu su
           uzrasu - be jo vaizde nebesimatytu, kiek ju is viso.
           Skaiciai - ANTROJE eiluteje (`\n`), kaip visur kitur pulte: vienoje
           eiluteje „Looking for overhangs 28% (161 / 173 layers)" issitempdavo per
           visa drobe ir `fitFont` dar sumazindavo srifta, kad tilptu (V 08-17). */
        prog.textContent='';
        paintPreviewProgress($('printPreviewCanvas'),
          what+' '+pct+'%\n'+done+' / '+total+' layers',f,true);
      });
    /* Paspaudus „Stop" po PASKUTINIO eigos kvietimo, pjaustymas spetu baigtis
       sekmingai, ir zmogus gautu rezultata, kurio ka tik atsisake (auditas 08-17).
       Tas pats zenklas cia uzdaro ta plysi. */
    if(sliceStopWanted)throw new Error(SLICE_STOP);
    /* Dervos ivertis - PRINTERIO matematika, ne mano: koeficientas ir priedas
       imami is jo nustatymu, tad rodomas skaicius yra tas, kuri jis ir duos. */
    const c=connectConfig||{};
    const ml=(r.rawMl*(Number(c.resinCalFactor)||1)+(Number(c.resinFixedMl)||0));
    /* Nesaugom is karto: pirma parodom, ka gavom. Issaugojimas - atskiras
       sprendimas, kai sluoksniai atrodo teisingai (V 08-12). */
    if(!r.files){slicerOut=null;
      prog.textContent='The slicer module is out of date - reload the page (Ctrl+F5).';
      return;}
    slicerOut=r; slicerOut.ml=ml;
    slicerSupportFacts(r.supports);
    prog.textContent='Sliced in '+((performance.now()-t0)/1000).toFixed(1)+' s \u00b7 '
      +r.layers+' layers \u00b7 ~'+ml.toFixed(1)+' ml';
    slicerLayerN=r.layers;                          // pradzioj - visas daiktas
    show('printPreviewBarFill',false);
    {const R=$('gl3dLayerRange');
     if(R){R.min=1;R.max=r.layers;R.value=r.layers;}
     slicerLayerUI(true);}
    slicerBuildView(true);        // ka tik suslicinta -> parodyti visa
    $('slicerName').disabled=false;
    /* Tas pats modelis du kartus nepjaunamas: mygtukas atsirakina tik kai
       kas nors pasikeicia arba ikeliamas naujas failas (V 08-12). */
    $('slicerGo').disabled=true;
    /* Vienintelis dalykas, kuri dar reikia irasyti, - tad kursorius ten. */
    $('slicerName').focus();
    $('slicerSave').disabled=false;
    $('slicerDiscardLink').style.visibility='visible';
    slicerStep();
    slicerShowLayer(slicerLayerN);
  }catch(e){
    /* Sustabde ne klaida: nei raudono snako, nei „kazkas nutiko" - modelis
       lieka toks pat, tik nesupjaustytas, ir viskas grizta i „galima pjauti". */
    if(e&&e.message===SLICE_STOP){
      msg('Slicing stopped.');
      slicerRender();                  // vaizde vel modelis, ne eigos uzrasas
      /* Uzrasas - PO perpiesimo: `slicerRender` per `slicerInvalidate` uzrasytu
         „Settings changed - slice again to save.", ir sustabdymas atrodytu kaip
         nustatymu pakeitimas (auditas 08-17). */
      prog.textContent='Slicing stopped. Nothing was changed.';
    }else{
      prog.textContent=e.message;
      msg(e.message,true);
    }
  }finally{
    sliceRunning=false; sliceStopWanted=false;
    go.textContent='Slice';
    /* Mygtukas atrakinamas VISADA, ir tai ne aplaidumas: virsuje (po sekmingo
       pjaustymo) jis uzrakinamas, bet atrakinti ji paskui butu nebe kam -
       `slicerInvalidate` (pasukus, pakeitus masteli) prie sio mygtuko neprieina,
       tad likes uzrakintas jis uzrakintu ir pakeisto modelio pjaustyma. Ta pati
       eilute stovejo cia ir iki 08-17. */
    go.disabled=false;
    slicerButtons(true);
  }
});

$('slicerAA').addEventListener('change',()=>slicerInvalidate());
/* Perjungus atramu tipa, ankstesnis rezultatas nebegalioja - kitaip „Save"
   issaugotu tai, ko ekrane jau nebera. */
document.querySelectorAll('input[name=slicerSupType]').forEach(r=>
  r.addEventListener('change',()=>slicerInvalidate()));
/* Uzdarymas VIENOJE vietoje: varnele, bakstelėjimas i vaizda ir modelio
   pasikeitimas visi grazina irankiu juosta - kitaip ji dingtu visam. */
/* Slicer'io vaizdas VISADA detalus, tad mygtukas rodo busena, o ne
   pasirinkima. Uzraktas budavo dedamas tik PO slicinimo, todel su ikeltu
   modeliu mygtukas vis dar persijungdavo ir gadindavo vaizda (V 08-12). */
const slicerDetLock=on=>{
  const b=$('gl3dDet'); if(!b)return;
  if(on){b.classList.add('on');b.disabled=true;
         b.title='Slicer preview is always detailed';}
  else{b.disabled=false;b.title='Detailed view';
       b.classList.toggle('on',VIEW_DETAIL);}
};
/* Apatineje juostoje trys grupes: sukimas (kaireje), irankiai (viduryje),
   detalumas+priartinimas (desineje). Scale eilutei reikia VISO plocio -
   kitaip patvirtinimo varnele uzlipa ant kaimynu (V 08-12). */
const POP_ROW={gl3dTools:'flex',gl3dRot:'grid',gl3dZoom:'flex'};
const popRow=show=>Object.keys(POP_ROW).forEach(id=>{
  const e=$(id); if(e)e.style.display=show?POP_ROW[id]:'none';});
/* Vaizdo savininkas ir detalumo uzraktas keiciasi KARTU. Anksciau uzraktas
   buvo dedamas rankomis, ir is penkiu vietu, kur vaizdas pereina kitam,
   atrakinimas buvo tik vienoje - todel po spausdinimo ar uzdarius sliceri
   mygtukas likdavo uzrakintas (V 08-12). */
/* Ta pati veliavele ir ant `window`: 3D scena gyvena ATSKIRAME modulyje, o jai
   butina zinoti, kad tinklelis dabar ne is SD sluoksniu, o slicerio (V 08-17). */
/* Narvas sliceryje: pagal nutylejima isjungtas (cia svarbus daiktas, ne turis),
   bet mygtukas lieka - anksciau narvas buvo rodomas ir jokio budo ji nuimti
   nebuvo (V 08-19). Perziuros pasirinkima grazinam isejus, kad slicerio apsilan-
   kymas neperrasytu zmogaus nustatymo. */
let slicerCageWas=null;
const slicerCage=enter=>{
  if(!window.gl3dCage||!window.gl3dCageOn)return;
  if(enter){
    if(slicerCageWas!==null)return;
    slicerCageWas=gl3dCageOn();
    if(slicerCageWas)gl3dCage(false);
  }else{
    if(slicerCageWas===null)return;
    gl3dCage(slicerCageWas);
    slicerCageWas=null;
  }
  if(typeof syncCageBtn==='function')syncCageBtn();
};
const slicerOwns=v=>{slicerOwnsPreview=v; window.slicerOwnsPreview=v;
                     slicerDetLock(v); slicerCage(v); if(!v)slicerLayerUI(false);};
/* Blokas atsidaro ir uzsidaro svarus: senas modelis, jo vardas ir vaizdas
   negali persekioti tarp atidarymu (V 08-12). */
const slicerReset=()=>{
  slicerOut=null; slicerRaw=null; slicerTr=null; slicerBudget=0;
  slicerFileName=''; slicerHome=true;
  slicerButtons(false);
  $('slicerGo').disabled=true; $('slicerSave').disabled=true;
  $('slicerName').value=''; $('slicerName').disabled=true;
  $('slicerFile').value='';
  $('slicerDims').textContent=''; $('slicerProg').textContent='';
  $('slicerInfo').textContent='Choose an STL file to begin.';
  $('slicerDiscardLink').style.visibility='hidden';
  slicerSupportFacts(null);
  if(gl3dUp())gl3dClip(null);
};
const popClose=()=>{
  const p=$('gl3dPop'); if(!p||p.style.display==='none')return;
  p.style.display='none'; popRow(true);
};

/* Irankiu juostos ant vaizdo valdiklis. Perkeldamas ji pirma karta praleidau -
   mygtukai matesi, bet nieko nedare (V 08-17: „toolsai yra bet neveiksnus").
   Jie nieko neskaiciuoja patys: kiekvienas tik paspaudzia atitinkama korteles
   mygtuka, kuris CSS'e pasleptas (#slicerTools{display:none}). Viena logika, du
   pavidalai - kortele lieka vieninteliu tikruoju saltiniu.
   Kabinam PAGRINDINEJE srityje, ne 3D IIFE viduje: ten popRow/popClose dar
   nebutu inicializuoti tuo metu, kai trys.js baigia kroviesi. */
{
  const tls=document.getElementById('gl3dTools');
  if(tls)tls.addEventListener('click',e=>{
    const t=e.target&&e.target.dataset&&e.target.dataset.tool; if(!t)return;
    e.stopPropagation();
    if(t==='scale'){
      const p=$('gl3dPop'); if(!p)return;
      if(p.style.display!=='none')popClose();
      else{p.style.display='flex';popRow(false);}
      return;
    }
    const id={fit:'slicerAutoFit',flat:'slicerFlat',flip:'slicerFlip',
              tilt:'slicerRotX',rot:'slicerRotZ'}[t];
    const b=id&&$(id); if(b&&!b.disabled)b.click();
  });
  const pd=document.getElementById('popDone');
  if(pd)pd.addEventListener('click',e=>{e.stopPropagation();popClose();});
}
/* Popup'ai NIEKO neskaiciuoja patys - tik persiuncia i esamus laukus. */
/* Apatine riba - 0,1 %, ne 10 %: plokste 40,8 x 30,6 mm, tad zaislo dydzio STL
   (o tokiu internete pilna) telpa tik ties keliais procentais, ir sena riba
   tyliai grazindavo ji atgal i netelpanti (V 08-17). Virsutine lieka 300 %. */
const SCALE_MIN_PCT=0.1;
const popScaleApply=pct=>{
  if(!slicerRaw)return;
  const v=Math.max(SCALE_MIN_PCT/100,Math.min(3,pct/100));
  slicerTr.scale=v; slicerRender();
};
{const pr=$('popScaleRange');
 /* Zyme uzsideda PRIES `input`, tad pirmas pat perpiesimas jau zino, kad vyksta
    tempimas, ir slankiklio ribu nebejudina. Nuimam ir per `pointercancel`
    (pirstas nuslydo) bei `keyup` (Escape ar raide, kuri reiksmes nekeicia -
    antras auditas 08-17: be jo zyme galejo likti uzstrigusi). */
 ['pointerdown','keydown'].forEach(ev=>pr.addEventListener(ev,()=>{scaleDragging=true;}));
 ['pointerup','pointercancel','keyup','change','blur'].forEach(ev=>
   pr.addEventListener(ev,()=>{
     if(!scaleDragging)return;
     scaleDragging=false;
     /* NE `slicerRender()`: jis pirmu veiksmu kviecia `slicerInvalidate()`, tad
        vien bakstelejimas i slankikli (be jokio judesio) issviestu ka tik
        supjaustyta rezultata ir uzrakintu „Save" - mano paties regresija, pagauta
        antro audito. Ribas grazinam TIESIOGIAI, is paskutinio piesimo matmenu:
        jokio `place()` per visas virsunes. */
     if(slicerRaw&&slicerLastBounds)slicerScaleUI(slicerLastBounds);
   }));
 pr.addEventListener('input',e=>popScaleApply(Number(e.target.value)));}
$('popScalePct').addEventListener('change',e=>popScaleApply(Number(e.target.value)));
/* Aukstis milimetrais: zmogus dazniau zino, kokio dydzio nori daiktas, nei
   kiek procentu tam reikia (V 08-12). */
$('popScaleMm').addEventListener('change',e=>{
  if(!slicerRaw)return;
  const want=Number(e.target.value); if(!(want>0))return;
  const now=slicerMod.bounds(slicerMod.place(slicerRaw,slicerTr)).size[2];
  if(now>0)popScaleApply(slicerTr.scale*(want/now)*100);
});
/* Kaukes jungiklis stovi salia priartinimo, nes tai to paties vaizdo kita puse. */
{const b=$('gl3dMask');
 if(b){b.addEventListener('click',e=>{e.stopPropagation();slicerSetView(slicerView+1);});
       b.addEventListener('pointerdown',e=>e.stopPropagation());}}
$('gl3dLayerRange').addEventListener('input',e=>slicerShowLayer(Number(e.target.value)));
$('gl3dLayerRange').addEventListener('pointerdown',e=>e.stopPropagation());

/* Issaugoto modelio vardas keliauja UZ try/finally: pats `finally` baigia
   slicerio darba (nuima jo vaizda, pastato tuscia perziura), tad ka tik padaryto
   modelio atidarymas turi vykti PO jo - kitaip valymas ji cia pat ir nutrintu. */
$('slicerSave').addEventListener('click',async()=>{
  if(!slicerOut)return;
  if(slicerBusyStop())return;
  let savedName='', renamed=false, namesWere=[];
  const nm=($('slicerName').value||'').trim().replace(/[^A-Za-z0-9_-]/g,'');
  if(!nm){msg('Give the model a name first.',true);$('slicerName').focus();return;}
  slicerOut.name=nm;
  const btn=$('slicerSave'), prog=$('slicerProg');
  btn.disabled=true;
  /* Pasisakom, kad printeri uzimam MES. Be sito pultas per savo apklausa raso
     „Printer not answering - an upload, share or other background job..." tame
     paciame lange, kuris ka tik paspaude „Save": eiga sukasi korteleje, o virsuje
     geltonas skundas pačiam ant saves (V 08-18, pasikartojo 2 kartus is 3).
     `uploadBusy` nuimam, kai tik baigiasi BAITAI - toliau snacka teisetai perima
     printerio ispakavimas su tikrais sluoksniu skaiciais. `bgJob` lieka iki galo:
     jei apklausa vis delto prakalbtu, ji pasakys darbo VARDA, ne bendra spėjima. */
  if(typeof uploadBusy!=='undefined')uploadBusy=true;
  if(typeof bgJob!=='undefined')bgJob='Saving to the printer';
  if(typeof syncActionLocks==='function')syncActionLocks();
  try{
    const MB=b=>(b/1048576).toFixed(1);
    /* Dydi zinom iki baito - archyva pagaminom patys, tad eiga tikra, ne
       apsimestine. XHR, nes fetch nepranesa, kiek isejo (V 08-12).
       Grazina konflikto duomenis, jei toks vardas jau yra, arba null. */
    const send=action=>new Promise((res,rej)=>{
      const fd=new FormData();
      if(action)fd.append('action',action);
      fd.append('source','slicer');
      /* ZALIA reiksme, ne `slicerOut.ml`: kalibracija (`resinCalFactor`,
         `resinFixedMl`) pritaikoma printeryje ISDUODANT (R-cal, Network.ino),
         tad atsiuntus jau kalibruota ji suveiktu du kartus. Be sio lauko
         printeris veliau pats perskaito visus sluoksnius nuo korteles ir
         suskaiciuoja ta pati, ka narsykle jau zino (auditas 08-17). */
      if(slicerOut.rawMl>0)fd.append('resin_ml',String(slicerOut.rawMl));
      fd.append('file',slicerOut.blob,slicerOut.name+'.zip');
      const x=new XMLHttpRequest();
      /* `/upload`, o ne `/api/files/local`: pastarasis su tusciu veiksmu
         SAMONINGAI reiskia \u201eperrasyk" - tuo keliu eina PrusaSlicer, kuris i
         klausima atsakyti negaletu. Sliceris yra pulto dalis, tad jam priklauso
         tas pats kelias ir tas pats klausimas, kaip pulto ikelimui: kitaip tas
         pats daiktas, suslicintas antra karta, nutrindavo sena modeli tylomis
         (auditas 08-17). */
      x.open('POST','/upload');
      x.setRequestHeader('X-TinyMaker','1');
      x.upload.onprogress=e=>{
        if(!e.lengthComputable){prog.textContent='Uploading \u2026';return;}
        const p=Math.round(e.loaded/e.total*100);
        prog.textContent='Uploading '+p+'%  ('+MB(e.loaded)+' / '+MB(e.total)+' MB)';
        paintPreviewProgress($('printPreviewCanvas'),'Uploading '+p+'%',e.loaded/e.total);
      };
      x.onload=()=>{
        if(x.status<400){res(null);return;}
        let d=null; try{d=JSON.parse(x.responseText);}catch(err){}
        if(x.status===409&&d&&d.conflict){res(d);return;}
        rej(new Error('upload failed ('+x.status+')'));
      };
      x.onerror=()=>rej(new Error('upload failed - connection lost'));
      x.ontimeout=()=>rej(new Error('upload timed out'));
      x.timeout=300000;
      x.send(fd);
    });
    /* Sarasas PRIES ikelima: is jo veliau atpazistam, kokiu vardu modelis is tikro
       atsirado. Butina del „rename" - zr. `renamed` zemiau. */
    const namesBefore=(typeof filesItems!=='undefined'&&filesItems)
      ? filesItems.filter(i=>i.type==='model').map(i=>i.name) : [];
    {const conflict=await send('');
     if(conflict){
       /* Tas pats langelis, kaip pulto ikelimui - su abieju modeliu palyginimu. */
       const choice=await uploadConflictChoice(conflict);
       if(choice!=='replace'&&choice!=='rename')throw new Error('Save cancelled');
       renamed=choice==='rename';
       await send(choice);
     }}
    /* Ikelta dar nereiskia paruosta: printeris dabar ISPAKUOJA archyva ir tuo
       metu SD priklauso jam. Laukiam, kol atsileis - kitaip pasakytume
       „issaugota", o modelio sarase dar nebutu (V 08-12). */
    /* Baitai suejo. Nuo cia snacka valdo printerio busena (`sdJob`), tad savo
       zyme nuimam - kitaip ispakavimo pranesimas su sluoksniais butu nuslopintas
       (ta sarga sedi `renderSdJob`: „kol MUSU ikelimas siuncia, jo neliesk"). */
    if(typeof uploadBusy!=='undefined')uploadBusy=false;
    if(typeof syncActionLocks==='function')syncActionLocks();
    prog.textContent='Unpacking on the printer \u2026';
    paintPreviewProgress($('printPreviewCanvas'),'Unpacking\u2026',null);
    for(let i=0;i<180;i++){
      await new Promise(r=>setTimeout(r,1000));
      try{
        const st=await api('/api/status',null,8000);
        if(!st.busy||st.sdJob!=='import')break;
        prog.textContent='Unpacking on the printer \u2026 ('+(i+1)+' s)';
      }catch(e){}
    }
    const done=slicerOut;
    /* Pervadinimo atveju TYLIM: prasytas vardas cia dar neteisingas, o blyksnis su
       netikru vardu blogiau nei sekundes tyla - tikra zinute ateina zemiau, kai
       pamatom, kuris modelis atsirado (antras auditas 08-17). */
    if(!renamed)msg('\u201c'+done.name+'\u201d is on the printer.');
    prog.textContent='Saved as \u201c'+done.name+'\u201d \u00b7 '+done.layers
      +' layers \u00b7 ~'+done.ml.toFixed(1)+' ml';
    /* Issaugojimas UZBAIGIA darba: modelis lieka atmintyje, o „Slice" ijungtas
       reikstu, kad atidarius is naujo slicer'is pamena sena STL ir supjaustytu
       ji antra karta (V 08-12). Uzdarymas - kas kita, ten darbas lieka. */
    slicerOut=null; slicerRaw=null; slicerTr=null; slicerBudget=0;
    slicerButtons(false);
    $('slicerGo').disabled=true;
    $('slicerName').value=''; $('slicerName').disabled=true;
    $('slicerFile').value='';
    $('slicerInfo').textContent='Choose an STL file to begin.';
    $('slicerFit').textContent='';
    show('printPreviewBarFill',true);slicerLayerUI(false);slicerSupportFacts(null);
    $('slicerSave').disabled=true;
    $('slicerDiscardLink').style.visibility='hidden';
    /* Padarei modeli - dabar reikia saraso, ne slicer'io (V 08-12). */
    $('slicerBody').style.display='none';
    slicerToggleUI(false);
    sdCollapse(false);
    savedName=done.name;
    namesWere=namesBefore;
  }catch(e){ prog.textContent=e.message; msg(e.message,true); }
  finally{
    btn.disabled=false;
    if(typeof uploadBusy!=='undefined')uploadBusy=false;
    if(typeof bgJob!=='undefined')bgJob='';
    if(typeof syncActionLocks==='function')syncActionLocks();
    /* Kad ir kaip baigesi - „Unpacking..." nebeturi likti kaboti drobeje
       (V 08-12: pranesimas liko, nors failas seniai suejo). */
    slicerOwns(false);
    if(gl3dUp())gl3dClip(null);   // kitas vaizdas neturi likti nupjautas
    {const t=$('gl3dTools'); if(t)t.style.display='none';}
    slicerDetLock(false);
    dashPreviewPlaceholder();
  }
  if(!savedName){loadFiles&&loadFiles();return;}
  /* Uodega irgi yra MUSU darbas: `loadFiles` perskaito korteles sarasa, po jo
     atsidaro perziura - printeris tuo metu vel neatsakineja, ir geltonas issokdavo
     kaip tik po „... is on the printer." (V 08-18). Ta pati zyme, kaip perziuros
     siurbimui: apklausa tyli, kol dirbam. */
  if(typeof setPreviewBusy==='function')setPreviewBusy(true);
  try{
  /* PIRMA palaukiam, kol busena atsileis, ir tik TADA imam sarasa. Atvirksciai
     buvo bergzdzia: `loadFiles` pati turi ankstyva isejima „spausdinant SD
     neskaitom", tad su dar nenuvalyta `statusData.busy` ji grizdavo nieko
     neatnaujinusi - ir „rename" salyga visada kristu i atsargini varianta,
     butent tada, kai jos labiausiai reikia (antras auditas 08-17).
     Riba - LAIKRODIS, ne zingsniai: viena apklausa gali uztrukti iki 4 s, tad
     „20 x 300 ms" blogiausiu atveju butu virtes puse minutes (trecias auditas). */
  /* Riba tikrinama ir PO kreipinio: viena apklausa gali uztrukti iki 4 s, tad
     tikrinant tik pries ji 6 s biudzetas virsdavo ~10 s (ketvirtas auditas). */
  {const until=Date.now()+6000;
   while(Date.now()<until&&typeof uiBusy==='function'&&uiBusy()){
     try{ if(typeof refreshStatus==='function')await refreshStatus(); }catch(e){}
     if(typeof uiBusy==='function'&&!uiBusy())break;
     /* Ne po kvietimo, o PRIES ji: viena apklausa turi 4 s riba, tad pradejus
        nauja rata ties 5,9 s biudzetas vis tiek virstu ~10 s (penktas auditas). */
     if(until-Date.now()<800)break;
     await new Promise(r=>setTimeout(r,300));
   }}
  try{ if(loadFiles)await loadFiles(); }catch(e){}
  /* KURIS modelis atsirado. Prasytas vardas NETINKA, jei ejom „rename" keliu:
     printeris tada issaugo kitu vardu (`uniqueModelName`, Import.ino), o `/upload`
     atsakymas grazina TA PATI prasyta varda - pervadinimas ivyksta veliau, eileje.
     Todel atidaryti pagal prasyta varda reikstu atidaryti SENA modeli, del kurio
     ir kilo konfliktas, ir dar duoti jam „Start" (auditas 08-17, kritinis). */
  let openName=savedName;
  if(renamed){
    /* Ta pati sarga, kaip renkant `namesWere`: jei viena vieta laiko `filesItems`
       galinti neegzistuoti, kita negali to paties kintamojo liesti plika ranka. */
    const now=(typeof filesItems!=='undefined'&&filesItems)?filesItems:[];
    const added=now.filter(i=>i.type==='model'&&namesWere.indexOf(i.name)<0)
                   .map(i=>i.name);
    if(added.length===1){
      openName=added[0];
      msg('Saved as “'+openName+'” - that name was taken.');
      prog.textContent='Saved as “'+openName+'” (the name was taken).';
    }else{
      /* Neaisku, kuris naujas - geriau nieko neatidaryti, nei atidaryti ne ta. */
      openName='';
      prog.textContent='Saved under a new name - pick it from the list.';
      msg('Saved under a new name - pick it from the list.');
    }
  }
  if(!openName)return;   // zyme nuima `finally` zemiau
  /* `typeof`, ne `window.…`: sitas failas sulipdomas i TA PATI pulto <script>, o
     ten viskas paskelbta per `const` - i `window` tokie vardai nepatenka. */
  if(typeof filesShowName==='function')filesShowName(openName);
  /* Ka tik pagamintas modelis atsidaro perziuroje pats: zmogus vis tiek spaustu ta
     pacia eilute, o be perziuros eiluteje neatsiranda ir „Start" - tad be sito
     „issaugota" baigiasi dar dviem paspaudimais iki spausdinimo (V 08-17). */
  if(typeof pickModel==='function')pickModel(openName);
  }finally{ if(typeof setPreviewBusy==='function')setPreviewBusy(false); }
});

$('slicerDiscardLink').addEventListener('click',e=>{
  e.preventDefault(); slicerOut=null;
  show('printPreviewBarFill',true);slicerLayerUI(false);slicerSupportFacts(null);
  $('slicerSave').disabled=true;
  $('slicerDiscardLink').style.visibility='hidden';
  $('slicerProg').textContent='Discarded. Adjust and slice again.';
  slicerRender();
  slicerStep();          // formos irankiai grizta: vel yra ka formuoti
});