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

#include "mcp_server.h"


#define TAG "HTTP-MP3-PLAYER"

bool http_get_response(std::unique_ptr<Http> &http, std::string& full_url, std::string& response, std::string& query_result);

HttpMp3Player::HttpMp3Player(){
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool("self.music.play_song",
        "播放指定的歌曲。当用户要求播放音乐时使用此工具，会自动获取歌曲详情并开始流式播放。\n"
             "参数:\n"
             "  `song_name`: 要播放的歌曲名称（可选，默认为空字符串）。\n"
             "  `artist_name`: 要播放的歌曲艺术家名称（可选，默认为空字符串）。\n"
             "返回:\n"
             "  播放状态信息，不需确认，立刻播放歌曲。", 
        PropertyList({
                 Property("song_name", kPropertyTypeString),//歌曲名称（必需）
                 Property("artist_name", kPropertyTypeString, "")//艺术家名称（可选，默认为空字符串）
        }), 
        [this](const PropertyList& properties) -> ReturnValue {
            ESP_LOGW(TAG, "MCP 執行 HTTP-MP3-PLAYER ");
            auto song_name = properties["song_name"].value<std::string>();
            auto artist_name = properties["artist_name"].value<std::string>();
            std::string message;
            if (!this->QuerySong(song_name, artist_name, message)) {
                return "{\"success\": false, \"message\": \"获取音乐资源失败\"}";
            }
            //auto download_result = music->GetDownloadResult();
            ESP_LOGI(TAG, "Music details result: %s", message.c_str());
            return "{\"success\": true, \"message\": \"" +  message + "\"}";
    });
    ESP_LOGI(TAG, "HttpMp3Player with MCP Tools `self.music.play_song` created.");
}

HttpMp3Player::~HttpMp3Player(){
    
}

