'use strict';
const $ = (s, r) => (r || document).querySelector(s);
const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const kb = n => {
  n = Number(n) || 0;
  return n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB'
       : n >= 1024 ? (n / 1024).toFixed(1) + ' KB' : n + ' B';
};

let toastTimer = 0;
function toast(msg, bad) {
  const t = $('#toast');
  t.textContent = msg;
  t.className = 'on' + (bad ? ' bad' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = ''; }, 2600);
}

async function api(path, opt) {
  const r = await fetch(path, opt);
  const ct = r.headers.get('content-type') || '';
  if (ct.indexOf('json') >= 0) return r.json();
  const txt = await r.text();
  if (!r.ok) throw new Error(txt || ('HTTP ' + r.status));
  return txt;
}

const timers = {};
function clearTimers() {
  Object.keys(timers).forEach(k => { clearInterval(timers[k]); delete timers[k]; });
}

/* ---------------- 側邊欄 ---------------- */
const loaders = {
  sys() { refreshStatus(); loadFs(); timers.sys = setInterval(refreshStatus, 4000); },
  sd() { loadSd(sdPath); },
  wifi() { refreshWifi(); },
  ai() { loadHistory(); loadAiConfig(); loadMic(); },
  env() { envTick(); timers.env = setInterval(envTick, 2000); },
  cam() { loadFaces(); camStart(); }
};

function go(view) {
  if (!loaders[view]) return;
  clearTimers();
  stopCam();
  $$('#nav button').forEach(b => b.classList.toggle('on', b.dataset.view === view));
  $$('.view').forEach(s => s.classList.toggle('on', s.id === 'view-' + view));
  document.body.classList.remove('nav-open');
  loaders[view]();
}

$$('#nav button').forEach(b => b.onclick = () => go(b.dataset.view));
$('#burger').onclick = () => document.body.classList.toggle('nav-open');
$('#btnMenu').onclick = async () => {
  try { await api('/api/action?code=2', { method: 'POST' }); toast('TFT 已回到主選單'); }
  catch (e) { toast('裝置忙碌中', true); }
};

$('#btnReboot').onclick = async () => {
  if (!confirm('確定要重新開機？')) return;
  toast('裝置重新開機中…');
  try { await api('/api/action?code=61', { method: 'POST' }); } catch (e) {}
};

/* ---------------- 系統狀態 ---------------- */
async function refreshStatus() {
  try {
    const s = await api('/api/status');
    $('#conn').textContent = s.wifi.ip || (s.wifi.apActive ? 'AP ' + s.wifi.ap : '未連線');
    $('#ipFoot').textContent = 'http://' + (s.wifi.ip || '192.168.4.1') + '/';
    const cards = [
      ['WiFi', s.wifi.state], ['IP 位址', s.wifi.ip || '—'],
      ['訊號', (s.wifi.rssi || 0) + ' dBm'], ['AP 熱點', s.wifi.apActive ? s.wifi.ap : '未開啟'],
      ['可用記憶體', s.sys.heap + ' KB'], ['PSRAM', s.sys.psram],
      ['SD 卡', s.sys.sd], ['人臉 ID', s.faces + ' 個'],
      ['BME280', s.bme ? '已連接' : '未偵測'], ['電池', s.power],
      ['執行時間', s.sys.uptime], ['AI 金鑰', s.ai ? '已設定' : '未設定']
    ];
    $('#statGrid').innerHTML = cards.map(c =>
      `<div class="kv"><b>${c[0]}</b><span>${esc(c[1])}</span></div>`).join('');
    $('#wifiInfo').innerHTML = [
      ['SSID', s.wifi.ssid || '（未設定）'], ['狀態', s.wifi.state],
      ['IP', s.wifi.ip || '—'], ['訊號', (s.wifi.rssi || 0) + ' dBm'],
      ['AP 熱點', s.wifi.apActive ? s.wifi.ap : '（未開啟）'], ['AI 模型', s.model]
    ].map(c => `<div class="row2"><b>${c[0]}</b><span>${esc(c[1])}</span></div>`).join('');
  } catch (e) {
    $('#conn').textContent = '離線';
  }
}

