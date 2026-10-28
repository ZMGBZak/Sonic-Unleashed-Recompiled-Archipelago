void AP_NotifyReverseControlsLoadingEnd();
void AP_NotifyFreezeLoadingEnd();

// AP_CUSTOM_MUSIC_RESTART_V1



#include <api/SWA.h>
#include <apu/custom_music.h>

// AP_CUSTOM_MUSIC_RESTART_V1
static bool g_apCustomMusicRestartPending = false;

void AP_NotifyCustomMusicRestart(bool wasPlaying)
{
    g_apCustomMusicRestartPending = wasPlaying && CustomMusic::IsEnabled();
}
#include <hid/hid.h>
#include <os/logger.h>
#include <user/achievement_manager.h>
#include <user/persistent_storage_manager.h>
#include <user/config.h>
#include <app.h>
#include "../archipelago_client.h"
#include <cstdint>
#include <cstring>

//SWA::Message::MsgRequestStartLoading::Impl
PPC_FUNC_IMPL(__imp__sub_824DCF38);
PPC_FUNC(sub_824DCF38)
{
    // Capture des registres avant modification.
    const uint32_t entryR3 = ctx.r3.u32;
    const uint32_t entryR4 = ctx.r4.u32;
    const uint32_t entryR5 = ctx.r5.u32;
    const uint32_t entryR6 = ctx.r6.u32;
    const uint32_t entryR7 = ctx.r7.u32;

    auto pLoading = (SWA::CLoading*)g_memory.Translate(ctx.r3.u32);

    // BEGIN AP_CUSTOM_MUSIC_LOADING_STOP
    CustomMusic::RequestStop();
    // END AP_CUSTOM_MUSIC_LOADING_STOP
    LOGFN("[AP MUSIC DIAG] LOADING START");
    App::s_isLoading = true;

    LOGFN(
        "AP LOADING TRACE: r3={:08X} r4={:08X} r5={:08X} r6={:08X} r7={:08X}",
        entryR3, entryR4, entryR5, entryR6, entryR7
    );

    LOGFN("LOADING START: r4={:08X}", ctx.r4.u32);

    if (ctx.r4.u32 == SWA::eLoadingDisplayType_WerehogMovie)
    {
        if (Config::TimeOfDayTransition == ETimeOfDayTransition::PlayStation)
        {
            ctx.r4.u32 = SWA::eLoadingDisplayType_ChangeTimeOfDay;
            pLoading->m_IsNightToDay = App::s_isWerehog;
        }

        if (Config::UseArrowsForTimeOfDayTransition)
            ctx.r4.u32 = SWA::eLoadingDisplayType_Arrows;
    }

    if (Config::FixEggmanlandUsingEventGalleryTransition)
    {
        if (auto pGameDocument = SWA::CGameDocument::GetInstance())
        {
            auto stageName = pGameDocument->m_pMember->m_StageName.c_str();

            if (stageName && strlen(stageName))
            {
                // Fix restarting Eggmanland as the Werehog
                // erroneously using the Event Gallery transition.
                if (ctx.r4.u32 == SWA::eLoadingDisplayType_EventGallery &&
                    !strcmp(stageName, "Act_EggmanLand"))
                {
                    ctx.r4.u32 = SWA::eLoadingDisplayType_NowLoading;
                }
            }
        }
    }

    __imp__sub_824DCF38(ctx, base);
}

// AP Shoes: applique les objets effectivement recus d'Archipelago.
// Ne jamais acceder a la RAM depuis le thread reseau.
// ATTENTION: peut modifier SYS-DATA lors de la sauvegarde; utiliser une copie de test.
extern uint8_t AP_GetReceivedShoeMask();

