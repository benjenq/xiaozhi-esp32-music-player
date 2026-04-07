#include "mp3_player.h"
#include "board.h"
#include "display.h"
#include "application.h"

#include <dirent.h>

#define SD_MOUNT_POINT "/sdcard"

#define TAG "Mp3Player_FATFS"

#pragma region 

void parse_mp3_name(const char *filename,
                    char *artist, size_t artist_size,
                    char *title, size_t title_size)
{
    // 1️⃣ 找 " - "
    const char *sep = strstr(filename, " - ");
    if (!sep) {
        // 格式不符
        snprintf(artist, artist_size, "Unknown");
        snprintf(title, title_size, "%s", filename);
        return;
    }

    // 2️⃣ 找副檔名 "."
    const char *dot = strrchr(filename, '.');
    if (!dot || dot <= sep) {
        snprintf(artist, artist_size, "Unknown");
        snprintf(title, title_size, "%s", filename);
        return;
    }

    // 3️⃣ 取 artist
    size_t artist_len = sep - filename;
    if (artist_len >= artist_size) artist_len = artist_size - 1;

    strncpy(artist, filename, artist_len);
    artist[artist_len] = '\0';

    // 4️⃣ 取 title（跳過 " - "）
    const char *title_start = sep + 3;
    size_t title_len = dot - title_start;
    if (title_len >= title_size) title_len = title_size - 1;

    strncpy(title, title_start, title_len);
    title[title_len] = '\0';
}


void get_mp3_files_from_fatfs(const char *base_path, const std::string& song_name, const std::string& artist_name, std::vector<MusicInfo> &playlists){
    struct dirent *entry;
    auto* disp = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(disp);
    DIR *dir = opendir(base_path);

    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s\n", base_path);
        return ;
    }

    while ((entry = readdir(dir)) != NULL) {
        // 1️⃣ 忽略 . 和 ..
        if (entry->d_name[0] == '.') {
            if (entry->d_name[1] == '\0') continue;                  // "."
            if (entry->d_name[1] == '.' && entry->d_name[2] == '\0') continue; // ".."
        }
        // 2️⃣ 🔥 忽略 macOS metadata（越早越好）
        if (entry->d_name[0] == '.') { // || 忽略所有隱藏檔與系統資料夾（macOS / Linux)， macOS 通常是 .Spotlight-V100 / .fseventsd / .Trashes / .DS_Store
            //(strncmp(entry->d_name, "._", 2) == 0)) {//「比對開頭前 2 個字元，可能是 ._xxxxx 的 macOS matadata 檔案。
            continue;
        }
        //承 2 ，過濾 macOS 的系統檔案，想保留 . 開頭隱藏檔
        /*
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strncmp(entry->d_name, "._", 2) == 0 ||
            strcmp(entry->d_name, ".Spotlight-V100") == 0 ||
            strcmp(entry->d_name, ".fseventsd") == 0 ||
            strcmp(entry->d_name, ".Trashes") == 0 ||
            strcmp(entry->d_name, ".DS_Store") == 0) {
            continue;
        } */
        // 3️⃣ 🔥 忽略 Windows 系統檔案與目錄
        if (strcmp(entry->d_name, "System Volume Information") == 0 ||
            strcmp(entry->d_name, "$RECYCLE.BIN") == 0 ||
            strcmp(entry->d_name, "Thumbs.db") == 0 ||
            strcmp(entry->d_name, "desktop.ini") == 0) {
            continue;
        }

        char full_path[512];
        if (snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name) >= sizeof(full_path)) {
            continue;
        }

        struct stat st;
        if (stat(full_path, &st) == -1) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (entry->d_name[0] == '.') continue; //過濾掉 .XXXX 的目錄，前面已經過濾過了，可能多此一舉
            // 👉 如果是資料夾 → 遞迴
            ESP_LOGW(TAG, "Recurse directory: %s", full_path);
            get_mp3_files_from_fatfs(full_path, song_name, artist_name, playlists); // 一路掃到底
        } else {
            // 👉 如果是檔案 → 檢查副檔名
            ESP_LOGI(TAG, "S_ISREG: %s", full_path); // entry->d_name 改 full_path
            const char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, ".mp3") == 0) { //strcasecmp 不分大小寫的比對

                // bool match = true;
                // if (!artist_name.empty()) {
                //     if (!strcasestr(entry->d_name, artist_name.c_str())) {
                //         match = false;
                //     }
                // }
                // if (!song_name.empty()) {
                //     if (!strcasestr(entry->d_name, song_name.c_str())) {
                //         match = false;
                //     }
                // }
                // 以下是一行的寫法
                bool match =
                    (artist_name.empty() || strcasestr(entry->d_name, artist_name.c_str())) && // artist_name 空值或非空且有找到 -> true
                    (song_name.empty()   || strcasestr(entry->d_name, song_name.c_str())); // song_name 空值或非空且有找到 -> true

                if (!match) continue;
                char artist[64];
                char title[128];
                parse_mp3_name(entry->d_name, artist, sizeof(artist), title, sizeof(title));
                MusicInfo m{};
                m.song_id = std::string(full_path);
                m.artist  = std::string(artist);
                m.title   = std::string(title);
                m.cover_id = std::string(full_path);
                playlists.push_back(m);
                ESP_LOGW(TAG,"新增歌曲：檔案路徑：%s, 歌曲：%s, 歌手：%s", full_path, m.title.c_str(), m.artist.c_str());
            }
        }
    }
    closedir(dir);
}

