#include <apu/custom_music.h>
#include <SDL_mixer.h>
#include <SDL.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace CustomMusic
{
    static Mix_Music* s_music = nullptr;
    static Mix_Music* s_musicAct1 = nullptr;
    static std::string s_apotosDayAct1Path = "CustomMusic/Apotos_Day_Act1.wav";
    static bool s_apotosDayAct1ReloadPending = false;
    static std::atomic<bool> s_requestApotosDay{false};
    static std::atomic<bool> s_requestApotosDayAct1{false};
    static bool s_initialized = false;
    static bool s_playing = false;

    // AP_UNIVERSAL_DAY_MUSIC_V1
    static constexpr int kDayRegionCount = static_cast<int>(DayRegion::Count);

    static std::string s_dayMusicPaths[kDayRegionCount] = {
        "CustomMusic/Apotos_Day_Act1.wav",
        "", "", "", "", "", "", "", ""
    };

    static std::atomic<int> s_requestedDayRegion{-1};

    static int DayRegionIndex(DayRegion region)
    {
        const int index = static_cast<int>(region);
        return index >= 0 && index < kDayRegionCount ? index : -1;
    }

    static void SaveSettings();

    void SetDayMusicPath(DayRegion region, const char* path)
    {
        const int index = DayRegionIndex(region);

        if (index < 0 || !path || !*path)
            return;

        s_dayMusicPaths[index] = path;
        SaveSettings();

        if (region == DayRegion::Apotos)
            SetApotosDayAct1Path(path);

        std::printf("[AP MUSIC] Day region %d path: %s\n",
            index, path);
    }

    const char* GetDayMusicPath(DayRegion region)
    {
        const int index = DayRegionIndex(region);

        if (index < 0)
            return "";

        return s_dayMusicPaths[index].c_str();
    }

    void RequestDayMusic(DayRegion region)
    {
        const int index = DayRegionIndex(region);

        if (index >= 0)
            s_requestedDayRegion.store(index);
    }
    // AP_UNIVERSAL_NIGHT_API_V1
    static std::string s_nightMusicPaths[kDayRegionCount] = {};
    static std::atomic<int> s_requestedNightRegion{-1};

    void SetNightMusicPath(DayRegion region, const char* path)
    {
        const int index = DayRegionIndex(region);

        if (index < 0 || !path)
            return;

        s_nightMusicPaths[index] = path;
        SaveSettings();

        std::printf("[AP MUSIC] Night region %d path: %s\n",
            index, path);
    }

    const char* GetNightMusicPath(DayRegion region)
    {
        const int index = DayRegionIndex(region);

        if (index < 0)
            return "";

        return s_nightMusicPaths[index].c_str();
    }

    void RequestNightMusic(DayRegion region)
    {
        const int index = DayRegionIndex(region);

        if (index >= 0)
            s_requestedNightRegion.store(index);
    }
    // END AP_UNIVERSAL_NIGHT_API_V1
    // END AP_UNIVERSAL_DAY_MUSIC_V1
    // AP_BOSS_DIAGNOSTIC_V1 - temporary file diagnostics
    static void ApBossDiag(const char* message, int value, int extra = -999)
    {
    // AP music file diagnostic disabled for release.
    }
    // END AP_BOSS_DIAGNOSTIC_V1
    // AP_GAME_LOOP_PLAYBACK_V1: saved per-file loop points for SDL_mixer music.
    // We use the mixer playback clock (not a second audio device) and seek
    // the currently playing Mix_Music back to its saved loop start.
    static Mix_Music* s_loopPlayingMusic = nullptr;
    static std::string s_loopPlayingPath;
    static bool s_loopActive = false;
    static std::int64_t s_loopStartMs = 0;
    static std::int64_t s_loopEndMs = 0;
    static Uint32 s_loopClockMs = 0;
    static double s_loopClockPositionMs = 0.0;
    static bool s_loopSeekWarned = false;

    static void ClearGameLoop()
    {
        s_loopPlayingMusic = nullptr;
        s_loopPlayingPath.clear();
        s_loopActive = false;
    }

    // Defined below the persistence API to avoid changing its implementation.
    static void ArmGameLoop(Mix_Music* music, const char* path);
    static void TickGameLoop();
    // END AP_GAME_LOOP_PLAYBACK_V1

    // AP_TOWN_MUSIC_V1
    static constexpr int kTownCount = static_cast<int>(TownTrack::Count);
    static std::string s_townPaths[kTownCount] = {};
    static std::atomic<int> s_requestedTown{-1};
    static Mix_Music* s_townMusic = nullptr;
    // AP_TOWN_STOP_GUARD_V2: ignore first stop immediately after Town WAV begins.
    static std::atomic<bool> s_townIgnoreNextStop{false};
    static std::atomic<bool> s_townPlaybackActive{false};

    void SetTownMusicPath(TownTrack track, const char* path)
    {
        const int index = static_cast<int>(track);
        if (index < 0 || index >= kTownCount || !path) return;
        s_townPaths[index] = path;
        SaveSettings();
    }

    const char* GetTownMusicPath(TownTrack track)
    {
        const int index = static_cast<int>(track);
        return (index >= 0 && index < kTownCount) ? s_townPaths[index].c_str() : "";
    }

    void RequestTownMusic(TownTrack track)
    {
        const int index = static_cast<int>(track);
        if (index >= 0 && index < kTownCount) s_requestedTown.store(index);
    }

    static void PlayTownMusic(int index)
    {
        if (index < 0 || index >= kTownCount || s_townPaths[index].empty()) return;
        Mix_Music* music = Mix_LoadMUS(s_townPaths[index].c_str());
        if (!music)
        {
            std::printf("[AP MUSIC] Town %d load failed: %s\n", index, Mix_GetError());
            return;
        }
        ClearGameLoop();
        Mix_HaltMusic();
        if (s_townMusic) Mix_FreeMusic(s_townMusic);
        s_townMusic = music;
        s_playing = (Mix_FadeInMusic(s_townMusic, -1, 1500) == 0);
        s_townPlaybackActive.store(s_playing);
        s_townIgnoreNextStop.store(s_playing);
        ArmGameLoop(s_playing ? s_townMusic : nullptr, s_townPaths[index].c_str());
        std::printf("[AP MUSIC] Town %d %s\n", index, s_playing ? "WAV started" : Mix_GetError());
    }
    // END AP_TOWN_MUSIC_V1

    // AP_BOSS_MUSIC_V1
    static constexpr int kBossCount = static_cast<int>(BossTrack::Count);
    static std::string s_bossPaths[kBossCount] = {};
    // AP_BOSS_STOP_GUARD_V1: ignore only the first stop after a successful boss start.
    static std::atomic<bool> s_bossIgnoreNextStop{false};
    static std::atomic<bool> s_bossPlaybackActive{false};
    static std::atomic<int> s_requestedBoss{-1};
    static Mix_Music* s_bossMusic = nullptr;

    void SetBossMusicPath(BossTrack track, const char* path)
    {
        const int index = static_cast<int>(track);
        if (index < 0 || index >= kBossCount || !path) return;
        s_bossPaths[index] = path;
        SaveSettings();
        std::printf("[AP MUSIC] Boss %d path: %s\n", index, path);
    }

    const char* GetBossMusicPath(BossTrack track)
    {
        const int index = static_cast<int>(track);
        return (index >= 0 && index < kBossCount) ? s_bossPaths[index].c_str() : "";
    }

    void RequestBossMusic(BossTrack track)
    {
        const int index = static_cast<int>(track);
        if (index >= 0 && index < kBossCount) { s_requestedBoss.store(index); ApBossDiag("RequestBoss", index); }
    }

    static void PlayBossMusic(int index)
    {
        if (index < 0 || index >= kBossCount || s_bossPaths[index].empty()) return;
        ApBossDiag("PlayBoss entered", index);
        Mix_Music* music = Mix_LoadMUS(s_bossPaths[index].c_str());
        if (!music)
        {
            ApBossDiag("Boss load failed", index);
            std::printf("[AP MUSIC] Boss %d load failed: %s\n", index, Mix_GetError());
            return;
        }
        ClearGameLoop();
        Mix_HaltMusic();
        if (s_bossMusic) Mix_FreeMusic(s_bossMusic);
        s_bossMusic = music;
        s_playing = (Mix_FadeInMusic(s_bossMusic, -1, 1500) == 0);
        s_townPlaybackActive.store(false);
        s_townIgnoreNextStop.store(false);
        s_bossPlaybackActive.store(s_playing);
        s_bossIgnoreNextStop.store(s_playing);
        ArmGameLoop(s_playing ? s_bossMusic : nullptr, s_bossPaths[index].c_str());
        ApBossDiag("Boss start success", index, s_playing ? 1 : 0);
        ApBossDiag("Boss playing and volume", Mix_PlayingMusic(), Mix_VolumeMusic(-1));
        std::printf("[AP MUSIC] Boss %d %s\n", index, s_playing ? "WAV started" : Mix_GetError());
    }
    // END AP_BOSS_MUSIC_V1
    // AP_UNIVERSAL_DAY_PLAYBACK_V1
    static Mix_Music* s_universalDayMusic = nullptr;

    static void PlayUniversalDayMusic(int index)
    {
        if (index < 0 || index >= kDayRegionCount)
            return;

        const std::string& path = s_dayMusicPaths[index];

        if (path.empty())
        {
            std::printf("[AP MUSIC] No WAV selected for region %d.\n", index);
            return;
        }

        Mix_Music* newMusic = Mix_LoadMUS(path.c_str());

        if (!newMusic)
        {
            std::printf("[AP MUSIC] Region %d load failed: %s\n",
                index, Mix_GetError());
            return;
        }

        ClearGameLoop();
        Mix_HaltMusic();

        if (s_universalDayMusic)
            Mix_FreeMusic(s_universalDayMusic);

        s_universalDayMusic = newMusic;

        if (Mix_FadeInMusic(s_universalDayMusic, -1, 1500) == 0)
        {
            s_playing = true;
            ArmGameLoop(s_universalDayMusic, path.c_str());
            std::printf("[AP MUSIC] Region %d WAV started!\n", index);
        }
        else
        {
            s_playing = false;
            std::printf("[AP MUSIC] Region %d playback failed: %s\n",
                index, Mix_GetError());
        }
    }
    // AP_UNIVERSAL_NIGHT_PLAYBACK_V1
    static Mix_Music* s_universalNightMusic = nullptr;

    static void PlayUniversalNightMusic(int index)
    {
        if (index < 0 || index >= kDayRegionCount)
            return;

        const std::string& path = s_nightMusicPaths[index];

        if (path.empty())
            return;

        Mix_Music* newMusic = Mix_LoadMUS(path.c_str());

        if (!newMusic)
        {
            std::printf("[AP MUSIC] Night region %d load failed: %s\n",
                index, Mix_GetError());
            return;
        }

        ClearGameLoop();
        Mix_HaltMusic();

        if (s_universalNightMusic)
            Mix_FreeMusic(s_universalNightMusic);

        s_universalNightMusic = newMusic;

        if (Mix_FadeInMusic(s_universalNightMusic, -1, 1500) == 0)
        {
            s_playing = true;
            ArmGameLoop(s_universalNightMusic, path.c_str());
            std::printf("[AP MUSIC] Night region %d WAV started!\n", index);
        }
        else
        {
            s_playing = false;
            std::printf("[AP MUSIC] Night region %d playback failed: %s\n",
                index, Mix_GetError());
        }
    }
    // END AP_UNIVERSAL_NIGHT_PLAYBACK_V1
    // END AP_UNIVERSAL_DAY_PLAYBACK_V1
    void RequestStop();

    // BEGIN AP_CUSTOM_MUSIC_ENABLE
    // Disabled by default: original game music stays active.
    static std::atomic<bool> s_enabled{false};

    // BEGIN AP_CUSTOM_MUSIC_SETTINGS
    // AP_DAY_PATH_PERSISTENCE_V1
    static void SaveSettings()
    {
        if (FILE* f = std::fopen("CustomMusic/settings.ini", "w"))
        {
            std::fprintf(f, "Enabled=%d\n", s_enabled.load() ? 1 : 0);

            for (int i = 0; i < kDayRegionCount; ++i)
            {
                std::fprintf(f, "DayRegion%d=%s\n",
                    i, s_dayMusicPaths[i].c_str());
            }

            // AP_NIGHT_SETTINGS_V1
            for (int i = 0; i < kDayRegionCount; ++i)
            {
                std::fprintf(f, "NightRegion%d=%s\n",
                    i, s_nightMusicPaths[i].c_str());
            }
            // END AP_NIGHT_SETTINGS_V1
            // AP_BOSS_MUSIC_V1 settings
            for (int i = 0; i < kBossCount; ++i)
                std::fprintf(f, "BossTrack%d=%s\n", i, s_bossPaths[i].c_str());
            for (int i = 0; i < kTownCount; ++i)
                std::fprintf(f, "TownTrack%d=%s\n", i, s_townPaths[i].c_str());
            std::fclose(f);
        }
    }

    static void LoadSettings()
    {
        s_enabled.store(false);

        if (FILE* f = std::fopen("CustomMusic/settings.ini", "r"))
        {
            char line[4096];

            while (std::fgets(line, sizeof(line), f))
            {
                int enabled = 0;
                int region = -1;

                if (std::sscanf(line, "Enabled=%d", &enabled) == 1)
                {
                    s_enabled.store(enabled == 1);
                    continue;
                }

                int offset = 0;

                if (std::sscanf(line, "DayRegion%d=%n",
                    &region, &offset) == 1 &&
                    region >= 0 && region < kDayRegionCount &&
                    offset > 0)
                {
                    std::string path = line + offset;

                    while (!path.empty() &&
                        (path.back() == '\n' || path.back() == '\r'))
                    {
                        path.pop_back();
                    }

                    s_dayMusicPaths[region] = path;
                }
                // AP_NIGHT_SETTINGS_LOAD_V1
                region = -1;
                offset = 0;

                if (std::sscanf(line, "NightRegion%d=%n",
                    &region, &offset) == 1 &&
                    region >= 0 && region < kDayRegionCount &&
                    offset > 0)
                {
                    std::string path = line + offset;

                    while (!path.empty() &&
                        (path.back() == '\n' || path.back() == '\r'))
                    {
                        path.pop_back();
                    }

                    s_nightMusicPaths[region] = path;
                }
                // END AP_NIGHT_SETTINGS_LOAD_V1
                // AP_TOWN_MUSIC_V1 settings load
                region = -1;
                offset = 0;
                if (std::sscanf(line, "TownTrack%d=%n", &region, &offset) == 1 &&
                    region >= 0 && region < kTownCount && offset > 0)
                {
                    std::string path = line + offset;
                    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
                        path.pop_back();
                    s_townPaths[region] = path;
                    continue;
                }
                // AP_BOSS_MUSIC_V1 settings load
                region = -1;
                offset = 0;
                if (std::sscanf(line, "BossTrack%d=%n", &region, &offset) == 1 &&
                    region >= 0 && region < kBossCount && offset > 0)
                {
                    std::string path = line + offset;
                    while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
                        path.pop_back();
                    s_bossPaths[region] = path;
                }
                
            }

            std::fclose(f);
        }

        // Synchroniser le chemin utilise par Apotos Act 1.
        if (!s_dayMusicPaths[0].empty())
        {
            s_apotosDayAct1Path = s_dayMusicPaths[0];
        }
    }
    // END AP_CUSTOM_MUSIC_SETTINGS
    void SetEnabled(bool enabled)
    {
        s_enabled.store(enabled);

        if (!enabled)
            RequestStop();

        SaveSettings();
    }
    // END AP_CUSTOM_MUSIC_SETTINGS

    bool IsEnabled()
    {
        return s_enabled.load();
    }
    // END AP_CUSTOM_MUSIC_ENABLE

    // AP_LOOP_PERSISTENCE_V1: separate from settings.ini, no audio behavior changed.
    struct SavedLoop { std::string path; std::int64_t startMs; std::int64_t endMs; bool enabled; };
    static std::vector<SavedLoop> s_savedLoops;

    static std::string LoopPathEncode(const std::string& path)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string result;
        for (unsigned char c : path)
        {
            result += digits[c >> 4];
            result += digits[c & 15];
        }
        return result;
    }

    static int LoopHexDigit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    static bool LoopPathDecode(const std::string& hex, std::string& result)
    {
        if (hex.empty() || hex.size() % 2) return false;
        result.clear();
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            int hi = LoopHexDigit(hex[i]);
            int lo = LoopHexDigit(hex[i + 1]);
            if (hi < 0 || lo < 0) return false;
            result += static_cast<char>((hi << 4) | lo);
        }
        return true;
    }

    static bool WriteLoopsFile()
    {
        const char* temp = "CustomMusic/loops.ini.tmp";
        FILE* f = std::fopen(temp, "wb");
        if (!f) return false;
        bool ok = std::fprintf(f, "# AP_LOOP_PERSISTENCE_V1 (hex-encoded UTF-8 file paths)\n") >= 0;
        for (const SavedLoop& loop : s_savedLoops)
        {
            const std::string hex = LoopPathEncode(loop.path);
            if (std::fprintf(f, "Loop=%s|%lld|%lld|%d\n", hex.c_str(),
                static_cast<long long>(loop.startMs), static_cast<long long>(loop.endMs),
                loop.enabled ? 1 : 0) < 0) ok = false;
        }
        if (std::fflush(f) != 0) ok = false;
        if (std::fclose(f) != 0) ok = false;
        if (!ok) { std::remove(temp); return false; }
        // Windows rename does not replace an existing destination; keep backup first.
        const char* dest = "CustomMusic/loops.ini";
        const char* bak = "CustomMusic/loops.ini.bak";
        std::remove(bak);
        bool hadOriginal = (std::rename(dest, bak) == 0);
        if (std::rename(temp, dest) != 0)
        {
            if (hadOriginal) std::rename(bak, dest);
            return false;
        }
        return true;
    }

    static void ReadLoopsFile()
    {
        s_savedLoops.clear();
        FILE* f = std::fopen("CustomMusic/loops.ini", "rb");
        if (!f) return;
        char line[16384];
        while (std::fgets(line, sizeof(line), f))
        {
            if (std::strncmp(line, "Loop=", 5) != 0) continue;
            std::string record(line + 5);
            while (!record.empty() && (record.back() == '\n' || record.back() == '\r')) record.pop_back();
            size_t a = record.find('|');
            size_t b = a == std::string::npos ? a : record.find('|', a + 1);
            size_t c = b == std::string::npos ? b : record.find('|', b + 1);
            if (a == std::string::npos || b == std::string::npos || c == std::string::npos) continue;
            std::string path;
            if (!LoopPathDecode(record.substr(0, a), path)) continue;
            const std::string start = record.substr(a + 1, b - a - 1);
            const std::string end = record.substr(b + 1, c - b - 1);
            const std::string enabled = record.substr(c + 1);
            char *ep1 = nullptr, *ep2 = nullptr;
            long long startMs = std::strtoll(start.c_str(), &ep1, 10);
            long long endMs = std::strtoll(end.c_str(), &ep2, 10);
            if (!ep1 || *ep1 || !ep2 || *ep2 || (enabled != "0" && enabled != "1")) continue;
            if (startMs < 0 || endMs <= startMs) continue;
            s_savedLoops.push_back({path, startMs, endMs, enabled == "1"});
        }
        std::fclose(f);
        std::printf("[AP LOOP] Loaded %zu saved loop(s).\n", s_savedLoops.size());
    }

    bool GetSavedLoop(const char* path, LoopPoints& out)
    {
        if (!path || !*path) return false;
        for (const SavedLoop& loop : s_savedLoops)
        {
            if (loop.path == path)
            {
                out.startMs = loop.startMs;
                out.endMs = loop.endMs;
                out.enabled = loop.enabled;
                return true;
            }
        }
        return false;
    }

    bool SaveLoop(const char* path, std::int64_t startMs, std::int64_t endMs, bool enabled)
    {
        if (!path || !*path || startMs < 0 || endMs <= startMs) return false;
        for (SavedLoop& loop : s_savedLoops)
        {
            if (loop.path == path)
            {
                SavedLoop old = loop;
                loop.startMs = startMs;
                loop.endMs = endMs;
                loop.enabled = enabled;
                if (WriteLoopsFile()) return true;
                loop = old;
                return false;
            }
        }
        s_savedLoops.push_back({path, startMs, endMs, enabled});
        if (WriteLoopsFile()) return true;
        s_savedLoops.pop_back();
        return false;
    }
    // AP_GAME_LOOP_PLAYBACK_V1
    static void ArmGameLoop(Mix_Music* music, const char* path)
    {
        ClearGameLoop();
        if (!music || !path || !*path) return;
        s_loopPlayingMusic = music;
        s_loopPlayingPath = path;
        LoopPoints points{};
        if (!GetSavedLoop(path, points) || !points.enabled ||
            points.startMs < 0 || points.endMs <= points.startMs) return;
        s_loopStartMs = points.startMs;
        s_loopEndMs = points.endMs;
        s_loopClockMs = SDL_GetTicks();
        s_loopClockPositionMs = 0.0;
        s_loopSeekWarned = false;
        s_loopActive = true;
        std::printf("[AP LOOP] Armed: %s (%lld -> %lld ms)\n", path,
            static_cast<long long>(s_loopStartMs), static_cast<long long>(s_loopEndMs));
    }

    static void TickGameLoop()
    {
        if (!s_loopActive || !s_playing || !s_loopPlayingMusic || !Mix_PlayingMusic()) return;
        if (Mix_PausedMusic()) return;

        const Uint32 now = SDL_GetTicks();
        const double elapsedMs = static_cast<double>(now - s_loopClockMs);
        double positionMs = s_loopClockPositionMs + elapsedMs;
#if SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
        // The decoder's reported position is more accurate when available.
        const double mixerSeconds = Mix_GetMusicPosition(s_loopPlayingMusic);
        if (mixerSeconds >= 0.0) positionMs = mixerSeconds * 1000.0;
#endif
        if (positionMs < static_cast<double>(s_loopEndMs)) return;

        if (Mix_SetMusicPosition(static_cast<double>(s_loopStartMs) / 1000.0) == 0)
        {
            s_loopClockMs = now;
            s_loopClockPositionMs = static_cast<double>(s_loopStartMs);
            std::printf("[AP LOOP] Jump: %s -> %lld ms\n", s_loopPlayingPath.c_str(),
                static_cast<long long>(s_loopStartMs));
        }
        else
        {
            if (!s_loopSeekWarned)
            {
                std::printf("[AP LOOP] Seeking unsupported for %s: %s\n",
                    s_loopPlayingPath.c_str(), Mix_GetError());
                s_loopSeekWarned = true;
            }
            s_loopActive = false;
        }
    }
    // END AP_GAME_LOOP_PLAYBACK_V1

    // END AP_LOOP_PERSISTENCE_V1

    void Init()
    {
        if (s_initialized)
            return;

        // SDL audio is already initialized by the game.
        if (Mix_OpenAudioDevice(48000, AUDIO_F32SYS, 2, 4096, nullptr, 0) < 0)
        {
            std::printf("[AP MUSIC] Init failed: %s\n", Mix_GetError());
            return;
        }

        

s_initialized = true;



        LoadSettings();
        ReadLoopsFile();

        s_music = Mix_LoadMUS("CustomMusic/Apotos_Day.wav");
        s_musicAct1 = Mix_LoadMUS(s_apotosDayAct1Path.c_str());

        if (!s_music)
            std::printf("[AP MUSIC] Load failed: %s\n", Mix_GetError());
        else
            std::printf("[AP MUSIC] WAV loaded successfully.\n");
    }

    void RequestApotosDay()
    {
        s_requestApotosDay.store(true);
    }

    // AP_CUSTOM_MUSIC_PATH_V1
    

    void SetApotosDayAct1Path(const char* path)
    {
        if (!path || !*path)
            return;

        s_apotosDayAct1Path = path;
        s_apotosDayAct1ReloadPending = true;
        std::printf("[AP MUSIC] Selected Act 1 path: %s\n",
            s_apotosDayAct1Path.c_str());
    }

    void RequestApotosDayAct1()
    {
        s_requestApotosDayAct1.store(true);
    }

    void RequestStop()
    {
        // AP_TOWN_STOP_GUARD_V2: preserve Town WAV across first unexpected stop.
        if (s_townPlaybackActive.load() && s_townIgnoreNextStop.exchange(false))
        {
    // AP music file diagnostic disabled for release.
            return;
        }
        s_townPlaybackActive.store(false);
        s_townIgnoreNextStop.store(false);
        // AP_BOSS_STOP_GUARD_V1: preserve boss WAV across the first unexpected stop.
        if (s_bossPlaybackActive.load() && s_bossIgnoreNextStop.exchange(false))
        {
    // AP music file diagnostic disabled for release.
            return;
        }
        s_bossPlaybackActive.store(false);
        s_bossIgnoreNextStop.store(false);
        s_requestApotosDay.store(false);
        s_requestApotosDayAct1.store(false);
        // AP_UNIVERSAL_STOP_CLEAR_V1
        s_requestedDayRegion.store(-1);
        // AP_NIGHT_STOP_CLEAR_V1
        s_requestedNightRegion.store(-1);
        // AP_BOSS_MUSIC_V1
        ApBossDiag("RequestStop pendingBoss", s_requestedBoss.load());
        s_requestedBoss.store(-1);
        s_requestedTown.store(-1);

        ClearGameLoop();
        if (s_initialized && s_playing)
        {
            Mix_FadeOutMusic(2000);
            s_playing = false;
            std::printf("[AP MUSIC] Custom music stopped.\n");
        }
    }
    void Update()
    {
        if (!s_initialized)
            return;

        // AP_TOWN_MUSIC_V1
        const int requestedTown = s_requestedTown.exchange(-1);
        if (requestedTown >= 0 && s_enabled.load()) PlayTownMusic(requestedTown);
        // AP_BOSS_MUSIC_V1
        const int requestedBoss = s_requestedBoss.exchange(-1);
        if (requestedBoss >= 0) ApBossDiag("Update boss", requestedBoss, s_enabled.load() ? 1 : 0);
        if (requestedBoss >= 0 && s_enabled.load())
            PlayBossMusic(requestedBoss);        // AP_NIGHT_UPDATE_V1
        const int requestedNightRegion = s_requestedNightRegion.exchange(-1);

        if (requestedNightRegion >= 0 && s_enabled.load())
        {
            PlayUniversalNightMusic(requestedNightRegion);
        }
        // END AP_NIGHT_UPDATE_V1
        const int requestedRegion = s_requestedDayRegion.exchange(-1);

        if (requestedRegion >= 0 && s_enabled.load())
        {
            PlayUniversalDayMusic(requestedRegion);
        }
        const bool requestAct1 = s_requestApotosDayAct1.exchange(false);
        const bool requestDay = s_requestApotosDay.exchange(false);

        if ((requestAct1 || requestDay) && s_enabled.load())
        {
            // AP_MUSIC_RELOAD_SAFE_V1
            if (requestAct1 && s_apotosDayAct1ReloadPending)
{
    Mix_Music* newMusic = Mix_LoadMUS(s_apotosDayAct1Path.c_str());

    if (newMusic)
    {
        // Stopper SDL_mixer avant de liberer l'ancien fichier.
        Mix_HaltMusic();

        if (s_musicAct1)
            Mix_FreeMusic(s_musicAct1);

        s_musicAct1 = newMusic;
        s_playing = false;
        s_apotosDayAct1ReloadPending = false;

        std::printf("[AP MUSIC] Act 1 track reloaded successfully.\n");
    }
    else
    {
        std::printf("[AP MUSIC] Act 1 reload failed: %s\n",
            Mix_GetError());

        // Conserver l'ancien fichier si le nouveau est invalide.
        s_apotosDayAct1ReloadPending = false;
    }
}

Mix_Music* selectedMusic = requestAct1 ? s_musicAct1 : s_music;

            if (selectedMusic)
            {
                if (Mix_FadeInMusic(selectedMusic, -1, 1500) == 0)
                {
                    s_playing = true;
                    ArmGameLoop(selectedMusic, requestAct1 ? s_apotosDayAct1Path.c_str() : "CustomMusic/Apotos_Day.wav");
                    std::printf("[AP MUSIC] Apotos %s started!\n",
                        requestAct1 ? "Day Act 1" : "Day");
                }
                else
                {
                    s_playing = false;
                    std::printf("[AP MUSIC] Playback failed: %s\n", Mix_GetError());
                }
            }
            else
            {
                s_playing = false;
                std::printf("[AP MUSIC] No custom track for Apotos %s.\n",
                    requestAct1 ? "Day Act 1" : "Day");
            }
        }

        TickGameLoop();
        if (s_playing && !Mix_PlayingMusic())
        {
            s_playing = false;
            ClearGameLoop();
        }
    }

    bool IsPlaying()
    {
        return s_playing;
    }

    void Shutdown()
    {
        if (!s_initialized)
            return;

        ClearGameLoop();
        Mix_HaltMusic();

        if (s_music)
        {
            Mix_FreeMusic(s_music);
            s_music = nullptr;
        }

        if (s_musicAct1)
        {
            Mix_FreeMusic(s_musicAct1);
            s_musicAct1 = nullptr;
        }

        // AP_TOWN_MUSIC_V1 cleanup
        if (s_townMusic) { Mix_FreeMusic(s_townMusic); s_townMusic = nullptr; }
        // AP_BOSS_MUSIC_V1 cleanup
        if (s_bossMusic) { Mix_FreeMusic(s_bossMusic); s_bossMusic = nullptr; }

        if (s_universalDayMusic)
        {
            Mix_FreeMusic(s_universalDayMusic);
            s_universalDayMusic = nullptr;
        }
        // AP_NIGHT_SHUTDOWN_V1
        if (s_universalNightMusic)
        {
            Mix_FreeMusic(s_universalNightMusic);
            s_universalNightMusic = nullptr;
        }
        // END AP_NIGHT_SHUTDOWN_V1

        Mix_CloseAudio();

        s_playing = false;
        s_initialized = false;
    }
}











