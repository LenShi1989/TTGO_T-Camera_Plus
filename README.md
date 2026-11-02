# TTGO Camera Plus

# TTGO Camera && TTGO Camera Plus

| Function    | TTGO T-Camera     | TTGO T-Camera Plus   |
| ----------- | ----------------- | -------------------- |
| MIC         | not supported     | supported            |
| SDCard      | not supported     | supported            |
| BME280      | supported         | supported            |
| Charging    | supported         | supported            |
| I2C         | supported         | supported            |
| Screen      | OLED SSD1306/0.96 | IPS Panel ST7789/1.3 |
| Camera      | OV2640            | OV2640               |
| PIR         | supported         | not supported        |
| User button | supported         | not supported        |
| Core        | ESP32-WROVER-B    | ESP32-DOWDQ6         |
| PSRAM       | 8MBytes           | 8MBytes              |
| FLASH       | 4MBytes           | 4MBytes              |
| UART        | CP2104            | CP2104               |

---

## TTGO CAMERA PINS

| Name  | Num    |
| ----- | ------ |
| Y9    | 36     |
| Y8    | 37     |
| Y7    | 38     |
| Y6    | 39     |
| Y5    | 35     |
| Y4    | 26     |
| Y3    | 13     |
| Y2    | 34     |
| VSNC  | 5      |
| HREF  | 27     |
| PCLK  | 25     |
| XCLK  | 4      |
| SIOD  | 18     |
| SIOC  | 23     |
| PWD   | No use |
| RESET | No use |

## BME280 & IP5306 Pins

| Name | Num |
| ---- | --- |
| SDA  | 18  |
| SCL  | 23  |

## MIC Pins

| Name     | Num    |
| -------- | ------ |
| I2S_SCLK | 14     |
| I2S_LCLK | 32     |
| I2S_DOUT | 33     |
| I2S_DIN  | No use |

## TFT & SD Card Pins

| Name   | Num |
| ------ | --- |
| MISO   | 22  |
| MOSI   | 19  |
| CLK    | 21  |
| DC     | 15  |
| TFT_CS | 12  |
| TFT_BK | 2   |
| SD_CS  | 0   |

---

## Arduino IDE 版：TTGO AI Camera（繁體中文）

本儲存庫另外附上一套 **Arduino IDE 1.8.19** 專案 `TTGO_AI_Camera/`，
不需要 ESP-IDF，直接用 Arduino IDE 開啟 `.ino` 就能編譯燒錄：

- 全繁體中文選單（字型放 SD 卡 `/font.bin`）
- Google Gemini AI 語音 / 文字聊天機器人（TFT 球體動畫）
- BME280 溫度 / 濕度 / 氣壓折線圖
- 人臉註冊、人臉辨識（成功綠框、失敗紅框）、人臉參數 CSV 匯出
- 內建網頁介面與序列埠指令（因為本板沒有實體按鍵）
- 所有參數都放在 SD 卡 `/config.json`

完整說明請見 **[TTGO_AI_Camera/README.md](TTGO_AI_Camera/README.md)**，
SD 卡範例檔案在 `sd_card/`（`config.example.json`、`faces.csv`、`font.bin`）。

> 注意：人臉功能需要 **ESP32 核心 1.0.6**，且 Partition Scheme 必須選
> **Huge APP (3MB No OTA)**。

# TTGO T-Camera Plus — AI 相機助理（Arduino IDE 版）

本專案是針對 **TTGO T-Camera Plus**（ESP32 + 1.3 吋 ST7789 240×240 + OV2640 + I2S 麥克風 + microSD）
所寫的 Arduino IDE 專案，提供全繁體中文的選單系統、Google Gemini AI 聊天機器人、
BME280 環境折線圖，以及人臉註冊 / 人臉辨識功能。

所有設定參數都放在 **SD 卡 `/config.json`**，不需要重新編譯就能調整。