#pragma endregion
/**
 * @brief 透過查詢條件（歌曲/歌手），建立 current_music_info_ 與 playlists_
 *
 * @param song_name      歌曲名稱
 * @param artist_name    演唱者/藝術家
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool Mp3Player::get_music_info_from_fatfs(const std::string& song_name, const std::string& artist_name){
    playlists_.clear();            // 清空 playlist，size = 0, capacity 可能不變
    //playlists_.shrink_to_fit(); // 請求減少容量以釋放未使用的內存
    get_mp3_files_from_fatfs(SD_MOUNT_POINT, song_name, artist_name, playlists_);

    current_music_info_ = MusicInfo{}; //清空並釋放資源

    if(!playlists_.empty()){
        if(!random_choose_song()){
            ESP_LOGE(TAG, "random_choose_song failed");
            return false;
        }
    }
    return !playlists_.empty();
}


/**
 * @brief 根據 song_id 查詢並產生歌詞資料
 *
 * @param song_id       歌曲 id
 *
 * @return true         請求成功
 * @return false        請求失敗
 */
bool Mp3Player::get_song_lyrics_from_fatfs(const std::string& song_id){
    auto *disp = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(disp);
    current_music_info_.lyrics.clear();
    //current_music_info_.lyrics.shrink_to_fit();
    //✅ Step 1：從 MP3 路徑轉成 LRC 路徑
    // 找最後一個 '.'
    size_t dot = song_id.find_last_of('.');
    if (dot == std::string::npos) {
        // 沒副檔名
        return false;
    }
    std::string lrc_path = song_id.substr(0, dot) + ".lrc";
    //✅ Step 2：確認 .lrc 存在
    struct stat st;
    if (stat(lrc_path.c_str(), &st) != 0) {
        ESP_LOGW(TAG, "no lrc file: %s", lrc_path.c_str());
        return false;
    }
    //✅ Step 3：讀取檔案
    FILE *f = fopen(lrc_path.c_str(), "r");
    if (!f) {
        ESP_LOGE(TAG, "open lrc failed");
        return false;
    }
    //✅ Step 4：解析 .lrc
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while ((p = strchr(p, '[')) != nullptr) {
            char *q = strchr(p, ']');
            if (!q) break; //壞掉的時間戳記

            int mm=0, ss=0;
            char frac_str[16] = {0};

            if (sscanf(p, "[%d:%d.%15[^]]]", &mm, &ss, frac_str) >= 2) {

                int ms = 0;

                if (strlen(frac_str) > 0) {
                    char tmp[4];
                    snprintf(tmp, sizeof(tmp), "%.3s", frac_str);

                    int val = atoi(tmp);

                    if (strlen(tmp) == 1) val *= 100;
                    else if (strlen(tmp) == 2) val *= 10;

                    ms = val;
                }

                int time_ms = mm * 60000 + ss * 1000 + ms;

                LyricLine lyric_line{};
                lyric_line.start_ms = time_ms;

                char *text = q + 1;

                size_t len = strlen(text);
                //Trim 歌詞
                while (len > 0 && (text[len-1] == '\n' || text[len-1] == '\r')) {
                    text[--len] = '\0';
                }
                strlcpy(lyric_line.text, text, sizeof(lyric_line.text));
                current_music_info_.lyrics.push_back(lyric_line);
            }
            p = q + 1;
        }
    }
    fclose(f);
    return !current_music_info_.lyrics.empty();
}

