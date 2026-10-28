#include <fmt/core.h>
#include "stdafx.h"
#include <cstdint>

namespace SonicDayDebug
{
    // Offsets observes dans default_patched.xex.
    // Leur appartenance exacte a CPlayerSpeedContext reste a confirmer.
    constexpr uint32_t ObjectPointerOffset = 0x100;
    constexpr uint32_t InnerFieldOffset = 0x23C;

    // Lecture d'un entier 32 bits big-endian en memoire emulee.
    uint32_t ReadU32(uint8_t* base, uint32_t address)
    {
        const uint8_t* p = base + address;

        return (uint32_t(p[0]) << 24) |
               (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8) |
                uint32_t(p[3]);
    }
}

// AP TEST - Candidat Sonic Day : 8238CA88
PPC_FUNC_IMPL(__imp__sub_8238CA88);

PPC_FUNC(sub_8238CA88)
{
    static unsigned int apDayCalls = 0;

    ++apDayCalls;

    if (apDayCalls <= 5 || apDayCalls % 100 == 0)
    {
        fmt::print("AP DAY HOOK TEST: 8238CA88 - {} appels", apDayCalls);
    }

    __imp__sub_8238CA88(ctx, base);
}