---

## 功能一覽

| 功能          | 說明                                                          |
| ------------- | ------------------------------------------------------------- |
| 繁體中文選單  | 16×16 中文字型由 SD 卡 `/font.bin` 載入（GNU Unifont）        |
| 系統狀態      | WiFi 連線資訊、記憶體 / SD / 電池 / 執行時間、螢幕亮度調整    |
| WiFi 設定     | 掃描 SSID、手動輸入 SSID + 密碼、清除連線設定                 |
| AI 聊天機器人 | 板載 I2S 麥克風（網頁 ON/OFF 開關）→ Gemini 語音理解 → 繁體中文回覆；TFT 球體動畫特效 |
| 環境參數      | BME280 溫度 / 濕度 / 氣壓折線圖（本機即時繪製）               |
| CAMERA        | 加入人臉 ID、刪除人臉 ID、人臉參數 CSV 匯出到 SD 卡           |
| 人臉辨識      | 成功顯示 **綠色** 框、失敗顯示 **紅色** 框，並標示相似度      |
| 網頁介面      | 前端 HTML 放在 **SPIFFS**，側邊欄六大頁籤，手機 / 電腦皆可用   |
| 系統狀態頁    | WiFi 連線資訊 + SPIFFS 目錄與容量 + 記憶體 / SD / 電池         |
| SD 卡設定頁   | SD 卡檔案 CRUD、文字檔線上編輯、**拖拉式上傳**、容量顯示       |
| 語音對話      | 板載麥克風收音（WAV）→ Gemini → 繁體中文回覆，TFT 同步球體   |
| 相機頁        | 瀏覽器即時影像（JPEG 串流）+ 人臉框（綠 = 成功、紅 = 未知）、TFT 串流影像 |
| 序列埠指令    | USB 連線即可下指令，適合量產測試與除錯                         |

---

## 硬體接腳

程式內的接腳定義集中在 `board_pins.h`，與原廠 `board_def.h` 一致。

| 功能                            | 接腳                           |
| ------------------------------- | ------------------------------ |
| SPI MISO / MOSI / CLK           | 22 / 19 / 21                   |
| TFT CS / DC / 背光              | 12 / 15 / 2                    |
| microSD CS                      | 0                              |
| I2C SDA / SCL（BME280、IP5306） | 18 / 23（使用 `Wire1`）        |
| 相機 XCLK                       | 4                              |
| 相機 SIOD / SIOC                | 18 / 23（使用 I2C0）           |
| 相機 D0–D7                      | 34, 13, 26, 35, 39, 38, 37, 36 |
| 相機 VSYNC / HREF / PCLK        | 5 / 27 / 25                    |
| I2S 麥克風 BCK / WS / DATA      | 14 / 32 / 33                   |

> 相機與 BME280 共用 18/23 兩支腳，但分別掛在 I2C0 與 I2C1。
> 因為 GPIO 矩陣同一個腳位只能綁一個輸出訊號，程式在相機初始化 / 關閉後
> 會自動重新綁定 I2C1（`bmeRebindBus()`），BME280 才能持續讀到數值。

---

## 開發環境需求

1. **Arduino IDE 1.8.19**
2. **ESP32 開發板核心 1.0.6**（重要）
   - 人臉偵測 / 辨識使用核心內建的 `esp-face`（`fd_forward.h`、`fr_forward.h`），
     新版核心已移除這些標頭檔，因此**必須使用 1.0.6**。
   - 安裝方式：Arduino IDE → 檔案 → 偏好設定 → 額外開發板管理員網址加入
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
     → 開發板管理員搜尋 `esp32` → 版本選 **1.0.6** → 安裝。
