#include "ap_ring_roulette.h" // AP_RING_ROULETTE_V1
#include <api/SWA.h>
#include <archipelago_client.h>
#include <os/logger.h>
#include <ui/game_window.h>
#include <user/achievement_manager.h>
#include <user/persistent_storage_manager.h>
#include <user/config.h>
#include <atomic>
#include <cstdio>

void AchievementManagerUnlockMidAsmHook(PPCRegister& id)
{
    AchievementManager::Unlock(id.u32);
}

bool DisableHintsMidAsmHook()
{
    return !Config::Hints;
}

// Disable Perfect Dark Gaia hints.
PPC_FUNC_IMPL(__imp__sub_82AC36E0);
PPC_FUNC(sub_82AC36E0)
{
    auto pPerfectDarkGaiaChipHintName = (xpointer<char>*)g_memory.Translate(0x8338EF10);

    strcpy(pPerfectDarkGaiaChipHintName->get(), Config::Hints ? "V_CHP_067\0" : "end\0");

    __imp__sub_82AC36E0(ctx, base);
}

bool DisableControlTutorialMidAsmHook()
{
    return !Config::ControlTutorial;
}

bool DisableEvilControlTutorialMidAsmHook(PPCRegister& r4, PPCRegister& r5)
{
    if (Config::ControlTutorial)
        return true;

    // Only allow enemy QTE prompts to get through.
    return r4.u32 == 1 && r5.u32 == 1;
}

bool DisableDLCIconMidAsmHook()
{
    return Config::DisableDLCIcon;
}

// AP_WEREHOG_BATTLE_STATE_DIAGNOSTIC_V1
// Diagnostic only: observes the RAW value before the game's existing
// battle-music suppression logic alters r11.u8. No sound behavior changed.
void WerehogBattleMusicMidAsmHook(PPCRegister& r11)
{
    const unsigned rawState = static_cast<unsigned>(r11.u8);

    // Original game/mod behavior: leave this untouched.
    if (!Config::BattleTheme && r11.u8 == 4)
        r11.u8 = 3;

    // Log raw state changes only, not every update/frame.
    // A rawState of 4 is the candidate battle music state; verify in-game.
    static std::atomic<int> lastRawState{-1};
    const int previous = lastRawState.exchange(
        static_cast<int>(rawState), std::memory_order_relaxed);
    if (previous != static_cast<int>(rawState))
    {
        std::printf(
            "[AP BATTLE DIAG] raw=%u effective=%u battle_theme=%d\n",
            rawState,
            static_cast<unsigned>(r11.u8),
            Config::BattleTheme ? 1 : 0);
    }
}
// END AP_WEREHOG_BATTLE_STATE_DIAGNOSTIC_V1

bool UseAlternateTitleMidAsmHook()
{
    auto isSWA = Config::Language == ELanguage::Japanese;

    if (Config::UseAlternateTitle)
        isSWA = !isSWA;

    return isSWA;
}

/* Hook function that gets the game region
   and force result to zero for Japanese
   to display the correct logos. */
PPC_FUNC_IMPL(__imp__sub_825197C0);
PPC_FUNC(sub_825197C0)
{
    if (Config::Language == ELanguage::Japanese)
    {
        ctx.r3.u64 = 0;
        return;
    }

    __imp__sub_825197C0(ctx, base);
}

// Logo skip
PPC_FUNC_IMPL(__imp__sub_82547DF0);
PPC_FUNC(sub_82547DF0)
{
    if (Config::SkipIntroLogos)
    {
        ctx.r4.u64 = 0;
        ctx.r5.u64 = 0;
        ctx.r6.u64 = 1;
        ctx.r7.u64 = 0;
        sub_825517C8(ctx, base);
    }
    else
    {
        __imp__sub_82547DF0(ctx, base);
    }
}

/* Ignore xercesc::EmptyStackException to
   allow DLC stages with invalid XML to load. */
PPC_FUNC_IMPL(__imp__sub_8305D5B8);
PPC_FUNC(sub_8305D5B8)
{
    auto value = PPC_LOAD_U32(ctx.r3.u32 + 4);

    if (!value)
        return;

    __imp__sub_8305D5B8(ctx, base);
}

// Disable auto save warning.
PPC_FUNC_IMPL(__imp__sub_82586698);
PPC_FUNC(sub_82586698)
{
    if (Config::DisableAutoSaveWarning)
        *(bool*)g_memory.Translate(0x83367BC2) = true;

    __imp__sub_82586698(ctx, base);
}

// SWA::CObjHint::MsgNotifyObjectEvent::Impl
// Disable only certain hints from hint volumes.
// This hook should be used to allow hint volumes specifically to also prevent them from affecting the player.
PPC_FUNC_IMPL(__imp__sub_82736E80);
PPC_FUNC(sub_82736E80)
{
    // GroupID parameter text
    auto* groupId = (const char*)(base + PPC_LOAD_U32(ctx.r3.u32 + 0x100));
    
    if (!Config::Hints)
    {
        // WhiteIsland_ACT1_001: "Your friend went off that way, Sonic. Quick, let's go after him!"
        // s20n_mykETF_c_navi_2: "Huh? Weird! We can't get through here anymore. We were able to earlier!"
        if (strcmp(groupId, "WhiteIsland_ACT1_001") != 0 && strcmp(groupId, "s20n_mykETF_c_navi_2") != 0)
            return;
    }

    __imp__sub_82736E80(ctx, base);
}

