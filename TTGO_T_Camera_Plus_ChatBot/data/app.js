/* ==========================================================================
   T-Camera Plus AI 助理 - 前端邏輯
   純 JavaScript，無外部相依，所有資料都來自裝置的 /api/*
   ========================================================================== */
'use strict';

const $  = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

const CHAT_KEY = 'ttc_chat_v1';
const MAX_STORED_MESSAGES = 200;

const state = {
  status: null,
  config: null,
  view: 'chat',
  sdPath: '/',
  sdParent: '/',
  sdLoaded: false,
  sdEntries: [],
  sdSelected: new Set(),
  sdDeleting: false,
  jobTimer: null,
  jobKind: null,
  handledJobId: -1,
  busy: false,
  uploading: false,
  editPath: '',
  confirmResolve: null,
};

const VIEW_META = {
  chat:   { title: '聊天',    sub: '與裝置上的 AI 對話，回覆會同步顯示在 240x240 螢幕。' },
  sd:     { title: 'SD 卡',   sub: '瀏覽、上傳、編輯與刪除 SD 卡上的檔案。' },
  wifi:   { title: 'Wi-Fi',   sub: '掃描附近網路、設定 STA 連線，或清除已儲存的憑證。' },
  ai:     { title: 'AI 設定', sub: '設定 AI 提供者、語音辨識與麥克風參數。' },
  system: { title: '系統',    sub: '裝置資訊、顯示器設定、SPIFFS 檔案與重新啟動。' },
};

const PRESETS = {
  openai:     { mode: 'openai', base: 'https://api.openai.com/v1',            model: 'gpt-4o-mini' },
  openrouter: { mode: 'openai', base: 'https://openrouter.ai/api/v1',         model: 'openai/gpt-4o-mini' },
  groq:       { mode: 'openai', base: 'https://api.groq.com/openai/v1',       model: 'llama-3.3-70b-versatile' },
  ollama:     { mode: 'ollama', base: 'http://192.168.1.100:11434',           model: 'llama3.1:8b' },
};

/* --------------------------------------------------------------------------
   共用工具
   -------------------------------------------------------------------------- */
function fmtBytes(n) {
  const v = Number(n) || 0;
  if (v < 1024) return v + ' B';
  if (v < 1024 * 1024) return (v / 1024).toFixed(1) + ' KB';
  if (v < 1024 * 1024 * 1024) return (v / (1024 * 1024)).toFixed(1) + ' MB';
  return (v / (1024 * 1024 * 1024)).toFixed(2) + ' GB';
}

function fmtUptime(sec) {
  const s = Math.max(0, Math.floor(Number(sec) || 0));
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (d) return `${d} 天 ${h} 小時 ${m} 分`;
  if (h) return `${h} 小時 ${m} 分`;
  if (m) return `${m} 分 ${s % 60} 秒`;
  return `${s} 秒`;
}

function nowLabel() {
  const d = new Date();
  const p = (n) => String(n).padStart(2, '0');
  return `${p(d.getHours())}:${p(d.getMinutes())}`;
}

function toast(message, type, timeout) {
  const el = document.createElement('div');
  const kind = type === 'error' ? 'bad' : (type || 'info');
  el.className = 'toast ' + kind;
  el.textContent = message;
  $('#toasts').appendChild(el);
  const ttl = timeout || 4200;
  setTimeout(() => {
    el.style.transition = 'opacity .25s, transform .25s';
    el.style.opacity = '0';
    el.style.transform = 'translateX(12px)';
    setTimeout(() => el.remove(), 260);
  }, ttl);
}

async function api(path, options) {
  const opts = options || {};
  const init = { method: opts.method || 'GET', headers: {} };
  if (opts.body !== undefined) {
    init.headers['Content-Type'] = 'application/json';
    init.body = JSON.stringify(opts.body);
  }

  let res;
  try {
    res = await fetch(path, init);
  } catch (err) {
    throw new Error('無法連線到裝置，請確認網路');
  }

  const text = await res.text();
  let data = {};
  if (text) {
    try {
      data = JSON.parse(text);
    } catch (err) {
      throw new Error('伺服器回應格式錯誤');
    }
  }
  if (!res.ok || data.ok === false) {
    throw new Error(data.error || ('HTTP ' + res.status));
  }
  return data;
}

/* --------------------------------------------------------------------------
   確認對話框
   -------------------------------------------------------------------------- */
function askConfirm(title, text) {
  $('#confirmTitle').textContent = title;
  $('#confirmText').textContent = text;
  $('#confirmModal').classList.remove('hidden');
  return new Promise((resolve) => { state.confirmResolve = resolve; });
}

function closeConfirm(value) {
  $('#confirmModal').classList.add('hidden');
  const resolve = state.confirmResolve;
  state.confirmResolve = null;
  if (resolve) resolve(value);
}

/* --------------------------------------------------------------------------
   導覽
   -------------------------------------------------------------------------- */
function switchView(name) {
  if (!VIEW_META[name]) return;
  state.view = name;

  $$('.nav-item').forEach((btn) => btn.classList.toggle('active', btn.dataset.view === name));
  $$('.view').forEach((view) => view.classList.toggle('active', view.id === 'view-' + name));

  $('#viewTitle').textContent = VIEW_META[name].title;
  $('#viewSub').textContent = VIEW_META[name].sub;

  if (name === 'sd' && !state.sdLoaded) loadSd(state.sdPath);
  if (name === 'wifi') renderWifi();
  if (name === 'system') { renderSystem(); loadSpiffs(); }
  if (name === 'chat') setTimeout(() => { const box = $('#messages'); box.scrollTop = box.scrollHeight; }, 30);
}

