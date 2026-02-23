#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H


class MusicPlayer {
public:
    virtual ~MusicPlayer() = default;  // 添加虚析构函数
    virtual bool Play() = 0;
    virtual bool PauseResume() = 0;
    virtual bool Stop() = 0 ;
    virtual bool IsPlaying() = 0;
};

#endif // MUSIC_H 