#include "webui.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app.h"
#include "app_config.h"
#include "bme280.h"
#include "camera_face.h"
#include "gemini.h"
#include "power.h"
#include "storage.h"
#include "util.h"
#include "wifi_manager.h"

static WebServer server(80);
static bool s_active = false;

static const char kIndexHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="zh-Hant"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TTGO AI Camera</title>
<style>
:root{--bg:#0b0e16;--panel:#161b28;--line:#252d40;--txt:#e8eef8;--dim:#8894ac;--acc:#00c8ff;--ok:#00dc78;--bad:#ff4848}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,-apple-system,"Noto Sans TC",sans-serif;font-size:15px}
header{padding:14px 16px;background:linear-gradient(90deg,#0f1a2a,#131a2b);border-bottom:1px solid var(--line)}
h1{margin:0;font-size:18px;letter-spacing:1px}
h1 span{color:var(--acc)}
#stat{margin-top:6px;color:var(--dim);font-size:13px}
nav{display:flex;gap:6px;overflow-x:auto;padding:10px 12px;background:var(--panel);border-bottom:1px solid var(--line)}
nav button{background:transparent;border:1px solid var(--line);color:var(--dim);padding:7px 14px;border-radius:999px;font-size:14px;white-space:nowrap}
nav button.on{background:var(--acc);border-color:var(--acc);color:#04121c;font-weight:700}
main{padding:14px;max-width:860px;margin:0 auto}
section{display:none}
section.on{display:block}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
.card h2{margin:0 0 10px;font-size:15px;color:var(--acc);font-weight:600}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
input,textarea,select{background:#0d1220;border:1px solid var(--line);color:var(--txt);border-radius:9px;padding:9px 11px;font-size:14px;font-family:inherit;flex:1;min-width:0}
textarea{width:100%;min-height:90px;resize:vertical}
button.act{background:#1d2740;border:1px solid #2c3a58;color:var(--txt);border-radius:9px;padding:9px 13px;font-size:14px;cursor:pointer}
button.act:hover{background:#243252}
button.primary{background:var(--acc);border-color:var(--acc);color:#04121c;font-weight:700}
button.danger{background:#3a1420;border-color:#5c2030;color:#ff9aa8}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.kv{background:#0d1220;border-radius:10px;padding:10px 12px}
.kv b{display:block;color:var(--dim);font-size:12px;font-weight:500;margin-bottom:4px}
.kv span{font-size:17px}
.chat{height:320px;overflow-y:auto;background:#0d1220;border-radius:10px;padding:10px;margin-bottom:10px}
.msg{margin:8px 0;max-width:86%;padding:9px 12px;border-radius:12px;line-height:1.5;white-space:pre-wrap;word-break:break-word}
.me{margin-left:auto;background:#1c3d5a}
.ai{background:#1d2436;border:1px solid #2b3550}
.face{display:flex;justify-content:space-between;align-items:center;background:#0d1220;border-radius:9px;padding:9px 12px;margin-bottom:7px}
.tag{font-size:12px;color:var(--dim)}
canvas{width:100%;height:150px;background:#0d1220;border-radius:10px}
#toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%);background:#1d2740;border:1px solid #2c3a58;padding:10px 18px;border-radius:999px;font-size:14px;opacity:0;transition:.25s;pointer-events:none}
#toast.on{opacity:1}
</style></head><body>
<header><h1>TTGO <span>AI Camera</span></h1><div id="stat">載入中…</div></header>
<nav>
<button class="on" data-tab="dash">總覽</button>
<button data-tab="chat">AI 聊天</button>
<button data-tab="env">環境</button>
<button data-tab="faces">人臉</button>
<button data-tab="wifi">WiFi</button>
<button data-tab="cfg">設定</button>
</nav>
<main>
<section id="tab-dash" class="on">
 <div class="card"><h2>裝置狀態</h2><div class="grid" id="dashGrid"></div></div>
 <div class="card"><h2>遠端操作</h2><div class="row" id="actions"></div></div>
 <div class="card"><h2>即時畫面</h2><img id="snap" style="width:100%;border-radius:10px;background:#0d1220" alt="">
  <div class="row" style="margin-top:10px"><button class="act" onclick="snapNow()">更新畫面</button></div></div>
</section>

<section id="tab-chat">
 <div class="card"><h2>與小 T 對話</h2>
  <div class="chat" id="chatBox"></div>
  <textarea id="chatIn" placeholder="輸入訊息，Enter 送出（Shift+Enter 換行）"></textarea>
  <div class="row" style="margin-top:8px"><button class="act primary" onclick="sendChat()">送出</button>
  <button class="act danger" onclick="clearChat()">清除對話</button></div>
 </div>
</section>

<section id="tab-env">
 <div class="card"><h2>BME280 環境參數</h2>
  <div class="grid" id="envGrid"></div>
  <canvas id="chart" width="600" height="220" style="margin-top:12px"></canvas>
  <div class="tag" style="margin-top:6px">每 2 秒更新一次；紅=溫度 青=濕度 綠=氣壓</div>
 </div>
</section>

<section id="tab-faces">
 <div class="card"><h2>已註冊人臉</h2><div id="faceList"></div>
  <div class="row" style="margin-top:10px">
   <input id="faceName" placeholder="名稱，例如：小明">
   <button class="act primary" onclick="addFace()">加入人臉</button>
   <button class="act" onclick="exportFaces()">匯出 CSV</button>
  </div>
  <div class="tag" style="margin-top:8px">加入人臉時請面向鏡頭約 3 秒。</div>
 </div>
</section>

<section id="tab-wifi">
 <div class="card"><h2>WiFi 設定</h2>
  <div class="row"><input id="wSsid" placeholder="SSID"><input id="wPass" type="password" placeholder="密碼"></div>
  <div class="row" style="margin-top:8px"><button class="act" onclick="scanWifi()">掃描</button>
  <button class="act primary" onclick="connectWifi()">連線</button>
  <button class="act danger" onclick="clearWifi()">清除設定</button></div>
  <div id="wifiList" style="margin-top:10px"></div>
 </div>
</section>

<section id="tab-cfg">
 <div class="card"><h2>config.json</h2>
  <div class="tag" style="margin-bottom:8px">儲存後會寫回 SD 卡；api_key 顯示為 *** 表示不變更。</div>
  <textarea id="cfgBox" style="min-height:340px;font-family:ui-monospace,Consolas,monospace;font-size:13px"></textarea>
  <div class="row" style="margin-top:8px"><button class="act primary" onclick="saveCfg()">儲存</button>
  <button class="act" onclick="loadCfg()">重新載入</button></div>
 </div>
</section>
</main>
<div id="toast"></div>
<script>
const $=s=>document.querySelector(s);
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(t._t);t._t=setTimeout(()=>t.classList.remove('on'),2200);}
async function api(p,o){const r=await fetch(p,o);if(!r.ok)throw new Error(await r.text());const ct=r.headers.get('content-type')||'';return ct.includes('json')?r.json():r.text();}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
 document.querySelectorAll('nav button').forEach(x=>x.classList.remove('on'));b.classList.add('on');
 document.querySelectorAll('section').forEach(x=>x.classList.remove('on'));$('#tab-'+b.dataset.tab).classList.add('on');
 if(b.dataset.tab==='cfg')loadCfg(); if(b.dataset.tab==='faces')loadFaces(); if(b.dataset.tab==='chat')loadHistory();
});

const ACTS=[['系統狀態',11],['顯示切換',12],['環境圖表',40],['人臉辨識',53],['人臉註冊',50],['匯出人臉 CSV',52],['WiFi 掃描',20],['重新開機',61]];
$('#actions').innerHTML=ACTS.map(a=>`<button class="act" onclick="doAct(${a[1]})">${a[0]}</button>`).join('');
async function doAct(c){try{await api('/api/action?code='+c,{method:'POST'});toast('已送出指令');refresh();}catch(e){toast('失敗：'+e.message);}}

async function refresh(){
 try{
  const s=await api('/api/status');
  $('#stat').textContent=`${s.wifi.ssid||'未設定'} · ${s.wifi.ip||'無 IP'} · ${s.wifi.state} · 人臉 ${s.faces} · 可用記憶體 ${s.sys.heap} KB`;
  const rows=[['WiFi',s.wifi.state],['IP',s.wifi.ip||'-'],['訊號',s.wifi.rssi+' dBm'],['人臉數',s.faces],['BME280',s.bme?'已連接':'未偵測'],['電池',s.power],['執行時間',s.sys.uptime],['PSRAM',s.sys.psram],['SD 卡',s.sys.sd],['AI 金鑰',s.ai?'已設定':'未設定']];
  $('#dashGrid').innerHTML=rows.map(r=>`<div class="kv"><b>${r[0]}</b><span>${r[1]}</span></div>`).join('');
 }catch(e){$('#stat').textContent='離線：'+e.message;}
}

async function snapNow(){
 try{const r=await fetch('/api/snapshot');const b=await r.blob();$('#snap').src=URL.createObjectURL(b);}catch(e){toast('取得畫面失敗');}
}

let hist=[];
async function loadHistory(){try{const h=await api('/api/history');hist=h.turns||[];renderChat();}catch(e){}}
function renderChat(){$('#chatBox').innerHTML=hist.map(t=>
 `<div class="msg me">${esc(t.user)}</div><div class="msg ai">${esc(t.reply)}</div>`).join('');
 $('#chatBox').scrollTop=$('#chatBox').scrollHeight;}
function esc(s){return (s||'').replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));}
async function sendChat(){
 const box=$('#chatIn');const t=box.value.trim();if(!t)return;box.value='';
 hist.push({user:t,reply:'…'});renderChat();
 try{const r=await api('/api/chat',{method:'POST',headers:{'Content-Type':'text/plain;charset=utf-8'},body:t});
  hist[hist.length-1].reply=r.reply||r.error||'（沒有回覆）';}
 catch(e){hist[hist.length-1].reply='錯誤：'+e.message;}
 renderChat();
}
async function clearChat(){await api('/api/history',{method:'DELETE'});hist=[];renderChat();toast('已清除對話');}

async function envTick(){
 try{
  const d=await api('/api/sensors');
  $('#envGrid').innerHTML=[['溫度',d.ok?d.t.toFixed(1)+' °C':'—'],['濕度',d.ok?d.h.toFixed(1)+' %':'—'],['氣壓',d.ok?d.p.toFixed(1)+' hPa':'—']]
   .map(r=>`<div class="kv"><b>${r[0]}</b><span>${r[1]}</span></div>`).join('');
  pushSeries(d);drawChart();
 }catch(e){}
}
let series={t:[],h:[],p:[]};
function pushSeries(d){if(!d.ok)return;['t','h','p'].forEach(k=>{series[k].push(d[k]);if(series[k].length>120)series[k].shift();});}
function drawChart(){
 const c=$('#chart'),g=c.getContext('2d');const W=c.width,H=c.height;
 g.clearRect(0,0,W,H);
 const bands=[['t',0,50,'#ff6b6b'],['h',0,100,'#3fd8e8'],['p',950,1050,'#4ade80']];
 bands.forEach((b,bi)=>{
  const y0=bi*H/3+8,y1=(bi+1)*H/3-8;const arr=series[b[0]];
  g.strokeStyle='#222b3d';g.beginPath();g.moveTo(0,y1);g.lineTo(W,y1);g.stroke();
  if(arr.length<2)return;
  g.strokeStyle=b[3];g.lineWidth=2;g.beginPath();
  arr.forEach((v,i)=>{
   const x=i*(W-10)/(arr.length-1)+5;
   let t=(v-b[1])/(b[2]-b[1]);t=Math.max(0,Math.min(1,t));
   const y=y1-(y1-y0)*t;
   i?g.lineTo(x,y):g.moveTo(x,y);
  });g.stroke();
 });
}

async function loadFaces(){
 try{const d=await api('/api/faces');
  $('#faceList').innerHTML=d.faces.length?d.faces.map(n=>
   `<div class="face"><span>${esc(n)}</span><button class="act danger" onclick="delFace('${encodeURIComponent(n)}')">刪除</button></div>`).join('')
   :'<div class="tag">尚未註冊任何人臉</div>';
 }catch(e){toast('讀取失敗');}
}
async function addFace(){
 const n=$('#faceName').value.trim()||('ID'+(Date.now()%1000));
 try{await api('/api/faces/add?name='+encodeURIComponent(n),{method:'POST'});toast('已加入 '+n);$('#faceName').value='';loadFaces();}
 catch(e){toast('加入失敗：'+e.message);}
}
async function delFace(n){if(!confirm('確定刪除？'))return;
 try{await api('/api/faces/delete?name='+n,{method:'POST'});toast('已刪除');loadFaces();}catch(e){toast('刪除失敗');}}
async function exportFaces(){try{const r=await api('/api/faces/export',{method:'POST'});toast(r.msg||'已匯出');}catch(e){toast('匯出失敗');}}

async function scanWifi(){
 try{const d=await api('/api/wifi/scan');
  $('#wifiList').innerHTML=d.networks.map(n=>
   `<div class="face"><span>${esc(n.ssid)} ${n.secure?'🔒':''}</span>
    <span class="tag">${n.rssi} dBm <button class="act" onclick="$('#wSsid').value='${encodeURIComponent(n.ssid)}';$('#wSsid').value=decodeURIComponent($('#wSsid').value)">選用</button></span></div>`).join('');
 }catch(e){toast('掃描失敗');}
}
async function connectWifi(){
 const ssid=$('#wSsid').value.trim(),pass=$('#wPass').value;
 if(!ssid)return toast('請輸入 SSID');
 toast('連線中，請稍候…');
 try{const r=await api('/api/wifi?ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pass),{method:'POST'});
  toast(r.ok?('已連線 '+r.ip):('失敗：'+r.error));refresh();
 }catch(e){toast('連線失敗：'+e.message);}
}
async function clearWifi(){if(!confirm('清除已儲存的 WiFi 設定？'))return;
 await api('/api/wifi/clear',{method:'POST'});toast('已清除');refresh();}

async function loadCfg(){try{const d=await api('/api/config');$('#cfgBox').value=JSON.stringify(d,null,2);}catch(e){toast('讀取設定失敗');}}
async function saveCfg(){
 try{const r=await api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:$('#cfgBox').value});
  toast(r.ok?'設定已儲存':'儲存失敗：'+r.error);}catch(e){toast('儲存失敗：'+e.message);}
}

$('#chatIn').addEventListener('keydown',e=>{if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();sendChat();}});
refresh();setInterval(refresh,5000);setInterval(envTick,2000);envTick();loadHistory();
</script></body></html>)HTML";

// ---------------------------------------------------------------- 輔助

static bool checkAuth() {
  if (g_cfg.web.user.isEmpty()) return true;
  if (server.authenticate(g_cfg.web.user.c_str(), g_cfg.web.password.c_str())) return true;
  server.requestAuthentication();
  return false;
}

static void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

static String okJson() { return "{\"ok\":true}"; }

// ---------------------------------------------------------------- 處理函式

static void handleRoot() {
  if (!checkAuth()) return;
  server.send_P(200, "text/html; charset=utf-8", kIndexHtml);
}

static void handleStatus() {
  if (!checkAuth()) return;
  sendJson(200, appStatusJson());
}

static void handleSensors() {
  if (!checkAuth()) return;
  BmeReading r;
  bool ok = bmeRead(r);
  String s = "{\"ok\":";
  s += ok ? "true" : "false";
  if (ok) {
    s += ",\"t\":";
    s += String(r.temperature, 2);
    s += ",\"h\":";
    s += String(r.humidity, 2);
    s += ",\"p\":";
    s += String(r.pressure, 2);
  }
  s += "}";
  sendJson(200, s);
}

static void handleHistory() {
  if (!checkAuth()) return;
  if (server.method() == HTTP_DELETE) {
    appClearHistory();
    sendJson(200, okJson());
    return;
  }
  sendJson(200, appHistoryJson());
}

static void handleChat() {
  if (!checkAuth()) return;
  String text = server.arg("plain");
  text.trim();
  if (text.isEmpty()) {
    sendJson(400, "{\"ok\":false,\"error\":\"訊息為空\"}");
    return;
  }
  AiReply r;
  appAiRun(false, text, nullptr, 0, r, nullptr);
  if (r.ok) {
    appAddHistory(text, r.reply);
    String out = "{\"ok\":true,\"reply\":\"" + jsonEscape(r.reply) + "\"}";
    sendJson(200, out);
  } else {
    String out = "{\"ok\":false,\"error\":\"" + jsonEscape(r.error) + "\"}";
    sendJson(200, out);
  }
}

static void handleFaces() {
  if (!checkAuth()) return;
  String s = "{\"faces\":[";
  for (int i = 0; i < faceCount(); i++) {
    if (i) s += ",";
    s += "\"" + jsonEscape(faceNameAt(i)) + "\"";
  }
  s += "]}";
  sendJson(200, s);
}

static void handleFaceAdd() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    sendJson(409, "{\"ok\":false,\"error\":\"裝置忙碌中\"}");
    return;
  }
  String name = server.arg("name");
  if (name.isEmpty()) name = "ID" + String(faceCount() + 1);
  if (!camBegin()) {
    sendJson(500, "{\"ok\":false,\"error\":\"相機初始化失敗\"}");
    return;
  }
  g_appBusy = true;
  String err;
  bool ok = faceEnroll(name, nullptr, err);
  g_appBusy = false;
  if (ok) {
    sendJson(200, okJson());
  } else {
    sendJson(500, "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}");
  }
}

static void handleFaceDelete() {
  if (!checkAuth()) return;
  String name = server.arg("name");
  if (name.isEmpty()) {
    sendJson(400, "{\"ok\":false,\"error\":\"缺少 name\"}");
    return;
  }
  if (faceRemove(name)) {
    sendJson(200, okJson());
  } else {
    sendJson(404, "{\"ok\":false,\"error\":\"找不到此人臉\"}");
  }
}

static void handleFaceExport() {
  if (!checkAuth()) return;
  String msg;
  bool ok = faceExportCsv(msg);
  sendJson(ok ? 200 : 500,
           String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + jsonEscape(msg) + "\"}");
}

static void handleWifiScan() {
  if (!checkAuth()) return;
  String ssids[16];
  int rssi[16];
  bool secure[16];
  int n = wifiScan(ssids, rssi, secure, 16);
  String s = "{\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) s += ",";
    s += "{\"ssid\":\"" + jsonEscape(ssids[i]) + "\",\"rssi\":" + String(rssi[i]) +
         ",\"secure\":" + (secure[i] ? "true" : "false") + "}";
  }
  s += "]}";
  sendJson(200, s);
}

static void handleWifiConnect() {
  if (!checkAuth()) return;
  String ssid = server.arg("ssid");
  String pass = server.arg("password");
  if (ssid.isEmpty()) {
    sendJson(400, "{\"ok\":false,\"error\":\"缺少 SSID\"}");
    return;
  }
  String err;
  bool ok = wifiConnect(ssid, pass, g_cfg.wifi.connect_timeout_ms, &err);
  if (ok) {
    sendJson(200, "{\"ok\":true,\"ip\":\"" + wifiIP() + "\"}");
  } else {
    sendJson(200, "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}");
  }
}

static void handleWifiClear() {
  if (!checkAuth()) return;
  wifiClearCredentials();
  sendJson(200, okJson());
}

static void handleConfigGet() {
  if (!checkAuth()) return;
  sendJson(200, g_cfg.toJson(true));
}

static void handleConfigSet() {
  if (!checkAuth()) return;
  String body = server.arg("plain");
  String err;
  if (!g_cfg.fromJson(body, &err)) {
    sendJson(400, "{\"ok\":false,\"error\":\"" + jsonEscape(err) + "\"}");
    return;
  }
  bool ok = g_cfg.save();
  sendJson(200, String("{\"ok\":") + (ok ? "true" : "false") +
                   (ok ? "" : ",\"error\":\"寫入 SD 卡失敗\"") + "}");
}

static void handleSnapshot() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    server.send(409, "text/plain; charset=utf-8", "裝置忙碌中");
    return;
  }
  if (!camBegin()) {
    server.send(500, "text/plain; charset=utf-8", "相機初始化失敗");
    return;
  }
  uint8_t *jpg = nullptr;
  size_t len = 0;
  if (!camSnapshotJpeg(&jpg, &len) || !jpg) {
    server.send(500, "text/plain; charset=utf-8", "擷取失敗");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char *)jpg, len);
  free(jpg);
}

