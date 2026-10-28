#include "ap_werehog_burn_damage.h"
#include <apu/custom_music.h>
#include <atomic>
#include <string>
#include <archipelago_client.h>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <os/logger.h>
#include <api/SWA.h>
#include <hid/hid.h>
#include <app.h>
#include <exports.h>
#include <sdl_listener.h>

constexpr double WORLD_MAP_ROTATE_DEADZONE = 0.69999999;
constexpr double WORLD_MAP_CURSOR_DEADZONE = 0.30000001;

class WorldMapCursorParams
{
public:
    float Damping{ 0.99f };
    float FlickAccelX{ 0.25f };
    float FlickAccelY{ 0.1f };
    float FlickTerminalVelocity{ 40.0f };
    float FlickThreshold{ 2.25f };
    float SensitivityX{};
    float SensitivityY{};
    float Smoothing{ 0.8f };
};

class WorldMapCursorParamsProspero : public WorldMapCursorParams
{
public:
    WorldMapCursorParamsProspero()
    {
        SensitivityX = 1.15f;
        SensitivityY = 1.05f;
    }
}
g_worldMapCursorParamsProspero;

class WorldMapCursorParamsOrbis : public WorldMapCursorParams
{
public:
    WorldMapCursorParamsOrbis()
    {
        SensitivityX = 0.95f;
        SensitivityY = 1.0f;
    }
}
g_worldMapCursorParamsOrbis;

#ifdef UNLEASHED_RECOMP_UI_KBM_SUPPORT
class WorldMapCursorParamsMouse : public WorldMapCursorParams
{
public:
    WorldMapCursorParamsMouse()
    {
        FlickAccelX = 0.025f;
        FlickAccelY = 0.025f;
        FlickThreshold = 7.5f;
        SensitivityX = 0.15f;
        SensitivityY = 0.15f;
    }
}
g_worldMapCursorParamsMouse;
#endif

WorldMapCursorParams g_worldMapCursorParams{};

static bool g_isCursorActive;

static float g_worldMapCursorVelocityX;
static float g_worldMapCursorVelocityY;

class SDLEventListenerForInputPatches : public SDLEventListener
{
    static inline bool ms_isMouseDown;

    static inline int ms_touchpadFingerCount;

    static inline float ms_cursorX;
    static inline float ms_cursorY;
    static inline float ms_cursorDeltaX;
    static inline float ms_cursorDeltaY;
    static inline float ms_cursorPrevX;
    static inline float ms_cursorPrevY;

public:
    static void Update(float deltaTime)
    {
        /* NOTE (Hyper): this code was written at 144Hz and was
           discovered later to be faulty at any other frame rate,
           so this is here to account for that without changing
           all the constants that I had tuned. */
        constexpr auto referenceDeltaTime = 1.0f / 144.0f;

        if (g_isCursorActive)
        {
            auto dxNorm = ms_cursorDeltaX / referenceDeltaTime;
            auto dyNorm = ms_cursorDeltaY / referenceDeltaTime;
            auto dxSens = dxNorm * g_worldMapCursorParams.SensitivityX;
            auto dySens = dyNorm * g_worldMapCursorParams.SensitivityY;

            auto smoothing = powf(g_worldMapCursorParams.Smoothing, deltaTime / referenceDeltaTime);

            g_worldMapCursorVelocityX = smoothing * g_worldMapCursorVelocityX + (1.0f - smoothing) * dxSens;
            g_worldMapCursorVelocityY = smoothing * g_worldMapCursorVelocityY + (1.0f - smoothing) * dySens;

            auto flickThreshold = g_worldMapCursorParams.FlickThreshold;

            if (fabs(dxSens) > flickThreshold || fabs(dySens) > flickThreshold)
            {
                g_worldMapCursorVelocityX += dxNorm * g_worldMapCursorParams.FlickAccelX * (deltaTime / referenceDeltaTime);
                g_worldMapCursorVelocityY += dyNorm * g_worldMapCursorParams.FlickAccelY * (deltaTime / referenceDeltaTime);
            }

            auto terminalVelocity = g_worldMapCursorParams.FlickTerminalVelocity;

            g_worldMapCursorVelocityX = std::clamp(g_worldMapCursorVelocityX, -terminalVelocity, terminalVelocity);
            g_worldMapCursorVelocityY = std::clamp(g_worldMapCursorVelocityY, -terminalVelocity, terminalVelocity);
        }
        else
        {
            auto dampingFactor = powf(g_worldMapCursorParams.Damping, deltaTime / referenceDeltaTime);

            g_worldMapCursorVelocityX *= dampingFactor;
            g_worldMapCursorVelocityY *= dampingFactor;
        }
    }

    bool OnSDLEvent(SDL_Event* event) override
    {
        if (!hid::IsInputAllowed())
            return false;

        switch (event->type)
        {
#ifdef UNLEASHED_RECOMP_UI_KBM_SUPPORT
            case SDL_MOUSEMOTION:
            {
                if (!ms_isMouseDown)
                    break;
            
                g_isCursorActive = true;
            
                ms_cursorDeltaX = (float)event->motion.xrel / 100.0f;
                ms_cursorDeltaY = (float)event->motion.yrel / 100.0f;
            
                break;
            }
            
            case SDL_MOUSEBUTTONDOWN:
            {
                if (event->button.button == SDL_BUTTON_LEFT)
                {
                    g_worldMapCursorParams = g_worldMapCursorParamsMouse;
                    ms_isMouseDown = true;
                }
            
                break;
            }
            
            case SDL_MOUSEBUTTONUP:
            {
                if (event->button.button == SDL_BUTTON_LEFT)
                {
                    g_isCursorActive = false;
                    ms_isMouseDown = false;
                }
            
                break;
            }
#endif
            case SDL_CONTROLLERTOUCHPADMOTION:
            {
                g_isCursorActive = true;

                if (ms_touchpadFingerCount > 1)
                {
                    g_isCursorActive = false;
                    break;
                }

                ms_cursorX = event->ctouchpad.x;
                ms_cursorY = event->ctouchpad.y;
                ms_cursorDeltaX = ms_cursorX - ms_cursorPrevX;
                ms_cursorDeltaY = ms_cursorY - ms_cursorPrevY;
                ms_cursorPrevX = ms_cursorX;
                ms_cursorPrevY = ms_cursorY;

                break;
            }

            case SDL_CONTROLLERTOUCHPADDOWN:
            {
                g_worldMapCursorParams = hid::g_inputDeviceExplicit == hid::EInputDeviceExplicit::DualSense
                    ? (WorldMapCursorParams)g_worldMapCursorParamsProspero
                    : (WorldMapCursorParams)g_worldMapCursorParamsOrbis;

                ms_touchpadFingerCount++;
                ms_cursorPrevX = event->ctouchpad.x;
                ms_cursorPrevY = event->ctouchpad.y;

                break;
            }

            case SDL_CONTROLLERTOUCHPADUP:
                g_isCursorActive = false;
                ms_touchpadFingerCount--;
                break;
        }

        return false;
    }
}
g_sdlEventListenerForInputPatches;

// -------------- COMMON --------------- //

static bool IsDPadThreshold(const SWA::SPadState* pPadState)
{
    if (Config::DisableDPadMovement)
        return false;

    return pPadState->IsDown(SWA::eKeyState_DpadUp)   ||
           pPadState->IsDown(SWA::eKeyState_DpadDown) ||
           pPadState->IsDown(SWA::eKeyState_DpadLeft) ||
           pPadState->IsDown(SWA::eKeyState_DpadRight);
}

static bool IsLeftStickThreshold(const SWA::SPadState* pPadState, double deadzone = 0)
{
    return sqrtl((pPadState->LeftStickHorizontal * pPadState->LeftStickHorizontal) +
        (pPadState->LeftStickVertical * pPadState->LeftStickVertical)) > deadzone;
}

static bool IsCursorThreshold(double deadzone = 0, bool isBelowThreshold = false)
{
    auto sqrt = sqrtl((g_worldMapCursorVelocityX * g_worldMapCursorVelocityX) +
        (g_worldMapCursorVelocityY * g_worldMapCursorVelocityY));

    if (isBelowThreshold)
        return sqrt < deadzone;

    return sqrt >= deadzone;
}

static void SetDPadAnalogDirectionX(PPCRegister& pPadState, PPCRegister& x, bool invert, float max = 1.0f)
{
    if (Config::DisableDPadMovement)
        return;

    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadLeft))
        x.f64 = invert ? max : -max;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadRight))
        x.f64 = invert ? -max : max;
}

static void SetDPadAnalogDirectionY(PPCRegister& pPadState, PPCRegister& y, bool invert, float max = 1.0f)
{
    if (Config::DisableDPadMovement)
        return;

    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadUp))
        y.f64 = invert ? -max : max;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadDown))
        y.f64 = invert ? max : -max;
}

// -------------- PLAYER --------------- //

void PostureDPadSupportMidAsmHook(PPCRegister& pPadState, PPCRegister& x, PPCRegister& y)
{
    SetDPadAnalogDirectionX(pPadState, x, false);
    SetDPadAnalogDirectionY(pPadState, y, false);
}

void PostureDPadSupportInvertYMidAsmHook(PPCRegister& pPadState, PPCRegister& x, PPCRegister& y)
{
    SetDPadAnalogDirectionX(pPadState, x, false);
    SetDPadAnalogDirectionY(pPadState, y, true);
}

void PostureDPadSupportXMidAsmHook(PPCRegister& pPadState, PPCRegister& x)
{
    SetDPadAnalogDirectionX(pPadState, x, false);
}

void PostureDPadSupportYMidAsmHook(PPCRegister& pPadState, PPCRegister& y)
{
    SetDPadAnalogDirectionY(pPadState, y, false);
}

void PostureSpaceHurrierDPadSupportXMidAsmHook(PPCRegister& pPadState, PPCVRegister& vector)
{
    if (Config::DisableDPadMovement)
        return;
    
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadLeft))
        vector.f32[3] = -1.0f;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadRight))
        vector.f32[3] = 1.0f;
}

void PostureSpaceHurrierDPadSupportYMidAsmHook(PPCRegister& pPadState, PPCVRegister& vector)
{
    if (Config::DisableDPadMovement)
        return;

    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadUp))
        vector.f32[3] = 1.0f;

    if (pGuestPadState->IsDown(SWA::eKeyState_DpadDown))
        vector.f32[3] = -1.0f;
}

void SetXButtonHomingMidAsmHook(PPCRegister& r1)
{
    auto pXButtonHoming = (bool*)(g_memory.base + r1.u32 + 0x63);

    *pXButtonHoming = !Config::HomingAttackOnJump;
}

bool IsHomingAttackOnJump()
{
    return Config::HomingAttackOnJump;
}

// ------------- WORLD MAP ------------- //

bool WorldMapDeadzoneMidAsmHook(PPCRegister& pPadState)
{
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (IsDPadThreshold(pGuestPadState) || IsLeftStickThreshold(pGuestPadState))
    {
        g_worldMapCursorVelocityX = 0;
        g_worldMapCursorVelocityY = 0;
    }
    else
    {
        SDLEventListenerForInputPatches::Update(App::s_deltaTime);

        /* Reduce noise if the cursor is resting in
           place, but allow much precise values for
           proper interpolation to zero. */
        if (IsCursorThreshold(0.05, true))
            return !g_isCursorActive;

        return IsCursorThreshold();
    }

    return IsDPadThreshold(pGuestPadState) || IsLeftStickThreshold(pGuestPadState, WORLD_MAP_ROTATE_DEADZONE);
}

bool WorldMapMagnetismMidAsmHook(PPCRegister& f0)
{
    if (IsCursorThreshold(f0.f64, true))
    {
        g_worldMapCursorVelocityX = 0;
        g_worldMapCursorVelocityY = 0;

        return true;
    }

    return false;
}

void WorldMapHidSupportXMidAsmHook(PPCRegister& pPadState, PPCRegister& x)
{
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (IsDPadThreshold(pGuestPadState))
    {
        SetDPadAnalogDirectionX(pPadState, x, false);
    }
    else if (fabs(g_worldMapCursorVelocityX) > 0)
    {
        x.f64 = -g_worldMapCursorVelocityX;
    }
}

void WorldMapHidSupportYMidAsmHook(PPCRegister& pPadState, PPCRegister& y)
{
    auto pGuestPadState = (SWA::SPadState*)g_memory.Translate(pPadState.u32);

    if (IsDPadThreshold(pGuestPadState))
    {
        SetDPadAnalogDirectionY(pPadState, y, false);
    }
    else if (fabs(g_worldMapCursorVelocityY) > 0)
    {
        y.f64 = g_worldMapCursorVelocityY;
    }
}

// SWA::CWorldMapCamera::Update
PPC_FUNC_IMPL(__imp__sub_82486968);
PPC_FUNC(sub_82486968)
{
    auto pWorldMapCamera = (SWA::CWorldMapCamera*)g_memory.Translate(ctx.r3.u32);

    // Reset vertical velocity if maximum pitch reached.
    if (fabs(pWorldMapCamera->m_Pitch) >= 80.0f)
        g_worldMapCursorVelocityY = 0;

    __imp__sub_82486968(ctx, base);
}

// World Map cursor move hook.
PPC_FUNC(sub_8256C938)
{
    auto pWorldMapCursor = (SWA::CWorldMapCursor*)g_memory.Translate(ctx.r3.u32);

    pWorldMapCursor->m_IsCursorMoving = g_isCursorActive && IsCursorThreshold(1.0);

    if (ctx.r4.u8)
    {
        pWorldMapCursor->m_LeftStickVertical = 0;
        pWorldMapCursor->m_LeftStickHorizontal = 0;
    }
    else if (auto pInputState = SWA::CInputState::GetInstance())
    {
        auto& rPadState = pInputState->GetPadState();

        pWorldMapCursor->m_LeftStickVertical = rPadState.LeftStickVertical;
        pWorldMapCursor->m_LeftStickHorizontal = rPadState.LeftStickHorizontal;

        if (!Config::DisableDPadMovement)
        {
            if (rPadState.IsDown(SWA::eKeyState_DpadUp))
                pWorldMapCursor->m_LeftStickVertical = 1.0f;
    
            if (rPadState.IsDown(SWA::eKeyState_DpadDown))
                pWorldMapCursor->m_LeftStickVertical = -1.0f;
    
            if (rPadState.IsDown(SWA::eKeyState_DpadLeft))
                pWorldMapCursor->m_LeftStickHorizontal = -1.0f;
    
            if (rPadState.IsDown(SWA::eKeyState_DpadRight))
                pWorldMapCursor->m_LeftStickHorizontal = 1.0f;
        }

        if (sqrtl((pWorldMapCursor->m_LeftStickHorizontal * pWorldMapCursor->m_LeftStickHorizontal) +
            (pWorldMapCursor->m_LeftStickVertical * pWorldMapCursor->m_LeftStickVertical)) > WORLD_MAP_ROTATE_DEADZONE)
        {
            pWorldMapCursor->m_IsCursorMoving = true;
        }
    }
}

// BEGIN AP_FREEZE_AUTO_GUARD_V1
namespace ApFreezeAuto
{
    static std::atomic<bool> restartRequested{false};
    static std::atomic<bool> loadingFinished{false};
    static bool blocked = true;
    static std::chrono::steady_clock::time_point readyAt{};
    static constexpr auto loadingDelay = std::chrono::seconds(3);

    void RequestRestart()
    {
        restartRequested.store(true, std::memory_order_release);
    }

    void NotifyLoadingEnd()
    {
        loadingFinished.store(true, std::memory_order_release);
    }

    bool CanTrigger()
    {
        if (restartRequested.exchange(false, std::memory_order_acq_rel))
        {
            blocked = true;
            loadingFinished.store(false, std::memory_order_release);
            APClient::CancelFreeze();
            return false;
        }

        if (loadingFinished.exchange(false, std::memory_order_acq_rel))
        {
            readyAt = std::chrono::steady_clock::now() + loadingDelay;
            blocked = false;
        }

        return !blocked &&
            std::chrono::steady_clock::now() >= readyAt;
    }
}

void AP_RequestFreezeRestart()
{
    ApFreezeAuto::RequestRestart();
}

void AP_NotifyFreezeLoadingEnd()
{
    ApFreezeAuto::NotifyLoadingEnd();
}
// END AP_FREEZE_AUTO_GUARD_V1
// AP Reverse Controls Trap - etat du minuteur.
static bool apReverseControlsActive = false;
static bool apReverseRestartBlocked = false;
static std::atomic<bool> apReverseLoadingFinished{false};

void AP_NotifyReverseControlsLoadingEnd()
{
    apReverseLoadingFinished.store(true);
}
static std::string apReverseControlsStartStage;
static std::chrono::steady_clock::time_point apReverseControlsEndTime{};
static constexpr auto apReverseControlsDuration = std::chrono::seconds(10);
// AP Reverse Controls Trap - niveaux autorises.
static bool AP_IsReverseControlsStageAllowed()
{
    const auto* doc = SWA::CGameDocument::GetInstance();

    if (!doc || !doc->m_pMember)
        return false;

    const char* stage = doc->m_pMember->m_StageName.c_str();

    return
        std::strncmp(stage, "ActD_", 5) == 0 ||
        std::strncmp(stage, "ActN_", 5) == 0 ||
        std::strcmp(stage, "Act_EggmanLand") == 0 ||
        std::strcmp(stage, "BossEggBeetle") == 0 ||
        std::strcmp(stage, "BossEggLancer") == 0 ||
        std::strcmp(stage, "BossEggRayBird") == 0 ||
        std::strcmp(stage, "BossDarkGaiaMoray") == 0 ||
        std::strcmp(stage, "BossPhoenix") == 0 ||
        std::strcmp(stage, "BossPetra") == 0 ||
        std::strcmp(stage, "BossEggDragoon") == 0;
}
// AP Reverse Controls Trap - signal d'annulation au Restart.
static std::atomic<bool> apReverseRestartRequested{false};

