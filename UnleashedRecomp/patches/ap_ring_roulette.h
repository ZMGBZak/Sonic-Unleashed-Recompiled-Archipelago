#pragma once
// RING ROULETTE - beta prototype, independent of the Archipelago server.
// Shared atomic bridge between the F8/SDL thread and the emulated game thread.
// IMPORTANT: native game writes happen ONLY from verified PPC callbacks.
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <string>
#include <user/paths.h>

namespace APRingRoulette
{
    constexpr int kRequiredRings = 300;
    // Match SDL_GameControllerButton integer constants, no SDL dependency in PPC patches.
    constexpr int kDefaultButton = 8; // SDL_CONTROLLER_BUTTON_RIGHTSTICK (R3)
    enum class Prize : int {
        None = 0, Ring1 = 1, Life1 = 2, Ring100 = 3, Hotdog = 4,
        Life3 = 5, Minute60 = 6, Score50000 = 7, Jackpot777 = 8
    };

    inline std::atomic<bool> enabled{false};
    inline std::atomic<int> button{kDefaultButton};
    inline std::atomic<int> progress{0};
    inline std::atomic<int> pending{0};
    inline std::atomic<int> lastPrize{0};
    inline std::atomic<bool> spinning{false};
    inline std::atomic<bool> inAct{false};
    inline std::atomic<bool> appliedJackpotLives{false};
    inline std::atomic<bool> appliedJackpotBank{false};

    inline bool Ready() {
        return enabled.load(std::memory_order_acquire) &&
               inAct.load(std::memory_order_acquire) &&
               progress.load(std::memory_order_acquire) >= kRequiredRings &&
               pending.load(std::memory_order_acquire) == 0 &&
               !spinning.load(std::memory_order_acquire);
    }

    // Charge continues while rewards wait, so inventory hooks cannot stall Ring tracking.
    // This only records positive *gameplay* Ring increments in ring hooks.
    // Archipelago item grants and roulette grants do not call this function.
    inline void RecordPickup(std::uint32_t count) {
        if (!enabled.load(std::memory_order_relaxed) ||
            spinning.load(std::memory_order_relaxed) ||
            !count || count > 100)
            return; // exclude invalid/scripted outlier amounts
        int old = progress.load(std::memory_order_relaxed);
        while (old < kRequiredRings &&
               !progress.compare_exchange_weak(old,
                   std::min(kRequiredRings, old + static_cast<int>(count)),
                   std::memory_order_release, std::memory_order_relaxed)) {}
    }

    inline bool StartSpin() {
        if (!Ready()) return false;
        spinning.store(true, std::memory_order_release);
        return true;
    }

    // Called by the UI ONLY after the visual spin finishes.
    inline void SetPending(Prize prize) {
        if (pending.load(std::memory_order_acquire) != 0) return;
        lastPrize.store(static_cast<int>(prize), std::memory_order_relaxed);
        appliedJackpotLives.store(false, std::memory_order_relaxed);
        appliedJackpotBank.store(false, std::memory_order_relaxed);
        progress.store(0, std::memory_order_release);
        pending.store(static_cast<int>(prize), std::memory_order_release);
        spinning.store(false, std::memory_order_release);
    }

    inline bool Wants(Prize prize) {
        return pending.load(std::memory_order_acquire) == static_cast<int>(prize);
    }
    inline void Finish(Prize prize) {
        int expected = static_cast<int>(prize);
        pending.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    }
    inline void TryFinishJackpot() {
        if (appliedJackpotLives.load(std::memory_order_acquire) &&
            appliedJackpotBank.load(std::memory_order_acquire))
            Finish(Prize::Jackpot777);
    }

    inline const char* PrizeName(Prize prize) {
        switch (prize) {
        case Prize::Ring1: return "+1 RING";
        case Prize::Life1: return "+1 LIFE";
        case Prize::Ring100: return "+100 RINGS";
        case Prize::Hotdog: return "+1 HOTDOG";
        case Prize::Life3: return "+3 LIVES";
        case Prize::Minute60: return "-1 MINUTE";
        case Prize::Score50000: return "+50,000 POINTS";
        case Prize::Jackpot777: return "777 LEGENDARY JACKPOT!";
        default: return "";
        }
    }
    inline std::filesystem::path SettingsPath() {
        return GetUserPath() / "ap_ring_roulette.txt";
    }
    inline void Load() {
        static bool once = false;
        if (once) return;
        once = true;
        std::ifstream file(SettingsPath());
        int e = 0, b = kDefaultButton, p = 0;
        if (file >> e >> b >> p) {
            enabled.store(e == 1);
            button.store(b >= 0 && b <= 14 ? b : kDefaultButton);
            progress.store(std::clamp(p, 0, kRequiredRings));
        }
    }
    inline void Save() {
        std::error_code ec;
        std::filesystem::create_directories(SettingsPath().parent_path(), ec);
        std::ofstream file(SettingsPath(), std::ios::trunc);
        if (file) file << (enabled.load() ? 1 : 0) << ' ' << button.load()
                       << ' ' << progress.load() << '\n';
    }
}
