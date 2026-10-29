#pragma once
// AP MEDAL RADAR - communication only between the PPC player hook and F8 audio.
// No SDL/audio calls from the emulated game's update thread.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace APMedalRadarBridge
{
    inline std::atomic<float> nearestDistance{-1.0f};
    inline std::atomic<std::int64_t> lastUpdateNs{0};

    inline std::int64_t NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Distance < 0: no currently tracked, uncollected medal.
    inline void Publish(float distance)
    {
        nearestDistance.store(std::isfinite(distance) && distance >= 0.0f
            ? distance : -1.0f, std::memory_order_relaxed);
        lastUpdateNs.store(NowNs(), std::memory_order_release);
    }

    inline void Clear() { Publish(-1.0f); }

    inline bool GetFreshDistance(float& distance)
    {
        const auto stamp = lastUpdateNs.load(std::memory_order_acquire);
        const auto now = NowNs();
        // After pause/load/shutdown, never keep beeping at a stale position.
        if (stamp <= 0 || now < stamp || now - stamp > 500000000LL)
            return false;
        distance = nearestDistance.load(std::memory_order_relaxed);
        return std::isfinite(distance) && distance >= 0.0f;
    }
}