/* --------------------------------------------------------------------------
   狀態
   -------------------------------------------------------------------------- */
async function refreshStatus() {
  try {
    const s = await api('/api/status');
    state.status = s;
    renderStatus();
    if (state.view === 'system') renderSystem();
    if (state.view === 'wifi') renderWifi();

    if (s.job && s.job.status === 'running' && !state.jobTimer) {
      state.jobKind = s.job.kind || 'chat';
      startJobPolling();
    }
  } catch (err) {
    const dot = $('#netDot');
    dot.className = 'dot bad';
    $('#netTitle').textContent = '離線';
    $('#netSub').textContent = '無法連線到裝置';
  }
}

function renderStatus() {
  const s = state.status;
  if (!s) return;
  const w = s.wifi || {};

  $('#deviceName').textContent = s.name || 'T-Camera Plus';
  $('#fwVersion').textContent = 'v' + (s.version || '--');
  $('#apInfo').textContent = 'AP ' + (w.ap_ip || '--');

  const host = (w.hostname || '') + '.local';
  const link = $('#mdnsLink');
  link.textContent = host;
  link.href = 'http://' + host + '/';

  $('#chipBadge').textContent = s.chip || 'ESP32';
  $('#heapBadge').textContent = '記憶體 ' + fmtBytes(s.heap_free);

  const dot = $('#netDot');
  if (w.sta_connected) {
    dot.className = 'dot ok';
    $('#netTitle').textContent = '已連線 ' + (w.ssid || '');
    $('#netSub').textContent = (w.ip || '') + ' · ' + (w.rssi || 0) + ' dBm';
  } else {
    dot.className = 'dot';
    $('#netTitle').textContent = '熱點模式（AP）';
    $('#netSub').textContent = (w.ap_ssid || '') + ' · ' + (w.ap_ip || '');
  }

  if (s.job && s.job.status === 'running') {
    $('#micLabel').textContent = (s.job.kind === 'voice' && s.job.progress < 55) ? '錄音中' : '處理中';
  }
}

function renderSystem() {
  const s = state.status;
  if (!s) return;
  $('#sysChip').textContent = `${s.chip || '--'} · ${s.cores || '?'} 核 · ${s.cpu_mhz || '?'} MHz`;
  $('#sysVersion').textContent = 'v' + (s.version || '--') + '（' + (s.sdk || '') + '）';
  $('#sysUptime').textContent = fmtUptime(s.uptime);
  $('#sysHeap').textContent = `${fmtBytes(s.heap_free)} / ${fmtBytes(s.heap_total)}（最低 ${fmtBytes(s.heap_min)}）`;
  $('#sysPsram').textContent = s.psram_total
    ? `${fmtBytes(s.psram_free)} / ${fmtBytes(s.psram_total)}`
    : '未啟用';
  $('#sysFlash').textContent = fmtBytes(s.flash);
  $('#sysTime').textContent = s.time || '--';
}

async function loadSpiffs() {
  try {
    const d = await api('/api/spiffs');
    renderUsage(d.used, d.total, '#spiffsUsed', '#spiffsUsageText');

    const body = $('#spiffsBody');
    body.innerHTML = '';
    if (!d.files || !d.files.length) {
      body.innerHTML = '<tr><td colspan="2" class="muted center">沒有檔案（尚未上傳 data 資料夾）</td></tr>';
      return;
    }
    d.files
      .slice()
      .sort((a, b) => a.path.localeCompare(b.path))
      .forEach((f) => {
        const tr = document.createElement('tr');
        const name = document.createElement('td');
        name.textContent = f.path;
        const size = document.createElement('td');
        size.textContent = fmtBytes(f.size);
        tr.append(name, size);
        body.appendChild(tr);
      });
  } catch (err) {
    toast('讀取 SPIFFS 失敗：' + err.message, 'error');
  }
}

function renderUsage(used, total, barSel, textSel) {
  const u = Number(used) || 0;
  const t = Number(total) || 0;
  const pct = t ? Math.min(100, (u / t) * 100) : 0;
  $(barSel).style.width = pct.toFixed(1) + '%';
  $(textSel).textContent = `${fmtBytes(u)} / ${fmtBytes(t)}（${pct.toFixed(1)}%）`;
}

/* --------------------------------------------------------------------------
   聊天
   -------------------------------------------------------------------------- */
let messages = [];
try {
  const raw = localStorage.getItem(CHAT_KEY);
  if (raw) messages = JSON.parse(raw);
} catch (err) {
  messages = [];
}
if (!Array.isArray(messages)) messages = [];

function saveMessages() {
  if (messages.length > MAX_STORED_MESSAGES) {
    messages = messages.slice(messages.length - MAX_STORED_MESSAGES);
  }
  try {
    localStorage.setItem(CHAT_KEY, JSON.stringify(messages));
  } catch (err) {
    /* 空間不足時忽略，聊天仍可繼續 */
  }
}

function bubbleEl(msg) {
  const wrap = document.createElement('div');
  wrap.className = 'msg' + (msg.role === 'user' ? ' out' : '') + (msg.role === 'error' ? ' error' : '');

  const avatar = document.createElement('div');
  avatar.className = 'avatar';
  avatar.textContent = msg.role === 'user' ? '🧑' : (msg.role === 'error' ? '⚠️' : '🤖');

  const bubble = document.createElement('div');
  bubble.className = 'bubble';
  bubble.textContent = msg.content;
  if (msg.time) {
    const meta = document.createElement('span');
    meta.className = 'meta';
    meta.textContent = msg.time;
    bubble.appendChild(meta);
  }

  wrap.append(avatar, bubble);
  return wrap;
}

