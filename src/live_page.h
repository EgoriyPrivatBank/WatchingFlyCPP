#pragma once
// Embedded live dashboard (no external resources; served at / by live.cpp).
static const char LIVE_PAGE[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Цифровая муха — live</title>
<style>
body{font-family:"Segoe UI",Arial,sans-serif;background:#12151b;color:#dce3ec;margin:0;padding:16px}
h1{font-size:19px;margin:0 0 2px}.sub{color:#8a97a8;font-size:12px;margin-bottom:10px}
.row{display:flex;gap:14px;flex-wrap:wrap}
.card{background:#1b212b;border-radius:10px;padding:10px 14px;margin:8px 0}
.kpi{min-width:96px}.kpi b{font-size:20px;display:block;color:#fff}.kpi span{font-size:11px;color:#8a97a8}
canvas{display:block}
#map{cursor:crosshair;background:#0d1015;border-radius:6px}
#banner{background:#8b1a1a;color:#fff;padding:8px 12px;border-radius:8px;display:none;margin:8px 0}
#les{background:#7a5a00;color:#ffe082;padding:6px 12px;border-radius:8px;display:none;margin:8px 0}
#flash{position:fixed;top:10px;right:10px;background:#d32f2f;color:#fff;padding:14px 22px;border-radius:10px;font-size:18px;opacity:0;pointer-events:none;transition:opacity .4s}
button,select,input[type=text]{background:#252d3a;color:#dce3ec;border:1px solid #37424f;border-radius:7px;padding:5px 11px;font-size:13px;margin:2px}
button:hover{background:#31405280}
.evlog{font-size:12px;max-height:130px;overflow:auto}
.evlog div{padding:2px 0;border-bottom:1px solid #232b36}
table.t20{border-collapse:collapse;font-size:12px}
table.t20 td,table.t20 th{padding:2px 10px 2px 0;text-align:left;border-bottom:1px solid #232b36}
.foot{font-size:11px;color:#77839a;font-style:italic;margin-top:6px}
#tip{position:fixed;background:#000d;border:1px solid #333c48;border-radius:8px;padding:6px 10px;font-size:12px;pointer-events:none;display:none;z-index:9;white-space:pre}
.leg{font-size:11px;color:#aab6c5;margin-top:4px}.leg i{display:inline-block;width:9px;height:9px;border-radius:2px;margin:0 4px 0 12px}
#vwrap{position:relative;width:640px;max-width:100%}
#overlay{position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none}
#flyview{image-rendering:pixelated;width:320px}
video{width:100%;border-radius:8px;background:#000}
</style></head><body>
<h1>Цифровая муха на коннектоме — LIVE</h1>
<div class="sub" id="sub">…</div>
<div id="banner">REGRESSION FAIL: карточка ≠ таблица ≠ маркеры</div>
<div id="les">ЛЕЗИЯ АКТИВНА: <span id="lesname"></span> (эксперимент не «чистый», колонка lesion в CSV)</div>
<div class="card" id="ctlbar">
<button id="bPause">⏸ пауза</button>
<button data-sp="0.5">0.5×</button><button data-sp="1">1×</button>
<button data-sp="2">2×</button><button data-sp="max">макс</button>
<button id="bRestart">⟳ перезапуск</button>
<select id="lesion"><option value="">без лезии</option>
 <option value="lc">выключить LC4/LPLC2</option><option value="t4t5">выключить T4/T5</option>
 <option value="da">выключить дофаминовые</option><option value="photos">выключить фоторецепторы</option>
 <option value="mbon">выключить MBON</option></select>
<label style="font-size:12px"><input type="checkbox" id="snd"> звук побега</label>
<span style="margin-left:12px;font-size:12px">новое видео:</span>
<input type="text" id="newurl" size="34" placeholder="https://youtu.be/...">
<button id="bGo">▶ смотреть</button><button id="bCancel" style="display:none">✕ отмена</button>
<div id="dlbar" style="display:none;margin-top:6px">
 <div style="height:14px;background:#0d1015;border-radius:7px;overflow:hidden">
  <div id="dlfill" style="height:100%;width:0%;background:#1e88e5;transition:width .3s"></div></div>
 <span class="foot" id="dltext"></span></div>
</div>
<div class="row">
 <div class="card"><div id="vwrap"><video id="vid" muted playsinline controls></video><canvas id="overlay" width="640" height="360"></canvas></div>
  <div style="margin-top:6px"><label style="font-size:12px"><input type="checkbox" id="showgrid" checked> сетка глаза (768 омматидиев)</label>
  <label style="font-size:12px;margin-left:12px"><input type="checkbox" id="showfly"> что видит муха</label>
  <span class="foot" id="vstat" style="margin-left:12px">видео: готовится…</span></div>
  <canvas id="flyview" width="32" height="48" style="display:none"></canvas>
 </div>
 <div class="card" style="flex:1;min-width:420px"><h3 style="margin:2px 0 6px;font-size:14px">Сводка</h3>
  <div class="row" id="kpis">
   <div class="kpi"><b id="kEsc">0</b><span>попыток побега</span></div>
   <div class="kpi"><b id="kBeh">—</b><span>поведение</span></div>
   <div class="kpi"><b id="kSpd">—</b><span>скорость ×rt</span></div>
   <div class="kpi"><b id="kTime">0:00</b><span>время</span></div>
   <div class="kpi"><b id="kFrm">0</b><span>кадр</span></div>
   <div class="kpi"><b id="kAct">0</b><span>активных (&gt;0.5)</span></div>
   <div class="kpi"><b id="kThr">—</b><span>потоки</span></div>
  </div>
  <h3 style="margin:10px 0 4px;font-size:14px">Журнал побегов</h3>
  <div class="evlog" id="evlog"></div>
  <div class="foot">GF = гигантское волокно DNp01; LC4/LPLC2 — детекторы лунинга.</div>
 </div>
</div>
<div class="card"><h3 style="font-size:14px;margin:2px 0 6px">DNp01 и LC4/LPLC2 (окно 60 с; пунктиры — побеги)</h3>
<canvas id="ch1" width="1200" height="130" style="width:100%"></canvas></div>
<div class="row">
<div class="card" style="flex:1"><h3 style="font-size:14px;margin:2px 0 6px">Удовлетворённость (z-норм., динамика, не среднее)</h3>
<canvas id="ch2" width="590" height="110" style="width:100%"></canvas></div>
<div class="card" style="flex:1"><h3 style="font-size:14px;margin:2px 0 6px">Глаз: движение / транзиенты / контраст</h3>
<canvas id="ch3" width="590" height="110" style="width:100%"></canvas></div>
</div>
<div class="row">
 <div class="card"><h3 style="font-size:14px;margin:2px 0 6px">Карта активности 166 700 нейронов</h3>
  <canvas id="map" width="408" height="420"></canvas>
  <div class="foot">В данных нет 3D-координат: карта схематична (группировка по классам), а не анатомическая.
  <select id="mapmode" style="font-size:11px;margin-left:6px"><option value="abs">абсолютная</option><option value="rel">относительно покоя</option></select><span id="mapnote"></span></div>
 </div>
 <div class="card" style="flex:1"><h3 style="font-size:14px;margin:2px 0 6px">Путь сигнала (яркость = активность, толщина = синапсы)</h3>
  <canvas id="pipe" width="560" height="170"></canvas>
  <h3 style="font-size:14px;margin:8px 0 4px">Топ-20 активных нейронов</h3>
  <table class="t20" id="t20"></table>
 </div>
</div>
<div id="flash">🐝 ПОБЕГ! (DNp01)</div>
<div id="tip"></div>
<script>
'use strict';
// Live dashboard: /stats (JSON, ~20 Hz), /frame.bin (N bytes), /eye.bin
// (9-byte header + 3*768 bytes), /layout.bin (13-byte header + N*18 + strings).
// Chart buffers are explicit rows; no object iteration, so lengths stay aligned.

window.onerror=(m,s,l)=>{const n=document.getElementById('mapnote');
 if(n)n.textContent=' JS-ОШИБКА: '+m+' @'+l; return false;};
const $=id=>document.getElementById(id);
const BEH=["покой","активность","ходьба/груминг","полёт","прыжок/побег"];
const BEHC=["#3b4754","#8d6e63","#388e3c","#1e88e5","#e53935"];
const NT=["?","ACh","GABA","Glu","Oct","DA","5-HT","Hist"];
let S=null,ESC=0,evSeen=0,t0=null;
const W=60,buf={rows:[]};
function ctl(body){fetch('/ctl',{method:'POST',body:body}).catch(()=>{});}
$('bPause').onclick=()=>{if(S&&S.paused)ctl('pause=0');else ctl('pause=1');};
document.querySelectorAll('[data-sp]').forEach(b=>b.onclick=()=>ctl('speed='+b.dataset.sp));
$('bRestart').onclick=()=>ctl('restart=1');
$('lesion').onchange=()=>ctl('lesion='+$('lesion').value);
$('bGo').onclick=()=>{const u=$('newurl').value.trim();if(u)ctl('url='+encodeURIComponent(u));};
$('bCancel').onclick=()=>ctl('cancel=1');
function drawDl(){const d=S&&S.dl;if(!d)return;
 const busy=d.stage&&d.stage!=='sim';
 $('dlbar').style.display=busy||d.err?'block':'none';
 $('newurl').disabled=busy;$('bGo').disabled=busy;
 $('bCancel').style.display=busy?'inline-block':'none';
 if(d.err){$('dltext').textContent='ОШИБКА: '+d.err;$('dlfill').style.width='0%';$('dlfill').style.background='#e53935';return;}
 $('dlfill').style.background='#1e88e5';
 if(!busy)return;
 const st=d.stage==='download'?'скачивание':d.stage==='convert'?'конвертация для браузера':d.stage;
 $('dltext').textContent=st+' '+d.pct.toFixed(1)+'% · '+d.mbs.toFixed(1)+' МиБ/с · осталось '+fmt(d.eta||0);
 $('dlfill').style.width=d.pct+'%';}
let audio=null;
function beep(){if(!$('snd').checked)return;try{audio=audio||new (window.AudioContext||window.webkitAudioContext)();
 const o=audio.createOscillator(),g=audio.createGain();o.type='square';o.frequency.value=880;
 g.gain.setValueAtTime(.12,audio.currentTime);g.gain.exponentialRampToValueAtTime(.001,audio.currentTime+.25);
 o.connect(g);g.connect(audio.destination);o.start();o.stop(audio.currentTime+.25);}catch(e){}}
let netOK=true,netFail=0,polling=false;
async function poll(){
 if(polling)return;
 polling=true;
 try{
  const v=$('vid');
  const u=(S&&S.replay)?('/stats?i='+Math.round((v.currentTime||0)*30)):'/stats';
  const ac=(typeof AbortController!=='undefined')?new AbortController():null;
  const to=ac?setTimeout(()=>ac.abort(),2500):0;
  let r;
  try{ r=await fetch(u,{cache:'no-store',signal:ac?ac.signal:undefined}); }
  finally{ if(to)clearTimeout(to); }
  if(!r.ok)throw new Error('http '+r.status);
  S=await r.json();
  netOK=true;netFail=0;upd();
 }catch(e){
  netOK=false;if(++netFail>=4){S=null;$('sub').textContent='НЕТ СВЯЗИ с симуляцией — повторные попытки…';}
 }finally{polling=false;}
}
setInterval(()=>{if(!netOK)$('sub').textContent='НЕТ СВЯЗИ с симуляцией — повторные попытки…';
 else if(!S)$('sub').textContent='подключение…';},500);
function fmt(s){return Math.floor(s/60)+':'+String(Math.floor(s%60)).padStart(2,'0');}
function upd(){
 $('sub').textContent=(S.replay?'REPLAY (данные из CSV, карта — агрегаты по классам)':'LIVE · 127.0.0.1 · проводка: реальный коннектом (166 700 нейронов)')+
  (S.paused?' · ПАУЗА':'')+' · симуляция продолжается независимо от этой вкладки';
 $('kEsc').textContent=S.escapes;$('kBeh').textContent=BEH[S.beh]||S.beh;
 $('kSpd').textContent=S.speed<=0?'макс':S.speed.toFixed(2)+'×';
 $('kTime').textContent=fmt(S.t);$('kFrm').textContent=S.frame;
 $('kAct').textContent=S.active;$('kThr').textContent=S.threads;
 $('bPause').textContent=S.paused?'▶ продолжить':'⏸ пауза';
 $('ctlbar').style.display=S.replay?'none':'block';
 if(!S.replay){const v=$('vid');
   if(S.paused&&v&&!v.paused)v.pause();
   if(!S.paused&&v&&v.paused&&v.src&&v.readyState>=2)v.play().catch(()=>{});}
 if(S.replay)drawMapAgg();
 if(S.lesion){$('les').style.display='block';$('lesname').textContent=S.lesion;}else $('les').style.display='none';
 t0=t0??S.t;
 // chart ring buffer: one row per poll, explicit keys only (never iterate buf)
 const fin=v=>(typeof v==='number'&&isFinite(v))?v:0;
 if(buf.rows.length&&S.t+0.01<buf.rows[buf.rows.length-1].t)buf.rows.length=0; // restart/new video
 buf.rows.push({t:fin(S.t),gf:fin(S.gf),lc4:fin(S.lc4),sat:fin(S.sat),
   motion:fin(S.motion),transient:fin(S.transient),contrast:fin(S.contrast)});
 while(buf.rows.length&&buf.rows[0].t<S.t-60)buf.rows.shift();
 if(buf.rows.length>7200)buf.rows.shift();  // hard cap (~6 min at 20 Hz)
 if(S.escapes>evSeen){evSeen=S.escapes;const f=$('flash');f.style.opacity=1;setTimeout(()=>f.style.opacity=0,900);beep();}
 const ev=S.events||[];
 $('evlog').innerHTML=ev.slice(-12).reverse().map(e=>`<div>${fmt(e[0])} (${e[0].toFixed(1)} с) · GF ${e[1].toFixed(3)} · LC4 ${e[2].toFixed(3)}</div>`).join('');
 // regression: card == log == markers
 const nLog=ev.length;
 $('banner').style.display=(S.escapes!==evSeen||S.escapes!==nLog)?'block':'none';
 if(!S.replay){const v=$('vid');if(Math.abs(v.currentTime-S.t)>0.15){try{v.currentTime=S.t;}catch(e){}}}
 drawCharts();
 drawDl();
}
function drawCharts(){
 const mk=(cv,series,evTimes)=>{const x=cv.getContext('2d'),w=cv.width,h=cv.height;
  x.clearRect(0,0,w,h);
  const rows=buf.rows;if(rows.length<2){x.fillStyle='#77839a';x.font='10px Segoe UI';x.fillText('нет данных…',30,h/2);return;}
  const t1=rows[rows.length-1].t,t0v=Math.max(rows[0].t,t1-W);
  if(!(t1>t0v)){x.fillStyle='#77839a';x.font='10px Segoe UI';x.fillText('ожидание времени симуляции…',30,h/2);return;}
  let hi=1e-9;for(const s of series)for(const r of rows){const v=r[s.k];if(isFinite(v)&&v>hi)hi=v;}
  hi*=1.15;
  const X=t=>(t-t0v)/((t1-t0v)||1)*(w-30)+30,Y=v=>h-8-((isFinite(v)?v:0)/hi)*(h-16);
  x.strokeStyle='#2a3340';for(let i=0;i<=4;i++){const y=8+(h-16)*i/4;x.beginPath();x.moveTo(30,y);x.lineTo(w,y);x.stroke();}
  for(const s of series){x.strokeStyle=s.c;x.lineWidth=1.5;x.beginPath();let st=false;
   for(const r of rows){if(r.t<t0v)continue;const px=X(r.t),py=Y(r[s.k]);
    if(!st){x.moveTo(px,py);st=true;}else x.lineTo(px,py);}x.stroke();}
  if(evTimes&&evTimes.length){x.strokeStyle='#ff5252';x.setLineDash([3,3]);
   for(const e of evTimes){if(e<t0v||e>t1)continue;const px=X(e);x.beginPath();x.moveTo(px,6);x.lineTo(px,h-8);x.stroke();}
   x.setLineDash([]);}
  x.fillStyle='#77839a';x.font='10px Segoe UI';x.fillText(hi.toFixed(3),2,12);};
 const evT=(S&&S.events||[]).map(e=>+e[0]||0).filter(v=>isFinite(v));
 mk($('ch1'),[{k:'gf',c:'#e53935'},{k:'lc4',c:'#ffb300'}],evT);
 mk($('ch2'),[{k:'sat',c:'#ab47bc'}],null);
 mk($('ch3'),[{k:'motion',c:'#26a69a'},{k:'transient',c:'#ffa726'},{k:'contrast',c:'#78909c'}],null);
}
// ---------- video ----------
const vid0=$('vid');vid0.pause();
function vstat(s){$('vstat').textContent='видео: '+s;}
let vidTries=0;
function vidAttach(){vid0.src='/video';}
vid0.addEventListener('error',()=>{const e=vid0.error;
 if(vid0.src && vidTries<40 && (e && (e.code===2||e.code===4))){  // network/format: retry until ready
  ++vidTries;vstat('видео готовится… ('+vidTries+')');
  setTimeout(()=>{vid0.load();vidAttach();},3000);
 } else vstat('ОШИБКА '+(e?('код '+e.code+': '+e.message):'?'));});
vid0.addEventListener('loadeddata',()=>{vidTries=0;vstat('готово ✓');});
vid0.addEventListener('canplay',()=>{vidTries=0;vstat('готово ✓');});
setInterval(()=>{if(vid0.readyState>=2)vstat('готово ✓');else if(!vid0.error)vstat('готовится…');},1000);
vidAttach();
const ov=$('overlay'),ovx=ov.getContext('2d');
let eye=new Uint8Array(768*3);
async function eyePoll(){try{const r=await fetch('/eye.bin');const b=new Uint8Array(await r.arrayBuffer());
 if(b.length>=9+768*3)eye=b.subarray(9);}catch(e){}}
function drawOverlay(){ovx.clearRect(0,0,ov.width,ov.height);
 if(!$('showgrid').checked)return;
 const C=32,R=24;
 for(let r=0;r<R;r++)for(let c=0;c<C;c++){const p=r*C+c;
  const x=(c+0.5)/C*ov.width,y=(r+0.5)/R*ov.height;
  const m=eye[1536+p]/127||0;
  if(m>0.15){ovx.strokeStyle=`rgba(255,180,0,${Math.min(0.9,m)})`;ovx.strokeRect(x-ov.width/64,y-ov.height/48,ov.width/32,ov.height/24);}}}
const fv=$('flyview'),fvx=fv.getContext('2d');
function drawFlyView(){if(!$('showfly').checked){fv.style.display='none';return;}fv.style.display='block';
 const im=fvx.createImageData(32,48);
 for(let p=0;p<768;p++){const row=Math.floor(p/32),col=p%32;
  const it=(row*32+col)*4;                   // top: photo drive
  const ib=((row+24)*32+col)*4;              // bottom: lamina drive
  const vp=eye[p], vl=eye[768+p];
  im.data[it]=vp;im.data[it+1]=vp*0.8;im.data[it+2]=vp*0.3;im.data[it+3]=255;
  im.data[ib]=0;im.data[ib+1]=vl*0.9;im.data[ib+2]=vl;im.data[ib+3]=255;}
 fvx.putImageData(im,0,0);}
// ---------- neuron map ----------
let LAY=null,mapCtx=$('map').getContext('2d'),mapImg=null,rev=null,mapBase=null;
async function loadLayoutOnce(){try{
 const r=await fetch('/layout.bin');const b=new Uint8Array(await r.arrayBuffer());
 const dv=new DataView(b.buffer);
 // server layout: "FLYL1"(5) | N(4) | ns(4) | N*18 neurons | ns*strings
 const N=dv.getUint32(5,true),NS=dv.getUint32(9,true);let o=13;
 if(N<1000)return false;GROUPS=await (await fetch('/groups')).json();
 LAY={N,px:new Uint16Array(N),py:new Uint16Array(N),g:new Uint8Array(N),root:new Uint32Array(N),
      ti:new Uint32Array(N),ci:new Uint32Array(N),nt:new Uint8Array(N),str:[]};
 for(let i=0;i<N;i++){LAY.px[i]=dv.getUint16(o,true);LAY.py[i]=dv.getUint16(o+2,true);LAY.g[i]=b[o+4];
  LAY.root[i]=dv.getUint32(o+5,true);LAY.ti[i]=dv.getUint32(o+9,true);LAY.ci[i]=dv.getUint32(o+13,true);LAY.nt[i]=b[o+17];o+=18;}
 for(let k=0;k<NS;k++){const L=dv.getUint32(o,true);o+=4;LAY.str.push(new TextDecoder().decode(b.subarray(o,o+L)));o+=L;}
 mapImg=mapCtx.createImageData(408,420);
 // base: dim per-class block color (map is visible even at rest)
 for(const g of GROUPS)for(let yy=g.y;yy<g.y+g.h&&yy<420;yy++)
  for(let xx=g.x;xx<g.x+g.w&&xx<408;xx++){const p=(yy*408+xx)*4;
   const m=g.c&&g.c.match(/\d+/g);
   if(m){mapImg.data[p]=+m[0];mapImg.data[p+1]=+m[1];mapImg.data[p+2]=+m[2];mapImg.data[p+3]=255;}}
 rev=new Int32Array(408*420).fill(-1);
 mapBase=new Uint8ClampedArray(mapImg.data);      // dim per-class base
 LAY.rest=new Float32Array(N);                    // slow EMA of activity (resting mean)
 for(let i=0;i<N;i++){const p=LAY.py[i]*408+LAY.px[i];if(p>=0&&p<rev.length)rev[p]=i;}
 drawMapLabels();drawPipe();$('mapnote').textContent=' Наведите курсор на пиксель.';
 return true;
}catch(e){return false;}}
async function loadLayout(){for(let k=0;k<6&&!LAY;k++){if(await loadLayoutOnce())break;await new Promise(r=>setTimeout(r,1000));}}
let GROUPS=null;
function drawMapLabels(){if(!GROUPS)return;const x=mapCtx;x.save();x.font='9px Segoe UI';
 for(const g of GROUPS){if(g.w===0)continue;x.fillStyle='#8a97a8';x.strokeStyle='#232b36';
  x.strokeRect(g.x+.5,g.y+.5,g.w,g.h);x.fillText(g.name,g.x,g.y+g.h+9);}x.restore();}
async function framePoll(){try{const r=await fetch('/frame.bin');const b=new Uint8Array(await r.arrayBuffer());
 if(!LAY||b.length<9+LAY.N)return;const a=b.subarray(9);
 const d=mapImg.data,base=mapBase;
 const rel=$('mapmode').value==='rel';
 const restA=1-0.05/20;   // ~20 s EMA of the resting mean per neuron
 for(let i=0;i<LAY.N;i++){const p=(LAY.py[i]*408+LAY.px[i])*4;
  const cur=Math.sqrt(a[i]/255);
  LAY.rest[i]+=(cur-LAY.rest[i])*(1-restA);
  // sqrt scale: visible at low rates; rel mode shows deviation from resting mean
  let v=cur;
  if(rel)v=Math.max(0,v-LAY.rest[i]);
  const gl=rel?Math.min(1,v*2):Math.min(1,v*1.6);
  d[p]  =base[p]  *(1-gl)+(gl>0?255*(0.35+0.65*gl):base[p]);
  d[p+1]=base[p+1]*(1-gl)+(gl>0?200*(0.35+0.65*gl):base[p+1]);
  d[p+2]=base[p+2]*(1-gl)+(gl>0?120*(0.35+0.65*gl):base[p+2]);
  d[p+3]=255;}
 mapCtx.putImageData(mapImg,0,0);drawMapLabels();
}catch(e){}}
$('map').onmousemove=e=>{if(!LAY)return;const r=$('map').getBoundingClientRect();
 const x=Math.floor((e.clientX-r.left)/r.width*408),y=Math.floor((e.clientY-r.top)/r.height*420);
 const i=(x>=0&&x<408&&y>=0&&y<420)?rev[y*408+x]:-1;
 const tip=$('tip');
 if(i<0){tip.style.display='none';return;}
 tip.textContent=`root ${LAY.root[i]}\nтип: ${LAY.str[LAY.ti[i]]||'—'}\nкласс: ${LAY.str[LAY.ci[i]]||'—'}\nНТ: ${NT[LAY.nt[i]]}\nактивность: ${Math.sqrt((mapImg.data[(y*408+x)*4]-mapBase[(y*408+x)*4])/255>0?(mapImg.data[(y*408+x)*4]-mapBase[(y*408+x)*4])/255:0).toFixed(3)}`;
 tip.style.display='block';tip.style.left=(e.clientX+14)+'px';tip.style.top=(e.clientY-8)+'px';};
$('map').onmouseleave=()=>$('tip').style.display='none';
// ---------- pipeline ----------
let PIPE=null;
async function loadPipe(){try{PIPE=await (await fetch('/pipeline')).json();}catch(e){}}
function drawPipe(){if(!PIPE)return;const cv=$('pipe'),x=cv.getContext('2d');
 x.clearRect(0,0,cv.width,cv.height);
 const names=["фоторецепторы","ламина","T4/T5","LC4/LPLC2","DNp01 (GF)","моторные"];
 const nw=cv.width/6;
 const act=[S?(+S.photo||0):0,S?(+S.lam||0):0,S?(+S.t4t5||0):0,S?(+S.lc4||0):0,
   S?(+S.gf||0):0,S?(+S.motor||0):0];
 const pts=names.map((n,i)=>({x:nw*i+nw/2,y:cv.height/2,n,a:act[i]}));
 for(const e of PIPE.edges){const a=pts[e.a],b=pts[e.b];
  const wgt=1+Math.min(9,Math.log10(e.w+1)*2);
  x.strokeStyle='#2e3947';x.lineWidth=wgt;x.beginPath();x.moveTo(a.x+18,a.y);x.lineTo(b.x-18,b.y);x.stroke();
  x.fillStyle='#77839a';x.font='9px Segoe UI';x.fillText(fmtW(e.w),(a.x+b.x)/2-14,(a.y+b.y)/2-4);}
 pts.forEach((p,i)=>{const gl=Math.min(1,p.a*6);
  x.beginPath();x.arc(p.x,p.y,15,0,7);
  x.fillStyle=`rgba(${120+135*gl|0},${180*gl|0},${60*gl|0},${0.35+0.65*gl})`;x.fill();
  x.strokeStyle='#37424f';x.stroke();
  x.fillStyle='#dce3ec';x.font='10px Segoe UI';x.textAlign='center';
  x.fillText(p.n,p.x,p.y-24);x.textAlign='left';
  x.fillStyle='#77839a';x.fillText((p.a).toFixed(3),p.x-13,p.y+30);});}
function fmtW(w){return w>=1e6?(w/1e6).toFixed(1)+'M':w>=1e3?(w/1e3).toFixed(0)+'k':''+w;}
// ---------- top20 ----------
function drawTop(){if(!S||!S.top20)return;
 $('t20').innerHTML='<tr><th>root ID</th><th>тип</th><th>класс</th><th>акт.</th></tr>'+
  S.top20.map(r=>`<tr><td>${r[0]??'—'}</td><td>${r[1]||'—'}</td><td>${r[2]||'—'}</td><td>${(+r[3]||0).toFixed(3)}</td></tr>`).join('');}
// ---------- loops ----------
loadLayout();loadPipe();
setInterval(poll,50);
setInterval(eyePoll,100);
setInterval(framePoll,50);
setInterval(drawOverlay,100);
setInterval(drawFlyView,100);
setInterval(drawPipe,500);
setInterval(drawTop,400);
// video is driven by sim time (currentTime=S.t); no free-running here
// replay: map shows per-class aggregates (no per-neuron data in the CSV)
const AGG=['photo','lam','t4t5','all','all','all','all','dntct','all','all','all','all','motor','all','all'];
function drawMapAgg(){if(!GROUPS||!S)return;const x=mapCtx;x.fillStyle='#0d1015';x.fillRect(0,0,408,420);
 GROUPS.forEach((g,gi)=>{if(g.w===0)return;const v=Math.min(1,(S[AGG[gi]]||0)*6);
  x.fillStyle=`rgb(${(17+238*v)|0},${(17+150*v)|0},${(17+60*v)|0})`;
  x.fillRect(g.x,g.y,g.w,g.h);x.strokeStyle='#232b36';x.strokeRect(g.x+.5,g.y+.5,g.w,g.h);
  x.fillStyle='#8a97a8';x.font='9px Segoe UI';x.fillText(g.name,g.x,g.y+g.h+9);});
 $('mapnote').textContent=' REPLAY: карта — агрегаты по классам (по-нейронной активности в CSV нет).';}
</script></body></html>)HTML";
