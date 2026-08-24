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
  if(statusData&&statusData.busy){
    /* Ne visada spausdinama: dazniausiai tuo metu printeris ISPAKUOJA ka tik ikelta
       faila. `busyBoxText` jau zino tikra darba, tad zinute nebemeluoja (V 08-20). */
    const kas=(typeof busyBoxText==='function'?busyBoxText():'').replace(/\u2026$/,'');
    msg(kas?('Not now - '+kas.charAt(0).toLowerCase()+kas.slice(1)+'.'):'Not while printing.',true);
    return true;}
  return false;
};
/* Kelias nuoseklus, tad oranzinis tik tas zingsnis, kuris einamas: kitaip
   visi trys atrodo vienodai svarbus (V 08-12). */
/* Pjaustymo eiga: deklaruota CIA, o ne prie paties mygtuko, nes `slicerStep`
   ja skaito, o jis kviečiamas anksciau. */
/* Kiekvienas pjaustymas turi savo numeri: sustabdytas darbas fone dar gali
   pasibaigti, ir jo atsakymas neturi nei pakeisti vaizdo, nei atsukti mygtuku,
   kuriuos zmogus jau mato (V 08-20). */
let sliceRunning=false, sliceRun=0;
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
   const go=$('slicerGo'), fitNow=$('slicerFitNow'), send=$('slicerSend'), save=$('slicerSave');
   const netelpa=loaded&&!sliced&&!slicerFits;
   /* Spausdinant sliceris nieko nedaro: pjaustymas piestu i vaizda, kuris priklauso
      spaudiniui, o „Send to printer" vis tiek grizdavo su 409 - printeris tuo metu SD
      neduoda (V 08-20: „spausdinimo metu leidzia slicint, nelogiska"). Failo pasirinkima
      ir nustatymus paliekam: pasiruosti kita modeli spausdinant - normalu. */
   /* Ne tik spaudinys: kol keliauja failas, printeris ji ispakuoja ar trina, rezultato
      vis tiek nebus kur deti - tad blokas uzrakintas VISAM tam laikui. Viena taisykle
      vietoj trijų isimciu, kurias reiktu atsiminti (V 08-20). */
   const spausdina=(typeof uiBusy==='function')?uiBusy()
                   :!!(typeof statusData!=='undefined'&&statusData&&statusData.busy);
   if(go&&!sliceRunning){
     go.disabled=!loaded||sliced||!slicerFits||spausdina;
     go.title=spausdina?'Not while the printer is working'
              :(netelpa?'Does not fit yet - Fast fit, or turn it by hand':'');
     /* Vienas lizdas, trys pavidalai: netelpa -> „Fit it", telpa -> „Slice",
        supjaustyta -> „Send to printer". Mygtukas priesais akis visada yra tas,
        kuris daro kita zingsni (V 08-20). */
     go.style.display=(netelpa||(sliced&&!sliceRunning))?'none':'';
   }
   if(fitNow)fitNow.style.display=netelpa?'':'none';
   if(send){
     send.style.display=(sliced&&!sliceRunning)?'':'none';
     send.disabled=!!(save&&save.disabled)||spausdina;
     send.title=spausdina?'Not while the printer is working':'';
   }
   /* Varda galima irasyti, vos tik yra ka pavadinti - jis nustatymas, ne veiksmas. */
   {const nm=$('slicerName'); if(nm)nm.disabled=!loaded||spausdina;}
   /* Spausdinant uzrakinam VISA bloka, o ne tik atsakinejam „not while printing":
      mygtukas, kuris atrodo gyvas, bet tik issoka su atsisakymu, erzina labiau nei
      prigesintas (V 08-20). „Choose STL" yra <label>, tad `disabled` jam negalioja -
      uzrakinam klase. */
   {const ch=$('slicerChoose'); if(ch)ch.classList.toggle('locked',spausdina);}
   {const ft=$('slicerFitNow'); if(ft)ft.disabled=spausdina;}
   {const sv=$('slicerSave'); if(sv&&spausdina)sv.disabled=true;}
   {const d=$('slicerDiscardLink');
    if(d)d.style.pointerEvents=spausdina?'none':'';}
   /* VISI likusieji kortelės valdikliai - vienu ejimu per konteineri, o ne vardijant
      po viena: atramų jungikliai, glotninimas ir bet kas, kas atsiras veliau. Anksciau
      uzrakinom po mygtuka, ir kaskart likdavo neuzrakintas dar vienas (V 08-20). */
   {const card=$('slicerCard');
    if(card)card.querySelectorAll('input,select,textarea').forEach(e=>{
      if(e.id==='slicerName'||e.id==='slicerFile')return;   // ju busena skaiciuojama auksciau
      e.disabled=spausdina;
    });
    if(card&&spausdina)card.querySelectorAll('button').forEach(b=>{
      if(b.id!=='slicerToggle')b.disabled=true;             // akordeonas lieka gyvas
    });
    /* Ir formos irankiai ant vaizdo - jie yra ta pati kortele, tik kitoje vietoje. */
    const t=$('gl3dTools');
    if(t&&spausdina)t.querySelectorAll('button').forEach(b=>{
      if(b.id!=='gl3dCage'&&b.id!=='gl3dV3'&&b.id!=='gl3dV2')b.disabled=true;
    });}}
  /* Formos irankiai turi prasme tik IKI pjovimo. Po jo jie keicia tai, kas jau
     supjaustyta: rezultatas tyliai issimeta, o zmogus to neprase - jis tiesiog
     paspaude ta, kas buvo ekrane (V 08-19). Grazina „Discard". */
  {const tools=$('gl3dTools');
   if(tools)['fit','fitpro','flat','flip','tilt','rot','scale'].forEach(k=>{
     const b=tools.querySelector("[data-tool='"+k+"']");
     if(b)b.style.display=sliced?'none':'';});
   /* Abi grupes - per vidury, tad jos negali stoveti ant tos pacios eilutes: formos
      irankiai VIRSUJE, vaizdo grupe - eilute po ju. Kai formos irankiu nera (po
      pjaustymo arba spausdinimo perziuroje), vaizdo grupe pakyla i ju vieta - kitaip
      liktu tuscia juosta (V 08-20; nuo 08-21 juostos gyvena virsuje). */
   const zoom=$('gl3dZoom');
   if(zoom){
     /* Pagal BUSENA, ne pagal `display`: uzdarius mastelio langeli irankiai dar
        akimirka buna paslepti, ir grupe atsistodavo ant ju (V 08-20). */
     const yraFormos=loaded&&!sliced;
     /* Atstumas imamas is TIKRO juostos aukscio, ne is skaiciaus: siaurame ekrane ji
        lauzoma i dvi eilutes (54 px), ir su ikaltais 44 px vaizdo grupe atsistodavo ant
        antros eilutes (ismatuota telefono kadre, 08-21). */
     const th=(tools&&tools.offsetHeight)||26;
     zoom.style.top=yraFormos?(th+12)+'px':'8px';
     zoom.style.bottom='auto';
   }
  }
};
window.slicerStep=slicerStep;   // apklausa perskaiciuoja zingsnius (V 08-20)
const slicerButtons=on=>{
  ['slicerAutoFit','slicerAutoFitPro','slicerFlat','slicerFlip','slicerRotX','slicerRotZ']
    .forEach(id=>{const b=$(id);if(b)b.disabled=!on;});
  /* Ta pati busena ir ant vaizdo esantiems - jie tik kita to paties veido puse. */
  const t=$('gl3dTools');
  /* Narvas ir kauke juostoje tik SVECIUOJASI (zr. `slicerBarMerge`) - jie ne apie
     daikto forma, o apie ziurejima, tad slicerio uzraktas ju neliecia. Be sios
     islygos jie uzsirakindavo kartu su formos irankiais ir tokie - negyvi - keliaudavo
     atgal i savo grupe: perziuroje narvas nebesispausdavo visai (V 08-20). */
  const svecias=id=>id==='gl3dCage'||id==='gl3dV3'||id==='gl3dV2';
  if(t){t.querySelectorAll('button').forEach(b=>{if(!svecias(b.id))b.disabled=!on;});
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
  ['filesFilter','filesList','uploadHint'].forEach(id=>{
    const e=$(id); if(e)e.style.display=on?'none':'';
  });
  /* Vietos juostele LIEKA ir suskleistame bloke: ji ir yra ta santrauka - kiek modeliu
     ir kiek vietos, - o sliceriui atsidarius apacioje vis tiek yra tuscios vietos
     (V 08-20). Tada tekstine eilute nebereikalinga: ji kartotu tuos pacius skaicius. */
  /* „Upload…" LIEKA ir suskleistame bloke: tai vienas mygtukas antrastėje, vietos
     nekainuoja, o failo ikelimas neturi priklausyti nuo to, kuris blokas atidarytas
     (V 08-20). */
  const uz=$('sdUsageBox'), yraJuosta=!!(uz&&!uz.classList.contains('hidden'));
  const h=$('sdCollapsedHint');
  if(h){h.style.display=(on&&!yraJuosta)?'block':'none';
        if(on&&!yraJuosta)h.textContent=sdSantrauka();}
  const t=$('sdToggle'); if(t){t.textContent=AKORD_ZENKLAS(!on);
    t.title=on?'Open the model list':'Collapse the model list';
    t.setAttribute('aria-expanded',on?'false':'true');}
};
/* Ka pasako suskleistas SD blokas: kiek modeliu ir kiek vietos liko - abu skaiciai
   jau yra korteleje, tik giliau. Suskleistas blokas turi likti informatyvus, o ne
   virsti tuscia antraste (V 08-20). */
const sdSantrauka=()=>{
  const n=(typeof filesItems!=='undefined'&&filesItems)?filesItems.length:0;
  const laisva=($('sdUsageText')&&$('sdUsageText').textContent||'').trim();
  const kiek=n?(n+(n===1?' file':' files')):'No models yet';
  return laisva&&laisva!=='-'?kiek+' · '+laisva:kiek;
};
/* Uzdarymas - zenklas, ne zodis, ir TA PATI seima, kaip lango didinimas virs
   perziuros: tas pats remelis, dydis ir 13 px piesinys. Atidarymas lieka zodis -
   jo ieskai, tad jis turi kviesti; uzdarymo ieskoti nereikia, jis randamas ten,
   kur ka tik paspaudei (V 08-17). */
const ICON_SLICER_CLOSE="<svg viewBox='0 0 16 16' width='13' height='13' fill='none' stroke='currentColor' stroke-width='1.5' stroke-linecap='round'><path d='M4.2 4.2l7.6 7.6M11.8 4.2l-7.6 7.6'/></svg>";
/* Abu akordeono blokai turi TA PATI jungikli: tas pats mygtuko pavidalas (kaip
   „isdidinti" virs peržiūros) ir ta pati rodykle - zemyn, kai atidaryta, i desine, kai
   suskleista. Anksciau sliceris rodė zodi „Open" ir kryziuka, o SD - rodykle: du
   skirtingi zenklai tam paciam veiksmui (V 08-20). */
const AKORD_ZENKLAS=on=>on?'▾':'▸';
const slicerToggleUI=open=>{
  const t=$('slicerToggle'); if(!t)return;
  /* Busena jau tokia - iseinam: si funkcija kvieciama ir is apklausos (1 Hz). */
  if(t.classList.contains('narrow')===open&&t.textContent===AKORD_ZENKLAS(open))return;
  t.classList.add('narrow');
  t.textContent=AKORD_ZENKLAS(open);
  t.title=open?'Collapse the slicer':'Open the slicer';
  t.setAttribute('aria-expanded',open?'true':'false');
  t.setAttribute('aria-label',t.title);
};
window.slicerToggleUI=slicerToggleUI;   // refreshSlicerCard gyvena auksciau uz si bloka
/* Atidarymas ir uzdarymas - viena vieta. Ta pati seka reikalinga ne tik nuo
   paspaudimo: isjungus moduli printeryje atidaryta kortele turi uzsidaryti pati.
   Iki 08-17 seka gyveno TIK paspaudimo tvarkytuve, o busenos apklausa kviete
   `slicerOpen`, kurio niekas nebuvo apibrezes - `typeof` toki kvietima tyliai
   praryja, tad kortele likdavo atidaryta. Vienoda busena - tyliai iseinam: kitaip
   apklausa kas sekunde perpiestu perziura. */
/* Ar sliceris atidarytas - KLAUSIAM cia, o ne skaitom `style.display` po visa pulta:
   akordeonui si busena tampa dazna, o issibarste patikros tyliai atsilieka (V 08-20). */
const slicerIsOpen=()=>{const b=$('slicerBody'); return !!(b&&b.style.display!=='none');};
window.slicerIsOpen=slicerIsOpen;
/* Kuris blokas atidarytas - isimenam, kad po perkrovimo zmogus liktu ten, kur buvo.
   PRIVERSTINIS uzdarymas (isjungtas web control) i atminti NERASOMAS: kitaip vienas
   printerio nustatymas amziams perstatytu zmogaus pasirinkima (V 08-20). */
const AKORD_RAKTAS='tmAkordeonas';
const akordIrasyk=kuris=>{try{localStorage.setItem(AKORD_RAKTAS,kuris);}catch(e){}};
window.akordPradinis=()=>{try{return localStorage.getItem(AKORD_RAKTAS)||'slicer';}
                          catch(e){return 'slicer';}};
/* Peržiūra perjungiant NIEKO neismeta: abu turiniai lieka atmintyje, o cia tik
   perpiesiam ta, kuris priklauso atidarytam blokui. Butent del sito is `slicerOpen`
   isimtas `slicerReset` - jis persikele ten, kur turinys tikrai keiciasi: i naujo STL
   ikelima ir „Discard" (V 08-20). */
const previewShowFor=()=>{
  /* Spausdinant perziura priklauso spaudiniui - jos neliecia niekas. */
  if(typeof statusData!=='undefined'&&statusData&&statusData.busy)return;
  if(slicerIsOpen()){
    slicerOwns(true);
    if(slicerOut){slicerBuildView(false);
      /* Sluoksniu slankiklis gyvena su REZULTATU, ne su piesimu: be sios eilutes
         grizus is SD jis likdavo paslėptas, nors modelis jau vel ekrane. Kartu
         grazinam ir ta pati sluoksni - zmogus paliko ji tam tikroje vietoje. */
      slicerLayerUI(true);
      slicerShowLayer(slicerLayerN||slicerOut.layers); return;}
    if(slicerRaw){slicerRender();return;}
    slicerOwns(false); dashPreviewPlaceholder(); return;
  }
  slicerOwns(false);
  /* SD pusėje modelis grazinamas per ta pati kelia, kaip eilutes paspaudimas -
     jei pjuviai dar kese, jis atsako is karto ir nieko is printerio netraukia. */
  const n=typeof dashPreviewName!=='undefined'?dashPreviewName:'';
  if(n&&typeof pickModel==='function')pickModel(n);
  else dashPreviewPlaceholder();
};
window.previewShowFor=previewShowFor;
/* Klase pasako maketui, KURIS blokas dabar valgo likusi auksti (zr. #homeRight CSS).
   Kai slicerio kortelės apskritai nera (modulis neaktyvus), visa vieta atitenka SD -
   kitaip desiniojo stulpelio apacioje likti tuscia juosta (V 08-20). */
const akordSync=open=>{
  const sc=$('slicerCard'), sd=$('sdSection');
  const yraSl=!!(sc&&!sc.classList.contains('hidden'));
  const sl=yraSl&&(open===undefined?slicerIsOpen():open);
  if(sc)sc.classList.toggle('akordOpen',!!sl);
  if(sd)sd.classList.toggle('akordOpen',!sl);
  if(window.sdFitRows)setTimeout(sdFitRows,0);
};
window.akordSync=akordSync;
const slicerOpen=(open,tylus)=>{
  const body=$('slicerBody'); if(!body)return;
  if(slicerIsOpen()===open)return;
  body.style.display=open?'block':'none';
  akordSync(open);
  slicerToggleUI(open);
  sdCollapse(open);
  if(window.sdFitRows)setTimeout(sdFitRows,0);   // eiluciu skaicius seka likusi auksti
  if(!tylus)akordIrasyk(open?'slicer':'sd');
  previewShowFor();
};
window.slicerOpen=slicerOpen;
/* SD antrastes rodykle suka TA PATI jungikli: vienas atidarytas, kitas suskleistas. */
{const sd=$('sdToggle');
 if(sd)sd.addEventListener('click',()=>{const t=$('slicerToggle');
   if(t&&!t.disabled&&!slicerIsOpen())t.click(); else slicerOpen(!slicerIsOpen());});}
/* Modulio krovimas atskirai nuo mygtuko: nuo akordeono blokas gali buti atidarytas
   ir po perkrovimo, o 3,5 MB variklio traukti KIEKVIENAM puslapio atidarymui butu
   grubu. Todel busena atsistato tuoj pat, o variklis atkeliauja tada, kai jo tikrai
   prireikia - paspaudus jungikli arba pasirinkus faila (V 08-20). */
const slicerLoadMod=async()=>{
  if(slicerMod)return slicerMod;
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
  const SV='3.1.1';
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
  return slicerMod;
};
$('slicerToggle').addEventListener('click',()=>{
  const wasOpen=slicerIsOpen();
  slicerOpen(!wasOpen);
  if(!wasOpen)slicerLoadMod();
});

/* Bet koks pakeitimas panaikina supjaustyta rezultata: kitaip „Save"
   siulytu issaugoti tai, ko ekrane jau nebera (V 08-12). */
const slicerInvalidate=()=>{
  if(typeof slicerOut==='undefined'||!slicerOut)return;
  slicerOut=null;
  /* Atramos priklauso TAM rezultatui: be sito senojo modelio atramos likdavo
     stovėti scenoje salia naujo (V 08-20 - ikelus kita modeli suportai liko seni). */
  if(window.gl3dSupports)gl3dSupports(null);
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
  /* Desinys galas - „tiksliai telpa". Fiksuoti 300 % buvo is oro: drakonui
     telpa ~17 %, tad devyni desimtadaliai juostos vede tik i „netelpa" (V 08-20).
     `fitCheck().worst` yra blogiausios asies uzimta limito dalis, tad riba =
     dabartinis mastelis / worst. Kaire - desimt kartu maziau uz desine: juosta
     visada dengia ta pati santyki, nesvarbu, koks modelis. */
  const f=(slicerMod&&slicerMod.fitCheck)?slicerMod.fitCheck(b.size):null;
  const fitPct=(f&&f.worst>0)?pct/f.worst:300;
  const top=Math.max(1,Math.min(300,Math.floor(fitPct)));
  const floor=Math.max(SCALE_MIN_PCT,Math.min(top/10,shown));
  const step=(pct<10||top<20)?0.1:1;
  /* Kol PIRŠTAS ant slankiklio, jo ribos NEJUDINAM. Apacia yra puse dabartinio
     mastelio, tad tempiant kairen ji irgi slenka zemyn, ruozas ilgeja, ir slankiklis
     po pirstu bega desinen - iki 0,1 % reikedavo keliu tempimu (auditas 08-17).
     Reiksme irgi ne: ja ka tik pasake pats zmogus. */
  if(pr&&!scaleDragging){pr.min=floor; pr.max=top; pr.step=step; pr.value=shown;}
  /* Skaiciu laukelis lieka platesnis uz juosta: iraseiciau bet koki procenta, o
     „netelpa" sarga pasakys, jei perlenkta - juosta tik neveda ten uz rankos. */
  if(pp){pp.min=SCALE_MIN_PCT; pp.step=step; pp.value=shown;}
  if(pm)pm.value=b.size[2].toFixed(1);
};
/* Tempimo zyme. `pointerup` ir `change` - abu: pirmas pagauna pele/pirsta, antras
   klaviatura ir atveji, kai pointer'is paleidziamas uz lango ribu.
   `slicerLastBounds` - paskutiniai piesimo matmenys, kad atleidus slankikli
   valdiklius butu galima atstatyti NEPERSKAICIUOJANT viso modelio. */
let scaleDragging=false, slicerLastBounds=null;
/* Perziuros kortele tikrai ne musu tik SPAUSDINANT - ten sukasi spaudinys.
   Korteles darbai (ispakavimas, trynimas) vaizdo neuzima, bet iki siol jie irgi
   blokavo slicerio piesima: ikelus modeli ir tuoj pat pasirinkus nauja STL,
   ekrane likdavo tuscias narvas, kol printeris baigs ispakuoti (V 08-20). */
const slicerPrinting=()=>!!(statusData&&statusData.busy&&!(statusData.sdJob||''));
/* Praleistas piesimas turi PATS sugrizti. Iki siol jis tiesiog nieko nedarydavo,
   ir vaizdas likdavo tuscias, kol zmogus paspausdavo bet kuri irankį - „Fit it"
   ji perpiesdavo, ir atrodydavo, kad kaltas failo ikelimas (V 08-20). */
let slicerRenderPending=false;
window.slicerRetryRender=()=>{
  if(!slicerRenderPending||slicerPrinting()||!slicerRaw)return;
  slicerRenderPending=false;
  slicerRender();
};
const slicerRender=()=>{
  if(slicerPrinting()){slicerRenderPending=true;return;}
  slicerInvalidate();
  const S=slicerMod;
  const placed=S.place(slicerRaw,slicerTr);
  const b=S.bounds(placed), f=S.fitCheck(b.size);
  {const n=slicerRaw.length/9;
   let vd,col;
   /* Didzioji raide, kaip visose kitose faktu eilutese - „fits" buvo vienintele
      is mazosios (V 08-20). */
   if(f.fits){vd='Fits the build volume';col='var(--muted)';}
   /* Verdiktas mini ir autofita: anksciau siule tik pasukti ar sumazinti
      ranka, nors mygtukas daro abu iskart (V 08-12). */
   /* Mygtukas STOVI pranesime. Anksciau pranesimas buvo desineje, o mygtukas,
      kuri jis liepia spausti, - kitame korteles gale; akis eina per visa
      kortele ir atgal (V 08-13). */
   else{vd='Too large - '+f.axis+' +'+Math.round((f.worst-1)*100)
        +'% · <button type="button" id="slicerFitHere" style="display:inline-flex;'
        +'width:auto;min-height:0;margin:0 0 0 4px;padding:2px 10px;border-radius:6px;'
        +'background:var(--accent);color:#fff;border:0;font-size:.85rem;cursor:pointer">'
        +'Fast fit</button> or turn it by hand';col='var(--warncol)';}
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
  /* Mastelio langelis uzima visa eilute, tad kaimynines grupes tuo metu paslepiam
     (`popRow`). Bet `gl3dMesh` jas grazina kas perpiesima, o mastelio slankiklis
     perpiesia kas judesi - tad +/- issilisdavo ant paties langelio (V 08-20). */
  {const pp=$('gl3dPop');
   if(pp&&pp.style.display!=='none'){popRow(false);pp.style.display='flex';}}
  slicerStep();          // juostos turinys priklauso nuo to, ar jau supjaustyta
  $('printPreviewTitle').textContent='Slicer preview';
  show('printPreviewCard',true);
};

$('slicerFile').addEventListener('change',async e=>{
  const f=e.target.files&&e.target.files[0]; if(!f)return;
  /* Variklis gali buti dar neuzsikroves (busena atsistatė is atminties, o mygtuko
     niekas nespaude) - tada palaukiam jo cia, o ne tyliai nieko nedarom. */
  if(!slicerMod){await slicerLoadMod(); if(!slicerMod)return;}
  if(slicerBusyStop())return;
  slicerHome=true;                 // naujas failas - vaizda pastatom is naujo
  slicerSay('slicerInfo','Reading '+f.name+'…');slicerSay('slicerFit','');
  try{
    const buf=await f.arrayBuffer();
    const r=slicerMod.parseSTL(buf);
    /* Senas rezultatas nuvalomas PRIES nauja modeli: kitaip jo duomenys
       gali persideti ant naujo ir vaizdas atrodo istemptas (V 08-12). */
    slicerOut=null;
    if(window.gl3dSupports)gl3dSupports(null);   // naujas modelis - senos atramos ne jo
    slicerRaw=r.positions; slicerFileName=f.name;
    slicerBudget=slicerMod.detailBudget(slicerRaw);
    const best=slicerMod.autoOrient(slicerRaw);      // padedam ant plokstumos iskart
    slicerTr=best.tr;
    slicerButtons(true);
    /* Naujas failas - naujas siulymas: senas vardas likdavo ir modelis
       issisaugodavo ne tuo pavadinimu (V 08-12). */
      $('slicerName').value=f.name.replace(/\.stl$/i,'')
        .replace(/[^A-Za-z0-9_-]/g,'').slice(0,40);   // tiek pat, kiek priima ikelimas is PrusaSlicer
        /* 14 raidziu buvo MUSU isgalvota riba: tiek matosi printerio ekrane. Bet
           is Prusos ateinantis vardas rezamas ties 40 (safeModelName), sarase
           matosi visas, o ekrane tiesiog nesitelpa - ir niekam tai netrukde.
           Sliceriui buti grieztesniam nera pagrindo (V 08-20). */
    $('slicerGo').disabled=false;
    slicerRender();
  }catch(err){slicerSay('slicerInfo',err.message);slicerButtons(false);}
});

/* Autofit = pasuka + sumazina TIK jei netelpa, ir apie tai pasako. Mazinimas
   keicia tikra detales dydi, tad tylus jis nebuna niekada (V 08-12). */
/* Ilgas darbas VIENU gabalu (autofitas, guldymas): narsykle tuo metu nieko
   nepiesia, tad uzrasas turi atsirasti PRIES ji, o darbas - kitame kadre.
   Kitaip uzrasas pasirodytu jau po visko, ir zmogus kelias sekundes ziuretu i
   sustinguși vaizda (V 08-19: „reikia biski paukt, gal saldaini"). */
/* Nuima uzrasa, kuris buvo piestas ANT 3D: isvalo drobe ir grazina jai ta pacia
   vieta stiklu tvarkoje, kokia buvo. Be sito drobe liktu virs GPU sluoksnio ir
   uzdengtu modeli (V 08-19: „slicina - modelio nerodo"). */
/* Kol vyksta ilgas darbas, juostos nieko negali pakeisti - o ekrane tuo metu arba
   uzrasas ant modelio, arba vien uzrasas. Formos irankius pasleps `slicerButtons`,
   o vaizdo juosta - sitas. Kampiniai valdikliai (priartinimas, sukimas) lieka: jie
   yra apie ziurejima, ne apie daikta, ir dirbant praverčia (V 08-20). */
/* Kol vyksta ilgas darbas, ekrane lieka VIEN zinute. Nei sukti, nei priartinti,
   nei zymeti nera ko: daikto dar nera arba jis kaip tik gaminamas (V 08-20).
   Formos eilute pasitraukia per `slicerButtons(false)`, visa kita - cia. */
const slicerWorkUI=dirba=>{
  ['gl3dZoom','gl3dZoomCorner','gl3dMarkWrap','gl3dPad','gl3dRot','gl3dHelp']
    .forEach(id=>{const e=$(id); if(e&&dirba)e.style.display='none';});
  /* Ir sluoksniu slankiklis: kol pjaustoma, slinkti dar nera ko - o jis rodydavo
     „334 / 334" salia „Slicing 85 %" (V 08-20). */
  if(dirba)slicerLayerUI(false);
  if(!dirba){
    /* Grazina tas pats, kas ir sprendzia, kas kuriame vaizde matoma. */
    slicerViewChrome();
    slicerMarkUI(!!slicerOwnsPreview);
    if(typeof slicerOut!=='undefined'&&slicerOut)slicerLayerUI(true);
  }
};
/* Ar vaizdas DABAR musu. Darbas gali tesltis fone (pjaustymas trunka 26 s), bet jei
   zmogus tuo metu nuejo i SD ir atsidare kita modeli, mes i ta drobe nerasom nieko ir
   pabaige nieko neperpiesiam: peržiūra priklauso ATIDARYTAM blokui (V 08-20 - „paleidi
   slicinima, nueini i SD, o pasibaiges permusa vaizda"). Rezultatas niekur nedingsta -
   ji nupiesim, kai zmogus grizta (`previewShowFor`). */
const slicerVaizdasMusu=()=>slicerIsOpen()&&!slicerPrinting();
const slicerPaint=(uzrasas,frac)=>{
  if(!slicerVaizdasMusu())return;
  paintPreviewProgress($('printPreviewCanvas'),uzrasas,frac,true);
};
const slicerPaintIndet=uzrasas=>{
  if(!slicerVaizdasMusu()||!window.paintPreviewIndet)return;
  paintPreviewIndet($('printPreviewCanvas'),uzrasas);
};
const slicerOverlayOff=()=>{
  /* PIRMA sustabdom vaikstanti ruozeli. Jis sukasi laikmaciu ir perpiesia uzrasa kas
     60 ms - jei jo nesustabdytum, darbas seniai baigtusi, modelis butu pastatytas, o
     ekrane amzinai liktu „Turning the part to fit…" (V 08-20: „nesibaigia"). */
  if(window.paintPreviewIndetStop)paintPreviewIndetStop();
  const cv=$('printPreviewCanvas'); if(!cv)return;
  if(typeof pvFit==='function'){const c=pvFit(cv);c.clearRect(0,0,PREV_W,PREV_H);}
  cv.style.zIndex=''; cv.style.position=''; cv.style.visibility='';
};
const slicerBusyPaint=(uzrasas,darbas)=>{
  slicerPaint(uzrasas,null);
  slicerWorkUI(true);
  /* `await` cia butinas: kruopstusis pastatymas (`autoOrientPro`) sukasi variklio
     gijoje ir grazina pazada. Be jo uzrasas dingtu tuoj pat, o modelis pasisuktu
     po keliu sekundziu - tarpe ekranas atrodytu tuscias (V 08-20). Sinchroniniams
     darbams `await` nieko nekeicia. */
  /* Ne VIEN kadro: paslėptame ar uzdengtame lange narsykle kadru nepiesia is viso,
     tad `requestAnimationFrame` nesuveikia NIEKADA - „Auto fit" ir „Lay flat" tokiame
     lange tyliai nieko nedarydavo (rado stendas 08-20; ta pati pamoka jau buvo
     `paintStage` pulte, V 08-14). Laikmatis paleidzia darba ir fone. */
  let paleista=false;
  const eik=async()=>{
    if(paleista)return; paleista=true;
    try{await darbas();}
    finally{slicerOverlayOff();slicerWorkUI(false);}
  };
  requestAnimationFrame(()=>requestAnimationFrame(()=>setTimeout(eik,0)));
  setTimeout(eik,150);
};
/* Kokio dydzio detale telpa su tokiu pastatymu. `worst` yra blogiausios asies
   uzimta limito dalis, tad 1/worst - kiek karta ja dar galima auginti (arba kiek
   teks mazinti). Vienintelis skaicius, kuriuo du pastatymai palyginami sazininagai:
   ne „graziau", o „didesne detale telpa". */
const slicerTelpa=tr=>{
  const b=slicerMod.bounds(slicerMod.place(slicerRaw,Object.assign({},tr,{scale:1})));
  const f=slicerMod.fitCheck(b.size);
  return {kiek:f.worst?1/f.worst:0, h:b.size[2]};
};
/* Kruopstaus pastatymo verdiktas viena eilute. Be jo mygtukas butu tikejimo
   klausimas, o akis apgauna: drakonas ant sparno atrodo blogai pastatytas, nors
   kaip tik toks telpa 34 % didesnis (matuota 08-20). */
const slicerPlaceNote=(pro,fast)=>{
  const e=$('slicerPlaceNote'); if(!e)return;
  if(!pro||!fast){e.textContent='';return;}
  const sant=fast.kiek?pro.kiek/fast.kiek:1, proc=Math.round((sant-1)*100);
  const auksciai=' ('+pro.h.toFixed(1)+' mm vs '+fast.h.toFixed(1)+' mm tall)';
  e.style.color=proc<0?'var(--warncol)':'';
  e.textContent=Math.abs(proc)<1
    ? 'Optimal fit: the same size as Fast fit'+auksciai+'.'
    : (proc>0 ? 'Optimal fit: the part prints '+proc+'% bigger than with Fast fit'+auksciai+'.'
              : 'Optimal fit: '+(-proc)+'% smaller than with Fast fit'+auksciai+
                ' - Fast fit suits this model better.');
};
/* Greitasis pastatymas nuo 3.1.1 turi SAVO gija (`autoOrientFast`) - ta pati skaiciuote,
   tik ne pulto gijoje. Del to pagaliau juda ir juostele: anksciau drakonui 7 s naršykle
   nepiesdavo NE VIENO kadro, tad bet kokia animacija butu sustingusi (matuota 08-20).
   Senesnis modulis tokios funkcijos neturi - tada dirbam kaip anksciau, sinchroniskai. */
const slicerGreitas=async()=>(slicerMod.autoOrientFast
  ? await slicerMod.autoOrientFast(slicerRaw)
  : slicerMod.autoOrient(slicerRaw));
$('slicerAutoFit').addEventListener('click',()=>{
  if(!slicerRaw)return;
  slicerBusyPaint('Turning the part to fit…',async()=>{
  if(slicerMod.autoOrientFast)slicerPaintIndet('Turning the part to fit…');
  const s0=slicerTr.scale;
  slicerTr=(await slicerGreitas()).tr; slicerTr.scale=s0;
  slicerPlaceNote(null,null);   // pastatymas pasikeite - senas verdiktas nebegalioja
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
});
/* Kruopstusis kelias (modulis 3.1.0): PAKRYPIMA - kuria puse guldyti - parenka
   tikras PrusaSlicer Rotfinder, sukamas variklio gijoje, o posuki ant plokstes,
   talpinima ir masteli toliau sprendziam mes (Prusa apie vertikale nesuka visai,
   o musu ploksté pailga). Atskiras mygtukas, nes trunka 10-15 s ir laimi ne
   visada. Drakonui jo atsakymas ATRODO keistas (detale atsistoja ant sparno) ir
   yra auksciau - 138,9 vs 100,5 mm, - bet plokste 40,8 x 30,6 mm, tad butent
   siauresnis pastatymas leidzia spausdinti 34 % didesne detale. Todel verdikta
   apacioje rodo skaicius, o ne akis (V 08-20).
   Eiga: nuo 3.1.1 variklis atiduoda tikrus 1..100 (`Rotfinder statuscb`). Kol
   ateina pirmas skaicius, sukasi vaikstantis ruozelis - ji sustabdo pati juostele,
   vos tik gauna skaiciu. Sustabdyti paieskos negalima: variklio gija viso
   skaiciavimo metu blokuota, tad musu zinute jos nepasiektu (slicerio sesija, #108). */
$('slicerAutoFitPro').addEventListener('click',()=>{
  if(!slicerRaw||!slicerMod.autoOrientPro)return;
  slicerBusyPaint('Searching for optimal fit…\nmay take long',async()=>{
    /* Variklis sukasi savo gijoje, tad pulto gija laisva ir ruozelis tikrai juda.
       Skaiciaus nera - variklis jo neatiduoda (vienas nedalomas kvietimas), tad
       juostele nieko nematuoja, tik sako „dirbama" (V 08-20). */
    slicerPaintIndet('Searching for optimal fit…\nmay take long');
    const s0=slicerTr.scale;
    const fast=slicerTelpa((await slicerGreitas()).tr);
    let pro=null;
    try{
      /* Nuo 3.1.1 variklis atiduoda TIKRUS 1..100, tad ruozelis pasitraukia, vos
         ateina pirmas skaicius: `paintPreviewProgress` su skaitine dalimi laikmati
         sustabdo pati. Be sio kabliuko juostele butu likusi vaikstanti visas 17 s. */
      const r=await slicerMod.autoOrientPro(slicerRaw,(done,total)=>{
        const f=total?done/total:null;
        /* Skaicius - PRIE uzraso, kaip pjaustant („Slicing 85 %"): vien juosta
           nesako, ar liko sekunde, ar dvylika (V 08-20). */
        slicerPaint(
          'Searching for optimal fit…'+(f!==null?' '+Math.round(f*100)+'%':'')
          +'\nmay take long',f);
      });
      slicerTr=r.tr; pro=slicerTelpa(r.tr);
    }catch(e){
      /* Variklis neatsake - paliekam, kas buvo, ir pasakom: tyliai grizti prie
         greitojo butu apgaule, nes zmogus praso butent sito. */
      msg('The engine did not answer - the position is unchanged.',true);
      return;
    }
    slicerTr.scale=s0;
    slicerPlaceNote(pro,fast);
    const b=slicerMod.bounds(slicerMod.place(slicerRaw,slicerTr));
    const f=slicerMod.fitCheck(b.size);
    if(!f.fits)slicerTr.scale*=f.scaleToFit;
    {const e=$('statusMsg');
     if(/does not fit/i.test(e.textContent||'')&&
        slicerMod.fitCheck(slicerMod.bounds(slicerMod.place(slicerRaw,slicerTr)).size).fits)
       msg('',false);}
    slicerHome=true;
    slicerRender();
  });
});
$('slicerFlat').addEventListener('click',()=>{
  if(!slicerRaw)return;
  slicerBusyPaint('Laying it flat…',async()=>{
    if(slicerMod.autoOrientFast)slicerPaintIndet('Laying it flat…');
    slicerPlaceNote(null,null);
    const s=slicerTr.scale;
    slicerTr=(await slicerGreitas()).tr; slicerTr.scale=s; slicerRender();});});
$('slicerFlip').addEventListener('click',()=>{if(slicerRaw){slicerPlaceNote(null,null);slicerTr.rx+=2;slicerRender();}});
$('slicerRotX').addEventListener('click',()=>{if(slicerRaw){slicerPlaceNote(null,null);slicerTr.rx++;slicerRender();}});
$('slicerRotZ').addEventListener('click',()=>{if(slicerRaw){slicerPlaceNote(null,null);slicerTr.rz++;slicerRender();}});
/* Pjaustymas ir issaugojimas. Archyvas keliauja ESAMU ikelimo keliu - tuo
   paciu, kuriuo ateina PrusaSlicer siuntiniai; printeriui naujo kodo nereikia. */
let slicerOut=null;   // supjaustytas rezultatas, laukiantis sprendimo

/* Tikra sluoksnio kauke. 3D vaizdas glotnina pavirsiu, tad plona 0.4 mm supporto
   gija jame tiesiog dingsta - o butent ja ir reikia patikrinti. Cia rodoma ta
   pati PNG, kuri keliauja i archyva: nedidinta, be glotninimo. Nieko naujo
   neskaiciuojam - failai jau atmintyje (V 08-13). */
let slicerMaskOn=false, slicerView=0;
/* Ar plokscias vaizdas rodomas taip, kaip sviecia ekranas: detale ir atramos
   viena spalva. Pasirinkimas isliekantis - kas ji ijunge, kito atidarymo
   nejunginetu is naujo. Numatytoji busena - senoji (atramos melynos): 08-13 V
   pasake, kad matyti, KUR atrama, yra butina, ir tas sprendimas galioja. */
const slicerUvOn=()=>{try{return localStorage.getItem('tmUvView')==='1';}catch(e){return false;}};
/* ~405 nm pro dangti. Sviesesnis vidus - ne pagrazinimas: sluoksnio nuotrauka
   turi pilkus krastus (antialiasing), o jie reiskia MAZESNE ekspozicija, tad ta
   pati informacija lieka matoma ir perdazius. */
const UV_DIM=[139,92,246], UV_HOT=[196,181,253];
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
    /* „Kaip sviecia“: jokio atskyrimo - viskas, kas kietėja, yra vienas plotas, ir
       butent ji zmogus turi ivertinti. Atramu kauke cia NEPIESIAMA (V 08-22). */
    if(slicerUvOn()){
      const px=ctx.getImageData(0,0,cv.width,cv.height);
      for(let p=0;p<px.data.length;p+=4){
        const v=px.data[p];
        if(v<24){px.data[p]=px.data[p+1]=px.data[p+2]=0;px.data[p+3]=255;continue;}
        const w=(v/255)*0.55;
        for(let q=0;q<3;q++)px.data[p+q]=Math.round(UV_DIM[q]+(UV_HOT[q]-UV_DIM[q])*w);
        px.data[p+3]=255;
      }
      ctx.putImageData(px,0,0);
      URL.revokeObjectURL(url);
      return;
    }
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
window.slicerMaskDraw=slicerMaskDraw;   // jungiklis perpiesia ta pati sluoksni
/* Plokscias vaizdas nebekeicia bloko auksčio (V 08-22). Iki tol jis perstatydavo
   `#previewStage` i 320:240, kad aplink juoda staciakampi neliktu tuscio krasto -
   bet tada perjungiant 3D <-> 2D blokas paaukstedavo ir visas puslapis soktelėdavo.
   Dabar plokste tiesiog susitraukia savo proporcija i TA PATI bloka (drobe yra
   `height:100%; width:auto`), o jos krasta pazymi kadro kampai - zr. `#plateFrame`
   pulte. Ismatuota stende: blokas 486x283 ir isdidintas 1023x702 abiem vaizdais,
   plokste 4:3 abiejuose. */
/* Langui pasikeitus (taip pat ir paspaudus „didinima") plokscio vaizdo taisykles
   turi praeiti is naujo: dydzio keitimas eina per `gl3dShow`, o tas grazina visas
   3D grupes, ir be sito valdikliai issibarstytu per kauke (V 08-20). */
window.addEventListener('resize',()=>{
  if(!(slicerMaskOn&&slicerView===2))return;
  slicerViewChrome();
});
/* „Lango didinimas" savo auksti nustato pats ir „resize" nesukelia, tad chrome
   po jo persistatom rankomis. */
{const g=$('stageOpen');
 if(g)g.addEventListener('click',()=>setTimeout(()=>{
   if(!(slicerMaskOn&&slicerView===2))return;
   slicerViewChrome();},0));}
let slicer3dLayer=null;      // kur stovejo slankiklis 3D vaizde
function slicerMaskSet(on){
  slicerMaskOn=!!on&&!!slicerOut&&!!slicerOut.files;
  /* Spalva ir piesinys pareina nuo rezimo (`slicerSetView`), ne nuo kaukes. */
  const box=$('slicerMask'), cv=$('slicerMaskCv');
  if(!slicerMaskOn){
    if(box)box.style.display='none';
    /* Grizom i 3D - atstatom auksti, kuri kauke buvo pakeitusi. */
    if(slicer3dLayer!==null&&slicerOut){
      const n=Math.max(1,Math.min(slicerOut.layers,slicer3dLayer));
      slicer3dLayer=null;
      slicerLayerN=n;
      const R=$('gl3dLayerRange'); if(R)R.value=n;
      if(gl3dUp())gl3dClip(n>=slicerOut.layers||!slicerModelH()?null
                           :Math.max(0,Math.min(1,n*0.05/slicerModelH())));
    }
    /* Drobe isvaloma: kitaip kitas ijungimas trumpam parodytu sena sluoksni. */
    if(cv){const c=cv.getContext('2d'); c&&c.clearRect(0,0,cv.width,cv.height);}
    return;
  }
  /* Slankiklio prasme dviejuose rezimuose skiriasi: 3D jis sako „kiek aukscio
     rodyti" (gale - visas daiktas), kaukeje - „kuri sluoksni rodyti" (gale -
     pati virsune, dazniausiai juodas kvadratas). Todel ijungiant kauke ties
     pabaiga persistojam i vidury: kitaip mygtukas atrodo negyvas (V 08-13). */
  /* Iseinant is kaukes grazinam ta pacia vieta, kurioje buvo 3D: vidurys yra
     kaukes reikalas, o 3D nuo to likdavo perpjautas per puse (V 08-20). */
  if(slicer3dLayer===null)slicer3dLayer=slicerLayerN||slicerOut.layers;
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
  /* #116: „atramu nereikia" galima sakyti TIK tada, kai niekas nekabo. Jei
     modulis atsiuntė ispejima, jis jau zino, kad kabo - tada si eilute neturi
     tvirtinti, kad viskas gerai. */
  a.textContent=s.pillars
    ?'Supports: '+s.pillars+(s.pillars===1?' pillar':' pillars')
      +(s.onModel?' ('+s.onModel+' standing on the part itself)':'')
      +(s.raft?' · raft on':'')
      +(s.pakelta?' · part lifted so they would fit':'')
    :s.perspejimas
      ?'No supports were built.'
      :'No supports needed - nothing on this part hangs in the air.';
  /* Supportai patys pasitikrina: suslicinus SU jais dar kartą ieškoma kabanciu
     vietu. Jei atsirado nauju - tai MUSU pacio klaida, ir apie ja butina
     pasakyti, o ne tyliai issaugoti (V 08-13). */
  /* #116: modulio ispejimas eina PIRMAS - jei kazkas spausdintusi ore be
     atramos, tai svarbiausias dalykas kortelėje. Pakelimo atveju ta pati eilute
     yra ne pavojus, o paaiskinimas, kodel spaudinys pailgo. */
  b.textContent=s.perspejimas
    ?(s.pakelta?'':'⚠ ')+s.perspejimas
    :s.hanging
      ?'⚠ '+s.hanging+' support'+(s.hanging===1?'':'s')+' would print hanging in the air - do not save this, tell the maintainer.'
      :s.islands
        ?s.islands+(s.islands===1?' spot starts':' spots start')+' in mid-air (the lowest at layer '
          +s.firstIsland+') - all held by supports.'
        :'';
  b.style.color=(s.hanging||(s.perspejimas&&!s.pakelta))?'#e8a020':'';
}
/* Sluoksnio valdikliai gimsta ir dingsta kartu: slankiklis, kaukes mygtukas ir
   pati kauke. Anksciau trys vietos slepe tik slankikli. */
function slicerLayerUI(on){
  /* Rodyti slankikli ant svetimo vaizdo nera prasmes: jis valdo MUSU rezultata. */
  if(on&&!slicerIsOpen())return;
  const L=$('gl3dLayer'); if(L)L.style.display=on?'flex':'none';
  ['gl3dV3','gl3dV2'].forEach(id=>{const b=$(id); if(b)b.style.display=on?'':'none';});
  /* Piesinys ir aktyvi puse perskaiciuojami CIA PAT: valdikliai pasirodo anksciau,
     nei ivyksta pirmas vaizdo perjungimas, ir be sitos eilutes mygtukai mirktelėdavo
     tusti (ismatuota stende 08-22). */
  if(on)slicerViewButtons();
  /* Sluoksnio NUMERIS cia NEBENULINAMAS: valdikliai dingsta ir grizta kaskart
     perjungiant akordeona, o zmogaus vieta sluoksniuose priklauso REZULTATUI -
     ji nulinama tik ten, kur rezultatas keiciasi (`slicerReset`, „Discard"). */
  if(!on){slicerMaskSet(false); slicerView=0;
          if(window.gl3dSupports)gl3dSupports(null);}
  else slicerSetView(slicerView);
}
/* Kurioje 3D poros pusėje zmogus buvo paskutini karta. Be sios atminties
   grizimas is 2D visada mestu i „su atramomis“, nors zmogus zurejo be ju. */
let slicer3dSub=0;
/* Abieju mygtuku isvaizda vienoje vietoje: kuris piesinys, kuris aktyvus, koks
   uzrasas. Kvieciama is `slicerSetView` ir verciant UV puse. */
function slicerViewButtons(){
  const uv=slicerUvOn(), flat=slicerView===2;
  if(!flat)slicer3dSub=slicerView;
  const b3=$('gl3dV3');
  if(b3){const s=flat?slicer3dSub:slicerView;
    b3.classList.remove('s0','s1'); b3.classList.add('s'+s);
    b3.classList.toggle('act',!flat);
    b3.title=(flat?'Back to 3D - ':'3D - ')+
      (s===0?'the model with its supports. Click for the model alone.'
            :'the model alone. Click to bring the supports back.');}
  const b2=$('gl3dV2');
  if(b2){b2.classList.remove('f0','f1'); b2.classList.add(uv?'f1':'f0');
    b2.classList.toggle('act',flat);
    b2.title=(flat?'':'Flat layer - ')+
      (uv?'as the screen lights it: the part and its supports in one colour. Click for the supports marked.'
         :'the true layer with its supports marked. Click to see it as the screen lights it.');}
}
const VIEW_TITLES=[
  '3D with supports - shapes, not pictures. Click for the printed layers.',
  'Printed layers - exactly what the printer builds. Click for one true layer.',
  'True layer - one picture, no smoothing. Click to go back to 3D.'];
/* Kas rodoma PLOKSCIAME vaizde. Atskira funkcija, nes kiekvienas 3D perpiesimas
   grazina visas grupes atgal (`gl3dMesh` -> `rodyk`), tad po jo si turi praeiti
   dar karta - kitaip padidinus langa valdikliai vel issibarstydavo per kauke
   (V 08-20). Plokscioje kaukeje prasminga tik: perjungti vaizda, slinkti
   sluoksnius. Sukimas, stumdymas, pagalba, narvas ir priartinimas - 3D reikalai. */
function slicerViewChrome(){
  const plokscia=!!window.slicerFlatView;
  /* Zyme pultui: rodomas PLOKSCIAS vaizdas. Pultas pagal ja atiduoda sluoksniu
     slankikliui beveik visa auksti - 3D valdikliu, kuriems tie tarpai buvo
     palikti, cia nebera (V 08-22). Zyme sako TIK „koks vaizdas rodomas", ne
     „kas valdo auksti" - senoji `stageFlat` reiske antra, ir butent del to
     abi puses raše ta pati stiliu. */
  {const st=$('previewStage'); if(st)st.classList.toggle('flatView',plokscia);}
  const cg=$('gl3dCage'); if(cg)cg.style.display=plokscia?'none':'';
  const zc=$('gl3dZoomCorner'); if(zc)zc.style.display=plokscia?'none':'flex';
  [['gl3dPad','grid'],['gl3dRot','grid'],['gl3dHelp','block']].forEach(([id,d])=>{
    const e=$(id); if(e)e.style.display=plokscia?'none':d;});
  slicerTopLeft();
}
window.slicerViewChrome=slicerViewChrome;
function slicerSetView(v){
  slicerView=((v%3)+3)%3;
  slicerViewButtons();
  slicerMaskSet(slicerView===2);
  /* Tikro sluoksnio vaizde priartinimas ir narvas nieko nedaro: tai plokscia
     kauke, pikselis prie pikselio, ir tokia ji turi likti (tam ji ir yra).
     Mygtukai slepiami, o ne uzrakinami - valdiklis, kuris nieko nekeicia, meluoja
     labiau uz nesancio mygtuko nebuvima (V 08-19). */
  window.slicerFlatView=(slicerView===2);
  slicerViewChrome();
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
  /* Pjuvio aukstis - PER SAVO lauka, ne per bendra `slicesCache`: i ji rasant
     kito modelio perziura likdavo su musu aukščiu ir suplokstedavo (V 08-20). */
  {const H=slicerModelH(); window.gl3dClipHeight=H||null;}
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
  /* Uzdarytas blokas vaizdo nebeima: pjaustymas galejo baigtis jau tada, kai zmogus
     ziuri SD modeli (V 08-20). Rezultatas lieka atmintyje, o nupiesim ji, kai grizta. */
  if(!slicerIsOpen())return false;
  /* Ta pati priezastis, kaip slicerRender: si funkcija perima perziuros kortele ir
     perrasoma globalu `slicesCache`, tad spausdinant ji ismestu gyva sluoksniu
     srauta ir jis butu traukiamas is naujo (auditas 08-17). */
  if(slicerPrinting())return false;
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
  if(slicerPrinting())return;   // ta pati priezastis, kaip slicerRender
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
/* „Stop" isimtas (V 08-22). Jis atrodė kaip stabdymas, bet variklio nestabdė:
   modulis sukasi VIENAME bendrame Web Worker'yje ir nutraukimo neturi (jo kode
   nera nei abort, nei cancel, nei terminate). Paspaudus tik nustodavom laukti
   atsakymo, o darbas fone likdavo suktis - ir kitas „Slice" atsistodavo i to
   paties darbininko EILE, tad juostele stovedavo ties nuliu, kol senasis
   pasibaigs. Atrodytu kaip pakibimas.
   Dabar sasaja sako tiesa: pjaustymo metu mygtukas lieka „Slice", uzrakintas ir
   su suktuku (`btnBusy` - ta pati idioma, kaip „Start" laukiant perziuros).
   Antro paleidimo neimanoma, ir niekas nemeluoja. Tikras nutraukimas grizs, kai
   modulis atiduos `abort()` - suplanuota 1.1. */
$('slicerGo').addEventListener('click',async()=>{
  if(sliceRunning)return;                 // uzrakintas, bet sarga pigi
  if(!slicerRaw||!slicerMod)return;
  if(slicerBusyStop())return;
  const placed=slicerMod.place(slicerRaw,slicerTr);
  const f=slicerMod.fitCheck(slicerMod.bounds(placed).size);
  if(!f.fits){msg('It does not fit yet - turn or scale it first.',true);return;}
  const go=$('slicerGo'), prog=$('slicerProg');
  const myRun=++sliceRun;
  sliceRunning=true;
  /* Uzrakintas ir su suktuku. Raudono „Stop" cia nebera - zr. komentara virsuje. */
  btnBusy(go,true);
  /* Ir visas pultas uzsirakina, kaip per printerio darba: uzraktai skaito `uiBusy`,
     tad uztenka pakelti zyme ir paprasyti ju persiskaiciuoti dabar pat - apklausa
     tai padarytu tik po dvieju sekundziu (V 08-22). */
  slicerBusyNow=true;
  if(typeof syncActionLocks==='function')syncActionLocks();
  slicerButtons(false);
  slicerWorkUI(true);
  try{
    prog.textContent='';
    slicerPaint('Slicing\u2026',0);
    const t0=performance.now();
    /* Du praejimai, viena juosta: pirma ieskoma, kur daiktas kabo (pirmas
       trecdalis), tada piesiami sluoksniai. Kitaip juosta nueitu iki galo ir
       pradetu is naujo - atrodytu, kad kazkas uzstrigo. */
    const supType=(document.querySelector('input[name=slicerSupType]:checked')||{}).value||'regular';
    const r=await slicerMod.slice(placed,{antialias:$('slicerAA').checked,
      supportType:supType,name:(slicerFileName||'print').replace(/\.stl$/i,'')},
      (done,total,phase)=>{
        /* `btnBusy` turi 60 s isleidimo voztuva (kad negyva uzklausa nepaliktu
           mygtuko amzinai suktis). Didelis modelis pjaustomas ilgiau, tad zyme
           gali nukristi vidury darbo - uzdedam atgal. */
        if(!go.classList.contains('btnBusy'))btnBusy(go,true);
        const f=phase==='scan'?done/total*0.3
               :phase==='draw'?0.3+done/total*0.7
               :done/total;                      // senas modulis - viena faze
        const pct=Math.round(f*100);
        const what=phase==='scan'?'Looking for overhangs':'Slicing';
        /* Sluoksniu skaiciaus NERODOM, kol ju dar nera: WASM variklis eiga duoda
           savo dirbtine 0..1000 skale, tad „300 / 1000 layers" buvo procentai su
           sluoksniu kauke, o pabaigoje skaicius virsdavo 316 (V 08-20). */
        const tikri=total&&total!==1000;
        /* VIENA vieta, ir ta vieta - vaizdas. Ta pati eilute stovejo ir korteleje,
           ir ant drobes; SD ikelimo atveju sis dubliavimas jau isnaikintas, tad ir
           cia elgiames vienodai (V 08-17). Sluoksniu skaicius keliauja kartu su
           uzrasu - be jo vaizde nebesimatytu, kiek ju is viso.
           Skaiciai - ANTROJE eiluteje (`\n`), kaip visur kitur pulte: vienoje
           eiluteje „Looking for overhangs 28% (161 / 173 layers)" issitempdavo per
           visa drobe ir `fitFont` dar sumazindavo srifta, kad tilptu (V 08-17). */
        prog.textContent='';
        slicerPaint(
          what+' '+pct+'%'+(tikri?('\n'+done+' / '+total+' layers'):''),f);
      });
    /* Ir dar viena patikra: sustabdytas darbas gali sugrizti su gatavu rezultatu,
       o jo niekas nebelaukia - net „stop" zenklas jau nuvalytas (V 08-20). */
    if(sliceRun!==myRun)return;
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
    /* Uzraktai nusiimami CIA - kai darbas tikrai baigtas, o ne `finally` bloke.
       Tvarka svarbi: zemiau atrakinam „Save“, o po jo eina `slicerStep()`, kuris
       darbo metu ta pati mygtuka VeL uzrakina. Palikus zyme pakelta, ka tik
       atrakintas mygtukas tuoj pat uzsirakindavo, ir „Send to printer“ likdavo
       pilkas iki kito veiksmo (V rado 08-22). `finally` zyme nusiima dar karta -
       ji ten lieka kaip saugiklis klaidos keliui. */
    slicerBusyNow=false;
    if(typeof syncActionLocks==='function')syncActionLocks();
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
    /* Sustabdymo sakos cia nebeliko kartu su paciu „Stop“ (V 08-22): pjaustymo
       nutraukti neimanoma, tad kiekviena klaida cia yra tikra klaida. */
    prog.textContent=e.message;
    msg(e.message,true);
  }finally{
    /* Sustabdyto (arba pakeisto nauju) pjaustymo uodega neturi liesti nieko: pultas
       jau grizes i darbine busena, o gal jau pjausto kita. */
    if(sliceRun!==myRun)return;
    slicerOverlayOff();
    slicerWorkUI(false);
    sliceRunning=false;
    btnBusy(go,false);
    slicerBusyNow=false;
    if(typeof syncActionLocks==='function')syncActionLocks();
    go.textContent='Slice'; go.classList.remove('danger');
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
/* Ka slepia mastelio eilute. `gl3dRot` cia NEBERA (V 08-21): vartymas gyvena apacioje
   kaireje, o eilute - virsuje, tad jie nebesikerta ir slepti ji nebera del ko. */
const POP_ROW={gl3dTools:'flex',gl3dZoom:'flex',gl3dZoomCorner:'flex'};
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
/* O jei zmogus narva sliceryje PATS ijungia ar isjungia - tai jau jo pasirinkimas,
   ne musu numatytoji busena: iseinant nieko nebegrazinam, kitaip jungiklis atrodytu
   pats atsisukantis atgal (V 08-20). */
let slicerCageWas=null, slicerCageTouched=false;
{const b=$('gl3dCage');
 if(b)b.addEventListener('click',()=>{if(slicerOwnsPreview)slicerCageTouched=true;});}
const slicerCage=enter=>{
  if(!window.gl3dCage||!window.gl3dCageOn)return;
  if(enter){
    if(slicerCageWas!==null)return;
    slicerCageTouched=false;
    slicerCageWas=gl3dCageOn();
    if(slicerCageWas)gl3dCage(false);
  }else{
    if(slicerCageWas===null)return;
    if(!slicerCageTouched)gl3dCage(slicerCageWas);
    slicerCageWas=null; slicerCageTouched=false;
  }
  if(typeof syncCageBtn==='function')syncCageBtn();
};
const slicerOwns=v=>{slicerOwnsPreview=v; window.slicerOwnsPreview=v;
                     /* Isdidinimo mygtukas klausia TURINIO, o turinys ka tik pasikeite. */
                     if(window.setStageGrowUI)
                       setStageGrowUI(!(typeof statusData!=='undefined'&&statusData&&
                                        statusData.busy&&!(statusData.sdJob||'')));
                     if(!v)window.gl3dClipHeight=null;   // aukstis buvo musu, ne kito modelio
                     slicerBarUI(v);
                     slicerDetLock(v); slicerCage(v); slicerMarkUI(v); slicerBarMerge(v);
                     if(!v)slicerLayerUI(false);};
/* Sliceryje is vaizdo grupes lieka du mygtukai - narvas ir vaizdo jungiklis, - o
   del dvieju mygtuku laikyti atskira centruota eilute per daug (V 08-20). Tad
   sliceryje jie stoja i ta pacia eilute su formos irankiais, o iseinant grizta
   ten, kur gyvena. Kartu tai reiskia, kad darbo metu jie dingsta kartu su ja. */
/* Spausdinimo progreso juostele po vaizdu: sliceryje ji tuscia ir niekada
   nepasipildo - spaudinio dar nera (V 08-20). Slepiam kartu su vaizdu. */
function slicerBarUI(on){
  /* Juostele valdo pultas (`previewBarShow`): ji matoma tik spausdinant. Sliceriui
     lieka pasakyti, kad jo rezime jos tikrai nera. */
  if(on&&window.previewBarShow)previewBarShow(false);
}
function slicerBarMerge(on){
  const tools=$('gl3dTools'), zoom=$('gl3dZoom');
  if(!tools||!zoom)return;
  /* ABU vaizdo mygtukai CIA (V 08-22). Zymes vieta nieko nelemia: sliceriui
     peremus perziura visas `#gl3dZoom` paslepiamas, o jo turinys, kuris turi likti
     matomas, perkeliamas i irankiu juosta. Praleidus si sarasa mygtukas buvo
     „rodomas“ nematomoje dezeje - V du kartus jo nerado, ir teisingai. */
  ['gl3dCage','gl3dV3','gl3dV2'].forEach(id=>{
    const b=$(id); if(!b)return;
    const kur=on?tools:zoom;
    if(b.parentElement!==kur)kur.appendChild(b);
    b.disabled=false;   // svecias niekada nelieka uzrakintas seimininko uzraktu
  });
  zoom.style.display=on?'none':'flex';
}
/* Zymeklis - slicerio irankis: spausdinimo perziuroje zymeti nera ko, o mygtukas
   ten tik kabojo (V 08-20). Rodom tik kai vaizdas priklauso sliceriui IR kai pats
   irankis apskritai ijungtas (jis dev'inis - be ?dev=1 jo mygtukas lieka „none"). */
function slicerMarkUI(on){
  const w=$('gl3dMarkWrap'), b=$('gl3dMark');
  if(!w||!b)return;
  w.style.display=(on&&b.style.display!=='none')?'flex':'none';
  /* Mygtuko paslepimo neuztenka: pats irankis lieka ijungtas, ir jo remelis su defektu
     bloku persikelia ant SD perziuros (V 08-20). Isjungiam ji patį. */
  if(!on&&typeof window.gl3dMarkOff==='function')window.gl3dMarkOff();
  slicerTopLeft();
}
/* Kai matomas tik vienas is dvieju virsutiniu kaireje - jis stovi pirmoje
   vietoje; kai abu - zymeklis pirmas, pagalba antra. Kitaip likdavo tuscia vieta
   ten, kur ka tik buvo mygtukas (V 08-20). */
function slicerTopLeft(){
  const w=$('gl3dMarkWrap'), h=$('gl3dHelp');
  if(!h)return;
  const zymeklis=!!(w&&w.style.display&&w.style.display!=='none');
  h.style.left=zymeklis?'44px':'10px';
}
/* Blokas atsidaro ir uzsidaro svarus: senas modelis, jo vardas ir vaizdas
   negali persekioti tarp atidarymu (V 08-12). */
const slicerReset=()=>{
  slicerOut=null; slicerRaw=null; slicerTr=null; slicerBudget=0;
  slicerLayerN=1; slicer3dLayer=null;
  slicerFileName=''; slicerHome=true;
  slicerButtons(false);
  $('slicerGo').disabled=true; $('slicerSave').disabled=true;
  $('slicerName').value=''; $('slicerName').disabled=true;
  $('slicerFile').value='';
  $('slicerDims').textContent=''; $('slicerProg').textContent='';
  {const pn=$('slicerPlaceNote'); if(pn)pn.textContent='';}
  $('slicerInfo').textContent='Choose an STL file to begin.';
  $('slicerDiscardLink').style.visibility='hidden';
  slicerSupportFacts(null);
  if(gl3dUp())gl3dClip(null);
};
const popClose=()=>{
  const p=$('gl3dPop'); if(!p||p.style.display==='none')return;
  p.style.display='none'; popRow(true);
  slicerStep();          // grupes grizo - eiluciu tvarka perskaiciuojama
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
    /* `closest`, ne pats taikinys: ikonos viduje gali gulėti piesinys, ir nors
       jam nustatytas `pointer-events:none`, remtis vien tuo trapu - uztenka
       vienos naujos ikonos be tos taisykles, ir mygtukas nustotu veikes tyliai
       (V 08-20: „tik nesugadink ikonu ir ju rezimu"). */
    const kur=e.target&&e.target.closest&&e.target.closest('[data-tool]');
    const t=kur&&kur.dataset.tool; if(!t)return;
    e.stopPropagation();
    if(t==='scale'){
      const p=$('gl3dPop'); if(!p)return;
      if(p.style.display!=='none')popClose();
      else{p.style.display='flex';popRow(false);}
      return;
    }
    const id={fit:'slicerAutoFit',fitpro:'slicerAutoFitPro',flat:'slicerFlat',flip:'slicerFlip',
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
/* Du vaizdo mygtukai. Kiekvienas junginėja SAVO pora, o paspaustas is svetimos
   poros pirma parsiveda i save - i ta puse, kurioje zmogus buvo paskutini karta.
   Del to pirmas paspaudimas niekada nenustebina: atidaro tai, ka mygtukas rodo. */
{const b3=$('gl3dV3');
 if(b3){b3.addEventListener('click',e=>{e.stopPropagation();
   if(slicerView===2)slicerSetView(slicer3dSub);
   else{slicer3dSub=slicerView?0:1; slicerSetView(slicer3dSub);}
 });
 b3.addEventListener('pointerdown',e=>e.stopPropagation());}}
{const b2=$('gl3dV2');
 if(b2){b2.addEventListener('click',e=>{e.stopPropagation();
   if(slicerView!==2){slicerSetView(2);return;}
   /* Jau plokščiame - vercia UV puse. Sluoksnis perpiesiamas TAS PATS: slankiklis
      nejuda, keiciasi tik dazymas. */
   const on=!slicerUvOn();
   try{localStorage.setItem('tmUvView',on?'1':'0');}catch(err){}
   slicerViewButtons();
   if(window.slicerMaskDraw&&typeof slicerOut!=='undefined'&&slicerOut)
     slicerMaskDraw(slicerLayerN||slicerOut.layers);
 });
 b2.addEventListener('pointerdown',e=>e.stopPropagation());}}
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
  /* Ikeliant ir ispakuojant ekrane - uzrasas, o ne daiktas, kuri butu galima
     sukioti ar pjaustyti sluoksniais. Tad tos pacios juostos, kaip ir pjaustant:
     formos irankiai, vaizdo jungiklis ir sluoksniu slankiklis pasitraukia
     (V 08-20: „uploading ir unpacking rodo zoom ir toolus - ne i tema"). */
  slicerButtons(false); slicerWorkUI(true); slicerLayerUI(false);
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
      /* Pazymim, kad tai MUSU siuntinys: printeris `receiving` laiko dar kelias
         sekundes po uzklausos pabaigos, ir be sito pultas apie ji pasakytu „is kito
         irenginio" (V 08-20). */
      if(window.ownRxSet)ownRxSet(slicerOut.name||'');
      x.open('POST','/upload');
      x.setRequestHeader('X-TinyMaker','1');
      /* Vaizde - vaikstantis ruozelis, ne procentai. `upload.onprogress` rodo, kiek
         baitu prarijo SIO kompiuterio siuntimo buferis, o ne kiek priem\u0117 printeris:
         drobeje akimirksniu atsirasdavo \u201eUploading 100 %", ir toliau tas pilnas
         ruozas kabodavo, kol printeris is tikruju \u0117m\u0117 fail\u0105 (V 08-20; tas pats
         melas jau buvo gaudytas pulto ikelime, 08-18). Korteles eiluteje skaicius
         lieka - ten jis skaitomas kaip \u201eissiusta", ne \u201epadaryta". */
      /* Ikelimas i kortele rodomas SNACKE, ne ant vaizdo (V 08-20): darbas su SD nera
         perziuros dalykas, o pulto ikelimas jau seniai taip ir daro. Tas pats uzrasas ir
         ta pati „makaronine" juostele - dvi vietos nebeturi atrodyti skirtingai. */
      try{ snackProgress(jobText('Uploading ',shortName(slicerOut.name||'the model'),
                                 'Progress shows on the printer screen'),
                         -1,-1,'',null,'indet'); }catch(e){}
      x.upload.onprogress=e=>{
        if(!e.lengthComputable){prog.textContent='Uploading \u2026';return;}
        const p=Math.round(e.loaded/e.total*100);
        prog.textContent='Uploading '+p+'%  ('+MB(e.loaded)+' / '+MB(e.total)+' MB)';
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
    /* Korteleje apie ispakavima NERASOM (V 08-20): ta pati zinia jau stovi snacke, ir
       ten ji tikslesne - vardas ir sluoksniai. SD puseje sitas dublis isimtas seniau;
       sliceris elgiasi taip pat. */
    prog.textContent='';
    /* Ant DROBES nieko nerasom (V 08-20): ispakavima jau rodo snackas, ir jis sako
       daugiau - varda ir sluoksnius („Unpacking Ziedas 94/167 · 56 %"). Du pranesimai
       apie ta pati darba yra vienas per daug.
       BET perdanga butina NUIMTI: ikelimo ruozelis („Uploading…") sukasi laikmaciu, o
       perrasydavo ji butent sitas uzrasas - be jo drobeje liktu kabeti „Uploading…",
       kol printeris jau seniai ispakuoja (V 08-20). Nuemus lieka matomas pats slicerio
       modelis, o eiga - snacke. */
    if(typeof slicerOverlayOff==='function')slicerOverlayOff();
    for(let i=0;i<180;i++){
      await new Promise(r=>setTimeout(r,1000));
      try{
        const st=await api('/api/status',null,8000);
        if(!st.busy||st.sdJob!=='import')break;
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
  /* Darbas pereina i kita bloka, tad ir akordeonas pereina su juo: modelis jau
     printeryje, o kitas zingsnis - „Start" SD sarase. Slicerio turinys niekur
     nedingsta, jis tik susiskleidzia (V 08-20). */
  slicerOpen(false);
  if(typeof pickModel==='function')pickModel(openName);
  }finally{
    if(typeof setPreviewBusy==='function')setPreviewBusy(false);
    slicerWorkUI(false); slicerButtons(true);
    if(slicerOut)slicerLayerUI(true);
  }
});

{const b=$('slicerFitNow');
 if(b)b.addEventListener('click',()=>{const a=$('slicerAutoFit'); if(a&&!a.disabled)a.click();});}
{const b=$('slicerSend');
 if(b)b.addEventListener('click',()=>{const sv=$('slicerSave'); if(sv&&!sv.disabled)sv.click();});}
$('slicerDiscardLink').addEventListener('click',e=>{
  e.preventDefault(); slicerOut=null;
  slicerLayerN=1; slicer3dLayer=null;   // rezultato nebera - nebera ir vietos jame
  show('printPreviewBarFill',true);slicerLayerUI(false);slicerSupportFacts(null);
  $('slicerSave').disabled=true;
  $('slicerDiscardLink').style.visibility='hidden';
  $('slicerProg').textContent='Discarded. Adjust and slice again.';
  slicerRender();
  slicerStep();          // formos irankiai grizta: vel yra ka formuoti
});

/* Akordeono busena atsistato pati: zmogus paliko atidaryta sliceri - toks ir randa.
   Kortelės dar gali nebuti (modulis neaktyvus) - tada viska pasiima SD, o `akordSync`
   tvarkosi pats. Modulio cia netraukiam (zr. `slicerLoadMod`). */
setTimeout(()=>{
  const sc=$('slicerCard');
  const yra=!!(sc&&!sc.classList.contains('hidden'));
  if(yra&&window.akordPradinis&&akordPradinis()==='slicer'&&!slicerIsOpen())slicerOpen(true,true);
  else akordSync();
},0);
