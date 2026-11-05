# 燒錄與上傳檢查清單（Arduino IDE 1.8.19）

適用：`TTGO_T_Camera_Plus_ChatBot`（TTGO T-Camera Plus：ESP32 + ST7789 240x240 + microSD + I2S 麥克風）。
照順序打勾，就能從全新環境走到「網頁打得開、AI 回得了話」。

---

## 0. 實測基準數字

| 項目 | 實測值 | 意義 |
| --- | --- | --- |
| 韌體大小 | 1,326,254 bytes（3,145,728 的 42%） | app 分區必須大於 1.27 MB |
| 靜態 RAM | 53,384 bytes（16%） | 還剩 274,296 bytes 給區域變數 |
| `data/` 總量 | 598,210 bytes（4 個檔） | 其中 `font_tc16.bin` 526,012 bytes |
| SPIFFS 映像 | 917,504 bytes（0xE0000） | 等於 `huge_app` 的 spiffs 分區大小 |
| 序列埠速率 | 115200 | 開機 log |

> 數字是在本機用 esp32 core **3.3.12** 編出來的。換核心版本或換字型後，請重看第 3 步的輸出。

---

## 1. 安裝（只做一次）

- [ ] Arduino IDE **1.8.19**
- [ ] 開發板管理員：`esp32 by Espressif Systems` → **2.0.17**（README 建議值）；本專案在 **3.3.12** 也編譯通過
- [ ] 函式庫管理員：`TFT_eSPI` **2.5.43**、`ArduinoJson` **6.21 以上**（7.x 可用，會有 deprecated 警告）
- [ ] SPIFFS 上傳外掛：確認存在 `Documents\Arduino\tools\ESP32FS\tool\esp32fs.jar`
      （沒有就裝 `arduino-esp32fs-plugin`，或直接用第 4 步的 CLI 方式）
- [ ] **不要**改 `Documents\Arduino\libraries\TFT_eSPI\User_Setup.h`：
      面板設定在專案的 `tft_setup.h`，TFT_eSPI 透過 `TFT_eSPI.h` 的 `__has_include(<tft_setup.h>)` 自動載入。
      若你之前手動改過該檔，請確認 `TFT_CS 12`、`TFT_DC 15`、`TFT_MOSI 19`、`TFT_SCLK 21`、`TFT_BL 2`

---

## 2. 工具選單設定（最常出錯，逐項確認）

| 選單 | 值 | 為什麼 |
| --- | --- | --- |
| 開發板 | **ESP32 Dev Module**（core 2.0.17 也可選「ESP32 Wrover Module」） | core 3.x 已移除 Wrover 板；Dev Module + PSRAM 就等價 |
| Flash Size | **4MB (32Mb)** | 板子實際 4 MB |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** | 預設分區的 app 只有 0x140000 = 1,310,720 bytes，塞不下 1,326,254 的韌體 |
| PSRAM | **Enabled** | 字型 526 KB、錄音與 HTTP 緩衝都用 `ps_malloc`，關掉會配置失敗 |
| CPU Frequency | 240MHz | 表情動畫與 SPI 刷新 |
| Upload Speed | 921600（不穩就降 460800） | — |
| Port | 板子對應的 COMx | 上傳與序列埠監視器都用它 |

`huge_app` 的實際配置（可在 core 的 `tools/partitions/huge_app.csv` 核對）：

| 分區 | Offset | Size | 換算 |
| --- | --- | --- | --- |
| app0 | 0x10000 | 0x300000 | 3 MB（韌體 1.27 MB） |
| spiffs | 0x310000 | 0xE0000 | 896 KB（`data/` 598 KB ≈ 65%） |

> 選單標籤寫「1MB SPIFFS」，實際可用是 **896 KB**，目前還剩約 300 KB。

---

## 3. 先編譯，確認 0 錯誤

- [ ] 開啟 `TTGO_T_Camera_Plus_ChatBot.ino`，按 **Ctrl+R**（Verify）
- [ ] 結尾應該像這樣：