3. **ArduinoJson 7.x**（函式庫管理員搜尋 `ArduinoJson` 安裝，測試版本 7.4.2）
4. **ESP32 SPIFFS Data Upload 工具**（必要）
   - 網頁介面（`data/index.html`、`style.css`、`app.js`）存在 **SPIFFS**，
     sketch 燒錄不會帶進去，必須另外上傳。
   - Arduino IDE 1.8.19：下載 `ESP32FS` 外掛，解壓到
     `文件/Arduino/tools/ESP32FS`（資料夾內需有 `tool/esp32fs.jar`），
     重開 IDE 後 Tools 選單會出現 **ESP32 Sketch Data Upload**。
   - 上傳前請先選好開發板與分割區，工具會依目前設定寫入 SPIFFS 分區。

### 開發板設定（Tools 選單）

| 項目             | 設定值                               |
| ---------------- | ------------------------------------ |
| Board            | **ESP32 Dev Module**                 |
| Flash Size       | 4MB (32Mb)                           |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| PSRAM            | **Enabled**                          |
| Flash Frequency  | 80MHz                                |
| Flash Mode       | QIO                                  |
| Upload Speed     | 921600                               |
| CPU Frequency    | 240MHz                               |

> 本程式編譯後約 **3.02 MB**，佔 3 MB app 分區的 **96%**，所以一定要選 `Huge APP`，
> 也因此**不支援 OTA 更新**（請用 USB 燒錄）。前端網頁放在 SPIFFS（1 MB 分區），
> 不佔用 app 空間。

---

## 安裝步驟

1. 用 Arduino IDE 開啟 `TTGO_AI_Camera/TTGO_AI_Camera.ino`。
2. 依照上表設定開發板。
3. 準備 SD 卡（FAT32），根目錄放入：

```
/config.json      ← 可先放入 sd_card/config.example.json 改名
/font.bin         ← 中文 16×16 字型（本專案已附 sd_card/font.bin）
/faces.csv        ← 可省略，第一次註冊人臉時會自動建立
```

4. 插入 SD 卡 → USB 連接開發板 → **上傳 sketch**。
5. **上傳網頁介面到 SPIFFS**：Tools → **ESP32 Sketch Data Upload**（約 30 秒）。
   - `data/` 資料夾必須和 `TTGO_AI_Camera.ino` 放在同一層（本專案已附）。
   - 沒上傳也能開機，但瀏覽器會看到「找不到 /index.html」的提示頁。

> 若 SD 卡沒有 `config.json`，開機時會自動產生一份預設設定檔。
> 之後所有參數（WiFi、AI 金鑰、相機…）都可在網頁或 SD 卡設定頁修改，不必重新編譯。

---

## SD 卡檔案說明

### `/font.bin`

由 `tools/make_font.py` 從 GNU Unifont 產生的 16×16 點陣字型，共 14,827 個字
（ASCII + Big5 常用繁體中文 + 全形標點），大小 593,104 bytes，開機時整份載入 PSRAM。

格式（little-endian）：

```
"TGF1" | u16 版本 | u16 寬 | u16 高 | u16 旗標
u32 字數 | u32 索引位移 | u32 資料位移
索引[字數]：u32 Unicode 碼點, u32 點陣位移
資料：每個字 32 bytes（16 列 × 2 bytes，MSB 在左）
```

若 `font.bin` 不存在或格式錯誤，會自動改用內建的 5×7 英文字型（中文會顯示為 `?`）。

