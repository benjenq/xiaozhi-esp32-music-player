#include "httpmp3_player.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "application.h"
#include <esp_log.h>
#include "assets/lang_config.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "http_stream.h"
#include "mp3_decoder.h"
#include "raw_stream.h"
#include <esp_crt_bundle.h> //當串流平台是 https 時，http_stream 使用 TLS

#include "mcp_server.h"

#define TAG "HttpMp3Player"

const int pipeline_task_prio_ = 10;
const std::string base_url = CONFIG_SUBSONICAPI_URL;
const std::string subsonic_api_para = CONFIG_SUBSONICAPI_PARA; //"u=admin&p=1111&s=raw&v=1.16.1&c=xiaozhi";

bool http_get_response(std::string& full_url, std::string& response, std::string& query_result);

HttpMp3Player::HttpMp3Player(){
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool("self.music.play_song",
            "播放指定的歌曲。当用户要求播放音乐时使用此工具，会自动获取歌曲详情并开始流式播放。\n"
            "参数说明:\n"
            "  `song_name`: 要播放的歌曲名称（可选，默认为空字符串）。\n"
            "  `artist_name`: 要播放的主唱者或艺术家名称（可选，默认为空字符串）。\n"
            "使用规则:\n"
            "  用户未提到明确的主唱、艺术家、歌曲名称，相关栏位以空字串符替代。\n"
            "返回:\n"
            "  播放状态信息，不需确认，立刻播放歌曲。\n"
            "范例:\n"
            "  '播放五月天的倔强'\n"
            "  '我想听周董的歌'\n"
            "  '帮我随便挑一首歌，歌名歌手随意'\n", 
        PropertyList({
                 Property("song_name", kPropertyTypeString),//歌曲名称（必需）
                 Property("artist_name", kPropertyTypeString, "")//艺术家名称（可选，默认为空字符串）
        }), 
        [this](const PropertyList& properties) -> ReturnValue {
            ESP_LOGW(TAG, "MCP 執行 Http-Mp3-Player ");
            auto song_name = properties["song_name"].value<std::string>();
            auto artist_name = properties["artist_name"].value<std::string>();
            std::string message;
            if (!this->QuerySong(song_name, artist_name, message)) {
                return "{\"success\": false, \"message\": \"获取音乐资源失败\"}";
            }
            ESP_LOGI(TAG, "Music details result: %s", message.c_str());
            return "{\"success\": true, \"message\": \"" +  message + "\"}";
    });
    ESP_LOGI(TAG, "HttpMp3Player with MCP Tools `self.music.play_song` created.");
    //播放模式設定
    mcp_server.AddTool("self.music.set_play_mode",
            "装置支援音乐播放，此工具可设置本设备对应的播放模式，可以选择单曲播放模式(播放一首后停止)或连续播放模式(持续播放不同歌曲)。\n"
            "参数:\n"
            "  `playmode`: 播放模式，可选值为 'single'(单曲）或 'continuous'（连续）。\n"
            "返回:\n"
            "  设置结果信息。\n"
            "使用規則:\n"
            "  当用户提到'设定播放模式'或类似需求时时使用，用户需求可参照范例。\n"
            "范例:\n"
            "  '单曲模式'\n"
            "  '单曲播放'\n"
            "  '设置单曲模式'\n"
            "  '设定单曲模式'\n"
            "  '单曲播放'\n"
            "  '设置连播模式'\n"
            "  '设定轮播模式'\n"
            "  '连续模式'\n"
            "  '连续播放'\n"
            "  '循环模式'\n"
            "  '连播模式'\n",
            PropertyList({
                Property("playmode", kPropertyTypeString)//播放模式: "single" 或 "continuous"
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                auto mode_str = properties["playmode"].value<std::string>();
                // 转换为小写以便比较
                std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);
                
                if (mode_str == "single" || mode_str == "單曲") {
                    // 设置为频谱显示模式
                    play_mode_ = PlayModeSingle;
                    return "{\"success\": true, \"message\": \"已切换到單曲模式\"}";
                } else if (mode_str == "continuous" || mode_str == "连续" || mode_str == "连播" ||mode_str == "循环") {
                // 设置为歌词显示模式
                    play_mode_ = PlayModeContinuous;
                    return "{\"success\": true, \"message\": \"已切换到连续播放模式。\"}";
                    } else {
                    return "{\"success\": false, \"message\": \"无效的音樂播放模式，请使用 'single' 或 'continuous'\"}";
                    }
                
                return "{\"success\": false, \"message\": \"设置播放模式失败。\"}";
            });
    ESP_LOGI(TAG, "HttpMp3Player with MCP Tools `self.music.set_play_mode` created.");
}