```
Sketch 使用 1326254 位元組（42%）的程式儲存空間。最大為 3145728 位元組
全域變數使用 53384 位元組 (16%) 的動態記憶體, 保留 274296 位元組給區域變數.
```

- [ ] 可接受的警告：ArduinoJson 的 `DynamicJsonDocument` deprecated、TFT_eSPI 的 `TOUCH_CS pin not defined`
- [ ] 不可出現：`text section exceeds available space in board`（分區錯，回第 2 步）、
      `TFT_eSPI.h: No such file or directory`（函式庫沒裝）、`ledcSetup was not declared`（核心版本問題）

命令列交叉驗證（本機實測通過）：

```bash
cd "C:/Users/Len/Documents/1.GitHub/TTGO_T-Camera_Plus/TTGO_T_Camera_Plus_ChatBot"
arduino-cli compile --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app,PSRAM=enabled,FlashFreq=80" --libraries ~/Documents/Arduino/libraries .
```

---

## 4. 上傳網頁到 SPIFFS（`data/`）

**沒做這步，裝置只會回一頁「尚未上傳網頁介面」。**

- [ ] **先關閉序列埠監視器**（佔用 COM 埠會讓上傳失敗）
- [ ] Tools → **ESP32 Sketch Data Upload** → 等到 `SPIFFS Image Uploaded`
- [ ] 若選單不存在或按了沒反應（2021 年版外掛在 core 3.x 有時抓不到分區），改用 CLI。
      以下指令已在本機實測產出正確的 917,504 bytes 映像：

```bash
cd "C:/Users/Len/Documents/1.GitHub/TTGO_T-Camera_Plus/TTGO_T_Camera_Plus_ChatBot"
MK="$LOCALAPPDATA/Arduino15/packages/esp32/tools/mkspiffs/0.2.3/mkspiffs.exe"
"$MK" -c data -b 4096 -p 256 -s 0xE0000 /tmp/spiffs.bin

ET="$LOCALAPPDATA/Arduino15/packages/esp32/tools/esptool_py/5.3.1/esptool.exe"
"$ET" --chip esp32 --port COM5 --baud 921600 write_flash 0x310000 /tmp/spiffs.bin
```

  - `0x310000` 只在 **huge_app** 分區下正確；換分區請查該 csv 的 spiffs Offset。
  - 前提是韌體（含分區表）已經燒過一次；全新板子先做第 6 步，再回來補這步。
- [ ] 驗證：開機 log 出現 `[fs] SPIFFS ok ...` 與 `[font] loaded 13842 glyphs, 526012 bytes`

---

## 5. 準備 microSD 卡

- [ ] 檔案系統 **FAT32**、MBR 分割、容量 **32 GB 以下**（8–32 GB 最保險）
- [ ] 卡**根目錄**放 `config.json`：把專案的 `sd_card/config.json` 複製過去
- [ ] 不必先建 `/voice/`，第一次錄音時韌體會自動建立
- [ ] 開機前先插好卡（熱插拔不保證偵測得到）
- [ ] 注意：SD 的 CS 是 **GPIO0**（開機 strapping）。插著卡若一直重開機 → 拔卡開機，插回卡後重新整理網頁

沒有卡也能跑（設定退回 SPIFFS，SD 頁停用）。卡上沒有 `config.json` 時，開機 log 會出現
`[cfg] no config.json found, creating defaults` 並自動寫入一份預設值。

---

## 6. 上傳韌體

- [ ] 選好 Port → **Ctrl+U**
- [ ] 看到 `Connecting....` → `Writing at 0x00010000...` → `Hash of data verified.` → `Hard resetting via RTS pin...`
- [ ] 卡在 `Connecting....` 時依序試：
      1. 拔掉 SD 卡再上傳（GPIO0 被卡拉低會進不了 bootloader）
      2. 手動進 bootloader：按住 **BOOT** → 按一下 **RST** → 放開 BOOT → 再上傳
      3. Upload Speed 降到 460800
      4. 換一條確定有資料線的 USB 線

---

## 7. 開機驗收（序列埠監視器 115200）

- [ ] 先出現版本橫幅，接著是（IP 與位元組數依你的環境而定）：