### `/config.json`

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
  "display": { "brightness": 255, "nav_timeout_ms": 2500, "auto_run": false },
  "web": { "enabled": true, "user": "", "password": "" },
  "font_path": "/font.bin",
  "faces_path": "/faces.csv"
}
```

| 欄位                          | 說明                                                    |
| ----------------------------- | ------------------------------------------------------- |
| `wifi.ssid` / `wifi.password` | 要連線的基地台；留空則開機直接進入 AP 模式              |
| `wifi.ap_ssid`                | AP 名稱前綴，實際會加上 MAC 末四碼，例如 `TTGO-AI-1A2B` |
| `wifi.ap_password`            | AP 密碼，至少 8 碼；留空則為開放網路                    |
| `ai.api_key`                  | Google Gemini API Key                                   |
| `ai.model`                    | 模型名稱，預設 `gemini-2.0-flash`                       |
| `ai.system_prompt`            | 系統提示詞（角色設定），可自由修改                      |
| `ai.max_output_tokens`        | 回覆長度上限（32–2048）                                 |
| `audio.enabled`               | 板載 I2S 麥克風開關（`false` = 不收音），網頁開關會寫回這裡 |
| `audio.max_record_ms`         | 單次錄音最長毫秒數（預設 6 秒）                         |
| `audio.min_record_ms`         | 最短錄音時間，之後偵測到靜音才會停止                    |
| `audio.vad_threshold`         | 靜音判斷的 RMS 門檻，麥克風太小聲可調低                 |
| `audio.gain`                  | 額外放大倍率 1–8（程式也會自動增益）                    |
| `bme280.address`              | **十進位**：118 = 0x76、119 = 0x77（程式也會自動偵測）  |
| `camera.framesize`            | 5 = QVGA(320×240)；數字對應 `framesize_t` 列舉值        |
| `camera.rec_threshold`        | 人臉辨識餘弦相似度門檻（0.1–0.99，越高越嚴格）          |
| `camera.enroll_samples`       | 註冊時取樣張數（1–10，越多越穩定）                      |
| `camera.vflip` / `hmirror`    | 影像上下 / 左右翻轉，若畫面方向相反請改為 1             |
| `display.nav_timeout_ms`      | 啟用 `auto_run` 時，選單停留多久自動執行（預設 2500 ms） |
| `display.auto_run`            | 預設 `false`：不再倒數自動執行，改由網頁操作            |
| `web.user` / `web.password`   | 網頁 Basic Auth；`user` 留空表示不需要密碼              |

> 透過網頁介面修改時，`api_key` / 密碼欄位顯示 `***` 代表「不變更」。

### `/faces.csv`

人臉特徵值（512 維 float），第一次註冊人臉時自動建立：

```
id,name,v0,v1,v2,...,v511
0,小明,0.012345,-0.004321,...
1,ID2,...
```

可以直接用 Excel / 文字編輯器備份或轉移到其他機器。

---

## 取得 Gemini API Key

1. 開啟 <https://aistudio.google.com/app/apikey>
2. 登入 Google 帳號 → **Create API key**
3. 複製金鑰（形如 `AIza...`），填入 `config.json` 的 `ai.api_key`，
   或用手機連線到本機 AP 後在網頁「設定」分頁填入。

> 本專案使用 `generateContent` 端點，語音則以 base64 內嵌 WAV 的方式送出，
> 不需要另外申請 Speech-to-Text。

---

## 操作說明

### 1. TFT 選單（僅顯示，不再自動執行）

TTGO T-Camera Plus **沒有使用者按鍵**，板上的 `RST` 直接接到 ESP32 的 `EN`，
按一下就是重新開機。為了避免「停留 2.5 秒自動執行」造成誤動作，
**選單倒數預設關閉**（`display.auto_run = false`），改用網頁前端操作：

| 動作                      | 結果                                             |
| ------------------------- | ------------------------------------------------ |
| 冷開機（插電 / 首次上電） | 顯示主選單，游標不亮                             |
| 按一次 RST                | 重新開機，游標往下移動一格（不執行任何功能）     |
| 在子畫面按 RST            | 回到選單，游標移到「下一項」                     |
| 畫面底部提示              | 「請由網頁前端操作」                             |

要執行任何功能，請用 **網頁側邊欄** 或 **序列埠指令**；網頁送出的動作會在 TFT 同步顯示
（球體動畫、折線圖、人臉框…）。網頁側邊欄最下方的「TFT 顯示主選單」按鈕（API `code=2`）
可以隨時中斷目前畫面。

想恢復舊的「倒數自動執行」行為，把 `config.json` 的 `display.auto_run` 改成 `true`。

> 因為每次按 RST 都會真的重新開機（WiFi 重新連線約需 2–5 秒），
> 日常操作請使用 **網頁介面**。

### 2. 網頁介面

開機後：

- 若 `wifi.ssid` 已設定且連線成功 → 瀏覽器開啟序列埠印出的 IP，例如 `http://192.168.1.50`
- 若連線失敗或未設定 → 自動開啟 AP 模式：
  - SSID：`TTGO-AI-XXXX`（XXXX 為 MAC 末四碼）
  - 密碼：`12345678`（可在 `config.json` 修改）
  - 網址：`http://192.168.4.1`

