# TTGO AI Camera（Arduino IDE 版）

TTGO T-Camera Plus（ESP32 + ST7789 240×240 + OV2640 + I2S 麥克風 + microSD）
的 Arduino IDE 專案。全繁體中文介面，主要操作由 **SPIFFS 內的網頁前端** 完成，
因為本板沒有實體按鍵，`RST` 直接接到 ESP32 的 `EN`。

- 所有裝置參數：SD 卡 `/config.json`
- 網頁前端：SPIFFS（`data/index.html`、`style.css`、`app.js`）
- AI：Google Gemini `generateContent`（文字 + WAV 語音）
- 人臉：ESP32 核心 1.0.6 內建 `esp-face`（偵測 / 註冊 / 辨識）

> 主儲存庫的硬體接腳表與其他說明請見上層 [`README.md`](../README.md)。

---

## 功能

| 頁籤 / 功能 | 內容 |
| --- | --- |
| 系統狀態 | WiFi SSID / 狀態 / IP / RSSI / AP、SPIFFS 目錄與容量、記憶體、PSRAM、SD、人臉數、BME280、電池、執行時間 |
| SD 卡設定 | 目錄瀏覽、拖拉上傳（含進度）、文字檔線上編輯、下載、刪除、新增資料夾、容量顯示 |
| WiFi 設定 | 掃描 SSID（含訊號強度）、手動輸入 SSID / 密碼、清除連線設定 |
| AI 聊天機器人 | **板載 I2S 麥克風 ON/OFF 開關** + 裝置端錄音 → Gemini、文字聊天、繁體中文回覆、TFT 球體動畫、AI 設定（Key / 模型 / 提示詞） |
| BME280 環境 | 溫度 / 濕度 / 大氣壓力三張折線圖，每 2 秒更新，保留 120 筆 |
| CAMERA | 瀏覽器即時影像（JPEG 串流）、人臉框（綠 = 成功、紅 = 未知）、TFT 串流影像、TFT 人臉辨識、人臉 ID 加入 / 刪除 / CSV 匯出 |
| 序列埠 | 量產測試與除錯指令（115200 bps） |

### 無實體按鍵的操作方式

- 冷開機顯示主選單，游標不亮。
- `display.auto_run` 預設為 `false`，選單**不會**倒數自動執行。
- 按 `RST` 只會重新開機並把游標往下移動一格，不會執行功能。
- 要離開目前 TFT 畫面，按網頁側邊欄最下方的 **「TFT 顯示主選單」**（API `code=2`）。
- 也可以使用序列埠 `menu` 指令。

---

## 開發環境

1. **Arduino IDE 1.8.19**
2. **ESP32 開發板核心 1.0.6**（必要）
   - 人臉功能使用核心內建的 `fd_forward.h` / `fr_forward.h` / `esp-face`，
     新版核心已移除，編譯會出現 `fd_forward.h: No such file`。
3. **ArduinoJson 7.x**（本專案以 7.4.2 驗證）
4. **ESP32FS / ESP32 Sketch Data Upload**（把 `data/` 上傳到 SPIFFS）

### Tools 設定

| 項目 | 值 |
| --- | --- |
| Board | ESP32 Dev Module |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| PSRAM | **Enabled** |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz |

> 本程式編譯後約 **3.02 MB**，佔 3 MB app 分區約 **96%**，因此不能使用 OTA。
> 前端檔案放在 SPIFFS（1 MB 分區），不佔 app 空間。

---

## 安裝

1. 用 Arduino IDE 開啟 `TTGO_AI_Camera/TTGO_AI_Camera.ino`。
2. 依上表設定開發板與分割區。
3. 準備 FAT32 microSD，根目錄放入：

```
/config.json      ← 可由 sd_card/config.example.json 改名
/font.bin         ← 16×16 中文點陣字型（sd_card/font.bin）
/faces.csv        ← 可省略，第一次註冊人臉時自動建立
```

4. 插入 SD 卡，USB 連線，上傳 sketch。
5. Tools → **ESP32 Sketch Data Upload**，上傳 `data/` 到 SPIFFS。
   - `data/` 必須和 `TTGO_AI_Camera.ino` 同層。
   - 沒上傳時瀏覽器會顯示「找不到 /index.html」的說明頁，但序列埠仍可操作。