function renderMessages() {
  const box = $('#messages');
  box.innerHTML = '';
  if (!messages.length) {
    const empty = document.createElement('div');
    empty.className = 'empty-chat';
    empty.innerHTML = '<div class="big">🤖</div><div>開始聊天吧！</div>' +
      '<div class="small muted">輸入文字，或按「語音」用麥克風說話。</div>' +
      '<div class="small muted">回覆會同步顯示在裝置的小螢幕上。</div>';
    box.appendChild(empty);
    return;
  }
  messages.forEach((m) => box.appendChild(bubbleEl(m)));
  box.scrollTop = box.scrollHeight;
}

function addMessage(role, content, quiet) {
  const text = String(content == null ? '' : content).trim();
  if (!text) return;
  messages.push({ role, content: text, time: nowLabel() });
  saveMessages();
  if (!quiet) renderMessages();
}

async function sendChat() {
  const input = $('#chatInput');
  const text = input.value.trim();
  if (!text || state.busy) return;

  input.value = '';
  addMessage('user', text);
  setBusy(true);
  state.jobKind = 'chat';
  state.handledJobId = -1;

  try {
    await api('/api/chat', { method: 'POST', body: { message: text } });
    startJobPolling();
  } catch (err) {
    addMessage('error', err.message);
    setBusy(false);
  }
}

async function startVoice() {
  if (state.busy) return;

  const cfg = state.config;
  const seconds = cfg ? Number(cfg.mic_seconds) || 6 : 6;

  if (cfg && !cfg.stt_enabled) {
    toast('尚未啟用語音辨識，這次只會錄音到 SD 卡 /voice/voice.wav', 'warn', 5200);
  }

  setBusy(true);
  state.jobKind = 'voice';
  state.handledJobId = -1;
  $('#micLabel').textContent = '錄音中';

  try {
    await api('/api/voice', { method: 'POST', body: { seconds } });
    startJobPolling();
  } catch (err) {
    addMessage('error', err.message);
    setBusy(false);
  }
}

function setBusy(busy) {
  state.busy = busy;
  $('#sendBtn').disabled = busy;
  $('#micBtn').disabled = busy;
  $('#sendBtn').textContent = busy ? '處理中…' : '送出';
  if (!busy) $('#micLabel').textContent = '語音';
}

function startJobPolling() {
  if (state.jobTimer) return;
  $('#jobBar').classList.remove('hidden');
  $('#jobPhase').textContent = '處理中…';
  $('#jobPct').textContent = '0%';
  $('#jobFill').style.width = '0%';
  state.jobTimer = setInterval(pollJob, 700);
  pollJob();
}

function stopJobPolling() {
  if (state.jobTimer) {
    clearInterval(state.jobTimer);
    state.jobTimer = null;
  }
  $('#jobBar').classList.add('hidden');
}

async function pollJob() {
  let job;
  try {
    job = await api('/api/job');
  } catch (err) {
    stopJobPolling();
    setBusy(false);
    toast(err.message, 'error');
    return;
  }

  const pct = Math.max(0, Math.min(100, Number(job.progress) || 0));
  $('#jobPhase').textContent = job.phase || '處理中…';
  $('#jobPct').textContent = pct + '%';
  $('#jobFill').style.width = pct + '%';

  if (job.kind === 'voice' && job.status === 'running') {
    $('#micLabel').textContent = pct < 55 ? '錄音中' : '辨識中';
  }

  if (job.status === 'running') return;

  stopJobPolling();
  setBusy(false);

  if (job.id === state.handledJobId) return;
  state.handledJobId = job.id;

  if (job.status === 'done') {
    if (job.kind === 'voice' && job.transcript) addMessage('user', job.transcript);
    if (job.result) addMessage('assistant', job.result);
    else addMessage('assistant', '（這次沒有產生回覆）');
    toast('已完成', 'good', 1800);
  } else if (job.status === 'failed') {
    if (job.kind === 'voice' && job.transcript) addMessage('user', job.transcript);
    addMessage('error', job.error || '處理失敗');
  }
}

async function resetChat() {
  const yes = await askConfirm('清除對話', '這會清除畫面上的訊息，同時重設裝置端保留的對話記憶。');
  if (!yes) return;
  try {
    await api('/api/chat/reset', { method: 'POST' });
    messages = [];
    saveMessages();
    renderMessages();
    toast('對話已清除', 'good');
  } catch (err) {
    toast(err.message, 'error');
  }
}

/* --------------------------------------------------------------------------
   SD 卡
   -------------------------------------------------------------------------- */
function joinSdPath(dir, name) {
  const d = dir === '/' ? '' : dir.replace(/\/+$/, '');
  return d + '/' + String(name).replace(/^\/+/, '');
}

function isTextFile(name) {
  return /\.(txt|md|markdown|json|js|css|html?|xml|ya?ml|ini|cfg|conf|log|csv|ini|ino|h|cpp|c|py|sh)$/i.test(name);
}

function isImageFile(name) {
  return /\.(jpe?g|png|gif|bmp|webp)$/i.test(name);
}

function inlineSdUrl(path) {
  return '/api/sd/download?inline=1&path=' + encodeURIComponent(path);
}

function openPhoto(entry) {
  $('#photoTitle').textContent = entry.name;
  $('#photoInfo').textContent = fmtBytes(entry.size) + (entry.mtime ? ' · ' + entry.mtime : '');
  $('#photoImg').src = inlineSdUrl(entry.path);
  const dl = $('#photoDownload');
  dl.href = '/api/sd/download?path=' + encodeURIComponent(entry.path);
  dl.setAttribute('download', entry.name);
  $('#photoModal').classList.remove('hidden');
}

