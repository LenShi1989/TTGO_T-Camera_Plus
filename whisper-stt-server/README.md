# whisper-stt-server

版本：**1.6.1**（與 ESP32 韌體版本一致）

Python + FastAPI + faster-whisper 的繁體中文語音轉文字（STT）伺服器。

用途：供 TTGO T-Camera Plus ESP32 裝置把麥克風錄製的 16 kHz 單聲道 WAV 上傳，
轉成文字後回給 ESP32，由後者的 Ollama + Qwen 處理語意，並輸出至 TFT 240x240 螢幕。

相容介面：

- `POST /audio/transcriptions`、`POST /v1/audio/transcriptions`（OpenAI 相容，multipart/form-data）
- `POST /chat/completions`、`POST /v1/chat/completions`（OpenAI 相容，轉給 Ollama）
- `POST /api/chat`（Ollama 相容）
- `GET /models`、`GET /v1/models`、`GET /api/tags`（模型列表）
- `POST /voice/chat`、`POST /v1/voice/chat`（語音辨識 + AI 回應一次完成）

## 環境

- Python 3.12+
- 已建立虛擬環境：`venv312/`
- 必要套件：`fastapi`, `uvicorn[standard]`, `faster-whisper`, `python-multipart`

### 安裝

```sh
py -3.12 -m venv venv312               # 建立環境
venv312\Scripts\activate               # 進入虛擬環境
python -m pip install --upgrade pip    # 更新pip
pip install -r requirements.txt        # 安裝套件
```

第一執行時會自動下載 Whisper 模型（預設 `base`）。若希望模型放在特定目錄，
請設定環境變數 `WHISPER_MODEL_DIR`。

### 啟動

```sh
cd C:\Users\Len\Documents\1.GitHub\TTGO_T-Camera_Plus\whisper-stt-server
venv312\Scripts\python.exe app_main.py
```

預設聽在 `http://0.0.0.0:8000`。若要指定位址與通訊埠：

```powershell
$env:STT_HOST="192.168.5.100"
$env:STT_PORT="8000"
cd C:\Users\Len\Documents\1.GitHub\TTGO_T-Camera_Plus\whisper-stt-server
venv312\Scripts\python.exe app_main.py
```

ESP32 可直接使用：
`http://192.168.5.100:8000/audio/transcriptions`

### ESP32 設定範例

> 網址一定要包含 `http://`。只填 `192.168.5.100:8000` 時，舊版韌體會顯示
> 「無法建立 HTTP 連線」；新版韌體會自動補上 `http://`。

**方式 A：Ollama 模式（建議，不需要 API Key）**

| 欄位 | 值 |
| --- | --- |
| 協定模式 | `ollama` |
| 服務網址 | `http://192.168.5.100:8000` |
| API Key | 留空 |
| 模型 | 由伺服器 `AI_MODEL` 決定（例如 `qwen3.5:9b`） |

**方式 B：OpenAI 相容模式**

| 欄位 | 值 |
| --- | --- |
| 協定模式 | `openai` |
| 服務網址 | `http://192.168.5.100:8000/v1` |
| API Key | 任意非空字串（例如 `local`） |
| 模型 | `qwen3.5:9b` |

**語音辨識（STT）**

| 欄位 | 值 |
| --- | --- |
| 啟用語音辨識 | 勾選 |
| 服務網址 | `http://192.168.5.100:8000`（留空則沿用 AI 網址） |
| API Key | 留空 |
| 模型 | `base` |
| 語言 | `zh` |

兩種模式都會把對話轉給 `AI_BASE_URL`（預設 `http://localhost:11434`）的 Ollama。

### 模型

預設模型是 `base`。若要改用其他模型，執行時設定 `WHISPER_MODEL`：

```powershell
$env:WHISPER_MODEL="small"
venv312\Scripts\python.exe app_main.py
```

常用選項（模型越大，辨識通常越好，但首次下載與記憶體需求也越高）：

- `tiny`
- `base`
- `small`
- `medium`
- `large-v3`

本機跑在這類裝置的伺服器時，建議先用 `base` 或 `small` 測過吞吐與延遲，
再決定是否上更大模型。

### 支援的上傳格式

