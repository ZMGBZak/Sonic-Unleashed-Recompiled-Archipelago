#pragma once
#include <cstdint>

namespace CustomMusic
{
    // AP UNIVERSAL DAY MUSIC V1
    enum class DayRegion
    {
        Apotos = 0,
        Spagonia,
        Mazuri,
        Holoska,
        ChunNan,
        Adabat,
        Shamar,
        EmpireCity,
        Eggmanland,
        Count
    };

    // AP_LOOP_PERSISTENCE_V1: storage API; playback integration comes later.
    struct LoopPoints { std::int64_t startMs; std::int64_t endMs; bool enabled; };
    bool GetSavedLoop(const char* path, LoopPoints& out);
    bool SaveLoop(const char* path, std::int64_t startMs, std::int64_t endMs, bool enabled);

    // Existing audio lifecycle
    void SetEnabled(bool enabled);
    bool IsEnabled();
    void Init();
    void Update();
    void Shutdown();
    void RequestStop();
    bool IsPlaying();

    // Existing Apotos compatibility
    void RequestApotosDay();
    void RequestApotosDayAct1();
    void SetApotosDayAct1Path(const char* path);

    // Universal Day music API
    void SetDayMusicPath(DayRegion region, const char* path);
    void RequestDayMusic(DayRegion region);
    const char* GetDayMusicPath(DayRegion region);

    // AP_TOWN_MUSIC_V1
    enum class TownTrack { ApotosDay=0, ApotosNight, SpagoniaDay, SpagoniaNight, MazuriDay, MazuriNight, ChunNanDay, ChunNanNight, HoloskaDay, HoloskaNight, AdabatDay, AdabatNight, ShamarDay, ShamarNight, EmpireCityDay, EmpireCityNight, EggmanlandDay, Count };
    void SetTownMusicPath(TownTrack track, const char* path);
    const char* GetTownMusicPath(TownTrack track);
    void RequestTownMusic(TownTrack track);

    // AP_BOSS_MUSIC_V1
    enum class BossTrack { Day = 0, Night, NightEgg, LastAir, Count };
    void SetBossMusicPath(BossTrack track, const char* path);
    const char* GetBossMusicPath(BossTrack track);
    void RequestBossMusic(BossTrack track);
    // Universal Night music API
    // Uses DayRegion for the same nine regions.
    void SetNightMusicPath(DayRegion region, const char* path);
    void RequestNightMusic(DayRegion region);
    const char* GetNightMusicPath(DayRegion region);
}