# TTGO T-Camera Plus AI 聊天助理

把 TTGO T-Camera Plus（ESP32 + ST7789 240x240 + microSD + I2S 麥克風）變成一台
**繁體中文 AI 聊天機器人**：網頁側邊欄介面、SD 卡檔案管理、Wi-Fi 掃描設定、
OpenAI／OpenRouter／Groq／本機 Ollama 對話、Whisper 語音輸入，以及會眨眼、
會思考、會說話的 TFT 表情動畫。

> 本資料夾是**獨立**的 Arduino 專案，不會修改上層的 `esp32-camera-screen-master`
> ESP-IDF 範例。硬體腳位與初始化順序參考該範例與上層 `README.md`。

---

## 功能總覽

| 分類 | 功能 |
| --- | --- |
| 網頁介面 | 深色側邊欄、聊天視窗、SD 卡檔案管理（拖放上傳／編輯／改名／刪除／下載）、Wi-Fi 掃描與連線、AI／STT／麥克風設定、系統資訊與顯示器設定 |
| AI 對話 | OpenAI 相容 `/chat/completions`、Ollama 本地 `/api/chat`（`stream:false`）、多輪對話記憶、自訂系統提示詞 |
| 語音輸入 | I2S 麥克風錄音 → 16-bit 單聲道 WAV 存到 SD 卡 → Whisper 相容 `/audio/transcriptions` → 自動送出對話 |
| TFT 顯示 | 240x240 狀態列（時間／IP）、8 種表情動畫（待機／聆聽／思考／說話／開心／難過／錯誤／睡覺）、繁體中文 16x16 點陣字、回覆同步上螢幕 |
| 設定儲存 | 所有參數都存在 SD 卡 `/config.json`；SD 卡不在時退回 SPIFFS `/config.json` |
| 網路 | 永遠開啟 AP+STA：熱點 `名稱-XXXX`（預設密碼 `12345678`，`192.168.4.1`），同時連線家中路由器，支援 mDNS `http://<名稱>.local/` |
| 穩定性 | AI／錄音在獨立 FreeRTOS 工作任務執行，網頁不卡住；SPI 匯流排（TFT + SD 共用）以遞迴互斥鎖保護 |

---

## 硬體

| 項目 | 內容 |
| --- | --- |
| 開發板 | TTGO T-Camera Plus（ESP32-DOWDQ6） |
| Flash / PSRAM | 4 MB / 8 MB |
| 螢幕 | ST7789 240x240 IPS，SPI：SCLK 21、MOSI 19、MISO 22、CS 12、DC 15、BL 2 |
| microSD | CS **GPIO0**、與螢幕共用 SPI |
| 麥克風 | I2S：BCLK 14、WS 32、DATA 33（32-bit 立體聲框，左右相加後 `>> 13`） |
| I2C | SDA 18、SCL 23（本專案未使用） |
| 按鍵 | 無使用者按鍵 |

完整腳位定義在 [`board_config.h`](board_config.h)。

---

## 專案結構

```
TTGO_T_Camera_Plus_ChatBot/
├── TTGO_T_Camera_Plus_ChatBot.ino   啟動順序、loop
├── board_config.h                   腳位與全域常數
├── tft_setup.h                      TFT_eSPI 設定（免改函式庫）
├── app_config.h / .cpp              /config.json 讀寫與驗證
├── storage.h / .cpp                 SPIFFS、SD 卡、SPI 互斥鎖
├── app_wifi.h / .cpp                AP+STA、掃描、mDNS、NTP
├── display.h / .cpp                 狀態列、表情動畫、中文字
├── font_tc.h / .cpp                 TCF2 字型載入與繪製
├── audio.h / .cpp                   I2S 錄音 → WAV
├── chat.h / .cpp                    OpenAI / Ollama / Whisper
├── jobs.h / .cpp                    背景 AI 工作任務
├── web_ui.h / .cpp                  REST API + SPIFFS 靜態網頁
├── data/                            ★ 上傳到 SPIFFS 的網頁資源
│   ├── index.html
│   ├── style.css
│   ├── app.js
│   └── font_tc16.bin                13,842 個 Big5 + ASCII 字模（約 514 KB）
├── sd_card/
│   └── config.json                  ★ 複製到 SD 卡根目錄的設定範本
└── tools/
    └── make_font.py                 重新產生 font_tc16.bin（需要 Pillow）
```