```
[tft] init ST7789 240x240
[fs] mounting SPIFFS ...
[fs] SPIFFS ok  ... bytes used
[fs] mounting SD card (attempt 1) ...
[fs] SD ok  /sd  ... bytes used
[cfg] loaded /sd/config.json (1234 bytes)
[font] loaded 13842 glyphs, 526012 bytes
[wifi] AP t-camera-plus-XXXX (ok) ap_ip=192.168.4.1
[web] http://192.168.1.23/  (AP http://192.168.4.1/)
[boot] ready
```

判讀：

| Log | 意思 | 處理 |
| --- | --- | --- |
| `[font] ... not found in SPIFFS - upload the data folder!` | 第 4 步沒做，或分區不是 huge_app | 重做第 4 步 |
| `[fs] SD mount FAILED - running without a card` | 卡沒插好／不是 FAT32／GPIO0 被拉低 | 重插或換卡 |
| `[cfg] no config.json found, creating defaults` | 卡上沒有設定檔 | 正常，會自動建立 |
| `[wifi] no station credentials, AP only` | 還沒設定家用 Wi-Fi | 正常，先用熱點設定 |
| `[boot] ready` 但螢幕全黑 | 背光或亮度 | 確認 `tft_setup.h` 的 `TFT_BL 2`，且 `brightness` 不是 0 |

---

## 8. 網頁功能驗收

連熱點 `t-camera-plus-XXXX`（密碼 `12345678`）→ <http://192.168.4.1/>

- [ ] 系統頁：Wi-Fi 資訊、SPIFFS 容量、**4 個檔案**（`index.html`、`app.js`、`style.css`、`font_tc16.bin`）
- [ ] SD 卡頁：列出根目錄、新增資料夾、改名、刪除、下載、拖拉上傳
- [ ] Wi-Fi 頁：掃描得到 SSID、輸入密碼可連線、「清除」可刪掉憑證
- [ ] AI 設定：provider／model／API Key／Ollama 存得下去（重新整理後仍在）
- [ ] 聊天頁：送「你好」→ 螢幕出現思考表情 → 回覆是繁體中文
- [ ] 語音：按 🎙️ 錄音後，SD 卡出現 `/voice/voice.wav`
- [ ] 表情測試：8 種表情都切得動

---

## 9. 之後的更新流程

| 改了什麼 | 要做的事 |
| --- | --- |
| 只改 `data/`（網頁、字型） | 重做第 4 步；韌體不必重燒 |
| 只改程式（`.ino`／`.cpp`／`.h`） | 上傳韌體；SPIFFS 與 SD 內容都保留 |
| 改了分區設定 | 韌體 + SPIFFS **都**要重燒（spiffs 的 Offset／大小會變） |
| 新增 config 欄位 | 上傳韌體後，到網頁按一次「儲存」才會寫回 SD 的 `config.json`；舊檔缺欄位時會先用預設值運作 |
| 換一張 SD 卡 | 把 `sd_card/config.json` 複製到新卡根目錄 |

---

## 10. 錯誤對照表

| 訊息／現象 | 原因 | 處理 |
| --- | --- | --- |
| `text section exceeds available space in board` | 分區不是 Huge APP | 回第 2 步 |
| `SPIFFS Image Uploaded` 但網頁仍是提示頁 | 分區換過，映像位置不符 | 用第 4 步 CLI 的 `0x310000` 重燒，或先燒韌體 |
| `A fatal error occurred: Failed to connect to ESP32` | 進不了 bootloader | 拔 SD 卡、手動 BOOT+RST、降速 |
| 編譯後百分比遠大於 42%（例如超過 100%） | 選到 1.2 MB app 分區 | 換 Huge APP |
| 網頁很慢、API 一直轉圈 | 沒插 SD 卡，SD 相關 API 在等逾時 | 插卡，或先不開 SD 頁 |
| 中文顯示空白 | `font_tc16.bin` 沒上傳 | 重做第 4 步 |
| 顏色相反或方向不對 | 面板差異 | 改 `tft_setup.h` 或網頁系統頁的旋轉設定 |