async function loadFs() {
  try {
    const d = await api('/api/fs');
    if (!d.mounted) {
      $('#spInfo').textContent = 'SPIFFS 未掛載（Partition Scheme 需為 Huge APP）';
      $('#spList').innerHTML = '';
      return;
    }
    const pct = d.total ? (d.used * 100 / d.total) : 0;
    $('#spBar').style.width = pct.toFixed(1) + '%';
    $('#spInfo').textContent = `已用 ${kb(d.used)} / ${kb(d.total)}（${pct.toFixed(1)}%）`;
    $('#spList').innerHTML = d.files.length
      ? d.files.map(f => `<div class="item"><span>${esc(f.name)}</span>
          <span class="muted">${kb(f.size)}</span></div>`).join('')
      : '<div class="muted">SPIFFS 是空的，請用 ESP32 Sketch Data Upload 上傳 data/</div>';
  } catch (e) {
    $('#spInfo').textContent = '讀取失敗：' + e.message;
  }
}

/* ---------------- SD 卡 ---------------- */
let sdPath = '/';
let edPath = '';

async function loadSd(path) {
  try {
    const d = await api('/api/sd/list?path=' + encodeURIComponent(path || '/'));
    if (d.ok === false) return toast(d.error, true);
    sdPath = d.path;
    $('#sdPath').textContent = d.path;
    $('#sdInfo').textContent = `已用 ${kb(d.used)} / ${kb(d.total)}` +
      (d.more ? '（僅顯示前 48 筆）' : '');
    const prefix = d.path === '/' ? '/' : d.path + '/';
    $('#sdList').innerHTML = d.entries.length ? d.entries.map(e => {
      const p = prefix + e.name;
      if (e.dir) {
        return `<div class="item"><button class="link" data-open="${esc(p)}">📁 ${esc(e.name)}</button>
          <span class="acts"><button class="mini danger" data-del="${esc(p)}">刪除</button></span></div>`;
      }
      const text = /\.(json|txt|csv|log|ini|md|html|htm|js|css|xml)$/i.test(e.name);
      return `<div class="item"><span>📄 ${esc(e.name)} <span class="muted">${kb(e.size)}</span></span>
        <span class="acts">
          ${text ? `<button class="mini" data-edit="${esc(p)}">編輯</button>` : ''}
          <button class="mini" data-get="${esc(p)}">下載</button>
          <button class="mini danger" data-del="${esc(p)}">刪除</button>
        </span></div>`;
    }).join('') : '<div class="muted">空資料夾</div>';
  } catch (e) {
    toast('讀取 SD 卡失敗：' + e.message, true);
  }
}

$('#sdList').onclick = async ev => {
  const b = ev.target.closest('button');
  if (!b) return;
  if (b.dataset.open) return loadSd(b.dataset.open);
  if (b.dataset.edit) return openEditor(b.dataset.edit);
  if (b.dataset.get) {
    location.href = '/api/sd/download?path=' + encodeURIComponent(b.dataset.get);
    return;
  }
  if (b.dataset.del) {
    if (!confirm('確定刪除？\n' + b.dataset.del)) return;
    const r = await api('/api/sd/delete?path=' + encodeURIComponent(b.dataset.del), { method: 'POST' });
    toast(r.ok ? '已刪除' : (r.error || '刪除失敗'), !r.ok);
    loadSd(sdPath);
  }
};

$('#sdUp').onclick = () => {
  if (sdPath === '/') return toast('已在根目錄');
  loadSd(sdPath.substring(0, sdPath.lastIndexOf('/')) || '/');
};
$('#sdReload').onclick = () => loadSd(sdPath);
$('#sdMkdir').onclick = async () => {
  const name = prompt('新資料夾名稱');
  if (!name) return;
  const p = (sdPath === '/' ? '/' : sdPath + '/') + name;
  const r = await api('/api/sd/mkdir?path=' + encodeURIComponent(p), { method: 'POST' });
  toast(r.ok ? '已建立 ' + name : (r.error || '建立失敗'), !r.ok);
  loadSd(sdPath);
};

/* 檔案編輯 */
async function openEditor(path) {
  try {
    const r = await fetch('/api/sd/read?path=' + encodeURIComponent(path));
    const txt = await r.text();
    if (!r.ok) {
      let msg = '讀取失敗';
      try { msg = JSON.parse(txt).error || msg; } catch (e) {}
      return toast(msg, true);
    }
    edPath = path;
    $('#edName').textContent = path;
    $('#edText').value = txt;
    $('#edNote').textContent = txt.length + ' 字元';
    $('#edCard').hidden = false;
    $('#edCard').scrollIntoView({ behavior: 'smooth', block: 'start' });
  } catch (e) {
    toast('讀取失敗：' + e.message, true);
  }
}