bool HttpMp3Player::Play() {
    return start_playing();
}
bool HttpMp3Player::PauseResume() {
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

bool HttpMp3Player::QuerySong(const std::string& song_name, const std::string& artist_name, std::string& query_result){

    ESP_LOGI(TAG, "查詢歌手: %s, 歌曲名: %s", artist_name.c_str(), song_name.c_str());

    std::string base_url = CONFIG_SUBSONICAPI_URL;
    std::string subsonic_api_para = CONFIG_SUBSONICAPI_PARA; //"u=admin&p=1111&s=raw&v=1.16.1&c=xiaozhi";
    std::string full_query_song_url = base_url + "/search3.view?" + subsonic_api_para + "&f=json&query=" + url_encode(song_name) + url_encode(" ") + url_encode(artist_name);
    ESP_LOGI(TAG, "查詢位址 URL: %s", full_query_song_url.c_str());

    // 使用Board提供的HTTP客户端
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetTimeout(1500);
    
    std::string s_response;
    if (!http_get_response(http, full_query_song_url, s_response, query_result)){
        ESP_LOGE(TAG, "取得回應失敗！");
        return false;
    }

    if (!parse_song_response(s_response, query_result))
    {
        auto& app = Application::GetInstance();
        auto* display = Board::GetInstance().GetDisplay();
        ESP_LOGE(TAG, "Audio URL not found or empty for song: %s", song_name.c_str());
        ESP_LOGE(TAG, "Failed to find music: 没有找到歌曲 '%s'", song_name.c_str());
        std::string msg = "找不到歌曲：【" + song_name + "】，歌手【" + artist_name + "】";
        app.Schedule([display, msg]() {
            display->SetChatMessage("assistant", msg.c_str());
        });
        return false;
    }
    current_music_info_.mp3_url =  base_url + "/stream.view?" + subsonic_api_para + "&id=" + url_encode(current_music_info_.song_id) ;

    std::string lyric_response;
    std::string full_query_lyric_url = base_url + "/getLyricsBySongId.view?" + subsonic_api_para + "&f=json&id=" + url_encode(current_music_info_.song_id);
    if (http_get_response(http, full_query_lyric_url, lyric_response, query_result)){
        if(!parse_lyric_response(lyric_response, query_result)){
            ESP_LOGE(TAG, "歌詞回應解析失敗！");
        }
    }
    else{
        ESP_LOGE(TAG, "取得歌詞回應失敗！");
    }

    ESP_LOGI(TAG, "開始播放歌曲: %s, 歌手: %s", current_music_info_.title.c_str(), current_music_info_.artist.c_str());
    query_result = "開始播放歌曲: " + current_music_info_.title + " ,歌手: " + current_music_info_.artist;
    bool success = start_playing();
    if(!success){
        ESP_LOGE(TAG, "start_playing() 失敗！");
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
bool http_get_response(std::unique_ptr<Http> &http, std::string& full_url, std::string& response, std::string& query_result){
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
 * @brief 解析 response 內容
 *
 * 傳入 response 內容，解析結果寫入 _current_music_info
 * 
 
 * @param response          已建立的 Http 物件引用
 * @param query_result  回傳的額外處理資料
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool HttpMp3Player::parse_song_response(std::string& response, std::string& query_result){
    cJSON *response_json = cJSON_Parse(response.c_str());
    if (!response_json){
        query_result = "json error";
        ESP_LOGE(TAG, "Failed to parse JSON response : %s", response.c_str());
        return false;
    }
    cJSON *subsonic = cJSON_GetObjectItem(response_json, "subsonic-response");
    cJSON *search   = cJSON_GetObjectItem(subsonic, "searchResult3");
    cJSON *songs    = cJSON_GetObjectItem(search, "song");

    cJSON* title = nullptr;
    cJSON* artist = nullptr;

    bool _has_result = false;

    if (songs) {
        int song_count = cJSON_GetArraySize(songs); //找到的歌曲集，可能超過一首
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

bool HttpMp3Player::parse_lyric_response(std::string& response, std::string& query_result){
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
        query_result = "沒有歌詞！";
        ESP_LOGE(TAG, "沒有歌詞！");
        cJSON_Delete(response_json);
        return false;
    }

    // 先釋放舊陣列，避免 memory leak
    if (current_music_info_.lyrics) {
        free(current_music_info_.lyrics);
        current_music_info_.lyrics = nullptr;
        current_music_info_.lyric_count = 0;
    }
    // 配置新陣列
    current_music_info_.lyrics = (LyricLine*)malloc(sizeof(LyricLine) * lyric_count);
    current_music_info_.lyric_count = lyric_count;

    for (int i = 0; i < lyric_count; i++) {
        cJSON *line = cJSON_GetArrayItem(lines, i);
        cJSON *start = cJSON_GetObjectItem(line, "start");
        cJSON *value = cJSON_GetObjectItem(line, "value");

        current_music_info_.lyrics[i].start_ms = start->valueint;
        strlcpy(current_music_info_.lyrics[i].text, value->valuestring, sizeof(current_music_info_.lyrics[i].text));
    }
    cJSON_Delete(response_json);
    if (current_music_info_.lyrics) {
        for (int i = 0; i < current_music_info_.lyric_count; i++) {
            ESP_LOGD(TAG, "%u ms : %s", 
                current_music_info_.lyrics[i].start_ms, 
                current_music_info_.lyrics[i].text);
        }
    }
    return lyric_count > 0 ? true : false;
}

bool HttpMp3Player::start_playing()
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

    xTaskCreate(
        streaming_task,
        "STREAM_TASK",
        8192,
        this,   // 👈 只傳 this
        5,
        nullptr
    );

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
    #warning "經實測驗證，audio pipeline 支援 ESP32-S3 / C6 ，不支援最早的 ESP32"

    if (current_music_info_.mp3_url.empty())
    {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }
    auto &app = Application::GetInstance();
    ESP_LOGW(TAG, "等待小智囉唆完畢....");
    while(app.GetDeviceState() == kDeviceStateSpeaking){
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while(app.GetDeviceState() == kDeviceStateListening){
        ESP_LOGI(TAG, "切換至待機狀態，以免邊播歌邊插嘴...");
        app.ToggleChatState();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    is_playing_ = true;
    stop_flag_ = false;

    ESP_LOGW(TAG, "開始 HTTP - MP3 - RAW 音樂管線流程....");
    ESP_LOGI(TAG, "代碼薅自 ESP-ADF 範例 pipeline_http_mp3 再做些修改...");
    
    ESP_LOGI(TAG, "[1.0] Prepare pipeline and stream.");
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t http_stream_reader, mp3_decoder, raw_stream_reader;

    ESP_LOGI(TAG, "[2.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create http stream to read data");
    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_stream_reader = http_stream_init(&http_cfg);

    ESP_LOGI(TAG, "[2.2] Create mp3 decoder to decode mp3 file");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[2.3] Create raw stream to write data to codec chip");
    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_stream_reader = raw_stream_init(&raw_cfg);

    ESP_LOGI(TAG, "[2.4] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, mp3_decoder,        "mp3");
    audio_pipeline_register(pipeline, raw_stream_reader,  "raw");

    ESP_LOGI(TAG, "[2.5] Link it together http_stream-->mp3_decoder-->raw_stream");
    const char *link_tag[3] = {"http", "mp3", "raw"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);

    ESP_LOGI(TAG, "[2.6] Set up  uri (http as http_stream, mp3 as mp3 decoder, and default output is i2s)");
    audio_element_set_uri(http_stream_reader, current_music_info_.mp3_url.c_str());

    ESP_LOGI(TAG, "[ 3 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[3.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    //取得小智 AI 的全域 codec
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    
    if (!codec->output_enabled())
    {
        codec->EnableOutput(true);
    }

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);
    
    // 5. 數據橋接迴圈
    constexpr size_t PCM_BYTES = 2048;
    int16_t pcm_buf[PCM_BYTES / sizeof(int16_t)];
    std::vector<int16_t> pcm_data;
    pcm_data.reserve(PCM_BYTES / sizeof(int16_t));


    int channels = 1;
    int sample_rate = 44100;
    size_t total_samples_played = 0;
    int current_lyric_index = 0;
    int complete_played = false; //判定是中斷或正常播完

    auto display = board.GetDisplay();

    if(current_music_info_.lyric_count == 0){
        std::string msg = "《" + current_music_info_.title + "》沒有歌詞。";
        app.Schedule([display, msg]() {
            display->SetChatMessage("assistant", msg.c_str());
        });
    }

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
            //正常播完
            if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
                msg.source == (void *) raw_stream_reader &&
                msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
                (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
                    complete_played = true;
                    stop_flag_ = true;                
                break;
            }
        }  
        // 2. 讀出管線中的 PCM 數據。mp3_decoder 已經幫忙處理成 PCM 數據，
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

            if(current_music_info_.lyric_count > 0){
                // ===== 歌時同步歌詞 =====
                // 計算目前播放時間（毫秒）
                uint32_t current_ms = (uint64_t)total_samples_played * 1000 / sample_rate;
                while (current_lyric_index < current_music_info_.lyric_count &&
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
        } else {
            vTaskDelay(5 / portTICK_PERIOD_MS); // 等 buffer 填滿
            ESP_LOGI(TAG, "沒資料");
        }

          
    }

    ESP_LOGI(TAG, "[ 5 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

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

    current_music_info_ = MusicInfo{}; //重置內容

    codec->ResetOutputSampleRate();

    if(complete_played){
        app.Schedule([display, message = Lang::Strings::MUSIC_FINISHED]() {
            display->SetChatMessage("assistant", message);
        });
        if(app.GetDeviceState() == kDeviceStateIdle){
            app.ToggleChatState();
        }
    }
    else{
        app.Schedule([display, message = Lang::Strings::MUSIC_STOPPED]() {
            display->SetChatMessage("user", message);
        });
    }

    return true;
}
