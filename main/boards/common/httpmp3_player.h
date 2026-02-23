#ifndef HTTP_MP3_PLAYER_H
#define HTTP_MP3_PLAYER_H
#include "music_player.h"
#include <string>

struct LyricLine {
    uint32_t start_ms;
    char text[128];   // 固定大小，避免動態配置
};


struct MusicInfo {
    std::string song_id;
    std::string title;
    std::string artist;
    std::string mp3_url;
    int sample_rate;
    LyricLine* lyrics = nullptr;
    int lyric_count = 0;
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
    MusicInfo current_music_info_ = {}; //宣告、初始化（initialization）時可以這樣寫。
    bool parse_song_response(std::string& response, std::string& query_result);
    bool parse_lyric_response(std::string& response, std::string& query_result);
    bool start_playing();
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