左側側邊欄共六大頁（手機版為漢堡選單）：

| 頁籤          | 內容                                                                                             |
| ------------- | ------------------------------------------------------------------------------------------------ |
| 系統狀態      | WiFi 連線資訊（SSID / 狀態 / IP / RSSI / AP）、SPIFFS 目錄與容量條、記憶體 / SD / 電池 / 執行時間 |
| SD 卡設定     | 目錄瀏覽、**拖拉式上傳**（顯示進度）、線上編輯文字檔、下載、刪除、新增資料夾、容量               |
| WiFi 設定     | 掃描 SSID（含訊號強度）、手動輸入 SSID / 密碼、清除連線設定                                      |
| AI 聊天機器人 | 板載麥克風 ON/OFF 開關、🎙 裝置端錄音（WAV 送 Gemini）、文字聊天、繁體中文回覆、AI 設定、清除對話 |
| BME280 環境   | 溫度 / 濕度 / 大氣壓力三張折線圖（每 2 秒更新，保留 120 筆）                                     |
| CAMERA        | 即時影像 + 人臉框（綠 = 成功、紅 = 未知）、TFT 串流 / 人臉辨識、加入 / 刪除人臉 ID、匯出 CSV     |
| AI 設定       | 網頁直接修改 Gemini API Key、模型、系統提示詞，寫回 SD 卡 `config.json`                         |

> 麥克風開關 **ON** 才初始化板載 I2S（BCK 14 / WS 32 / DIN 33），**OFF** 會卸載驅動停止收音；
> 開關狀態寫回 `config.json` 的 `audio.enabled`，關閉時 TFT 語音畫面也會提示先開啟。
> 錄音在裝置端完成（16 kHz 單聲道 WAV），再轉 base64 送給 Gemini，不需要瀏覽器麥克風權限。
> 文字聊天與語音聊天都會同步在 TFT 顯示球體動畫；回覆完成後自動切到 TFT 回覆畫面。
> 前端檔案來自 SPIFFS，改版面只要重新上傳 `data/`，不必重新編譯 sketch。
> AI 頁最下方的 **AI 設定** 可直接填入 Gemini API Key、模型與系統提示詞，儲存後寫回 SD 卡。

### 3. 序列埠指令（115200 bps）

| 指令                     | 說明                                                |
| ------------------------ | --------------------------------------------------- |
| `help`                   | 顯示說明                                            |
| `status`                 | 裝置狀態（WiFi / IP / 記憶體 / SD / 人臉數 / 電池） |
| `sensors`                | 讀取 BME280 一次                                    |
| `menu`                   | 回到主選單                                          |
| `list`                   | 於 TFT 顯示目前選單                                 |
| `select <n>`             | 直接跳到第 n 項（從 0 起算）並倒數執行              |
| `chat <文字>`            | 與 Gemini 文字對話                                  |
| `faces`                  | 列出已註冊人臉                                      |
| `face add <名稱>`        | 用相機註冊人臉（約 3 秒）                           |
| `face del <名稱>`        | 刪除人臉                                            |
| `face export`            | 將人臉 CSV 寫回 SD 卡                               |
| `snapshot`               | 拍照存成 `/snapshot.jpg`                            |
| `wifi scan`              | 掃描附近基地台                                      |
| `wifi set <SSID> <密碼>` | 連線並寫回 `config.json`                            |
| `wifi clear`             | 清除 WiFi 設定，並立即開啟 AP 模式                  |
| `bright <0-255>`         | 設定螢幕亮度                                        |
| `config`                 | 印出目前設定（金鑰以 `***` 顯示）                   |
| `save`                   | 儲存 `config.json`                                  |
| `reboot`                 | 重新開機                                            |