---

## 安裝步驟

### 1. Arduino IDE 1.8.19

1. **開發板管理員** 安裝 `esp32 by Espressif Systems` 版本 **2.0.17**
   （建議；3.x 也可編譯，但 I2S 舊 API 的路徑不同，程式已用 `ESP_ARDUINO_VERSION_MAJOR` 區分）。
2. **函式庫管理員** 安裝：
   - `TFT_eSPI` **2.5.43**
   - `ArduinoJson` **6.21 以上**（7.x 亦可）
3. 開發板設定（工具 → 開發板 → ESP32 Arduino → **ESP32 Wrover Module**）：

   | 選項 | 值 |
   | --- | --- |
   | Flash Size | **4MB (32Mb)** |
   | Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
   | PSRAM | **Enabled** |
   | Upload Speed | 115200 |
   | CPU Frequency | 240MHz |

   > 選 `ESP32 Dev Module` 也可以，只要把 PSRAM 設為 Enabled。

### 2. 不需要修改 TFT_eSPI 的 `User_Setup.h`

本專案把 TFT_eSPI 設定放在 sketch 資料夾的 [`tft_setup.h`](tft_setup.h)，
TFT_eSPI 會自動優先載入（`TFT_eSPI.h` 的 `__has_include(<tft_setup.h>)` 機制），
所以**不用**去改 `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`。

### 3. 上傳網頁到 SPIFFS

`data/` 資料夾必須上傳到 SPIFFS，網頁才看得到（否則只會顯示提示頁）：

1. 安裝外掛 **ESP32 Sketch Data Upload**
   （Arduino IDE 1.8.x：`arduino-esp32fs-plugin`，把 jar 放到 `tools/ESP32FS/tool/`）。
2. 開啟本 sketch，選單 **Tools → ESP32 Sketch Data Upload**。
3. 等待 `SPIFFS Image Uploaded`。

> `font_tc16.bin` 約 514 KB，加上網頁檔案約 540 KB，1 MB SPIFFS 空間足夠。
> 若曾經上傳過其他專案的 SPIFFS 映像，先執行一次上傳即可覆蓋。

### 4. 準備 SD 卡

把 [`sd_card/config.json`](sd_card/config.json) 複製到 microSD 卡**根目錄**，
插入裝置。開機時若找不到設定檔，韌體會自動建立預設 `/config.json`。
`/voice/` 資料夾會自動建立。

> SD 卡建議 FAT32、32 GB 以下。GPIO0 同時是開機 strapping 腳位，若插卡後
> 無法開機，先拔卡開機再插回（或換一張卡測試）。

### 5. 編譯與上傳

打開 `TTGO_T_Camera_Plus_ChatBot.ino` → 選擇序列埠 → 上傳。
第一次開機會看到：

```
==============================================
  T-Camera Plus AI  v1.1.0
==============================================
[tft] init ST7789 240x240
[fs] mounting SPIFFS ...
[fs] mounting SD card (attempt 1) ...
[cfg] loaded /sd/config.json (1234 bytes)
[font] loaded 13842 glyphs, 526012 bytes
[wifi] AP t-camera-plus-XXXX (ok) ap_ip=192.168.4.1
[web] http://192.168.1.23/  (AP http://192.168.4.1/)
[boot] ready
```

> 要逐步打勾的完整燒錄流程（分區、SPIFFS 上傳、SD 卡準備、開機與網頁驗收）見 [`UPLOAD_CHECKLIST.md`](UPLOAD_CHECKLIST.md)。

---

## 第一次設定