void AP_RequestReverseControlsCancel()
{
    apReverseRestartRequested.store(true);
}
// AP Reverse Controls Trap - diagnostic initial
PPC_FUNC_IMPL(__imp__sub_822C0288);
PPC_FUNC(sub_822C0288)
{
    const uint32_t apPadArray = ctx.r4.u32;
    const uint32_t apPadCount = ctx.r5.u32;

    __imp__sub_822C0288(ctx, base);

    const auto now = std::chrono::steady_clock::now();
    const bool allowedStage = AP_IsReverseControlsStageAllowed();

    // Annulation demandee par le hook de Restart.
    if (apReverseRestartRequested.exchange(false))
    {
        apReverseControlsActive = false;
        apReverseControlsStartStage.clear();
        apReverseRestartBlocked = true;

        LOGFN("[AP REVERSE] Annule apres Restart");

        // Ne pas reactiver un Trap pendant ce meme appel.
        return;
    }

    // Fin du chargement : autoriser de nouveau les Traps.
    if (apReverseLoadingFinished.exchange(false))
    {
        apReverseRestartBlocked = false;
        LOGFN("[AP REVERSE] Chargement termine - Traps debloques");
    }

    // Aucun nouveau Trap pendant un Restart.
    if (apReverseRestartBlocked)
        return;
    const auto* apReverseDoc = SWA::CGameDocument::GetInstance();
    const std::string apCurrentReverseStage =
        (apReverseDoc && apReverseDoc->m_pMember)
        ? apReverseDoc->m_pMember->m_StageName.c_str()
        : "";

    const bool apReverseStageChanged =
        apReverseControlsActive &&
        (!allowedStage ||
         apCurrentReverseStage != apReverseControlsStartStage);

    if (apReverseControlsActive &&
        (apReverseStageChanged ||
         now >= apReverseControlsEndTime))
    {
        apReverseControlsActive = false;
        LOGFN("[AP REVERSE] Desactive");
    }

    if (!allowedStage || apReverseStageChanged)
        return;

    // Ne pas consommer un Trap sans donnees de manette valides.
    if (apPadArray == 0 || apPadCount == 0 || apPadCount > 4)
        return;

    if (!apReverseControlsActive &&
        APClient::GetPendingReverseControls() > 0)
    {
        apReverseControlsActive = true;
        apReverseControlsStartStage = apCurrentReverseStage;
        apReverseControlsEndTime = now + apReverseControlsDuration;

        APClient::ConfirmReverseControlsApplied();

        LOGFN("[AP REVERSE] ACTIVE POUR 10 SECONDES !");
    }

    if (!apReverseControlsActive)
        return;

    if (apPadArray == 0 || apPadCount == 0 || apPadCount > 4)
        return;

    for (uint32_t i = 0; i < apPadCount; ++i)
    {
        const uint32_t pad = apPadArray + i * 20;

        const int16_t x =
            static_cast<int16_t>(PPC_LOAD_U16(pad + 8));

        const int16_t y =
            static_cast<int16_t>(PPC_LOAD_U16(pad + 10));

        const int16_t reverseX =
            x == -32768 ? 32767 : -x;

        const int16_t reverseY =
            y == -32768 ? 32767 : -y;

        PPC_STORE_U16(pad + 8, static_cast<uint16_t>(reverseX));
        PPC_STORE_U16(pad + 10, static_cast<uint16_t>(reverseY));
    }
}

// ============================================================
// AP QTE OF DOOM - Diagnostic Sonic Trick Jump
// ============================================================
PPC_FUNC_IMPL(__imp__sub_82332738);