$('#edSave').onclick = async () => {
  if (!edPath) return;
  const body = JSON.stringify({ path: edPath, data: $('#edText').value });
  try {
    const r = await api('/api/sd/write', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body
    });
    toast(r.ok ? '已儲存 ' + edPath : (r.error || '儲存失敗'), !r.ok);
    if (r.ok) { $('#edNote').textContent = $('#edText').value.length + ' 字元（已儲存）'; loadSd(sdPath); }
  } catch (e) {
    toast('儲存失敗：' + e.message, true);
  }
};
$('#edClose').onclick = () => { $('#edCard').hidden = true; edPath = ''; };

/* 拖曳上傳 */
const drop = $('#drop');
drop.onclick = () => $('#sdPick').click();
$('#sdPick').onchange = e => { uploadFiles(Array.from(e.target.files)); e.target.value = ''; };
['dragenter', 'dragover'].forEach(ev => drop.addEventListener(ev, e => {
  e.preventDefault(); drop.classList.add('hot');
}));
['dragleave', 'drop'].forEach(ev => drop.addEventListener(ev, e => {
  e.preventDefault(); drop.classList.remove('hot');
}));
drop.addEventListener('drop', e => {
  if (e.dataTransfer && e.dataTransfer.files) uploadFiles(Array.from(e.dataTransfer.files));
});

function uploadFiles(files) {
  if (!files.length) return;
  let i = 0;
  const next = () => {
    if (i >= files.length) {
      $('#upWrap').hidden = true;
      $('#dropText').textContent = '拖曳檔案到這裡上傳（或點擊選擇）';
      toast('上傳完成');
      loadSd(sdPath);
      return;
    }
    const f = files[i++];
    const fd = new FormData();
    fd.append('file', f, f.name);
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/sd/upload?path=' + encodeURIComponent(sdPath));
    $('#upWrap').hidden = false;
    $('#upBar').style.width = '0%';
    $('#dropText').textContent = '上傳中：' + f.name + '（' + kb(f.size) + '）';
    xhr.upload.onprogress = e => {
      if (e.lengthComputable) $('#upBar').style.width = (e.loaded * 100 / e.total).toFixed(0) + '%';
    };
    xhr.onload = () => {
      let ok = false;
      try { ok = JSON.parse(xhr.responseText).ok === true; } catch (e) {}
      if (!ok) toast('上傳 ' + f.name + ' 失敗', true);
      next();
    };
    xhr.onerror = () => { toast('上傳 ' + f.name + ' 失敗', true); next(); };
    xhr.send(fd);
  };
  next();
}

/* ---------------- WiFi ---------------- */
async function refreshWifi() {
  try {
    const s = await api('/api/status');
    $('#wNow').textContent = `${s.wifi.ssid || '未設定'} · ${s.wifi.state} · ${s.wifi.ip || '無 IP'}`;
    $('#wState').textContent = s.wifi.apActive
      ? `AP 熱點：${s.wifi.ap}（http://192.168.4.1）` : '';
    if (!$('#wSsid').value) $('#wSsid').value = s.wifi.ssid || '';
  } catch (e) {}
}

$('#wScan').onclick = async () => {
  const btn = $('#wScan');
  btn.disabled = true;
  $('#wList').innerHTML = '<div class="muted">掃描中，約需 3～6 秒…</div>';
  try {
    const d = await api('/api/wifi/scan');
    $('#wList').innerHTML = d.networks.length ? d.networks.map(n => {
      const dbm = n.rssi;
      const lvl = dbm > -55 ? 4 : dbm > -67 ? 3 : dbm > -78 ? 2 : 1;
      const bars = '▮'.repeat(lvl) + '▯'.repeat(4 - lvl);
      return `<div class="item"><span>${esc(n.ssid)} ${n.secure ? '🔒' : '🔓'}
        <span class="muted">${bars} ${dbm} dBm</span></span>
        <button class="mini" data-pick="${esc(n.ssid)}">選用</button></div>`;
    }).join('') : '<div class="muted">沒有掃到基地台</div>';
  } catch (e) {
    $('#wList').innerHTML = '<div class="muted">掃描失敗</div>';
  }
  btn.disabled = false;
};