1. 用手機／電腦連線熱點 **`t-camera-plus-XXXX`**（密碼 `12345678`）。
2. 瀏覽 **<http://192.168.4.1/>**（或 `http://t-camera-plus.local/`）。
3. 到 **Wi-Fi** 頁掃描 SSID、輸入密碼後按「連線」。
   連線成功後裝置會同時保留熱點，區網內也可用 `http://<名稱>.local/` 或 STA IP 開啟。
4. 到 **AI 設定** 選擇提供者、填入 API Key 與模型，按「儲存 AI 設定」。
5. 回到 **聊天** 頁輸入訊息，或按「語音」錄音。

### AI 提供者範例

| 提供者 | 模式 | Base URL | 模型範例 |
| --- | --- | --- | --- |
| OpenAI | openai | `https://api.openai.com/v1` | `gpt-4o-mini` |
| OpenRouter | openai | `https://openrouter.ai/api/v1` | `openai/gpt-4o-mini` |
| Groq | openai | `https://api.groq.com/openai/v1` | `llama-3.3-70b-versatile` |
| 本機 Ollama | ollama | `http://192.168.5.100:11434` | `qwen3.5:9b` |

網址只要填到 `/v1`（或 Ollama 的根網址），韌體會自動接上
`/chat/completions` 或 `/api/chat`。若 API Key 留空且模式是 `openai`，
送出時會提示「尚未設定 API Key」。

### 本機 Ollama 設定

Ollama 預設只聽 `127.0.0.1`，ESP32 連不到。請在跑 Ollama 的電腦上：

```powershell
# Windows（系統內容 → 環境變數 → 新增使用者變數）
setx OLLAMA_HOST 0.0.0.0
# 重新啟動 Ollama 服務後確認
curl http://192.168.5.100:11434/api/tags
```

並確認防火牆允許 TCP 11434。接著在網頁 AI 設定選「本機 Ollama」，
Base URL 改成該電腦的區網 IP，模型填 `ollama list` 看到的名稱（例如 `qwen3.5:9b`）。

### 語音辨識

- 在 **AI 設定 → 語音辨識** 勾選「啟用語音辨識」。
- STT 服務網址／Key 留空時會沿用 AI 的 Base URL 與 Key（OpenAI 可直接用）。
- 按聊天頁的「🎙️ 語音」會錄 `mic_seconds` 秒，存成 `/voice/voice.wav`，
  上傳到 `/audio/transcriptions`，辨識文字會自動當成使用者訊息送出。
- 未啟用 STT 時仍可錄音，只是不會自動對話；檔案可在 SD 卡頁下載。

> Whisper 需要能解 WAV 的服務。若使用 OpenAI，模型填 `whisper-1`、語言填 `zh`。
> 若走本機 Whisper（例如 `whisper.cpp` server），請確認它支援
> `multipart/form-data` 的 `file`、`model`、`language` 欄位。

---

## TFT 表情與中文字

| 表情 | 觸發時機 |
| --- | --- |
| `idle` 待機 | 平常，會隨機眨眼、眼球漂移 |
| `listen` 聆聽 | 錄音中（含音量柱狀動畫） |
| `think` 思考 | 等待 AI 回應／語音辨識 |
| `speak` 說話 | 顯示 AI 回覆時（嘴巴開合動畫） |
| `happy` 開心 | 工作完成但沒有文字 |
| `sad` 難過 | 工作失敗 |
| `error` 錯誤 | 錯誤提示（交叉眼） |
| `sleep` 睡覺 | 可由網頁「表情測試」手動切換 |

- 回覆文字顯示在下方 6 行區域（每行 17 px），超過會被截斷。
- 網頁「表情測試」可直接切換表情，方便確認面板。
- 系統頁可調整亮度、旋轉（0/90/180/270）、是否啟用臉部動畫。

### 自訂字型

`data/font_tc16.bin` 由 GNU Unifont 18.0.01 產生，涵蓋：

- 可列印 ASCII
- 所有 Big5 可解碼字元（繁體中文常用字）
- CJK 標點、全形符號（U+3000–U+303F、U+FF01–U+FF5E）

若要重新產生（需要 Python 3 + Pillow）：

```powershell
python tools\make_font.py
```