static void AP_ShoesDiagnosticAtLoadingEnd()
{
    auto* gameDoc = SWA::CGameDocument::GetInstance();
    if (!gameDoc || !gameDoc->m_pMember ||
        gameDoc->m_pMember->m_StageName.c_str()[0] == '\0')
        return;

    auto* appDoc = SWA::CApplicationDocument::GetInstance();
    if (!appDoc || !appDoc->m_pMember)
        return;

    auto* param = appDoc->m_pMember->m_spGameParameter.get();
    if (!param || !param->m_pSaveData.get())
        return;

    auto* bytes = reinterpret_cast<uint8_t*>(param->m_pSaveData.get());
    constexpr size_t shoesOffset = 0x8004;
    constexpr size_t shoesCount = 4;
    constexpr size_t saveSize = sizeof(SWA::CGameParameter::SSaveData);
    if (shoesOffset + shoesCount > saveSize)
        return;

    LOGFN("AP SHOES: avant={:02X} {:02X} {:02X} {:02X}",
        bytes[shoesOffset], bytes[shoesOffset + 1],
        bytes[shoesOffset + 2], bytes[shoesOffset + 3]);

    // Les quatre flags doivent etre booleens. Sinon on refuse toute ecriture.
    for (size_t i = 0; i < shoesCount; ++i)
    {
        if (bytes[shoesOffset + i] > 1)
        {
            LOGFN("AP SHOES: flags inattendus, aucune ecriture");
            return;
        }
    }

    const uint8_t receivedMask = AP_GetReceivedShoeMask();
    LOGFN("AP SHOES: objets AP recus mask=0x{:02X}",
        static_cast<uint32_t>(receivedMask));

    static constexpr const char* names[4] =
        { "Stomp", "Wall Jump", "Air Boost", "Light Speed Dash" };

    for (size_t i = 0; i < shoesCount; ++i)
    {
        if ((receivedMask & (1u << i)) && bytes[shoesOffset + i] == 0)
        {
            bytes[shoesOffset + i] = 1;
            LOGFN("AP SHOES UNLOCK: {} offset 0x{:X} 00 -> 01",
                names[i], static_cast<uint32_t>(shoesOffset + i));
        }
    }

    LOGFN("AP SHOES: apres={:02X} {:02X} {:02X} {:02X}",
        bytes[shoesOffset], bytes[shoesOffset + 1],
        bytes[shoesOffset + 2], bytes[shoesOffset + 3]);
}

// The game checks for a bool to render the PS3 transition animation.
// It's never set so it's presumably an "is PS3" bool.
bool LoadingRenderMidAsmHook()
{
    return Config::TimeOfDayTransition == ETimeOfDayTransition::PlayStation;
}

// Patch "ui_loading.yncp" to remove the medal swinging animation.
// SWA::CCsdProject::Make
PPC_FUNC_IMPL(__imp__sub_825E4068);
PPC_FUNC(sub_825E4068)
{
    if (ctx.r4.u32 != NULL &&
        ctx.r5.u32 == 0x65C0C &&
        XXH3_64bits(base + ctx.r4.u32, ctx.r5.u32) ==
            0xD4DA1A9BE4D79BED)
    {
        // Keyframe count. First keyframe is at the center of the screen.
        PPC_STORE_U32(ctx.r4.u32 + 0x2794C, 1);
    }

    __imp__sub_825E4068(ctx, base);
}

static void AP_CheckCollectibles(const uint8_t* bytes)
{
    static unsigned scanCounter = 0;
    if (++scanCounter < 60)
        return;

    scanCounter = 0;

    // 62 CDs: RAM 0x8154, Archipelago 88000671+
    for (int n = 1; n <= 62; ++n)
    {
        const size_t offset = 0x8154 + 2 * (n - 1);

        if (bytes[offset] == 1)
            APClient::QueueLocationCheck(88000670LL + n);
    }

    // 71 livres, sauf les numeros 19 et 20.
    for (int n = 1; n <= 71; ++n)
    {
        if (n == 19 || n == 20)
            continue;

        const size_t offset = 0x81D0 + 2 * (n - 1);

        if (bytes[offset] == 1)
            APClient::QueueLocationCheck(88000732LL + n);
    }
}
static void AP_ShoesDiagnosticAtLoadingEnd();

