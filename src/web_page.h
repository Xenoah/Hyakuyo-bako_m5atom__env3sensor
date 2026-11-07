#pragma once

#include <Arduino.h>

// ブラウザ向けの画面。外部 CDN を使わず、このファイルで完結させる。
// 値のカードは /api/status の metrics から作るので、センサーを足しても画面の修正はいらない。
const char STYLE_CSS[] PROGMEM = R"CSS(:root{color-scheme:dark;--bg:#0d0f14;--card:#171a22;--line:#262b36;--text:#e8eaf0;--sub:#8a91a3;
--accent:#00e5ff;--ok:#39ff14;--ng:#ff4d4d}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.5 system-ui,-apple-system,"Segoe UI","Hiragino Sans","Noto Sans JP",sans-serif}
main{max-width:980px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:16px}
h1{font-size:20px;margin:0;letter-spacing:.04em}
h2{font-size:13px;font-weight:600;color:var(--sub);margin:0 0 10px;letter-spacing:.06em}
h3{font-size:14px;margin:0;font-weight:600}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:13px;color:var(--sub);background:var(--card);border:1px solid var(--line);border-radius:999px;padding:4px 10px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ng)}
.dot.on{background:var(--ok);box-shadow:0 0 8px var(--ok)}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;min-width:0}
.metric .value{font-size:40px;font-weight:700;font-variant-numeric:tabular-nums;line-height:1.1}
.metric .unit{font-size:16px;color:var(--sub);margin-left:4px;font-weight:500}
.metric .bin{font-family:ui-monospace,Consolas,monospace;font-size:13px;color:var(--sub);margin:4px 0 8px;word-break:break-all}
.metric .range{font-size:12px;color:var(--sub);display:flex;justify-content:space-between}
.metric .key{font-family:ui-monospace,Consolas,monospace;font-size:11px;color:#5d6475;float:right;font-weight:400}
canvas{width:100%;height:120px;display:block;margin-top:8px}
.panel{display:flex;gap:20px;align-items:center;flex-wrap:wrap}
.matrix{display:grid;grid-template-columns:repeat(5,22px);gap:6px;padding:12px;background:#000;border-radius:10px;border:1px solid var(--line)}
.matrix i{width:22px;height:22px;border-radius:50%;background:#111}
.controls{flex:1;min-width:220px;display:flex;flex-direction:column;gap:12px}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:10px;overflow:hidden}
.seg button{border:0;border-radius:0}
button{background:#222735;color:var(--text);border:1px solid var(--line);border-radius:10px;padding:8px 14px;font:inherit;cursor:pointer}
button:hover{background:#2b3142}
button.active{background:#e8eaf0;color:#0d0f14}
button:disabled{opacity:.5;cursor:default}
button.small{padding:4px 10px;font-size:13px}
input[type=range]{width:100%;accent-color:var(--accent)}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.between{display:flex;justify-content:space-between;align-items:center;gap:8px}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0;font-size:14px}
dt{color:var(--sub)}dd{margin:0;font-variant-numeric:tabular-nums;word-break:break-all}
.ok{color:var(--ok)}.ng{color:var(--ng)}.sub{color:var(--sub)}
a{color:var(--accent)}
.section{margin-top:12px}
.target{border-top:1px solid var(--line);padding-top:10px;margin-top:10px}
.banner{background:#2a2410;border:1px solid #6b5a1a;color:#ffd666;border-radius:12px;padding:12px 14px;margin-bottom:12px}
form{display:flex;flex-direction:column;gap:14px}
label.field{display:flex;flex-direction:column;gap:4px;font-size:13px;color:var(--sub)}
label.check{display:flex;gap:8px;align-items:center;font-size:13px;color:var(--sub)}
input[type=text],input[type=password],input[type=url],input[type=number]{background:#0d0f14;color:var(--text);border:1px solid var(--line);border-radius:8px;padding:9px 10px;font:inherit;width:100%}
input:focus{outline:2px solid #00e5ff66;border-color:var(--accent)}
.hint{font-size:12px;color:var(--sub)}
.nets{display:flex;flex-wrap:wrap;gap:6px}
.msg{min-height:1.5em}
.gas{display:flex;flex-direction:column;gap:10px;border:1px solid var(--line);border-radius:12px;padding:12px}
button.primary{background:var(--accent);color:#0d0f14;border-color:var(--accent);font-weight:600}
button.danger{border-color:#6b2a2a;color:#ff8080}
)CSS";

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>百葉箱</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<main>
<header>
  <h1>百葉箱 <span class="sub" id="sensorNames"></span></h1>
  <div class="row"><span class="pill"><span id="dot" class="dot"></span><span id="conn">接続中…</span></span><a href="/settings" class="pill">設定</a></div>
</header>

<div class="banner" id="apbanner" hidden>セットアップモードです。<a href="/settings">設定</a>から Wi-Fi を設定してください。</div>

<div class="grid" id="metrics"></div>

<div class="grid section">
  <div class="card">
    <h2>マトリクス</h2>
    <div class="panel">
      <div class="matrix" id="matrix"></div>
      <div class="controls">
        <div class="seg"><button id="m-decimal">10進数</button><button id="m-binary">2進数</button></div>
        <label>明るさ <span id="bl"></span><input type="range" id="bright" min="1" max="80"></label>
        <div class="row"><button id="showip">IP を流す</button></div>
      </div>
    </div>
  </div>
  <div class="card">
    <h2>スプレッドシート送信</h2>
    <dl>
      <dt>送信間隔</dt><dd id="u-interval">--</dd>
      <dt>次回送信</dt><dd id="u-next">--</dd>
    </dl>
    <div id="targets"></div>
    <div class="row section"><button id="upload">今すぐ送信</button></div>
  </div>
  <div class="card">
    <h2>デバイス</h2>
    <dl>
      <dt>ホスト</dt><dd id="d-host">--</dd>
      <dt>IP</dt><dd id="d-ip">--</dd>
      <dt>Wi-Fi</dt><dd id="d-wifi">--</dd>
      <dt>センサー</dt><dd id="d-sensor">--</dd>
      <dt>時刻</dt><dd id="d-time">--</dd>
      <dt>稼働時間</dt><dd id="d-up">--</dd>
      <dt>空きメモリ</dt><dd id="d-heap">--</dd>
    </dl>
    <div class="row section"><a href="/history.csv">履歴 CSV をダウンロード</a></div>
  </div>
</div>
</main>

<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const post=(path)=>fetch(path,{method:'POST'}).then(r=>r.json());
const fmt=(v,d)=>v==null?'--':v.toFixed(d);
const bin=v=>(v<0?'-':'')+Math.abs(Math.round(v)).toString(2);
const dur=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+'日 ':'')+h+'時間 '+m+'分'};
const hhmm=ms=>new Date(ms).toLocaleTimeString('ja-JP',{hour:'2-digit',minute:'2-digit'});
const dtm=ms=>new Date(ms).toLocaleString('ja-JP',{month:'numeric',day:'numeric',hour:'2-digit',minute:'2-digit',second:'2-digit'});
// デバイスの「起動からの秒数」をブラウザ側の時刻に直す
const upToMs=(s,st)=>st.timeSynced?(st.epoch-(st.uptime-s))*1000:Date.now()-(st.uptime-s)*1000;

let metrics=[],hist=null;

const cells=[];
for(let i=0;i<25;i++){const c=document.createElement('i');$('matrix').appendChild(c);cells.push(c)}

// metrics の並びが変わったときだけカードを作り直す
function buildCards(list){
  const sig=list.map(m=>m.key).join();
  if(sig===metrics.map(m=>m.key).join())return;
  $('metrics').innerHTML=list.map(m=>
    '<div class="card metric" id="card-'+esc(m.key)+'"><h2>'+esc(m.label)+'<span class="key">'+esc(m.key)+'</span></h2>'+
    '<div><span class="value" style="color:'+m.color+';text-shadow:0 0 18px '+m.color+'55">--</span><span class="unit">'+esc(m.unit)+'</span></div>'+
    '<div class="bin">&nbsp;</div><div class="range"></div><canvas></canvas></div>').join('');
  metrics=list;drawHistory();
}
const card=key=>$('card-'+key);

async function loadStatus(){
  try{
    const s=await fetch('/api/status').then(r=>r.json());
    $('dot').classList.add('on');$('conn').textContent=s.apMode?'セットアップ ('+s.apSsid+')':s.ip;
    $('apbanner').hidden=!s.apMode;
    $('sensorNames').textContent=s.sensors.map(x=>x.name).join(' / ');
    buildCards(s.metrics);
    for(const m of s.metrics){
      const c=card(m.key);
      c.querySelector('.value').textContent=fmt(m.value,m.decimals);
      c.querySelector('.bin').textContent=m.value==null?' ':'0b'+bin(m.value);
    }
    $('m-decimal').classList.toggle('active',s.mode==='decimal');
    $('m-binary').classList.toggle('active',s.mode==='binary');
    if(document.activeElement!==$('bright')){$('bright').max=s.maxBrightness;$('bright').value=s.brightness}
    $('bl').textContent=s.brightness;

    const u=s.upload;
    $('u-interval').textContent=Math.round(u.intervalSec/60)+'分';
    const any=u.targets.some(t=>t.configured);
    $('u-next').textContent=any?Math.floor(u.nextInSec/60)+'分 '+(u.nextInSec%60)+'秒後':'--';
    $('targets').innerHTML=u.targets.map((t,i)=>{
      let state;
      if(!t.configured)state='<span class="ng">URL 未設定</span>';
      else if(t.lastAttempt===0)state='待機中';
      else state=t.lastAttempt===t.lastSuccess?'<span class="ok">成功</span>':'<span class="ng">失敗 ('+(t.lastCode>0?'HTTP '+t.lastCode:'通信エラー')+')</span>';
      return '<div class="target"><div class="between"><h3>'+esc(t.name||'送信先 '+(i+1))+'</h3>'+state+'</div>'+
        '<dl class="section"><dt>最終成功</dt><dd>'+(t.lastSuccess?dtm(upToMs(t.lastSuccess,s)):'--')+'</dd>'+
        '<dt>成功 / 失敗</dt><dd>'+t.successCount+' / '+t.failureCount+'</dd>'+
        '<dt>最新の応答</dt><dd>'+esc(t.message)+'</dd></dl></div>';
    }).join('');
    $('upload').disabled=!any;

    $('d-host').textContent=s.device+'.local';
    $('d-ip').textContent=s.ip+'  ('+s.mac+')';
    $('d-wifi').textContent=s.apMode?'未接続 (AP: '+s.apSsid+')':s.ssid+'  '+s.rssi+' dBm';
    $('d-sensor').innerHTML=s.sensors.map(x=>esc(x.name)+' '+(x.ok?'<span class="ok">正常</span>':'<span class="ng">未検出 / エラー</span>')).join('<br>');
    $('d-time').textContent=s.timeSynced?dtm(s.epoch*1000)+' (NTP)':'NTP 未同期';
    $('d-up').textContent=dur(s.uptime);
    $('d-heap').textContent=(s.freeHeap/1024).toFixed(1)+' KB';
  }catch(e){$('dot').classList.remove('on');$('conn').textContent='切断'}
}

async function loadMatrix(){
  if(document.hidden)return;
  try{
    const hex=await fetch('/api/matrix').then(r=>r.text());
    for(let i=0;i<25;i++){
      const c=hex.substr(i*6,6),lit=c!=='000000';
      cells[i].style.background=lit?'#'+c:'#111';
      cells[i].style.boxShadow=lit?'0 0 10px #'+c:'none';
    }
  }catch(e){}
}

function drawChart(canvas,pts,color,dec,t0,t1,gapMs){
  const dpr=window.devicePixelRatio||1,w=canvas.clientWidth,h=canvas.clientHeight;
  canvas.width=w*dpr;canvas.height=h*dpr;
  const g=canvas.getContext('2d');g.scale(dpr,dpr);g.clearRect(0,0,w,h);
  g.font='11px system-ui,sans-serif';g.fillStyle='#8a91a3';
  if(pts.length<2){g.fillText('データ収集中…',8,h/2);return}
  let lo=Math.min(...pts.map(p=>p.v)),hi=Math.max(...pts.map(p=>p.v));
  const pad=Math.max((hi-lo)*0.15,dec?0.2:1);lo-=pad;hi+=pad;
  const L=38,R=w-4,T=6,B=h-16;
  const x=t=>L+(t-t0)/(t1-t0)*(R-L),y=v=>B-(v-lo)/(hi-lo)*(B-T);
  g.strokeStyle='#262b36';g.lineWidth=1;
  for(let i=0;i<=3;i++){const v=lo+(hi-lo)*i/3,yy=y(v);g.beginPath();g.moveTo(L,yy);g.lineTo(R,yy);g.stroke();g.fillText(v.toFixed(dec),2,yy+4)}
  for(let i=0;i<=4;i++){const t=t0+(t1-t0)*i/4;g.fillText(hhmm(t),Math.min(x(t)-14,R-30),h-2)}
  g.strokeStyle=color;g.lineWidth=1.8;g.shadowColor=color;g.shadowBlur=6;g.beginPath();
  let prev=null;
  for(const p of pts){
    if(prev&&p.t-prev.t<=gapMs)g.lineTo(x(p.t),y(p.v));else g.moveTo(x(p.t),y(p.v));
    prev=p;
  }
  g.stroke();
}

async function loadHistory(){
  try{hist=await fetch('/api/history').then(r=>r.json());drawHistory()}catch(e){}
}
function drawHistory(){
  if(!hist||!metrics.length)return;
  const t1=Date.now(),t0=t1-24*3600*1000;
  const st={timeSynced:hist.epoch>1700000000,epoch:hist.epoch,uptime:hist.uptime};
  const rows=hist.rows.map(r=>({t:upToMs(r[0],st),v:r.slice(1)})).filter(r=>r.t>=t0);
  const gap=hist.intervalSec*3000;
  for(const m of metrics){
    const col=hist.keys.indexOf(m.key),c=card(m.key);
    if(col<0||!c)continue;
    const pts=rows.filter(r=>r.v[col]!=null).map(r=>({t:r.t,v:r.v[col]}));
    drawChart(c.querySelector('canvas'),pts,m.color,m.decimals,t0,t1,gap);
    if(pts.length){const vs=pts.map(p=>p.v);c.querySelector('.range').innerHTML='<span>24h 最低 '+Math.min(...vs).toFixed(m.decimals)+'</span><span>最高 '+Math.max(...vs).toFixed(m.decimals)+'</span>'}
  }
}

$('m-decimal').onclick=()=>post('/api/mode?value=decimal').then(loadStatus);
$('m-binary').onclick=()=>post('/api/mode?value=binary').then(loadStatus);
$('bright').oninput=e=>$('bl').textContent=e.target.value;
$('bright').onchange=e=>post('/api/brightness?value='+e.target.value).then(loadStatus);
$('showip').onclick=()=>post('/api/showip');
$('upload').onclick=async e=>{e.target.disabled=true;await post('/api/upload');setTimeout(()=>{e.target.disabled=false;loadStatus()},10000)};
window.addEventListener('resize',drawHistory);

loadStatus().then(loadHistory);loadMatrix();
setInterval(loadStatus,5000);setInterval(loadHistory,60000);setInterval(loadMatrix,300);
</script>
</body>
</html>
)HTML";

const char SETTINGS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>百葉箱 設定</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<main style="max-width:560px">
<header>
  <h1>設定</h1>
  <a href="/" class="pill">← ダッシュボード</a>
</header>

<div class="banner" id="apbanner" hidden></div>

<form id="form" class="card" autocomplete="off">
  <h2>Wi-Fi</h2>
  <label class="field">SSID
    <input type="text" name="wifiSsid" id="wifiSsid" maxlength="32" placeholder="2.4GHz のネットワーク">
  </label>
  <div class="row"><button type="button" id="scan">周辺のネットワークを探す</button><span class="hint" id="scanmsg"></span></div>
  <div class="nets" id="nets"></div>
  <label class="field">パスワード
    <input type="password" name="wifiPassword" id="wifiPassword" maxlength="63">
    <span class="hint">SSID を変えたときは入力してください (オープンなネットワークなら空欄)。</span>
  </label>

  <h2 class="section">Google スプレッドシート (GAS)</h2>
  <span class="hint">登録した全ての送信先に同じ値を送ります。最大 10 件。</span>
  <div id="gasList" class="controls"></div>
  <div class="row"><button type="button" id="addGas">送信先を追加</button><span class="hint" id="gasCount"></span></div>
  <label class="field">送信間隔 (分)
    <input type="number" name="uploadIntervalMin" id="uploadIntervalMin" min="1" max="60" required>
  </label>

  <h2 class="section">管理パスワード</h2>
  <label class="field">この設定画面と OTA 書き込みのパスワード (ユーザー名 admin)
    <input type="password" name="adminPassword" id="adminPassword" maxlength="64">
  </label>
  <label class="check" id="clearwrap" hidden><input type="checkbox" name="clearAdminPassword"> パスワードを解除する</label>

  <div class="row section"><button type="submit" class="primary">保存</button></div>
  <div class="msg" id="msg"></div>
</form>

<div class="card section">
  <h2>初期化</h2>
  <p class="hint">保存した設定を消して、書き込み時の初期値 (secrets.h) に戻し再起動します。</p>
  <button id="reset" class="danger">設定を初期化</button>
</div>
</main>

<template id="gasTemplate">
  <div class="gas">
    <div class="between"><h3 class="gasTitle"></h3><button type="button" class="small danger gasRemove">削除</button></div>
    <label class="field">名前 (任意)
      <input type="text" class="gasName" maxlength="32" placeholder="例: 自宅のシート">
    </label>
    <label class="field">ウェブアプリの URL
      <input type="url" class="gasUrl" maxlength="256" placeholder="https://script.google.com/macros/s/.../exec">
    </label>
    <label class="field">合言葉 (GAS のスクリプトプロパティ TOKEN)
      <input type="text" class="gasToken" maxlength="64">
    </label>
  </div>
</template>

<script>
const $=id=>document.getElementById(id);
const esc=s=>s.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let current=null,maxGas=10;

function say(text,ok){$('msg').innerHTML='<span class="'+(ok?'ok':'ng')+'">'+esc(text)+'</span>'}

function renumberGas(){
  const items=[...$('gasList').children];
  items.forEach((el,i)=>{
    el.querySelector('.gasTitle').textContent='送信先 '+(i+1);
    el.querySelector('.gasRemove').hidden=items.length<=1;
  });
  $('addGas').disabled=items.length>=maxGas;
  $('gasCount').textContent=items.length+' / '+maxGas+' 件';
}
function addGas(t){
  const el=$('gasTemplate').content.firstElementChild.cloneNode(true);
  el.querySelector('.gasName').value=t.name||'';
  el.querySelector('.gasUrl').value=t.url||'';
  el.querySelector('.gasToken').value=t.token||'';
  el.querySelector('.gasRemove').onclick=()=>{el.remove();renumberGas()};
  $('gasList').appendChild(el);
  renumberGas();
}

async function load(){
  const s=await fetch('/api/settings').then(r=>r.json());
  current=s;maxGas=s.maxGasTargets;
  $('wifiSsid').value=s.wifiSsid;
  $('wifiPassword').placeholder=s.hasWifiPassword?'設定済み (変更しない場合は空欄)':'';
  $('gasList').innerHTML='';
  (s.gasTargets.length?s.gasTargets:[{}]).forEach(addGas);
  $('uploadIntervalMin').value=s.uploadIntervalMin;
  $('adminPassword').placeholder=s.hasAdminPassword?'設定済み (変更しない場合は空欄)':'未設定 (誰でも設定を変更できます)';
  $('clearwrap').hidden=!s.hasAdminPassword;
  if(s.apMode){
    $('apbanner').hidden=false;
    $('apbanner').textContent='セットアップモード ('+s.apSsid+') です。Wi-Fi を設定して保存すると再起動し、そのネットワークにつながります。';
  }
}

$('addGas').onclick=()=>{addGas({});$('gasList').lastElementChild.querySelector('.gasUrl').focus()};

$('scan').onclick=async()=>{
  $('scan').disabled=true;$('scanmsg').textContent='検索中…';$('nets').innerHTML='';
  try{
    const list=(await fetch('/api/scan').then(r=>r.json())).sort((a,b)=>b.rssi-a.rssi);
    $('scanmsg').textContent=list.length?'':'見つかりませんでした';
    for(const n of list){
      const b=document.createElement('button');b.type='button';b.className='small';
      b.textContent=(n.secure?'🔒 ':'')+n.ssid+'  '+n.rssi+'dBm';
      b.onclick=()=>{$('wifiSsid').value=n.ssid;$('wifiPassword').focus()};
      $('nets').appendChild(b);
    }
  }catch(e){$('scanmsg').textContent='検索に失敗しました'}
  $('scan').disabled=false;
};

$('form').onsubmit=async e=>{
  e.preventDefault();
  const body=new URLSearchParams(new FormData($('form')));
  const items=[...$('gasList').children];
  body.set('gasCount',items.length);
  items.forEach((el,i)=>{
    body.set('gasName'+i,el.querySelector('.gasName').value);
    body.set('gasUrl'+i,el.querySelector('.gasUrl').value);
    body.set('gasToken'+i,el.querySelector('.gasToken').value);
  });
  const ssid=$('wifiSsid').value.trim();
  try{
    const r=await fetch('/api/settings',{method:'POST',body}).then(r=>r.json());
    if(!r.ok){say(r.error||'保存できませんでした',false);return}
    if(r.restart){
      say(ssid?'保存しました。再起動して「'+ssid+'」に接続します。PC/スマホを同じネットワークに戻し、30秒ほどしてから http://'+current.device+'.local/ を開いてください (開けないときはボタン長押しでマトリクスに IP が流れます)。':'保存しました。再起動します。',true);
    }else{
      say('保存しました。',true);
      $('wifiPassword').value='';$('adminPassword').value='';load();
    }
  }catch(err){say('保存できませんでした',false)}
};

$('reset').onclick=async()=>{
  if(!confirm('保存した設定を消して再起動します。よろしいですか？'))return;
  await fetch('/api/reset',{method:'POST'});
  say('初期化しました。再起動します。',true);
};

load();
</script>
</body>
</html>
)HTML";