PPC_FUNC(sub_82332738)
{
    static uint32_t apQteCallCount = 0;
    ++apQteCallCount;

    if (apQteCallCount <= 30)
    {
        LOGFN(
            "[AP QTE TEST] Call={} r3=0x{:08X} r4=0x{:08X}",
            apQteCallCount,
            ctx.r3.u32,
            ctx.r4.u32
        );
    }

    __imp__sub_82332738(ctx, base);
}
// AP QTE OF DOOM - Diagnostic creation QTE
PPC_FUNC_IMPL(__imp__sub_82313BA0);
PPC_FUNC(sub_82313BA0)
{
    static uint32_t apQteCreateCount = 0;
    ++apQteCreateCount;

    if (apQteCreateCount <= 20)
    {
        LOGFN("[AP QTE CREATE] Call={} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r6=0x{:08X}",
              apQteCreateCount, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
    }

    __imp__sub_82313BA0(ctx, base);
}

// AP QTE of Doom - diagnostic du demarrage natif
PPC_FUNC_IMPL(__imp__sub_8233FAB8);
PPC_FUNC(sub_8233FAB8)
{
    static unsigned int apQteStartCalls = 0;
    static uint32_t apLastQteObject = 0;

    ++apQteStartCalls;

    const uint32_t currentObject = ctx.r3.u32;
    const bool objectChanged = currentObject != apLastQteObject;

    LOGFN(
        "[AP QTE OBJECT WATCH] Call={} object=0x{:08X} previous=0x{:08X} changed={}",
        apQteStartCalls,
        currentObject,
        apLastQteObject,
        objectChanged
    );

    apLastQteObject = currentObject;

    __imp__sub_8233FAB8(ctx, base);
}

uint32_t g_apQteManagerWatch = 0;

// AP QTE of Doom - diagnostic activation native
PPC_FUNC_IMPL(__imp__sub_82333D30);
PPC_FUNC(sub_82333D30)
{
    static unsigned int apQteActivateCalls = 0;
    ++apQteActivateCalls;

    if (apQteActivateCalls <= 40)
    {
        LOGFN(
            "[AP QTE ACTIVATE TEST] Call={} r3=0x{:08X} r4=0x{:08X}",
            apQteActivateCalls,
            ctx.r3.u32,
            ctx.r4.u32
        );
    }

    g_apQteManagerWatch = ctx.r3.u32;
    __imp__sub_82333D30(ctx, base);
}



// BEGIN AP_QTE_OF_DOOM v2
// =====================================================================
// AP: QTE of Doom (Sonic Day) - declenche un vrai Trick Jump natif.
// On construit un MsgHitTrickJump sur la pile GUEST (aucune allocation,
// rien a liberer) et on appelle le handler natif sub_82314FE0 depuis le
// thread de jeu via le hook CSonic::Update deja existant.
// Le contexte PPC du hook est utilise SUR PLACE (jamais copie) : r1/r3..r10
// sont sauvegardes puis restaures, fpscr reste donc coherent.
// Appeler AP_QteOfDoomQueue() (thread quelconque) pour armer la trap.
// =====================================================================
#include <atomic>
#include <random>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

PPC_EXTERN_FUNC(sub_8258C4E0);
PPC_EXTERN_FUNC(sub_8231B7D8);
PPC_EXTERN_FUNC(sub_82333D78);
PPC_EXTERN_FUNC(sub_82317698);
PPC_EXTERN_FUNC(sub_82E84BE0);
PPC_FUNC_IMPL(__imp__sub_82314FE0);

namespace ApQteDoom
{
    std::atomic<bool> g_pending{false};
    constexpr uint32_t kCSonicVtable = 0x82016BF4; // Player::CSonic, Sonic Day

    // Disposition du scratch guest (offsets depuis S ; S est un multiple de 16).
    constexpr uint32_t kMsg = 0x000, kMsgSize = 0x090;   // MsgHitTrickJump (144 octets)
    constexpr uint32_t kEnt = 0x090, kEntSize = 0x01C;   // 1 entree de 28 octets
    constexpr uint32_t kBtn = 0x0B0, kBtnMax  = 5 * 4;   // boutons (5 x uint32 max)
    constexpr uint32_t kQuat = 0x100, kQuatSize = 0x10;  // copie du quaternion de Sonic
    constexpr uint32_t kMat = 0x120, kMatSize = 0x40;    // matrice produite par sub_82E84BE0
    constexpr uint32_t kZeroEnd = kBtn + kBtnMax;        // zone remise a zero avant construction
    constexpr uint32_t kSpDrop = 0x400;                  // r1 abaisse de 0x400
    constexpr uint32_t kGap = 0x100;                     // scratch place 0x100 AU-DESSUS du nouveau r1
    static_assert(kMsg + kMsgSize <= kEnt, "msg/entree se chevauchent");
    static_assert(kEnt + kEntSize <= kBtn, "entree/boutons se chevauchent");
    static_assert(kBtn + kBtnMax <= kQuat, "boutons/quaternion se chevauchent");
    static_assert(kQuat + kQuatSize <= kMat, "quaternion/matrice se chevauchent");
    static_assert(kEnt % 16 == 0 && kBtn % 16 == 0 && kQuat % 16 == 0 && kMat % 16 == 0, "alignement 16");
    static_assert(kGap + kMat + kMatSize + 0x40 <= kSpDrop, "scratch deborde sur la frame de l'appelant");

    // Sauvegarde/restaure les registres de ctx modifies par nos appels natifs.
    struct CtxGuard
    {
        PPCContext& c;
        uint64_t r1, r3, r4, r5, r6, r7, r8, r9, r10;
        explicit CtxGuard(PPCContext& ctx)
            : c(ctx), r1(ctx.r1.u64), r3(ctx.r3.u64), r4(ctx.r4.u64), r5(ctx.r5.u64),
              r6(ctx.r6.u64), r7(ctx.r7.u64), r8(ctx.r8.u64), r9(ctx.r9.u64), r10(ctx.r10.u64) {}
        ~CtxGuard()
        {
            c.r1.u64 = r1; c.r3.u64 = r3; c.r4.u64 = r4; c.r5.u64 = r5; c.r6.u64 = r6;
            c.r7.u64 = r7; c.r8.u64 = r8; c.r9.u64 = r9; c.r10.u64 = r10;
        }
    };

    static void Log(const char* msg)
    {
        if (FILE* f = std::fopen("ap_qte_doom.log", "a"))
        {
            std::fprintf(f, "%s\n", msg);
            std::fclose(f);
        }
    }

    static float BitsToFloat(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
    static uint32_t FloatToBits(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
    static float RdF(uint8_t* base, uint32_t a) { return BitsToFloat(PPC_LOAD_U32(a)); }
    static void WrF(uint8_t* base, uint32_t a, float f) { PPC_STORE_U32(a, FloatToBits(f)); }

    typedef void GuestFn(PPCContext&, uint8_t*);

    static uint32_t Call(PPCContext& ctx, uint8_t* base, GuestFn* fn, uint32_t a0, uint32_t a1 = 0)
    {
        ctx.r3.u64 = a0;
        ctx.r4.u64 = a1;
        fn(ctx, base);
        return ctx.r3.u32;
    }

    // Appel virtuel vtable[3] (comme sub_8231B6F0), avec controle de la cible.
    static uint32_t CallVirt12(PPCContext& ctx, uint8_t* base, uint32_t obj)
    {
        const uint32_t vt = PPC_LOAD_U32(obj);
        if (!vt) return 0;
        const uint32_t fn = PPC_LOAD_U32(vt + 12);
        if (fn < PPC_CODE_BASE || fn >= PPC_CODE_BASE + PPC_CODE_SIZE || (fn & 3u)) return 0;
        PPCFunc* target = PPC_LOOKUP_FUNC(base, fn);
        if (!target) return 0;
        ctx.r3.u64 = obj;
        target(ctx, base);
        return ctx.r3.u32;
    }

    static bool FlagA(PPCContext& ctx, uint8_t* base, uint32_t sctx, uint32_t id)
    {
        return (Call(ctx, base, sub_82333D78, PPC_LOAD_U32(sctx + 1196), id) & 0xFFu) != 0;
    }

    // nullptr si le message a ete envoye ; sinon la raison du refus (nouvel essai a la frame suivante).
    static const char* TryFire(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t sonic = ctx.r3.u32;               // CSonic* vivant, jamais memorise
        const uint32_t sp0 = ctx.r1.u32;
        if (!sonic) return "sonic=0";
        if (PPC_LOAD_U32(sonic) != kCSonicVtable) return "pas CSonic";
        if (sp0 < 0x10000u) return "r1 invalide";

        CtxGuard guard(ctx);                             // restaure r1, r3..r10 a la sortie
        const uint32_t newSp = (sp0 - kSpDrop) & ~0xFu;
        ctx.r1.u64 = newSp;                              // frames natives sous newSp ; scratch au-dessus
        const uint32_t S = newSp + kGap;

        const uint32_t sctx = Call(ctx, base, sub_8258C4E0, sonic);
        if (!sctx) return "ctx=0";
        if (Call(ctx, base, sub_8231B7D8, sctx) & 0xFFu) return "garde 8231B7D8";
        if (FlagA(ctx, base, sctx, 41) || FlagA(ctx, base, sctx, 12)) return "flag A41/A12";

        const uint32_t qte = Call(ctx, base, sub_82317698, sctx);
        if (!qte) return "qte=0";
        if (PPC_LOAD_U8(qte + 101)) return "qte deja actif";

        const uint32_t tf = PPC_LOAD_U32(sctx + 1432);
        if (!tf) return "transform=0";
        const uint32_t P = CallVirt12(ctx, base, tf);
        if (!P) return "pos=0";
        const float px = RdF(base, P + 48), py = RdF(base, P + 52), pz = RdF(base, P + 56);
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return "pos nan";

    LOGFN("[AP QTE TRANSFORM] X=({},{},{}) Z=({},{},{})",
        RdF(base, P + 0), RdF(base, P + 4), RdF(base, P + 8),
        RdF(base, P + 32), RdF(base, P + 36), RdF(base, P + 40));

        // Cap de Sonic : ligne 2 de la matrice issue du quaternion (memes maths que sub_8231A730).
        float fx = 0.f, fz = 1.f;
        if (!FlagA(ctx, base, sctx, 1))
        {
            const uint32_t Q = S + kQuat, Mx = S + kMat;
            for (uint32_t i = 0; i < 4; ++i)
                PPC_STORE_U32(Q + 4 * i, PPC_LOAD_U32(sctx + 1088 + 4 * i));
            Call(ctx, base, sub_82E84BE0, Mx, Q);
            fx = RdF(base, P + 32);
        fz = RdF(base, P + 40);
        }
        const float hl = std::sqrt(fx * fx + fz * fz);
        if (!(hl > 1e-3f)) return "cap invalide";
        fx /= hl; fz /= hl;
        LOGFN("[AP QTE DIRECTION] Flag1={} fx={} fz={}",
            FlagA(ctx, base, sctx, 1) ? 1 : 0, fx, fz);
        if (!FlagA(ctx, base, sctx, 1))
        {
            const uint32_t Mx = S + kMat;
            LOGFN("[AP QTE MATRIX] X=({},{},{}) Y=({},{},{}) Z=({},{},{})",
                RdF(base, Mx + 0), RdF(base, Mx + 4), RdF(base, Mx + 8),
                RdF(base, Mx + 16), RdF(base, Mx + 20), RdF(base, Mx + 24),
                RdF(base, Mx + 32), RdF(base, Mx + 36), RdF(base, Mx + 40));
        }

        // Geometrie capturee sur un vrai tremplin (Mykonos Act 2), reutilisee en bloc rigide.
        const float b48[3]  = { BitsToFloat(0xC402CE56), BitsToFloat(0xC1D00000), BitsToFloat(0xC5608CCD) };
        const float b64[3]  = { BitsToFloat(0xB6CB3BC4), BitsToFloat(0x41A00000), BitsToFloat(0xC20A9066) };
        const float b96[3]  = { BitsToFloat(0xC402CE56), BitsToFloat(0xC1AA4924), BitsToFloat(0xC561C985) };
        const float b112[3] = { BitsToFloat(0xB700C84E), BitsToFloat(0x41CE0EC9), BitsToFloat(0xC22F9B6A) };

        const float d = std::atan2(fx, fz) - std::atan2(b64[0], b64[2]);
        const float c = std::cos(d), s = std::sin(d);
        auto rot = [&](const float v[3], float o[3]) { o[0] = v[0] * c + v[2] * s; o[1] = v[1]; o[2] = v[2] * c - v[0] * s; };

        float n64[3], n112[3], rel[3];
        const float dv[3] = { b96[0] - b48[0], b96[1] - b48[1], b96[2] - b48[2] };
        rot(b64, n64); rot(b112, n112); rot(dv, rel);
        // AP QTE V3 TRAJECTOIRE REDUITE : essai de saut court vers le cap de Sonic.
        // Conserve des vecteurs non nuls pour eviter de casser la logique native.
        // AP QTE V6 ELAN AVANT : horizontal 5, hauteur 10, arrivee 2.
        n64[0] = fx * 36.f; n64[1] = 25.f; n64[2] = fz * 36.f;
        n112[0] = fx * 36.f; n112[1] = 25.f; n112[2] = fz * 36.f;
        rel[0] = fx * 12.0f; rel[1] = 2.0f; rel[2] = fz * 12.0f;

        // --- Construction du message : plus AUCUN appel natif entre ici et le handler ---
        const uint32_t M0 = S + kMsg, ENT = S + kEnt, BTN = S + kBtn;
        for (uint32_t o = 0; o < kZeroEnd; o += 4) PPC_STORE_U32(S + o, 0);

        static std::mt19937 rng{ std::random_device{}() };
        const uint32_t nBtn = 3;                         // seule longueur observee
        for (uint32_t i = 0; i < nBtn; ++i) PPC_STORE_U32(BTN + 4 * i, rng() % 6u);

        WrF(base, ENT + 0, 5.0f);                        // limite de temps
        WrF(base, ENT + 4, 2.5f);                        // seuil rang 3
        WrF(base, ENT + 8, 3.75f);                       // seuil rang 2
        PPC_STORE_U32(ENT + 16, BTN);                    // vecteur de boutons : begin
        PPC_STORE_U32(ENT + 20, BTN + 4 * nBtn);         //                      end
        PPC_STORE_U32(ENT + 24, BTN + 4 * nBtn);         //                      cap

        PPC_STORE_U32(M0 + 0, 0x820C5DD4);               // vtable MsgHitTrickJump
        PPC_STORE_U32(M0 + 28, ENT);                     // entrees : begin
        PPC_STORE_U32(M0 + 32, ENT + 28);                //           end
        PPC_STORE_U32(M0 + 36, ENT + 28);                //           cap
        auto put4 = [&](uint32_t off, float x, float y, float z, float w)
        { WrF(base, M0 + off, x); WrF(base, M0 + off + 4, y); WrF(base, M0 + off + 8, z); WrF(base, M0 + off + 12, w); };
        put4(48,  px, py, pz, 1.0f);                                                // ancre = Sonic : aucun deplacement force
        put4(64,  n64[0], n64[1], n64[2], 1.0f);                                    // direction de saut
        WrF(base, M0 + 80, 1.5f);
        put4(96,  px + rel[0], py + rel[1], pz + rel[2], BitsToFloat(0x3EDB6DB6));  // point d'arrivee
        put4(112, n112[0], n112[1], n112[2], 1.0f);                                 // direction de sortie
        WrF(base, M0 + 128, 1.0f);
        PPC_STORE_U32(M0 + 132, 0x00000BB8);             // valeur capturee (3000), role exact non confirme
        WrF(base, M0 + 136, 1.0f);

        ctx.r3.u64 = sonic;                              // CSonic*, comme le dispatcher natif
        ctx.r4.u64 = M0;
        __imp__sub_82314FE0(ctx, base);                  // copie tout par valeur, ne garde aucun pointeur
        return nullptr;
    }
}

void AP_QteOfDoomQueue() { ApQteDoom::g_pending.store(true); }

void AP_QteOfDoomOnSonicUpdate(PPCContext& ctx, uint8_t* base)
{
    using Clock = std::chrono::steady_clock;

    static int s_tick = 0;
    static int s_cooldown = 0;
    static const char* s_lastWhy = nullptr;

    static bool s_apReserved = false;
    static bool s_apConfirmationFailed = false;
    static bool s_apWaiting = false;
    static std::string s_apStage;

    // Identifier les changements de partie Archipelago.
    static std::uint64_t s_apGeneration = 0;
    static bool s_apGenerationInitialized = false;
    static Clock::time_point s_apReadySince{};
    static Clock::time_point s_lastSonicUpdate{};

    const auto now = Clock::now();

    // Les QTE Archipelago sont autorises uniquement en niveau.
    const auto* apQteDoc = SWA::CGameDocument::GetInstance();
    const std::string apQteStage =
        (apQteDoc && apQteDoc->m_pMember)
        ? apQteDoc->m_pMember->m_StageName.c_str()
        : "";

    const bool apQteStageAllowed =
        AP_IsReverseControlsStageAllowed() &&
        !apReverseRestartBlocked;

    if (!apQteStageAllowed || apQteStage != s_apStage)
    {
        s_apWaiting = false;
        s_apStage = apQteStage;
    }

    // Une nouvelle generation invalide les reservations locales.
    const std::uint64_t generation =
        APClient::GetQteOfDoomGeneration();

    if (!s_apGenerationInitialized || generation != s_apGeneration)
    {
        s_apGeneration = generation;
        s_apGenerationInitialized = true;

        s_apReserved = false;
        s_apConfirmationFailed = false;
        s_apWaiting = false;
        s_lastWhy = nullptr;

        ApQteDoom::Log("AP QTE DOOM: generation synchronisee");
    }

    // Si Sonic n'a pas ete mis a jour depuis 250 ms,
    // sa disponibilite n'etait pas continue.
    if (s_lastSonicUpdate != Clock::time_point{} &&
        now - s_lastSonicUpdate > std::chrono::milliseconds(250))
    {
        s_apWaiting = false;
    }

    s_lastSonicUpdate = now;

    // Ne jamais declencher sur un autre personnage.
    if (!ctx.r3.u32 ||
        PPC_LOAD_U32(ctx.r3.u32) != ApQteDoom::kCSonicVtable)
    {
        s_apWaiting = false;
        return;
    }

    // Conserver le declencheur manuel pour les tests.
    if ((++s_tick % 120) == 0)
    {
        if (FILE* f = std::fopen("ap_qte_doom.trigger", "rb"))
        {
            std::fclose(f);
            std::remove("ap_qte_doom.trigger");
            ApQteDoom::g_pending.store(true);
        }
    }

    if (s_cooldown > 0)
    {
        --s_cooldown;
        s_apWaiting = false;
        return;
    }

    // Priorite au test manuel, independant d'Archipelago.
    if (ApQteDoom::g_pending.load())
    {
        const char* why = ApQteDoom::TryFire(ctx, base);

        if (!why)
        {
            ApQteDoom::g_pending.store(false);
            s_cooldown = 600;
            s_apWaiting = false;
            s_lastWhy = nullptr;
            ApQteDoom::Log("QTE of Doom: declenche (manuel)");
        }
        else if (why != s_lastWhy)
        {
            s_lastWhy = why;
            ApQteDoom::Log(why);
        }

        return;
    }

    // Ne pas demarrer le QTE Archipelago hors d'un niveau valide.
    if (!apQteStageAllowed)
        return;

    // Ne pas rejouer un QTE deja execute si sa sauvegarde a echoue.
    if (s_apConfirmationFailed) return;

    // Reserver un seul QTE Archipelago a la fois.
    if (!s_apReserved)
    {
        if (APClient::GetPendingQteOfDoom() == 0)
        {
            s_apWaiting = false;
            return;
        }

        s_apReserved = true;
        s_apWaiting = false;
        ApQteDoom::Log("AP QTE DOOM: reserve");
    }

    // Attendre 3 secondes de Sonic Day disponible.
    if (!s_apWaiting)
    {
        s_apReadySince = now;
        s_apWaiting = true;
        return;
    }

    if (now - s_apReadySince < std::chrono::seconds(3))
        return;

    // TryFire conserve toutes ses verifications natives.
    const char* why = ApQteDoom::TryFire(ctx, base);

    if (!why)
    {
        const bool saved = APClient::ConfirmQteOfDoomApplied();

        // Le QTE a deja ete declenche : ne jamais le rejouer.
        s_apReserved = !saved;
        s_apConfirmationFailed = !saved;
        s_apWaiting = false;
        s_cooldown = 600;
        s_lastWhy = nullptr;

        ApQteDoom::Log("AP QTE DOOM: declenche et confirmation demandee");
    }
    else
    {
        // Ne pas consommer le QTE si TryFire refuse.
        s_apWaiting = false;

        if (why != s_lastWhy)
        {
            s_lastWhy = why;
            ApQteDoom::Log(why);
        }
    }
}
// END AP_QTE_OF_DOOM v2
// BEGIN AP_MUSIC_LOG v1
#include <cstdio>
#include <cstdint>

PPC_FUNC_IMPL(__imp__sub_8250F2B8);

namespace ApMusicLog
{
    static void Str(uint8_t* base, uint32_t obj, char* out, size_t n)
    {
        out[0] = 0;

        if (obj < 0x10000u)
        {
            std::snprintf(out, n, "<null>");
            return;
        }

        const uint32_t ptr = PPC_LOAD_U32(obj);

        if (ptr < 0x10000u)
        {
            std::snprintf(out, n, "<empty>");
            return;
        }

        size_t i = 0;

        for (; i + 1 < n && i < 120; ++i)
        {
            const char ch = static_cast<char>(
                PPC_LOAD_U8(ptr + static_cast<uint32_t>(i))
            );

            if (!ch)
                break;

            out[i] = (ch >= 32 && ch < 127) ? ch : '?';
        }

        out[i] = 0;
    }
}

// BEGIN AP_ACT1_DEFERRED flag
#include <atomic>
static std::atomic<bool> g_apMusicAct1Pending{false};
// END AP_ACT1_DEFERRED flag
// BEGIN AP_NIGHT_STATE_HOOK_V1
static int g_apNightRegion = -1;

static void AP_NightStartNormal()
{
    if (g_apNightRegion < 0)
        return;

    auto region = static_cast<CustomMusic::DayRegion>(g_apNightRegion);
    const char* path = CustomMusic::GetNightMusicPath(region);

    if (CustomMusic::IsEnabled() && path && *path)
    {
        CustomMusic::RequestNightMusic(region);
        std::printf("[AP NIGHT STATE] Normal: region=%d\n", g_apNightRegion);
    }
}

PPC_FUNC_IMPL(__imp__sub_82B45DF0);
PPC_FUNC_IMPL(__imp__sub_82B45EC8);
PPC_FUNC_IMPL(__imp__sub_82B45F80);

PPC_FUNC(sub_82B45DF0)
{
    std::printf("[AP NIGHT STATE] StartJingle\n");
    CustomMusic::RequestStop();
    __imp__sub_82B45DF0(ctx, base);
}

PPC_FUNC(sub_82B45EC8)
{
    std::printf("[AP NIGHT STATE] Restart\n");
    CustomMusic::RequestStop();
    __imp__sub_82B45EC8(ctx, base);
}

PPC_FUNC(sub_82B45F80)
{
    std::printf("[AP NIGHT STATE] Normal Enter\n");
    AP_NightStartNormal();
    __imp__sub_82B45F80(ctx, base);
}
// END AP_NIGHT_STATE_HOOK_V1
PPC_FUNC(sub_8250F2B8)
{
    char bank[128];
    char cue[128];

    ApMusicLog::Str(base, ctx.r4.u32, bank, sizeof(bank));
    ApMusicLog::Str(base, ctx.r5.u32, cue, sizeof(cue));
    // BEGIN AP_NIGHT_REGION_TRACK_V1
    if (std::strcmp(cue, "evil_jingle") == 0)
    {
        struct NightBank
        {
            const char* bank;
            CustomMusic::DayRegion region;
        };

        static const NightBank nightBanks[] = {
            { "bgm_stg_e_myk", CustomMusic::DayRegion::Apotos },
            { "bgm_stg_e_euc", CustomMusic::DayRegion::Spagonia },
            { "bgm_stg_e_afr", CustomMusic::DayRegion::Mazuri },
            { "bgm_stg_e_snw", CustomMusic::DayRegion::Holoska },
            { "bgm_stg_e_chn", CustomMusic::DayRegion::ChunNan },
            { "bgm_stg_e_sea", CustomMusic::DayRegion::Adabat },
            { "bgm_stg_e_ptr", CustomMusic::DayRegion::Shamar },
            { "bgm_stg_e_nyc", CustomMusic::DayRegion::EmpireCity },
            { "bgm_stg_e_egb", CustomMusic::DayRegion::Eggmanland }
        };

        for (const NightBank& entry : nightBanks)
        {
            if (std::strcmp(bank, entry.bank) == 0)
            {
                g_apNightRegion = static_cast<int>(entry.region);
                std::printf("[AP NIGHT STATE] Region tracked: %s\n", bank);
                break;
            }
        }
    }
    else if (std::strncmp(bank, "bgm_stg_", 8) == 0 ||
             std::strncmp(bank, "bgm_twn_", 8) == 0 ||
             std::strcmp(bank, "bgm_sys_title") == 0)
    {
        g_apNightRegion = -1;
    }
    // END AP_NIGHT_REGION_TRACK_V1
    // BEGIN AP_ACT1_DEFERRED cancel-stage
    // Changement de stage / ville / menu principal : tout BGM bgm_stg_*, bgm_twn_* (Hub compris) ou bgm_sys_title annule l'attente.
    // Si c'est Act 1, le bloc AP_CUSTOM_MUSIC_HOOK ci-dessous la reactive.
    if (std::strncmp(bank, "bgm_stg_", 8) == 0 || std::strncmp(bank, "bgm_twn_", 8) == 0 ||
        std::strcmp(bank, "bgm_sys_title") == 0)
        g_apMusicAct1Pending.store(false, std::memory_order_relaxed);
    // END AP_ACT1_DEFERRED cancel-stage
    // BEGIN AP_CUSTOM_MUSIC_HOOK
    if (std::strcmp(bank, "bgm_stg_myk_act1") == 0 &&
        std::strcmp(cue, "sonic_normal") == 0)
    {
        g_apMusicAct1Pending.store(CustomMusic::IsEnabled(), std::memory_order_relaxed) /*AP_ACT1_DEFERRED set*/;
    }
    else if (std::strcmp(bank, "bgm_stg_myk") == 0 &&
        std::strcmp(cue, "sonic_normal") == 0)
    {
        CustomMusic::RequestApotosDay();
    }
    // AP_UNIVERSAL_DAY_HOOK_V1
    else if (std::strcmp(cue, "sonic_normal") == 0)
    {
        struct DayBank
        {
            const char* bank;
            CustomMusic::DayRegion region;
        };

        static const DayBank dayBanks[] = {
            { "bgm_stg_euc", CustomMusic::DayRegion::Spagonia },
            { "bgm_stg_afr", CustomMusic::DayRegion::Mazuri },
            { "bgm_stg_snw", CustomMusic::DayRegion::Holoska },
            { "bgm_stg_chn", CustomMusic::DayRegion::ChunNan },
            { "bgm_stg_sea", CustomMusic::DayRegion::Adabat },
            { "bgm_stg_ptr", CustomMusic::DayRegion::Shamar },
            { "bgm_stg_nyc", CustomMusic::DayRegion::EmpireCity },
            { "bgm_stg_egb", CustomMusic::DayRegion::Eggmanland }
        };

        for (const DayBank& entry : dayBanks)
        {
            if (std::strcmp(bank, entry.bank) == 0)
            {
                const char* path = CustomMusic::GetDayMusicPath(entry.region);

                if (CustomMusic::IsEnabled() && path && *path)
                    CustomMusic::RequestDayMusic(entry.region);

                break;
            }
        }
    }
    // END AP_UNIVERSAL_DAY_HOOK_V1
    // AP_UNIVERSAL_NIGHT_HOOK_V1
    else if (std::strcmp(cue, "evil_normal") == 0)
    {
        struct NightBank
        {
            const char* bank;
            CustomMusic::DayRegion region;
        };

        static const NightBank nightBanks[] = {
            { "bgm_stg_e_myk", CustomMusic::DayRegion::Apotos },
            { "bgm_stg_e_euc", CustomMusic::DayRegion::Spagonia },
            { "bgm_stg_e_afr", CustomMusic::DayRegion::Mazuri },
            { "bgm_stg_e_snw", CustomMusic::DayRegion::Holoska },
            { "bgm_stg_e_chn", CustomMusic::DayRegion::ChunNan },
            { "bgm_stg_e_sea", CustomMusic::DayRegion::Adabat },
            { "bgm_stg_e_ptr", CustomMusic::DayRegion::Shamar },
            { "bgm_stg_e_nyc", CustomMusic::DayRegion::EmpireCity },
            { "bgm_stg_e_egb", CustomMusic::DayRegion::Eggmanland }
        };

        for (const NightBank& entry : nightBanks)
        {
            if (std::strcmp(bank, entry.bank) == 0)
            {
                const char* path =
                    CustomMusic::GetNightMusicPath(entry.region);

                if (CustomMusic::IsEnabled() && path && *path)
                {
                    CustomMusic::RequestNightMusic(entry.region);

                    std::printf(
                        "[AP MUSIC] Night BGM requested: %s\n",
                        bank
                    );
                }

                break;
            }
        }
    }
    // END AP_UNIVERSAL_NIGHT_HOOK_V1

// END AP_CUSTOM_MUSIC_HOOK

    if (FILE* f = std::fopen("ap_music.log", "a"))
    {
        std::fprintf(
            f,
            "BGM bank='%s' cue='%s' flag=%u\n",
            bank,
            cue,
            static_cast<unsigned>(ctx.r6.u32 & 0xFFu)
        );

        std::fclose(f);
    }

    // AP_TOWN_MUSIC_V1 - leave the game's BGM untouched if no WAV is configured.
    if (CustomMusic::IsEnabled())
    {
        struct TownCue { const char* bank; const char* cue; CustomMusic::TownTrack track; };
        static const TownCue towns[] = {
            { "bgm_twn_myk_day", "twn_day", static_cast<CustomMusic::TownTrack>(0) },
            { "bgm_twn_myk_night", "twn_night", static_cast<CustomMusic::TownTrack>(1) },
            { "bgm_twn_euc_day", "twn_day", static_cast<CustomMusic::TownTrack>(2) },
            { "bgm_twn_euc_night", "twn_night", static_cast<CustomMusic::TownTrack>(3) },
            { "bgm_twn_afr_day", "twn_day", static_cast<CustomMusic::TownTrack>(4) },
            { "bgm_twn_afr_night", "twn_night", static_cast<CustomMusic::TownTrack>(5) },
            { "bgm_twn_chn_day", "twn_day", static_cast<CustomMusic::TownTrack>(6) },
            { "bgm_twn_chn_night", "twn_night", static_cast<CustomMusic::TownTrack>(7) },
            { "bgm_twn_snw_day", "twn_day", static_cast<CustomMusic::TownTrack>(8) },
            { "bgm_twn_snw_night", "twn_night", static_cast<CustomMusic::TownTrack>(9) },
            { "bgm_twn_sea_day", "twn_day", static_cast<CustomMusic::TownTrack>(10) },
            { "bgm_twn_sea_night", "twn_night", static_cast<CustomMusic::TownTrack>(11) },
            { "bgm_twn_ptr_day", "twn_day", static_cast<CustomMusic::TownTrack>(12) },
            { "bgm_twn_ptr_night", "twn_night", static_cast<CustomMusic::TownTrack>(13) },
            { "bgm_twn_nyc_day", "twn_day", static_cast<CustomMusic::TownTrack>(14) },
            { "bgm_twn_nyc_night", "twn_night", static_cast<CustomMusic::TownTrack>(15) },
            { "bgm_twn_egb", "twn_day", static_cast<CustomMusic::TownTrack>(16) },
        };
        for (const TownCue& entry : towns)
        {
            if (std::strcmp(bank, entry.bank) == 0 && std::strcmp(cue, entry.cue) == 0)
            {
                const char* path = CustomMusic::GetTownMusicPath(entry.track);
                if (path && *path)
                {
                    CustomMusic::RequestTownMusic(entry.track);
                    std::printf("[AP MUSIC] Town requested: %s\n", bank);
                    return;
                }
                break;
            }
        }
    }
    // END AP_TOWN_MUSIC_V1

    // AP_BOSS_MUSIC_V1
    if (CustomMusic::IsEnabled())
    {
        struct BossCue { const char* bank; const char* cue; CustomMusic::BossTrack track; };
        static const BossCue bosses[] = {
            { "bgm_boss_day", "boss_day", CustomMusic::BossTrack::Day },
            { "bgm_boss_night", "boss_night", CustomMusic::BossTrack::Night },
            { "bgm_boss_night_egg", "boss_night_egg", CustomMusic::BossTrack::NightEgg },
            { "bgm_boss_last1_air", "boss_last1_air", CustomMusic::BossTrack::LastAir }
        };
        for (const BossCue& entry : bosses)
        {
            if (std::strcmp(bank, entry.bank) == 0 && std::strcmp(cue, entry.cue) == 0)
            {
                const char* path = CustomMusic::GetBossMusicPath(entry.track);
                if (path && *path)
                {
                    CustomMusic::RequestBossMusic(entry.track);
                    std::printf("[AP MUSIC] Boss requested: %s\n", bank);
                    return; // Prevent original BGM only when a custom WAV is configured.
                }
                break;
            }
        }
    }
    // END AP_BOSS_MUSIC_V1
    // AP_SUPPRESS_UNIVERSAL_NIGHT_BGM_V1
    if (CustomMusic::IsEnabled() &&
        std::strcmp(cue, "evil_normal") == 0)
    {
        struct NightBank
        {
            const char* bank;
            CustomMusic::DayRegion region;
        };

        static const NightBank nightBanks[] = {
            { "bgm_stg_e_myk", CustomMusic::DayRegion::Apotos },
            { "bgm_stg_e_euc", CustomMusic::DayRegion::Spagonia },
            { "bgm_stg_e_afr", CustomMusic::DayRegion::Mazuri },
            { "bgm_stg_e_snw", CustomMusic::DayRegion::Holoska },
            { "bgm_stg_e_chn", CustomMusic::DayRegion::ChunNan },
            { "bgm_stg_e_sea", CustomMusic::DayRegion::Adabat },
            { "bgm_stg_e_ptr", CustomMusic::DayRegion::Shamar },
            { "bgm_stg_e_nyc", CustomMusic::DayRegion::EmpireCity },
            { "bgm_stg_e_egb", CustomMusic::DayRegion::Eggmanland }
        };

        for (const NightBank& entry : nightBanks)
        {
            if (std::strcmp(bank, entry.bank) == 0)
            {
                const char* path =
                    CustomMusic::GetNightMusicPath(entry.region);

                if (path && *path)
                {
                    std::printf(
                        "[AP MUSIC] Suppressed original Night BGM: %s\n",
                        bank
                    );

                    return;
                }

                break;
            }
        }
    }
    // END AP_SUPPRESS_UNIVERSAL_NIGHT_BGM_V1
    // AP_SUPPRESS_UNIVERSAL_DAY_BGM_V1
    if (CustomMusic::IsEnabled() &&
        std::strcmp(cue, "sonic_normal") == 0)
    {
        struct DayBank
        {
            const char* bank;
            CustomMusic::DayRegion region;
        };

        static const DayBank dayBanks[] = {
            { "bgm_stg_myk", CustomMusic::DayRegion::Apotos },
            { "bgm_stg_euc", CustomMusic::DayRegion::Spagonia },
            { "bgm_stg_afr", CustomMusic::DayRegion::Mazuri },
            { "bgm_stg_snw", CustomMusic::DayRegion::Holoska },
            { "bgm_stg_chn", CustomMusic::DayRegion::ChunNan },
            { "bgm_stg_sea", CustomMusic::DayRegion::Adabat },
            { "bgm_stg_ptr", CustomMusic::DayRegion::Shamar },
            { "bgm_stg_nyc", CustomMusic::DayRegion::EmpireCity },
            { "bgm_stg_egb", CustomMusic::DayRegion::Eggmanland }
        };

        for (const DayBank& entry : dayBanks)
        {
            if (std::strcmp(bank, entry.bank) == 0)
            {
                const char* path =
                    CustomMusic::GetDayMusicPath(entry.region);

                if (path && *path)
                {
                    std::printf(
                        "[AP MUSIC] Suppressed original Day BGM: %s\n",
                        bank
                    );
                    return;
                }

                break;
            }
        }
    }
    // END AP_SUPPRESS_UNIVERSAL_DAY_BGM_V1
    __imp__sub_8250F2B8(ctx, base);
}
// END AP_MUSIC_LOG v1

// BEGIN AP_MUSIC_RESULT_HOOK v1
PPC_FUNC_IMPL(__imp__sub_82B421A0);
PPC_FUNC_IMPL(__imp__sub_82B42140);

static void ApMusicResultLog(const char* event)
{
    if (FILE* f = std::fopen("ap_music.log", "a"))
    {
        std::fprintf(f, "BGM EVENT %s\n", event);
        std::fclose(f);
    }
}

PPC_FUNC(sub_82B421A0)
{
    // BEGIN AP_ACT1_DEFERRED cancel-clear
    g_apMusicAct1Pending.store(false, std::memory_order_relaxed);
    // END AP_ACT1_DEFERRED cancel-clear
    ApMusicResultLog("CLEAR");
    CustomMusic::RequestStop();
    __imp__sub_82B421A0(ctx, base);
}

PPC_FUNC(sub_82B42140)
{
    // BEGIN AP_ACT1_DEFERRED cancel-result
    g_apMusicAct1Pending.store(false, std::memory_order_relaxed);
    // END AP_ACT1_DEFERRED cancel-result
    ApMusicResultLog("RESULT");
    CustomMusic::RequestStop();
    __imp__sub_82B42140(ctx, base);
}
// END AP_MUSIC_RESULT_HOOK v1

// BEGIN AP_MUSIC_D48_DIAG v1
// Diagnostic pur : journalise chaque appel a sub_82B42D48 (administrateur son, message MsgFinishMovie -> demarrage BGM)
// puis appelle l'original sans rien modifier. Aucune interaction avec CustomMusic.
#include <cstdio>
#include <chrono>

PPC_FUNC_IMPL(__imp__sub_82B42D48);

PPC_FUNC(sub_82B42D48)
{
    static const auto s_t0 = std::chrono::steady_clock::now();
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - s_t0).count();

    if (FILE* f = std::fopen("ap_music.log", "a"))
    {
        std::fprintf(f, "BGM D48 t=%lldms r3=0x%08X r4=0x%08X\n", ms, ctx.r3.u32, ctx.r4.u32);
        std::fclose(f);
    }

    // BEGIN AP_ACT1_DEFERRED trigger
    // Fin de cinematique (MsgFinishMovie -> sub_82B42D48) : demarre la musique Act 1 si elle est en attente
    // ET si le Stage ID courant est bien ActD_MykonosAct1. Sinon le drapeau est simplement annule.
    if (g_apMusicAct1Pending.exchange(false, std::memory_order_relaxed))
    {
        const auto* apAct1Doc = SWA::CGameDocument::GetInstance();
        const char* apAct1Stage =
            (apAct1Doc && apAct1Doc->m_pMember) ? apAct1Doc->m_pMember->m_StageName.c_str() : "";
        if (CustomMusic::IsEnabled() && apAct1Stage && std::strcmp(apAct1Stage, "ActD_MykonosAct1") == 0)
            CustomMusic::RequestApotosDayAct1();
    }
    // END AP_ACT1_DEFERRED trigger
    __imp__sub_82B42D48(ctx, base);
}
// END AP_MUSIC_D48_DIAG v1

// BEGIN AP_FREEZE_INJECT early
// Etat minimal partage avec les segments AP_FREEZE_TRACE (doit etre declare avant eux). Aucun effet de jeu.
#include <atomic>
#include <cstdint>
namespace ApFreezeInject
{
    static thread_local int t_injecting     = 0;   // > 0 : dans un appel natif provoque par l'injection
    static thread_local int t_stateDepth    = 0;   // > 0 : dans sub_823047A8 (changement d'etat en cours)
    static thread_local int t_dispatchDepth = 0;   // > 0 : dans sub_8236DFF0 (dispatcher des messages de CSonic)
    static std::atomic<unsigned> g_stateChanges{0}; // changements d'etat vus pendant l'appel injecte

    static inline void StateBegin()
    {
        ++t_stateDepth;
        if (t_injecting > 0) g_stateChanges.fetch_add(1, std::memory_order_relaxed);
    }
    static inline void StateEnd()
    {
        if (t_stateDepth > 0) --t_stateDepth;
    }
}

// END AP_FREEZE_INJECT early
// BEGIN AP_FREEZE_TRACE helpers
// Diagnostic pur : aucun effet de jeu. Ne modifie ni registre, ni message, ni pile PPC.
// Journal : ap_freeze_trace.log (dossier de travail du jeu).
// But : savoir si le gel naturel de Sonic Day survient DANS ou HORS de CSonic::Update (sub_82310F20),
// et dans quel ordre s'enchainent MsgDamage -> sub_823535E8 -> sub_8237A160 -> sub_823047A8.
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#endif

PPC_FUNC_IMPL(__imp__sub_823535E8);
PPC_FUNC_IMPL(__imp__sub_823047A8);

namespace ApFzTrace
{
    static constexpr unsigned kMaxLines = 400;   // au plus 400 lignes / session
    static constexpr unsigned kCrumbs   = 64;    // puissance de 2

    struct Crumb { long long ms; const char* tag; uint32_t a, b, c, tid; };

    static std::atomic<unsigned>           g_lines{0};
    static std::atomic<uint8_t*>           g_base{nullptr};
    static std::atomic<uint32_t>           g_updR1{0};
    static std::atomic<uint32_t>           g_updTid{0};
    static std::atomic<long long>          g_updMs{-1};
    static std::atomic<unsigned long long> g_updCount{0};
    static std::atomic<unsigned>           g_otherDamage{0};
    static std::atomic<uint32_t>           g_crumbIdx{0};
    static Crumb                           g_crumbs[kCrumbs];
    static thread_local int                t_inUpdate = 0;   // > 0 : dans l'original de CSonic::Update
    static thread_local int                t_freeze   = 0;   // > 0 : dans un traitement de gel observe

    static long long Ms()
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
    }

    static uint32_t Tid()
    {
#if defined(_WIN32)
        return static_cast<uint32_t>(GetCurrentThreadId());
#else
        return 0u;
#endif
    }

    static void AddCrumb(const char* tag, uint32_t a, uint32_t b = 0, uint32_t c = 0)
    {
        Crumb& e = g_crumbs[g_crumbIdx.fetch_add(1, std::memory_order_relaxed) & (kCrumbs - 1)];
        e.ms = Ms(); e.tag = tag; e.a = a; e.b = b; e.c = c; e.tid = Tid();
    }

    static void Line(const char* fmt, ...)
    {
        const unsigned n = g_lines.fetch_add(1, std::memory_order_relaxed);
        if (n > kMaxLines) return;
        FILE* f = std::fopen("ap_freeze_trace.log", "a");
        if (!f) return;
        std::fprintf(f, "[t=%lldms tid=%u] ", Ms(), Tid());
        // BEGIN AP_FREEZE_INJECT trace-tag
        if (ApFreezeInject::t_injecting > 0) std::fputs("[INJECTE] ", f);
        // END AP_FREEZE_INJECT trace-tag
        if (n == kMaxLines)
            std::fputs("[LIMITE DE LIGNES ATTEINTE]", f);
        else
        {
            va_list ap;
            va_start(ap, fmt);
            std::vfprintf(f, fmt, ap);
            va_end(ap);
        }
        std::fputc('\n', f);
        std::fclose(f);
    }

    // true si [a, a+len) est dans une page hote commitee et lisible (sans exception).
    static bool Readable(uint8_t* base, uint32_t a, uint32_t len)
    {
        if (!base || a < 0x10000u || len == 0u || a > 0xFFFFFFFFu - len)
            return false;
#if defined(_WIN32)
        const uintptr_t start = reinterpret_cast<uintptr_t>(base) + static_cast<uintptr_t>(a);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(start), &mbi, sizeof(mbi)))
            return false;
        const DWORD p = mbi.Protect;
        if (mbi.State != MEM_COMMIT || (p & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const DWORD r = p & 0xFFu;
        if (r != PAGE_READONLY && r != PAGE_READWRITE && r != PAGE_WRITECOPY &&
            r != PAGE_EXECUTE_READ && r != PAGE_EXECUTE_READWRITE && r != PAGE_EXECUTE_WRITECOPY)
            return false;
        const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        return start >= region && (start - region) <= mbi.RegionSize &&
               (mbi.RegionSize - (start - region)) >= len;
#else
        return false;
#endif
    }

    static bool Ld(uint8_t* base, uint32_t a, uint32_t& out)
    {
        if ((a & 3u) != 0u || !Readable(base, a, 4u))
            return false;
        out = PPC_LOAD_U32(a);
        return true;
    }

    // Chaine ASCII imprimable (3 a 38 car.) a l'adresse guest a ; false sinon.
    static bool Str(uint8_t* base, uint32_t a, char (&out)[40])
    {
        out[0] = 0;
        if (!Readable(base, a, 40u))
            return false;
        unsigned n = 0;
        for (; n < 39u; ++n)
        {
            const uint8_t c = PPC_LOAD_U8(a + n);
            if (c == 0) break;
            if (c < 0x20 || c > 0x7E) return false;
            out[n] = static_cast<char>(c);
        }
        out[n] = 0;
        return n >= 3u && n < 39u;
    }

    struct Where { const char* verdict; int inUpd; uint32_t updTid; long long dR1; long long age; };

    static Where Locate(uint32_t r1)
    {
        Where w{};
        w.inUpd  = t_inUpdate;
        w.updTid = g_updTid.load(std::memory_order_relaxed);
        w.dR1    = static_cast<long long>(g_updR1.load(std::memory_order_relaxed)) - static_cast<long long>(r1);
        const long long um = g_updMs.load(std::memory_order_relaxed);
        w.age    = um < 0 ? -1 : Ms() - um;
        if (um < 0)               w.verdict = "UPDATE_JAMAIS_VU";
        else if (t_inUpdate > 0)  w.verdict = "DANS_UPDATE";
        else if (w.updTid == Tid()) w.verdict = "HORS_UPDATE_MEME_THREAD";
        else                      w.verdict = "AUTRE_THREAD";
        return w;
    }

#if defined(_WIN32)
    static void Emit(HANDLE h, const char* s, int n)
    {
        DWORD w = 0;
        if (n > 0) WriteFile(h, s, static_cast<DWORD>(n), &w, nullptr);
    }

    // Journalise l'exception puis laisse le jeu la traiter normalement (EXCEPTION_CONTINUE_SEARCH).
    static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* e)
    {
        const DWORD code = e->ExceptionRecord->ExceptionCode;
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
            code != EXCEPTION_IN_PAGE_ERROR && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
            code != EXCEPTION_PRIV_INSTRUCTION)
            return EXCEPTION_CONTINUE_SEARCH;
        static std::atomic<unsigned> n{0};
        if (n.fetch_add(1) >= 16u)
            return EXCEPTION_CONTINUE_SEARCH;

        const uintptr_t rip = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
        HMODULE mod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(rip), &mod);
        char modName[MAX_PATH]; modName[0] = 0;
        if (mod) GetModuleFileNameA(mod, modName, MAX_PATH);
        const char* shortName = modName;
        for (const char* p = modName; *p; ++p) if (*p == '\\' || *p == '/') shortName = p + 1;

        const unsigned np = e->ExceptionRecord->NumberParameters;
        const int rw = np >= 1 ? static_cast<int>(e->ExceptionRecord->ExceptionInformation[0]) : -1;
        const uintptr_t fa = np >= 2 ? static_cast<uintptr_t>(e->ExceptionRecord->ExceptionInformation[1]) : 0;
        const uint8_t* gb = g_base.load(std::memory_order_relaxed);
        char guest[24]; std::snprintf(guest, sizeof guest, "hors-guest");
        if (gb && fa >= reinterpret_cast<uintptr_t>(gb) && fa - reinterpret_cast<uintptr_t>(gb) <= 0xFFFFFFFFull)
            std::snprintf(guest, sizeof guest, "%08llX",
                          static_cast<unsigned long long>(fa - reinterpret_cast<uintptr_t>(gb)));

        char buf[1024];
        int len = std::snprintf(buf, sizeof buf,
            "[t=%lldms tid=%u] CRASH-PREMIERE-CHANCE code=%08lX rip=%p module=%s rva=%llX faultaddr=%p rw=%d(0=lecture 1=ecriture 8=DEP) guest=%s "
            "inUpdate=%d freezeScope=%d updCount=%llu updAgeMs=%lld updTid=%u\r\n",
            Ms(), Tid(), static_cast<unsigned long>(code), reinterpret_cast<void*>(rip), shortName,
            static_cast<unsigned long long>(mod ? rip - reinterpret_cast<uintptr_t>(mod) : 0),
            reinterpret_cast<void*>(fa), rw, guest, t_inUpdate, t_freeze,
            g_updCount.load(std::memory_order_relaxed),
            g_updMs.load(std::memory_order_relaxed) < 0 ? -1LL : Ms() - g_updMs.load(std::memory_order_relaxed),
            g_updTid.load(std::memory_order_relaxed));
        HANDLE h = CreateFileA("ap_freeze_trace.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
        Emit(h, buf, len < 0 ? 0 : (len < static_cast<int>(sizeof buf) ? len : static_cast<int>(sizeof buf) - 1));
        const uint32_t end = g_crumbIdx.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < kCrumbs; ++i)
        {
            const Crumb& c = g_crumbs[(end + i) & (kCrumbs - 1)];
            if (!c.tag) continue;
            len = std::snprintf(buf, sizeof buf, "    miette t=%lldms tid=%u %s a=%08X b=%08X c=%08X\r\n",
                                c.ms, c.tid, c.tag, c.a, c.b, c.c);
            Emit(h, buf, len < 0 ? 0 : (len < static_cast<int>(sizeof buf) ? len : static_cast<int>(sizeof buf) - 1));
        }
        CloseHandle(h);
        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

    static void InstallCrashLogger()
    {
#if defined(_WIN32)
        static std::atomic<bool> done{false};
        if (done.exchange(true)) return;
        (void)Ms();
        AddVectoredExceptionHandler(1, &CrashHandler);
        Line("crash logger installe (journalise seulement ; l'exception reste traitee par le jeu)");
#endif
    }

    // ---- CSonic::Update (sub_82310F20) : appelees autour de l'original ----
    static void UpdateEnter(PPCContext& ctx, uint8_t* base)
    {
        g_base.store(base, std::memory_order_relaxed);
        InstallCrashLogger();
        ++t_inUpdate;
        g_updR1.store(ctx.r1.u32, std::memory_order_relaxed);
        g_updTid.store(Tid(), std::memory_order_relaxed);
        g_updMs.store(Ms(), std::memory_order_relaxed);
        g_updCount.fetch_add(1, std::memory_order_relaxed);
    }

    static void UpdateLeave()
    {
        if (t_inUpdate > 0) --t_inUpdate;
    }

    // ---- MsgDamage de Sonic Day : adaptateur sub_823535E8 (r3 = CSonic, r4 = message) ----
    static bool MsgEnter(PPCContext& ctx, uint8_t* base)
    {
        g_base.store(base, std::memory_order_relaxed);
        const uint32_t csonic = ctx.r3.u32, msg = ctx.r4.u32;
        uint32_t type = 0, val = 0, flags = 0;
        if (!(Ld(base, msg + 24u, type) && Ld(base, msg + 28u, val) && Ld(base, msg + 96u, flags)))
        {
            Line("MsgDamage sub_823535E8 enter csonic=%08X msg=%08X <message illisible>", csonic, msg);
            AddCrumb("msg-illisible", csonic, msg);
            return false;
        }
        if ((flags & 2u) == 0u)
        {
            g_otherDamage.fetch_add(1, std::memory_order_relaxed);   // feu / choc / autres : compte seulement
            return false;
        }
        const Where w = Locate(ctx.r1.u32);
        uint32_t c = 0, sm = 0, fl = 0, st = 0;
        if (Ld(base, csonic + 196u, c) && c)
        {
            Ld(base, c + 256u, sm);
            Ld(base, c + 508u, fl);
            Ld(base, c + 1196u, st);
        }
        Line("MsgDamage GEL enter csonic=%08X ctx=%08X msg=%08X type=%08X val=%08X flags=%08X | ctx+256=%08X ctx+508=%08X ctx+1196=%08X | r1=%08X %s inUpdate=%d updTid=%u dR1=%lld updAgeMs=%lld autresDegatsAvant=%u",
             csonic, c, msg, type, val, flags, sm, fl, st, ctx.r1.u32, w.verdict, w.inUpd, w.updTid, w.dR1, w.age,
             g_otherDamage.load(std::memory_order_relaxed));
        AddCrumb("msg-enter", csonic, msg, flags);
        ++t_freeze;
        return true;
    }

    static void MsgLeave(bool scope, PPCContext& ctx)
    {
        if (!scope) return;
        if (t_freeze > 0) --t_freeze;
        Line("MsgDamage GEL leave r3=%08X (retour normal)", ctx.r3.u32);
        AddCrumb("msg-leave", ctx.r3.u32);
    }

    // ---- sub_8237A160 (appele depuis le hook AP_DAMAGE_DIAG existant) ----
    static bool A160Enter(PPCContext& ctx, uint8_t* base)
    {
        g_base.store(base, std::memory_order_relaxed);
        if ((ctx.r7.u32 & 2u) == 0u) return false;               // gel uniquement
        const Where w = Locate(ctx.r1.u32);
        uint32_t sm = 0, fl = 0, st = 0;
        if (ctx.r3.u32 >= 0x10000u)
        {
            Ld(base, ctx.r3.u32 + 256u, sm);
            Ld(base, ctx.r3.u32 + 508u, fl);
            Ld(base, ctx.r3.u32 + 1196u, st);
        }
        Line("sub_8237A160 GEL enter ctx=%08X r4=%08X r5=%08X r6=%08X r7=%08X | ctx+256=%08X ctx+508=%08X ctx+1196=%08X | r1=%08X %s inUpdate=%d updTid=%u dR1=%lld updAgeMs=%lld viaMsgDamage=%d",
             ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, sm, fl, st, ctx.r1.u32,
             w.verdict, w.inUpd, w.updTid, w.dR1, w.age, t_freeze > 0 ? 1 : 0);
        AddCrumb("a160-enter", ctx.r3.u32, ctx.r4.u32, ctx.r7.u32);
        ++t_freeze;
        return true;
    }

    static void A160Leave(bool scope, PPCContext& ctx)
    {
        if (!scope) return;
        if (t_freeze > 0) --t_freeze;
        Line("sub_8237A160 GEL leave r3=%08X (retour normal)", ctx.r3.u32);
        AddCrumb("a160-leave", ctx.r3.u32);
    }

    // ---- sub_823047A8 (changement d'etat par nom) : journalise seulement pendant un gel observe ----
    static bool StateEnter(PPCContext& ctx, uint8_t* base)
    {
        if (t_freeze <= 0) return false;
        uint32_t w0 = 0, w1 = 0;
        const bool ok = Ld(base, ctx.r4.u32, w0) && Ld(base, ctx.r4.u32 + 4u, w1);
        char s0[40], s1[40];
        s0[0] = 0; s1[0] = 0;
        const bool h0 = ok && Str(base, w0, s0);
        const bool h1 = ok && Str(base, w1, s1);
        Line("sub_823047A8 changement d'etat enter sm=%08X r4=%08X w0=%08X w1=%08X chaine(w0)=%s chaine(w1)=%s (la structure de la chaine est une hypothese)",
             ctx.r3.u32, ctx.r4.u32, w0, w1, h0 ? s0 : "<?>", h1 ? s1 : "<?>");
        AddCrumb("state-enter", ctx.r3.u32, ctx.r4.u32, w0);
        return true;
    }

    static void StateLeave(bool scope, PPCContext& ctx)
    {
        if (!scope) return;
        Line("sub_823047A8 changement d'etat leave r3=%08X (retour normal)", ctx.r3.u32);
        AddCrumb("state-leave", ctx.r3.u32);
    }
}

