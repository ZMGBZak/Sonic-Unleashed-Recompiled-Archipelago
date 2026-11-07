

void AP_RequestReverseControlsCancel();
void AP_RequestFreezeRestart();
/* Hook function for when checkpoints are activated
   to preserve the current checkpoint score. */
#include <apu/custom_music.h>
#include <api/SWA.h>
#include <apu/custom_music.h>
#include <ui/game_window.h>
#include <apu/custom_music.h>
#include <user/config.h>
#include <apu/custom_music.h>
#include <os/logger.h>
#include <apu/custom_music.h>
#include <app.h>
#include <apu/custom_music.h>
#include <sdl_events.h>
#include <string>

static uint32_t g_lastEnemyScore;
static uint32_t g_lastTrickScore;
// AP_CHECKPOINT_SCORE_V1: bind checkpoint scores to the actual stage.
static std::string g_checkpointScoreStage;
static bool g_hasCheckpointScore = false;
static float g_lastDarkGaiaEnergy;
static bool g_isUnleashCancelled;

PPC_FUNC_IMPL(__imp__sub_82624308);

PPC_FUNC(sub_82624308)
{
    const uint32_t entryR3 = ctx.r3.u32;
    const uint32_t entryR4 = ctx.r4.u32;
    const uint32_t entryR5 = ctx.r5.u32;
    const uint32_t entryR6 = ctx.r6.u32;

    // Capture f1 before the original function changes it.
    const double entryF1 = ctx.f1.f64;

    __imp__sub_82624308(ctx, base);

    LOGFN(
        "CHECKPOINT ENTRY! r3={:08X} r4={:08X} r5={:08X} r6={:08X}",
        entryR3,
        entryR4,
        entryR5,
        entryR6
    );

    LOGFN(
        "AP CHECKPOINT TEST: f1={} r3={:08X}",
        entryF1,
        entryR3
    );

    LOGFN("AP TEST: sub_82624308 triggered");

    if (!Config::SaveScoreAtCheckpoints)
        return;

    if (auto pGameDocument = SWA::CGameDocument::GetInstance())
    {
        if (!pGameDocument->m_pMember)
            return;

        g_lastEnemyScore =
            pGameDocument->m_pMember->m_ScoreInfo.EnemyScore;

        g_lastTrickScore =
            pGameDocument->m_pMember->m_ScoreInfo.TrickScore;

        // The checkpoint state must not leak into another stage.
        g_checkpointScoreStage = pGameDocument->m_pMember->m_StageName.c_str();
        g_hasCheckpointScore = !g_checkpointScoreStage.empty();

        LOGFN("[AP CHECKPOINT SCORE] saved stage={} enemy={} trick={}",
            g_checkpointScoreStage, g_lastEnemyScore, g_lastTrickScore);
    }
}

// AP_CHECKPOINT_SCORE_V1
// The original function clears the score on respawn. Reapply only the last
// checkpoint snapshot from this same Act. The mid-ASM full restart hook below
// clears the snapshot before this function runs on a manual Act restart.
PPC_FUNC_IMPL(__imp__sub_8245F048);
PPC_FUNC(sub_8245F048)
{
    __imp__sub_8245F048(ctx, base);

    if (!Config::SaveScoreAtCheckpoints)
        return;

    auto* doc = SWA::CGameDocument::GetInstance();
    if (!doc || !doc->m_pMember)
        return;

    const char* stage = doc->m_pMember->m_StageName.c_str();
    if (!g_hasCheckpointScore || !stage || g_checkpointScoreStage != stage)
    {
        // No checkpoint reached in this Act, or a different Act: leave 0.
        LOGFN("[AP CHECKPOINT SCORE] no valid checkpoint for stage={}",
            stage ? stage : "");
        return;
    }

    auto& score = doc->m_pMember->m_ScoreInfo;
    score.EnemyScore = g_lastEnemyScore;
    score.TrickScore = g_lastTrickScore;
    LOGFN("[AP CHECKPOINT SCORE] restored stage={} enemy={} trick={}",
        g_checkpointScoreStage, g_lastEnemyScore, g_lastTrickScore);
}
// AP_CHECKPOINT_SCORE_V1_END

void AP_NotifyCustomMusicRestart(bool wasPlaying);

void ResetScoreOnRestartMidAsmHook()
{
    AP_RequestReverseControlsCancel();
    AP_RequestFreezeRestart();

    LOGFN("[AP REVERSE RESPAWN TEST] Restart hook declenche !");
    LOGFN("[AP MUSIC DIAG] RESTART detected");
    // AP_CUSTOM_MUSIC_RESTART_V1
    AP_NotifyCustomMusicRestart(CustomMusic::IsPlaying());
    g_lastEnemyScore = 0;
    g_lastTrickScore = 0;
    g_hasCheckpointScore = false;
    g_checkpointScoreStage.clear();
}

void PostUnleashMidAsmHook(PPCRegister& r30)
{
    if (!g_isUnleashCancelled)
        return;

    if (auto pEvilSonicContext =
        (SWA::Player::CEvilSonicContext*)g_memory.Translate(r30.u32))
    {
        pEvilSonicContext->m_DarkGaiaEnergy =
            std::max(0.0f, g_lastDarkGaiaEnergy - 35.0f);
    }

    g_isUnleashCancelled = false;
}

// AP DEBUG - First checkpoint-related caller.
PPC_FUNC_IMPL(__imp__sub_826248F0);

PPC_FUNC(sub_826248F0)
{
    LOGFN("AP TEST: CHECKPOINT FUNCTION A (826248F0)");
    __imp__sub_826248F0(ctx, base);
}

// AP DEBUG - Second checkpoint-related caller.
PPC_FUNC_IMPL(__imp__sub_82624980);

PPC_FUNC(sub_82624980)
{
    LOGFN("AP TEST: CHECKPOINT FUNCTION B (82624980)");
    __imp__sub_82624980(ctx, base);
}



// AP TEST : detecter les appels de la fonction Werehog.
PPC_FUNC_IMPL(__imp__sub_823AFDD0);

PPC_FUNC(sub_823AFDD0)
{
    static unsigned int apCalls = 0;

    if (++apCalls % 100 == 0)
    {
        LOGFN("AP PLAYER HOOK TEST: 823AFDD0 - {} appels", apCalls);
    }

    static bool apPointerChecked = false;

    if (apCalls % 100 == 0)
    {
        const uint32_t apPlayer = ctx.r3.u32;
        const uint32_t apStats = apPlayer != 0 ? PPC_LOAD_U32(apPlayer + 196) : 0;

        LOGFN("AP FULL RING POINTER TEST: player={:08X} stats={:08X}",
            apPlayer, apStats);

        apPointerChecked = true;
    }

    __imp__sub_823AFDD0(ctx, base);
}




extern uint32_t g_apQteManagerWatch;

// AP SONIC DAY POSTURE TEST
PPC_FUNC_IMPL(__imp__sub_8234F4D0);

PPC_FUNC(sub_8234F4D0)
{
    static unsigned int apDayCalls = 0;

    if (++apDayCalls == 1 || apDayCalls % 60 == 0)
    {
        LOGFN("AP SONIC DAY POSTURE TEST: 8234F4D0 - {} appels", apDayCalls);
        LOGFN("[AP QTE MANAGER WATCH] Calls={} manager=0x{:08X}", apDayCalls, g_apQteManagerWatch);
    }

    __imp__sub_8234F4D0(ctx, base);
}






// AP SONIC DEATH TEST - diagnostic uniquement.
void AP_SonicDeathMidAsmHook()
{
    LOGFN("[AP SONIC DEATH TEST] Mort detectee !");
}