$('#wList').onclick = ev => {
  const b = ev.target.closest('button[data-pick]');
  if (!b) return;
  $('#wSsid').value = b.dataset.pick;
  $('#wPass').focus();
};

$('#wConn').onclick = async () => {
  const ssid = $('#wSsid').value.trim();
  if (!ssid) return toast('請輸入 SSID', true);
  const btn = $('#wConn');
  btn.disabled = true;
  $('#wState').textContent = '連線中，最多等 15 秒…';
  try {
    const r = await api('/api/wifi?ssid=' + encodeURIComponent(ssid) +
      '&password=' + encodeURIComponent($('#wPass').value), { method: 'POST' });
    $('#wState').textContent = r.ok ? '已連線，IP：' + r.ip : '連線失敗：' + r.error;
    toast(r.ok ? '已連線' : '連線失敗', !r.ok);
    if (r.ok) setTimeout(() => { location.href = 'http://' + r.ip + '/'; }, 1500);
  } catch (e) {
    $('#wState').textContent = '連線失敗：' + e.message;
  }
  btn.disabled = false;
  refreshWifi();
};

$('#wClear').onclick = async () => {
  if (!confirm('清除已儲存的 WiFi 設定？（會立即開啟 AP 模式）')) return;
  await api('/api/wifi/clear', { method: 'POST' });
  toast('已清除 WiFi 設定');
  refreshWifi();
};

/* ---------------- BME280 ---------------- */
const env = { t: [], h: [], p: [] };

async function envTick() {
  try {
    const d = await api('/api/sensors');
    if (d.ok) {
      env.t.push(d.t); env.h.push(d.h); env.p.push(d.p);
      ['t', 'h', 'p'].forEach(k => { if (env[k].length > 120) env[k].shift(); });
      $('#envGrid').innerHTML = [
        ['溫度', d.t.toFixed(1) + ' °C'], ['濕度', d.h.toFixed(1) + ' %'],
        ['大氣壓力', d.p.toFixed(1) + ' hPa']
      ].map(c => `<div class="kv"><b>${c[0]}</b><span>${c[1]}</span></div>`).join('');
    } else {
      $('#envGrid').innerHTML = '<div class="kv"><b>BME280</b><span>未偵測</span></div>';
    }
    drawChart('#cT', env.t, 0, 50, '#ff6b6b', '°C');
    drawChart('#cH', env.h, 0, 100, '#3fd8e8', '%');
    drawChart('#cP', env.p, 950, 1050, '#4ade80', 'hPa');
  } catch (e) {}
}

function drawChart(sel, arr, lo, hi, color, unit) {
  const c = $(sel);
  if (!c) return;
  const w = c.clientWidth || 600, h = 150;
  const dpr = window.devicePixelRatio || 1;
  if (c.width !== Math.round(w * dpr)) {
    c.width = Math.round(w * dpr);
    c.height = Math.round(h * dpr);
  }
  const g = c.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  const pad = { l: 46, r: 12, t: 12, b: 20 };
  const pw = w - pad.l - pad.r, ph = h - pad.t - pad.b;
  g.strokeStyle = '#1e2739'; g.fillStyle = '#8a97af';
  g.font = '11px system-ui'; g.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = pad.t + ph * i / 4;
    g.beginPath(); g.moveTo(pad.l, y); g.lineTo(w - pad.r, y); g.stroke();
    g.fillText((hi - (hi - lo) * i / 4).toFixed(0), 8, y + 4);
  }
  g.fillStyle = color;
  g.fillText(unit, w - pad.r - 20, pad.t + 10);
  if (arr.length < 2) return;
  g.strokeStyle = color; g.lineWidth = 2; g.beginPath();
  arr.forEach((v, i) => {
    const x = pad.l + pw * i / (arr.length - 1);
    let t = (v - lo) / (hi - lo);
    t = Math.max(0, Math.min(1, t));
    const y = pad.t + ph * (1 - t);
    i ? g.lineTo(x, y) : g.moveTo(x, y);
  });
  g.stroke();
}