function closePhoto() {
  $('#photoModal').classList.add('hidden');
  $('#photoImg').removeAttribute('src');
  $('#photoInfo').textContent = '';
}

async function loadSd(path) {
  try {
    const data = await api('/api/sd/list?path=' + encodeURIComponent(path || '/'));
    state.sdPath = data.path || '/';
    state.sdParent = data.parent || '/';
    state.sdLoaded = true;
    state.sdEntries = data.entries || [];
    state.sdSelected.clear();

    $('#sdPath').textContent = state.sdPath;
    $('#sdUp').disabled = state.sdPath === '/';
    renderUsage(data.used, data.total, '#sdUsed', '#sdUsageText');
    renderSdRows(state.sdEntries);

    if (data.truncated) toast('檔案數量過多，僅顯示前 ' + data.entries.length + ' 筆', 'warn');
  } catch (err) {
    toast('讀取 SD 卡失敗：' + err.message, 'error', 6000);
    state.sdEntries = [];
    state.sdSelected.clear();
    const body = $('#sdBody');
    body.innerHTML = '';
    const tr = document.createElement('tr');
    const td = document.createElement('td');
    td.colSpan = 5;
    td.className = 'muted center';
    td.textContent = err.message;
    tr.appendChild(td);
    body.appendChild(tr);
    updateSdSelection();
  }
}

function renderSdRows(entries) {
  const body = $('#sdBody');
  body.innerHTML = '';

  if (!entries.length) {
    const tr = document.createElement('tr');
    const td = document.createElement('td');
    td.colSpan = 5;
    td.className = 'muted center';
    td.textContent = '這個資料夾是空的';
    tr.appendChild(td);
    body.appendChild(tr);
    updateSdSelection();
    return;
  }

  entries.forEach((entry) => {
    const tr = document.createElement('tr');
    const selected = state.sdSelected.has(entry.path);
    if (selected) tr.classList.add('selected');

    const tdCheck = document.createElement('td');
    tdCheck.className = 'col-check';
    const check = document.createElement('input');
    check.type = 'checkbox';
    check.className = 'row-check';
    check.dataset.path = entry.path;
    check.checked = selected;
    check.title = '選取「' + entry.name + '」';
    check.addEventListener('change', () => {
      if (check.checked) state.sdSelected.add(entry.path);
      else state.sdSelected.delete(entry.path);
      tr.classList.toggle('selected', check.checked);
      updateSdSelection();
    });
    tdCheck.appendChild(check);

    const tdName = document.createElement('td');
    const nameCell = document.createElement('div');
    nameCell.className = 'name-cell';

    const image = !entry.dir && isImageFile(entry.name);
    if (image) {
      const thumb = document.createElement('img');
      thumb.className = 'thumb';
      thumb.alt = '';
      thumb.loading = 'lazy';
      thumb.decoding = 'async';
      thumb.title = '點擊預覽 ' + entry.name;
      thumb.src = inlineSdUrl(entry.path);
      thumb.addEventListener('click', (ev) => {
        ev.preventDefault();
        ev.stopPropagation();
        openPhoto(entry);
      });
      thumb.addEventListener('error', () => {
        thumb.classList.add('broken');
        thumb.removeAttribute('src');
      });
      nameCell.appendChild(thumb);
    }

    const link = document.createElement('a');
    link.className = 'entry-name' + (entry.dir ? ' dir' : '');
    link.href = '#';
    link.textContent = (entry.dir ? '📁 ' : '📄 ') + entry.name;
    if (entry.dir) {
      link.addEventListener('click', (ev) => { ev.preventDefault(); loadSd(entry.path); });
    } else if (image) {
      link.addEventListener('click', (ev) => { ev.preventDefault(); openPhoto(entry); });
    } else {
      link.addEventListener('click', (ev) => { ev.preventDefault(); openEditor(entry.path, entry.name); });
    }
    nameCell.appendChild(link);
    tdName.appendChild(nameCell);

    const tdSize = document.createElement('td');
    tdSize.textContent = entry.dir ? '—' : fmtBytes(entry.size);

    const tdTime = document.createElement('td');
    tdTime.className = 'muted';
    tdTime.textContent = entry.mtime || '—';

    const tdActions = document.createElement('td');
    const actions = document.createElement('div');
    actions.className = 'row-actions';

    if (!entry.dir && isImageFile(entry.name)) {
      actions.appendChild(miniButton('預覽', () => openPhoto(entry)));
    }
    if (!entry.dir && isTextFile(entry.name)) {
      actions.appendChild(miniButton('編輯', () => openEditor(entry.path, entry.name)));
    }
    if (!entry.dir) {
      const a = document.createElement('a');
      a.className = 'mini';
      a.textContent = '下載';
      a.href = '/api/sd/download?path=' + encodeURIComponent(entry.path);
      actions.appendChild(a);
    }
    actions.appendChild(miniButton('重新命名', () => renameEntry(entry)));
    actions.appendChild(miniButton('刪除', () => deleteEntry(entry), true));

    tdActions.appendChild(actions);

    tr.append(tdCheck, tdName, tdSize, tdTime, tdActions);
    body.appendChild(tr);
  });

  updateSdSelection();
}