HttpMp3Player::~HttpMp3Player(){
    
}

bool HttpMp3Player::PauseResume() {
    //尚未實作
    return true;
}
bool HttpMp3Player::Stop() {
    ESP_LOGW(TAG, "嘗試停止播放音樂...");
    if(is_playing_){
        stop_flag_ = true;
    }
    return true;
}
bool HttpMp3Player::IsPlaying() {
    return is_playing_;
}

// URL编码函数
static std::string url_encode(const std::string &str)
{
    std::string encoded;
    char hex[4];

    for (size_t i = 0; i < str.length(); i++)
    {
        unsigned char c = str[i];

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += c;
        }
        else if (c == ' ')
        {
            encoded += '+'; // 空格编码为'+'或'%20'
        }
        else
        {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

//解析 url ，賦予 protocol 值 http / https
bool parse_url_protocol(const std::string &url, std::string &protocol){
    protocol.clear();   // 先清空輸出參數
    size_t protocol_end = url.find("://");
    if (protocol_end == std::string::npos) {
        ESP_LOGE(TAG, "Invalid URL format: %s", url.c_str());
        return false;
    }
    protocol = url.substr(0, protocol_end);
    std::transform(protocol.begin(), protocol.end(), protocol.begin(), ::tolower);
    if (protocol != "http" && protocol != "https") {     //支援 http 與 https
        ESP_LOGE(TAG, "Unsupported protocol: %s", protocol.c_str());
        return false;
    }
    return true;
}

bool HttpMp3Player::QuerySong(const std::string& song_name, const std::string& artist_name, std::string& query_result){
    ESP_LOGI(TAG, "查詢歌手: %s, 歌曲名: %s", artist_name.c_str(), song_name.c_str());
    // 經查詢結果產生歌曲資訊 current_music_info_
    if (!create_music_info(song_name,artist_name,query_result)){
        ESP_LOGW(TAG, "create_music_info 錯誤！");
        return false;
    }

    current_music_info_.mp3_url =  base_url + "/stream.view?" + subsonic_api_para + "&id=" + url_encode(current_music_info_.song_id) ;

    std::string lyric_response;
    std::string full_query_lyric_url = base_url + "/getLyricsBySongId.view?" + subsonic_api_para + "&f=json&id=" + url_encode(current_music_info_.song_id);
    if (http_get_response(full_query_lyric_url, lyric_response, query_result)){
        if(!parse_response_to_lyric(lyric_response, query_result)){
            ESP_LOGE(TAG, "歌詞回應解析失敗！");
        }
    }
    else{
        ESP_LOGE(TAG, "取得歌詞回應失敗！");
    }

    ESP_LOGI(TAG, "開始播放歌曲: %s, 歌手: %s", current_music_info_.title.c_str(), current_music_info_.artist.c_str());
    if(play_mode_ == PlayModeContinuous){
        query_result = "開始隨機播放歌曲:《" + current_music_info_.title + "》等 " + std::to_string(playlists.size()) + " 首歌。";
        auto *display = Board::GetInstance().GetDisplay();
        auto &app = Application::GetInstance();
        //std::string msg = "《找到 " + std::to_string((int)playlists.size()) + " 首歌》";
        char msg[30];
        snprintf(msg, sizeof(msg), Lang::Strings::NUM_SONGS_FOUND, playlists.size());
        app.Schedule([display, msg]() {
            display->SetChatMessage("assistant", msg);
        });
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    else{
        query_result = "開始播放歌曲: " + current_music_info_.title + " ,歌手: " + current_music_info_.artist;
    }
    bool success = Play();
    if(!success){
        ESP_LOGE(TAG, "Play() 失敗！");
        return false;
    }
    return true;
}

/**
 * @brief 透過 HTTP 建立 current_music_info_ 與 playlists
 *
 * 這個函式使用提供的 Http 物件，對指定 URL 發出 GET 請求，
 * 並將回傳內容寫入 response 和 query_result。
 *
 * @param http          已建立的 Http 物件引用
 * @param full_url      要請求的完整 URL
 * @param response      回傳的原始內容
 * @param query_result  結果文字
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool HttpMp3Player::create_music_info(const std::string& song_name, const std::string& artist_name, std::string& query_result){
    std::string full_query_song_url = base_url + "/search2.view?" + subsonic_api_para + "&f=json&artistCount=0&albumCount=0&songCount=30&query=" + url_encode(song_name) + url_encode(" ") + url_encode(artist_name);
    ESP_LOGI(TAG, "查詢位址 URL: %s", full_query_song_url.c_str());
    // 使用Board提供的HTTP客户端    
    std::string s_response;
    if (!http_get_response(full_query_song_url, s_response, query_result)){
        ESP_LOGE(TAG, "取得回應失敗！");
        return false;
    }

    if (!parse_response_to_musicinfo(s_response, query_result))
    {
        auto& app = Application::GetInstance();
        auto* display = Board::GetInstance().GetDisplay();
        ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
        ESP_LOGE(TAG, "Failed to find music: 没有找到歌曲 '%s'", song_name.c_str());
        std::string msg = "找不到歌曲：【" + song_name + "】，歌手【" + artist_name + "】";
        app.Schedule([display, msg]() {
            display->SetChatMessage("assistant", msg.c_str());
        });
        ESP_LOGE(TAG, "%s", msg.c_str());
        return false;
    }
    return true;
}
/**
 * @brief 透過 HTTP GET 取得回應內容
 *
 * 這個函式使用提供的 Http 物件，對指定 URL 發出 GET 請求，
 * 並將回傳內容寫入 response 和 query_result。
 *
 * @param http          已建立的 Http 物件引用
 * @param full_url      要請求的完整 URL
 * @param response      回傳的原始內容
 * @param query_result  結果文字
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool http_get_response(std::string& full_url, std::string& response, std::string& query_result){
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetTimeout(1500);
    // 打开GET连接
    if (!http->Open("GET", full_url))
    {
        ESP_LOGE(TAG, "Failed to connect to music API");
        query_result = "Failed to connect to music API";
        return false;
    }
    // 检查响应状态码
    int status_code = http->GetStatusCode();
    if (status_code != 200)
    {
        ESP_LOGE(TAG, "HTTP GET failed with status code: %d", status_code);
        http->Close();
        query_result = "HTTP GET failed with status code: " + status_code;
        return false;
    }

    ESP_LOGI(TAG, "取得回應內容");
    size_t len = http->GetBodyLength();
    ESP_LOGI("HTTP", "Body length: %u", len); //0: chunk 模式
    //std::string response = http->ReadAll();  會卡死沒回應，感謝 ChatGPT，說要用 http->Read

    response.clear();
    char buf[1024];
    int n;
    while ((n = http->Read(buf, sizeof(buf))) > 0) {
        response.append(buf, n);
    }
    if (n < 0) {
        query_result = "http error";
        ESP_LOGE(TAG, "HTTP error");
        http->Close();        
        return false;
    }
    http->Close();
    ESP_LOGI(TAG, "HTTP read finished, total %u bytes", response.size());

    ESP_LOGD(TAG, "回應 response = %s", response.c_str());
    return true;
}

/**
 * @brief 解析 response 內容，寫入 _current_music_info
 * 
 * @param response      回應內容（從外部傳入）
 * @param query_result  結果
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool HttpMp3Player::parse_response_to_musicinfo(std::string& response, std::string& query_result){
    cJSON *response_json = cJSON_Parse(response.c_str());
    if (!response_json){
        query_result = "json error";
        ESP_LOGE(TAG, "Failed to parse JSON response : %s", response.c_str());
        return false;
    }
    cJSON *subsonic = cJSON_GetObjectItem(response_json, "subsonic-response");
    cJSON *search   = cJSON_GetObjectItem(subsonic, "searchResult2");
    cJSON *songs    = cJSON_GetObjectItem(search, "song");

    cJSON* title = nullptr;
    cJSON* artist = nullptr;

    bool _has_result = false;

    if (songs) {
        int song_count = cJSON_GetArraySize(songs);

        // 清空 playlist，並釋放未使用的內存
        playlists.clear();            // 清空 playlist，size = 0, capacity 可能不變
        playlists.shrink_to_fit(); // 請求減少容量以釋放未使用的內存
        // 遍歷並添加到 playlist
        for (int i = 0; i < song_count; ++i) {
            cJSON* song = cJSON_GetArrayItem(songs, i);
            cJSON* song_id = cJSON_GetObjectItem(song, "id");
            if(cJSON_IsString(song_id) && song_id->valuestring){
                playlists.push_back(song_id->valuestring);
            }
            //cJSON *_title = cJSON_GetObjectItem(song, "title");
            //cJSON *_artist = cJSON_GetObjectItem(song, "artist");
            //ESP_LOGI(TAG,"歌曲：%s, 歌手：%s", _title->valuestring, _artist->valuestring);
        }
        current_music_info_ = MusicInfo{}; //清空並釋放資源

        uint32_t r = esp_random();   // 硬體亂數
        int index = r % song_count; // 取餘數，範圍是 0~ (song_count-1)，亂數取歌

        cJSON *song = cJSON_GetArrayItem(songs, index); //亂數取到的歌曲資訊
        cJSON* song_id = cJSON_GetObjectItem(song, "id"); //取得的歌曲 id，播放的必要參數
        title = cJSON_GetObjectItem(song, "title");
        artist = cJSON_GetObjectItem(song, "artist");
        //將取得的音樂資訊放入全域 _current_music_info
        if(cJSON_IsString(song_id) && song_id->valuestring){
            current_music_info_.song_id = song_id->valuestring;
            _has_result = true;
        }
        if(cJSON_IsString(title) && title->valuestring){
            current_music_info_.title = title->valuestring;
        }
        if(cJSON_IsString(artist) && artist->valuestring){
            current_music_info_.artist = artist->valuestring;
        }
    }
    cJSON_Delete(response_json);
    return _has_result;
}
/**
 * @brief 解析 response 內容，結果寫入 current_music_info_.lyrics
 * 
 * @param response      回應內容（從外部傳入）
 * @param query_result  處理結果
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool HttpMp3Player::parse_response_to_lyric(std::string& response, std::string& query_result){
    cJSON *response_json = cJSON_Parse(response.c_str());
    if (!response_json){
        query_result = "lyric json error";
        ESP_LOGE(TAG, "Failed to parse lyric JSON response : %s", response.c_str());
        return false;
    }
    cJSON *subsonic = cJSON_GetObjectItem(response_json, "subsonic-response");
    cJSON *lyricsList = cJSON_GetObjectItem(subsonic, "lyricsList");

    cJSON *structuredLyrics = cJSON_GetObjectItem(lyricsList, "structuredLyrics");
    cJSON *first = cJSON_GetArrayItem(structuredLyrics, 0);
    cJSON *lines = cJSON_GetObjectItem(first, "line");
    
    int lyric_count = cJSON_GetArraySize(lines);

    if(lyric_count == 0){
        query_result = "response 没有歌词资料！";
        ESP_LOGE(TAG, "response 沒有歌詞資料！");
        cJSON_Delete(response_json);
        return false;
    }

    // 先釋放舊陣列
    current_music_info_.lyrics.clear();
    current_music_info_.lyrics.shrink_to_fit();

    for (int i = 0; i < lyric_count; i++) {
        cJSON *line = cJSON_GetArrayItem(lines, i);
        cJSON *start = cJSON_GetObjectItem(line, "start");
        cJSON *value = cJSON_GetObjectItem(line, "value");

        LyricLine lyric{};
        lyric.start_ms = start->valueint;
        strlcpy(lyric.text,
            value->valuestring,
            sizeof(lyric.text));
        current_music_info_.lyrics.push_back(lyric);
    }
    
    cJSON_Delete(response_json);
    if (current_music_info_.lyrics.size() > 0) {
        for (int i = 0; i < current_music_info_.lyrics.size() ; i++) {
            ESP_LOGD(TAG, "%u ms : %s", 
                current_music_info_.lyrics[i].start_ms, 
                current_music_info_.lyrics[i].text);
        }
    }
    return !current_music_info_.lyrics.empty();
    //return lyric_count > 0 ? true : false;
}

void HttpMp3Player::continuous_playing(){
    int song_count = playlists.size();
    if (song_count <= 0){
        ESP_LOGE(TAG, "playlists 沒有歌曲！");
    }

    try { //重建 current_music_info_ 與歌詞
        std::string next_song_id = current_music_info_.song_id;
        while (song_count >= 2 && next_song_id == current_music_info_.song_id) //避免下一首挑到同一首歌
        {
            uint32_t r = esp_random();   // 硬體亂數
            int index = r % song_count;
            next_song_id = playlists.at(index);
        }
        
        current_music_info_.song_id = next_song_id;  // 使用 at()，带边界检查
        current_music_info_.mp3_url = base_url + "/stream.view?" + subsonic_api_para + "&id=" + url_encode(current_music_info_.song_id) ;

        std::string lyric_response, query_result;
        std::string full_query_lyric_url = base_url + "/getLyricsBySongId.view?" + subsonic_api_para + "&f=json&id=" + url_encode(current_music_info_.song_id);

        if(!http_){
            auto network = Board::GetInstance().GetNetwork();
            http_ = network->CreateHttp(0);
            http_->SetTimeout(1500);
        }
        if (http_get_response(full_query_lyric_url, lyric_response, query_result)){
            if(!parse_response_to_lyric(lyric_response, query_result)){
                ESP_LOGE(TAG, "歌詞回應解析失敗！");
            }
        }    
        else{
            ESP_LOGE(TAG, "取得歌詞回應失敗！");
        }
        Play();
    } catch (const std::out_of_range& e) {
        ESP_LOGE(TAG, "Out of range: %s", e.what() );
    }
    
}
bool HttpMp3Player::Play()
{
    if (current_music_info_.mp3_url.empty()) {
        ESP_LOGE(TAG, "_current_music_info.mp3_url.empty()");
        return false;
    }
    /*
    FreeRTOS 的 Task 用 static streaming_task 進場，
    C++ 用物件收尾。
    */    
    // 啟動 task
    /*
    BaseType_t result = xTaskCreate(
        streaming_task,
        "STREAM_TASK",
        8192,
        this,   // 👈 只傳 this
        5,
        nullptr
    );
    */
    // TaskHandle_t task_handle_ = nullptr;
    const BaseType_t core_id = CONFIG_FREERTOS_NUMBER_OF_CORES - 1;
    BaseType_t result = xTaskCreatePinnedToCore(
        streaming_task,
        "STREAM_TASK",
        8192,
        this,
        pipeline_task_prio_, //優先權，0~24，越大越優先，要實際測試過才知道，太大會與 WIFI 搶資源，太小會斷音
        nullptr, //&task_handle_,
        core_id
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create streaming task");
        return false;
    }

    return true; // 立即回傳
}

void HttpMp3Player::streaming_task(void* arg)
{
    auto* self = static_cast<HttpMp3Player*>(arg);

    vTaskDelay(pdMS_TO_TICKS(300));

    self->start_streaming_pipeline();

    vTaskDelete(nullptr);
}

bool HttpMp3Player::start_streaming_pipeline(){
    #warning "經實測驗證，audio pipeline 支援 ESP32-S3 / C5 / C6 ，不支援最早的 ESP32"
    #warning "音樂串流平台若為 https://，裝置需要 PSRAM ，否則 audio pipeline 可能因記憶體不足而跳出"

    if (current_music_info_.mp3_url.empty())
    {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    // 判斷是否 https
    std::string protocol_ ; //賦與值 http 或 https
    if (!parse_url_protocol(current_music_info_.mp3_url,protocol_)){
        ESP_LOGE(TAG, "parse_url_protocol error! 不支援的網址！");
        return false;
    }

    auto &app = Application::GetInstance();
    ESP_LOGW(TAG, "等待小智囉唆完畢....");
    while(app.GetDeviceState() == kDeviceStateSpeaking){
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while(app.GetDeviceState() == kDeviceStateListening){
        ESP_LOGI(TAG, "切換至待機狀態，以免小智邊播歌邊插嘴...");
        app.ToggleChatState();
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    is_playing_ = true;
    stop_flag_ = false;
    
    auto &board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE); //避免待機時 WIFI 進入低功耗導致網路降速
    vTaskDelay(pdMS_TO_TICKS(200));

    // 定義管線 ring buffer 大小
#if defined(CONFIG_SPIRAM)
    const size_t PREBUFFER_THRESHOLD = 16 * 1024; // 啟用 PSRAM 時 16KB 緩衝
#else
    const size_t PREBUFFER_THRESHOLD = 8 * 1024; // 8KB 緩衝
#endif
    ESP_LOGW(TAG, "開始 HTTP - MP3 - RAW 音樂管線流程....");
    ESP_LOGI(TAG, "代碼薅自 ESP-ADF 範例 pipeline_http_mp3 再做些修改...");
    
    ESP_LOGI(TAG, "[1.0] Prepare pipeline and stream.");
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t http_stream_reader, mp3_decoder, raw_stream_reader;

    ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create http stream to get data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_cfg.task_prio = pipeline_task_prio_; //解決 ESP32 C5 出現看門狗警告、下載卡頓。
    if (protocol_ == "https") {
        //Subsonic API 採用 HTTPS，CPU 負載提高且需更大緩衝區。
        ESP_LOGW(TAG, "[2.1.1] Subsonic API uses HTTPS/TLS, increasing CPU load and requiring larger buffers for stable playback");
        http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
#if defined(CONFIG_SPIRAM)
        //具備 PSRAM，可擴大 TCP 緩衝與 ring buffer，改善 HTTPS 播放順暢度。
        ESP_LOGI(TAG, "[2.1.2] PSRAM available: increasing TCP request size and ringbuffer for smoother HTTPS playback");
        http_cfg.request_size = 32 * 1024;
        http_cfg.out_rb_size  = 64 * 1024;
#endif
    }
    http_stream_reader = http_stream_init(&http_cfg);

    ESP_LOGI(TAG, "[2.2] Create mp3 decoder to decode mp3 data");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.out_rb_size = PREBUFFER_THRESHOLD ; //原本只有 2K 有偶發斷音的現象
    mp3_cfg.task_prio = pipeline_task_prio_;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[2.3] Create raw stream reader to read data from pipeline then write to codec chip");
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_cfg.out_rb_size = PREBUFFER_THRESHOLD;
    raw_stream_reader = raw_stream_init(&raw_cfg);

    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder,        "mp3");
    audio_pipeline_register(pipeline, raw_stream_reader,  "raw");

    ESP_LOGI(TAG, "[2.5] Link it together http_stream-->mp3_decoder-->raw_stream");
    const char *link_tag[3] = {"http", "mp3", "raw"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[2.6] Set up  uri");
    audio_element_set_uri(http_stream_reader, current_music_info_.mp3_url.c_str());

    ESP_LOGI(TAG, "[ 3 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[3.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[3.2] Prepare anything for playback control");
    
    //取得小智 AI 的全域 codec
    auto codec = board.GetAudioCodec();
    
    if (!codec->output_enabled())
    {
        codec->EnableOutput(true);
    }
    //取得小智 AI 的屏幕物件
    auto display = board.GetDisplay();

    if((int)current_music_info_.lyrics.size() == 0){
        std::string msg = "《" + current_music_info_.title + "》沒有歌詞。";
        app.Schedule([display, msg]() {
            display->SetChatMessage("assistant", msg.c_str());
        });
    }
    
    // PCM 數據參數
    constexpr size_t PCM_BYTES = 2048;
    int16_t pcm_buf[PCM_BYTES / sizeof(int16_t)];
    std::vector<int16_t> pcm_data;
    pcm_data.reserve(PCM_BYTES / sizeof(int16_t));

    //codec->SetOutputSampleRate 的預設參數：
    //1. 將雙聲道合併為單聲，2. sample_rate 會跟著 MP3 變化。
    int channels = 1; //
    int sample_rate = 44100; // 當前 MP3 的採樣頻率

    //參數：播放進度推算歌詞的位置
    size_t total_samples_played = 0; //已播放的音頻數據，用來換算播放時間
    size_t current_lyric_index = 0; //當前歌詞的位置

    bool complete_played = false; //判定是中斷或正常播完

    //預緩存機制
    bool buffer_enabled = true; //是否啟用預緩存

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    while (!stop_flag_) {
        // 1. 同步事件檢查 pipeline stop 或 music info
        audio_event_iface_msg_t msg;
        while (audio_event_iface_listen(evt, &msg, 0) == ESP_OK) { // 非阻塞
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) mp3_decoder &&
                msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(mp3_decoder, &music_info);
                ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);
                channels = (int)music_info.channels;
                sample_rate = music_info.sample_rates;
                codec->SetOutputSampleRate(music_info.sample_rates);
            }
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) http_stream_reader &&
                msg.cmd == AEL_MSG_CMD_REPORT_POSITION) {
                    audio_element_info_t http_info = {0};
                    audio_element_getinfo(http_stream_reader,&http_info);
                    ESP_LOGI(TAG, "[ * ] http current position %d", http_info.byte_pos);                    

            }
            // 加入對 mp3_decoder 狀態的監聽
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) mp3_decoder && 
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS && 
                ((int)msg.data == AEL_STATUS_STATE_STOPPED || (int)msg.data == AEL_STATUS_STATE_FINISHED)) {
                
                ESP_LOGW(TAG, "mp3_decoder (AEL_STATUS_STATE = %d)，切換至強迫輸出模式...", msg.data);
                // 關鍵：一旦解碼器結束，就強制解除 buffer_enabled 狀態
                buffer_enabled = false;
            }
            //正常播完
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) raw_stream_reader &&
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
                ((int)msg.data == AEL_STATUS_STATE_STOPPED || (int)msg.data == AEL_STATUS_STATE_FINISHED)) {
                    complete_played = true;
                    stop_flag_ = true;
                    ESP_LOGW(TAG, "raw_stream_reader (AEL_STATUS_STATE = %d)，正常結束...", msg.data);
                    break;
                }
        }

        // 2. 緩衝機制
        if (buffer_enabled){
            // 2.1 檢查 mp3_decoder 輸出端目前累積了多少資料
            //ringbuf_handle_t out_rb = audio_element_get_output_ringbuf(mp3_decoder);
            // 2.1 檢查 Raw Stream 輸入端目前累積了多少資料
            ringbuf_handle_t in_rb = audio_element_get_input_ringbuf(raw_stream_reader);
            int fill_level = rb_bytes_filled(in_rb);
            // 2.2. 如果解碼器還在跑，才執行補水邏輯；如果解碼器停了，就直接往下走。
            // 只有在「真的乾了(fill_level < 4 * 1024)」或是「正在補水且還沒補滿(is_buffering)」時才停下來
            if(fill_level < ((4 * 1024 < PREBUFFER_THRESHOLD / 2) ? (4 * 1024) : (PREBUFFER_THRESHOLD / 2)) ){
                ESP_LOGI(TAG, "預緩衝中... %d/%d", fill_level, PREBUFFER_THRESHOLD);
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
        }
        try {
            // 3. 讀出管線中的 PCM 數據。mp3_decoder 已經幫忙處理成 PCM 數據，
            int read_len = raw_stream_read(raw_stream_reader, reinterpret_cast<char *>(pcm_buf), PCM_BYTES);
            if (read_len > 0) {
                size_t num_samples = read_len / sizeof(int16_t); // 總樣本數
                //size_t channels = music_info.channels;          // mp3 decoder 的 channel 數

                if (channels == 2) {
                    // stereo -> mono
                    size_t mono_samples = num_samples / 2;
                    for (size_t i = 0; i < mono_samples; ++i) {
                        int16_t left  = pcm_buf[2*i];
                        int16_t right = pcm_buf[2*i + 1];
                        pcm_buf[i] = (left / 2 + right / 2); // 混合成 mono
                    }
                    num_samples = mono_samples;
                }
                // 開始計算時間，準備同步歌詞
                // 累計樣本數，以原本的 num_samples 為計算基準而不是單聲道 mono_samples
                total_samples_played += num_samples;

                if((int)current_music_info_.lyrics.size() > 0){
                    // ===== 播歌時同步歌詞 =====
                    // 計算目前播放時間（毫秒）
                    uint32_t current_ms = (uint64_t)total_samples_played * 1000 / sample_rate;
                    size_t lyric_count = current_music_info_.lyrics.size();
                    while (current_lyric_index < lyric_count &&
                        current_ms >= current_music_info_.lyrics[current_lyric_index].start_ms) {
                        //display_lyric(_current_music_info.lyrics[_current_lyric_index].text);
                        //ESP_LOGI(TAG, "%s", current_music_info_.lyrics[current_lyric_index].text );
                        app.Schedule([display , message = current_music_info_.lyrics[current_lyric_index].text ]() {
                            display->SetChatMessage("assistant", message);
                        });
                        current_lyric_index++;
                    }
                }
                // 計算秒數 保留這段寫法
                //float seconds_played = (float)total_samples_played / sample_rate;
                //int minutes = (int)(seconds_played / 60);
                //int seconds = (int)(seconds_played) % 60;

                // 日誌 / 外部可讀
                //ESP_LOGI(TAG, "播放進度 %02d:%02d", minutes, seconds);

                // 將 PCM 放入 vector
                pcm_data.assign(pcm_buf, pcm_buf + num_samples);

                // 送給 codec
                codec->OutputData(pcm_data);
            }

        } catch(const std::exception &e) {
            ESP_LOGE(TAG, "Exception: %s", e.what());
            stop_flag_ = true; // 安全停止
        }
    }

    ESP_LOGI(TAG, "[ 5 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    // ⭐ 再等一下確保 element task 真正退出，播免後續資源清除不乾淨，例如監聽物件仍在作用，導致崩潰錯誤
    vTaskDelay(pdMS_TO_TICKS(30));
    audio_pipeline_terminate(pipeline);
    audio_pipeline_wait_for_stop(pipeline);   // 再一次保險

    /* Terminate the pipeline before removing the listener */
    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, raw_stream_reader);
    audio_pipeline_unregister(pipeline, mp3_decoder);

    audio_pipeline_remove_listener(pipeline);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(raw_stream_reader);
    audio_element_deinit(mp3_decoder);

    is_playing_ = false;
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); //恢復待機時 WIFI 低功耗

    //當完整播完歌，且為連續播放模式時保留 current_music_info_ 用來判定下一首不要重複，否則清空
    if(!(complete_played && play_mode_ == PlayModeContinuous)){
        current_music_info_ = MusicInfo{}; //重置內容
    }

    codec->ResetOutputSampleRate(); //恢復原來的設定

    if(complete_played){
        app.Schedule([display, message = Lang::Strings::MUSIC_FINISHED]() {
            display->SetChatMessage("assistant", message);
        });
        vTaskDelay(pdMS_TO_TICKS(400)); //讓對話的畫面滑順一點
        if(app.GetDeviceState() == kDeviceStateIdle){
            if(play_mode_ == PlayModeSingle){
                ESP_LOGW(TAG,"單曲模式");
                app.ToggleChatState();
            }
            else if(play_mode_ == PlayModeContinuous){
                ESP_LOGW(TAG,"連播模式");
                continuous_playing();
            }
        }
    }
    else{
        app.Schedule([display, message = Lang::Strings::MUSIC_STOPPED]() {
            display->SetChatMessage("user", message);
        });
    }

    return true;
}