6. 開啟序列埠監控（115200）查看 IP，或連線裝置 AP：
   - SSID：`TTGO-AI-XXXX`（MAC 末四碼）
   - 密碼：`12345678`（可在 `config.json` 修改）
   - 網址：`http://192.168.4.1`

---

## SD 卡檔案

### `/config.json`

若檔案不存在，開機會自動建立預設值。所有欄位都可以用網頁「SD 卡設定」直接編輯，
或用 AI 頁的「AI 設定」修改 AI 相關欄位。

```json
{
  "wifi": {
    "ssid": "",
    "password": "",
    "connect_timeout_ms": 15000,
    "ap_ssid": "TTGO-AI",
    "ap_password": "12345678"
  },
  "ai": {
    "api_key": "",
    "model": "gemini-2.0-flash",
    "system_prompt": "你是一個親切的繁體中文語音助理…",
    "timeout_ms": 45000,
    "max_output_tokens": 512,
    "temperature": 0.7
  },
  "audio": {
    "enabled": true,
    "sample_rate": 16000,
    "max_record_ms": 6000,
    "min_record_ms": 1200,
    "silence_ms": 800,
    "vad_threshold": 700,
    "gain": 1
  },
  "bme280": { "enabled": true, "address": 118, "interval_ms": 2000 },
  "camera": {
    "xclk_hz": 20000000,
    "framesize": 5,
    "jpeg_quality": 12,
    "rec_threshold": 0.55,
    "enroll_samples": 3,
    "max_faces": 16,
    "vflip": 0,
    "hmirror": 0
  },
  "display": {
    "brightness": 255,
    "nav_timeout_ms": 2500,
    "auto_run": false
  },
  "web": { "enabled": true, "user": "", "password": "" },
  "font_path": "/font.bin",
  "faces_path": "/faces.csv"
}
```

| 欄位 | 說明 |
| --- | --- |
| `wifi.ssid` / `wifi.password` | 要連線的基地台；留空則開機進入 AP 模式 |
| `wifi.ap_ssid` | AP 名稱前綴，實際為 `TTGO-AI-XXXX` |
| `wifi.ap_password` | AP 密碼，至少 8 碼；留空為開放網路 |
| `ai.api_key` | Google Gemini API Key |
| `ai.model` | 模型名稱；若帳號不支援請改成可用的模型 |
| `ai.system_prompt` | 角色設定與回覆風格 |
| `ai.max_output_tokens` | 回覆長度上限（32–2048） |
| `ai.temperature` | 創意程度（0–1 左右） |
| `audio.enabled` | 板載 I2S 麥克風開關（`false` = 不收音）；網頁開關會同步寫回這個欄位 |
| `audio.max_record_ms` | 裝置端麥克風單次錄音上限（預設 6 秒） |
| `audio.vad_threshold` | 靜音判斷 RMS 門檻；麥克風太小聲可調低 |
| `audio.gain` | 額外放大倍率 1–8 |
| `bme280.address` | 十進位：118 = 0x76、119 = 0x77（程式也會自動偵測） |
| `camera.framesize` | 5 = QVGA(320×240)，對應 `framesize_t` |
| `camera.rec_threshold` | 人臉辨識餘弦相似度門檻（0.1–0.99） |
| `camera.enroll_samples` | 註冊取樣張數（1–10） |
| `camera.vflip` / `hmirror` | 影像上下 / 左右翻轉，方向相反時改為 1 |
| `display.auto_run` | `false` = 關閉選單倒數自動執行（預設） |
| `web.user` / `web.password` | 網頁 Basic Auth；`user` 留空表示不需要密碼 |

> 網頁讀取 `/api/config` 時，`api_key` 與密碼會顯示 `***`。
> 儲存時保留 `***` 代表「不變更」。

### `/faces.csv`

人臉特徵值（512 維 float），第一次註冊時自動建立：

```
id,name,v0,v1,v2,...,v511
0,小明,0.012345,-0.004321,...
```

可用 Excel 或文字編輯器備份，也可以直接複製到另一台同型裝置。

### `/font.bin`

由 `tools/make_font.py` 從 GNU Unifont 產生的 16×16 點陣字型，共 14,827 字，
開機時載入 PSRAM。若檔案不存在或格式錯誤，會退回內建 5×7 英文字型（中文顯示為 `?`）。