腳本會自動下載 Unifont 到 `tools/.cache/`，輸出 `data/font_tc16.bin`，
格式為 `TCF2`（16 bytes 標頭 + 每字 38 bytes：codepoint + 寬度 + 16x16 點陣）。
之後重新執行 **ESP32 Sketch Data Upload**。

字型檔不存在時，TFT 會退回 TFT_eSPI 內建的 ASCII 字型（中文會空白），
網頁介面不受影響。

---

## REST API

所有回應皆為 JSON（除靜態檔與下載）。錯誤格式：`{"ok":false,"error":"..."}`。

| 方法 | 路徑 | 說明 |
| --- | --- | --- |
| GET | `/api/status` | 裝置／Wi-Fi／SD／SPIFFS／記憶體／目前工作狀態 |
| GET | `/api/spiffs` | SPIFFS 檔案清單與容量 |
| GET | `/api/sd/list?path=/` | 列出 SD 卡目錄 |
| GET | `/api/sd/read?path=/config.json` | 讀取文字檔（上限 32 KB） |
| GET | `/api/sd/download?path=...` | 下載檔案 |
| POST | `/api/sd/upload?dir=/path` | 上傳單一檔案（multipart `file`；也可用 `X-Upload-Dir` 標頭） |
| POST | `/api/sd/mkdir` | `{"path":"/新資料夾"}` |
| POST | `/api/sd/delete` | `{"path":"/檔案"}`（資料夾會遞迴刪除） |
| POST | `/api/sd/rename` | `{"from":"/a","to":"/b"}` |
| POST | `/api/sd/write` | `{"path":"/a.txt","content":"..."}` |
| GET | `/api/wifi/status` | Wi-Fi 詳細狀態 |
| GET/POST | `/api/wifi/scan` | 掃描附近 SSID |
| POST | `/api/wifi/connect` | `{"ssid":"...","password":"..."}`（會寫入 config） |
| POST | `/api/wifi/forget` | 清除 STA 憑證 |
| GET | `/api/config` | 讀取完整設定 |
| POST | `/api/config` | 部分更新設定並存檔（只處理有送的欄位） |
| POST | `/api/chat` | `{"message":"..."}` 啟動背景對話工作 |
| POST | `/api/voice` | `{"seconds":6}` 啟動錄音 → STT → 對話 |
| GET | `/api/job` | 查詢背景工作進度／結果 |
| POST | `/api/chat/reset` | 清除裝置端對話記憶 |
| GET | `/api/chat/history` | 目前對話記憶 |
| POST | `/api/face` | `{"face":"happy"}` 切換表情 |
| POST | `/api/display` | `{"brightness":200,"rotation":0,"face_enabled":true}` |
| POST | `/api/reboot` | 重新啟動 |

範例：

```powershell
curl http://192.168.4.1/api/status
curl -X POST http://192.168.4.1/api/chat -H "Content-Type: application/json" -d "{\"message\":\"你好\"}"
curl http://192.168.4.1/api/job
```

---

## 設定檔欄位（`/sd/config.json`）

| 欄位 | 預設 | 說明 |
| --- | --- | --- |
| `device_name` | `T-Camera-Plus` | 裝置名稱、mDNS 主機名、AP SSID 前綴 |
| `ap_password` | `12345678` | 熱點密碼（至少 8 碼） |
| `wifi_ssid` / `wifi_password` | 空 | STA 連線；可在網頁設定 |
| `ai_mode` | `openai` | `openai` 或 `ollama` |
| `ai_base_url` | `https://api.openai.com/v1` | AI 服務根網址 |
| `ai_api_key` | 空 | Bearer token |
| `ai_model` | `gpt-4o-mini` | 模型名稱 |
| `ai_system_prompt` | 繁中簡短回覆 | 系統提示詞（最多 800 字） |
| `ai_temperature` | `0.7` | 0–2 |
| `ai_max_tokens` | `512` | 32–4096 |
| `ai_timeout_ms` | `45000` | 5–120 秒（HTTPClient 上限 60 秒） |
| `ai_history` | `8` | 記住的對話輪數（0 = 不記） |
| `stt_enabled` | `false` | 啟用 Whisper 語音辨識 |
| `stt_base_url` / `stt_api_key` | 空 | 留空沿用 AI 設定 |
| `stt_model` | `whisper-1` | 語音辨識模型 |
| `stt_language` | `zh` | 語言代碼 |
| `mic_sample_rate` | `16000` | 8000–48000 |
| `mic_seconds` | `6` | 每次錄音秒數（1–20） |
| `mic_gain` | `100` | 增益百分比（25–400） |
| `brightness` | `200` | 背光 PWM（5–255） |
| `rotation` | `0` | 0–3（90° 單位） |
| `face_enabled` | `true` | 是否播放臉部動畫 |