function updateSdSelection() {
  const valid = new Set(state.sdEntries.map((entry) => entry.path));
  Array.from(state.sdSelected).forEach((path) => {
    if (!valid.has(path)) state.sdSelected.delete(path);
  });

  const total = state.sdEntries.length;
  const count = state.sdSelected.size;
  const all = $('#sdSelectAll');
  all.disabled = total === 0;
  all.checked = total > 0 && count === total;
  all.indeterminate = count > 0 && count < total;

  $('#sdSelectInfo').textContent = total ? `已選 ${count} / ${total} 項` : '已選 0 項';

  const btn = $('#sdDeleteSel');
  btn.disabled = count === 0 || state.sdDeleting;
  btn.textContent = state.sdDeleting ? '刪除中…' : (count ? `刪除選取 (${count})` : '刪除選取');
}

function miniButton(label, onClick, danger) {
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'mini' + (danger ? ' danger' : '');
  btn.textContent = label;
  btn.addEventListener('click', onClick);
  return btn;
}

async function mkdir() {
  const name = window.prompt('新資料夾名稱');
  if (!name) return;
  try {
    await api('/api/sd/mkdir', { method: 'POST', body: { path: joinSdPath(state.sdPath, name) } });
    toast('已建立資料夾', 'good');
    loadSd(state.sdPath);
  } catch (err) {
    toast(err.message, 'error');
  }
}

async function renameEntry(entry) {
  const name = window.prompt('新的名稱', entry.name);
  if (!name || name === entry.name) return;
  const parent = entry.path.slice(0, entry.path.lastIndexOf('/')) || '/';
  try {
    await api('/api/sd/rename', {
      method: 'POST',
      body: { from: entry.path, to: joinSdPath(parent, name) },
    });
    toast('已重新命名', 'good');
    loadSd(state.sdPath);
  } catch (err) {
    toast(err.message, 'error');
  }
}

async function deleteEntry(entry) {
  const yes = await askConfirm(
    '刪除' + (entry.dir ? '資料夾' : '檔案'),
    `確定要刪除「${entry.path}」嗎？` + (entry.dir ? '資料夾內的所有內容也會一併刪除。' : '')
  );
  if (!yes) return;
  try {
    await api('/api/sd/delete', { method: 'POST', body: { path: entry.path } });
    toast('已刪除', 'good');
    loadSd(state.sdPath);
  } catch (err) {
    toast(err.message, 'error');
  }
}

async function deleteSelected() {
  if (state.sdDeleting) return;
  const entries = state.sdEntries.filter((entry) => state.sdSelected.has(entry.path));
  if (!entries.length) return;

  const dirs = entries.filter((entry) => entry.dir).length;
  const yes = await askConfirm(
    '刪除選取的項目',
    `確定要刪除這 ${entries.length} 個項目嗎？` +
      (dirs ? `其中包含 ${dirs} 個資料夾，資料夾內的所有內容也會一併刪除。` : '')
  );
  if (!yes) return;

  state.sdDeleting = true;
  updateSdSelection();

  let ok = 0;
  const failed = [];
  for (let i = 0; i < entries.length; i += 1) {
    const entry = entries[i];
    $('#sdDeleteSel').textContent = `刪除中 ${i + 1} / ${entries.length}`;
    try {
      await api('/api/sd/delete', { method: 'POST', body: { path: entry.path } });
      ok += 1;
      state.sdSelected.delete(entry.path);
    } catch (err) {
      failed.push(entry.name + '：' + err.message);
    }
  }

  state.sdDeleting = false;
  if (failed.length) {
    toast(`已刪除 ${ok} 項，${failed.length} 項失敗（${failed[0]}）`, 'error', 7000);
  } else {
    toast(`已刪除 ${ok} 項`, 'good');
  }
  loadSd(state.sdPath);
}

async function uploadFiles(fileList) {
  const files = Array.from(fileList || []);
  if (!files.length) return;
  if (state.uploading) {
    toast('已有檔案正在上傳，請稍候', 'warn');
    return;
  }

  state.uploading = true;
  const wrap = $('#uploadWrap');
  const hint = $('#dropHint');
  wrap.classList.remove('hidden');

  let done = 0;
  for (let i = 0; i < files.length; ++i) {
    const file = files[i];
    try {
      await uploadOne(file, (pct) => {
        $('#uploadFill').style.width = pct + '%';
        hint.textContent = `上傳中 ${i + 1}/${files.length}：${file.name} ${pct}%`;
      });
      done++;
    } catch (err) {
      toast(`上傳「${file.name}」失敗：${err.message}`, 'error', 6500);
    }
  }

  state.uploading = false;
  wrap.classList.add('hidden');
  $('#uploadFill').style.width = '0%';
  hint.textContent = '也可以按右上角的「上傳檔案」';

  if (done) {
    toast(`已上傳 ${done} 個檔案`, 'good');
    loadSd(state.sdPath);
  }
}

function uploadOne(file, onProgress) {
  return new Promise((resolve, reject) => {
    const dir = state.sdPath;
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/sd/upload?dir=' + encodeURIComponent(dir));

    // ASCII-only header fallback; non-ASCII paths go through the query string.
    if (/^[\x20-\x7E]*$/.test(dir)) {
      xhr.setRequestHeader('X-Upload-Dir', dir);
    }

    xhr.upload.onprogress = (ev) => {
      if (ev.lengthComputable) onProgress(Math.round((ev.loaded / ev.total) * 100));
    };

    xhr.onload = () => {
      let data = {};
      try { data = JSON.parse(xhr.responseText || '{}'); } catch (err) { data = {}; }
      if (xhr.status >= 200 && xhr.status < 300 && data.ok) resolve(data);
      else reject(new Error(data.error || ('HTTP ' + xhr.status)));
    };
    xhr.onerror = () => reject(new Error('網路錯誤或連線中斷'));

    const form = new FormData();
    form.append('file', file, file.name);
    xhr.send(form);
  });
}