---

## 選單結構

```
主選單
├── 系統狀態
│   ├── WiFi連線資訊      SSID / 狀態 / IP / 訊號 / AP
│   ├── 系統狀態          記憶體 / SD / 人臉數 / BME280 / 電池 / 執行時間
│   ├── 顯示              循環切換亮度 255 → 170 → 100 → 40
│   └── 返回
├── WiFi設定
│   ├── 網路掃描SSID      列出附近基地台與訊號強度
│   ├── 手動輸入SSID+密碼  顯示網頁 / 序列埠輸入方式
│   ├── 清除連線設定      清除後改用 AP 模式
│   └── 返回
├── AI聊天機器人
│   ├── 麥克風收音        錄音 → Gemini → 繁體中文回覆（球體動畫）
│   ├── TFT OLED顯示      球體動畫 + 最近一次回覆
│   ├── 清除對話記錄
│   └── 返回
├── 環境參數              BME280 溫度 / 濕度 / 氣壓折線圖
├── CAMERA
│   ├── 串流影像          240×240 即時畫面（QVGA 置中裁切）
│   ├── 人臉設定
│   │   ├── 加入人臉ID            自動命名 ID1、ID2…（網頁加入時 TFT 同步顯示鏡頭與綠框）
│   │   ├── 刪除人臉ID            動態列出所有人臉
│   │   ├── 人臉ID參數CSV存到SD卡
│   │   └── 返回
│   ├── 人臉辨識          綠框 = 辨識成功、紅框 = 未知人臉
│   └── 返回
└── 關於本機              版本 / 模型 / 記憶體資訊
```

---

## 網頁 API

| 方法       | 路徑                          | 說明                                       |
| ---------- | ----------------------------- | ------------------------------------------ |
| GET        | `/`                           | 控制面板網頁                               |
| GET        | `/api/status`                 | 裝置狀態 JSON                              |
| GET        | `/api/fs`                     | SPIFFS 目錄、已用 / 總容量                 |
| GET        | `/api/sensors`                | BME280 即時讀值                            |
| GET        | `/api/history`                | 對話記錄                                   |
| DELETE     | `/api/history`                | 清除對話記錄                               |
| POST       | `/api/chat`                   | 送出純文字，回傳 `{"ok":true,"reply":"…"}` |
| GET        | `/api/faces`                  | 人臉名稱清單                               |
| POST       | `/api/faces/add?name=小明`    | 用相機註冊人臉                             |
| POST       | `/api/faces/delete?name=小明` | 刪除人臉                                   |
| POST       | `/api/faces/export`           | 匯出 CSV                                   |
| GET        | `/api/wifi/scan`              | 掃描基地台                                 |
| POST       | `/api/wifi?ssid=…&password=…` | 連線並儲存                                 |
| POST       | `/api/wifi/clear`             | 清除 WiFi                                  |
| GET / POST | `/api/config`                 | 讀取 / 寫入 `config.json`                  |
| GET        | `/api/sd/list?path=/`         | SD 卡目錄（目錄優先排序）                  |
| GET        | `/api/sd/read?path=/a.txt`    | 讀取文字檔（≤ 96 KB）                      |
| GET        | `/api/sd/download?path=/a.txt`| 下載檔案                                   |
| POST       | `/api/sd/write`               | JSON `{path,data}` 寫入（覆蓋）            |
| POST       | `/api/sd/upload?path=/`       | multipart 上傳（邊收邊寫 SD 卡）           |
| POST       | `/api/sd/delete?path=…`       | 刪除檔案 / 空資料夾                        |
| POST       | `/api/sd/mkdir?path=…`        | 新增資料夾                                 |
| POST       | `/api/voice`                  | multipart WAV → Gemini 語音對話（舊版相容）|
| GET        | `/api/mic`                    | 板載麥克風開關狀態 `{on,ready}`            |
| POST       | `/api/mic?on=1\|0`            | 開啟 / 關閉板載 I2S 麥克風                 |
| POST       | `/api/mic/record`             | 用板載麥克風錄音並送 Gemini                |
| GET        | `/api/camera/frame`           | 相機單張 JPEG（即時影像輪詢）              |
| GET        | `/api/camera/face?recognize=1`| 偵測 / 辨識結果與框線座標（240 座標系）    |
| GET        | `/api/snapshot`               | 回傳一張 JPEG（舊版相容）                  |
| POST       | `/api/action?code=<n>`        | 觸發本機動作（見下表）                     |