void AP_FreezeTraceUpdateEnter(PPCContext& ctx, uint8_t* base) { ApFzTrace::UpdateEnter(ctx, base); }
void AP_FreezeTraceUpdateLeave(PPCContext& ctx, uint8_t* base) { (void)ctx; (void)base; ApFzTrace::UpdateLeave(); }

PPC_FUNC(sub_823535E8)
{
    const bool apFzScope = ApFzTrace::MsgEnter(ctx, base);
    __imp__sub_823535E8(ctx, base);
    ApFzTrace::MsgLeave(apFzScope, ctx);
}

PPC_FUNC(sub_823047A8)
{
    const bool apFzScope = ApFzTrace::StateEnter(ctx, base);
    // BEGIN AP_FREEZE_INJECT state-begin
    ApFreezeInject::StateBegin();
    // END AP_FREEZE_INJECT state-begin
    __imp__sub_823047A8(ctx, base);
    // BEGIN AP_FREEZE_INJECT state-end
    ApFreezeInject::StateEnd();
    // END AP_FREEZE_INJECT state-end
    ApFzTrace::StateLeave(apFzScope, ctx);
}

// END AP_FREEZE_TRACE helpers
// BEGIN AP_FREEZE_INJECT core
// Declenchement du gel de Sonic Day a une frontiere de traitement des messages.
// Principe : apres le retour de l'original de sub_8236DFF0 (dispatcher des messages de CSonic), hors
// CSonic::Update, hors transition d'etat, hors dispatch imbrique, on rejoue le chemin natif d'un gel :
//   sub_823535E8(CSonic, faux MsgDamage) -> sub_8237A160 (flags = 2) -> changement d'etat "DamageFreeze".
// Securites : verrou ap_freeze_inject.lock (desactivation automatique apres un crash), 10 tentatives
// maximum sur 5 s, trap conserve tant que le jeu refuse (aucun changement d'etat observe).
PPC_FUNC_IMPL(__imp__sub_8236DFF0);