/* --------------------------------------------------------------------------
   文字編輯器
   -------------------------------------------------------------------------- */
async function openEditor(path, name) {
  try {
    const data = await api('/api/sd/read?path=' + encodeURIComponent(path));
    state.editPath = data.path;
    $('#editorTitle').textContent = '編輯 ' + (name || data.path);
    $('#editorText').value = data.content || '';
    $('#editorInfo').textContent =
      fmtBytes(data.size) + (data.truncated ? '（檔案過大，僅顯示前 32 KB）' : '');
    $('#editorModal').classList.remove('hidden');
    setTimeout(() => $('#editorText').focus(), 60);
  } catch (err) {
    toast('無法開啟檔案：' + err.message, 'error', 6000);
  }
}

function closeEditor() {
  $('#editorModal').classList.add('hidden');
  state.editPath = '';
}

async function saveEditor() {
  if (!state.editPath) return;
  const btn = $('#editorSave');
  btn.disabled = true;
  try {
    const data = await api('/api/sd/write', {
      method: 'POST',
      body: { path: state.editPath, content: $('#editorText').value },
    });
    toast('已儲存（' + fmtBytes(data.size) + '）', 'good');
    closeEditor();
    loadSd(state.sdPath);
  } catch (err) {
    toast('儲存失敗：' + err.message, 'error', 6000);
  } finally {
    btn.disabled = false;
  }
}

/* --------------------------------------------------------------------------
   Wi-Fi
   -------------------------------------------------------------------------- */
function renderWifi() {
  const s = state.status;
  if (!s || !s.wifi) return;
  const w = s.wifi;

  $('#wifiState').textContent = w.sta_connected ? '已連線（STA + AP）' : '僅熱點模式（AP）';
  $('#wifiSsid').textContent = w.ssid || '（未設定）';
  $('#wifiIp').textContent = w.ip || '—';
  $('#wifiRssi').textContent = w.sta_connected ? (w.rssi + ' dBm') : '—';
  $('#wifiAp').textContent = `${w.ap_ssid || '--'} · ${w.ap_ip || '--'}（${w.ap_clients || 0} 台裝置）`;
  $('#wifiMdns').textContent = (w.hostname || '--') + '.local';
  $('#wifiMac').textContent = w.mac || '--';
}

function signalBars(rssi) {
  const r = Number(rssi) || -100;
  const level = r >= -55 ? 4 : r >= -67 ? 3 : r >= -78 ? 2 : r >= -88 ? 1 : 0;
  const wrap = document.createElement('span');
  wrap.className = 'bars';
  for (let i = 0; i < 4; ++i) {
    const bar = document.createElement('i');
    if (i < level) bar.className = 'on';
    wrap.appendChild(bar);
  }
  return wrap;
}

async function scanWifi() {
  const btn = $('#scanBtn');
  const list = $('#scanList');
  btn.disabled = true;
  $('#scanHint').textContent = '掃描中，約需數秒…';
  list.innerHTML = '<div class="muted small">掃描中…</div>';

  try {
    const data = await api('/api/wifi/scan');
    const nets = data.networks || [];
    list.innerHTML = '';

    if (!nets.length) {
      list.innerHTML = '<div class="muted small">沒有找到無線網路</div>';
    } else {
      nets.forEach((net) => {
        const item = document.createElement('div');
        item.className = 'scan-item';

        item.appendChild(signalBars(net.rssi));

        const ssid = document.createElement('span');
        ssid.className = 'ssid';
        ssid.textContent = net.ssid + (net.secure ? ' 🔒' : '');
        item.appendChild(ssid);

        const info = document.createElement('span');
        info.className = 'rssi';
        info.textContent = `${net.rssi} dBm · CH${net.channel}`;
        item.appendChild(info);

        item.addEventListener('click', () => {
          $('#wifiSsidInput').value = net.ssid;
          $('#wifiPassInput').focus();
          toast('已帶入 SSID：' + net.ssid, 'info', 2200);
        });

        list.appendChild(item);
      });
    }
    $('#scanHint').textContent = `找到 ${nets.length} 個網路`;
  } catch (err) {
    list.innerHTML = '';
    const p = document.createElement('div');
    p.className = 'muted small';
    p.textContent = err.message;
    list.appendChild(p);
    $('#scanHint').textContent = '掃描失敗';
  } finally {
    btn.disabled = false;
  }
}

async function connectWifi(ev) {
  ev.preventDefault();
  const ssid = $('#wifiSsidInput').value.trim();
  const password = $('#wifiPassInput').value;
  if (!ssid) {
    toast('請輸入 SSID', 'warn');
    return;
  }

  const btn = $('#wifiConnectBtn');
  btn.disabled = true;
  btn.textContent = '連線中…';

  try {
    const data = await api('/api/wifi/connect', { method: 'POST', body: { ssid, password } });
    toast('已連線，IP：' + (data.ip || '--'), 'good', 5200);
    await refreshStatus();
    await loadConfig();
  } catch (err) {
    toast('連線失敗：' + err.message, 'error', 6500);
  } finally {
    btn.disabled = false;
    btn.textContent = '連線';
  }
}

async function forgetWifi() {
  const yes = await askConfirm('清除 Wi-Fi 憑證', '裝置會中斷目前的 STA 連線，只保留熱點模式。確定要繼續嗎？');
  if (!yes) return;
  try {
    await api('/api/wifi/forget', { method: 'POST' });
    toast('已清除已儲存的 Wi-Fi', 'good');
    await refreshStatus();
    await loadConfig();
  } catch (err) {
    toast(err.message, 'error');
  }
}