動作代碼：

| code | 動作                  |
| ---- | --------------------- |
| 2    | 返回主選單（可中斷目前畫面） |
| 11   | 系統狀態              |
| 12   | 切換亮度              |
| 20   | WiFi 掃描             |
| 30   | 麥克風收音（AI 聊天） |
| 31   | 球體動畫              |
| 40   | 環境圖表              |
| 50   | 加入人臉              |
| 52   | 匯出人臉 CSV          |
| 53   | 人臉辨識              |
| 56   | TFT 相機串流影像      |
| 61   | 重新開機              |

---

## 程式架構

| 檔案                 | 負責                                                    |
| -------------------- | ------------------------------------------------------- |
| `TTGO_AI_Camera.ino` | 主流程、選單、各畫面、動作分派                          |
| `board_pins.h`       | 所有硬體接腳                                            |
| `display.*`          | ST7789 自訂驅動 + 240×240 PSRAM 全畫面緩衝 + 中文繪字   |
| `font.*`             | SD 卡字型檔載入與二分搜尋                               |
| `nav.*`              | RTC 記憶體選單狀態（跨重開機保存游標）                  |
| `app_config.*`       | `config.json` 讀寫（ArduinoJson）                       |
| `storage.*`          | 共用 HSPI 的 SD 卡存取                                  |
| `wifi_manager.*`     | STA / AP 連線、掃描                                     |
| `gemini.*`           | Gemini `generateContent`（文字 + 語音）、手動 HTTP 解析 |
| `audio.*`            | I2S 麥克風錄音、VAD、自動增益                           |
| `bme280.*`           | BME280 / BMP280 驅動、I2C 重綁與自動重試（不依賴 Adafruit） |
| `camera_face.*`      | OV2640、人臉偵測 / 註冊 / 辨識、CSV 存取                |
| `power.*`            | IP5306 電量讀取                                         |
| `webui.*`            | SPIFFS 靜態網頁、REST API、SD 上傳、語音與相機端點      |
| `TTGO_AI_Camera/data/` | 上傳到 SPIFFS 的前端：`index.html` / `style.css` / `app.js` |
| `serial_console.*`   | 序列埠指令                                              |

---

## 疑難排解