window.addEventListener('resize', () => {
  if (!$('#view-env').classList.contains('on')) return;
  drawChart('#cT', env.t, 0, 50, '#ff6b6b', '°C');
  drawChart('#cH', env.h, 0, 100, '#3fd8e8', '%');
  drawChart('#cP', env.p, 950, 1050, '#4ade80', 'hPa');
});

/* ---------------- AI 聊天 ---------------- */
let msgs = [];

function renderChat() {
  const box = $('#chat');
  box.innerHTML = msgs.length
    ? msgs.map(m => `<div class="msg ${m.role}">${esc(m.text)}</div>`).join('')
    : '<div class="msg sys">按下麥克風說話，或直接打字。TFT 會同步顯示球體動畫。</div>';
  box.scrollTop = box.scrollHeight;
}

async function loadHistory() {
  try {
    const h = await api('/api/history');
    msgs = [];
    (h.turns || []).forEach(t => {
      if (t.user) msgs.push({ role: 'me', text: t.user });
      if (t.reply) msgs.push({ role: 'ai', text: t.reply });
    });
    renderChat();
  } catch (e) {}
}

async function sendText() {
  const el = $('#msg');
  const text = el.value.trim();
  if (!text) return;
  el.value = '';
  msgs.push({ role: 'me', text });
  const idx = msgs.push({ role: 'ai', text: '思考中…' }) - 1;
  renderChat();
  try {
    const r = await api('/api/chat', {
      method: 'POST', headers: { 'Content-Type': 'text/plain;charset=utf-8' }, body: text
    });
    msgs[idx].text = r.ok ? r.reply : ('錯誤：' + (r.error || '沒有回覆'));
  } catch (e) {
    msgs[idx].text = '錯誤：' + e.message;
  }
  renderChat();
}

$('#send').onclick = sendText;
$('#aiOrb').onclick = async () => {
  try { await api('/api/action?code=31', { method: 'POST' }); toast('TFT 顯示球體動畫'); }
  catch (e) { toast('裝置忙碌中', true); }
};
$('#msg').addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendText(); }
});
$('#clr').onclick = async () => {
  if (!confirm('清除本機對話記錄？')) return;
  await api('/api/history', { method: 'DELETE' });
  msgs = []; renderChat(); toast('已清除對話');
};

/* ---------------- 板載麥克風開關 / 錄音 ---------------- */
let micRecording = false;

function micHintReady(on) {
  const h = $('#micHint');
  if (h) h.textContent = on
    ? '按一下麥克風開始錄音（最長 6 秒，靜音會自動停止）'
    : '請先開啟「板載麥克風」開關';
}

function setMicUI(on) {
  const cb = $('#micOn');
  if (cb) cb.checked = !!on;
  const lbl = $('#micLabel');
  if (lbl) lbl.textContent = '板載麥克風：' + (on ? '開啟' : '關閉');
  const btn = $('#mic');
  if (btn) btn.classList.toggle('off', !on);
}

async function loadMic() {
  try {
    const d = await api('/api/mic');
    setMicUI(!!d.on);
    micHintReady(!!d.on);
  } catch (e) {}
}

$('#micOn').onchange = async e => {
  const want = e.target.checked;
  const cb = e.target;
  cb.disabled = true;
  try {
    const d = await api('/api/mic?on=' + (want ? 1 : 0), { method: 'POST' });
    setMicUI(!!d.on);
    micHintReady(!!d.on);
    toast(d.on ? '板載麥克風已開啟' : '板載麥克風已關閉');
  } catch (err) {
    setMicUI(!want);
    micHintReady(!want);
    toast(err.message || '切換麥克風失敗', true);
  }
  cb.disabled = false;
};

$('#mic').onclick = async () => {
  if (micRecording) return;
  if (!($('#micOn').checked)) return toast('請先開啟板載麥克風', true);
  const btn = $('#mic');
  micRecording = true;
  btn.classList.add('on');
  btn.disabled = true;
  $('#micOn').disabled = true;
  $('#micHint').textContent = '板載麥克風錄音中…（最長 6 秒）';
  try {
    const j = await api('/api/mic/record', { method: 'POST' });
    if (j.ok) {
      if (j.user) msgs.push({ role: 'me', text: j.user });
      msgs.push({ role: 'ai', text: j.reply });
      renderChat();
      $('#micHint').textContent = '已完成，可再按一次麥克風';
    } else {
      toast(j.error || 'AI 沒有回應', true);
      $('#micHint').textContent = j.error || '錄音失敗';
    }
  } catch (e) {
    toast('錄音送出失敗：' + e.message, true);
    $('#micHint').textContent = '錄音送出失敗：' + e.message;
  }
  micRecording = false;
  btn.classList.remove('on');
  btn.disabled = false;
  $('#micOn').disabled = false;
};