// SWA::CLoading::Update
PPC_FUNC_IMPL(__imp__sub_824DAB60);
PPC_FUNC(sub_824DAB60)
{
    auto pLoading = (SWA::CLoading*)g_memory.Translate(ctx.r3.u32);

    const bool wasLoading = pLoading->m_LoadingDisplayType != 0;

    __imp__sub_824DAB60(ctx, base);

    if (wasLoading && !pLoading->m_LoadingDisplayType)
    {
        App::s_isLoading = false;
        AP_NotifyReverseControlsLoadingEnd();
        AP_NotifyFreezeLoadingEnd();
        LOGFN("[AP MUSIC DIAG] LOADING END");

        // AP_CUSTOM_MUSIC_RESTART_V1
        if (g_apCustomMusicRestartPending)
        {
            if (auto* doc = SWA::CGameDocument::GetInstance())
            {
                if (doc->m_pMember)
                {
                    const char* stage = doc->m_pMember->m_StageName.c_str();

                    if (stage && std::strcmp(stage, "ActD_MykonosAct1") == 0)
                    {
                        g_apCustomMusicRestartPending = false;
                        CustomMusic::RequestApotosDayAct1();
                        LOGFN("[AP MUSIC RESTART] Act 1 music requested");
                    }
                }
            }
        }

        if (auto pGameDocument = SWA::CGameDocument::GetInstance())
        {
            auto member = pGameDocument->m_pMember;

            if (member)
            {
                auto& score = member->m_ScoreInfo;

                LOGFN(
                    "LOADING END: stage={}, ranks={}/{}/{}/{}/{}, score={}",
                    member->m_StageName.c_str(),
                    static_cast<uint32_t>(score.SRank),
                    static_cast<uint32_t>(score.ARank),
                    static_cast<uint32_t>(score.BRank),
                    static_cast<uint32_t>(score.CRank),
                    static_cast<uint32_t>(score.DRank),
                    static_cast<uint32_t>(score.EnemyScore) +
                        static_cast<uint32_t>(score.TrickScore)
                );

                // AP_UNIVERSAL_DAY_LOADING_END_V1
                // Demarrer la musique regionale apres le chargement.
                // Apotos Act 1 conserve son traitement special.
                if (CustomMusic::IsEnabled())
                {
                    const char* stage = member->m_StageName.c_str();

                    struct DayStage
                    {
                        const char* prefix;
                        CustomMusic::DayRegion region;
                    };

                    static const DayStage dayStages[] = {
                        // Acts secondaires et DLC
                        { "ActD_SubMykonos_", CustomMusic::DayRegion::Apotos },
                        { "ActD_Mission_Mykonos", CustomMusic::DayRegion::Apotos },
                        { "ActD_SubEU_", CustomMusic::DayRegion::Spagonia },
                        { "ActD_Mission_EU", CustomMusic::DayRegion::Spagonia },
                        { "ActD_SubAfrica_", CustomMusic::DayRegion::Mazuri },
                        { "ActD_Mission_Africa", CustomMusic::DayRegion::Mazuri },
                        { "ActD_SubSnow_", CustomMusic::DayRegion::Holoska },
                        { "ActD_Mission_Snow", CustomMusic::DayRegion::Holoska },
                        { "ActD_SubChina_", CustomMusic::DayRegion::ChunNan },
                        { "ActD_Mission_China", CustomMusic::DayRegion::ChunNan },
                        { "ActD_SubBeach_", CustomMusic::DayRegion::Adabat },
                        { "ActD_Mission_Beach", CustomMusic::DayRegion::Adabat },
                        { "ActD_SubPetra_", CustomMusic::DayRegion::Shamar },
                        { "ActD_Mission_Petra", CustomMusic::DayRegion::Shamar },
                        { "ActD_SubNY_", CustomMusic::DayRegion::EmpireCity },
                        { "ActD_Mission_NY", CustomMusic::DayRegion::EmpireCity },
                        { "ActD_EU", CustomMusic::DayRegion::Spagonia },
                        { "ActD_Africa", CustomMusic::DayRegion::Mazuri },
                        { "ActD_Snow", CustomMusic::DayRegion::Holoska },
                        { "ActD_China", CustomMusic::DayRegion::ChunNan },
                        { "ActD_Beach", CustomMusic::DayRegion::Adabat },
                        { "ActD_Petra", CustomMusic::DayRegion::Shamar },
                        { "ActD_NY", CustomMusic::DayRegion::EmpireCity },
                        { "ActD_EggmanLand", CustomMusic::DayRegion::Eggmanland },
                        { "ActD_MykonosAct2", CustomMusic::DayRegion::Apotos }
                    };

                    for (const DayStage& entry : dayStages)
                    {
                        if (std::strncmp(stage, entry.prefix,
                            std::strlen(entry.prefix)) == 0)
                        {
                            const char* path =
                                CustomMusic::GetDayMusicPath(entry.region);

                            if (path && *path)
                            {
                                CustomMusic::RequestDayMusic(entry.region);
                                LOGFN("[AP MUSIC] Loading-end region requested: {}",
                                    entry.prefix);
                            }
                            break;
                        }
                    }
                }
                // END AP_UNIVERSAL_DAY_LOADING_END_V1
                AP_ShoesDiagnosticAtLoadingEnd();
            }
            else
            {
                LOGFN("LOADING END: GameDocument member unavailable");
            }
        }
        else
        {
            LOGFN("LOADING END: GameDocument unavailable");
        }
    }
}