| 症狀                                  | 可能原因與處理                                                                                                      |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| 一直重啟（Guru Meditation / StoreProhibited） | 螢幕緩衝區或字型配置失敗，通常是 PSRAM 沒開。請確認 Tools → **PSRAM: Enabled**；程式已改為配置失敗時停用螢幕，不會再進入重啟循環 |
| 螢幕全黑                              | 確認 `display.brightness` 不是 0；執行「系統狀態 → 顯示」切換亮度                                                   |
| 瀏覽器顯示「找不到 /index.html」      | 還沒上傳 SPIFFS：Tools → **ESP32 Sketch Data Upload**（`data/` 要與 `.ino` 同層）；分區需為 `Huge APP` |
| 中文變成 `?`                          | SD 卡缺少 `/font.bin` 或格式錯誤，請重新複製 `sd_card/font.bin`                                                     |
| 完全沒有畫面但有序列輸出              | SD 卡未掛載會導致 SPI 匯流排沒初始化，請確認 SD 卡插好（TFT 與 SD 共用 SPI）                                        |
| 相機初始化失敗                        | 鏡頭排線鬆脫，或 PSRAM 未啟用（需選 `PSRAM: Enabled`）                                                              |
| 人臉辨識一直失敗                      | 光線不足、`camera.rec_threshold` 太高，或重新註冊（可調高 `enroll_samples`）                                        |
| 錄音沒聲音                            | 先確認網頁麥克風開關為 ON；麥克風在板子另一側，請靠近說話或調低 `audio.vad_threshold`                               |
| BME280 沒有數值                       | 看序列埠的 I2C 掃描結果，確認 SDA 18 / SCL 23 與 `bme280.enabled`；相機初始化後程式會自動重綁 I2C1，仍失敗每 3 秒重試 |
| 網頁即時影像黑畫面 / 卡住             | 先用 `/api/camera/frame` 確認單張影像；相機會自動重新初始化，仍失敗請重新整理頁面                                    |
| Gemini 回 `HTTP 400/403`              | API Key 錯誤、額度用完，或 `ai.model` 名稱不支援                                                                    |
| Gemini 回 `HTTP 429`                  | 免費額度限制，稍後再試                                                                                              |
| WiFi 連不上                           | 開機會自動進入 AP 模式，用手機連 `TTGO-AI-XXXX` 到 `http://192.168.4.1` 重設                                        |
| 編譯錯誤 `fd_forward.h: No such file` | ESP32 核心不是 1.0.6                                                                                                |
| 編譯錯誤「程式空間不足」              | Partition Scheme 沒選 `Huge APP (3MB No OTA)`                                                                       |
| 影像顏色異常（紅藍互換）              | 請將 `camera.vflip` / `hmirror` 之外，回報 RGB565 位元組順序問題；可先用網頁 `/api/snapshot` 確認 JPEG 顏色是否正常 |

---

## 注意事項

- 本程式使用 **PSRAM**（畫面緩衝 115 KB、字型 593 KB、錄音與 base64 緩衝），
  未啟用 PSRAM 將無法運作。
- 人臉特徵以自訂格式儲存（非核心的 `face_id_list`），因此可以單獨刪除 / 匯出 / 匯入。
- BME280 與相機共用 GPIO 18/23（I2C1 / I2C0），程式會在相機初始化後自動重綁 I2C1；
  讀取失敗會自動重試，仍找不到時每 3 秒重新偵測，並在序列埠印出 I2C 掃描結果。
- 相機以 **JPEG** 模式擷取（`camera.framesize` 預設 QVGA），網頁直接顯示 JPEG；
  TFT 預覽與人臉偵測才解碼成 RGB888→RGB565，即時影像比 RGB565 輪詢順暢許多。
- 錄音長度預設 6 秒，`audio.max_record_ms` 越大，PSRAM 與 base64 緩衝需求越高。
- 程式已佔用 96% 的 app 分區（3.02 MB / 3 MB）；前端網頁另外放在 SPIFFS，
  若要再新增大量功能，建議改為 ESP-IDF 專案或精簡功能。
- `display.auto_run` 預設 `false`：TFT 選單不再倒數自動執行，一切由網頁 / 序列埠觸發。

---

## 授權

本專案程式碼可自由使用與修改。字型檔來自
[GNU Unifont](https://unifoundry.com/unifont/)，授權為 GNU GPL v2+ with font embedding exception。