---

## 網頁前端（SPIFFS）

| 檔案 | 內容 |
| --- | --- |
| `data/index.html` | 側邊欄與六大頁面 |
| `data/style.css` | 深色響應式版面 |
| `data/app.js` | API 呼叫、聊天、錄音、折線圖、相機 overlay |

### 板載麥克風開關

「AI 聊天機器人」頁有一顆 **ON / OFF 開關**，控制板上的 I2S 麥克風：

| 狀態 | 行為 |
| --- | --- |
| **ON** | 初始化 I2S（BCK 14 / WS 32 / DIN 33），可錄音並送 Gemini |
| **OFF** | 卸載 I2S 驅動，麥克風完全停止收音 |

- 開關狀態會寫回 SD 卡 `/config.json` 的 `audio.enabled`，重新開機後保持。
- 關閉時 TFT「AI聊天機器人 → 麥克風收音」也會提示先到網頁開啟，不會偷偷收音。
- 按一下 🎙 按鈕才真正錄音（最長 `audio.max_record_ms`，偵測到靜音會自動停止）。

### AI 設定

「AI 聊天機器人」頁最下方可直接修改：

- Gemini API Key
- 模型名稱
- 系統提示詞

按下「儲存 AI 設定」會透過 `POST /api/config` 寫回 SD 卡 `/config.json`，
不需要重新編譯或拔記憶卡。

### 語音流程

1. 網頁把麥克風開關切到 **ON**（`POST /api/mic?on=1`）。
2. 按 🎙 呼叫 `POST /api/mic/record`，ESP32 用板載 I2S 麥克風錄音到 PSRAM。
3. 錄音同時在 TFT 顯示球體動畫（音量驅動）。
4. 錄音結束後在裝置端組成 16 kHz / 單聲道 / 16-bit WAV。
5. WAV base64 內嵌到 Gemini `inlineData` 的 `generateContent` 請求。
6. Gemini 回覆 JSON，ESP32 解析出 `user` / `reply`，寫入對話記錄。
7. 網頁顯示訊息，TFT 同步顯示繁體中文回覆。

> 網頁不需要瀏覽器麥克風權限，手機 / 平板也能直接用板載麥克風。
> TFT 選單的「麥克風收音」（序列埠 `select 30`）走同一套 I2S 流程。

---

## 人臉設定

- **加入人臉 ID**：網頁輸入名稱後按「框出人臉並加入 ID」。
  - TFT 會同步顯示鏡頭畫面、綠色人臉框與取樣進度。
  - 預設取樣 `camera.enroll_samples` 張，成功後寫入 `/faces.csv`。
  - 不輸入名稱時自動命名 `ID1`、`ID2`…
- **刪除人臉 ID**：網頁人臉清單按「刪除」。
- **匯出 CSV**：按「匯出 CSV 到 SD 卡」，寫入 `faces_path`。
- **人臉辨識**：
  - TFT 選單「人臉辨識」或網頁「TFT 人臉辨識」。
  - 成功：綠色框 + 名稱 + 相似度。
  - 失敗 / 未知：紅色框。
- **TFT 串流影像**：網頁「TFT 串流影像」把 QVGA 畫面置中裁切成 240×240 連續顯示。
  相機以 JPEG 模式擷取，TFT 預覽與人臉偵測才即時解碼成 RGB888 / RGB565。

> 註冊與辨識需要光線充足；`rec_threshold` 越高越嚴格。
> 若一直辨識失敗，可降低門檻或增加 `enroll_samples`。

---

## BME280

- 使用 `Wire1`（I2C1，SDA 18 / SCL 23），與相機 SCCB（I2C0）分開。
- **18/23 是相機與感測器共用的腳位**：相機初始化會把 GPIO 矩陣搶回 I2C0，
  所以 `camBegin()` / `camEnd()` 之後都會自動呼叫 `bmeRebindBus()` 把 I2C1 綁回來。
- 讀取失敗時會自動重新綁定再試一次；仍失敗就每 **3 秒**重新偵測，不需要重開機。
- 開機自動偵測 `bme280.address`（預設 0x76）與 0x77，並支援 BMP280（無濕度）。
- 找不到感測器時，序列埠會列出 I2C 掃描結果，方便確認接線與供電。
- 可透過 `bme280.enabled`、`bme280.address`、`bme280.interval_ms` 調整；
  未偵測到時圖表會顯示提示，其他功能不受影響。