/* ---------------- AI 設定（SD 卡 config.json） ---------------- */
async function loadAiConfig() {
  try {
    const c = await api('/api/config');
    const ai = (c && c.ai) || {};
    $('#aiKey').value = ai.api_key || '';
    $('#aiModel').value = ai.model || '';
    $('#aiPrompt').value = ai.system_prompt || '';
    $('#aiNote').textContent = ai.api_key
      ? '金鑰已設定；欄位顯示 *** 表示不變更。'
      : '尚未設定 Gemini API Key，請填入後儲存。';
  } catch (e) {
    $('#aiNote').textContent = '讀取 AI 設定失敗：' + e.message;
  }
}

$('#aiReload').onclick = loadAiConfig;
$('#aiSave').onclick = async () => {
  const btn = $('#aiSave');
  btn.disabled = true;
  $('#aiNote').textContent = '儲存到 SD 卡…';
  try {
    const body = JSON.stringify({
      ai: {
        api_key: $('#aiKey').value.trim(),
        model: $('#aiModel').value.trim() || 'gemini-2.0-flash',
        system_prompt: $('#aiPrompt').value.trim()
      }
    });
    const r = await api('/api/config', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body
    });
    if (r.ok) {
      toast('AI 設定已儲存');
      await loadAiConfig();
    } else {
      toast(r.error || '儲存失敗', true);
      $('#aiNote').textContent = r.error || '儲存失敗';
    }
  } catch (e) {
    toast('儲存失敗：' + e.message, true);
    $('#aiNote').textContent = '儲存失敗：' + e.message;
  }
  btn.disabled = false;
};

/* ---------------- CAMERA ---------------- */
let camOn = false, camFrameTimer = 0, camFaceTimer = 0, camFetching = false, camFails = 0;
const CAM_FRAME_MS = 150, CAM_FACE_MS = 1500;

function camStart() {
  if (camOn) return;
  camOn = true;
  camFails = 0;
  $('#camHint').textContent = '即時影像';
  $('#camState').textContent = '連線中…';
  camTick();
  camFaceTick();
}

function stopCam() {
  camOn = false;
  clearTimeout(camFrameTimer);
  clearTimeout(camFaceTimer);
  camFetching = false;
  const hint = $('#camHint');
  if (hint) hint.textContent = '停止';
}

async function camTick() {
  if (!camOn) return;
  if (!camFetching) {
    camFetching = true;
    try {
      const r = await fetch('/api/camera/frame?t=' + Date.now(), { cache: 'no-store' });
      if (r.ok) {
        const blob = await r.blob();
        if (blob.size > 100) {
          camFails = 0;
          const url = URL.createObjectURL(blob);
          const img = $('#camImg');
          img.onload = () => URL.revokeObjectURL(url);
          img.onerror = () => {
            URL.revokeObjectURL(url);
            $('#camState').textContent = '影像解碼失敗，請重新開始';
          };
          img.src = url;
        }
      } else {
        const txt = await r.text();
        camFails++;
        $('#camState').textContent = '影像錯誤：' + (txt || ('HTTP ' + r.status));
        if (camFails >= 4) {
          stopCam();
          toast('即時影像中斷，請按「開始即時影像」重試', true);
          return;
        }
      }
    } catch (e) {
      camFails++;
    }
    camFetching = false;
  }
  if (camOn) camFrameTimer = setTimeout(camTick, CAM_FRAME_MS);
}

async function camFaceTick() {
  if (!camOn) return;
  try {
    const d = await api('/api/camera/face?recognize=1');
    drawOverlay(d);
    const st = $('#camState');
    if (!d.detected) st.textContent = '未偵測到人臉';
    else if (d.recognized) st.textContent = '辨識成功：' + d.name + '（相似度 ' + d.score.toFixed(2) + '）';
    else st.textContent = '未知人臉（相似度 ' + d.score.toFixed(2) + ' / 門檻 ' + d.threshold + '）';
  } catch (e) {}
  if (camOn) camFaceTimer = setTimeout(camFaceTick, CAM_FACE_MS);
}

