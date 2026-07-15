// TinyMakerWiFi demo shim: a fake printer behind the real dashboard UI.
// Injected by build_demo.py before the app scripts. Intercepts fetch()
// (JSON API), XMLHttpRequest (/upload) and <img src> (layer/preview PNGs),
// so the extracted dashboard runs fully offline - "try without installing".
// Demo data below started as REAL captures from a live printer (2026-07-15,
// fw 0.15.0) with every secret/token replaced by fakes.
(function(){
'use strict';

// --- storage guard: sandboxed iframes (claude.ai artifacts) may block
// localStorage; fall back to an in-memory stand-in so the app never throws.
try{window.localStorage.getItem('x');}catch(e){
  var mem={};
  var fake={getItem:function(k){return k in mem?mem[k]:null;},
            setItem:function(k,v){mem[k]=String(v);},
            removeItem:function(k){delete mem[k];},clear:function(){mem={};}};
  try{Object.defineProperty(window,'localStorage',{value:fake});}catch(e2){}
}

// ---------------------------------------------------------------- demo data
var STATUS={firmwareVersion:'0.15.0',firmwareBuild:'demo',busy:false,paused:false,
  pausing:false,resuming:false,stopping:false,dryRun:false,canPause:false,
  canResume:false,canStop:false,state:'Idle',stateCode:0,layerHeight:0.10,
  wifiRssi:-47,wifiText:'-47 dBm',ip:'tinymaker.demo',sdReady:true,sdText:'Ready',
  lifetimePrintSecs:15936,lifetimePrintTime:'4h 25m',uvLedSecs:252,uvLedTime:'4m 12s',
  model:'',currentLayer:0,totalLayers:0,layerText:'0 / 0',resinUsedMl:0,
  resinText:'0.0 ml',runSecs:0,runTime:'0m 0s',remainingSecs:0,remainingTime:'0m 0s',
  webControl:true,askRefill:true,vatRemainingMl:15.0,vatText:'15.0 ml',vatLow:false,
  freeHeap:181080,minFreeHeap:126952,maxAllocHeap:110580,uptimeSecs:33};

var CONFIG={ok:true,locked:false,layerHeight:0.10,baseExposure:35,regularExposure:14,
  prevRegularExposure:0,baseLayers:2,transitionLayers:5,slowLiftDistance:1,
  fastLiftDistance:2,slowLiftFeedrate:40,fastLiftFeedrate:50,dropBackFeedrate:50,
  vatMl:15,lowResinPause:true,lowResinMl:2,askRefill:true,uiTimeoutSecs:300,
  dryRun:false,uvLedEnabled:true,wifiEnabled:true,webDashboardEnabled:true,
  bootUpdateCheck:true,statsPing:true,mqttEnabled:false,mqttConfigured:false,
  mqttHost:'',mqttPort:1883,mqttUser:'',mqttPasswordSet:false,mqttTopic:'TinyMaker',
  sdBackupPresent:true,sdBackupEpoch:1784025065,
  connectEnabled:false,connectConfigured:false,connectBaseUrl:'https://connect.tinymakerwifi.com',
  connectPrinterName:'TinyMaker demo',connectLeaderboardOptIn:false,connectAutoBackup:false,
  connectBackupEpoch:0,connectReclaimRequired:false,connectPrinterPublicId:'',
  connectTokenSet:false,connectRecoveryCodeSet:false,connectTokenTail:'',
  connectPublishToken:'',connectLastStatus:'',
  tgEnabled:false,tgTokenSet:false,tgTokenTail:'',tgChat:'',
  waEnabled:false,waKeySet:false,waKeyTail:'',waPhone:'',
  dcEnabled:false,dcHookSet:false,dcHookTail:''};

// name -> [printLayers, heightMm, estSecs, resinMl, shape]
var MODELS={
  alienBust:[433,43.3,9987,14.2,'bust'],
  dragonHead:[380,38.0,8900,11.6,'bust2'],
  Predator:[520,52.0,11900,16.8,'spike'],
  ScreamingEvil:[433,43.3,9987,13.1,'bust2'],
  Tooth:[210,21.0,4900,3.9,'spike'],
  Xenomorph3:[610,61.0,14100,18.4,'bust']};

var BOOTANIM={selected:'rippleboot',animations:[
  {name:'rippleboot',display:'Rippleboot',sizeBytes:768012},
  {name:'bunny',display:'Bunny',sizeBytes:537612},
  {name:'malfunction',display:'Malfunction',sizeBytes:1433612},
  {name:'resin-drip',display:'Resin Drip',sizeBytes:588812}]};

var UPDATE={ok:true,installed:'0.15.0',latest:'0.15.0',state:0,hasUpdate:false,allowed:true};

var uploadedSeq=0;
var sim=null; // {name,total,startMs,pausedAt,pausedTotal,secsPerLayer,ml}

// ------------------------------------------------------------- tiny helpers
function q(url,key){var m=url.match(new RegExp('[?&]'+key+'=([^&]*)'));return m?decodeURIComponent(m[1].replace(/\+/g,' ')):'';}
function fmtDur(s){s=Math.max(0,Math.round(s));var h=Math.floor(s/3600),m=Math.floor(s%3600/60);
  if(h)return h+'h '+(m<10?'0':'')+m+'m';return m+'m '+(s%60<10?'0':'')+(s%60)+'s';}
function estTime(secs){var h=Math.floor(secs/3600),m=Math.round(secs%3600/60);return h?h+'h '+(m<10?'0':'')+m+'m':m+'m';}
function jresp(obj,code){return new Response(JSON.stringify(obj),{status:code||200,headers:{'Content-Type':'application/json'}});}
function modelMeta(name){var m=MODELS[name];if(!m)return null;
  return {ok:true,name:name,sourceLayers:m[0]*2,layers:m[0]*2,printLayers:m[0],
    heightMm:m[1],estimatedSecs:m[2],estimatedTime:estTime(m[2]),
    preview:false,preview05:false,preview1:false,resinEstimated:true,resinMl:m[3]};}
function filesPayload(){
  var items=Object.keys(MODELS).map(function(n){return {name:n,type:'model',printable:true,sizeBytes:'0'};});
  return {ok:true,sdReady:true,usageKnown:true,totalBytes:'248512512',
    freeBytes:'218304512',usedBytes:'30208000',usagePct:12,items:items,hiddenCount:3};}

// -------------------------------------------------------- print simulation
function startSim(name,dry){
  var m=MODELS[name];if(!m)return false;
  sim={name:name,total:m[0],startMs:Date.now(),pausedAt:0,pausedTotal:0,
       secsPerLayer:0.45,ml:m[3],dry:!!dry}; // ~0.45 s/layer: a demo print finishes in minutes
  return true;}
function simElapsed(){if(!sim)return 0;
  var end=sim.pausedAt||Date.now();return (end-sim.startMs-sim.pausedTotal)/1000;}
function currentStatus(){
  var s=Object.assign({},STATUS);
  s.uptimeSecs=Math.round(performance.now()/1000)+33;
  s.vatRemainingMl=Math.max(0,15-(sim&&!sim.dry?Math.min(simElapsed()/(sim.total*sim.secsPerLayer),1)*sim.ml:0));
  s.vatText=s.vatRemainingMl.toFixed(1)+' ml';
  if(!sim)return s;
  var el=simElapsed(),cur=Math.min(sim.total,Math.floor(el/sim.secsPerLayer));
  if(cur>=sim.total){ // finished
    STATUS.lifetimePrintSecs+=Math.round(el);
    STATUS.lifetimePrintTime=fmtDur(STATUS.lifetimePrintSecs);
    sim=null;return currentStatus();}
  var remain=(sim.total-cur)*sim.secsPerLayer;
  var paused=!!sim.pausedAt;
  s.busy=true;s.dryRun=sim.dry;s.model=sim.name;
  s.state=paused?'Paused':(cur<1?'Homing':'Printing');
  s.stateCode=paused?2:1;
  s.paused=paused;s.canPause=!paused;s.canResume=paused;s.canStop=true;
  s.currentLayer=cur;s.totalLayers=sim.total;s.layerText=cur+' / '+sim.total;
  s.runSecs=Math.round(el);s.runTime=fmtDur(el);
  s.remainingSecs=Math.round(remain);s.remainingTime=fmtDur(remain);
  s.resinUsedMl=sim.dry?0:+(sim.ml*cur/sim.total).toFixed(1);
  s.resinText=s.resinUsedMl.toFixed(1)+' ml';
  s.sdText='Locked';
  return s;}

// -------------------------------------------------- procedural layer slices
// White silhouette on black, like a real sliced PNG. Radius profile by shape.
function profileR(shape,t){
  if(shape==='spike')return 0.85-0.65*t;
  if(shape==='bust2')return t<0.12?0.9:(t<0.55?0.45+0.25*Math.sin(t*9):(t<0.8?0.62:0.62*Math.sqrt(Math.max(0,1-((t-0.8)/0.2)*((t-0.8)/0.2)))));
  // 'bust': pedestal, neck, head
  return t<0.10?0.92:(t<0.5?0.4+0.12*Math.cos(t*14):(t<0.85?0.66:0.66*Math.sqrt(Math.max(0,1-((t-0.85)/0.15)*((t-0.85)/0.15)))));}
var sliceCanvas=null;
function sliceDataURI(name,i,total){
  var m=MODELS[name]||[400,40,9000,12,'bust'];
  var shape=m[4],t=Math.max(0,Math.min(1,total>1?(i-1)/(total-1):0));
  if(!sliceCanvas){sliceCanvas=document.createElement('canvas');sliceCanvas.width=160;sliceCanvas.height=120;}
  var cv=sliceCanvas,ctx=cv.getContext('2d');
  ctx.fillStyle='#000';ctx.fillRect(0,0,cv.width,cv.height);
  var r=profileR(shape,t);
  ctx.fillStyle='#fff';
  ctx.save();ctx.translate(cv.width/2,cv.height/2);
  ctx.beginPath();
  // slightly lumpy ellipse so renders look organic, seeded by name+layer
  var seed=0;for(var k=0;k<name.length;k++)seed=(seed*31+name.charCodeAt(k))|0;
  for(var a=0;a<=32;a++){
    var ang=a/32*Math.PI*2;
    var wob=1+0.10*Math.sin(ang*3+seed)+0.06*Math.sin(ang*5+t*10+seed);
    var rx=r*70*wob,ry=r*52*wob;
    var x=Math.cos(ang)*rx,y=Math.sin(ang)*ry;
    if(a===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);}
  ctx.closePath();ctx.fill();ctx.restore();
  return cv.toDataURL('image/png');}

// <img src> interception for layer/preview requests
var srcDesc=Object.getOwnPropertyDescriptor(HTMLImageElement.prototype,'src');
Object.defineProperty(HTMLImageElement.prototype,'src',{
  configurable:true,
  get:function(){return srcDesc.get.call(this);},
  set:function(v){
    try{
      if(typeof v==='string'&&v.indexOf('/api/files/layer')===0){
        var name=q(v,'name'),i=parseInt(q(v,'i'))||1;
        var m=MODELS[name];var total=m?m[0]:400;
        v=sliceDataURI(name,i,total);
      }else if(typeof v==='string'&&v.indexOf('/api/files/model/preview')===0){
        v='data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw=='; // never used: preview flags are off
      }
    }catch(e){}
    return srcDesc.set.call(this,v);
  }});

// --------------------------------------------------------------- fetch mock
var realFetch=window.fetch.bind(window);
window.fetch=function(path,opt){
  if(typeof path!=='string'||path.indexOf('/api/')!==0&&path.indexOf('/upload')!==0)
    return realFetch(path,opt);
  var delay=120+Math.random()*180;
  return new Promise(function(resolve){
    setTimeout(function(){resolve(route(path,opt||{}));},delay);
  });};

function route(path,opt){
  var body=typeof opt.body==='string'?opt.body:'';
  function bodyVal(key){var m=body.match(new RegExp('(?:^|&)'+key+'=([^&]*)'));return m?decodeURIComponent(m[1].replace(/\+/g,' ')):'';}

  if(path.indexOf('/api/status')===0)return jresp(currentStatus());
  if(path.indexOf('/api/config/dry-run')===0){CONFIG.dryRun=bodyVal('enabled')!=='0'&&bodyVal('enabled')!=='false';return jresp({ok:true,dryRun:CONFIG.dryRun});}
  if(path.indexOf('/api/config/backup')===0)return jresp({ok:true,note:'demo'});
  if(path.indexOf('/api/config/restore')===0)return jresp({ok:false,error:'Demo mode: restore is simulated only'},400);
  if(path.indexOf('/api/config')===0){
    if((opt.method||'GET')==='GET')return jresp(CONFIG);
    // save: merge posted urlencoded values over CONFIG where keys match
    body.split('&').forEach(function(kv){
      var p=kv.split('=');if(p.length!==2)return;
      var k=decodeURIComponent(p[0]),v=decodeURIComponent(p[1].replace(/\+/g,' '));
      if(!(k in CONFIG))return;
      if(typeof CONFIG[k]==='number')CONFIG[k]=parseFloat(v)||0;
      else if(typeof CONFIG[k]==='boolean')CONFIG[k]=(v==='1'||v==='true'||v==='on');
      else CONFIG[k]=v;});
    if(bodyVal('regular_exposure')){var nv=parseFloat(bodyVal('regular_exposure'));
      if(nv&&nv!==CONFIG.regularExposure){CONFIG.prevRegularExposure=CONFIG.regularExposure;CONFIG.regularExposure=nv;}}
    return jresp({ok:true});}
  if(path.indexOf('/api/files/model/metadata')===0)return jresp({ok:true});
  if(path.indexOf('/api/files/model/preview')===0)return jresp({ok:true});
  if(path.indexOf('/api/files/model')===0){
    var meta=modelMeta(q(path,'name'));
    return meta?jresp(meta):jresp({ok:false,error:'model not found'},404);}
  if(path.indexOf('/api/files/delete')===0){
    var dn=q(path,'name');delete MODELS[dn];return jresp({ok:true});}
  if(path.indexOf('/api/files')===0)return jresp(filesPayload());
  if(path.indexOf('/api/print/start')===0){
    var pn=q(path,'name');
    if(sim)return jresp({ok:false,error:'printer busy'},409);
    return startSim(pn,CONFIG.dryRun)?jresp({ok:true}):jresp({ok:false,error:'model not found'},404);}
  if(path.indexOf('/api/print/pause')===0){if(sim&&!sim.pausedAt)sim.pausedAt=Date.now();return jresp({ok:true});}
  if(path.indexOf('/api/print/resume')===0){if(sim&&sim.pausedAt){sim.pausedTotal+=Date.now()-sim.pausedAt;sim.pausedAt=0;}return jresp({ok:true});}
  if(path.indexOf('/api/print/stop')===0){sim=null;return jresp({ok:true});}
  if(path.indexOf('/api/vat/refilled')===0)return jresp({ok:true,vatRemainingMl:15});
  if(path.indexOf('/api/update/install')===0)return jresp({ok:false,error:'Demo mode: the update flow needs a real printer'},400);
  if(path.indexOf('/api/update')===0)return jresp(UPDATE);
  if(path.indexOf('/api/boot-anim/select')===0){BOOTANIM.selected=bodyVal('name');return jresp({ok:true});}
  if(path.indexOf('/api/boot-anim/delete')===0){
    var bn=bodyVal('name');
    BOOTANIM.animations=BOOTANIM.animations.filter(function(a){return a.name!==bn;});
    if(BOOTANIM.selected===bn)BOOTANIM.selected='';
    return jresp({ok:true});}
  if(path.indexOf('/api/boot-anim/preview')===0)return jresp({ok:true,note:'demo: pretend the printer screen just played it'});
  if(path.indexOf('/api/boot-anim/install')===0)return jresp({ok:false,error:'Demo mode: no SD card to install to'},400);
  if(path.indexOf('/api/boot-anim')===0)return jresp(BOOTANIM);
  if(path.indexOf('/api/telegram/test')===0||path.indexOf('/api/whatsapp/test')===0||path.indexOf('/api/discord/test')===0)
    return jresp({ok:true});
  if(path.indexOf('/api/connect')===0)return jresp({ok:false,error:'Demo mode: TinyMaker Connect needs a real printer'},400);
  if(path.indexOf('/api/version')===0)return jresp({api:'0.1',server:'1.5.0',text:'Prusa SLA (TinyMaker demo)'});
  return jresp({ok:true});}

// ------------------------------------------------------- XHR mock (/upload)
var RealXHR=window.XMLHttpRequest;
window.XMLHttpRequest=function(){
  var real=new RealXHR(),self=this,fakeUrl=null;
  this.upload={};
  ['status','responseText','response','readyState'].forEach(function(k){
    Object.defineProperty(self,k,{get:function(){return fakeUrl?self['_'+k]:real[k];}});});
  this.open=function(method,url){fakeUrl=(url==='/upload')?url:null;
    if(!fakeUrl)real.open(method,url);};
  this.setRequestHeader=function(a,b){if(!fakeUrl)real.setRequestHeader(a,b);};
  this.abort=function(){if(!fakeUrl)real.abort();};
  this.send=function(data){
    if(!fakeUrl){
      ['onload','onerror','onabort','onprogress'].forEach(function(k){real[k]=self[k];});
      if(self.upload.onprogress)real.upload.onprogress=self.upload.onprogress;
      if(self.upload.onload)real.upload.onload=self.upload.onload;
      return real.send(data);}
    // simulated upload: ~2.5 s of progress, then ~2 s "unpacking", then OK
    var total=(data&&data.get&&data.get('file')&&data.get('file').size)||3200000;
    var t0=Date.now(),D=2500;
    var iv=setInterval(function(){
      var f=Math.min(1,(Date.now()-t0)/D);
      if(self.upload.onprogress)self.upload.onprogress({loaded:Math.round(total*f),total:total,lengthComputable:true});
      if(f>=1){
        clearInterval(iv);
        if(self.upload.onload)self.upload.onload({});
        setTimeout(function(){
          uploadedSeq++;
          var name='MyUpload'+(uploadedSeq>1?uploadedSeq:'');
          MODELS[name]=[240,24.0,5600,6.5,'spike'];
          self._status=200;
          self._responseText=JSON.stringify({ok:true,name:name,layers:480,printLayers:240,sourceLayers:480,heightMm:24.0});
          if(self.onload)self.onload({});
        },2000);}
    },120);};
};

// ---------------------------------------------------------------- demo chrome
document.addEventListener('DOMContentLoaded',function(){
  var b=document.createElement('div');
  b.innerHTML='DEMO &middot; simulated printer &nbsp;<a href="#" id="demoReset" style="color:inherit;text-decoration:underline">reset</a>';
  b.style.cssText='position:fixed;right:10px;bottom:10px;z-index:9999;background:#e8720c;color:#fff;'+
    'font:600 12px/1 -apple-system,Segoe UI,sans-serif;padding:7px 11px;border-radius:8px;opacity:.92';
  document.body.appendChild(b);
  b.querySelector('#demoReset').addEventListener('click',function(ev){
    ev.preventDefault();try{localStorage.clear();}catch(e){}location.reload();});
});
})();