ESP32 會上傳 16 kHz、16-bit、單聲道 WAV。伺服器會：

- 接受 `multipart/form-data`
- 讀取 `file` 欄位中的 WAV
- 接受選填的 `model` 與 `language` 參數
- 回傳 `{"text": "..."}`

若上傳的音檔不是 WAV，或解碼失敗，會回傳錯誤訊息與對應的 HTTP 狀態。

### 本機快速測試

先把一個 WAV 放到專案目錄，然後從命令列試：

```powershell
$env:STT_HOST="127.0.0.1"
$env:STT_PORT="8000"
$env:WHISPER_MODEL="base"
$env:WHISPER_MODEL_DIR="model" | Out-Null
Start-Process -FilePath "venv312\Scripts\python.exe" -ArgumentList "app_main.py" -NoNewWindow
```

另開 PowerShell：

```powershell
$c = New-Object System.Net.Http.HttpClient
$form = New-Object System.Net.Http.MultipartFormDataContent
$form.Add((New-Object System.Net.Http.StreamContent((Get-Item "voice.wav").OpenRead())), "file", "voice.wav")
$form.Add((New-Object System.Net.Http.StringContent("base")), "model")
$form.Add((New-Object System.Net.Http.StringContent("zh")), "language")
$res = $c.PostAsync("http://127.0.0.1:8000/audio/transcriptions", $form).Result
$res.Content.ReadAsStringAsync().Result
```

### 提示詞與模型行為

- `/audio/transcriptions` 只做語音轉文字；`/voice/chat`、`/chat/completions`、`/api/chat` 會再交給 Ollama 做語意理解。
- 語意理解交給 ESP32 後端的 Ollama + Qwen。
- Whisper 輸出的是逐字稿，可直接當成使用者訊息送給 AI。

### 疑難排解

- `TypeError: open() got an unexpected keyword argument 'metadata_errors'`
  → PyAV 14 以上不相容目前的 faster-whisper。執行
  `pip install "av>=11,<14"`（`requirements.txt` 已固定版本）。
- `File object has no read() method, or readable() returned False`
  → 舊版程式把 raw bytes 直接交給 faster-whisper；更新後的 `app_logic.py`
  會用 `io.BytesIO` 包裝。
- ESP32 顯示「無法建立 HTTP 連線」→ Base URL 必須有 `http://`；新版韌體
  也會自動補上。舊版韌體在語音流程切割網址時會把 `:8000` 的結尾切掉，
  已用 `voice_endpoint()` 修正。
- 修改程式或套件後記得 **重啟伺服器**（Ctrl+C 後重新執行 `app_main.py`）。

### 限制

- 只處理單一檔案上傳，不排隊也不快取。
- 模型推論會占用 CPU/GPU，若有多台 ESP32 同時送語音，可能會排隊。
- 第一次載入模型時會較慢。

## 版本紀錄

- **1.6.1**：與 ESP32 韌體同步版本。修正 TFT 相機預覽顏色反向（不再切換面板色彩反轉），伺服器端無 API 變更。
- **1.6.0**：與 ESP32 韌體同步版本。語音拍照改為 TFT 顯示相機即時預覽 3 秒後拍攝（裝置端功能），伺服器端無 API 變更。
- **1.5.0**：與 ESP32 韌體同步版本。語音拍照新增 TFT 3 秒倒數畫面（裝置端功能），伺服器端無 API 變更。
- **1.4.0**：與 ESP32 韌體同步版本。SD 卡頁新增照片縮圖與放大預覽（搭配韌體 `/api/sd/download?inline=1`），伺服器端無 API 變更。
- **1.3.0**：與 ESP32 韌體同步版本。語音拍照為裝置端功能（OV2640 → SD 卡 `/photos`），伺服器端無 API 變更。
- **1.2.0**：新增 OpenAI／Ollama 相容對話端點（`/chat/completions`、`/api/chat`）、`/v1` 路徑與 `/v1/models`；修正 STT 的 bytes 解碼與 PyAV 相容性；`app_main.py` 支援 `STT_HOST`／`STT_PORT`。
- **1.1.0**：整合 Ollama + Qwen 語意理解。