namespace ApFreezeInject
{
    static constexpr long long kWindowMs    = 5000;  // fenetre totale apres l'armement
    static constexpr long long kGapMs       = 450;   // ecart minimal entre deux tentatives
    static constexpr unsigned  kMaxAttempts = 10;    // tentatives maximum (= appels natifs maximum)
    // kFakeType SUPPRIME : le type du message (r4) est l'INDEX d'un masque de 128 bits de "types d'attaque", numerote par
    // sub_827BF798 dans l'ordre d'enregistrement (0x23, 0x26, 0x29... observes selon les sessions) : il n'a pas de valeur fixe.
    static constexpr uint32_t  kMaxScan     = 256u;  // indices testes au maximum pour trouver un type accepte par la garde du jeu
    static constexpr uint32_t  kVecGlobal   = 0x83368E7Cu; // pointeur du vecteur de masques (lu par sub_827BF360 / sub_827BEFE0)
    static constexpr uint32_t  kFakeValue   = 1u;    // r5 observe
    static constexpr uint32_t  kFakeFlags   = 2u;    // r7 observe (bit 2 = gel)
    static constexpr uint32_t  kMsgSize     = 0x80u; // sub_823535E8 / sub_8237A160 lisent jusqu'a +100

    static std::atomic<bool>      g_pending{false};
    static std::atomic<bool>      g_apOwned{false};
    static std::atomic<bool>      g_burnMode{false};
    // BEGIN AP_PARALYZED_MODE_V1
    static std::atomic<bool>      g_paralyzedMode{false};
    // END AP_PARALYZED_MODE_V1
    // BEGIN AP_BURN_TYPE_CACHE_V1
    static std::atomic<uint32_t> g_burnCachedType{0xFFFFFFFFu};
    // END AP_BURN_TYPE_CACHE_V1
    static std::atomic<long long> g_lastApSuccessMs{-1000000};
    static constexpr long long kApCooldownMs = 5000;
    static std::atomic<uint32_t>  g_sonic{0};
    static std::atomic<long long> g_armMs{0};
    static std::atomic<long long> g_lastMs{-1000000};
    static std::atomic<unsigned>  g_attempts{0};
    static std::atomic<unsigned>  g_refused{0};
    static std::atomic<unsigned>  g_skipUpdate{0};
    static std::atomic<unsigned>  g_skipNested{0};
    static std::atomic<unsigned>  g_skipState{0};
    static std::atomic<unsigned>  g_skipIdent{0};
    static std::atomic<unsigned>  g_skipOther{0};
    static std::atomic<bool>      g_identLogged{false};
    static std::atomic<int>       g_disabled{-1};    // -1 inconnu, 0 actif, 1 desactive

    static bool FileExists(const char* p)
    {
        FILE* f = std::fopen(p, "rb");
        if (!f) return false;
        std::fclose(f);
        return true;
    }

    // Desactivation persistante : fichier ap_freeze_inject.disabled, cree automatiquement si le verrou
    // d'un essai precedent est encore la (le jeu n'a pas rendu la main = crash probable).
    static bool Disabled()
    {
        const int d = g_disabled.load(std::memory_order_relaxed);
        if (d >= 0) return d == 1;
        bool dis = false;
        if (FileExists("ap_freeze_inject.disabled"))
        {
            ApFzTrace::Line("injection DESACTIVEE : ap_freeze_inject.disabled present (supprimez-le pour reactiver)");
            dis = true;
        }
        else if (FileExists("ap_freeze_inject.lock"))
        {
            if (FILE* f = std::fopen("ap_freeze_inject.disabled", "wb"))
            {
                std::fputs("Desactive automatiquement : le verrou ap_freeze_inject.lock d'un essai precedent n'a pas ete retire\n"
                           "(le jeu n'a pas rendu la main pendant l'injection = crash probable). Supprimez ce fichier pour reactiver.\n", f);
                std::fclose(f);
            }
            std::remove("ap_freeze_inject.lock");
            ApFzTrace::Line("injection DESACTIVEE AUTOMATIQUEMENT : verrou d'un essai precedent retrouve (crash probable). ap_freeze_inject.disabled cree.");
            dis = true;
        }
        g_disabled.store(dis ? 1 : 0, std::memory_order_relaxed);
        return dis;
    }

    // Appelee depuis AP_SonicDayFreezeManualOnUpdate apres consommation de ap_freeze.trigger.
    // N'effectue aucun appel natif : arme seulement.
    static bool Arm(uint32_t sonic, bool apOwned = false, bool burnMode = false, bool paralyzedMode = false)
    {
        if (Disabled())
        {
            ApFzTrace::Line("trap gel : injection desactivee, rien n'est arme");
            return false;
        }
        if (g_pending.load(std::memory_order_relaxed))
        {
            ApFzTrace::Line("trap gel : deja arme, nouvelle demande ignoree");
            return false;
        }
        g_sonic.store(sonic, std::memory_order_relaxed);
        g_armMs.store(ApFzTrace::Ms(), std::memory_order_relaxed);
        g_lastMs.store(-1000000, std::memory_order_relaxed);
        g_attempts.store(0, std::memory_order_relaxed);
        g_refused.store(0, std::memory_order_relaxed);
        g_skipUpdate.store(0, std::memory_order_relaxed);
        g_skipNested.store(0, std::memory_order_relaxed);
        g_skipState.store(0, std::memory_order_relaxed);
        g_skipIdent.store(0, std::memory_order_relaxed);
        g_skipOther.store(0, std::memory_order_relaxed);
        g_identLogged.store(false, std::memory_order_relaxed);
        g_burnMode.store(burnMode, std::memory_order_release);
        g_paralyzedMode.store(paralyzedMode, std::memory_order_release);
        g_apOwned.store(apOwned, std::memory_order_release);
        g_pending.store(true, std::memory_order_release);
        ApFzTrace::Line("trap gel ARME : csonic=%08X fenetre=%lldms tentatives<=%u ecart>=%lldms",
                        sonic, kWindowMs, kMaxAttempts, kGapMs);
        ApFzTrace::AddCrumb("inject-arm", sonic);
        return true;
    }

    static void Expire(const char* why)
    {
        if (!g_pending.exchange(false, std::memory_order_acq_rel)) return;
        ApFzTrace::Line("trap gel ABANDONNE (%s) : tentatives=%u refus-du-jeu=%u | ignores : dans-Update=%u imbrique=%u transition=%u identite=%u autre=%u",
                        why, g_attempts.load(), g_refused.load(), g_skipUpdate.load(), g_skipNested.load(),
                        g_skipState.load(), g_skipIdent.load(), g_skipOther.load());
        ApFzTrace::AddCrumb("inject-expire", g_attempts.load());
        if (g_apOwned.exchange(false, std::memory_order_acq_rel))
        {
            if (g_burnMode.load(std::memory_order_acquire))
                APClient::CancelBurn();
            else if (g_paralyzedMode.load(std::memory_order_acquire))
                APClient::CancelParalyzed();
            else
                APClient::CancelFreeze();
        }
        g_burnMode.store(false, std::memory_order_release);
            g_paralyzedMode.store(false, std::memory_order_release);
    }

    // Verrou : cree avant l'appel natif, retire des que l'appel est revenu (y compris sortie anticipee).
    struct LockScope
    {
        bool ok = false;
        LockScope(unsigned attempt, uint32_t csonic)
        {
            if (FILE* f = std::fopen("ap_freeze_inject.lock", "wb"))
            {
                std::fprintf(f, "attempt=%u csonic=%08X t=%lldms\n", attempt, csonic, ApFzTrace::Ms());
                std::fflush(f);
                std::fclose(f);
                ok = true;
            }
        }
        ~LockScope() { if (ok) std::remove("ap_freeze_inject.lock"); }
        LockScope(const LockScope&) = delete;
        LockScope& operator=(const LockScope&) = delete;
    };

    // Sauvegarde / restauration de r1 et r3 (valeur de retour du dispatcher) sur TOUTE sortie, + drapeau d'injection.
    struct CtxScope
    {
        PPCContext& c;
        uint64_t r1, r3;
        explicit CtxScope(PPCContext& ctx) : c(ctx), r1(ctx.r1.u64), r3(ctx.r3.u64) { ++t_injecting; }
        ~CtxScope()
        {
            c.r1.u64 = r1;
            c.r3.u64 = r3;
            if (t_injecting > 0) --t_injecting;
        }
        CtxScope(const CtxScope&) = delete;
        CtxScope& operator=(const CtxScope&) = delete;
    };

    struct DispatchScope
    {
        DispatchScope() { ++t_dispatchDepth; }
        ~DispatchScope() { if (t_dispatchDepth > 0) --t_dispatchDepth; }
    };

    // Cherche un type de message que la garde du jeu accepte pour CE Sonic. Predicat utilise : sub_8237A098(ctx, type), identique a
    // la garde de sub_8237A160 (sub_82318650 puis sub_827BF360(type, cle)). Appels sur une COPIE du contexte, avec une pile libre,
    // avant toute ecriture dans ctx ou dans le faux message. Aucun indice n'est passe sans verification (vecteur lu, cle et type < taille).
    // Retour : 0 = trouve ; 1 = vecteur illisible ; 2 = cle ctx+1216 hors vecteur (la garde leverait une exception std) ; 3 = aucun type accepte.
    static int PickType(PPCContext& ctx, uint8_t* base, uint32_t c, uint32_t sp, uint32_t& typeOut, uint32_t& scanned, uint32_t& vsize, uint32_t& key)
    {
        typeOut = 0; scanned = 0; vsize = 0; key = 0;
        uint32_t vptr = 0, b = 0, e = 0;
        if (!ApFzTrace::Ld(base, kVecGlobal, vptr) || !ApFzTrace::Ld(base, vptr + 4u, b) || !ApFzTrace::Ld(base, vptr + 8u, e) ||
            b == 0u || e < b || ((e - b) & 15u) != 0u)
            return 1;
        vsize = (e - b) >> 4;
        if (vsize == 0u) return 1;
        if (vsize > kMaxScan) vsize = kMaxScan;
        if (!ApFzTrace::Ld(base, c + 1216u, key) || key >= vsize) return 2;   // cle lue par sub_82316320 : hors vecteur = exception std
        PPCContext q = ctx;                                                    // copie : le contexte du dispatcher n'est jamais modifie
        // Burn : verifier le type memorise avant la recherche complete.
        if (g_burnMode.load(std::memory_order_acquire))
        {
            const uint32_t cached = g_burnCachedType.load(std::memory_order_acquire);

            if (cached < vsize)
            {
                q.r1.u64 = sp;
                q.r3.u64 = c;
                q.r4.u64 = cached;
                sub_8237A098(q, base);
                ++scanned;

                if ((q.r3.u32 & 0xFFu) != 0u)
                {
                    typeOut = cached;
                    return 0;
                }
            }
        }
        for (uint32_t a = 0; a < vsize; ++a)
        {
            q.r1.u64 = sp;
            q.r3.u64 = c;
            q.r4.u64 = a;
            sub_8237A098(q, base);
            ++scanned;
            if ((q.r3.u32 & 0xFFu) != 0u)
            {
                typeOut = a;

                if (g_burnMode.load(std::memory_order_acquire))
                    g_burnCachedType.store(a, std::memory_order_release);

                return 0;
            }
        }
        return 3;
    }

    // Appelee APRES le retour de l'original du dispatcher. Toutes les verifications precedent la
    // moindre ecriture dans ctx : un retour anticipe ne modifie jamais les registres.
    static void TryInject(PPCContext& ctx, uint8_t* base, uint32_t listener)
    {
        const long long now = ApFzTrace::Ms();
        if (now - g_armMs.load(std::memory_order_relaxed) > kWindowMs) { Expire("fenetre de 5 s ecoulee"); return; }
        if (t_injecting > 0) return;                                          // rappel provoque par l'injection elle-meme
        if (t_dispatchDepth != 0) { g_skipNested.fetch_add(1, std::memory_order_relaxed); return; }
        if (ApFzTrace::g_updMs.load(std::memory_order_relaxed) < 0 ||
            ApFzTrace::g_updTid.load(std::memory_order_relaxed) != ApFzTrace::Tid())
        { g_skipOther.fetch_add(1, std::memory_order_relaxed); return; }      // Update jamais vu ou autre thread
        if (ApFzTrace::t_inUpdate != 0) { g_skipUpdate.fetch_add(1, std::memory_order_relaxed); return; }
        if (t_stateDepth != 0 || ApFzTrace::t_freeze != 0) { g_skipState.fetch_add(1, std::memory_order_relaxed); return; }

        const uint32_t csonic = listener - 40u;                              // meme calcul que le dispatcher natif
        uint32_t vt = 0, c = 0;
        if (csonic != g_sonic.load(std::memory_order_relaxed) ||
            !ApFzTrace::Ld(base, csonic, vt) || vt != ApQteDoom::kCSonicVtable ||
            !ApFzTrace::Ld(base, csonic + 196u, c) || c < 0x10000u)
        {
            g_skipIdent.fetch_add(1, std::memory_order_relaxed);
            if (!g_identLogged.exchange(true))
                ApFzTrace::Line("identite : csonic=%08X (listener-40) attendu=%08X vtable=%08X contexte=%08X -> pas d'injection sur cet objet",
                                csonic, g_sonic.load(), vt, c);
            return;
        }
        if (now - g_lastMs.load(std::memory_order_relaxed) < kGapMs) return;  // trop tot : tentative non comptee
        if (g_attempts.load(std::memory_order_relaxed) >=
    ((g_burnMode.load(std::memory_order_acquire) || g_paralyzedMode.load(std::memory_order_acquire)) ? 1u : kMaxAttempts))
{
    Expire("nombre maximal de tentatives atteint");
    return;
}

        const uint32_t newSp = (ctx.r1.u32 - 0x300u) & ~15u;                 // sous le cadre libere du dispatcher
        const uint32_t msg   = newSp + 0x40u;                                // > newSp+0x1C : sub_8237A160 ecrit a r1+28
        if (!ApFzTrace::Readable(base, newSp, 0x100u)) { g_skipOther.fetch_add(1, std::memory_order_relaxed); return; }

        uint32_t fakeType = 0, scanned = 0, vsize = 0, key = 0;
        const int pick = PickType(ctx, base, c, newSp, fakeType, scanned, vsize, key);
        if (pick != 0)
        {
            // Aucun appel natif d'injection : la tentative est comptee (10 maximum) mais le trap reste arme.
            g_lastMs.store(now, std::memory_order_relaxed);
            const unsigned att = g_attempts.fetch_add(1, std::memory_order_relaxed) + 1u;
            g_refused.fetch_add(1, std::memory_order_relaxed);
            static const char* const why[] = { "", "vecteur de masques illisible", "cle ctx+1216 hors du vecteur (la garde leverait une exception std)", "aucun type accepte par la garde du jeu (Sonic dans un etat protege ?)" };
            ApFzTrace::Line("INJECT tentative=%u/%u SANS appel natif : %s | essais=%u taille_vecteur=%u cle=%08X -> trap conserve", att, kMaxAttempts, why[pick], scanned, vsize, key);
            ApFzTrace::AddCrumb("inject-no-type", csonic, static_cast<uint32_t>(pick), att);
            if (att >= kMaxAttempts) Expire("10 tentatives sans changement d'etat");
            return;
        }

        LockScope lock(g_attempts.load() + 1u, csonic);
        if (!lock.ok) { Expire("impossible d'ecrire ap_freeze_inject.lock : pas d'injection sans verrou"); return; }
        const unsigned attempt = g_attempts.fetch_add(1, std::memory_order_relaxed) + 1u;
        g_lastMs.store(now, std::memory_order_relaxed);

        CtxScope scope(ctx);                                                  // restaure r1 / r3 a la sortie, quoi qu'il arrive
        for (uint32_t o = 0; o < kMsgSize; o += 4u) PPC_STORE_U32(msg + o, 0u);
        PPC_STORE_U32(msg + 24u, fakeType);                                   // -> r4 de sub_8237A160 : type accepte par la garde du jeu
        PPC_STORE_U32(msg + 28u, kFakeValue);                                 // -> r5
        PPC_STORE_U32(msg + 96u, g_paralyzedMode.load(std::memory_order_acquire) ? 4u : (g_burnMode.load(std::memory_order_acquire) ? 1u : kFakeFlags));                                 // -> r7 (bit 2 = gel) ; msg+80 (vecteur) reste a zero
        g_stateChanges.store(0, std::memory_order_relaxed);

        const ApFzTrace::Where w = ApFzTrace::Locate(ctx.r1.u32);
        ApFzTrace::Line("INJECT begin tentative=%u/%u csonic=%08X ctx=%08X listener=%08X r1=%08X newSp=%08X msg=%08X type=%08X(garde du jeu : %u essai(s), taille_vecteur=%u, cle=%08X) %s dR1=%lld",
                        attempt, kMaxAttempts, csonic, c, listener, ctx.r1.u32, newSp, msg, fakeType, scanned, vsize, key, w.verdict, w.dR1);
        ApFzTrace::AddCrumb("inject-begin", csonic, msg, attempt);

        ctx.r1.u64 = newSp;
        ctx.r3.u64 = csonic;
        ctx.r4.u64 = msg;
        sub_823535E8(ctx, base); // UN SEUL appel natif par tentative

        const unsigned changes = g_stateChanges.load(std::memory_order_relaxed);
        ApFzTrace::AddCrumb("inject-end", csonic, msg, changes);
        if (changes > 0)
        {
            g_pending.store(false, std::memory_order_release);
            if (g_apOwned.exchange(false, std::memory_order_acq_rel))
            {
                g_lastApSuccessMs.store(ApFzTrace::Ms(), std::memory_order_release);
                if (g_burnMode.load(std::memory_order_acquire))
                {
                    APClient::ConfirmBurnApplied();
                }
                else if (g_paralyzedMode.load(std::memory_order_acquire))
                {
                    APClient::ConfirmParalyzedApplied();
                }
                else
                {
                    APClient::ConfirmFreezeApplied();
                }
            }
            ApFzTrace::Line("INJECT end tentative=%u appelsNatifs=1 changementsEtat=%u -> SUCCES, trap consomme", attempt, changes);
            g_burnMode.store(false, std::memory_order_release);
            g_paralyzedMode.store(false, std::memory_order_release);
        }
        else
        {
            g_refused.fetch_add(1, std::memory_order_relaxed);
            ApFzTrace::Line("INJECT end tentative=%u appelsNatifs=1 changementsEtat=0 -> refuse par les gardes du jeu, trap conserve", attempt);
            if (attempt >= kMaxAttempts) Expire("10 tentatives sans changement d'etat");
        }
    }
}