// SWA::CHelpWindow::MsgRequestHelp::Impl
// Disable only certain hints from other sequences.
// This hook should be used to block hint messages from unknown sources.
PPC_FUNC_IMPL(__imp__sub_824C1E60);
PPC_FUNC(sub_824C1E60)
{
    auto pMsgRequestHelp = (SWA::Message::MsgRequestHelp*)(base + ctx.r4.u32);

    if (!Config::Hints)
    {
        // s10d_mykETF_c_navi: "Looks like we can get to a bunch of places in the village from here!"
        if (strcmp(pMsgRequestHelp->m_Name.c_str(), "s10d_mykETF_c_navi") == 0)
            return;
    }

    __imp__sub_824C1E60(ctx, base);
}

// This function is called in various places but primarily for the boost filter
// when the second argument (r4) is set to "boost". Whilst boosting the third argument (f1)
// will go up to 1.0f and then down to 0.0f as the player lets off of the boost button.
// To avoid the boost filter from kicking in at all if the function is called with "boost"
// we set the third argument to zero no matter what (if the code is on).
PPC_FUNC_IMPL(__imp__sub_82B4DB48);
PPC_FUNC(sub_82B4DB48)
{
    if (Config::DisableBoostFilter && strcmp((const char*)(base + ctx.r4.u32), "boost") == 0)
    {
        ctx.f1.f64 = 0.0;
    }

    __imp__sub_82B4DB48(ctx, base);
}

// DLC save data flag check.
// 
// The DLC checks are fundamentally broken in this game, resulting in this method always
// returning true and displaying the DLC info message when it shouldn't be.
// 
// The original intent here seems to have been to display the message every time new DLC
// content is installed, but the flags in the save data never get written to properly,
// causing this function to always pass in some way.
//
// We bypass the save data completely and write to external persistent storage to store
// whether we've seen the DLC info message instead. This way we can retain the original
// broken game behaviour, whilst also providing a fix for this issue that is safe.
PPC_FUNC_IMPL(__imp__sub_824EE620);
PPC_FUNC(sub_824EE620)
{
    __imp__sub_824EE620(ctx, base);

    ctx.r3.u32 = PersistentStorageManager::ShouldDisplayDLCMessage(true);
}





// Archipelago Hotdog diagnostic (temporary)
void APHotdogDebugMidAsmHook(PPCRegister& r27, PPCRegister& r28)
{
    static unsigned int logCount = 0;

    if (logCount++ >= 100)
        return;

    printf("[AP HOTDOG DEBUG] ID=%u | Quantity=%u\n",
        r27.u32, r28.u32);
}


void APInventoryDebugMidAsmHook(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5)
{
    printf("[AP INVENTORY DEBUG] Progress=0x%08X | ID=%u | Delta=%d\n",
        r3.u32, r4.u32, r5.s32);
}

void APMenuInventoryDebugMidAsmHook(PPCRegister& r3, PPCRegister& r4)
{
    if (r3.u32 == 0 || r4.u32 != 0)
        return;

    // Ring Roulette: award a local Hotdog without using the AP network queue.
    // The verified native inventory hook is only available when this menu is active.
    if (APRingRoulette::Wants(APRingRoulette::Prize::Hotdog))
    {
        constexpr std::uint32_t kHotdogNativeId = 111u;
        const std::uint32_t before = GuestToHostFunction<std::uint32_t>(
            sub_82519C38, r3.u32, kHotdogNativeId);
        if (before < 99u)
        {
            GuestToHostFunction<void>(sub_8251B810, r3.u32, kHotdogNativeId, 1);
            const std::uint32_t after = GuestToHostFunction<std::uint32_t>(
                sub_82519C38, r3.u32, kHotdogNativeId);
            if (after > before)
            {
                APRingRoulette::Finish(APRingRoulette::Prize::Hotdog);
                printf("[RING ROULETTE] Hotdog granted: %u -> %u\n", before, after);
            }
        }
    }

    // Distribuer tous les consommables AP disponibles.
    // Limite de securite pour eviter une boucle infinie.
    for (unsigned int attempt = 0; attempt < 1000; ++attempt)
    {
        const std::uint32_t itemId = APClient::GetPendingConsumable();

        if (itemId == 0)
            break;

        const std::uint32_t before =
            GuestToHostFunction<std::uint32_t>(
                sub_82519C38, r3.u32, itemId
            );

        if (before >= 99)
        {
            printf("[AP CONSUMABLE] Inventory full: item=%u quantity=%u\n",
                itemId, before);
            APClient::CancelConsumable();
            break;
        }

        GuestToHostFunction<void>(
            sub_8251B810, r3.u32, itemId, 1
        );

        const std::uint32_t after =
            GuestToHostFunction<std::uint32_t>(
                sub_82519C38, r3.u32, itemId
            );

        if (after <= before)
        {
            printf("[AP CONSUMABLE] Grant failed: item=%u before=%u after=%u\n",
                itemId, before, after);
            APClient::CancelConsumable();
            break;
        }

        APClient::ConfirmConsumableApplied();

        printf("[AP CONSUMABLE] Granted item=%u quantity=%u -> %u\n",
            itemId, before, after);
    }
}