static void handleAction() {
  if (!checkAuth()) return;
  if (!appCanRunAction()) {
    sendJson(409, "{\"ok\":false,\"error\":\"裝置忙碌中\"}");
    return;
  }
  int code = server.arg("code").toInt();
  if (code <= 0) {
    sendJson(400, "{\"ok\":false,\"error\":\"缺少 code\"}");
    return;
  }
  appRunAction((uint16_t)code);
  sendJson(200, okJson());
}

static void handleNotFound() {
  server.send(404, "text/plain; charset=utf-8", "找不到頁面");
}

// ---------------------------------------------------------------- 公開 API

bool webuiActive() { return s_active; }

void webuiBegin() {
  if (!g_cfg.web.enabled) {
    logLine("WEB", "設定中已停用網頁介面");
    return;
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/sensors", HTTP_GET, handleSensors);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/history", HTTP_DELETE, handleHistory);
  server.on("/api/chat", HTTP_POST, handleChat);
  server.on("/api/faces", HTTP_GET, handleFaces);
  server.on("/api/faces/add", HTTP_POST, handleFaceAdd);
  server.on("/api/faces/delete", HTTP_POST, handleFaceDelete);
  server.on("/api/faces/export", HTTP_POST, handleFaceExport);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/clear", HTTP_POST, handleWifiClear);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigSet);
  server.on("/api/snapshot", HTTP_GET, handleSnapshot);
  server.on("/api/action", HTTP_POST, handleAction);
  server.onNotFound(handleNotFound);

  server.begin();
  s_active = true;
  logLine("WEB", "網頁介面已啟動");
}

void webuiHandle() {
  if (!s_active) return;
  server.handleClient();
}