---

## 序列埠指令（115200 bps）

| 指令 | 說明 |
| --- | --- |
| `help` | 顯示說明 |
| `status` | WiFi / IP / 記憶體 / SD / 人臉數 / 電池 |
| `sensors` | 讀取 BME280 一次 |
| `menu` | 回到主選單 |
| `list` | 列出目前選單 |
| `select <n>` | 執行第 n 項（從 0 起算） |
| `chat <文字>` | 與 Gemini 文字對話 |
| `faces` | 列出人臉 ID |
| `face add <名稱>` | 用相機註冊人臉 |
| `face del <名稱>` | 刪除人臉 |
| `face export` | 匯出 CSV |
| `snapshot` | 拍照存成 `/snapshot.jpg` |
| `wifi scan` | 掃描基地台 |
| `wifi set <SSID> <密碼>` | 連線並寫回 `config.json` |
| `wifi clear` | 清除 WiFi 並立即開啟 AP |
| `bright <0-255>` | 設定螢幕亮度 |
| `config` | 印出設定（金鑰以 `***` 顯示） |
| `save` | 儲存 `config.json` |
| `reboot` | 重新開機 |

---

## 網頁 API

| 方法 | 路徑 | 說明 |
| --- | --- | --- |
| GET | `/` | 控制面板 |
| GET | `/api/status` | 裝置狀態 |
| GET | `/api/fs` | SPIFFS 目錄與容量 |
| GET | `/api/sensors` | BME280 讀值 |
| GET / DELETE | `/api/history` | 讀取 / 清除對話 |
| POST | `/api/chat` | 純文字聊天 |
| POST | `/api/voice` | multipart WAV 語音聊天 |
| GET | `/api/faces` | 人臉清單 |
| POST | `/api/faces/add?name=...` | 註冊人臉 |
| POST | `/api/faces/delete?name=...` | 刪除人臉 |
| POST | `/api/faces/export` | 匯出 CSV |
| GET | `/api/wifi/scan` | 掃描基地台 |
| POST | `/api/wifi?ssid=...&password=...` | 連線並儲存 |
| POST | `/api/wifi/clear` | 清除 WiFi |
| GET / POST | `/api/config` | 讀取 / 寫入 `config.json` |
| GET | `/api/sd/list?path=/` | SD 目錄 |
| GET | `/api/sd/read?path=/a.txt` | 讀取文字檔（≤ 96 KB） |
| GET | `/api/sd/download?path=/a.txt` | 下載檔案 |
| POST | `/api/sd/write` | JSON `{path,data}` 寫入 |
| POST | `/api/sd/upload?path=/` | multipart 上傳 |
| POST | `/api/sd/delete?path=...` | 刪除檔案 / 空資料夾 |
| POST | `/api/sd/mkdir?path=...` | 新增資料夾 |
| GET | `/api/mic` | 板載麥克風開關狀態 `{on,ready}` |
| POST | `/api/mic?on=1\|0` | 開啟 / 關閉板載 I2S 麥克風（寫回 `config.json`） |
| POST | `/api/mic/record` | 用板載麥克風錄音並送 Gemini |
| GET | `/api/camera/frame` | 單張 JPEG（即時影像輪詢） |
| GET | `/api/camera/face?recognize=1` | 人臉偵測 / 辨識結果 |
| GET | `/api/snapshot` | JPEG（舊版相容） |
| POST | `/api/action?code=<n>` | 觸發 TFT 動作 |

### 動作代碼

| code | 動作 |
| --- | --- |
| 2 | 返回主選單（可中斷目前 TFT 畫面） |
| 11 | 系統狀態 |
| 12 | 切換亮度 |
| 20 | WiFi 掃描 |
| 30 | 麥克風收音（AI 聊天） |
| 31 | TFT 球體動畫 |
| 40 | 環境圖表 |
| 50 | 加入人臉 |
| 52 | 匯出人臉 CSV |
| 53 | 人臉辨識 |
| 56 | TFT 相機串流 |
| 61 | 重新開機 |

---

## 程式架構