function drawOverlay(d) {
  const img = $('#camImg'), cv = $('#overlay');
  if (!cv || !img.clientWidth) return;
  cv.width = img.clientWidth;
  cv.height = img.clientHeight;
  const g = cv.getContext('2d');
  g.clearRect(0, 0, cv.width, cv.height);
  if (!img.naturalWidth) return;
  const sx = cv.width / img.naturalWidth;
  const sy = cv.height / img.naturalHeight;
  const cropX = (img.naturalWidth - 240) / 2 * sx;
  g.strokeStyle = 'rgba(0,200,255,.28)';
  g.setLineDash([5, 5]);
  g.strokeRect(cropX, 0, 240 * sx, 240 * sy);
  g.setLineDash([]);
  if (!d || !d.detected) return;
  const x = cropX + d.box[0] * sx, y = d.box[1] * sy;
  const w = d.box[2] * sx, h = d.box[3] * sy;
  g.strokeStyle = d.recognized ? '#00dc78' : '#ff5151';
  g.lineWidth = 2;
  g.strokeRect(x, y, w, h);
  g.fillStyle = g.strokeStyle;
  g.font = 'bold 13px system-ui';
  g.fillText(d.recognized ? d.name : '未知人臉', x + 2, Math.max(14, y - 5));
}

$('#camStart').onclick = camStart;
$('#camStop').onclick = () => { stopCam(); toast('已停止即時影像'); };
$('#camTft').onclick = async () => {
  try { await api('/api/action?code=56', { method: 'POST' }); toast('TFT 開始顯示串流影像'); }
  catch (e) { toast('裝置忙碌中', true); }
};
$('#camRecog').onclick = async () => {
  try { await api('/api/action?code=53', { method: 'POST' }); toast('TFT 開始人臉辨識'); }
  catch (e) { toast('裝置忙碌中', true); }
};
$('#camSnap').onclick = () => { window.open('/api/camera/frame', '_blank'); };

/* ---------------- 人臉設定 ---------------- */
async function loadFaces() {
  try {
    const d = await api('/api/faces');
    $('#faceList').innerHTML = d.faces.length ? d.faces.map(n =>
      `<div class="item"><span>👤 ${esc(n)}</span>
        <button class="mini danger" data-face="${esc(n)}">刪除</button></div>`).join('')
      : '<div class="muted">尚未註冊任何人臉</div>';
    $('#faceNote').textContent =
      `已註冊 ${d.faces.length} / ${d.max} 個 · 辨識門檻 ${d.threshold}`;
  } catch (e) {}
}

$('#faceList').onclick = async ev => {
  const b = ev.target.closest('button[data-face]');
  if (!b) return;
  const name = b.dataset.face;
  if (!confirm('刪除人臉 ID「' + name + '」？')) return;
  const r = await api('/api/faces/delete?name=' + encodeURIComponent(name), { method: 'POST' });
  toast(r.ok ? '已刪除 ' + name : (r.error || '刪除失敗'), !r.ok);
  loadFaces();
};

$('#faceAdd').onclick = async () => {
  const name = $('#faceName').value.trim();
  const btn = $('#faceAdd');
  btn.disabled = true;
  $('#faceNote').textContent = '請面向鏡頭保持不動，取樣中…（約 3～10 秒）';
  try {
    const r = await api('/api/faces/add?name=' + encodeURIComponent(name), { method: 'POST' });
    if (r.ok) {
      toast('已加入 ' + r.name);
      $('#faceName').value = '';
      loadFaces();
    } else {
      toast(r.error || '加入失敗', true);
      $('#faceNote').textContent = r.error || '加入失敗';
    }
  } catch (e) {
    toast('加入失敗：' + e.message, true);
  }
  btn.disabled = false;
};

$('#faceCsv').onclick = async () => {
  try {
    const r = await api('/api/faces/export', { method: 'POST' });
    toast(r.ok ? r.msg : (r.error || '匯出失敗'), !r.ok);
    loadSd(sdPath);
  } catch (e) {
    toast('匯出失敗：' + e.message, true);
  }
};

/* ---------------- 啟動 ---------------- */
go('sys');
