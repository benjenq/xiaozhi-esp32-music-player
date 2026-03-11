# 支援播放 Subsonic API 串流音樂平台的小智 AI 聊天機器人

### 緣起

剛接觸「小智 AI 聊天機器人」時，對於網路上看到第三方支援網路串流音樂的版本很感興趣。不過找到的開源專案（例如 [Maggotxy/xiaozhi-esp32-music](https://github.com/Maggotxy/xiaozhi-esp32-music)）多為較舊的 v1.8.5，且串流音樂位址似乎已失效。

因此，我個人嘗試改寫成支援 Subsonic API 的版本。好處是能經由開源軟體，自行在內網架設串流音樂平台。然而完成後發現，部分歌曲會不定時當機，或在播放第二首時容易崩潰；即使在第三方原始碼基礎上調整相關程式碼，在個人有限的程式能力和 AI 協助下，仍無法徹底解決。

基於以上種種原因，決定從蝦哥的源代碼（[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)）開始進行二次開發，並參考先前第三方部分代碼，構建一套基於 v2.2.3 且能播放網路音樂串流的版本。

## 專案說明

基於 [78/xiaozhi-esp32 v2.2.3](https://github.com/78/xiaozhi-esp32/tree/b34a9b19baebfa17d8fcaa0ed494444aaba174e5) 的二次開發，支援播放 Subsonic API 串流音樂平台的聊天機器人，並支援
- 歌詞同步顯示。
- 連續播放模式：當取得的歌曲數量超過 1 首時，會隨機選歌播放。

***註：啟動裝置時為「單曲模式」，可用語音切換為「連續模式」。設定值不會儲存在本體。***

專案使用 `ESP-ADF` 開發套件中的 `audio_stream` 與 `audio_pipeline` 組件。因此編譯本專案時，編譯環境中必須安裝 `ESP-ADF`。

此外，ESP-ADF 需補完 `config AUDIO_BOARD_CUSTOM` 的設置，以及 ESP-IDF 也必須進行修正。

實測支援 ESP32-S3 與 ESP32-C5、ESP32-C6，不支援最早期的 ESP32（資源不足），其他晶片未測試。使用 ESP-IDF 版本為 v5.5.2/v5.5.3。

### 提醒事項

**1. Flash 容量建議 8MB 以上。**

**2. ESP32-C6 因效能有限，需做以下調整：**
- 關閉提詞喚醒功能，
- 顯示風格(`display style`) 選擇 `Enabled default message style`（不使用微信對話風格）。

否則 ESP32-C6 容易出現間音樂歇性斷音，突發中斷等情況。

**3. 串流音樂平台使用 https 連線時：**
- 未搭載使用 PSRAM 的裝置（如 ESP32-C6 ）可能無法順利播放。
- https 的 TLS 程序會消耗一定的 CPU 資源，實測 ESP32-S3 可能會出現偶爾播放斷音。

## 前提準備工作

- 您已具備編譯 [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32) 源代碼的能力。

- 環境內已經安裝 `ESP-ADF`，並且能成功編譯 `ESP-ADF` 內提供的 [`pipeline_http_mp3`](https://gitee.com/EspressifSystems/esp-adf/tree/master/examples/player/pipeline_http_mp3) 或其他範例（[**ESP-ADF 官方文件**](https://docs.espressif.com/projects/esp-adf/zh_CN/latest/get-started/index.html#vs-code-extension)）。

- 可以存取支援 Subsonic API 的網路音樂串流平台：
  - 可在內網使用免費開源的軟體（如 Navidrome、Gonic、Airsonic）架設音樂串流平台。
  - 或是互聯網上支援 Subsonic API 的網路音樂串流平台。

### 自行架設支援 Subsonic API 的網路音樂串流平台的補充：

建議使用 [Navidrome](https://www.navidrome.org/)，優點如下：

- 開源免費，支援多平台（Windows / macOS / Linux / 樹莓派...）

- 單一執行檔，免安裝，技術門檻極低，架站 3 分鐘搞定。另有 Docker 的安裝方法，可安裝於 NAS 主機。

- Navidrome 支援歌詞同步，歌詞形式可用 ID3 內嵌（`LYRICS` 或 `UNSYNCEDLYRICS` 欄位）或外掛 .lrc 檔案。外掛歌詞僅須將 .lrc 歌詞文件與 .mp3 放在同一目錄下，使用相同主檔名即可。

- Navidrome 依賴 mp3 音樂檔案的 ID3 標籤進行建檔與搜尋，請確保 artist（歌手）和 title（歌名）欄位填寫正確完整（可使用 [Mp3tag](https://www.mp3tag.de/en/download.html) 或其他 ID3 工具編輯）。

- 因語言模型偏好簡體中文，建議中文歌曲的 ID3 標籤使用簡體填寫，以免搜尋不到。
  
  *註：Navidrome 的內建 Web 播放器，僅支援 mp3 的 ID3 內嵌歌詞，不支援外部 .lrc 歌詞顯示。所以請勿使用 Navidrome 內建 Web 播放器測試外掛歌詞功能。*

## 如何使用本專案
使用 `git clone` 指令下載專案源代碼
```shell
git clone https://github.com/benjenq/xiaozhi-esp32-music-player.git
``` 

### 1. 修改代碼

所有的開發板（`waveshare-s3-touch-lcd-3.5b`除外）預設沒有引入播放串流音樂的功能，需在對應的開發板上進行少量代碼修改進行啟用。
- 可參考 [`waveshare-s3-touch-lcd-3.5b.cc`](main/boards/waveshare/esp32-s3-touch-lcd-3.5b/waveshare-s3-touch-lcd-3.5b.cc)）：

`#include`標頭段新增：

```cpp
#include "httpmp3_player.h"
```

類別宣告段 `class CustomBoard : public WifiBoard` （或根據您擁有開發板的宣告類別）新增：

```cpp
HttpMp3Player* music_player_ = nullptr;
```

新增播放器的初始化：

```cpp
    void InitializeTools(){
        music_player_ = new HttpMp3Player();
    }
```

開發板初始化引入播放器初始化程序（需參照不同的開發板的初始化結構）：

```cpp
    CustomBoard() : ...{
      ...
      InitializeTools();
      ...
    }
```

複寫 `GetMusicPlayer()` 方法：

```cpp
    virtual HttpMp3Player * GetMusicPlayer() override {
        return music_player_;
    }
```

### 2. 修改 menuconfig 內容：

- Subsonic API 伺服器位址：根據實際情況填寫有效的串流音樂平台網址，含埠號，以`/rest`結尾，例如 `http://192.168.0.101:4533/rest`
- Subsonic API 的基本參數：有兩種固定格式，擇一：
  - `u=帳號&p=密碼&s=raw&v=1.16.1&c=xiaozhi`
  - `u=帳號&s=任意字串&t=密碼結合任意字串的MD5生成碼&v=1.16.1&c=xiaozhi`

### 3. 小智機器人後台設定

智能體 - 角色配置 - 角色介紹，新增提示詞，

```text
收到音乐相关的需求时，只使用 MCP 工具 self.music.play_song，同时禁止使用 search_music 功能。播放成功时回覆播放讯息。
```

強迫機器人關閉內建的雲端音樂播放功能，執行指定的音樂流播放工具。

### 4. ESP-ADF 的補完，以及 ESP-IDF 的修正

#### 4.1  ESP-ADF 的修正：補完 `config AUDIO_BOARD_CUSTOM`

本專案使用 ESP-ADF 開發套件中兩個組件：`audio_stream` 與 `audio_pipeline`，並參考官方範例[`pipeline_http_mp3`](https://gitee.com/EspressifSystems/esp-adf/tree/master/examples/player/pipeline_http_mp3)。這個範例中，僅一百多行就處理了區塊下載、緩存管理、mp3 解碼、音頻採樣等複雜的工作。

雖然本專案只用到兩個組件，不過由於 ESP-ADF 組件的相依性，仍須引入 ESP-IDF 中的 `audio_board` 組件，否則專案會編譯失敗。

引入 `audio_board` 組件後，menuconfig 會出現 `Audio HAL - Audio board` 項目，必須選擇開發板，否則也會編譯失敗。

然而 `Audio board` 開發板清單又與小智 AI 專案的支援開發板清單不太相同，隨便選還會導致編譯失敗，所以必須「**補完 Custom audio board 的最少設定**」。概述如下：

1. `esp-adf/components/audio_board` 新增目錄 `esp_audio_board_custom`

2. 將 `esp-adf/components/audio_board/esp32_c6_devkit/` 底下的檔案複製到 `esp_audio_board_custom/` 目錄下

3. 修改 `esp-adf/components/audio_board/CMakeLists.txt` 新增：
   
   ```cmake
   if (CONFIG_AUDIO_BOARD_CUSTOM)
   message(STATUS "Current board name is " CONFIG_AUDIO_BOARD_CUSTOM)
   list(APPEND COMPONENT_ADD_INCLUDEDIRS ./esp_audio_board_custom)
   set(COMPONENT_SRCS
   ./esp_audio_board_custom/board.c
   ./esp_audio_board_custom/board_pins_config.c
   )
   endif()
   ```

4. 修改 `esp_audio_board_custom/` 目錄內的所有有關 GPIO PIN 的定義內容：
   
   - `GPIO_NUM_數字` 全改為 `GPIO_NUM_NC`
   - `board_def.h` 內補足 `ESP_SD_PIN_D0` ~ `ESP_SD_PIN_D7` ，以及 `ESP_SD_PIN_CD` 與 `ESP_SD_PIN_WP`。

5. 編譯時 `Audio HAL - Audio board` 選擇 `Custom audio board`。

以上操作可避免編譯 ESP-ADF 的 audio_board 組件時發生錯誤。

#### 4.2 ESP-IDF 的修正

ESP-ADF 的某些組件會用到 ESP-IDF 中不存在的方法，所以 ESP-IDF 需要修正，否則組件的功能可能會發生異常。修正的指令位於 `esp-adf/idf_patches` 內：

- 須根據 ESP-IDF 的版本，選擇對應的修正指令。
- 修正的指令為 *(以下是 macOS 環境變數為例)*：
  
  ```shell
  cd $IDF_PATH
  git apply --ignore-space-change $ADF_PATH/idf_patches/idf_vX.X_freertos.patch
  ```
  X.X 為 ESP-IDF 的版本。個人實測 ESP-IDF v5.5.2 版有效。

### 逐一完成上述的操作，便可編譯和刷寫本專案韌體。

## 使用操作

播放音樂：說出自然語言命令

```text
我想聽周董的歌
播放五月天的音樂
播放周杰倫的花海
...
```

播放模式切換：
- 使用`「設置單曲模式」`、`「設置連播模式」`等類似命令切換。
- 有時 AI 會假回覆但其實沒有調用 MCP，命令加上「工具」可改善這個問題，如`「工具設置連播模式」`。

單曲模式：
- 播放一首歌結束後，會進入聆聽命令模式，等待語音命令。

連續播放模式：
- 只要有找到歌，就會一直播放。音樂超過兩首時，會隨機選取。
- 用 BOOT 按鈕中斷連續播放。

## 其他說明

從 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 修改的內容如下：

新增代碼文件：

| 文件名稱                                 | 說明      |
| ------------------------------------ | ------- |
| main/boards/common/music_player.h    | 播放器虛擬介面 |
| main/boards/common/httpmp3_player.h  | 播放器程序   |
| main/boards/common/httpmp3_player.cc | 同上      |

修改代碼文件：

| 文件名稱                                               | 說明                                                                         |
| ----------------------------------------------------- | ---------------------------------------------------------------------------- |
| main/assets/locales/zh-CN,zh-TW,en-US/language.json   | 新增音樂播放提示多語系                                                           |
| sdkconfig.defaults.xxxx                               | 新增 CONFIG_FREERTOS_ENABLE_BACKWARD_COMPATIBILITY=y ，編譯 ESP-ADF 必要。      |
| idf_component.yml                                     | 新增 bblanchon/arduinojson: ^7.4.3                                            |
| CMakeLists.txt                                        | 新增 ESP-ADF 組件                                                              |
| main/CMakeLists.txt                                   | 加入播放器源碼，並搭配根目錄 CMakeLists.txt 的相關修改                              |
| main/application.cc                                   | 切換聊天狀態時，停止音樂播放                                                      |
| main/Kconfig.projbuild                                | 加入 Subsonic API 相關項目，修改串流音樂位址不需修改代碼                            |
| main/audio/audio_codec.h                              | 新增切換音樂和語音 Sample Rate 的功能                                            |
| main/audio/audio_codec.cc                             | 同上                                                                          |
| main/audio/audio_service.cc                           | 中斷音頻輸出加入音樂播放判定，避免播放音樂時被系統中斷輸出                             |
| main/boards/common/board.h                            | 新增音樂播放器的虛擬介面                                                         |
| main/boards/common/board.cc                           | 同上                                                                          |
| main/boards/common/power_save_timer.cc                | 啟用電源管理程序時，播放音樂不進入省電模式判定                                       |
| main/boards/common/sleep_timer.cc                     | 啟用裝置睡眠模式時，播放音樂不進入省電模式判定                                       |

[這裡](https://github.com/benjenq/xiaozhi-esp32-music-player/commit/6ec4ef7fcc7d4a1b56a6a167324ef183fa456824)可以查看具體修改了哪些部分。

---

# An MCP-based Chatbot

（[简体中文](README_zh.md) | 繁體中文 | [English](README_en.md) | [日本語](README_ja.md)）

## 介绍

👉 [人类：给 AI 装摄像头 vs AI：当场发现主人三天没洗头【bilibili】](https://www.bilibili.com/video/BV1bpjgzKEhd/)

👉 [手工打造你的 AI 女友，新手入门教程【bilibili】](https://www.bilibili.com/video/BV1XnmFYLEJN/)

小智 AI 聊天机器人作为一个语音交互入口，利用 Qwen / DeepSeek 等大模型的 AI 能力，通过 MCP 协议实现多端控制。

<img src="docs/mcp-based-graph.jpg" alt="通过MCP控制万物" width="320">

### 版本说明

当前 v2 版本与 v1 版本分区表不兼容，所以无法从 v1 版本通过 OTA 升级到 v2 版本。分区表说明参见 [partitions/v2/README.md](partitions/v2/README.md)。

使用 v1 版本的所有硬件，可以通过手动烧录固件来升级到 v2 版本。

v1 的稳定版本为 1.9.2，可以通过 `git checkout v1` 来切换到 v1 版本，该分支会持续维护到 2026 年 2 月。

### 已实现功能

- Wi-Fi / ML307 Cat.1 4G
- 离线语音唤醒 [ESP-SR](https://github.com/espressif/esp-sr)
- 支持两种通信协议（[Websocket](docs/websocket.md) 或 MQTT+UDP）
- 采用 OPUS 音频编解码
- 基于流式 ASR + LLM + TTS 架构的语音交互
- 声纹识别，识别当前说话人的身份 [3D Speaker](https://github.com/modelscope/3D-Speaker)
- OLED / LCD 显示屏，支持表情显示
- 电量显示与电源管理
- 支持多语言（中文、英文、日文）
- 支持 ESP32-C3、ESP32-S3、ESP32-P4 芯片平台
- 通过设备端 MCP 实现设备控制（音量、灯光、电机、GPIO 等）
- 通过云端 MCP 扩展大模型能力（智能家居控制、PC桌面操作、知识搜索、邮件收发等）
- 自定义唤醒词、字体、表情与聊天背景，支持网页端在线修改 ([自定义Assets生成器](https://github.com/78/xiaozhi-assets-generator))

## 硬件

### 面包板手工制作实践

详见飞书文档教程：

👉 [《小智 AI 聊天机器人百科全书》](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb?from=from_copylink)

面包板效果图如下：

![面包板效果图](docs/v1/wiring2.jpg)

### 支持 70 多个开源硬件（仅展示部分）

- <a href="https://oshwhub.com/li-chuang-kai-fa-ban/li-chuang-shi-zhan-pai-esp32-s3-kai-fa-ban" target="_blank" title="立创·实战派 ESP32-S3 开发板">立创·实战派 ESP32-S3 开发板</a>
- <a href="https://github.com/espressif/esp-box" target="_blank" title="乐鑫 ESP32-S3-BOX3">乐鑫 ESP32-S3-BOX3</a>
- <a href="https://docs.m5stack.com/zh_CN/core/CoreS3" target="_blank" title="M5Stack CoreS3">M5Stack CoreS3</a>
- <a href="https://docs.m5stack.com/en/atom/Atomic%20Echo%20Base" target="_blank" title="AtomS3R + Echo Base">M5Stack AtomS3R + Echo Base</a>
- <a href="https://gf.bilibili.com/item/detail/1108782064" target="_blank" title="神奇按钮 2.4">神奇按钮 2.4</a>
- <a href="https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.8.htm" target="_blank" title="微雪电子 ESP32-S3-Touch-AMOLED-1.8">微雪电子 ESP32-S3-Touch-AMOLED-1.8</a>
- <a href="https://github.com/Xinyuan-LilyGO/T-Circle-S3" target="_blank" title="LILYGO T-Circle-S3">LILYGO T-Circle-S3</a>
- <a href="https://oshwhub.com/tenclass01/xmini_c3" target="_blank" title="虾哥 Mini C3">虾哥 Mini C3</a>
- <a href="https://oshwhub.com/movecall/cuican-ai-pendant-lights-up-y" target="_blank" title="Movecall CuiCan ESP32S3">璀璨·AI 吊坠</a>
- <a href="https://github.com/WMnologo/xingzhi-ai" target="_blank" title="无名科技Nologo-星智-1.54">无名科技 Nologo-星智-1.54TFT</a>
- <a href="https://www.seeedstudio.com/SenseCAP-Watcher-W1-A-p-5979.html" target="_blank" title="SenseCAP Watcher">SenseCAP Watcher</a>
- <a href="https://www.bilibili.com/video/BV1BHJtz6E2S/" target="_blank" title="ESP-HI 超低成本机器狗">ESP-HI 超低成本机器狗</a>

<div style="display: flex; justify-content: space-between;">
  <a href="docs/v1/lichuang-s3.jpg" target="_blank" title="立创·实战派 ESP32-S3 开发板">
    <img src="docs/v1/lichuang-s3.jpg" width="240" />
  </a>
  <a href="docs/v1/espbox3.jpg" target="_blank" title="乐鑫 ESP32-S3-BOX3">
    <img src="docs/v1/espbox3.jpg" width="240" />
  </a>
  <a href="docs/v1/m5cores3.jpg" target="_blank" title="M5Stack CoreS3">
    <img src="docs/v1/m5cores3.jpg" width="240" />
  </a>
  <a href="docs/v1/atoms3r.jpg" target="_blank" title="AtomS3R + Echo Base">
    <img src="docs/v1/atoms3r.jpg" width="240" />
  </a>
  <a href="docs/v1/magiclick.jpg" target="_blank" title="神奇按钮 2.4">
    <img src="docs/v1/magiclick.jpg" width="240" />
  </a>
  <a href="docs/v1/waveshare.jpg" target="_blank" title="微雪电子 ESP32-S3-Touch-AMOLED-1.8">
    <img src="docs/v1/waveshare.jpg" width="240" />
  </a>
  <a href="docs/v1/lilygo-t-circle-s3.jpg" target="_blank" title="LILYGO T-Circle-S3">
    <img src="docs/v1/lilygo-t-circle-s3.jpg" width="240" />
  </a>
  <a href="docs/v1/xmini-c3.jpg" target="_blank" title="虾哥 Mini C3">
    <img src="docs/v1/xmini-c3.jpg" width="240" />
  </a>
  <a href="docs/v1/movecall-cuican-esp32s3.jpg" target="_blank" title="CuiCan">
    <img src="docs/v1/movecall-cuican-esp32s3.jpg" width="240" />
  </a>
  <a href="docs/v1/wmnologo_xingzhi_1.54.jpg" target="_blank" title="无名科技Nologo-星智-1.54">
    <img src="docs/v1/wmnologo_xingzhi_1.54.jpg" width="240" />
  </a>
  <a href="docs/v1/sensecap_watcher.jpg" target="_blank" title="SenseCAP Watcher">
    <img src="docs/v1/sensecap_watcher.jpg" width="240" />
  </a>
  <a href="docs/v1/esp-hi.jpg" target="_blank" title="ESP-HI 超低成本机器狗">
    <img src="docs/v1/esp-hi.jpg" width="240" />
  </a>
</div>

## 软件

### 固件烧录

新手第一次操作建议先不要搭建开发环境，直接使用免开发环境烧录的固件。

固件默认接入 [xiaozhi.me](https://xiaozhi.me) 官方服务器，个人用户注册账号可以免费使用 Qwen 实时模型。

👉 [新手烧录固件教程](https://ccnphfhqs21z.feishu.cn/wiki/Zpz4wXBtdimBrLk25WdcXzxcnNS)

### 开发环境

- Cursor 或 VSCode
- 安装 ESP-IDF 插件，选择 SDK 版本 5.4 或以上
- Linux 比 Windows 更好，编译速度快，也免去驱动问题的困扰
- 本项目使用 Google C++ 代码风格，提交代码时请确保符合规范

### 开发者文档

- [自定义开发板指南](docs/custom-board.md) - 学习如何为小智 AI 创建自定义开发板
- [MCP 协议物联网控制用法说明](docs/mcp-usage.md) - 了解如何通过 MCP 协议控制物联网设备
- [MCP 协议交互流程](docs/mcp-protocol.md) - 设备端 MCP 协议的实现方式
- [MQTT + UDP 混合通信协议文档](docs/mqtt-udp.md)
- [一份详细的 WebSocket 通信协议文档](docs/websocket.md)

## 大模型配置

如果你已经拥有一个小智 AI 聊天机器人设备，并且已接入官方服务器，可以登录 [xiaozhi.me](https://xiaozhi.me) 控制台进行配置。

👉 [后台操作视频教程（旧版界面）](https://www.bilibili.com/video/BV1jUCUY2EKM/)

## 相关开源项目

在个人电脑上部署服务器，可以参考以下第三方开源的项目：

- [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) Python 服务器
- [joey-zhou/xiaozhi-esp32-server-java](https://github.com/joey-zhou/xiaozhi-esp32-server-java) Java 服务器
- [AnimeAIChat/xiaozhi-server-go](https://github.com/AnimeAIChat/xiaozhi-server-go) Golang 服务器
- [hackers365/xiaozhi-esp32-server-golang](https://github.com/hackers365/xiaozhi-esp32-server-golang) Golang 服务器

使用小智通信协议的第三方客户端项目：

- [huangjunsen0406/py-xiaozhi](https://github.com/huangjunsen0406/py-xiaozhi) Python 客户端
- [TOM88812/xiaozhi-android-client](https://github.com/TOM88812/xiaozhi-android-client) Android 客户端
- [100askTeam/xiaozhi-linux](http://github.com/100askTeam/xiaozhi-linux) 百问科技提供的 Linux 客户端
- [78/xiaozhi-sf32](https://github.com/78/xiaozhi-sf32) 思澈科技的蓝牙芯片固件
- [QuecPython/solution-xiaozhiAI](https://github.com/QuecPython/solution-xiaozhiAI) 移远提供的 QuecPython 固件

## 关于项目

这是一个由虾哥开源的 ESP32 项目，以 MIT 许可证发布，允许任何人免费使用，修改或用于商业用途。

我们希望通过这个项目，能够帮助大家了解 AI 硬件开发，将当下飞速发展的大语言模型应用到实际的硬件设备中。

如果你有任何想法或建议，请随时提出 Issues 或加入 [Discord](https://discord.gg/bXqgAfRm) 或 QQ 群：1011329060

## Star History

<a href="https://star-history.com/#78/xiaozhi-esp32&Date">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date&theme=dark" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
   <img alt="Star History Chart" src="https://api.star-history.com/svg?repos=78/xiaozhi-esp32&type=Date" />
 </picture>
</a>