// Load voice language files.
PPC_FUNC_IMPL(__imp__sub_824EB9B0);
PPC_FUNC(sub_824EB9B0)
{
    auto pApplicationDocument =
        (SWA::CApplicationDocument*)g_memory.Translate(ctx.r4.u32);

    pApplicationDocument->m_VoiceLanguage =
        (SWA::EVoiceLanguage)Config::VoiceLanguage.Value;

    __imp__sub_824EB9B0(ctx, base);
}


 // SWA::CSaveIcon::Update
PPC_FUNC_IMPL(__imp__sub_824E5170);
PPC_FUNC(sub_824E5170)
{
    auto pSaveIcon = (SWA::CSaveIcon*)g_memory.Translate(ctx.r3.u32);

    __imp__sub_824E5170(ctx, base);

    static bool wasSaving = false;
    const bool isSaving = pSaveIcon->m_IsVisible;

    // Diagnostic temporaire Archipelago.
    constexpr size_t SAVE_SIZE = sizeof(SWA::CGameParameter::SSaveData);
    static std::array<uint8_t, SAVE_SIZE> before{};
    static bool snapshotReady = false;

    auto* appDoc = SWA::CApplicationDocument::GetInstance();
    auto* gameParam = (appDoc && appDoc->m_pMember)
        ? appDoc->m_pMember->m_spGameParameter.get()
        : nullptr;

    auto* saveData = gameParam
        ? gameParam->m_pSaveData.get()
        : nullptr;

    if (saveData)
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(saveData);
        AP_CheckCollectibles(bytes);
        static unsigned apHookCounter = 0;
        if (++apHookCounter == 600)
        {
            LOGFN("AP SAVEICON HOOK ACTIVE: 600 appels");
            LOGFN("AP COLLECTIBLES TEST: CD35={:02X} CD36={:02X} CD37={:02X} BOOK71={:02X}", bytes[0x8198], bytes[0x819A], bytes[0x819C], bytes[0x825C]);
            apHookCounter = 0;
        }

       static bool firstSaveSeen = false;

if (isSaving && !wasSaving && !firstSaveSeen)
{
    firstSaveSeen = true;
    LOGFN("AP DIFF: premiere sauvegarde ignoree");
}

if (firstSaveSeen && !snapshotReady &&
    wasSaving && !isSaving)
{
    std::memcpy(before.data(), bytes, SAVE_SIZE);
    snapshotReady = true;

    LOGFN("AP DIFF: SNAPSHOT APRES PREMIERE SAUVEGARDE");
    LOGFN("AP DIFF: taille = {} octets", SAVE_SIZE);
}
      if (isSaving && !wasSaving && snapshotReady)
{
    if (!snapshotReady)
            {
                LOGFN("AP DIFF: ERREUR - snapshot absent");
            }
            else
            {
                size_t changed = 0;
                size_t displayed = 0;

                if (auto* gameDoc = SWA::CGameDocument::GetInstance())
{
    if (gameDoc->m_pMember)
    {
        LOGFN(
            "AP ACT: StageName={}",
            gameDoc->m_pMember->m_StageName.c_str()
        );
    }
}

LOGFN(
    "AP ACT: MykonosAct2 SavedRank={}",
    static_cast<uint32_t>(bytes[0x00D7])
);


LOGFN("AP DIFF: DEBUT COMPARAISON");

                for (size_t i = 0; i < SAVE_SIZE; ++i)
                {
                    if (before[i] != bytes[i])
                    {
                        ++changed;

                        if (displayed < 500)
                        {
                            LOGFN(
                                "AP DIFF: offset 0x{:04X}: {:02X} -> {:02X}",
                                static_cast<uint32_t>(i),
                                static_cast<uint32_t>(before[i]),
                                static_cast<uint32_t>(bytes[i])
                            );
                            ++displayed;
                        }
                    }
                }

                LOGFN("AP DIFF: {} octets modifies", changed);

                if (changed > displayed)
                {
                    LOGFN("AP DIFF: affichage limite aux 500 premiers changements");
                }

                LOGFN("AP DIFF: FIN COMPARAISON");
            }
        }
    }

    wasSaving = isSaving;
    App::s_isSaving = isSaving;

    static bool isSavedExtraData = false;

    if (pSaveIcon->m_IsVisible)
    {
        App::s_isSaveDataCorrupt = false;

        if (!isSavedExtraData)
        {
            AchievementManager::SaveBinary();
            PersistentStorageManager::SaveBinary();
            isSavedExtraData = true;
        }
    }
    else
    {
        isSavedExtraData = false;
    }

    
    }