bool Mp3Player::get_cover_by_coverid_fatfs(const std::string& cover_id,
                                           uint8_t** out_buf,
                                           size_t* out_size)
{
    if (!out_buf || !out_size) {
        ESP_LOGE(TAG, "Invalid output pointer");
        return false;
    }
    auto *disp = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(disp);

    *out_buf = nullptr;
    *out_size = 0;

    // ---------- 1. 產生 base path（去副檔名） ----------
    std::string base = cover_id;

    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos) {
        ESP_LOGE(TAG, "Invalid filename (no extension): %s", cover_id.c_str());
        return false;
    }

    base = base.substr(0, dot);

    // ---------- 2. 嘗試副檔名 ----------
    const char* exts[] = {".jpg", ".png", ".JPG", ".PNG"};

    FILE* f = nullptr;
    std::string final_path;

    for (auto ext : exts) {
        std::string path = base + ext;
        f = fopen(path.c_str(), "rb");
        if (f) {
            final_path = path;
            break;
        }
    }

    if (!f) {
        ESP_LOGW(TAG, "Cover not found for: %s", cover_id.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Cover path: %s", final_path.c_str());

    // ---------- 3. 取得檔案大小 ----------
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "Invalid file size");
        fclose(f);
        return false;
    }

    size_t size = (size_t)file_size;

    // ---------- 4. 記憶體配置 ----------
#ifdef CONFIG_SPIRAM
#define BUF_CAP MALLOC_CAP_SPIRAM
    const size_t MAX_COVER_SIZE = 512 * 1024;
#else
#define BUF_CAP MALLOC_CAP_INTERNAL
    const size_t MAX_COVER_SIZE = 128 * 1024;
#endif

    if (size > MAX_COVER_SIZE) {
        ESP_LOGE(TAG, "Cover too large: %u", (unsigned int)size);
        fclose(f);
        return false;
    }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, BUF_CAP | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "Memory alloc failed");
        fclose(f);
        return false;
    }

    // ---------- 5. 讀檔 ----------
    size_t read_bytes = fread(buf, 1, size, f);
    fclose(f);

    if (read_bytes != size) {
        ESP_LOGE(TAG, "Read failed (%u/%u)", (unsigned int)read_bytes, (unsigned int)size);
        heap_caps_free(buf);
        return false;
    }

    ESP_LOGI(TAG, "Cover loaded: %u bytes", (unsigned int)size);

    // ---------- 6. 格式檢查 ----------
    bool is_jpeg = (size >= 2 && buf[0] == 0xFF && buf[1] == 0xD8);
    bool is_png  = (size >= 4 && buf[0] == 0x89 && buf[1] == 0x50 &&
                            buf[2] == 0x4E && buf[3] == 0x47);

    ESP_LOGW(TAG,
        "Format: %02X %02X %02X %02X %02X %02X %02X %02X ... %02X %02X",
        buf[0], buf[1], buf[2], buf[3],
        buf[4], buf[5], buf[6], buf[7],
        buf[size - 2], buf[size - 1]);

    if (!is_jpeg && !is_png) {
        ESP_LOGE(TAG, "Unsupported image format");
        heap_caps_free(buf);
        return false;
    }

    // ---------- 7. 回傳 ----------
    *out_buf = buf;
    *out_size = size;

    return true;
}