PPC_FUNC(sub_8236DFF0)
{
    const uint32_t listener = ctx.r3.u32;                                     // = CSonic + 40
    {
        ApFreezeInject::DispatchScope apFiScope;
        __imp__sub_8236DFF0(ctx, base);
    }
    if (ApFreezeInject::g_pending.load(std::memory_order_acquire))
        ApFreezeInject::TryInject(ctx, base, listener);
}

// END AP_FREEZE_INJECT core
// BEGIN AP_FREEZE_PROBE probe
// Sonde de diagnostic V7b (journal seul, journalisation reduite). N'altere NI registre, NI memoire invitee, NI flux d'execution :
// chaque hook lit (lectures verifiees), journalise, puis appelle l'original avec le MEME ctx.
// Question posee : quelle exception std est levee pendant le gel injecte, par quelle fonction, avec quelles valeurs,
// et en quoi l'appel injecte differe-t-il d'un appel naturel ? Journal : ap_freeze_trace.log (lignes "PROBE ...").
//
// Faits tires du code recompile (pas d'hypothese) :
//  - sub_822C1CA8 est appelee juste apres la construction d'une exception std (appel de handler global, virtuelle vtbl[2],
//    puis sub_82DF82B0 -> sub_82DF8228 = fflush(NULL) + ecriture de what() + abort). Elle est le point commun de toutes les levees.
//  - sub_827BEFE0 = vector<T>::operator[] verifie (element de 16 octets) ; leve si begin==0 ou (end-begin)>>4 <= indice.
//  - sub_827BF360(A,B) lit vecteur[B] puis (via sub_827BF108) vecteur[A] dans le MEME vecteur global (pointeur a 0x83368E7C).
//    Dans sub_8237A160 : A = r4 d'entree (type du message), B = cle (ctx+1216 ou valeur statique en cache 0x833647AC).
//  - sub_82E60C58 = test de capacite d'une list (0x0CCCCCCC = 0xFFFFFFFF/20) ; avec compte = 1, ne leve que si taille == 0x0CCCCCCC.
// Hypotheses (signalees "HYP") : mise en page de l'exception (chaine en exc+8, tampon en exc+12, taille +28, capacite +32).
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdint>

PPC_FUNC_IMPL(__imp__sub_827BF360);
PPC_FUNC_IMPL(__imp__sub_827BEFE0);
PPC_FUNC_IMPL(__imp__sub_82316318);
PPC_FUNC_IMPL(__imp__sub_82316320);
PPC_FUNC_IMPL(__imp__sub_82E61A78);
PPC_FUNC_IMPL(__imp__sub_82E60C58);
PPC_FUNC_IMPL(__imp__sub_822C1CA8);
PPC_FUNC_IMPL(__imp__sub_82DF82B0);
PPC_FUNC_IMPL(__imp__sub_82DF8228);

namespace ApFzProbe
{
    static constexpr uint32_t kVecGlobal = 0x83368E7Cu;   // lis -31945 ; lwz -29060 (sub_827BF360 / sub_827BF108)
    static constexpr uint32_t kCacheKey  = 0x833647ACu;   // lis -31946 ; 18348 (cache statique de la cle dans sub_8237A160)
    static constexpr uint32_t kCacheFlag = 0x833647B0u;   // lis -31946 ; 18352 (bit 0 = cache initialise)
    static constexpr uint32_t kRaiseHook = 0x83395EB8u;   // lis -31943 ; 24248 (handler global lu par sub_822C1CA8)
    static constexpr uint32_t kListMax   = 0x0CCCCCCCu;   // constante de sub_82E60C58
    static constexpr unsigned kNormalMax = 40;            // lignes "normales" maximum (V7b : reduit)
    static constexpr unsigned kPerKind   = 3;             // lignes detaillees par categorie ET par origine (naturel / injecte)
    static constexpr unsigned kCritMax   = 60;            // lignes critiques (levee d'exception, indices invalides) maximum

    struct Last { bool v = false; uint32_t a = 0, b = 0, c = 0, d = 0; };
    static std::atomic<unsigned> g_normal{0};
    static std::atomic<unsigned> g_crit{0};
    static std::atomic<unsigned> g_kind[7][2];            // 0:827BF360 enter 1:827BF360 leave 2:82E61A78 enter 3:82E61A78 leave 4:82E60C58 5:82316318 6:82316320
    static thread_local Last t_l360;   // dernier sub_827BF360 : A, B, taille du vecteur, prediction
    static thread_local Last t_l60;    // dernier sub_82E60C58 : liste, taille, compte, prediction
    static thread_local Last t_l61;    // dernier sub_82E61A78 : liste, taille, noeuds parcourus
    static thread_local Last t_key;    // dernier sub_82316320 : ctx, cle

    static inline bool Scope() { return ApFzTrace::t_freeze > 0 || ApFreezeInject::t_injecting > 0; }
    static inline const char* Origin() { return ApFreezeInject::t_injecting > 0 ? "INJECTE" : "NATUREL"; }
    // true tant que la categorie n'a pas atteint kPerKind lignes pour cette origine : evite tout travail (lectures, formatage, fichier) ensuite.
    static inline bool Allow(unsigned kind) { return g_kind[kind][ApFreezeInject::t_injecting > 0 ? 1 : 0].fetch_add(1, std::memory_order_relaxed) < kPerKind; }

    static void P(bool critical, const char* fmt, ...)
    {
        if (critical) { if (g_crit.fetch_add(1, std::memory_order_relaxed) >= kCritMax) return; }
        else          { if (g_normal.fetch_add(1, std::memory_order_relaxed) >= kNormalMax) return; }
        char buf[1100];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        ApFzTrace::Line("PROBE %s", buf);
    }

    // Mot invite a l'adresse addr, ecrit "%08X" ou "????????" (illisible, non aligne ou hors page commitee).
    static const char* Hx(uint8_t* base, uint32_t addr, char (&b)[12])
    {
        uint32_t v = 0;
        if (ApFzTrace::Ld(base, addr, v)) std::snprintf(b, sizeof b, "%08X", static_cast<unsigned>(v));
        else                              std::snprintf(b, sizeof b, "????????");
        return b;
    }

    static bool Rd(uint8_t* base, uint32_t addr, uint32_t& v) { return ApFzTrace::Ld(base, addr, v); }

    // Chaine ASCII lue octet par octet avec verification de page ; non imprimable -> '?'. false si rien de lisible.
    static bool SafeStr(uint8_t* base, uint32_t a, char* out, unsigned cap)
    {
        if (cap < 2u) return false;
        out[0] = 0;
        if (a < 0x10000u || a > 0xFFFFFF00u) return false;
        unsigned n = 0;
        for (; n + 1u < cap; ++n)
        {
            if (!ApFzTrace::Readable(base, a + n, 1u)) break;
            const uint8_t c = PPC_LOAD_U8(a + n);
            if (c == 0) { out[n] = 0; return n > 0u; }
            out[n] = (c < 0x20 || c > 0x7E) ? '?' : static_cast<char>(c);
        }
        out[n] = 0;
        return n > 0u;
    }

    struct Vec { bool ok = false; uint32_t obj = 0, begin = 0, end = 0, size = 0; };

    // vector de la forme : +4 = begin, +8 = end (tel que lu par sub_827BEFE0). size = (end-begin) >> 4 (srawi, arithmetique).
    static Vec ReadVec(uint8_t* base, uint32_t obj)
    {
        Vec v;
        v.obj = obj;
        if (Rd(base, obj + 4u, v.begin) && Rd(base, obj + 8u, v.end))
        {
            v.ok = true;
            v.size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 4);
        }
        return v;
    }

    // Reproduit EXACTEMENT le test de sub_827BEFE0 : begin != 0 et (end-begin)>>4 > indice (comparaison non signee).
    static bool IdxOk(const Vec& v, uint32_t idx) { return v.ok && v.begin != 0u && v.size > idx; }

    static void CtxText(const PPCContext& ctx, char (&o)[300])
    {
        uint64_t f1 = 0;
        std::memcpy(&f1, &ctx.f1.f64, sizeof f1);
        std::snprintf(o, sizeof o,
            "r1=%08X(mod16=%u) r13=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X f1=%016llX",
            ctx.r1.u32, static_cast<unsigned>(ctx.r1.u32 & 15u), ctx.r13.u32, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
            ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, static_cast<unsigned long long>(f1));
    }

    // ---------------- sub_827BF360(A = r3, B = r4) : test de compatibilite vecteur[B] / vecteur[A] ----------------
    static bool G360Enter(PPCContext& ctx, uint8_t* base)
    {
        if (!Scope()) return false;
        const uint32_t A = ctx.r3.u32, B = ctx.r4.u32;
        uint32_t vptr = 0;
        const bool gv = Rd(base, kVecGlobal, vptr);
        Vec v;
        if (gv) v = ReadVec(base, vptr);
        const bool okA = IdxOk(v, A), okB = IdxOk(v, B);
        const bool raise = !(okA && okB);
        if (!raise && !Allow(0))                                   // gel deja decrit pour cette origine : rien d'autre a lire ni a ecrire
        {
            t_l360 = Last{true, A, B, v.size, 0u};
            ApFzTrace::AddCrumb("p360-enter", A, B, v.size);
            return true;
        }
        uint32_t ea[4] = {0, 0, 0, 0}, eb[4] = {0, 0, 0, 0};
        bool ra = false, rb = false;
        if (okA) { ra = Rd(base, v.begin + A * 16u, ea[0]) && Rd(base, v.begin + A * 16u + 4u, ea[1]) && Rd(base, v.begin + A * 16u + 8u, ea[2]) && Rd(base, v.begin + A * 16u + 12u, ea[3]); }
        if (okB) { rb = Rd(base, v.begin + B * 16u, eb[0]) && Rd(base, v.begin + B * 16u + 4u, eb[1]) && Rd(base, v.begin + B * 16u + 8u, eb[2]) && Rd(base, v.begin + B * 16u + 12u, eb[3]); }
        const bool inter = ra && rb && ((ea[0] & eb[0]) | (ea[1] & eb[1]) | (ea[2] & eb[2]) | (ea[3] & eb[3])) != 0u;
        char ck[12], cf[12], ctxt[300];
        CtxText(ctx, ctxt);
        const ApFzTrace::Where w = ApFzTrace::Locate(ctx.r1.u32);
        P(raise,
          "827BF360 %s A(type)=%08X B(cle)=%08X | vecteur_global=%s vptr=%08X begin=%08X end=%08X taille=%s%u | A<taille=%d B<taille=%d -> %s | "
          "bitsetA=%s%08X.%08X.%08X.%08X bitsetB=%s%08X.%08X.%08X.%08X intersection=%d | cache_cle=%s cache_flag=%s | %s | r1+448(r1 d'entree de sub_8237A160, deduit)=%08X %s dR1=%lld",
          Origin(), A, B, gv ? "lu" : "ILLISIBLE", vptr, v.begin, v.end, v.ok ? "" : "?", v.size, okA ? 1 : 0, okB ? 1 : 0,
          raise ? "LEVEE std ATTENDUE (indice invalide)" : "indices valides",
          ra ? "" : "(n/a)", ea[0], ea[1], ea[2], ea[3], rb ? "" : "(n/a)", eb[0], eb[1], eb[2], eb[3], inter ? 1 : 0,
          Hx(base, kCacheKey, ck), Hx(base, kCacheFlag, cf), ctxt, ctx.r1.u32 + 448u, w.verdict, w.dR1);
        t_l360 = Last{true, A, B, v.size, raise ? 1u : 0u};
        ApFzTrace::AddCrumb(raise ? "p360-PREDIT-LEVEE" : "p360-enter", A, B, v.size);
        return true;
    }

    static void G360Leave(bool sc, PPCContext& ctx)
    {
        if (!sc) return;
        if (!Allow(1)) { ApFzTrace::AddCrumb("p360-leave", ctx.r3.u32); return; }
        P(false, "827BF360 leave r3=%08X (%s)", ctx.r3.u32, (ctx.r3.u32 & 0xFFu) ? "compatible : le jeu poursuit vers la publication du message" : "incompatible : le jeu abandonne le gel");
        ApFzTrace::AddCrumb("p360-leave", ctx.r3.u32);
    }

    // ---------------- sub_827BEFE0(vecteur, indice) : operator[] verifie ----------------
    static void V0Check(PPCContext& ctx, uint8_t* base)
    {
        if (!Scope()) return;
        const Vec v = ReadVec(base, ctx.r3.u32);
        if (IdxOk(v, ctx.r4.u32)) return;               // cas normal : rien a journaliser
        char ctxt[300];
        CtxText(ctx, ctxt);
        P(true, "827BEFE0 %s vector<T>::operator[] sur le point de LEVER : vecteur=%08X lisible=%d begin=%08X end=%08X taille=%u indice=%08X | %s",
          Origin(), ctx.r3.u32, v.ok ? 1 : 0, v.begin, v.end, v.size, ctx.r4.u32, ctxt);
        ApFzTrace::AddCrumb("p0FE0-PREDIT-LEVEE", ctx.r3.u32, ctx.r4.u32, v.size);
    }

    // ---------------- sub_82316318 / sub_82316320 : source de la cle B ----------------
    static void KeyLog(unsigned kind, const char* name, uint32_t ctxPtr, uint32_t result, uint8_t* base)
    {
        if (!Allow(kind)) { t_key = Last{true, ctxPtr, result, 0, 0}; return; }
        char a[12], b[12], c[12];
        P(false, "%s %s ctx=%08X ctx+1216=%s ctx+508=%s ctx+1196=%s -> r3=%08X", name, Origin(), ctxPtr,
          Hx(base, ctxPtr + 1216u, a), Hx(base, ctxPtr + 508u, b), Hx(base, ctxPtr + 1196u, c), result);
        t_key = Last{true, ctxPtr, result, 0, 0};
        ApFzTrace::AddCrumb("p-cle", ctxPtr, result);
    }

    // ---------------- sub_82E61A78 : publication d'un message dans la liste de CSonic+40 ----------------
    static bool L61Enter(PPCContext& ctx, uint8_t* base)
    {
        if (!Scope()) return false;
        const uint32_t L = ctx.r3.u32;
        if (!Allow(2)) { ApFzTrace::AddCrumb("p61a-enter", L); return true; }
        char w24[12], w28[12], w44[12], w72[12], w76[12], w112[12], ctxt[300], s4[100];
        uint32_t size = 0, head = 0, w57 = 0;
        const bool rs = Rd(base, L + 32u, size);
        const bool rh = Rd(base, L + 28u, head);
        unsigned walked = 0;
        bool complete = false;
        if (rh && head >= 0x10000u)
        {
            uint32_t p = 0;
            if (Rd(base, head, p))
            {
                while (walked < 4096u)
                {
                    if (p == head) { complete = true; break; }
                    uint32_t nx = 0;
                    if (!Rd(base, p, nx)) break;
                    ++walked;
                    p = nx;
                }
            }
        }
        bool b57 = false;
        if (ApFzTrace::Readable(base, L + 57u, 1u)) { w57 = PPC_LOAD_U8(L + 57u); b57 = true; }
        uint32_t r7w0 = 0, r7w1 = 0, r7w2 = 0, r7w3 = 0;
        const bool r7ok = Rd(base, ctx.r7.u32, r7w0) && Rd(base, ctx.r7.u32 + 4u, r7w1) && Rd(base, ctx.r7.u32 + 8u, r7w2) && Rd(base, ctx.r7.u32 + 12u, r7w3);
        const bool s4ok = SafeStr(base, ctx.r4.u32, s4, sizeof s4);
        CtxText(ctx, ctxt);
        const ApFzTrace::Where w = ApFzTrace::Locate(ctx.r1.u32);
        P(false,
          "82E61A78 %s liste=%08X(+24) alloc/proxy=%s tete=%s taille=%s%u noeuds_parcourus=%u(%s) | +44=%s +57=%s%02X +72=%s +76=%s +112=%s | "
          "r4=%08X(%s) r5=%u r6=%08X r7=%08X->[%s%08X %08X %08X %08X] | %s | r1+448=%08X %s dR1=%lld",
          Origin(), L + 24u, Hx(base, L + 24u, w24), Hx(base, L + 28u, w28), rs ? "" : "?", size, walked, complete ? "chaine complete" : "chaine INCOMPLETE",
          Hx(base, L + 44u, w44), b57 ? "" : "?", w57, Hx(base, L + 72u, w72), Hx(base, L + 76u, w76), Hx(base, L + 112u, w112),
          ctx.r4.u32, s4ok ? s4 : "illisible", ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, r7ok ? "" : "?", r7w0, r7w1, r7w2, r7w3, ctxt,
          ctx.r1.u32 + 448u, w.verdict, w.dR1);
        t_l61 = Last{true, L, size, walked, complete ? 1u : 0u};
        ApFzTrace::AddCrumb("p61a-enter", L, size, walked);
        return true;
    }

    static void L61Leave(bool sc, PPCContext& ctx)
    {
        if (!sc) return;
        if (!Allow(3)) { ApFzTrace::AddCrumb("p61a-leave", ctx.r3.u32); return; }
        P(false, "82E61A78 leave (retour normal) r3=%08X", ctx.r3.u32);
        ApFzTrace::AddCrumb("p61a-leave", ctx.r3.u32);
    }

    // ---------------- sub_82E60C58(liste, n) : controle de capacite d'une std::list ----------------
    static bool L60Enter(PPCContext& ctx, uint8_t* base)
    {
        if (!Scope()) return false;
        const uint32_t L = ctx.r3.u32, n = ctx.r4.u32;
        uint32_t size = 0;
        const bool ok = Rd(base, L + 8u, size);
        const uint32_t room = kListMax - size;                     // subf non signe, avec retournement (comme le code recompile)
        const bool raise = ok && (room < n);                       // cmplw room, n ; bge -> pas de levee
        if (raise || Allow(4))
        {
            P(raise, "82E60C58 %s liste=%08X taille=%s%u(0x%08X) compte=%u max=0x%08X marge=0x%08X -> %s",
              Origin(), L, ok ? "" : "?", size, size, n, kListMax, room,
              raise ? "LEVEE 'list<T> too long' ATTENDUE" : "capacite suffisante (aucune levee par ce chemin)");
        }
        t_l60 = Last{true, L, size, n, raise ? 1u : 0u};
        ApFzTrace::AddCrumb(raise ? "p60c-PREDIT-LEVEE" : "p60c-enter", L, size, n);
        return true;
    }

    // ---------------- sub_822C1CA8 : levee d'exception std (jamais de retour normal : fprintf + abort) ----------------
    static void RaiseEnter(PPCContext& ctx, uint8_t* base)
    {
        const uint32_t exc = ctx.r3.u32;
        uint32_t wd[10];
        bool wok[10];
        for (unsigned i = 0; i < 10u; ++i) { wd[i] = 0; wok[i] = Rd(base, exc + i * 4u, wd[i]); }
        uint32_t vt1 = 0, vt2 = 0, hook = 0;
        const bool v1 = wok[0] && Rd(base, wd[0] + 4u, vt1);
        const bool v2 = wok[0] && Rd(base, wd[0] + 8u, vt2);
        const bool hk = Rd(base, kRaiseHook, hook);
        // HYP : chaine std::string en exc+8 : tampon en exc+12 si capacite(exc+32) < 16, sinon pointeur en exc+12 ; taille en exc+28.
        char msg[100];
        msg[0] = 0;
        bool mok = false;
        if (wok[3] && wok[7] && wok[8])
        {
            const uint32_t cap = wd[8];
            const uint32_t ptr = (cap >= 16u) ? wd[3] : exc + 12u;
            mok = SafeStr(base, ptr, msg, sizeof msg);
        }
        char ctxt[300];
        CtxText(ctx, ctxt);
        const ApFzTrace::Where w = ApFzTrace::Locate(ctx.r1.u32);
        P(true,
          "RAISE sub_822C1CA8 (levee d'une exception std : le jeu va ecrire what() puis ABORT) origine=%s scope=%d | exc=%08X mots[+0..+36]=%s%08X %08X %08X %08X %08X %08X %08X %08X %08X %08X "
          "| vtbl=%08X vtbl[1]=%s%08X vtbl[2]=%s%08X handler_global=%s%08X | HYP message='%s' | %s | %s dR1=%lld",
          Origin(), Scope() ? 1 : 0, exc, wok[0] ? "" : "(?)", wd[0], wd[1], wd[2], wd[3], wd[4], wd[5], wd[6], wd[7], wd[8], wd[9],
          wd[0], v1 ? "" : "?", vt1, v2 ? "" : "?", vt2, hk ? "" : "?", hook, mok ? msg : "illisible", ctxt, w.verdict, w.dR1);
        P(true, "RAISE contexte recent (meme thread) : 827BF360=%s(A=%08X B=%08X taille=%u prediction=%u) 82E60C58=%s(liste=%08X taille=%u compte=%u prediction=%u) "
                "82E61A78=%s(liste=%08X taille=%u noeuds=%u complete=%u) cle=%s(ctx=%08X cle=%08X)",
          t_l360.v ? "vu" : "non", t_l360.a, t_l360.b, t_l360.c, t_l360.d, t_l60.v ? "vu" : "non", t_l60.a, t_l60.b, t_l60.c, t_l60.d,
          t_l61.v ? "vu" : "non", t_l61.a, t_l61.b, t_l61.c, t_l61.d, t_key.v ? "vu" : "non", t_key.a, t_key.b);
        ApFzTrace::AddCrumb("RAISE", exc, wd[0], wd[3]);
    }

    static void RaiseStep(const char* name, bool withStrings, PPCContext& ctx, uint8_t* base)
    {
        char a[100], b[100];
        a[0] = 0; b[0] = 0;
        const bool ok3 = withStrings && SafeStr(base, ctx.r3.u32, a, sizeof a);
        const bool ok4 = withStrings && SafeStr(base, ctx.r4.u32, b, sizeof b);
        P(true, "%s (suite de la levee ; comportement inchange, fflush(NULL) conserve) r3=%08X('%s') r4=%08X('%s') r1=%08X",
          name, ctx.r3.u32, ok3 ? a : "-", ctx.r4.u32, ok4 ? b : "-", ctx.r1.u32);
        ApFzTrace::AddCrumb(name[0] ? "RAISE-suite" : "?", ctx.r3.u32, ctx.r4.u32);
    }
}