/* --------------------------------------------------------------------------
   AI 設定
   -------------------------------------------------------------------------- */
async function loadConfig() {
  try {
    const data = await api('/api/config');
    state.config = data.config || {};
    fillAiForm(state.config);
    fillDisplayForm(state.config);
    fillDeviceForm(state.config);
  } catch (err) {
    toast('讀取設定失敗：' + err.message, 'error');
  }
}

function fillAiForm(cfg) {
  $('#aiMode').value = cfg.ai_mode === 'ollama' ? 'ollama' : 'openai';
  $('#aiBaseUrl').value = cfg.ai_base_url || '';
  $('#aiApiKey').value = cfg.ai_api_key || '';
  $('#aiModel').value = cfg.ai_model || '';
  $('#aiTemp').value = cfg.ai_temperature != null ? cfg.ai_temperature : 0.7;
  $('#aiMaxTokens').value = cfg.ai_max_tokens || 512;
  $('#aiHistory').value = cfg.ai_history != null ? cfg.ai_history : 8;
  $('#aiPrompt').value = cfg.ai_system_prompt || '';
  $('#sttEnabled').checked = !!cfg.stt_enabled;
  $('#sttBaseUrl').value = cfg.stt_base_url || '';
  $('#sttApiKey').value = cfg.stt_api_key || '';
  $('#sttModel').value = cfg.stt_model || 'whisper-1';
  $('#sttLang').value = cfg.stt_language || 'zh';
  $('#micRate').value = String(cfg.mic_sample_rate || 16000);
  $('#micSeconds').value = cfg.mic_seconds || 6;
  $('#micGain').value = cfg.mic_gain || 100;
}

function fillDisplayForm(cfg) {
  $('#brightness').value = cfg.brightness || 200;
  $('#brightnessVal').textContent = cfg.brightness || 200;
  $('#rotation').value = String(cfg.rotation || 0);
  $('#faceEnabled').checked = !!cfg.face_enabled;
}

function fillDeviceForm(cfg) {
  $('#devName').value = cfg.device_name || '';
  $('#devApPass').value = cfg.ap_password || '';
}

function applyPreset(name) {
  const preset = PRESETS[name];
  if (!preset) return;
  $('#aiMode').value = preset.mode;
  $('#aiBaseUrl').value = preset.base;
  $('#aiModel').value = preset.model;
  $$('#aiForm .preset-row .chip').forEach((c) => c.classList.toggle('active', c.dataset.preset === name));
  toast('已套用 ' + name + ' 預設值，記得按儲存', 'info', 3000);
}

async function saveAiConfig(ev) {
  ev.preventDefault();
  const payload = {
    ai_mode: $('#aiMode').value,
    ai_base_url: $('#aiBaseUrl').value.trim(),
    ai_api_key: $('#aiApiKey').value.trim(),
    ai_model: $('#aiModel').value.trim(),
    ai_temperature: Number($('#aiTemp').value),
    ai_max_tokens: Number($('#aiMaxTokens').value),
    ai_history: Number($('#aiHistory').value),
    ai_system_prompt: $('#aiPrompt').value,
    stt_enabled: $('#sttEnabled').checked,
    stt_base_url: $('#sttBaseUrl').value.trim(),
    stt_api_key: $('#sttApiKey').value.trim(),
    stt_model: $('#sttModel').value.trim(),
    stt_language: $('#sttLang').value.trim(),
    mic_sample_rate: Number($('#micRate').value),
    mic_seconds: Number($('#micSeconds').value),
    mic_gain: Number($('#micGain').value),
  };

  const btn = $('#aiSaveBtn');
  btn.disabled = true;
  try {
    await api('/api/config', { method: 'POST', body: payload });
    toast('AI 設定已儲存', 'good');
    await loadConfig();
  } catch (err) {
    toast('儲存失敗：' + err.message, 'error', 6000);
  } finally {
    btn.disabled = false;
  }
}

/* --------------------------------------------------------------------------
   系統 / 顯示器
   -------------------------------------------------------------------------- */
async function saveDisplay() {
  const payload = {
    brightness: Number($('#brightness').value),
    rotation: Number($('#rotation').value),
    face_enabled: $('#faceEnabled').checked,
  };
  const btn = $('#displaySave');
  btn.disabled = true;
  try {
    await api('/api/display', { method: 'POST', body: payload });
    toast('顯示設定已套用', 'good');
    await refreshStatus();
  } catch (err) {
    toast(err.message, 'error');
  } finally {
    btn.disabled = false;
  }
}

async function saveDevice() {
  const payload = {
    device_name: $('#devName').value.trim(),
    ap_password: $('#devApPass').value,
  };
  const btn = $('#devSave');
  btn.disabled = true;
  try {
    const data = await api('/api/config', { method: 'POST', body: payload });
    toast('裝置設定已儲存', 'good');
    await loadConfig();
    if (data.reboot_required) {
      const yes = await askConfirm('需要重新啟動', '裝置名稱或熱點密碼已變更，重新啟動後才會生效。要現在重新啟動嗎？');
      if (yes) await rebootDevice();
    }
  } catch (err) {
    toast('儲存失敗：' + err.message, 'error', 6000);
  } finally {
    btn.disabled = false;
  }
}

async function rebootDevice() {
  try {
    await api('/api/reboot', { method: 'POST' });
    toast('裝置重新啟動中，請稍候重新整理頁面…', 'warn', 7000);
  } catch (err) {
    toast(err.message, 'error');
  }
}