---

## 疑難排解

| 現象 | 處理 |
| --- | --- |
| 網頁顯示「尚未上傳網頁介面」 | 執行 **Tools → ESP32 Sketch Data Upload**；確認分區是 1 MB SPIFFS |
| 開機卡在 `SD mount FAILED` | 拔插 SD 卡、確認 FAT32、避免 GPIO0 被拉低；沒有卡片時設定會存在 SPIFFS |
| 網頁開啟很慢或 API 沒反應 | 確認有插 SD 卡（沒插卡時 SD 相關 API 會等待逾時）；重新整理即可 |
| AI 回覆「無法建立 HTTP 連線」 | Base URL 少了 `http://`（例如只填 `192.168.5.100:8000`）；新版韌體會自動補上，舊版請補齊 |
| AI 回覆「網路錯誤」 | 確認 STA 已連線、Base URL 可從區網連到；HTTPS 憑證採 `setInsecure()`，只適合個人區網使用 |
| STT 回「HTTP 404」 | 服務網址要對著 `/audio/transcriptions` 的上一層（例如 `http://192.168.5.100:8000` 或 `.../v1`） |
| Ollama 連不上 | `OLLAMA_HOST=0.0.0.0`、重啟服務、防火牆放行 11434、用電腦瀏覽器先測 `http://IP:11434` |
| 錄音很小聲 | 提高 `mic_gain`（例如 200–300），或靠近麥克風；確認 `mic_sample_rate` 與 STT 支援的格式 |
| 語音辨識回「HTTP 400」 | 多數 Whisper 服務要求 16 kHz 單聲道 WAV；把 `mic_sample_rate` 設回 16000 |
| 中文顯示空白 | `font_tc16.bin` 沒上傳或損壞，重新產生並上傳 data 資料夾 |
| 螢幕顏色相反／方向不對 | 修改 `tft_setup.h` 的 `invertDisplay` 或系統頁的旋轉設定 |
| 編譯錯誤 `ledcSetup was not declared` | 使用 ESP32 core **2.0.17**；3.x 由程式內條件編譯處理，若仍有錯請回報版本 |
| 上傳大檔失敗 | 單檔上限 64 MB；SD 卡需有足夠空間；一次上傳一個檔案的 UI 會自動排隊 |

### 已知限制

- 網頁與 API 沒有登入機制，**只適合信任的區網**；不要把 80 埠直接暴露到網際網路。
- HTTPS 使用 `setInsecure()`，不驗證憑證（ESP32 記憶體與時鐘限制）。
- 影像／語音不會上傳到雲端以外的地方；API Key 以明文存在 SD 卡。
- 語音是「按一下錄固定秒數」，不是自動 VAD 偵測。
- TFT 為 16x16 點陣，不支援表情符號；網頁上的 emoji 不會顯示在螢幕上。
- 一次只能處理一個 AI／錄音工作（佇列長度 3）。

---

## 授權與致謝

- 硬體初始化與腳位參考上層 `esp32-camera-screen-master`（ESP-IDF 範例）。
- 字型：GNU Unifont（OFL-1.1 / GPLv2+ with font embedding exception），
  由 `tools/make_font.py` 轉成點陣格式。
- TFT 驅動：[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)（Bodmer）。
- JSON：[ArduinoJson](https://arduinojson.org/)（Benoit Blanchon）。