PPC_FUNC(sub_827BF360)
{
    const bool apPrScope = ApFzProbe::G360Enter(ctx, base);
    __imp__sub_827BF360(ctx, base);
    ApFzProbe::G360Leave(apPrScope, ctx);
}

PPC_FUNC(sub_827BEFE0)
{
    ApFzProbe::V0Check(ctx, base);
    __imp__sub_827BEFE0(ctx, base);
}

PPC_FUNC(sub_82316318)
{
    const uint32_t apPrCtx = ctx.r3.u32;
    __imp__sub_82316318(ctx, base);
    if (ApFzProbe::Scope()) ApFzProbe::KeyLog(5u, "82316318", apPrCtx, ctx.r3.u32, base);
}

PPC_FUNC(sub_82316320)
{
    const uint32_t apPrCtx = ctx.r3.u32;
    __imp__sub_82316320(ctx, base);
    if (ApFzProbe::Scope()) ApFzProbe::KeyLog(6u, "82316320", apPrCtx, ctx.r3.u32, base);
}

PPC_FUNC(sub_82E61A78)
{
    const bool apPrScope = ApFzProbe::L61Enter(ctx, base);
    __imp__sub_82E61A78(ctx, base);
    ApFzProbe::L61Leave(apPrScope, ctx);
}

PPC_FUNC(sub_82E60C58)
{
    const bool apPrScope = ApFzProbe::L60Enter(ctx, base);
    __imp__sub_82E60C58(ctx, base);
    if (apPrScope) ApFzTrace::AddCrumb("p60c-leave", ctx.r3.u32);
}

PPC_FUNC(sub_822C1CA8)
{
    ApFzProbe::RaiseEnter(ctx, base);
    __imp__sub_822C1CA8(ctx, base);
}

PPC_FUNC(sub_82DF82B0)
{
    ApFzProbe::RaiseStep("82DF82B0", false, ctx, base);
    __imp__sub_82DF82B0(ctx, base);
}

PPC_FUNC(sub_82DF8228)
{
    ApFzProbe::RaiseStep("82DF8228", true, ctx, base);
    __imp__sub_82DF8228(ctx, base);
}

// END AP_FREEZE_PROBE probe
// BEGIN AP_DAMAGE_DIAG v3
// Diagnostic pur (aucun effet de jeu) : journalise les arguments reels des degats
// subis par Sonic Day (sub_8237A160) et par le Werehog (sub_823BE770 = MsgDamage,
// sub_823BF4C8 = MsgFreeze) dans ap_damage_diag.log, puis appelle l'original.
// Lectures memoire verifiees avec VirtualQuery sous Windows, plages limitees a ce que les
// gestionnaires natifs lisent eux-memes, et limitation de frequence par hook.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#endif

PPC_FUNC_IMPL(__imp__sub_8237A160);
PPC_FUNC_IMPL(__imp__sub_823BE770);
PPC_FUNC_IMPL(__imp__sub_823BF4C8);

namespace ApDmgDiag
{
    static constexpr long long kMinGapMs = 50;   // au plus 1 ligne / 50 ms / hook
    static constexpr unsigned  kMaxLines = 200;  // au plus 200 lignes / hook / session

    struct Gate { long long last = -100000; unsigned count = 0; unsigned suppressed = 0; };

    static long long Ms()
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
    }

    static float AsFloat(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

    // Verifie que les 4 octets guest sont dans une page hote lisible.
    // VirtualQuery ne garantit pas la validite logique du message, mais
    // evite les lectures sur des pages non commises / protegees.
    static bool SafeLoad(uint8_t* base, uint32_t a, uint32_t& out)
    {
        if (!base || a < 0x10000u || (a & 3u) != 0u || a > 0xFFFFFFFBu)
            return false;
#if defined(_WIN32)
        const uintptr_t origin = reinterpret_cast<uintptr_t>(base);
        if (origin > UINTPTR_MAX - static_cast<uintptr_t>(a) - 4u)
            return false;
        const uintptr_t start = origin + static_cast<uintptr_t>(a);
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void*>(start), &mbi, sizeof(mbi)))
            return false;
        const DWORD p = mbi.Protect;
        if (mbi.State != MEM_COMMIT || (p & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const DWORD readable = p & 0xFFu;
        if (readable != PAGE_READONLY && readable != PAGE_READWRITE &&
            readable != PAGE_WRITECOPY && readable != PAGE_EXECUTE_READ &&
            readable != PAGE_EXECUTE_READWRITE && readable != PAGE_EXECUTE_WRITECOPY)
            return false;
        const uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (start < region || start - region > mbi.RegionSize ||
            mbi.RegionSize - (start - region) < 4u)
            return false;
        out = PPC_LOAD_U32(a);
        return true;
#else
        // Pas de lecture non protegee sur une plateforme non verifiee.
        (void)a; (void)out;
        return false;
#endif
    }

    static void DumpWords(FILE* f, uint8_t* base, uint32_t msg, uint32_t from, uint32_t to)
    {
        for (uint32_t o = from; o < to; o += 4)
        {
            uint32_t u = 0;
            if (msg > UINT32_MAX - o || !SafeLoad(base, msg + o, u))
            {
                std::fprintf(f, " +%u=<illisible>", o);
                return;
            }
            std::fprintf(f, " +%u=%08X(%g)", o, u, AsFloat(u));
        }
    }

    // Retourne true si une ligne peut etre ecrite ; suppressed = lignes ignorees depuis la derniere.
    static bool Allow(Gate& g, unsigned& suppressed)
    {
        const long long now = Ms();
        if (g.count >= kMaxLines || now - g.last < kMinGapMs)
        {
            ++g.suppressed;
            return false;
        }
        g.last = now;
        ++g.count;
        suppressed = g.suppressed;
        g.suppressed = 0;
        return true;
    }

    static void Tail(FILE* f, const Gate& g, unsigned suppressed)
    {
        if (suppressed) std::fprintf(f, " [ignorees=%u]", suppressed);
        if (g.count >= kMaxLines) std::fprintf(f, " [LIMITE %u ATTEINTE]", kMaxLines);
        std::fputc('\n', f);
    }
}

// BEGIN AP_SONIC_DAY_FREEZE_MANUAL_V1
// Test manuel. Le Werehog conserve son propre hook.
void AP_SonicDayFreezeManualOnUpdate(PPCContext& ctx, uint8_t* base)
{
    static unsigned tick = 0;
    if ((++tick % 30u) != 0u) return;
    const auto* doc = SWA::CGameDocument::GetInstance();
    const char* stage = (doc && doc->m_pMember) ? doc->m_pMember->m_StageName.c_str() : nullptr;
    if (!stage || std::strncmp(stage, "ActD_", 5) != 0) return;
    const uint32_t sonic = ctx.r3.u32;
    if (sonic < 0x10000u || ctx.r1.u32 < 0x10000u) return;
    const uint32_t sonicContext = PPC_LOAD_U32(sonic + 196u);
    if (sonicContext < 0x10000u) return;
    const uint32_t sonicVtable = PPC_LOAD_U32(sonic);
    LOGFN("[AP SONIC FREEZE CHECK] sonic={:08X} vtable={:08X} expected={:08X} context={:08X}",
        sonic, sonicVtable, ApQteDoom::kCSonicVtable, sonicContext);
    if (sonicVtable != ApQteDoom::kCSonicVtable) return;

    // BEGIN AP_BURN_MANUAL_V1
    if (!ApFreezeInject::g_pending.load(std::memory_order_acquire))
    {
        if (FILE* burnTrigger = std::fopen("ap_burn.trigger", "rb"))
        {
            std::fclose(burnTrigger);
            std::remove("ap_burn.trigger");

            if (ApFreezeInject::Arm(sonic, false, true))
                LOGFN("[AP BURN] Injection native armee");
        }
    }
    // END AP_BURN_MANUAL_V1

    // BEGIN AP_PARALYZED_MANUAL_V1
    if (!ApFreezeInject::g_pending.load(std::memory_order_acquire))
    {
        if (FILE* paralyzedTrigger = std::fopen("ap_paralyzed.trigger", "rb"))
        {
            std::fclose(paralyzedTrigger);
            std::remove("ap_paralyzed.trigger");

            if (ApFreezeInject::Arm(sonic, false, false, true))
                LOGFN("[AP PARALYZED] Injection native armee");
        }
    }
    // END AP_PARALYZED_MANUAL_V1
    // BEGIN AP_BURN_PARALYZED_AUTO_DAY_V1
    if (!ApFreezeInject::g_pending.load(std::memory_order_acquire) &&
        ApFzTrace::Ms() -
            ApFreezeInject::g_lastApSuccessMs.load(std::memory_order_acquire) >=
            ApFreezeInject::kApCooldownMs &&
        ApFreezeAuto::CanTrigger())
    {
        if (APClient::GetPendingBurn() > 0)
        {
            if (!ApFreezeInject::Arm(sonic, true, true, false))
                APClient::CancelBurn();
        }
        else if (APClient::GetPendingParalyzed() > 0)
        {
            if (!ApFreezeInject::Arm(sonic, true, false, true))
                APClient::CancelParalyzed();
        }
    }
    // END AP_BURN_PARALYZED_AUTO_DAY_V1
    // BEGIN AP_FREEZE_AUTO_DAY_V1
    if (!ApFreezeInject::g_pending.load(std::memory_order_acquire) &&
        ApFzTrace::Ms() - ApFreezeInject::g_lastApSuccessMs.load(std::memory_order_acquire) >=
            ApFreezeInject::kApCooldownMs &&
        ApFreezeAuto::CanTrigger() &&
        APClient::GetPendingFreeze() > 0)
    {
        if (!ApFreezeInject::Arm(sonic, true))
            APClient::CancelFreeze();
    }
    // END AP_FREEZE_AUTO_DAY_V1

    FILE* trigger = std::fopen("ap_freeze.trigger", "rb");
    if (!trigger) return;
    std::fclose(trigger);
    std::remove("ap_freeze.trigger");
    // BEGIN AP_FREEZE_INJECT arm
    ApFreezeInject::Arm(sonic);   // arme seulement : aucun appel natif ici (nous sommes dans CSonic::Update)
    // END AP_FREEZE_INJECT arm
    // Le code natif lit r6 : utiliser une zone zero-initialisee sur la pile PPC.
    PPCContext freezeCtx = ctx;
    const uint32_t sp = (ctx.r1.u32 - 0x400u) & ~15u;
    const uint32_t vec = sp + 0x100u;
    for (uint32_t off = 0; off < 32u; off += 4u) PPC_STORE_U32(vec + off, 0u);
    freezeCtx.r1.u32 = sp;
    freezeCtx.r3.u32 = sonicContext;
    freezeCtx.r4.u32 = 0x23u;
    freezeCtx.r5.u32 = 1u;
    freezeCtx.r6.u32 = vec;
    freezeCtx.r7.u32 = 2u;
    LOGFN("[AP SONIC FREEZE MANUAL] Tentative context={:08X} stage={}", sonicContext, stage);
    LOGFN("[AP SONIC FREEZE DIAG] Appel natif suspendu pour diagnostic. sonic={:08X} context={:08X} r4={:08X} r5={} r6={:08X} r7={}", sonic, sonicContext, freezeCtx.r4.u32, freezeCtx.r5.u32, freezeCtx.r6.u32, freezeCtx.r7.u32);
    LOGFN("[AP SONIC FREEZE MANUAL] Appel termine");
}
// END AP_SONIC_DAY_FREEZE_MANUAL_V1
PPC_FUNC(sub_8237A160)
{
    static ApDmgDiag::Gate gate;
    unsigned sup = 0;
    if (ApDmgDiag::Allow(gate, sup))
    {
        if (FILE* f = std::fopen("ap_damage_diag.log", "a"))
        {
            const uint32_t flags = ctx.r7.u32;
            std::fprintf(f, "[t=%lldms] DAY sub_8237A160 ctx=%08X r4=%08X r5=%08X r7=%08X (fire=%d freeze=%d shock=%d)",
                ApDmgDiag::Ms(), ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, flags,
                (int)(flags & 1u), (int)((flags >> 1) & 1u), (int)((flags >> 2) & 1u));
            std::fprintf(f, " vec=");
            ApDmgDiag::DumpWords(f, base, ctx.r6.u32, 0, 16);
            ApDmgDiag::Tail(f, gate, sup);
            std::fclose(f);
        }
    }
    // BEGIN AP_FREEZE_TRACE a160-enter
    const bool apFzScope = ApFzTrace::A160Enter(ctx, base);
    // END AP_FREEZE_TRACE a160-enter
    __imp__sub_8237A160(ctx, base);
    // BEGIN AP_FREEZE_TRACE a160-leave
    ApFzTrace::A160Leave(apFzScope, ctx);
    // END AP_FREEZE_TRACE a160-leave
}

