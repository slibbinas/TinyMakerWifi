/* Bendra dervų profilio lentelė: ją naudoja ir resin-publish.html, ir
   resin-lab/. Viena vieta, kad firmware ribos negalėtų išsiskirti. */
'use strict';
/* ---- laukai: ribos tiksliai tokios, kokias tikrina firmware ---------------
   ResinProfile.ino:193-230. Skirtumas svarbus: „clamp" reiškia, kad printeris
   reikšmę apkarpys iki ribos, o „skip" - kad visai jos nepriims ir liks gamyklinė.
   Abu atvejai tylūs, todėl neleidžiam jiems atsirasti. --------------------- */
const F=[
 {k:'base_exposure',    t:'Bazinė ekspozicija',u:'s',  min:5,  max:60, step:1,    dec:0,mode:'clamp'},
 {k:'regular_exposure', t:'Įprasta ekspozicija',u:'s', min:1,  max:30, step:0.1,  dec:1,mode:'clamp'},
 {k:'base_layers',      t:'Bazinių sluoksnių',u:'',    min:1,  max:8,  step:1,    dec:0,mode:'clamp'},
 {k:'transition_layers',t:'Perėjimo sluoksnių',u:'',   min:0,  max:10, step:1,    dec:0,mode:'clamp'},
 {k:'slow_lift_distance',t:'Lėto kėlimo aukštis',u:'mm',min:1, max:3,  step:1,    dec:0,mode:'clamp'},
 {k:'fast_lift_distance',t:'Greito kėlimo aukštis',u:'mm',min:1,max:3, step:1,    dec:0,mode:'clamp'},
 {k:'slow_lift_feedrate',t:'Lėto kėlimo greitis',u:'', min:20, max:50, step:10,   dec:0,mode:'clamp'},
 {k:'fast_lift_feedrate',t:'Greito kėlimo greitis',u:'',min:20,max:50, step:10,   dec:0,mode:'clamp'},
 {k:'drop_back_feedrate',t:'Nuleidimo greitis',u:'',   min:20, max:50, step:10,   dec:0,mode:'clamp'},
 {k:'density',          t:'Tankis',u:'g/ml',           min:0.8,max:2.0,step:0.001,dec:3,mode:'skip',dmin:1},
 {k:'cal_factor',       t:'Kalibr. koeficientas',u:'', min:0.5,max:2.0,step:0.001,dec:3,mode:'skip',dmin:1},
 {k:'cal_fixed_ml',     t:'Pastovus priedas',u:'ml',   min:0,  max:10, step:0.01, dec:2,mode:'skip',dmin:1}
];
/* Gamyklinis „slow" - naujos dervos startas (RESIN_BUILTIN, ResinProfile.ino). */
const BASE={layer_height:0.05,base_exposure:18,regular_exposure:8.0,base_layers:4,
 transition_layers:5,slow_lift_distance:1,fast_lift_distance:2,slow_lift_feedrate:40,
 fast_lift_feedrate:50,drop_back_feedrate:50,density:1.1,cal_factor:1.0,cal_fixed_ml:0.0};

/* Lietuviškas rašmuo virsta artimiausiu lotynišku, o ne dingsta: „Testinė derva"
   turi tapti „testine-derva", ne „testin-derva". Rezultatas visada lieka toks,
   kokį priima ir pultas, ir kortelės failų sistema. */
const LT_MAP={'ą':'a','č':'c','ę':'e','ė':'e','į':'i','š':'s','ų':'u','ū':'u','ž':'z'};
const slugify=s=>(s||'').toLowerCase().replace(/[ąčęėįšųūž]/g,c=>LT_MAP[c])
  .replace(/[^a-z0-9-_]+/g,'-').replace(/^-+|-+$/g,'').slice(0,40);