async function confirmReboot() {
  const yes = await askConfirm('重新啟動裝置', '裝置會中斷連線約 10 秒，確定要重新啟動嗎？');
  if (yes) await rebootDevice();
}

/* --------------------------------------------------------------------------
   事件綁定
   -------------------------------------------------------------------------- */
function bindNavigation() {
  $$('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => switchView(btn.dataset.view));
  });
}

function bindChat() {
  $('#sendBtn').addEventListener('click', sendChat);
  $('#micBtn').addEventListener('click', startVoice);
  $('#resetChat').addEventListener('click', resetChat);

  $('#chatInput').addEventListener('keydown', (ev) => {
    if (ev.key === 'Enter' && !ev.shiftKey && !ev.isComposing) {
      ev.preventDefault();
      sendChat();
    }
  });

  $$('#view-chat .chip[data-face]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      try {
        await api('/api/face', { method: 'POST', body: { face: btn.dataset.face } });
        toast('已切換表情：' + btn.textContent, 'good', 1600);
      } catch (err) {
        toast(err.message, 'error');
      }
    });
  });
}

function bindSd() {
  $('#sdRefresh').addEventListener('click', () => loadSd(state.sdPath));
  $('#sdUp').addEventListener('click', () => loadSd(state.sdParent));
  $('#sdMkdir').addEventListener('click', mkdir);

  $('#sdSelectAll').addEventListener('change', (ev) => {
    if (ev.target.checked) {
      state.sdEntries.forEach((entry) => state.sdSelected.add(entry.path));
    } else {
      state.sdSelected.clear();
    }
    $$('#sdBody .row-check').forEach((check) => {
      const on = state.sdSelected.has(check.dataset.path);
      check.checked = on;
      const tr = check.closest('tr');
      if (tr) tr.classList.toggle('selected', on);
    });
    updateSdSelection();
  });

  $('#sdDeleteSel').addEventListener('click', deleteSelected);

  $('#sdFile').addEventListener('change', (ev) => {
    uploadFiles(ev.target.files);
    ev.target.value = '';
  });

  const zone = $('#dropZone');
  ['dragenter', 'dragover'].forEach((name) => {
    zone.addEventListener(name, (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      zone.classList.add('drag');
    });
  });
  ['dragleave', 'dragend'].forEach((name) => {
    zone.addEventListener(name, (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      zone.classList.remove('drag');
    });
  });
  zone.addEventListener('drop', (ev) => {
    ev.preventDefault();
    ev.stopPropagation();
    zone.classList.remove('drag');
    if (ev.dataTransfer && ev.dataTransfer.files) uploadFiles(ev.dataTransfer.files);
  });

  // Prevent the browser from opening dropped files outside the drop zone.
  window.addEventListener('dragover', (ev) => ev.preventDefault());
  window.addEventListener('drop', (ev) => ev.preventDefault());

  $('#editorSave').addEventListener('click', saveEditor);
  $('#editorCancel').addEventListener('click', closeEditor);
  $('#editorClose').addEventListener('click', closeEditor);
  $('#photoClose').addEventListener('click', closePhoto);
}

function bindWifi() {
  $('#scanBtn').addEventListener('click', scanWifi);
  $('#wifiRefresh').addEventListener('click', refreshStatus);
  $('#wifiForget').addEventListener('click', forgetWifi);
  $('#wifiForm').addEventListener('submit', connectWifi);

  $('#wifiShowPass').addEventListener('change', (ev) => {
    $('#wifiPassInput').type = ev.target.checked ? 'text' : 'password';
  });
}

function bindAi() {
  $('#aiForm').addEventListener('submit', saveAiConfig);

  $$('#aiForm .preset-row .chip').forEach((btn) => {
    btn.addEventListener('click', () => applyPreset(btn.dataset.preset));
  });

  $('#aiMode').addEventListener('change', () => {
    if ($('#aiMode').value === 'ollama' && !$('#aiBaseUrl').value.trim()) {
      $('#aiBaseUrl').value = PRESETS.ollama.base;
      $('#aiModel').value = PRESETS.ollama.model;
    }
  });
}

function bindSystem() {
  $('#brightness').addEventListener('input', (ev) => {
    $('#brightnessVal').textContent = ev.target.value;
  });
  $('#displaySave').addEventListener('click', saveDisplay);
  $('#devSave').addEventListener('click', saveDevice);
  $('#rebootBtn').addEventListener('click', confirmReboot);
}

function bindModals() {
  $('#confirmOk').addEventListener('click', () => closeConfirm(true));
  $('#confirmCancel').addEventListener('click', () => closeConfirm(false));
  $('#confirmClose').addEventListener('click', () => closeConfirm(false));

  [['#editorModal', closeEditor], ['#photoModal', closePhoto], ['#confirmModal', () => closeConfirm(false)]].forEach(([sel, fn]) => {
    $(sel).addEventListener('click', (ev) => {
      if (ev.target === $(sel)) fn();
    });
  });

  document.addEventListener('keydown', (ev) => {
    if (ev.key !== 'Escape') return;
    if (!$('#editorModal').classList.contains('hidden')) closeEditor();
    if (!$('#photoModal').classList.contains('hidden')) closePhoto();
    if (!$('#confirmModal').classList.contains('hidden')) closeConfirm(false);
  });
}

/* --------------------------------------------------------------------------
   啟動
   -------------------------------------------------------------------------- */
async function init() {
  bindNavigation();
  bindChat();
  bindSd();
  bindWifi();
  bindAi();
  bindSystem();
  bindModals();

  renderMessages();
  await refreshStatus();
  await loadConfig();

  setInterval(refreshStatus, 5000);
}

document.addEventListener('DOMContentLoaded', init);
