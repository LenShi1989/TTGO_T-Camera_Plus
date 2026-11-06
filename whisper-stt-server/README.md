# whisper-stt-server

Python + FastAPI + faster-whisper 的繁體中文語音轉文字（STT）伺服器。

用途：供 TTGO T-Camera Plus ESP32 裝置把麥克風錄製的 16 kHz 單聲道 WAV 上傳，
轉成文字後回給 ESP32，由後者的 Ollama + Qwen 處理語意，並輸出至 TFT 240x240 螢幕。

相容介面：OpenAI 相容的 `POST /audio/transcriptions`（multipart/form-data）。

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

在裝置網頁的「AI 設定 -> 語音辨識」中：

- 啟用語音辨識：勾選
- 服務網址：`http://192.168.5.100:8000`
- API Key：留空（本機服務通常不需金鑰）
- 模型：`base`
- 語言：`zh`

若 ESP32 與該伺服器位在同一區網，網路設定裏的 Base URL 與 STT Base URL
都可填 `http://192.168.5.100:8000`，API Key 留空。

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

- 這個伺服器只做語音轉文字，不作語意理解。
- 語意理解交給 ESP32 後端的 Ollama + Qwen。
- Whisper 輸出的是逐字稿，可直接當成使用者訊息送給 AI。

### 限制

- 只處理單一檔案上傳，不排隊也不快取。
- 模型推論會占用 CPU/GPU，若有多台 ESP32 同時送語音，可能會排隊。
- 第一次載入模型時會較慢。