// SWA::CApplicationDocument::LoadArchiveDatabases
PPC_FUNC_IMPL(__imp__sub_824EFD28);
PPC_FUNC(sub_824EFD28)
{
    auto r3 = ctx.r3;

    // SWA::CSigninXenon::InitializeDLC
    ctx.r3.u64 = PPC_LOAD_U32(r3.u32 + 4) + 200;
    ctx.r4.u64 = 0;
    sub_822C57D8(ctx, base);

    ctx.r3 = r3;
    __imp__sub_824EFD28(ctx, base);
}

// SWA::CFileReaderXenon_DLC::InitializeParallel
PPC_FUNC(sub_822C3778)
{
    if (!PPC_LOAD_U8(0x83361F10)) // ms_DLCInitialized
    {
        // CFileReaderXenon_DLC::InitializeSerial
        sub_822C2CF0(ctx, base);
    }
}

// HasAnyDLC
// NOTE: This is not used outside title screen outro,
// so it's safe to hook.
PPC_FUNC(sub_82BD06C8)
{
    ctx.r3.u64 = 0;
}

void LoadingScreenControllerMidAsmHook()
{
    static constexpr size_t STR_ADDRESSES[] =
    {
        0x820301AC, // 360_sonic1
        0x820301B8, // 360_sonic2
        0x820301C4, // 360_evil
        0x820301D0, // 360_robo
        0x820301DC, // 360_super
        0x820301E8  // 360_sonic
    };

    auto isPlayStation =
        Config::ControllerIcons == EControllerIcons::PlayStation;

    if (Config::ControllerIcons == EControllerIcons::Auto)
    {
        isPlayStation =
            hid::g_inputDeviceController == hid::EInputDevice::PlayStation;
    }

    const char* prefix = isPlayStation ? "ps3" : "360";

    for (auto address : STR_ADDRESSES)
    {
        memcpy(g_memory.Translate(address), prefix, 3);
    }
}
