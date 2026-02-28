#ifndef HTTP_MP3_PLAYER_H
#define HTTP_MP3_PLAYER_H
#include "music_player.h"
#include <string>
#include <vector> //vector
#include <memory> //unique_ptr
#include <http.h> //<Http> http_

typedef enum {
    PlayModeSingle = 0, //單曲播放模式
    PlayModeContinuous = 1 //連續播放模式
} PlayMode;

struct LyricLine {
    uint32_t start_ms;
    char text[128];   // 固定大小，避免動態配置出現 heap fragmentation（碎片化）
};
struct MusicInfo {
    std::string song_id;
    std::string title;
    std::string artist;
    std::string mp3_url;
    int sample_rate;
    std::vector<LyricLine> lyrics;
};

class HttpMp3Player : public MusicPlayer {
public: //Misic override
    HttpMp3Player();
    ~HttpMp3Player() override;

    bool Play() override;
    bool PauseResume() override;
    bool Stop() override ;
    bool IsPlaying() override;

public: // HttpMp3Player 方法
    bool QuerySong(const std::string& song_name, const std::string& artist_name, std::string& query_result);

private:
    bool is_playing_ = false;
    bool stop_flag_ = false;
    MusicInfo current_music_info_ = {}; //宣告、初始化（initialization）時可以這樣寫。後續清空也只需要 current_music_info_ = {};
    std::unique_ptr<Http> http_ = nullptr; //共用 http 連線物件

    //連續播放模式
    PlayMode play_mode_ = PlayModeSingle; //播放模式
    std::vector<std::string> playlists = {}; //播放的曲目清單

    bool create_music_info(std::unique_ptr<Http> &http, const std::string& song_name, const std::string& artist_name, std::string& query_result);
    bool parse_response_to_musicinfo(std::string& response, std::string& query_result);
    bool parse_response_to_lyric(std::string& response, std::string& query_result);
    bool start_playing();
    void continuous_playing(); //連續播放模式入口
    static void streaming_task(void* arg);
    bool start_streaming_pipeline();

};
#endif

/*
HttpMp3Player player;          // ✅ OK
Music* music = &player;       // ✅ 多型
music->Play();                // ✅ 動態繫結
*/

/* _current_music_info 已初始化，要重置的話，最好不要用 {0}

正確用法是 _current_music_info = MusicInfo{};

初始化可以用 {} / {0}，
但「重新賦值」只能用「一個完整的 C++ 物件」

*/