| 檔案 | 負責 |
| --- | --- |
| `TTGO_AI_Camera.ino` | 主流程、選單、TFT 畫面、動作分派、AI 任務 |
| `board_pins.h` | 硬體接腳 |
| `display.*` | ST7789 驅動、240×240 PSRAM 緩衝、中文繪字、相機 blit |
| `font.*` | SD 字型載入與二分搜尋 |
| `nav.*` | RTC 記憶體選單狀態 |
| `app_config.*` | `config.json` 讀寫與邊界檢查 |
| `storage.*` | 共用 HSPI 的 SD 存取 |
| `wifi_manager.*` | STA / AP、掃描、清除 |
| `gemini.*` | Gemini 文字 / 語音請求與回應解析 |
| `audio.*` | I2S 麥克風、VAD、增益 |
| `bme280.*` | BME280 / BMP280 驅動、I2C 重綁與自動重試 |
| `camera_face.*` | OV2640、人臉偵測 / 註冊 / 辨識、CSV |
| `power.*` | IP5306 電量 |
| `webui.*` | SPIFFS、REST API、SD 上傳、語音、相機 |
| `serial_console.*` | 序列埠指令 |
| `data/` | 上傳到 SPIFFS 的網頁前端 |

---

## 疑難排解

| 症狀 | 處理 |
| --- | --- |
| 一直重啟 / Guru Meditation | 確認 Tools → PSRAM = Enabled；螢幕緩衝與字型需要 PSRAM |
| 螢幕全黑 | 確認 `display.brightness` 不是 0；從網頁「系統狀態 → 重新開機」或序列埠 `bright 255` |
| 瀏覽器顯示「找不到 /index.html」 | 尚未上傳 SPIFFS：Tools → ESP32 Sketch Data Upload；分割區需 Huge APP |
| 中文變成 `?` | SD 卡缺少 `/font.bin` 或格式錯誤，重新複製 `sd_card/font.bin` |
| 完全沒畫面但有序列輸出 | SD 卡未掛載會導致共用 SPI 未初始化，確認 SD 卡插好 |
| 相機初始化失敗 | 鏡頭排線、PSRAM；必要時重新開機 |
| 人臉辨識一直失敗 | 光線不足、`rec_threshold` 太高，或重新註冊 |
| 錄音沒聲音 | 先確認網頁麥克風開關是 ON；麥克風在板子另一側，調低 `vad_threshold` 或提高 `gain` |
| BME280 讀不到數值 | 序列埠看 I2C 掃描結果；確認 `bme280.enabled=true`、SDA 18 / SCL 23。相機初始化後程式會自動重綁 I2C1，仍失敗會每 3 秒重試 |
| 網頁即時影像一片黑 / 停住 | 先開 `/api/camera/frame` 單張確認；相機會自動重新初始化，仍失敗請重新整理頁面 |
| Gemini HTTP 400/403 | API Key 錯誤、額度用完，或 `ai.model` 不支援 |
| Gemini HTTP 429 | 免費額度限制，稍後再試 |
| WiFi 連不上 | 裝置會開 AP，手機連 `TTGO-AI-XXXX` 後到 `http://192.168.4.1` 重設 |
| 編譯 `fd_forward.h: No such file` | ESP32 核心不是 1.0.6 |
| 編譯「程式空間不足」 | Partition Scheme 沒選 Huge APP (3MB No OTA) |
| 網頁相機顏色異常 | 先用 `/api/camera/frame` 確認 JPEG 顏色；必要時調整 `vflip` / `hmirror` |

---

## 注意事項

- 需要 **PSRAM**：畫面緩衝 115 KB、字型 593 KB、錄音與 base64 緩衝。
- 人臉特徵使用自訂格式，可單獨刪除 / 匯出 / 匯入，不依賴核心的 `face_id_list`。
- 相機（I2C0）與 BME280（I2C1）共用 GPIO 18/23，程式會在相機初始化後自動重綁 I2C1；
  若自行加了其他 I2C 裝置，請確認位址不衝突。
- 程式已佔用約 96% app 分區，新增大量功能建議改用 ESP-IDF 或精簡功能。
- 本專案以 Arduino IDE 1.8.19 + ESP32 核心 1.0.6 + ArduinoJson 7.4.2 編譯驗證。

---

## 授權

程式碼可自由使用與修改。字型檔來自
[GNU Unifont](https://unifoundry.com/unifont/)，授權為 GNU GPL v2+ with font embedding exception。