PPC_FUNC(sub_823BE770)
{
    static ApDmgDiag::Gate gate;
    unsigned sup = 0;
    if (ApDmgDiag::Allow(gate, sup))
    {
        if (FILE* f = std::fopen("ap_damage_diag.log", "a"))
        {
            std::fprintf(f, "[t=%lldms] EVIL MsgDamage sub_823BE770 this=%08X msg=%08X",
                ApDmgDiag::Ms(), ctx.r3.u32, ctx.r4.u32);
            ApDmgDiag::DumpWords(f, base, ctx.r4.u32, 16, 100);   // le gestionnaire natif lit jusqu'a +96
            ApDmgDiag::Tail(f, gate, sup);
            std::fclose(f);
        }
    }
    __imp__sub_823BE770(ctx, base);
}

PPC_FUNC(sub_823BF4C8)
{
    static ApDmgDiag::Gate gate;
    unsigned sup = 0;

    const uint32_t player = ctx.r3.u32;
    uint32_t context = 0;
    const bool hasContext =
        player <= UINT32_MAX - 196u &&
        ApDmgDiag::SafeLoad(base, player + 196u, context);

    const uint32_t offsets[] = {2196u, 2204u, 2208u, 2224u};
    uint32_t before[4] = {};
    bool readableBefore[4] = {};

    const bool logThis = ApDmgDiag::Allow(gate, sup);

    if (logThis && hasContext)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            if (context <= UINT32_MAX - offsets[i])
                readableBefore[i] = ApDmgDiag::SafeLoad(
                    base, context + offsets[i], before[i]);
        }
    }

    if (logThis)
    {
        if (FILE* f = std::fopen("ap_damage_diag.log", "a"))
        {
            std::fprintf(f, "[t=%lldms] EVIL MsgFreeze sub_823BF4C8 this=%08X msg=%08X context=%08X",
                ApDmgDiag::Ms(), player, ctx.r4.u32, hasContext ? context : 0u);
            ApDmgDiag::DumpWords(f, base, ctx.r4.u32, 0, 52);
            ApDmgDiag::Tail(f, gate, sup);
            std::fclose(f);
        }
    }

    __imp__sub_823BF4C8(ctx, base);

    if (logThis && hasContext)
    {
        if (FILE* f = std::fopen("ap_damage_diag.log", "a"))
        {
            std::fprintf(f, "[t=%lldms] EVIL FREEZE CONTEXT", ApDmgDiag::Ms());
            for (unsigned i = 0; i < 4; ++i)
            {
                uint32_t after = 0;
                const bool readableAfter =
                    context <= UINT32_MAX - offsets[i] &&
                    ApDmgDiag::SafeLoad(base, context + offsets[i], after);

                if (readableBefore[i] && readableAfter)
                    std::fprintf(f, " +%u:%08X->%08X",
                        offsets[i], before[i], after);
                else
                    std::fprintf(f, " +%u:<illisible>", offsets[i]);
            }
            std::fputc('\n', f);
            std::fclose(f);
        }
    }
}
// END AP_DAMAGE_DIAG v3

// BEGIN AP_WEREHOG_DISPATCH_DIAG v1
// Diagnostic uniquement : ne modifie aucun registre ni message.
PPC_FUNC_IMPL(__imp__sub_823C73C8);
PPC_FUNC(sub_823C73C8)
{
    static unsigned apDispatchLogCount = 0;
    if (apDispatchLogCount < 40)
    {
        const auto* doc = SWA::CGameDocument::GetInstance();
        const char* stage = (doc && doc->m_pMember)
            ? doc->m_pMember->m_StageName.c_str() : nullptr;
        const bool nightStage = stage &&
            (std::strncmp(stage, "ActN_", 5) == 0 ||
             std::strcmp(stage, "Act_EggmanLand") == 0 ||
             std::strcmp(stage, "BossDarkGaiaMoray") == 0 ||
             std::strcmp(stage, "BossPhoenix") == 0 ||
             std::strcmp(stage, "BossPetra") == 0 ||
             std::strcmp(stage, "BossEggDragoon") == 0);
        if (nightStage)
        {
            ++apDispatchLogCount;
            LOGFN("[AP WEREHOG DISPATCH] Call={} Stage={} r3={:08X} r4={:08X} r5={:08X}",
                apDispatchLogCount, stage, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
        }
    }
    // BEGIN AP_BURN_PARALYZED_AUTO_WEREHOG_V1
    const auto* apTrapDoc = SWA::CGameDocument::GetInstance();
    const char* apTrapStage = (apTrapDoc && apTrapDoc->m_pMember)
        ? apTrapDoc->m_pMember->m_StageName.c_str() : nullptr;

    const bool apTrapNightStage = apTrapStage &&
        (std::strncmp(apTrapStage, "ActN_", 5) == 0 ||
         std::strcmp(apTrapStage, "Act_EggmanLand") == 0 ||
         std::strcmp(apTrapStage, "BossDarkGaiaMoray") == 0 ||
         std::strcmp(apTrapStage, "BossPhoenix") == 0 ||
         std::strcmp(apTrapStage, "BossPetra") == 0 ||
         std::strcmp(apTrapStage, "BossEggDragoon") == 0);

    const bool apTrapCanTrigger =
        apTrapNightStage &&
        ctx.r5.u32 == 1u &&
        ctx.r3.u32 > 0x10040u &&
        ctx.r1.u32 > 0x10000u &&
        ApFzTrace::Ms() -
            ApFreezeInject::g_lastApSuccessMs.load(std::memory_order_acquire) >=
            ApFreezeInject::kApCooldownMs &&
        ApFreezeAuto::CanTrigger();
    // END AP_BURN_PARALYZED_AUTO_WEREHOG_V1
    // BEGIN AP_WEREHOG_BURN_MANUAL_V1
    // Test manuel uniquement : aucun item Archipelago consomme.
    bool apAutoBurn = false;
    FILE* apBurnFile = std::fopen("ap_burn.trigger", "rb");

    if (!apBurnFile && apTrapCanTrigger)
        apAutoBurn = APClient::GetPendingBurn() > 0;

    if (apBurnFile || apAutoBurn)
    {
        if (apBurnFile)
        {
            std::fclose(apBurnFile);
            std::remove("ap_burn.trigger");
        }


        const uint32_t apPlayer = ctx.r3.u32 - 40u;
        uint32_t apContext = 0;

        if (ApDmgDiag::SafeLoad(base, apPlayer + 196u, apContext) &&
            apContext >= 0x10000u)
        {
            const uint32_t apOldSp = ctx.r1.u32;
            const uint64_t apOldR3 = ctx.r3.u64;
            const uint64_t apOldR4 = ctx.r4.u64;
            const uint64_t apOldR5 = ctx.r5.u64;
            const uint64_t apOldR6 = ctx.r6.u64;
            const uint64_t apOldR7 = ctx.r7.u64;
            const uint64_t apOldR8 = ctx.r8.u64;
            const uint64_t apOldR9 = ctx.r9.u64;
            const uint64_t apOldR10 = ctx.r10.u64;

            const uint32_t apNewSp = (apOldSp - 0x400u) & ~15u;
            const uint32_t apMsg = apNewSp + 0x100u;

            ctx.r1.u32 = apNewSp;

            for (uint32_t off = 0; off < 64u; off += 4u)
                PPC_STORE_U32(apMsg + off, 0u);

            // Parametres experimentaux : 3.0 et 1.0.
            PPC_STORE_U32(apMsg + 24u, 0x40400000u);
            PPC_STORE_U32(apMsg + 28u, 0x3F800000u);

            uint32_t apBeforeA = 0, apBeforeB = 0;
            const bool apCanCheck =
                ApDmgDiag::SafeLoad(base, apContext + 2144u, apBeforeA) &&
                ApDmgDiag::SafeLoad(base, apContext + 2152u, apBeforeB);

            ctx.r3.u64 = apPlayer;
            ctx.r4.u64 = apMsg;

            LOGFN("[AP BURN WEREHOG] Injection native en cours");
            sub_823B74B0(ctx, base);

            uint32_t apAfterA = 0, apAfterB = 0;
            const bool apChanged =
                apCanCheck &&
                ApDmgDiag::SafeLoad(base, apContext + 2144u, apAfterA) &&
                ApDmgDiag::SafeLoad(base, apContext + 2152u, apAfterB) &&
                (apBeforeA != apAfterA || apBeforeB != apAfterB);

            LOGFN("[AP BURN WEREHOG] Champs modifies={}", apChanged);
            if (apAutoBurn)
            {
                if (apChanged)
                {
                    APClient::ConfirmBurnApplied();
                    ApFreezeInject::g_lastApSuccessMs.store(
                        ApFzTrace::Ms(), std::memory_order_release);
                    LOGFN("[AP BURN WEREHOG] Trap Archipelago confirmee");
                }
                else
                {
                    APClient::CancelBurn();
                    LOGFN("[AP BURN WEREHOG] Trap conservee");
                }
            }

            // BEGIN AP_WEREHOG_BURN_DAMAGE_START_V1
            if (apChanged)
            {
                AP_WerehogBurnStart(apContext);
            }
            // END AP_WEREHOG_BURN_DAMAGE_START_V1


            ctx.r1.u32 = apOldSp;
            ctx.r3.u64 = apOldR3;
            ctx.r4.u64 = apOldR4;
            ctx.r5.u64 = apOldR5;
            ctx.r6.u64 = apOldR6;
            ctx.r7.u64 = apOldR7;
            ctx.r8.u64 = apOldR8;
            ctx.r9.u64 = apOldR9;
            ctx.r10.u64 = apOldR10;
        }
        else
        {
            if (apAutoBurn)
                APClient::CancelBurn();
            LOGFN("[AP BURN WEREHOG] Contexte indisponible");
        }
    }
    // END AP_WEREHOG_BURN_MANUAL_V1
    // BEGIN AP_WEREHOG_PARALYZED_MANUAL_V1
    // Test manuel uniquement : aucun item Archipelago consomme.
    bool apAutoParalyzed = false;
    FILE* apParaFile = std::fopen("ap_paralyzed.trigger", "rb");

    if (!apParaFile && apTrapCanTrigger && !apAutoBurn)
        apAutoParalyzed = APClient::GetPendingParalyzed() > 0;

    if (apParaFile || apAutoParalyzed)
    {
        if (apParaFile)
        {
            std::fclose(apParaFile);
            std::remove("ap_paralyzed.trigger");
        }


        const uint32_t apPlayer = ctx.r3.u32 - 40u;
        uint32_t apContext = 0;

        if (ApDmgDiag::SafeLoad(base, apPlayer + 196u, apContext) &&
            apContext >= 0x10000u)
        {
            const uint32_t apOldSp = ctx.r1.u32;
            const uint64_t apOldR3 = ctx.r3.u64;
            const uint64_t apOldR4 = ctx.r4.u64;
            const uint64_t apOldR5 = ctx.r5.u64;
            const uint64_t apOldR6 = ctx.r6.u64;
            const uint64_t apOldR7 = ctx.r7.u64;
            const uint64_t apOldR8 = ctx.r8.u64;
            const uint64_t apOldR9 = ctx.r9.u64;
            const uint64_t apOldR10 = ctx.r10.u64;

            const uint32_t apNewSp = (apOldSp - 0x400u) & ~15u;
            const uint32_t apMsg = apNewSp + 0x100u;

            ctx.r1.u32 = apNewSp;

            for (uint32_t off = 0; off < 64u; off += 4u)
                PPC_STORE_U32(apMsg + off, 0u);

            // Parametres experimentaux : 3.0 et 1.0.
            PPC_STORE_U32(apMsg + 24u, 0x40400000u);
            PPC_STORE_U32(apMsg + 28u, 0x3F800000u);

            uint32_t apBeforeA = 0, apBeforeB = 0;
            const bool apCanCheck =
                ApDmgDiag::SafeLoad(base, apContext + 2160u, apBeforeA) &&
                ApDmgDiag::SafeLoad(base, apContext + 2168u, apBeforeB);

            ctx.r3.u64 = apPlayer;
            ctx.r4.u64 = apMsg;

            LOGFN("[AP PARALYZED WEREHOG] Injection native en cours");
            sub_823B8500(ctx, base);

            uint32_t apAfterA = 0, apAfterB = 0;
            const bool apChanged =
                apCanCheck &&
                ApDmgDiag::SafeLoad(base, apContext + 2160u, apAfterA) &&
                ApDmgDiag::SafeLoad(base, apContext + 2168u, apAfterB) &&
                (apBeforeA != apAfterA || apBeforeB != apAfterB);

            LOGFN("[AP PARALYZED WEREHOG] Champs modifies={}", apChanged);
            if (apAutoParalyzed)
            {
                if (apChanged)
                {
                    APClient::ConfirmParalyzedApplied();
                    ApFreezeInject::g_lastApSuccessMs.store(
                        ApFzTrace::Ms(), std::memory_order_release);
                    LOGFN("[AP PARALYZED WEREHOG] Trap Archipelago confirmee");
                }
                else
                {
                    APClient::CancelParalyzed();
                    LOGFN("[AP PARALYZED WEREHOG] Trap conservee");
                }
            }

            ctx.r1.u32 = apOldSp;
            ctx.r3.u64 = apOldR3;
            ctx.r4.u64 = apOldR4;
            ctx.r5.u64 = apOldR5;
            ctx.r6.u64 = apOldR6;
            ctx.r7.u64 = apOldR7;
            ctx.r8.u64 = apOldR8;
            ctx.r9.u64 = apOldR9;
            ctx.r10.u64 = apOldR10;
        }
        else
        {
            if (apAutoParalyzed)
                APClient::CancelParalyzed();
            LOGFN("[AP PARALYZED WEREHOG] Contexte indisponible");
        }
    }
    // END AP_WEREHOG_PARALYZED_MANUAL_V1

    // BEGIN AP_WEREHOG_FREEZE_MANUAL_TEST v1
    static unsigned apFreezePoll = 0;

    if ((++apFreezePoll % 60u) == 0u &&
        ctx.r5.u32 == 1u &&
        ctx.r3.u32 > 0x10040u &&
        ctx.r1.u32 > 0x10000u)
    {
        const auto* doc = SWA::CGameDocument::GetInstance();
        const char* stage = (doc && doc->m_pMember)
            ? doc->m_pMember->m_StageName.c_str() : nullptr;

        const bool night = stage &&
            (std::strncmp(stage, "ActN_", 5) == 0 ||
             std::strcmp(stage, "Act_EggmanLand") == 0 ||
             std::strcmp(stage, "BossDarkGaiaMoray") == 0 ||
             std::strcmp(stage, "BossPhoenix") == 0 ||
             std::strcmp(stage, "BossPetra") == 0 ||
             std::strcmp(stage, "BossEggDragoon") == 0);

        if (night)
        {
            // BEGIN AP_FREEZE_AUTO_WEREHOG_V1
            bool apManualFreeze = false;
            if (FILE* f = std::fopen("ap_freeze.trigger", "rb"))
            {
                std::fclose(f);
                std::remove("ap_freeze.trigger");
                apManualFreeze = true;
            }

            const bool apAutoFreeze =
                !apManualFreeze &&
                ApFzTrace::Ms() -
                    ApFreezeInject::g_lastApSuccessMs.load(std::memory_order_acquire) >=
                    ApFreezeInject::kApCooldownMs &&
                ApFreezeAuto::CanTrigger() &&
                APClient::GetPendingFreeze() > 0;

            if (apManualFreeze || apAutoFreeze)
            {
                const uint32_t player = ctx.r3.u32 - 40u;
                const uint32_t oldSp = ctx.r1.u32;

                const uint64_t oldR3 = ctx.r3.u64;
                const uint64_t oldR4 = ctx.r4.u64;
                const uint64_t oldR5 = ctx.r5.u64;
                const uint64_t oldR6 = ctx.r6.u64;
                const uint64_t oldR7 = ctx.r7.u64;
                const uint64_t oldR8 = ctx.r8.u64;
                const uint64_t oldR9 = ctx.r9.u64;
                const uint64_t oldR10 = ctx.r10.u64;

                const uint32_t newSp = (oldSp - 0x400u) & ~15u;
                const uint32_t msg = newSp + 0x100u;

                ctx.r1.u32 = newSp;

                for (uint32_t off = 0; off < 64; off += 4)
                    PPC_STORE_U32(msg + off, 0);

                PPC_STORE_U32(msg + 0, 0x820C5A64u);
                PPC_STORE_U32(msg + 4, 0x820A9D80u);
                PPC_STORE_U32(msg + 24, 0x40A00000u);
                PPC_STORE_U32(msg + 28, 0x42480000u);
                PPC_STORE_U32(msg + 48, 1u);

                LOGFN("[AP FREEZE MANUAL] Tentative player={:08X} msg={:08X}",
                    player, msg);

                ctx.r3.u64 = player;
                ctx.r4.u64 = msg;

                uint32_t freezeContext = 0;
                uint32_t beforeA = 0, beforeB = 0;
                const bool canVerify =
                    apAutoFreeze &&
                    ApDmgDiag::SafeLoad(base, player + 196u, freezeContext) &&
                    freezeContext <= UINT32_MAX - 2208u &&
                    ApDmgDiag::SafeLoad(base, freezeContext + 2196u, beforeA) &&
                    ApDmgDiag::SafeLoad(base, freezeContext + 2208u, beforeB);

                if (apAutoFreeze && !canVerify)
                {
                    APClient::CancelFreeze();
                }
                else
                {
                    sub_823BF4C8(ctx, base);

                    if (apAutoFreeze)
                    {
                        uint32_t afterA = 0, afterB = 0;
                        const bool changed =
                            ApDmgDiag::SafeLoad(base, freezeContext + 2196u, afterA) &&
                            ApDmgDiag::SafeLoad(base, freezeContext + 2208u, afterB) &&
                            (afterA != beforeA || afterB != beforeB);

                        if (changed)
                        {
                            ApFreezeInject::g_lastApSuccessMs.store(
                                ApFzTrace::Ms(), std::memory_order_release);
                            APClient::ConfirmFreezeApplied();
                            LOGFN("[AP FREEZE WEREHOG] Application confirmee");
                        }
                        else
                        {
                            APClient::CancelFreeze();
                            LOGFN("[AP FREEZE WEREHOG] Aucun changement, trap conserve");
                        }
                    }
                }

                ctx.r1.u32 = oldSp;
                ctx.r3.u64 = oldR3;
                ctx.r4.u64 = oldR4;
                ctx.r5.u64 = oldR5;
                ctx.r6.u64 = oldR6;
                ctx.r7.u64 = oldR7;
                ctx.r8.u64 = oldR8;
                ctx.r9.u64 = oldR9;
                ctx.r10.u64 = oldR10;

                LOGFN("[AP FREEZE MANUAL] Handler termine");
            }
        }
    }
    // END AP_WEREHOG_FREEZE_MANUAL_TEST v1
    __imp__sub_823C73C8(ctx, base);
}
// END AP_WEREHOG_DISPATCH_DIAG v1


