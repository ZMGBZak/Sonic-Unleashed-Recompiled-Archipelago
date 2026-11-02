

#include <cstdio>
#include <cstdarg> // AP_ACT_DIAG_COMPAT
#include "../archipelago_client.h"
#include "ap_stage_registry.h"
#include <chrono>
#include <cstring>
#include <cmath> // AP_MEDAL_RADAR_DIAG_V1: validate transform coordinates
#include <curl/curl.h>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <mutex> // AP_RING_ROULETTE_V1
#include <user/config.h>
#include <api/SWA.h>
#include <app.h>
#include <ui/game_window.h>
#include <ui/black_bar.h>
#include <gpu/video.h>
#include <xxHashMap.h>
#include <os/logger.h>
#include <kernel/function.h>
#include <ppc/ppc_recomp_shared.h>
#include "aspect_ratio_patches.h"
#include "ap_medal_radar.h" // AP_MEDAL_RADAR_AUDIO_V1
#include "ap_medal_display_names.h" // AP_MEDAL_CHECK_NAMES_V082
#include "ap_ring_roulette.h" // AP_RING_ROULETTE_V1
#include "camera_patches.h"
#include "inspire_patches.h"

// AP_TRAP_ACT_END_GUARD_V082: implemented in input_patches.cpp.
void AP_NotifyTrapStageFinished();

// These are here for now to not recompile basically all of the project.
namespace Chao::CSD
{
    struct Cast
    {
        SWA_INSERT_PADDING(0x144);
    };

    struct CastLink
    {
        be<uint32_t> ChildCastIndex;
        be<uint32_t> SiblingCastIndex;
    };

    struct CastNode
    {
        be<uint32_t> CastCount;
        xpointer<xpointer<Cast>> pCasts;
        be<uint32_t> RootCastIndex;
        xpointer<CastLink> pCastLinks;
    };

    struct CastIndex
    {
        xpointer<const char> pCastName;
        be<uint32_t> CastNodeIndex;
        be<uint32_t> CastIndex;
    };

    struct Scene
    {
        SWA_INSERT_PADDING(0x24);
        be<uint32_t> CastNodeCount;
        xpointer<CastNode> pCastNodes;
        be<uint32_t> CastCount;
        xpointer<CastIndex> pCastIndices;
    };

    struct SceneIndex
    {
        xpointer<const char> pSceneName;
        be<uint32_t> SceneIndex;
    };

    struct SceneNodeIndex
    {
        xpointer<const char> pSceneNodeName;
        be<uint32_t> SceneNodeIndex;
    };

    struct SceneNode
    {
        be<uint32_t> SceneCount;
        xpointer<xpointer<Scene>> pScenes;
        xpointer<SceneIndex> pSceneIndices;
        be<uint32_t> SceneNodeCount;
        xpointer<SceneNode> pSceneNodes;
        xpointer<SceneNodeIndex> pSceneNodeIndices;
    };

    struct Project
    {
        xpointer<SceneNode> pRootNode;
    };
}

static Mutex g_pathMutex;
static std::map<const void*, XXH64_hash_t> g_paths;

static XXH64_hash_t HashStr(const std::string_view& value)
{
    return XXH3_64bits(value.data(), value.size());
}

static void EmplacePath(const void* key, const std::string_view& value)
{
    std::lock_guard lock(g_pathMutex);
    g_paths.emplace(key, HashStr(value));
}

static void TraverseCast(Chao::CSD::Scene* scene, uint32_t castNodeIndex, Chao::CSD::CastNode* castNode, uint32_t castIndex, const std::string& parentPath)
{
    if (castIndex == ~0)
        return;

    TraverseCast(scene, castNodeIndex, castNode, castNode->pCastLinks[castIndex].SiblingCastIndex, parentPath);

    std::string path = parentPath;

    for (size_t i = 0; i < scene->CastCount; i++)
    {
        auto& index = scene->pCastIndices[i];
        if (index.CastNodeIndex == castNodeIndex && index.CastIndex == castIndex)
        {
            path += index.pCastName;
            break;
        }
    }

    EmplacePath(castNode->pCasts[castIndex].get(), path);

    if (castNode->RootCastIndex == castIndex)
        EmplacePath(castNode, path);

    path += "/";

    TraverseCast(scene, castNodeIndex, castNode, castNode->pCastLinks[castIndex].ChildCastIndex, path);
}

static void TraverseScene(Chao::CSD::Scene* scene, std::string path)
{
    EmplacePath(scene, path);
    path += "/";

    for (size_t i = 0; i < scene->CastNodeCount; i++)
    {
        auto& castNode = scene->pCastNodes[i];
        TraverseCast(scene, i, &castNode, castNode.RootCastIndex, path);
    }
}

static void TraverseSceneNode(Chao::CSD::SceneNode* sceneNode, std::string path)
{
    EmplacePath(sceneNode, path);
    path += "/";

    for (size_t i = 0; i < sceneNode->SceneCount; i++)
    {
        auto& sceneIndex = sceneNode->pSceneIndices[i];
        TraverseScene(sceneNode->pScenes[sceneIndex.SceneIndex], path + sceneIndex.pSceneName.get());
    }

    for (size_t i = 0; i < sceneNode->SceneNodeCount; i++)
    {
        auto& sceneNodeIndex = sceneNode->pSceneNodeIndices[i];
        TraverseSceneNode(&sceneNode->pSceneNodes[sceneNodeIndex.SceneNodeIndex], path + sceneNodeIndex.pSceneNodeName.get());
    }
}

void MakeCsdProjectMidAsmHook(PPCRegister& r3, PPCRegister& r29)
{
    uint8_t* base = g_memory.base;
    auto csdProject = reinterpret_cast<Chao::CSD::CProject*>(base + PPC_LOAD_U32(PPC_LOAD_U32(r3.u32 + 16) + 4));
    auto name = reinterpret_cast<const char*>(base + PPC_LOAD_U32(r29.u32));
    TraverseSceneNode(csdProject->m_pResource->pRootNode, name);
}

// Chao::CSD::CMemoryAlloc::Free
PPC_FUNC_IMPL(__imp__sub_825E2E60);
PPC_FUNC(sub_825E2E60)
{
    if (ctx.r4.u32 != NULL && PPC_LOAD_U32(ctx.r4.u32) == 0x4E594946 && PPC_LOAD_U32(ctx.r4.u32 + 0x20) == 0x6E43504A) // NYIF, nCPJ
    {
        uint32_t fileSize = PPC_LOAD_U32(ctx.r4.u32 + 0x14);

        std::lock_guard lock(g_pathMutex);
        const uint8_t* key = base + ctx.r4.u32;

        auto lower = g_paths.lower_bound(key);
        auto upper = g_paths.lower_bound(key + fileSize);

        g_paths.erase(lower, upper);
    }

    __imp__sub_825E2E60(ctx, base);
}

static float ComputeScale(float aspectRatio)
{
    return ((aspectRatio * 720.0f) / 1280.0f) / sqrt((aspectRatio * 720.0f) / 1280.0f);
}

void AspectRatioPatches::ComputeOffsets()
{
    float width = Video::s_viewportWidth;
    float height = Video::s_viewportHeight;

    g_aspectRatio = width / height;
    g_aspectRatioGameplayScale = 1.0f;

    if (g_aspectRatio >= NARROW_ASPECT_RATIO)
    {
        g_aspectRatioOffsetX = (width - height * WIDE_ASPECT_RATIO) / 2.0f;
        g_aspectRatioOffsetY = 0.0f;
        g_aspectRatioScale = height / 720.0f;

        // keep same scale above Steam Deck aspect ratio
        if (g_aspectRatio < WIDE_ASPECT_RATIO)
        {
            // interpolate to original 4:3 scale
            float steamDeckScale = g_aspectRatio / WIDE_ASPECT_RATIO;
            float narrowScale = ComputeScale(NARROW_ASPECT_RATIO);

            float lerpFactor = std::clamp((g_aspectRatio - NARROW_ASPECT_RATIO) / (STEAM_DECK_ASPECT_RATIO - NARROW_ASPECT_RATIO), 0.0f, 1.0f);
            g_aspectRatioGameplayScale = narrowScale + (steamDeckScale - narrowScale) * lerpFactor;
        }
    }
    else
    {
        // 4:3 crop
        g_aspectRatioOffsetX = (width - width * NARROW_ASPECT_RATIO) / 2.0f;
        g_aspectRatioOffsetY = (height - width / NARROW_ASPECT_RATIO) / 2.0f;
        g_aspectRatioScale = width / 960.0f;
        g_aspectRatioGameplayScale = ComputeScale(NARROW_ASPECT_RATIO);
    } 

    g_aspectRatioNarrowScale = std::clamp((g_aspectRatio - NARROW_ASPECT_RATIO) / (WIDE_ASPECT_RATIO - NARROW_ASPECT_RATIO), 0.0f, 1.0f);
}

static void GetViewport(void* application, be<uint32_t>* width, be<uint32_t>* height)
{
    *width = 1280;
    *height = 720;
}

GUEST_FUNCTION_HOOK(sub_82E169B8, GetViewport);


// AP Ring Roulette: game-thread tick. Reuses existing validated hooks.
static void AP_RouletteGameTick(const std::string& stage, void* saveData);
static thread_local bool g_apRouletteInjectingRings = false;
static std::mutex g_apRouletteRingMutex;
static std::string g_apRouletteRingStage;
static std::uint32_t g_apRouletteRingObject = 0;
static bool g_apRouletteRingNight = false;
// A native gameplay manager is captured only from the existing verified life-read hook.
// It is revalidated against the currently loaded save before any inventory payout.
static std::mutex g_apRouletteInventoryMutex;
static std::uint32_t g_apRouletteInventoryManager = 0;
static std::chrono::steady_clock::time_point g_apRouletteInventorySeen{};
static std::chrono::steady_clock::time_point g_apRouletteRingSeen;
static bool AP_RouletteIsAct(const char* stage)
{
    return stage &&
        (std::strncmp(stage, "ActD_", 5) == 0 ||
         std::strncmp(stage, "ActN_", 5) == 0 ||
         std::strcmp(stage, "Act_EggmanLand") == 0);
}
static void AP_RouletteRememberRing(std::uint32_t manager, bool isNight, const char* stage)
{
    if (!manager || !AP_RouletteIsAct(stage)) return;
    std::lock_guard<std::mutex> lock(g_apRouletteRingMutex);
    g_apRouletteRingStage = stage;
    g_apRouletteRingObject = manager;
    g_apRouletteRingNight = isNight;
    g_apRouletteRingSeen = std::chrono::steady_clock::now();
}

void AP_RememberRankStage(const std::string& stage);

static void AP_WatchActRank()
{
    static std::string lastStage;
    static int lastRank = -1;
    static bool initialized = false;

    auto* gameDoc = SWA::CGameDocument::GetInstance();

    if (!gameDoc || !gameDoc->m_pMember)
    {
        APRingRoulette::inAct.store(false, std::memory_order_release);
        { std::lock_guard<std::mutex> lock(g_apRouletteRingMutex);
          g_apRouletteRingObject = 0;
          g_apRouletteRingStage.clear(); }
        { std::lock_guard<std::mutex> lock(g_apRouletteInventoryMutex);
          g_apRouletteInventoryManager = 0; }
        return;
    }

    const std::string stage =
        gameDoc->m_pMember->m_StageName.c_str();
    APRingRoulette::inAct.store(AP_RouletteIsAct(stage.c_str()),
        std::memory_order_release);

    // BEGIN AP_MUSIC_RESULT_DIAGNOSTIC
    static std::string apMusicPreviousStage = "<initial>";
    if (stage != apMusicPreviousStage)
    {
        if (FILE* f = std::fopen("ap_music_stage.log", "a"))
        {
            std::fprintf(f, "STAGE: '%s' -> '%s'\n",
                apMusicPreviousStage.c_str(), stage.c_str());
            std::fclose(f);
        }
        apMusicPreviousStage = stage;
    }
    // END AP_MUSIC_RESULT_DIAGNOSTIC
    if (!stage.empty())
    {
        // The screen-position hook runs during normal gameplay, not just medal events.
        // Remember the active act before the results screen clears its Stage ID.
        AP_RememberRankStage(stage);
        if (stage != lastStage)
        {
            // Never re-use a Ring manager pointer from the previous Act.
            { std::lock_guard<std::mutex> lock(g_apRouletteRingMutex);
              g_apRouletteRingObject = 0;
              g_apRouletteRingStage.clear(); }
            { std::lock_guard<std::mutex> lock(g_apRouletteInventoryMutex);
              g_apRouletteInventoryManager = 0; }
            lastStage = stage;
            LOGFN("AP RANK WATCH: STAGE = {}", stage);
        }
    }

    auto* appDoc = SWA::CApplicationDocument::GetInstance();

    if (!appDoc || !appDoc->m_pMember)
        return;

    auto* gameParam =
        appDoc->m_pMember->m_spGameParameter.get();

    if (!gameParam)
        return;

    auto* saveData = gameParam->m_pSaveData.get();

    if (!saveData)
        return;

    AP_RouletteGameTick(stage, saveData);

    // AP Rings : lecture du total sauvegarde (Big Endian).
    static bool apRingsReadLogged = false;
    if (!apRingsReadLogged)
    {
        const auto* ringBytes = reinterpret_cast<const uint8_t*>(saveData) + 0x85A8;
        const std::uint32_t rings =
            (std::uint32_t(ringBytes[0]) << 24) |
            (std::uint32_t(ringBytes[1]) << 16) |
            (std::uint32_t(ringBytes[2]) << 8) |
             std::uint32_t(ringBytes[3]);

        LOGFN("AP RINGS SAVE READ: {}", rings);
        apRingsReadLogged = true;
    }
    // AP Rings : application des bonus en attente sur le thread du jeu.
    // TEST LOCAL UNIQUEMENT : ne pas utiliser avec les vrais items AP
    // avant d'avoir implemente la protection persistante contre les doublons.
    /* ANCIEN BONUS RINGS SAUVEGARDE DESACTIVE
    const std::uint32_t apRingBonus = APClient::GetPendingRingBonus();
    if (apRingBonus > 0)
    {
        auto* ringBytes = reinterpret_cast<std::uint8_t*>(saveData) + 0x85A8;

        const std::uint32_t oldRings =
            (std::uint32_t(ringBytes[0]) << 24) |
            (std::uint32_t(ringBytes[1]) << 16) |
            (std::uint32_t(ringBytes[2]) << 8) |
             std::uint32_t(ringBytes[3]);

        const std::uint32_t newRings =
            oldRings > UINT32_MAX - apRingBonus
                ? UINT32_MAX
                : oldRings + apRingBonus;

        ringBytes[0] = static_cast<std::uint8_t>(newRings >> 24);
        ringBytes[1] = static_cast<std::uint8_t>(newRings >> 16);
        ringBytes[2] = static_cast<std::uint8_t>(newRings >> 8);
        ringBytes[3] = static_cast<std::uint8_t>(newRings);

        LOGFN("AP RINGS APPLIED: {} + {} = {}",
            oldRings, apRingBonus, newRings);
    }
    */
    LOGFN("AP SAVE POINTER: {}", static_cast<const void*>(saveData));

    const auto* bytes =
        reinterpret_cast<const uint8_t*>(saveData);

    // Offset confirme pour Mykonos Act 2 uniquement.
    const int rank = bytes[0x00D7];

    if (!initialized)
    {
        initialized = true;
        lastRank = rank;
        LOGFN("AP RANK WATCH: INITIAL RANK = {}", rank);
    }
    else if (rank != lastRank)
    {
        LOGFN(
            "AP RANK WATCH: RANK CHANGE {} -> {} | STAGE = {}",
            lastRank, rank, stage
        );

        lastRank = rank;
    }
}


// Archipelago Shoes: diagnostic en lecture seule pour retrouver les flags en RAM.
// Les offsets 0x8998..0x899B sont des offsets du fichier SYS-DATA,
// PAS des offsets memoire verifies.
static void AP_DiagnoseShoesMemory()
{
    static bool logged = false;
    if (logged)
        return;

    auto* gameDoc = SWA::CGameDocument::GetInstance();
    if (!gameDoc || !gameDoc->m_pMember ||
        gameDoc->m_pMember->m_StageName.c_str()[0] == '\0')
        return;

    auto* appDoc = SWA::CApplicationDocument::GetInstance();
    if (!appDoc || !appDoc->m_pMember)
        return;

    auto* gameParam = appDoc->m_pMember->m_spGameParameter.get();
    if (!gameParam || !gameParam->m_pSaveData.get())
        return;

    const auto* bytes = reinterpret_cast<const uint8_t*>(gameParam->m_pSaveData.get());
    constexpr size_t size = sizeof(SWA::CGameParameter::SSaveData);
    logged = true;

    LOGFN("AP SHOES RAM: diagnostic read-only; SSaveData size=0x{:X}",
        static_cast<uint32_t>(size));

    // Hypothese seulement : si SYS-DATA se terminait par SSaveData,
    // l'offset fichier 0x8998 correspondrait a 0x67C4 en RAM.
    constexpr size_t candidate = 0x67C4;
    if (candidate + 4 <= size)
        LOGFN("AP SHOES RAM: candidate 0x67C4 = {:02X} {:02X} {:02X} {:02X}",
            bytes[candidate], bytes[candidate + 1],
            bytes[candidate + 2], bytes[candidate + 3]);

    // Sequence observee dans le SYS-DATA 'SAVE WITH SHOES'.
    // Une correspondance est une piste, pas une preuve.
    static constexpr uint8_t signature[] =
        { 0x63, 0x01, 0x01, 0x01, 0x01, 0x01, 0x07, 0x01, 0x07, 0x01, 0x07 };
    size_t matches = 0;
    for (size_t i = 0; i + sizeof(signature) <= size; ++i)
    {
        if (std::memcmp(bytes + i, signature, sizeof(signature)) == 0)
        {
            LOGFN("AP SHOES RAM: signature found at 0x{:X}; shoe bytes candidate 0x{:X}",
                static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1));
            ++matches;
        }
    }
    LOGFN("AP SHOES RAM: signature matches={}", matches);

    // AP CD RAM : recherche des CD #35, #36 et #37.
    // Diagnostic en lecture seule.
    static constexpr uint8_t cdSignature[] =
        { 0x01, 0x07, 0x01, 0x07, 0x01, 0x07 };

    size_t cdMatches = 0;

    for (size_t i = 0; i + sizeof(cdSignature) <= size; ++i)
    {
        if (std::memcmp(bytes + i, cdSignature, sizeof(cdSignature)) == 0)
        {
            if (cdMatches < 20)
                LOGFN("AP CD RAM: signature found at 0x{:X}",
                    static_cast<uint32_t>(i));

            ++cdMatches;
        }
    }

    LOGFN("AP CD RAM: total matches={}", cdMatches);
}

// AP_SHOES_SYNC_V082: appliquer pendant le gameplay apres les pickups.
extern void AP_SyncShoesFromArchipelago();

// SWA::CGameDocument::ComputeScreenPosition
PPC_FUNC_IMPL(__imp__sub_8250FC70);
PPC_FUNC(sub_8250FC70)
{
    __imp__sub_8250FC70(ctx, base);
    // AP_SHOES_SYNC_V082: verifier ~20 fois/seconde, au maximum.
    static uint32_t apShoeSyncCalls = 0;
    if ((++apShoeSyncCalls & 0x1Fu) == 0)
    {
        static auto nextShoeSync = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextShoeSync)
        {
            nextShoeSync = now + std::chrono::milliseconds(50);
            AP_SyncShoesFromArchipelago();
        }
    }
    static unsigned int apTriggerCalls = 0;
    if (++apTriggerCalls <= 10) LOGFN("AP FILLER TRIGGER TEST: {} appels", apTriggerCalls);
    // AP Crystal XP : application depuis le thread du jeu.
    {
        auto* appDoc = SWA::CApplicationDocument::GetInstance();
        auto* param = (appDoc && appDoc->m_pMember)
            ? appDoc->m_pMember->m_spGameParameter.get()
            : nullptr;
        auto* save = param ? param->m_pSaveData.get() : nullptr;

        if (save)
        {
            const std::uint32_t crystalXP =
                APClient::GetPendingCrystalXPBonus();

            if (crystalXP == 50u ||
                crystalXP == 100u ||
                crystalXP == 200u)
            {
                LOGFN("AP CRYSTAL XP: applying +{} XP", crystalXP);

                GuestToHostFunction<void>(
                    sub_823A8760, 0u, crystalXP
                );

                APClient::ConfirmCrystalXPBonusApplied();

                LOGFN("AP CRYSTAL XP: native call completed");
            }
        }
    }

            static bool apHookTestLogged = false;

    if (!apHookTestLogged)
    {
        LOGFN("AP RANK WATCH: HOOK EXECUTED");
        apHookTestLogged = true;
    }

        static bool apLocalRingsTestDone = false;
        if (!apLocalRingsTestDone)
        {
            apLocalRingsTestDone = true;
            // AP_TestLocalRings(); // Ancien test des Rings desactive
        }
        AP_WatchActRank();
        AP_DiagnoseShoesMemory();
        // AP CD RAM : diagnostic independant en lecture seule.
        {
            static unsigned cdCounter = 0;
            static bool cdInitialized = false;
            static std::vector<size_t> cdPreviousMatches;

            if (++cdCounter >= 1)
            {
                cdCounter = 0;

                auto* appDoc = SWA::CApplicationDocument::GetInstance();
                auto* param = (appDoc && appDoc->m_pMember)
                    ? appDoc->m_pMember->m_spGameParameter.get()
                    : nullptr;
                auto* save = param ? param->m_pSaveData.get() : nullptr;

                if (!save)
                {
                    cdInitialized = false;
                    cdPreviousMatches.clear();
                }
                else
                {
                    const auto* bytes = reinterpret_cast<const uint8_t*>(save);
                    constexpr size_t size = sizeof(SWA::CGameParameter::SSaveData);
                    static constexpr uint8_t signature[] =
                        { 0x01, 0x07, 0x01, 0x07, 0x01, 0x07 };

                    // AP CD RAM : verifier les octets candidats des CD #35-37.
                    constexpr size_t cdOffset = 0x8198;
                    if (cdOffset + 6 <= size)
                    {
                        static bool firstRead = true;
                        static uint8_t previousCD[6]{};

                        if (firstRead ||
                            std::memcmp(previousCD, bytes + cdOffset, 6) != 0)
                        {
                            LOGFN(
                                "AP CD BYTES 0x8198: {:02X} {:02X} {:02X} {:02X} {:02X} {:02X}",
                                bytes[cdOffset], bytes[cdOffset + 1],
                                bytes[cdOffset + 2], bytes[cdOffset + 3],
                                bytes[cdOffset + 4], bytes[cdOffset + 5]
                            );

                            std::memcpy(previousCD, bytes + cdOffset, 6);
                            firstRead = false;
                        }
                    }
                    
                    std::vector<size_t> found;

                    for (size_t i = 0; i + sizeof(signature) <= size; ++i)
                    {
                        if (std::memcmp(bytes + i, signature, sizeof(signature)) == 0)
                            found.push_back(i);
                    }

                    if (!cdInitialized || found != cdPreviousMatches)
                    {
                        LOGFN("AP CD WATCH ACTIVE: total matches={}", found.size());

                        for (size_t i = 0; i < found.size() && i < 20; ++i)
                            LOGFN("AP CD RAM: signature found at 0x{:X}",
                                static_cast<uint32_t>(found[i]));

                        cdPreviousMatches = found;
                        cdInitialized = true;
                    }
                }
            }
        }
        // AP DEBUG : lecture seule des changements dans SSaveData.
        {
            constexpr size_t AP_SAVE_SIZE = sizeof(SWA::CGameParameter::SSaveData);
            static uint8_t previous[AP_SAVE_SIZE]{};
            static const void* previousSaveData = nullptr;
            static bool initialized = false;
            static uint32_t counter = 0;

            auto* appDoc = SWA::CApplicationDocument::GetInstance();
            auto* gameParam = (appDoc && appDoc->m_pMember)
                ? appDoc->m_pMember->m_spGameParameter.get()
                : nullptr;
            auto* saveData = gameParam ? gameParam->m_pSaveData.get() : nullptr;

            if (!saveData) {
                initialized = false;
                previousSaveData = nullptr;
                counter = 0;
            } else {
                const auto* current = reinterpret_cast<const uint8_t*>(saveData);
                if (!initialized || previousSaveData != saveData) {
                    for (size_t i = 0; i < AP_SAVE_SIZE; ++i)
                        previous[i] = current[i];
                    previousSaveData = saveData;
                    initialized = true;
                    counter = 0;
                    LOGFN("AP COLLECTIBLE DEBUG: surveillance active");
                } else if (++counter >= 60) {
                    counter = 0;
                    uint32_t changed = 0;
                    for (size_t i = 0; i < AP_SAVE_SIZE; ++i) {
                        if (previous[i] != current[i]) {
                            if (changed < 40) {
                                LOGFN(
                                    "AP COLLECTIBLE RAM: offset 0x{:04X}: {:02X} -> {:02X}",
                                    static_cast<uint32_t>(i),
                                    static_cast<uint32_t>(previous[i]),
                                    static_cast<uint32_t>(current[i])
                                );
                            }
                            ++changed;
                            previous[i] = current[i];
                        }
                    }
                    if (changed > 0)
                        LOGFN("AP COLLECTIBLE DEBUG: {} octets modifies", changed);
                }
            }
        }

    auto position = reinterpret_cast<be<float>*>(base + ctx.r3.u32);

    position[0] = (position[0] / 1280.0f * Video::s_viewportWidth - g_aspectRatioOffsetX) / g_aspectRatioScale;
    position[1] = (position[1] / 720.0f * Video::s_viewportHeight - g_aspectRatioOffsetY) / g_aspectRatioScale;
}

void ComputeScreenPositionMidAsmHook(PPCRegister& f1, PPCRegister& f2)
{
    f1.f64 = (f1.f64 / 1280.0 * Video::s_viewportWidth - g_aspectRatioOffsetX) / g_aspectRatioScale;
    f2.f64 = (f2.f64 / 720.0 * Video::s_viewportHeight - g_aspectRatioOffsetY) / g_aspectRatioScale;
}

void APHubSunMedalHook(PPCRegister& r5)
{
    // AP HUB HOOK TEST
    static unsigned int apHubCalls = 0;
    if (++apHubCalls % 300 == 0)
        LOGFN("[AP HUB HOOK TEST] Calls={}", apHubCalls);

    r5.u64 = APClient::GetSunMedalBonus();
}

void APHubMoonMedalHook(PPCRegister& r5)
{
    r5.u64 = APClient::GetMoonMedalBonus();
}
void APWorldMapSunMedalHook(PPCRegister& r6)
{
    r6.u64 = APClient::GetSunMedalBonus();
}

void APWorldMapMoonMedalHook(PPCRegister& r6)
{
    r6.u64 = APClient::GetMoonMedalBonus();
}
void WorldMapInfoMidAsmHook(PPCRegister& r4)
{
    // Prevent the game from snapping "cts_parts_sun_moon"
    // to "cts_guide_icon" automatically, we will do this ourselves.
    r4.u32 = 0x8200A621;
}

// SWA::CTitleStateWorldMap::Update
PPC_FUNC_IMPL(__imp__sub_8258B558);
PPC_FUNC(sub_8258B558)
{
    auto r3 = ctx.r3;
    __imp__sub_8258B558(ctx, base);

    uint32_t worldMapSimpleInfo = PPC_LOAD_U32(r3.u32 + 0x70);
    if (worldMapSimpleInfo != NULL)
    {
        auto setPosition = [&](uint32_t rcPtr, float offsetX = 0.0f, float offsetY = 0.0f)
            {
                uint32_t scene = PPC_LOAD_U32(rcPtr + 0x4);
                if (scene != NULL)
                {
                    scene = PPC_LOAD_U32(scene + 0x4);
                    if (scene != NULL)
                    {
                        ctx.r3.u32 = scene;
                        ctx.f1.f64 = offsetX + g_aspectRatioNarrowScale * 140.0f;
                        ctx.f2.f64 = offsetY;

                        if (Config::UIAlignmentMode == EUIAlignmentMode::Edge && g_aspectRatioNarrowScale >= 1.0f)
                            ctx.f1.f64 += g_aspectRatioOffsetX / g_aspectRatioScale;

                        sub_830BB3D0(ctx, base);
                    }
                }
            };

        setPosition(worldMapSimpleInfo + 0x2C, 299.0f, -178.0f);
        setPosition(worldMapSimpleInfo + 0x34);
        setPosition(worldMapSimpleInfo + 0x4C);

        for (uint32_t it = PPC_LOAD_U32(worldMapSimpleInfo + 0x20); it != PPC_LOAD_U32(worldMapSimpleInfo + 0x24); it += 8)
            setPosition(it);

        uint32_t menuTextBox = PPC_LOAD_U32(worldMapSimpleInfo + 0x5C);
        if (menuTextBox != NULL)
        {
            uint32_t textBox = PPC_LOAD_U32(menuTextBox + 0x4);
            if (textBox != NULL)
            {
                float value = 708.0f + g_aspectRatioNarrowScale * 140.0f;
                if (Config::UIAlignmentMode == EUIAlignmentMode::Edge && g_aspectRatioNarrowScale >= 1.0f)
                    value += g_aspectRatioOffsetX / g_aspectRatioScale;

                PPC_STORE_U32(textBox + 0x38, reinterpret_cast<uint32_t&>(value));
            }
        }
    }
}

enum
{
    ALIGN_CENTER = 0 << 0,

    ALIGN_TOP = 1 << 0,
    ALIGN_LEFT = 1 << 1,
    ALIGN_BOTTOM = 1 << 2,
    ALIGN_RIGHT = 1 << 3,

    ALIGN_TOP_LEFT = ALIGN_TOP | ALIGN_LEFT,
    ALIGN_TOP_RIGHT = ALIGN_TOP | ALIGN_RIGHT,
    ALIGN_BOTTOM_LEFT = ALIGN_BOTTOM | ALIGN_LEFT,
    ALIGN_BOTTOM_RIGHT = ALIGN_BOTTOM | ALIGN_RIGHT,

    STRETCH_HORIZONTAL = 1 << 4,
    STRETCH_VERTICAL = 1 << 5,

    STRETCH = STRETCH_HORIZONTAL | STRETCH_VERTICAL,

    SCALE = 1 << 6,

    WORLD_MAP = 1 << 7,

    EXTEND_LEFT = 1 << 8,
    EXTEND_RIGHT = 1 << 9,

    STORE_LEFT_CORNER = 1 << 10,
    STORE_RIGHT_CORNER = 1 << 11,

    SKIP = 1 << 12,

    OFFSET_SCALE_LEFT = 1 << 13,
    OFFSET_SCALE_RIGHT = 1 << 14,

    REPEAT_LEFT = 1 << 15,

    TORNADO_DEFENSE = 1 << 16,

    LOADING_BLACK_BAR_MIN = 1 << 17,
    LOADING_BLACK_BAR_MAX = 1 << 18,

    UNSTRETCH_HORIZONTAL = 1 << 19,

    CORNER_EXTRACT = 1 << 20,

    SKIP_INSPIRE = 1 << 21,

    CONTROL_TUTORIAL = 1 << 22,

    LOADING_ARROW = 1 << 23,
};

struct CsdModifier
{
    uint32_t flags{};
    float cornerMax{};
    uint32_t cornerIndex{};
};

static const xxHashMap<CsdModifier> g_modifiers =
{
    // ui_balloon
    { HashStr("ui_balloon/window/bg"), { STRETCH } },
    { HashStr("ui_balloon/window/footer"), { ALIGN_BOTTOM } },

    // ui_boss_gauge
    { HashStr("ui_boss_gauge/gauge_bg"), { ALIGN_TOP_RIGHT | SCALE | SKIP_INSPIRE} },
    { HashStr("ui_boss_gauge/gauge_2"), { ALIGN_TOP_RIGHT | SCALE | SKIP_INSPIRE} },
    { HashStr("ui_boss_gauge/gauge_1"), { ALIGN_TOP_RIGHT | SCALE | SKIP_INSPIRE} },
    { HashStr("ui_boss_gauge/gauge_breakpoint"), { ALIGN_TOP_RIGHT | SCALE | SKIP_INSPIRE} },

    // ui_boss_name
    { HashStr("ui_boss_name/name_so/bg"), { UNSTRETCH_HORIZONTAL } },
    { HashStr("ui_boss_name/name_so/pale"), { UNSTRETCH_HORIZONTAL } },

    // ui_exstage
    { HashStr("ui_exstage/shield/L_gauge"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_exstage/shield/L_gauge_effect"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_exstage/shield/L_gauge_effect_2"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_exstage/energy/R_gauge"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_exstage/energy/R_gauge_effect"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_exstage/energy/R_gauge_effect_2"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_exstage/hit/hit_counter_bg"), { ALIGN_RIGHT | SCALE | OFFSET_SCALE_RIGHT, 986.0f } },
    { HashStr("ui_exstage/hit/hit_counter_bg/C"), { ALIGN_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_exstage/hit/hit_counter_bg/C/L"), { SKIP } }, // L/R are mixed up
    { HashStr("ui_exstage/hit/hit_counter_bg/C/R"), { ALIGN_RIGHT | SCALE | STORE_LEFT_CORNER } },
    { HashStr("ui_exstage/hit/hit_counter_num"), { ALIGN_RIGHT | SCALE } },

    // ui_gate
    { HashStr("ui_gate/footer/status_footer"), { ALIGN_BOTTOM } },
    { HashStr("ui_gate/header/status_title"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 652.0f } },
    { HashStr("ui_gate/header/status_title/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_gate/header/status_title/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_gate/header/status_title/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },
    { HashStr("ui_gate/window/window_bg"), { STRETCH } },

    // ui_general
    { HashStr("ui_general/bg"), { STRETCH } },
    { HashStr("ui_general/footer"), { ALIGN_BOTTOM } },

    // ui_itemresult
    { HashStr("ui_itemresult/footer/result_footer"), { ALIGN_BOTTOM } },
    { HashStr("ui_itemresult/main/iresult_title"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 688.0f } },
    { HashStr("ui_itemresult/main/iresult_title/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_itemresult/main/iresult_title/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_itemresult/main/iresult_title/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },
    { HashStr("ui_itemresult/main/iresult_title/title_brilliance1"), { ALIGN_TOP | STORE_RIGHT_CORNER | OFFSET_SCALE_LEFT, 632.25775f, 1 } },
    { HashStr("ui_itemresult/main/iresult_title/title_brilliance2"), { ALIGN_TOP | STORE_RIGHT_CORNER | OFFSET_SCALE_LEFT, 830.0f, 2 } },
    { HashStr("ui_itemresult/main/iresult_title/title_brilliance3"), { ALIGN_TOP | STORE_RIGHT_CORNER | OFFSET_SCALE_LEFT, 640.0f, 3 } },

    // ui_loading
    { HashStr("ui_loading/bg_1"), { STRETCH } },
    { HashStr("ui_loading/bg_1/arrow"), { STRETCH | LOADING_ARROW } },
    { HashStr("ui_loading/bg_2"), { STRETCH } },
    { HashStr("ui_loading/bg_2/arrow"), { STRETCH | LOADING_ARROW } },
    { HashStr("ui_loading/n_2_d/bg/sky"), { STRETCH } },
    { HashStr("ui_loading/n_2_d/bg/under"), { STRETCH } },
    { HashStr("ui_loading/n_2_d/letterbox/letterbox_under"), { STRETCH } },
    { HashStr("ui_loading/n_2_d/letterbox/letterbox_top"), { STRETCH } },
    { HashStr("ui_loading/n_2_d/letterbox/black_l"), { EXTEND_LEFT | STRETCH_VERTICAL } },
    { HashStr("ui_loading/n_2_d/letterbox/black_r"), { EXTEND_RIGHT | STRETCH_VERTICAL } },
    { HashStr("ui_loading/event_viewer/black/black_top"), { LOADING_BLACK_BAR_MIN } },
    { HashStr("ui_loading/event_viewer/black/black_under"), { LOADING_BLACK_BAR_MAX } },
    { HashStr("ui_loading/pda/pda_frame/L"), { LOADING_BLACK_BAR_MIN } },
    { HashStr("ui_loading/pda/pda_frame/R"), { LOADING_BLACK_BAR_MAX } },

    // ui_mediaroom
    { HashStr("ui_mediaroom/header/bg/img_1"), { EXTEND_LEFT } },
    { HashStr("ui_mediaroom/header/bg/img_10"), { EXTEND_RIGHT } },  
    { HashStr("ui_mediaroom/header/frame/img_1"), { EXTEND_LEFT } },
    { HashStr("ui_mediaroom/header/frame/img_5"), { EXTEND_RIGHT } },

    // ui_missionscreen
    { HashStr("ui_missionscreen/player_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_missionscreen/time_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_missionscreen/time_count/position_S/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_S/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_S/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_S/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_L/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_L/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_L/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/time_count/position_L/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_missionscreen/laptime_count/position_S/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_S/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_S/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_S/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_L/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_L/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_L/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/laptime_count/position_L/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_missionscreen/score_count/position_S/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_S/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_S/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_S/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_L/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_L/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_L/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/score_count/position_L/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/item_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_missionscreen/item_count/position_L/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/item_count/position_L/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/item_count/position_L/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/item_count/position_L/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_missionscreen/lap_count"), { ALIGN_TOP_RIGHT | SCALE } },
    { HashStr("ui_missionscreen/lap_count/position/bar"), { ALIGN_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_missionscreen/lap_count/position/bar/R"), { SKIP } },

    // ui_misson
    { HashStr("ui_misson/bg"), { STRETCH } },
    { HashStr("ui_misson/footer/footer_B"), { ALIGN_BOTTOM } },
    { HashStr("ui_misson/header/misson_title_B"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 638.0f } },
    { HashStr("ui_misson/header/misson_title_B/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_misson/header/misson_title_B/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT} },
    { HashStr("ui_misson/header/misson_title_B/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },
    { HashStr("ui_misson/window/bg_B2/position/bg"), { STRETCH } },

    // ui_pause
    { HashStr("ui_pause/bg"), { STRETCH } },
    { HashStr("ui_pause/footer/footer_A"), { ALIGN_BOTTOM } },
    { HashStr("ui_pause/footer/footer_B"), { ALIGN_BOTTOM } },
    { HashStr("ui_pause/header/status_title"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 585.0f } },
    { HashStr("ui_pause/header/status_title/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_pause/header/status_title/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT} },
    { HashStr("ui_pause/header/status_title/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },

    // ui_playscreen
    { HashStr("ui_playscreen/player_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen/time_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen/time_count/position_s/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/time_count/position_s/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },   
    { HashStr("ui_playscreen/time_count/position_s/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/time_count/position_s/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/score_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen/score_count/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/score_count/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/score_count/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/score_count/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/exp_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen/exp_count/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/exp_count/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/exp_count/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/exp_count/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen/so_speed_gauge"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen/so_ringenagy_gauge"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen/gauge_frame"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen/ring_count"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen/ring_get"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen/add/speed_count"), { ALIGN_RIGHT | SCALE } },
    { HashStr("ui_playscreen/add/speed_count/position/bar"), { ALIGN_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen/add/speed_count/position/bar/R"), { SKIP } }, 
    { HashStr("ui_playscreen/add/speed_count/position/bar_pale"), { ALIGN_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen/add/speed_count/position/bar_pale/R"), { SKIP } },
    { HashStr("ui_playscreen/add/u_info"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_playscreen/add/u_info/position/bar"), { ALIGN_BOTTOM_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen/add/u_info/position/bar/R"), { SKIP } },
    { HashStr("ui_playscreen/add/medal_get_s"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_playscreen/add/medal_get_s/position/bar"), { ALIGN_BOTTOM_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen/add/medal_get_s/position/bar/R"), { SKIP } },
    { HashStr("ui_playscreen/add/medal_get_m"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_playscreen/add/medal_get_m/position/bar"), { ALIGN_BOTTOM_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen/add/medal_get_m/position/bar/R"), { SKIP } },

    // ui_playscreen_ev
    { HashStr("ui_playscreen_ev/player_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/score_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/score_count/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/score_count/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/score_count/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/score_count/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/ring_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/ring_count/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/ring_count/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/ring_count/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/ring_count/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/ring_get"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/exp_count"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/exp_count/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/exp_count/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/exp_count/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/exp_count/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | EXTEND_LEFT } },
    { HashStr("ui_playscreen_ev/add/u_info"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev/add/u_info/position/bar"), { ALIGN_BOTTOM_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen_ev/add/u_info/position/bar/R"), { SKIP } },
    { HashStr("ui_playscreen_ev/add/medal_get_s"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev/add/medal_get_s/position/bar"), { ALIGN_BOTTOM_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen_ev/add/medal_get_s/position/bar/R"), { SKIP } },
    { HashStr("ui_playscreen_ev/add/medal_get_m"), { ALIGN_BOTTOM_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev/add/medal_get_m/position/bar"), { ALIGN_BOTTOM_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen_ev/add/medal_get_m/position/bar/R"), { SKIP } },
    { HashStr("ui_playscreen_ev/gauge/unleash_bg"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/life_bg"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/unleash_body"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/unleash_bar_1"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/unleash_gauge"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/unleash_gauge_effect_2"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/unleash_gauge_effect"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/unleash_bar_2"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/life"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield_position"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_01"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_02"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_03"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_04"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_05"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_06"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_07"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_08"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_09"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_10"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_11"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_12"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_13"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_14"), { ALIGN_BOTTOM_LEFT | SCALE } },
    { HashStr("ui_playscreen_ev/gauge/shield/shield_15"), { ALIGN_BOTTOM_LEFT | SCALE } },

    // ui_playscreen_ev_hit
    { HashStr("ui_playscreen_ev_hit/hit_counter_bg"), { ALIGN_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev_hit/hit_counter_bg/C"), { ALIGN_RIGHT | SCALE | EXTEND_RIGHT } },
    { HashStr("ui_playscreen_ev_hit/hit_counter_bg/C/L"), { ALIGN_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev_hit/hit_counter_bg/C/R"), { SKIP } },
    { HashStr("ui_playscreen_ev_hit/hit_counter_num"), { ALIGN_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev_hit/hit_counter_txt_1"), { ALIGN_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev_hit/hit_counter_txt_2"), { ALIGN_RIGHT | SCALE } },
    { HashStr("ui_playscreen_ev_hit/chance_attack"), { ALIGN_RIGHT | SCALE } },

    // ui_playscreen_su
    { HashStr("ui_playscreen_su/su_sonic_gauge"), { ALIGN_BOTTOM_LEFT | SCALE | OFFSET_SCALE_LEFT, 632.0f } },
    { HashStr("ui_playscreen_su/su_sonic_gauge/position/C/R"), { ALIGN_BOTTOM_LEFT | SCALE | STORE_RIGHT_CORNER } },
    { HashStr("ui_playscreen_su/gaia_gauge"), { ALIGN_BOTTOM_LEFT | SCALE | OFFSET_SCALE_LEFT, 632.0f } },
    { HashStr("ui_playscreen_su/gaia_gauge/position/C/R"), { ALIGN_BOTTOM_LEFT | SCALE | STORE_RIGHT_CORNER } },
    { HashStr("ui_playscreen_su/footer"), { ALIGN_BOTTOM_RIGHT | SCALE | CONTROL_TUTORIAL } },

    // ui_prov_playscreen
    { HashStr("ui_prov_playscreen/so_speed_gauge"), { ALIGN_BOTTOM_LEFT | SCALE | TORNADO_DEFENSE } },
    { HashStr("ui_prov_playscreen/so_ringenagy_gauge"), { ALIGN_BOTTOM_LEFT | SCALE | TORNADO_DEFENSE } },
    { HashStr("ui_prov_playscreen/bg"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE } },
    { HashStr("ui_prov_playscreen/info_1"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE } },
    { HashStr("ui_prov_playscreen/info_1/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_1/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_1/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_1/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_2"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE } },
    { HashStr("ui_prov_playscreen/info_2/position/bg_1"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_2/position/bg_1/C_h"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_2/position/bg_2"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/info_2/position/bg_2/C_h"), { ALIGN_TOP_LEFT | SCALE | TORNADO_DEFENSE | EXTEND_LEFT } },
    { HashStr("ui_prov_playscreen/ring_get_effect"), { ALIGN_BOTTOM_LEFT | SCALE | TORNADO_DEFENSE } },

    // ui_result
    { HashStr("ui_result/footer/result_footer"), { ALIGN_BOTTOM } },
    { HashStr("ui_result/main/result_title"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 688.0f } },
    { HashStr("ui_result/main/result_title/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_result/main/result_title/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT} },
    { HashStr("ui_result/main/result_title/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },
    { HashStr("ui_result/main/result_num_1"), { CORNER_EXTRACT } },
    { HashStr("ui_result/main/result_num_1/num_bg"), { OFFSET_SCALE_RIGHT, 669.0f } },
    { HashStr("ui_result/main/result_num_1/num_bg/position_1/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_1/num_bg/position_1/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_1/num_bg/position_1/center_1/left"), { STORE_LEFT_CORNER } },
    { HashStr("ui_result/main/result_num_1/num_bg/position_1/center_1/right"), { SKIP } },
    { HashStr("ui_result/main/result_num_1/num_bg/position_1/center_1/right/h_light"), { SKIP } },  
    { HashStr("ui_result/main/result_num_2"), { CORNER_EXTRACT } },
    { HashStr("ui_result/main/result_num_2/num_bg"), { OFFSET_SCALE_RIGHT, 669.0f } },
    { HashStr("ui_result/main/result_num_2/num_bg/position_2/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_2/num_bg/position_2/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_2/num_bg/position_2/center_1/left"), { STORE_LEFT_CORNER } },
    { HashStr("ui_result/main/result_num_2/num_bg/position_2/center_1/right"), { SKIP } },
    { HashStr("ui_result/main/result_num_2/num_bg/position_2/center_1/right/h_light"), { SKIP } },   
    { HashStr("ui_result/main/result_num_3"), { CORNER_EXTRACT } },
    { HashStr("ui_result/main/result_num_3/num_bg"), { OFFSET_SCALE_RIGHT, 669.0f } },
    { HashStr("ui_result/main/result_num_3/num_bg/position_3/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_3/num_bg/position_3/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_3/num_bg/position_3/center_1/left"), { STORE_LEFT_CORNER } },
    { HashStr("ui_result/main/result_num_3/num_bg/position_3/center_1/right"), { SKIP } },
    { HashStr("ui_result/main/result_num_3/num_bg/position_3/center_1/right/h_light"), { SKIP } },  
    { HashStr("ui_result/main/result_num_4"), { CORNER_EXTRACT } },
    { HashStr("ui_result/main/result_num_4/num_bg"), { OFFSET_SCALE_RIGHT, 669.0f } },
    { HashStr("ui_result/main/result_num_4/num_bg/position_4/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_4/num_bg/position_4/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_4/num_bg/position_4/center_1/left"), { STORE_LEFT_CORNER } },
    { HashStr("ui_result/main/result_num_4/num_bg/position_4/center_1/right"), { SKIP } },
    { HashStr("ui_result/main/result_num_4/num_bg/position_4/center_1/right/h_light"), { SKIP } },   
    { HashStr("ui_result/main/result_num_5"), { CORNER_EXTRACT } },
    { HashStr("ui_result/main/result_num_5/num_bg"), { OFFSET_SCALE_RIGHT, 669.0f } },
    { HashStr("ui_result/main/result_num_5/num_bg/position_5/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_5/num_bg/position_5/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result/main/result_num_5/num_bg/position_5/center_1/left"), { STORE_LEFT_CORNER } },
    { HashStr("ui_result/main/result_num_5/num_bg/position_5/center_1/right"), { SKIP } },
    { HashStr("ui_result/main/result_num_5/num_bg/position_5/center_1/right/h_light"), { SKIP } }, 
    { HashStr("ui_result/main/result_num_6"), { CORNER_EXTRACT } },
    { HashStr("ui_result/main/result_num_6/num_bg"), { OFFSET_SCALE_LEFT, 1094.0f } },
    { HashStr("ui_result/main/result_num_6/num_bg/position_6/center"), { EXTEND_LEFT } },
    { HashStr("ui_result/main/result_num_6/num_bg/position_6/center/h_light"), { EXTEND_LEFT } },
    { HashStr("ui_result/main/result_num_6/num_bg/position_6/center/right"), { STORE_RIGHT_CORNER } },
    { HashStr("ui_result/main/result_num_6/num_bg/position_6/center/left"), { SKIP } },
    { HashStr("ui_result/main/result_num_6/num_bg/position_6/center/left/h_light"), { SKIP } },
    { HashStr("ui_result/newRecode/result_newR/position/newR_brilliance1"), { OFFSET_SCALE_RIGHT | STORE_LEFT_CORNER, 458.4f } },
    { HashStr("ui_result/newRecode/result_newR/position/newR_brilliance2"), { OFFSET_SCALE_RIGHT | STORE_LEFT_CORNER, 458.4f } },
    { HashStr("ui_result/newRecode/result_newR/position/newR_brilliance3"), { OFFSET_SCALE_RIGHT | STORE_LEFT_CORNER, 458.4f } },

    // ui_result_ex
    { HashStr("ui_result_ex/footer/result_footer"), { ALIGN_BOTTOM } },
    { HashStr("ui_result_ex/main/result_title"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 688.0f } },
    { HashStr("ui_result_ex/main/result_title/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_result_ex/main/result_title/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT} },
    { HashStr("ui_result_ex/main/result_title/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },
    { HashStr("ui_result_ex/main/number/result_num_1"), { OFFSET_SCALE_LEFT | OFFSET_SCALE_RIGHT, 669.0f } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_1"), { OFFSET_SCALE_RIGHT, 669.0f, 0 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_1/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_1/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_1/center_1/left"), { STORE_LEFT_CORNER, 0, 0 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_1/center_1/right"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_1/center_1/right/h_light"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_2"), { OFFSET_SCALE_RIGHT, 669.0f, 1 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_2/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_2/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_2/center_1/left"), { STORE_LEFT_CORNER, 0, 1 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_2/center_1/right"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_2/center_1/right/h_light"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_3"), { OFFSET_SCALE_RIGHT, 669.0f, 2 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_3/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_3/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_3/center_1/left"), { STORE_LEFT_CORNER, 0, 2 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_3/center_1/right"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_3/center_1/right/h_light"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_4"), { OFFSET_SCALE_RIGHT, 669.0f, 3 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_4/center_1"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_4/center_1/h_light"), { EXTEND_RIGHT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_4/center_1/left"), { STORE_LEFT_CORNER, 0, 3 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_4/center_1/right"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_4/center_1/right/h_light"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_6"), { OFFSET_SCALE_LEFT, 1094.0f, 4 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_6/center"), { EXTEND_LEFT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_6/center/right"), { STORE_RIGHT_CORNER, 0, 4 } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_6/center/h_light"), { EXTEND_LEFT } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_6/center/left"), { SKIP } },
    { HashStr("ui_result_ex/main/number/result_num_1/position_6/center/left/h_light"), { SKIP } },
    { HashStr("ui_result_ex/newRecode/result_newR/position/newR_brilliance1"), { OFFSET_SCALE_RIGHT | STORE_LEFT_CORNER, 458.4f } },
    { HashStr("ui_result_ex/newRecode/result_newR/position/newR_brilliance2"), { OFFSET_SCALE_RIGHT | STORE_LEFT_CORNER, 458.4f } },
    { HashStr("ui_result_ex/newRecode/result_newR/position/newR_brilliance3"), { OFFSET_SCALE_RIGHT | STORE_LEFT_CORNER, 458.4f } },

    // ui_shop
    { HashStr("ui_shop/footer/shop_footer"), { ALIGN_BOTTOM } },

    // ui_start
    { HashStr("ui_start/Clear/position/bg/bg_1"), { STRETCH } },
    { HashStr("ui_start/Clear/position/bg/bg_2"), { STRETCH } },
    { HashStr("ui_start/Start/img/bg/bg_1"), { STRETCH } },
    { HashStr("ui_start/Start/img/bg/bg_2"), { STRETCH } },

    // ui_status
    { HashStr("ui_status/footer/status_footer"), { ALIGN_BOTTOM } },
    { HashStr("ui_status/header/status_title"), { ALIGN_TOP | OFFSET_SCALE_LEFT, 617.0f } },
    { HashStr("ui_status/header/status_title/title_bg/center"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_status/header/status_title/title_bg/center/h_light"), { ALIGN_TOP | EXTEND_LEFT } },
    { HashStr("ui_status/header/status_title/title_bg/right"), { ALIGN_TOP | STORE_RIGHT_CORNER } },
    { HashStr("ui_status/logo/logo/bg_position/c_1"), { STRETCH_HORIZONTAL } },
    { HashStr("ui_status/logo/logo/bg_position/c_2"), { STRETCH_HORIZONTAL } },
    { HashStr("ui_status/main/arrow_effect/a_efc_1"), { OFFSET_SCALE_LEFT, 866.0f } },
    { HashStr("ui_status/main/arrow_effect/a_efc_1/position/img_01"), { STORE_RIGHT_CORNER } },
    { HashStr("ui_status/main/progless/bg/prgs_bg_1"), { OFFSET_SCALE_LEFT, 714.0f } },
    { HashStr("ui_status/main/progless/bg/prgs_bg_1/position/center/right"), { STORE_RIGHT_CORNER } },
    { HashStr("ui_status/main/progless/prgs/prgs_bar_1"), { OFFSET_SCALE_LEFT, 586.0f } },
    { HashStr("ui_status/main/progless/prgs/prgs_bar_1/position/bg/center/right"), { STORE_RIGHT_CORNER } },
    { HashStr("ui_status/main/tag/bg/tag_bg_1"), { OFFSET_SCALE_LEFT, 413.0f } },
    { HashStr("ui_status/main/tag/bg/tag_bg_1/total_1_bg/center"), { EXTEND_LEFT } },
    { HashStr("ui_status/main/tag/bg/tag_bg_1/total_1_bg/center/h_light"), { EXTEND_LEFT } },
    { HashStr("ui_status/main/tag/bg/tag_bg_1/total_1_bg/center/right"), { STORE_RIGHT_CORNER } },
    { HashStr("ui_status/main/tag/bg/tag_bg_1/total_1_bg/center/left"), { SKIP } },
    { HashStr("ui_status/main/tag/bg/tag_bg_1/total_1_bg/center/left/h_light"), { SKIP } },
    { HashStr("ui_status/main/tag/txt/tag_txt_1"), { OFFSET_SCALE_LEFT, 352.0f } },
    { HashStr("ui_status/main/tag/txt/tag_txt_1/position/img"), { STORE_RIGHT_CORNER } },
    { HashStr("ui_status/window/bg"), { STRETCH } },

    // ui_title
    { HashStr("ui_title/bg/bg"), { STRETCH } },
    { HashStr("ui_title/bg/headr"), { ALIGN_TOP | STRETCH_HORIZONTAL } },
    { HashStr("ui_title/bg/footer"), { ALIGN_BOTTOM | STRETCH_HORIZONTAL } },
    { HashStr("ui_title/bg/position"), { ALIGN_BOTTOM } },

    // ui_townscreen
    { HashStr("ui_townscreen/time"), { ALIGN_TOP_RIGHT | SCALE } },
    { HashStr("ui_townscreen/time_effect"), { ALIGN_TOP_RIGHT | SCALE } },
    { HashStr("ui_townscreen/info"), { ALIGN_TOP_LEFT | SCALE } },
    { HashStr("ui_townscreen/cam"), { ALIGN_TOP_RIGHT | SCALE } },
    { HashStr("ui_townscreen/footer"), { ALIGN_BOTTOM_RIGHT | SCALE } },

    // ui_worldmap
    { HashStr("ui_worldmap/contents/choices/cts_choices_bg"), { STRETCH } },
    { HashStr("ui_worldmap/contents/info/bg/cts_info_bg"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_01/row_01/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_01/row_02/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_01/row_03/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_01/row_04/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },  
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_02/row_01/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_02/row_02/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_02/row_03/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_02/row_04/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } }, 
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_03/row_01/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_03/row_02/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_03/row_03/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_03/row_04/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } }, 
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_04/row_01/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_04/row_02/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_04/row_03/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/bg/info_bg_1/position_04/row_04/img_12"), { ALIGN_TOP_LEFT | WORLD_MAP | REPEAT_LEFT } },
    { HashStr("ui_worldmap/contents/info/img/info_img_1"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/contents/info/img/info_img_2"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/contents/info/img/info_img_3"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/contents/info/img/info_img_4"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/footer/worldmap_footer_bg"), { ALIGN_BOTTOM } },
    { HashStr("ui_worldmap/footer/worldmap_footer_img_A"), { ALIGN_BOTTOM } },
    { HashStr("ui_worldmap/header/worldmap_header_bg"), { ALIGN_TOP } },
    { HashStr("ui_worldmap/header/worldmap_header_img"), { ALIGN_TOP_LEFT | WORLD_MAP } },
    { HashStr("ui_worldmap/header/worldmap_header_img/head_icon"), { SKIP } },

    // ui_worldmap_help
    { HashStr("ui_worldmap_help/balloon/help_window/position/msg_bg_l"), { EXTEND_LEFT } },
    { HashStr("ui_worldmap_help/balloon/help_window/position/msg_bg_r"), { EXTEND_RIGHT } },
};

static std::optional<CsdModifier> FindModifier(uint32_t data)
{
    XXH64_hash_t path;
    {
        std::lock_guard lock(g_pathMutex);

        auto findResult = g_paths.find(g_memory.Translate(data));
        if (findResult == g_paths.end())
            return {};

        path = findResult->second;
    }

    auto findResult = g_modifiers.find(path);
    if (findResult != g_modifiers.end())
        return findResult->second;

    return {};
}

static std::optional<CsdModifier> g_sceneModifier;
static float g_corners[8];
static bool g_cornerExtract;

//#define CORNER_DEBUG

// Explicit translations don't get affected by gameplay UI downscaling.
// This is required for the medal info in pause menu.
static float g_scenePositionX;
static float g_scenePositionY;

// Chao::CSD::CScene::Render
PPC_FUNC_IMPL(__imp__sub_830BC640);
PPC_FUNC(sub_830BC640)
{
    g_scenePositionX = 0.0f;
    g_scenePositionY = 0.0f;

    uint32_t motionPattern = PPC_LOAD_U32(ctx.r3.u32 + 0x98);
    if (motionPattern != NULL)
    {
        uint32_t member = PPC_LOAD_U32(motionPattern + 0xC);
        if (member != NULL)
        {
            uint32_t x = PPC_LOAD_U32(member + 0x2C);
            uint32_t y = PPC_LOAD_U32(member + 0x30);

            g_scenePositionX = 1280.0f * reinterpret_cast<float&>(x);
            g_scenePositionY = 720.0f * reinterpret_cast<float&>(y);
        }
    }

    __imp__sub_830BC640(ctx, base);
}

// Chao::CSD::Scene::Render
PPC_FUNC_IMPL(__imp__sub_830C6A00);
PPC_FUNC(sub_830C6A00)
{
    g_sceneModifier = FindModifier(ctx.r3.u32);

    if (g_sceneModifier.has_value())
    {
        if (!Config::ControlTutorial && (g_sceneModifier->flags & CONTROL_TUTORIAL) != 0)
        {
            return;
        }

        // Tornado Defense bugs out when applying gameplay UI scaling.
        // This seems consistent with base game behavior, because the UI
        // is normally squashed, which was probably done to work around this.
        if ((g_sceneModifier->flags & TORNADO_DEFENSE) != 0)
        {
            g_scenePositionX = 0.0f;
            g_scenePositionY = 0.0f;
        }

        if (g_aspectRatio > WIDE_ASPECT_RATIO && (g_sceneModifier->flags & (OFFSET_SCALE_LEFT | OFFSET_SCALE_RIGHT | CORNER_EXTRACT)) != 0)
        {
            auto r3 = ctx.r3;
            auto r4 = ctx.r4;
            auto r5 = ctx.r5;
            auto r6 = ctx.r6;

            // Queue draw calls, but don't actually draw anything. We just want to extract the corner.
            g_cornerExtract = true;
            __imp__sub_830C6A00(ctx, base);
            g_cornerExtract = false;

#ifdef CORNER_DEBUG
            if (g_sceneModifier->cornerMax == FLT_MAX)
            {
                fmt::print("Corners: ");
                for (auto corner : g_corners)
                    fmt::print("{} ", corner);

                fmt::println("");
            }
#endif

            ctx.r3 = r3;
            ctx.r4 = r4;
            ctx.r5 = r5;
            ctx.r6 = r6;
        }
    }

    __imp__sub_830C6A00(ctx, base);
}

static std::optional<CsdModifier> g_castNodeModifier;

void RenderCsdCastNodeMidAsmHook(PPCRegister& r10, PPCRegister& r27)
{
    g_castNodeModifier = FindModifier(r10.u32 + r27.u32);
}

static std::optional<CsdModifier> g_castModifier;

void RenderCsdCastMidAsmHook(PPCRegister& r4)
{
    g_castModifier = FindModifier(r4.u32);
}

static void Draw(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t stride)
{
    CsdModifier modifier{};

    if (g_castModifier.has_value())
    {
        modifier = g_castModifier.value();
    }
    else if (g_castNodeModifier.has_value())
    {
        modifier = g_castNodeModifier.value();
    }
    else if (g_sceneModifier.has_value())
    {
        modifier = g_sceneModifier.value();
    }

    if ((modifier.flags & SKIP) != 0)
    {
        return;
    }

    // That goddamn boss gauge doesn't disappear in the cutscene where Dark Gaia and Chip hug each other
    if ((modifier.flags & SKIP_INSPIRE) != 0 && !InspirePatches::s_sceneName.empty() && *reinterpret_cast<be<float>*>(base + ctx.r4.u32) >= 1280.0f)
    {
        return;
    }

    if (g_cornerExtract)
    {
        if ((modifier.flags & (STORE_LEFT_CORNER | STORE_RIGHT_CORNER)) != 0)
        {
            uint32_t vertexIndex = ((modifier.flags & STORE_LEFT_CORNER) != 0) ? 0 : 3;
            g_corners[modifier.cornerIndex] = *reinterpret_cast<be<float>*>(base + ctx.r4.u32 + vertexIndex * stride);
        }

        return;
    }

    if (Config::UIAlignmentMode == EUIAlignmentMode::Centre)
    {
        if (g_aspectRatio >= WIDE_ASPECT_RATIO)
            modifier.flags &= ~(ALIGN_LEFT | ALIGN_RIGHT);
    }

    // Tornado Defense UI is squashed at 4:3, presumably to work around an explicit translation issue.
    bool squash = Config::AspectRatio == EAspectRatio::OriginalNarrow && (modifier.flags & TORNADO_DEFENSE) != 0;

    uint32_t size = ctx.r5.u32 * stride;
    ctx.r1.u32 -= size;

    uint8_t* stack = base + ctx.r1.u32;
    memcpy(stack, base + ctx.r4.u32, size);

    auto getPosition = [&](size_t index)
        {
            return reinterpret_cast<be<float>*>(stack + index * stride);
        };

    // Subtract half a pixel from loading arrows to prevent transparent pixels surrounding them from leaking into the texture filtering.
    if (Video::s_viewportHeight > 720 && (modifier.flags & LOADING_ARROW) != 0 && stride == 0x14)
    {
        for (size_t i = 0; i < 4; i++)
        {
            auto texCoord = getPosition(i) + 4;

            constexpr float OFFSET = 0.5f / 720.0f;

            if (i == 0 || i == 2) // Top
                *texCoord = (*texCoord + OFFSET);
            else // Bottom
                *texCoord = (*texCoord - OFFSET);
        }
    }

    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    bool needsStretch = g_aspectRatio >= WIDE_ASPECT_RATIO;

    if (squash || (needsStretch && (modifier.flags & STRETCH_HORIZONTAL) != 0))
    {
        scaleX = Video::s_viewportWidth / 1280.0f;
    }
    else
    {
        scaleX = g_aspectRatioScale;

        if (needsStretch && (modifier.flags & UNSTRETCH_HORIZONTAL) != 0)
        {
            pivotX = *getPosition(0);
            offsetX = pivotX * Video::s_viewportWidth / 1280.0f;
        }
        else
        {
            if ((modifier.flags & ALIGN_RIGHT) != 0)
                offsetX = g_aspectRatioOffsetX * 2.0f;
            else if ((modifier.flags & ALIGN_LEFT) == 0)
                offsetX = g_aspectRatioOffsetX;

            if ((modifier.flags & SCALE) != 0)
            {
                scaleX *= g_aspectRatioGameplayScale;
                pivotX = g_scenePositionX;

                if ((modifier.flags & ALIGN_RIGHT) != 0)
                    offsetX += 1280.0f * (1.0f - g_aspectRatioGameplayScale) * g_aspectRatioScale;
                else if ((modifier.flags & ALIGN_LEFT) == 0)
                    offsetX += 640.0f * (1.0f - g_aspectRatioGameplayScale) * g_aspectRatioScale;

                offsetX += pivotX * g_aspectRatioScale;
            }

            if ((modifier.flags & WORLD_MAP) != 0)
            {
                if ((modifier.flags & ALIGN_LEFT) != 0)
                    offsetX += (1.0f - g_aspectRatioNarrowScale) * g_aspectRatioScale * -20.0f;
            }
        }
    }

    if (squash || ((modifier.flags & STRETCH_VERTICAL) != 0))
    {
        scaleY = Video::s_viewportHeight / 720.0f;
    }
    else
    {
        scaleY = g_aspectRatioScale;

        if ((modifier.flags & ALIGN_BOTTOM) != 0)
            offsetY = g_aspectRatioOffsetY * 2.0f;
        else if ((modifier.flags & ALIGN_TOP) == 0)
            offsetY = g_aspectRatioOffsetY;

        if ((modifier.flags & SCALE) != 0)
        {
            scaleY *= g_aspectRatioGameplayScale;
            pivotY = g_scenePositionY;

            if ((modifier.flags & ALIGN_BOTTOM) != 0)
                offsetY += 720.0f * (1.0f - g_aspectRatioGameplayScale) * g_aspectRatioScale;
            else if ((modifier.flags & ALIGN_TOP) == 0)
                offsetY += 360.0f * (1.0f - g_aspectRatioGameplayScale) * g_aspectRatioScale;

            offsetY += pivotY * g_aspectRatioScale;
        }
    }

    if (g_aspectRatio > WIDE_ASPECT_RATIO)
    {
        CsdModifier offsetScaleModifier{};
        float corner = 0.0f;

        if (g_castModifier.has_value())
        {
            offsetScaleModifier = g_castModifier.value();

            uint32_t vertexIndex = ((offsetScaleModifier.flags & STORE_LEFT_CORNER) != 0) ? 0 : 3;
            corner = *getPosition(vertexIndex);
        }

        if (offsetScaleModifier.cornerMax == 0.0f && g_castNodeModifier.has_value())
        {
            offsetScaleModifier = g_castNodeModifier.value();
            corner = g_corners[offsetScaleModifier.cornerIndex];
        }

        if (offsetScaleModifier.cornerMax == 0.0f && g_sceneModifier.has_value())
        {
            offsetScaleModifier = g_sceneModifier.value();
            corner = g_corners[offsetScaleModifier.cornerIndex];
        }

#ifdef CORNER_DEBUG
        if ((offsetScaleModifier.flags & (OFFSET_SCALE_LEFT | OFFSET_SCALE_RIGHT)) != 0 && offsetScaleModifier.cornerMax == FLT_MAX)
            fmt::println("Corner: {}", corner);
#endif

        if ((offsetScaleModifier.flags & OFFSET_SCALE_LEFT) != 0)
            offsetX *= corner / offsetScaleModifier.cornerMax;
        else if ((offsetScaleModifier.flags & OFFSET_SCALE_RIGHT) != 0)
            offsetX = Video::s_viewportWidth - (Video::s_viewportWidth - offsetX) * (1280.0f - corner) / (1280.0f - offsetScaleModifier.cornerMax);
    }

    for (size_t i = 0; i < ctx.r5.u32; i++)
    {
        auto position = getPosition(i);

        float x = offsetX + (position[0] - pivotX) * scaleX;
        float y = offsetY + (position[1] - pivotY) * scaleY;

        if ((modifier.flags & EXTEND_LEFT) != 0 && (i == 0 || i == 1))
        {
            x = std::min(x, 0.0f);
        }
        else if ((modifier.flags & EXTEND_RIGHT) != 0 && (i == 2 || i == 3))
        {
            x = std::max(x, float(Video::s_viewportWidth));
        }

        position[0] = round(x);
        position[1] = round(y);
    }

    if ((modifier.flags & LOADING_BLACK_BAR_MIN) != 0)
    {
        auto position = getPosition(0);
        BlackBar::g_loadingBlackBarMin = ImVec2{ position[0], position[1] };
        BlackBar::g_loadingBlackBarAlpha = *(base + ctx.r1.u32 + 0xB);
    }
    else if ((modifier.flags & LOADING_BLACK_BAR_MAX) != 0)
    {
        auto position = getPosition(3);
        BlackBar::g_loadingBlackBarMax = ImVec2{ position[0], position[1] };
    }

    if ((modifier.flags & REPEAT_LEFT) != 0)
    {
        float width = *getPosition(2) - *getPosition(0);

        auto r3 = ctx.r3;
        auto r5 = ctx.r5;
        auto r6 = ctx.r6;
        auto r7 = ctx.r7;
        auto r8 = ctx.r8;

        while (*getPosition(2) > 0.0f)
        {
            ctx.r3 = r3;
            ctx.r4 = ctx.r1;
            ctx.r5 = r5;
            ctx.r6 = r6;
            ctx.r7 = r7;
            ctx.r8 = r8;
            original(ctx, base);

            for (size_t i = 0; i < ctx.r5.u32; i++)
                *getPosition(i) = *getPosition(i) - width;
        }

        ctx.r1.u32 += size;
    }
    else
    {
        ctx.r4.u32 = ctx.r1.u32;
        original(ctx, base);
        ctx.r1.u32 += size;
    }
}

// SWA::CCsdPlatformMirage::Draw
PPC_FUNC_IMPL(__imp__sub_825E2E70);
PPC_FUNC(sub_825E2E70)
{
    Draw(ctx, base, __imp__sub_825E2E70, 0x14);
}

// SWA::CCsdPlatformMirage::DrawNoTex
PPC_FUNC_IMPL(__imp__sub_825E2E88);
PPC_FUNC(sub_825E2E88)
{
    Draw(ctx, base, __imp__sub_825E2E88, 0xC);
}

// Hedgehog::MirageDebug::SetScissorRect
PPC_FUNC_IMPL(__imp__sub_82E16C70);
PPC_FUNC(sub_82E16C70)
{
    auto scissorRect = reinterpret_cast<GuestRect*>(base + ctx.r4.u32);

    scissorRect->left = scissorRect->left * g_aspectRatioScale + g_aspectRatioOffsetX;
    scissorRect->top = scissorRect->top * g_aspectRatioScale + g_aspectRatioOffsetY;
    scissorRect->right = scissorRect->right * g_aspectRatioScale + g_aspectRatioOffsetX;
    scissorRect->bottom = scissorRect->bottom * g_aspectRatioScale + g_aspectRatioOffsetY;

    __imp__sub_82E16C70(ctx, base);
}

// Store whether the primitive should be stretched in available padding space.
static constexpr size_t PRIMITIVE_2D_PADDING_OFFSET = 0x29;
static constexpr size_t PRIMITIVE_2D_PADDING_SIZE = 0x3;

// Hedgehog::MirageDebug::CPrimitive2D::CPrimitive2D
PPC_FUNC_IMPL(__imp__sub_822D0328);
PPC_FUNC(sub_822D0328)
{
    memset(base + ctx.r3.u32 + PRIMITIVE_2D_PADDING_OFFSET, 0, PRIMITIVE_2D_PADDING_SIZE);
    __imp__sub_822D0328(ctx, base);
}

// Hedgehog::MirageDebug::CPrimitive2D::CPrimitive2D(const Hedgehog::MirageDebug::CPrimitive2D&)
PPC_FUNC_IMPL(__imp__sub_830D2328);
PPC_FUNC(sub_830D2328)
{
    memcpy(base + ctx.r3.u32 + PRIMITIVE_2D_PADDING_OFFSET, base + ctx.r4.u32 + PRIMITIVE_2D_PADDING_OFFSET, PRIMITIVE_2D_PADDING_SIZE);
    __imp__sub_830D2328(ctx, base);
}

void AddPrimitive2DMidAsmHook(PPCRegister& r3)
{
    *(g_memory.base + r3.u32 + PRIMITIVE_2D_PADDING_OFFSET) = 0x01;
}

// Hedgehog::MirageDebug::CPrimitive2D::Draw
PPC_FUNC_IMPL(__imp__sub_830D1EF0);
PPC_FUNC(sub_830D1EF0)
{
    auto r3 = ctx.r3;

    __imp__sub_830D1EF0(ctx, base);

    struct Vertex
    {
        be<float> x;
        be<float> y;
        be<float> z;
        be<float> w;
        be<uint32_t> color;
        be<float> u;
        be<float> v;
    };

    auto vertex = reinterpret_cast<Vertex*>(base + ctx.r4.u32);

    for (size_t i = 0; i < 4; i++)
    {
        float x = vertex[i].x * 640.0f + 640.0f;
        float y = vertex[i].y * -360.0f + 360.0f;

        if (PPC_LOAD_U8(r3.u32 + PRIMITIVE_2D_PADDING_OFFSET))
        {
            // Stretch
            x = ((x + 0.5f) / 1280.0f) * Video::s_viewportWidth;
            y = ((y + 0.5f) / 720.0f) * Video::s_viewportHeight;
        }
        else
        {
            // Center
            x = g_aspectRatioOffsetX + (x + 0.5f) * g_aspectRatioScale;
            y = g_aspectRatioOffsetY + (y + 0.5f) * g_aspectRatioScale;
        }

        vertex[i].x = ((x - 0.5f) / Video::s_viewportWidth) * 2.0f - 1.0f;
        vertex[i].y = ((y - 0.5f) / Video::s_viewportHeight) * -2.0f + 1.0f;
    }

    bool letterboxTop = PPC_LOAD_U8(r3.u32 + PRIMITIVE_2D_PADDING_OFFSET + 0x1);
    bool letterboxBottom = PPC_LOAD_U8(r3.u32 + PRIMITIVE_2D_PADDING_OFFSET + 0x2);

    if (letterboxTop || letterboxBottom)
    {
        float halfPixelX = 1.0f / Video::s_viewportWidth;
        float halfPixelY = 1.0f / Video::s_viewportHeight;

        if (letterboxTop)
        {
            vertex[0].x = -1.0f - halfPixelX;
            vertex[0].y = 1.0f + halfPixelY;

            vertex[1].x = 1.0f - halfPixelX;
            vertex[1].y = 1.0f + halfPixelY;

            vertex[2].x = -1.0f - halfPixelX;
            // vertex[2].y untouched

            vertex[3].x = 1.0f - halfPixelX;
            // vertex[3].y untouched
        }
        else if (letterboxBottom)
        {
            vertex[0].x = -1.0f - halfPixelX;
            // vertex[0].y untouched

            vertex[1].x = 1.0f - halfPixelX;
            // vertex[1].y untouched

            vertex[2].x = -1.0f - halfPixelX;
            vertex[2].y = -1.0f + halfPixelY;

            vertex[3].x = 1.0f - halfPixelX;
            vertex[3].y = -1.0f + halfPixelY;
        }
    }
}

// Objects are pushed forward by 1m, so the coordinates need to compensate for it.
static const double OBJ_GET_ITEM_TANGENT = tan(M_PI / 8.0);

// Coordinates are in [-1, 1] range. Automatically fit and centered.
// The tangent is calculated incorrectly in game, causing distortion.
// The hook makes them move to the correct position regardless of FOV.
static float ComputeObjGetItemTangent(float fieldOfView, float aspectRatio)
{
    return tan(AdjustFieldOfView(fieldOfView, aspectRatio) / 2.0) / OBJ_GET_ITEM_TANGENT;
}

void ObjGetItemFieldOfViewMidAsmHook(PPCRegister& r1, PPCRegister& f1)
{
    if (Config::AspectRatio != EAspectRatio::OriginalNarrow)
        *reinterpret_cast<be<float>*>(g_memory.base + r1.u32 + 0x58) = ComputeObjGetItemTangent(f1.f64, g_aspectRatio);
}

static double ComputeObjGetItemX(uint32_t type)
{
    if (type >= 47) // Ring, Moon Medal, Sun Medal
    {
        double x;

        if (type == 47) // Ring
            x = 142.0;
        else // Medal
            x = 1058.0;

        x *= g_aspectRatioScale;
        x *= g_aspectRatioGameplayScale;

        double scaleOffset = (1280.0 * (1.0 - g_aspectRatioGameplayScale)) * g_aspectRatioScale;

        if (Config::UIAlignmentMode == EUIAlignmentMode::Edge)
        {
            if (type != 47) // Medal
                x += g_aspectRatioOffsetX * 2.0 + scaleOffset;
        }
        else if (Config::UIAlignmentMode == EUIAlignmentMode::Centre)
        {
            x += g_aspectRatioOffsetX + scaleOffset;
        }

        return (x - (0.5 * Video::s_viewportWidth)) / (0.5 * Video::s_viewportHeight) * OBJ_GET_ITEM_TANGENT;
    }

    return 0.0;
}

 // SWA::CObjGetItem::GetX
PPC_FUNC_IMPL(__imp__sub_82690660);
PPC_FUNC(sub_82690660)
{
    uint32_t type = PPC_LOAD_U32(ctx.r3.u32 + 0xE8);

    if (Config::AspectRatio == EAspectRatio::OriginalNarrow)
    {
        __imp__sub_82690660(ctx, base);
        return;
    }

    ctx.f1.f64 = ComputeObjGetItemX(type);
}

static double ComputeObjGetItemY(uint32_t type)
{
    if (type >= 47) // Ring, Moon Medal, Sun Medal
    {
        double y;

        if (type == 47) // Ring
            y = 642.0;
        else if (type == 48) // Moon Medal
            y = 632.0;
        else if (type == 49) // Sun Medal
            y = 582.0;

        y *= g_aspectRatioScale;
        y *= g_aspectRatioGameplayScale;
        y += g_aspectRatioOffsetY * 2.0 + 720.0 * (1.0 - g_aspectRatioGameplayScale) * g_aspectRatioScale;

        return ((0.5 * Video::s_viewportHeight) - y) / (0.5 * Video::s_viewportHeight) * OBJ_GET_ITEM_TANGENT;
    }

    return 0.25;
}

// SWA::CObjGetItem::GetY
PPC_FUNC_IMPL(__imp__sub_826906A8);
PPC_FUNC(sub_826906A8)
{
    if (Config::AspectRatio == EAspectRatio::OriginalNarrow)
    {
        __imp__sub_826906A8(ctx, base);
        // Game scales Y by 1.4 at 4:3 aspect ratio.
        ctx.f1.f64 *= 1.4;
        return;
    }

    uint32_t type = PPC_LOAD_U32(ctx.r3.u32 + 0xE8);
    ctx.f1.f64 = ComputeObjGetItemY(type);
}

void WorldMapProjectionMidAsmHook(PPCVRegister& v63, PPCVRegister& v62)
{
    // The world map icons are actually broken at 4:3 in the original game!!!
    if (Config::AspectRatio != EAspectRatio::OriginalNarrow)
    {
        v63.f32[3] *= std::max(NARROW_ASPECT_RATIO, g_aspectRatio) / WIDE_ASPECT_RATIO;
        v62.f32[2] *= NARROW_ASPECT_RATIO / std::min(NARROW_ASPECT_RATIO, g_aspectRatio);
    }
}

// CViewRing has the same exact incorrect math as CObjGetItem.
void ViewRingFieldOfViewMidAsmHook(PPCRegister& r1, PPCRegister& f1)
{
    if (Config::AspectRatio != EAspectRatio::OriginalNarrow)
        *reinterpret_cast<be<float>*>(g_memory.base + r1.u32 + 0x54) = ComputeObjGetItemTangent(f1.f64, g_aspectRatio);
}

void ViewRingYMidAsmHook(PPCRegister& f1)
{
    if (Config::AspectRatio != EAspectRatio::OriginalNarrow)
        f1.f64 = -ComputeObjGetItemY(47); // Ring
}

void ViewRingXMidAsmHook(PPCRegister& f1, PPCVRegister& v62)
{
    if (Config::AspectRatio == EAspectRatio::OriginalNarrow)
    {
        // Game scales Y by 1.4 at 4:3 aspect ratio.
        for (size_t i = 0; i < 4; i++)
            v62.f32[i] *= 1.4f;
    }
    else
    {
        f1.f64 = -ComputeObjGetItemX(47); // Ring
    }
}

// SWA::Inspire::CLetterbox::CLetterbox
PPC_FUNC_IMPL(__imp__sub_82B8A8F8);
PPC_FUNC(sub_82B8A8F8)
{
    // Permanently store the letterbox bool instead of letting the game set it to false from widescreen check.
    bool letterbox = PPC_LOAD_U8(ctx.r4.u32);
    __imp__sub_82B8A8F8(ctx, base);
    PPC_STORE_U8(ctx.r3.u32, letterbox);
}

// SWA::Inspire::CLetterbox::Draw
PPC_FUNC_IMPL(__imp__sub_82B8AA40);
PPC_FUNC(sub_82B8AA40)
{
    bool letterbox = PPC_LOAD_U8(ctx.r3.u32);
    bool shouldDrawLetterbox = letterbox && Config::CutsceneAspectRatio != ECutsceneAspectRatio::Unlocked && g_aspectRatio < WIDE_ASPECT_RATIO;

    PPC_STORE_U8(ctx.r3.u32, shouldDrawLetterbox);
    if (shouldDrawLetterbox)
    {
        float aspectRatio = std::max(NARROW_ASPECT_RATIO, g_aspectRatio);
        uint32_t width = aspectRatio * 720;

        PPC_STORE_U32(ctx.r3.u32 + 0xC, width);
        PPC_STORE_U32(ctx.r3.u32 + 0x10, 720);
        PPC_STORE_U32(ctx.r3.u32 + 0x14, (720 - width * 9 / 16) / 2);
    }

    auto r3 = ctx.r3;
    __imp__sub_82B8AA40(ctx, base);

    // Restore the original letterbox value.
    PPC_STORE_U8(r3.u32, letterbox);

    if (letterbox)
    {
        // Would be nice to also push this as a 2D primitive but I really cannot be bothered right now...
        BlackBar::g_inspirePillarbox = Config::CutsceneAspectRatio != ECutsceneAspectRatio::Unlocked && g_aspectRatio > WIDE_ASPECT_RATIO;
    }
}

void InspireLetterboxTopMidAsmHook(PPCRegister& r3)
{
    *(g_memory.base + r3.u32 + PRIMITIVE_2D_PADDING_OFFSET + 0x1) = 0x01; // Letterbox Top
    *(g_memory.base + r3.u32 + PRIMITIVE_2D_PADDING_OFFSET + 0x2) = 0x00; // Letterbox Bottom
}

void InspireLetterboxBottomMidAsmHook(PPCRegister& r3)
{
    *(g_memory.base + r3.u32 + PRIMITIVE_2D_PADDING_OFFSET + 0x1) = 0x00; // Letterbox Top
    *(g_memory.base + r3.u32 + PRIMITIVE_2D_PADDING_OFFSET + 0x2) = 0x01; // Letterbox Bottom
}

void InspireSubtitleMidAsmHook(PPCRegister& r3)
{
    constexpr float NARROW_OFFSET = 485.0f;
    constexpr float WIDE_OFFSET = 560.0f;

    *reinterpret_cast<be<float>*>(g_memory.base + r3.u32 + 0x3C) = NARROW_OFFSET + (WIDE_OFFSET - NARROW_OFFSET) * g_aspectRatioNarrowScale;
}

enum class FadeTextureMode
{
    Unknown,
    Letterbox,
    SideCrop
};

static FadeTextureMode g_fadeTextureMode;

void FxFadePreRenderQuadMidAsmHook(PPCRegister& r31)
{
    g_fadeTextureMode = *(g_memory.base + r31.u32 + 0x44) ? FadeTextureMode::Letterbox : FadeTextureMode::SideCrop;
}

void FxFadePostRenderQuadMidAsmHook()
{
    g_fadeTextureMode = FadeTextureMode::Unknown;
}

void YggdrasillRenderQuadMidAsmHook(PPCRegister& r3, PPCRegister& r6)
{
    if (g_fadeTextureMode != FadeTextureMode::Unknown)
    {
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        
        // Fade textures are slightly squashed in the original game at 4:3.
        if (Config::AspectRatio == EAspectRatio::OriginalNarrow)
        {
            if (g_fadeTextureMode == FadeTextureMode::Letterbox)
                scaleY = NARROW_ASPECT_RATIO;
            else
                scaleX = 0.8f;
        }
        else
        {
            if (g_fadeTextureMode == FadeTextureMode::Letterbox && g_aspectRatio < WIDE_ASPECT_RATIO)
                scaleY = WIDE_ASPECT_RATIO / g_aspectRatio;
            else
                scaleX = g_aspectRatio / WIDE_ASPECT_RATIO;
        }

        struct Vertex
        {
            be<float> x;
            be<float> y;
            be<float> z;
            be<float> u;
            be<float> v;
        };

        auto vertex = reinterpret_cast<Vertex*>(g_memory.base + r6.u32);

        for (size_t i = 0; i < 6; i++)
        {
            vertex[i].u = (vertex[i].u - 0.5f) * scaleX + 0.5f;
            vertex[i].v = (vertex[i].v - 0.5f) * scaleY + 0.5f;
        }
    }
}

// Explicit CSD set position calls don't seem to care about the
// viewport size. This causes them to appear shifted by 1.5x,
// as the backbuffer resolution is 640x480 at 4:3. We need to account
// for this manually to make the positioning match with the original game.
static constexpr uint32_t EVIL_HUD_GUIDE_BYTE_SIZE = 0x154;

void EvilHudGuideAllocMidAsmHook(PPCRegister& r3)
{
    r3.u32 += sizeof(float);
}

// SWA::Player::CEvilHudGuide::CEvilHudGuide
PPC_FUNC_IMPL(__imp__sub_82448CF0);
PPC_FUNC(sub_82448CF0)
{
    *reinterpret_cast<float*>(base + ctx.r3.u32 + EVIL_HUD_GUIDE_BYTE_SIZE) = 0.0f;
    __imp__sub_82448CF0(ctx, base);
}

void EvilHudGuideUpdateMidAsmHook(PPCRegister& r30, PPCRegister& f30)
{
    *reinterpret_cast<float*>(g_memory.base + r30.u32 + EVIL_HUD_GUIDE_BYTE_SIZE) = f30.f64;
}

// SWA::Player::CEvilHudGuide::Update
PPC_FUNC_IMPL(__imp__sub_82449088);
PPC_FUNC(sub_82449088)
{
    auto r3 = ctx.r3;
    __imp__sub_82449088(ctx, base);

    float positionX = *reinterpret_cast<float*>(base + r3.u32 + EVIL_HUD_GUIDE_BYTE_SIZE);
    constexpr uint32_t OFFSETS[] = { 312, 320 };

    for (const auto offset : OFFSETS)
    {
        uint32_t scene = PPC_LOAD_U32(r3.u32 + offset + 0x4);
        if (scene != NULL)
        {
            scene = PPC_LOAD_U32(scene + 0x4);
            if (scene != NULL)
            {
                ctx.r3.u32 = scene;
                ctx.f1.f64 = (1.5 - 0.5 * g_aspectRatioNarrowScale) * positionX;
                ctx.f2.f64 = 0.0;

                sub_830BB3D0(ctx, base);
            }
        }
    }
}

// The shadow offseting is buggy for FCO text just like Werehog button guide,
// making them appear thicker than they actually are.
PPC_FUNC_IMPL(__imp__sub_82E54950);
PPC_FUNC(sub_82E54950)
{
    if (Config::AspectRatio == EAspectRatio::OriginalNarrow)
    {
        // Luckily, they have shadow offset scale values that are only used in this function.
        uint32_t x = PPC_LOAD_U32(0x8332B7B8);
        uint32_t y = PPC_LOAD_U32(0x8332B7BC);

        PPCRegister scaled;

        // X
        scaled.u32 = x;
        scaled.f32 *= 1.5f;
        PPC_STORE_U32(0x8332B7B8, scaled.u32);

        // Y
        scaled.u32 = y;
        scaled.f32 *= 1.5f;
        PPC_STORE_U32(0x8332B7BC, scaled.u32);

        __imp__sub_82E54950(ctx, base);

        // Restore old values.
        PPC_STORE_U32(0x8332B7B8, x);
        PPC_STORE_U32(0x8332B7BC, y);
    }
    else
    {
        __imp__sub_82E54950(ctx, base);
    }
}

// Credits while Sonic is running are offseted by 133 pixels at 16:9.
// We can make this dynamic by remembering the anchoring and shifting accordingly.
void EndingTextAllocMidAsmHook(PPCRegister& r3)
{
    r3.u32 += sizeof(uint32_t);
}

static constexpr uint32_t ENDING_TEXT_SIZE = 0x164;

void EndingTextCtorRightMidAsmHook(PPCRegister& r3)
{
    *reinterpret_cast<uint32_t*>(g_memory.base + r3.u32 + ENDING_TEXT_SIZE) = ALIGN_RIGHT;
}

void EndingTextCtorLeftMidAsmHook(PPCRegister& r3)
{
    *reinterpret_cast<uint32_t*>(g_memory.base + r3.u32 + ENDING_TEXT_SIZE) = ALIGN_LEFT;
}

void EndingTextCtorCenterMidAsmHook(PPCRegister& r3)
{
    *reinterpret_cast<uint32_t*>(g_memory.base + r3.u32 + ENDING_TEXT_SIZE) = ALIGN_CENTER;
}

void EndingTextPositionMidAsmHook(PPCRegister& r31, PPCRegister& f13)
{
    uint32_t align = *reinterpret_cast<uint32_t*>(g_memory.base + r31.u32 + ENDING_TEXT_SIZE);

    // Since widescreen is always forced, 133 offset will always be part of the position.
    if (align == ALIGN_RIGHT)
        f13.f64 += -133.0 * (1.0 - g_aspectRatioNarrowScale);

    else if (align == ALIGN_LEFT)
        f13.f64 += 133.0 * (1.0 - g_aspectRatioNarrowScale);
}

// AP TEST - Reception evenement GetItem
PPC_FUNC_IMPL(__imp__sub_8268B0D8);
PPC_FUNC(sub_8268B0D8)
{
    __imp__sub_8268B0D8(ctx, base);

}

// AP TEST - libcurl WebSocket support
static void AP_TestCurlWebSocket()
{
    const curl_version_info_data* info =
        curl_version_info(CURLVERSION_NOW);

    if (!info)
    {
        LOGFN("AP CURL: version info unavailable");
        return;
    }

    LOGFN("AP CURL VERSION: {}", info->version);

    bool ws = false;
    bool wss = false;

    if (info->protocols)
    {
        for (const char* const* protocol = info->protocols;
             *protocol != nullptr; ++protocol)
        {
            if (std::strcmp(*protocol, "ws") == 0)
                ws = true;

            if (std::strcmp(*protocol, "wss") == 0)
                wss = true;
        }
    }

    LOGFN("AP CURL WEBSOCKET: ws={} wss={}", ws, wss);
}

// AP TEST - Exact internal medal ID -> Archipelago location ID mapping.
// Matched exactly against sonic_unleashed_V082_medal_names_clean.apworld / internal_to_id.
// Indices 0..N-1 are still provisional for stages not individually verified.
static const std::unordered_map<std::string, int64_t> apMedalLocationIds = {
    {"Town_Mykonos_Moon_0", 88000003},
    {"Town_Mykonos_Moon_1", 88000004},
    {"Town_Mykonos_Night_Sun_0", 88000005},
    {"Town_Mykonos_Night_Sun_1", 88000006},
    {"Town_MykonosETF_Sun_0", 88000007},
    {"Town_MykonosETF_Sun_1", 88000008},
    {"Town_MykonosETF_Moon_0", 88000009},
    {"Town_MykonosETF_Moon_1", 88000010},
    {"Town_MykonosETF_Night_Sun_0", 88000007},
    {"Town_MykonosETF_Night_Sun_1", 88000008},
    {"Town_MykonosETF_Night_Moon_0", 88000009},
    {"Town_MykonosETF_Night_Moon_1", 88000010},
    {"ActD_MykonosAct2_Sun_0", 88000001},
    {"ActD_MykonosAct2_Sun_1", 88000015},
    {"ActD_MykonosAct2_Sun_2", 88000016},
    {"ActD_MykonosAct2_Moon_0", 88000017},
    {"ActD_MykonosAct2_Moon_1", 88000018},
    {"ActD_MykonosAct2_Moon_2", 88000019},
    {"ActD_MykonosAct2_Moon_3", 88000020},
    {"ActD_MykonosAct2_Moon_4", 88000021},
    {"ActD_MykonosAct2_Moon_5", 88000022},
    {"ActD_MykonosAct2_Moon_6", 88000023},
    {"ActD_SubMykonos_01_Sun_0", 88000024},
    {"ActD_SubMykonos_01_Moon_0", 88000025},
    {"ActD_SubMykonos_01_Moon_1", 88000026},
    {"ActN_MykonosEvil_Sun_0", 88000027},
    {"ActN_MykonosEvil_Sun_1", 88000028},
    {"ActN_MykonosEvil_Sun_2", 88000029},
    {"ActN_MykonosEvil_Sun_3", 88000030},
    {"ActN_MykonosEvil_Sun_4", 88000031},
    {"ActN_MykonosEvil_Sun_5", 88000032},
    {"ActN_MykonosEvil_Sun_6", 88000033},
    {"ActN_MykonosEvil_Sun_7", 88000034},
    {"ActN_MykonosEvil_Sun_8", 88000035},
    {"ActN_MykonosEvil_Sun_9", 88000036},
    {"ActN_MykonosEvil_Moon_0", 88000037},
    {"ActN_MykonosEvil_Moon_1", 88000038},
    {"ActN_MykonosEvil_Moon_2", 88000039},
    {"ActN_MykonosEvil_Moon_3", 88000040},
    {"ActN_MykonosEvil_Moon_4", 88000041},
    {"ActN_MykonosEvil_Moon_5", 88000002},
    {"ActN_Mission_Mykonos_Sun_0", 88000042},
    {"ActN_SubMykonos_01_Sun_0", 88000043},
    {"ActN_SubMykonos_01_Sun_1", 88000044},
    {"ActN_SubMykonos_01_Sun_2", 88000045},
    {"ActN_SubMykonos_01_Sun_3", 88000046},
    {"ActN_SubMykonos_01_Moon_0", 88000047},
    {"Town_EuropeanCity_Moon_0", 88000048},
    {"Town_EuropeanCity_Moon_1", 88000049},
    {"Town_EuropeanCity_Moon_2", 88000050},
    {"Town_EuropeanCity_Moon_3", 88000051},
    {"Town_EuropeanCity_Night_Sun_0", 88000052},
    {"Town_EuropeanCity_Night_Sun_1", 88000053},
    {"Town_EuropeanCity_Night_Sun_2", 88000054},
    {"Town_EuropeanCity_Night_Sun_3", 88000055},
    {"Town_EuropeanCityETF_Sun_0", 88000056},
    {"Town_EuropeanCityETF_Sun_1", 88000057},
    {"Town_EuropeanCityETF_Moon_0", 88000058},
    {"Town_EuropeanCityETF_Moon_1", 88000059},
    {"Town_EuropeanCityETF_Night_Sun_0", 88000056},
    {"Town_EuropeanCityETF_Night_Sun_1", 88000057},
    {"Town_EuropeanCityETF_Night_Moon_0", 88000058},
    {"Town_EuropeanCityETF_Night_Moon_1", 88000059},
    {"ActD_EU_Sun_0", 88000064},
    {"ActD_EU_Sun_1", 88000065},
    {"ActD_EU_Sun_2", 88000066},
    {"ActD_EU_Moon_0", 88000067},
    {"ActD_EU_Moon_1", 88000068},
    {"ActD_EU_Moon_2", 88000069},
    {"ActD_EU_Moon_3", 88000070},
    {"ActD_EU_Moon_4", 88000071},
    {"ActD_EU_Moon_5", 88000072},
    {"ActD_EU_Moon_6", 88000073},
    {"BossEggRayBird_Sun_0", 88000074},
    {"BossEggRayBird_Sun_1", 88000075},
    {"BossEggRayBird_Moon_0", 88000076},
    {"BossEggRayBird_Moon_1", 88000077},
    {"BossEggRayBird_Moon_2", 88000078},
    {"ActN_EUEvil_Sun_0", 88000079},
    {"ActN_EUEvil_Sun_1", 88000080},
    {"ActN_EUEvil_Sun_2", 88000081},
    {"ActN_EUEvil_Sun_3", 88000082},
    {"ActN_EUEvil_Sun_4", 88000083},
    {"ActN_EUEvil_Sun_5", 88000084},
    {"ActN_EUEvil_Sun_6", 88000085},
    {"ActN_EUEvil_Sun_7", 88000086},
    {"ActN_EUEvil_Sun_8", 88000087},
    {"ActN_EUEvil_Sun_9", 88000088},
    {"ActN_EUEvil_Moon_0", 88000089},
    {"ActN_EUEvil_Moon_1", 88000090},
    {"ActN_EUEvil_Moon_2", 88000091},
    {"ActN_EUEvil_Moon_3", 88000092},
    {"ActN_EUEvil_Moon_4", 88000093},
    {"ActN_EUEvil_Moon_5", 88000094},
    {"Town_Africa_Moon_0", 88000095},
    {"Town_Africa_Moon_1", 88000096},
    {"Town_Africa_Night_Sun_0", 88000097},
    {"Town_Africa_Night_Sun_1", 88000098},
    {"Town_AfricaETF_Sun_0", 88000099},
    {"Town_AfricaETF_Sun_1", 88000100},
    {"Town_AfricaETF_Moon_0", 88000101},
    {"Town_AfricaETF_Moon_1", 88000102},
    {"Town_AfricaETF_Night_Sun_0", 88000099},
    {"Town_AfricaETF_Night_Sun_1", 88000100},
    {"Town_AfricaETF_Night_Moon_0", 88000101},
    {"Town_AfricaETF_Night_Moon_1", 88000102},
    {"ActD_Africa_Sun_0", 88000107},
    {"ActD_Africa_Sun_1", 88000108},
    {"ActD_Africa_Sun_2", 88000109},
    {"ActD_Africa_Moon_0", 88000110},
    {"ActD_Africa_Moon_1", 88000111},
    {"ActD_Africa_Moon_2", 88000112},
    {"ActD_Africa_Moon_3", 88000113},
    {"ActD_Africa_Moon_4", 88000114},
    {"ActD_Africa_Moon_5", 88000115},
    {"ActD_Africa_Moon_6", 88000116},
    {"BossEggBeetle_Sun_0", 88000117},
    {"BossEggBeetle_Sun_1", 88000118},
    {"BossEggBeetle_Moon_0", 88000119},
    {"BossEggBeetle_Moon_1", 88000120},
    {"BossEggBeetle_Moon_2", 88000121},
    {"ActN_AfricaEvil_Sun_0", 88000122},
    {"ActN_AfricaEvil_Sun_1", 88000123},
    {"ActN_AfricaEvil_Sun_2", 88000124},
    {"ActN_AfricaEvil_Sun_3", 88000125},
    {"ActN_AfricaEvil_Sun_4", 88000126},
    {"ActN_AfricaEvil_Sun_5", 88000127},
    {"ActN_AfricaEvil_Sun_6", 88000128},
    {"ActN_AfricaEvil_Sun_7", 88000129},
    {"ActN_AfricaEvil_Sun_8", 88000130},
    {"ActN_AfricaEvil_Sun_9", 88000131},
    {"ActN_AfricaEvil_Moon_0", 88000132},
    {"ActN_AfricaEvil_Moon_1", 88000133},
    {"ActN_AfricaEvil_Moon_2", 88000134},
    {"ActN_AfricaEvil_Moon_3", 88000135},
    {"ActN_AfricaEvil_Moon_4", 88000136},
    {"ActN_AfricaEvil_Moon_5", 88000137},
    {"Town_China_Moon_0", 88000138},
    {"Town_China_Moon_1", 88000139},
    {"Town_China_Night_Sun_0", 88000140},
    {"Town_China_Night_Sun_1", 88000141},
    {"Town_ChinaETF_Sun_0", 88000142},
    {"Town_ChinaETF_Sun_1", 88000143},
    {"Town_ChinaETF_Moon_0", 88000144},
    {"Town_ChinaETF_Moon_1", 88000145},
    {"Town_ChinaETF_Night_Sun_0", 88000142},
    {"Town_ChinaETF_Night_Sun_1", 88000143},
    {"Town_ChinaETF_Night_Moon_0", 88000144},
    {"Town_ChinaETF_Night_Moon_1", 88000145},
    {"ActD_China_Sun_0", 88000150},
    {"ActD_China_Sun_1", 88000151},
    {"ActD_China_Sun_2", 88000152},
    {"ActD_China_Moon_0", 88000153},
    {"ActD_China_Moon_1", 88000154},
    {"ActD_China_Moon_2", 88000155},
    {"ActD_China_Moon_3", 88000156},
    {"ActD_China_Moon_4", 88000157},
    {"ActD_China_Moon_5", 88000158},
    {"ActD_China_Moon_6", 88000159},
    {"ActD_SubChina_03_Sun_0", 88000160},
    {"ActD_SubChina_03_Moon_0", 88000161},
    {"ActD_SubChina_03_Moon_1", 88000162},
    {"ActD_SubChina_04_Sun_0", 88000163},
    {"ActD_SubChina_04_Moon_0", 88000164},
    {"ActD_SubChina_04_Moon_1", 88000165},
    {"ActN_ChinaEvil_Sun_0", 88000166},
    {"ActN_ChinaEvil_Sun_1", 88000167},
    {"ActN_ChinaEvil_Sun_2", 88000168},
    {"ActN_ChinaEvil_Sun_3", 88000169},
    {"ActN_ChinaEvil_Sun_4", 88000170},
    {"ActN_ChinaEvil_Sun_5", 88000171},
    {"ActN_ChinaEvil_Sun_6", 88000172},
    {"ActN_ChinaEvil_Sun_7", 88000173},
    {"ActN_ChinaEvil_Sun_8", 88000174},
    {"ActN_ChinaEvil_Sun_9", 88000175},
    {"ActN_ChinaEvil_Moon_0", 88000176},
    {"ActN_ChinaEvil_Moon_1", 88000177},
    {"ActN_ChinaEvil_Moon_2", 88000178},
    {"ActN_ChinaEvil_Moon_3", 88000179},
    {"ActN_ChinaEvil_Moon_4", 88000180},
    {"ActN_ChinaEvil_Moon_5", 88000181},
    {"ActN_SubChina_01_Sun_0", 88000182},
    {"ActN_SubChina_01_Sun_1", 88000183},
    {"ActN_SubChina_01_Sun_2", 88000184},
    {"ActN_SubChina_01_Sun_3", 88000185},
    {"ActN_SubChina_01_Moon_0", 88000186},
    {"BossPhoenix_Sun_0", 88000187},
    {"BossPhoenix_Sun_1", 88000188},
    {"BossPhoenix_Sun_2", 88000189},
    {"BossPhoenix_Moon_0", 88000190},
    {"BossPhoenix_Moon_1", 88000191},
    {"Town_Snow_Moon_0", 88000192},
    {"Town_Snow_Moon_1", 88000193},
    {"Town_Snow_Night_Sun_0", 88000194},
    {"Town_Snow_Night_Sun_1", 88000195},
    {"Town_SnowETF_Sun_0", 88000196},
    {"Town_SnowETF_Sun_1", 88000197},
    {"Town_SnowETF_Moon_0", 88000198},
    {"Town_SnowETF_Moon_1", 88000199},
    {"Town_SnowETF_Night_Sun_0", 88000196},
    {"Town_SnowETF_Night_Sun_1", 88000197},
    {"Town_SnowETF_Night_Moon_0", 88000198},
    {"Town_SnowETF_Night_Moon_1", 88000199},
    {"ActD_Snow_Sun_0", 88000204},
    {"ActD_Snow_Sun_1", 88000205},
    {"ActD_Snow_Sun_2", 88000206},
    {"ActD_Snow_Moon_0", 88000207},
    {"ActD_Snow_Moon_1", 88000208},
    {"ActD_Snow_Moon_2", 88000209},
    {"ActD_Snow_Moon_3", 88000210},
    {"ActD_Snow_Moon_4", 88000211},
    {"ActD_Snow_Moon_5", 88000212},
    {"ActD_Snow_Moon_6", 88000213},
    {"ActD_SubSnow_01_Sun_0", 88000214},
    {"ActD_SubSnow_01_Moon_0", 88000215},
    {"ActD_SubSnow_01_Moon_1", 88000216},
    {"ActN_SnowEvil_Sun_0", 88000217},
    {"ActN_SnowEvil_Sun_1", 88000218},
    {"ActN_SnowEvil_Sun_2", 88000219},
    {"ActN_SnowEvil_Sun_3", 88000220},
    {"ActN_SnowEvil_Sun_4", 88000221},
    {"ActN_SnowEvil_Sun_5", 88000222},
    {"ActN_SnowEvil_Sun_6", 88000223},
    {"ActN_SnowEvil_Sun_7", 88000224},
    {"ActN_SnowEvil_Sun_8", 88000225},
    {"ActN_SnowEvil_Sun_9", 88000226},
    {"ActN_SnowEvil_Moon_0", 88000227},
    {"ActN_SnowEvil_Moon_1", 88000228},
    {"ActN_SnowEvil_Moon_2", 88000229},
    {"ActN_SnowEvil_Moon_3", 88000230},
    {"ActN_SnowEvil_Moon_4", 88000231},
    {"ActN_SnowEvil_Moon_5", 88000232},
    {"BossDarkGaiaMoray_Sun_0", 88000233},
    {"BossDarkGaiaMoray_Sun_1", 88000234},
    {"BossDarkGaiaMoray_Sun_2", 88000235},
    {"BossDarkGaiaMoray_Moon_0", 88000236},
    {"BossDarkGaiaMoray_Moon_1", 88000237},
    {"Town_PetraCapital_Moon_0", 88000238},
    {"Town_PetraCapital_Moon_1", 88000239},
    {"Town_PetraCapital_Moon_2", 88000240},
    {"Town_PetraCapital_Moon_3", 88000241},
    {"Town_PetraCapital_Night_Sun_0", 88000242},
    {"Town_PetraCapital_Night_Sun_1", 88000243},
    {"Town_PetraCapital_Night_Sun_2", 88000244},
    {"Town_PetraCapital_Night_Sun_3", 88000245},
    {"Town_PetraCapitalETF_Sun_0", 88000246},
    {"Town_PetraCapitalETF_Sun_1", 88000247},
    {"Town_PetraCapitalETF_Moon_0", 88000248},
    {"Town_PetraCapitalETF_Moon_1", 88000249},
    {"Town_PetraCapitalETF_Night_Sun_0", 88000246},
    {"Town_PetraCapitalETF_Night_Sun_1", 88000247},
    {"Town_PetraCapitalETF_Night_Moon_0", 88000248},
    {"Town_PetraCapitalETF_Night_Moon_1", 88000249},
    {"ActD_Petra_Sun_0", 88000254},
    {"ActD_Petra_Sun_1", 88000255},
    {"ActD_Petra_Sun_2", 88000256},
    {"ActD_Petra_Moon_0", 88000257},
    {"ActD_Petra_Moon_1", 88000258},
    {"ActD_Petra_Moon_2", 88000259},
    {"ActD_Petra_Moon_3", 88000260},
    {"ActD_Petra_Moon_4", 88000261},
    {"ActD_Petra_Moon_5", 88000262},
    {"ActD_Petra_Moon_6", 88000263},
    {"ActN_PetraEvil_Sun_0", 88000264},
    {"ActN_PetraEvil_Sun_1", 88000265},
    {"ActN_PetraEvil_Sun_2", 88000266},
    {"ActN_PetraEvil_Sun_3", 88000267},
    {"ActN_PetraEvil_Sun_4", 88000268},
    {"ActN_PetraEvil_Sun_5", 88000269},
    {"ActN_PetraEvil_Sun_6", 88000270},
    {"ActN_PetraEvil_Sun_7", 88000271},
    {"ActN_PetraEvil_Sun_8", 88000272},
    {"ActN_PetraEvil_Sun_9", 88000273},
    {"ActN_PetraEvil_Moon_0", 88000274},
    {"ActN_PetraEvil_Moon_1", 88000275},
    {"ActN_PetraEvil_Moon_2", 88000276},
    {"ActN_PetraEvil_Moon_3", 88000277},
    {"ActN_PetraEvil_Moon_4", 88000278},
    {"ActN_PetraEvil_Moon_5", 88000279},
    {"BossPetra_Sun_0", 88000280},
    {"BossPetra_Sun_1", 88000281},
    {"BossPetra_Sun_2", 88000282},
    {"BossPetra_Moon_0", 88000283},
    {"BossPetra_Moon_1", 88000284},
    {"Town_NYCity_Moon_0", 88000285},
    {"Town_NYCity_Moon_1", 88000286},
    {"Town_NYCity_Night_Sun_0", 88000287},
    {"Town_NYCity_Night_Sun_1", 88000288},
    {"Town_NYCityETF_Sun_0", 88000289},
    {"Town_NYCityETF_Sun_1", 88000290},
    {"Town_NYCityETF_Moon_0", 88000291},
    {"Town_NYCityETF_Moon_1", 88000292},
    {"Town_NYCityETF_Night_Sun_0", 88000289},
    {"Town_NYCityETF_Night_Sun_1", 88000290},
    {"Town_NYCityETF_Night_Moon_0", 88000291},
    {"Town_NYCityETF_Night_Moon_1", 88000292},
    {"ActD_NY_Sun_0", 88000297},
    {"ActD_NY_Sun_1", 88000298},
    {"ActD_NY_Sun_2", 88000299},
    {"ActD_NY_Moon_0", 88000300},
    {"ActD_NY_Moon_1", 88000301},
    {"ActD_NY_Moon_2", 88000302},
    {"ActD_NY_Moon_3", 88000303},
    {"ActD_NY_Moon_4", 88000304},
    {"ActD_NY_Moon_5", 88000305},
    {"ActD_NY_Moon_6", 88000306},
    {"ActN_NYEvil_Sun_0", 88000307},
    {"ActN_NYEvil_Sun_1", 88000308},
    {"ActN_NYEvil_Sun_2", 88000309},
    {"ActN_NYEvil_Sun_3", 88000310},
    {"ActN_NYEvil_Sun_4", 88000311},
    {"ActN_NYEvil_Sun_5", 88000312},
    {"ActN_NYEvil_Sun_6", 88000313},
    {"ActN_NYEvil_Sun_7", 88000314},
    {"ActN_NYEvil_Sun_8", 88000315},
    {"ActN_NYEvil_Sun_9", 88000316},
    {"ActN_NYEvil_Moon_0", 88000317},
    {"ActN_NYEvil_Moon_1", 88000318},
    {"ActN_NYEvil_Moon_2", 88000319},
    {"ActN_NYEvil_Moon_3", 88000320},
    {"ActN_NYEvil_Moon_4", 88000321},
    {"ActN_NYEvil_Moon_5", 88000322},
    {"Town_SouthEastAsia_Moon_0", 88000323},
    {"Town_SouthEastAsia_Moon_1", 88000324},
    {"Town_SouthEastAsia_Night_Sun_0", 88000325},
    {"Town_SouthEastAsia_Night_Sun_1", 88000326},
    {"Town_SouthEastAsiaETF_Sun_0", 88000327},
    {"Town_SouthEastAsiaETF_Sun_1", 88000328},
    {"Town_SouthEastAsiaETF_Moon_0", 88000329},
    {"Town_SouthEastAsiaETF_Moon_1", 88000330},
    {"Town_SouthEastAsiaETF_Night_Sun_0", 88000327},
    {"Town_SouthEastAsiaETF_Night_Sun_1", 88000328},
    {"Town_SouthEastAsiaETF_Night_Moon_0", 88000329},
    {"Town_SouthEastAsiaETF_Night_Moon_1", 88000330},
    {"ActD_Beach_Sun_0", 88000335},
    {"ActD_Beach_Sun_1", 88000336},
    {"ActD_Beach_Sun_2", 88000337},
    {"ActD_Beach_Moon_0", 88000338},
    {"ActD_Beach_Moon_1", 88000339},
    {"ActD_Beach_Moon_2", 88000340},
    {"ActD_Beach_Moon_3", 88000341},
    {"ActD_Beach_Moon_4", 88000342},
    {"ActD_Beach_Moon_5", 88000343},
    {"ActD_Beach_Moon_6", 88000344},
    {"BossEggLancer_Sun_0", 88000345},
    {"BossEggLancer_Sun_1", 88000346},
    {"BossEggLancer_Moon_0", 88000347},
    {"BossEggLancer_Moon_1", 88000348},
    {"BossEggLancer_Moon_2", 88000349},
    {"ActN_BeachEvil_Sun_0", 88000350},
    {"ActN_BeachEvil_Sun_1", 88000351},
    {"ActN_BeachEvil_Sun_2", 88000352},
    {"ActN_BeachEvil_Sun_3", 88000353},
    {"ActN_BeachEvil_Sun_4", 88000354},
    {"ActN_BeachEvil_Sun_5", 88000355},
    {"ActN_BeachEvil_Sun_6", 88000356},
    {"ActN_BeachEvil_Sun_7", 88000357},
    {"ActN_BeachEvil_Sun_8", 88000358},
    {"ActN_BeachEvil_Sun_9", 88000359},
    {"ActN_BeachEvil_Moon_0", 88000360},
    {"ActN_BeachEvil_Moon_1", 88000361},
    {"ActN_BeachEvil_Moon_2", 88000362},
    {"ActN_BeachEvil_Moon_3", 88000363},
    {"ActN_BeachEvil_Moon_4", 88000364},
    {"ActN_BeachEvil_Moon_5", 88000365},
    {"ActN_SubBeach_01_Sun_0", 88000366},
    {"ActN_SubBeach_01_Sun_1", 88000367},
    {"ActN_SubBeach_01_Sun_2", 88000368},
    {"ActN_SubBeach_01_Sun_3", 88000369},
    {"ActN_SubBeach_01_Moon_0", 88000370},
    // AP MEDALS VERIFIED: observed on Savannah Citadel Day Act 3.
    {"ActD_SubAfrica_03_Sun_0", 88000929},
    {"ActD_SubAfrica_03_Moon_0", 88000930},
    {"ActD_SubAfrica_03_Moon_1", 88000931},
    {"ActD_SubAfrica_01_Sun_0", 88000932},
    {"ActD_SubAfrica_01_Moon_0", 88000933},
    {"ActD_SubAfrica_01_Moon_1", 88000934},
};

// AP_ACT_DIAG_COMPAT: diagnostic only - source BEFORE Act Fix V1.
static void AP_ActDiagLog(const char* format, ...)
{
    FILE* file = std::fopen("AP_Acts_Diagnostic.txt", "a");
    if (!file) return;
    std::va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}
static std::string AP_ActDiagStage()
{
    if (auto* doc = SWA::CGameDocument::GetInstance())
        if (doc->m_pMember)
            return doc->m_pMember->m_StageName.c_str();
    return "";
}

// AP ACT CLEAR + RANK CHECKS (61-stage server test).
// All 370 medal locations are unchanged. Stage metadata is in ap_stage_registry.h.
static std::string apRankLastStage;
static std::chrono::steady_clock::time_point apRankStageSeenAt = std::chrono::steady_clock::now();
static std::unordered_map<uint32_t, uint32_t> apRankPreviouslyReadByObject;

void AP_RememberRankStage(const std::string& stage)
{
    static std::string lastLoggedStage;
    if (lastLoggedStage != stage)
    {
        lastLoggedStage = stage;
        size_t mappedMedals = 0;
        const std::string prefix = stage + "_";
        for (const auto& entry : apMedalLocationIds)
            if (entry.first.compare(0, prefix.size(), prefix) == 0) ++mappedMedals;
        const auto found = apRankActLocations.find(stage);
        AP_ActDiagLog("STAGE name='%s' rankRegistry=%d isDlc=%d mappedMedals=%zu",
            stage.c_str(), found != apRankActLocations.end(),
            found != apRankActLocations.end() && found->second.isDlc, mappedMedals);
    }
    // During transitions/results the game temporarily exposes an empty StageName.
    // Preserve the last valid Act for the results hook; the 45-second age guard
    // and the next nonempty stage prevent attribution to an unrelated Act.
    if (stage.empty())
        return;
    if (apRankActLocations.find(stage) == apRankActLocations.end())
    {
        // Never attribute a result to a previously played act.
        if (!apRankLastStage.empty())
        {
            LOGFN("AP RANK STAGE UNKNOWN: {} (clearing {})", stage, apRankLastStage);
            apRankLastStage.clear();
        }
        return;
    }

    apRankStageSeenAt = std::chrono::steady_clock::now();
    if (stage != apRankLastStage)
    {
        apRankLastStage = stage;
        LOGFN("AP RANK STAGE REMEMBERED: {}", stage);
    }
}

// The game returns 5 for an Act without a recorded rank.
PPC_FUNC_IMPL(__imp__sub_8251B7A8);
PPC_FUNC(sub_8251B7A8)
{
    const uint32_t object = ctx.r3.u32;
    __imp__sub_8251B7A8(ctx, base);

    if (object != 0 && ctx.r3.u32 <= 5)
        apRankPreviouslyReadByObject[object] = ctx.r3.u32;
}

// Invoked by the results logic when a new best rank is written.
PPC_FUNC_IMPL(__imp__sub_8251C618);
PPC_FUNC(sub_8251C618)
{
    const uint32_t object = ctx.r3.u32;
    const uint32_t newRank = ctx.r4.u32;
    AP_ActDiagLog("RANK_SET active='%s' cached='%s' value=%u object=0x%08X",
        AP_ActDiagStage().c_str(), apRankLastStage.c_str(), newRank, object);
    const auto oldEntry = apRankPreviouslyReadByObject.find(object);
    const bool hasOldRank = oldEntry != apRankPreviouslyReadByObject.end();
    const uint32_t oldRank = hasOldRank ? oldEntry->second : 999;

    // Never change the original game's rank update behavior.
    __imp__sub_8251C618(ctx, base);

    // The setter fires when the game commits a new best rank. A rank 0..4
    // confirms a successful result; rank 5 indicates no valid completion.
    // Do not depend on an earlier rank READ for the same save object.
    const std::string activeStage = AP_ActDiagStage();
    const bool recent = std::chrono::steady_clock::now() - apRankStageSeenAt <
        std::chrono::seconds(180);
    std::string stage;
    if (apRankActLocations.find(activeStage) != apRankActLocations.end())
        stage = activeStage;
    else if (activeStage.empty() && recent &&
             apRankActLocations.find(apRankLastStage) != apRankActLocations.end())
        stage = apRankLastStage;

    if (stage.empty() || newRank > 4)
    {
        AP_ActDiagLog("RANK_SET_SKIPPED active='%s' cached='%s' rank=%u recent=%d",
            activeStage.c_str(), apRankLastStage.c_str(), newRank, recent);
        return;
    }

    const auto& locations = apRankActLocations.at(stage);
    const uint32_t ids[] = {locations.actClear, locations.rankD,
                            locations.rankC, locations.rankB, locations.rankA};
    APClient::QueueLocationCheck(ids[0]);
    for (uint32_t t = 4, i = 1; i < 5; ++i, --t)
        if (newRank <= t) APClient::QueueLocationCheck(ids[i]);
    AP_ActDiagLog("RANK_SET_CHECK_ATTEMPT stage='%s' rank=%u clear=%u oldKnown=%d oldRank=%u",
        stage.c_str(), newRank, ids[0], hasOldRank, oldRank);
    apRankPreviouslyReadByObject[object] = newRank;
}

// AP ACT RESULTS V4: native Werehog/Night result path.
// Proven chain in PPC recompile: sub_82AFxxxx -> sub_82AEE248 computes
// the grade (0..4 valid, 5 missing), passes it as r9 to sub_82B113F8;
// sub_82B113F8 commits that grade through sub_8251C618.
// Run the native function unchanged, then try to report rank checks even
// if the game does not improve the previous record.
PPC_FUNC_IMPL(__imp__sub_82B113F8);
PPC_FUNC(sub_82B113F8)
{
    const std::string activeStage = AP_ActDiagStage();
    const std::string cachedStage = apRankLastStage;
    const uint32_t completedRank = ctx.r9.u32;
    const bool recent = std::chrono::steady_clock::now() - apRankStageSeenAt <
        std::chrono::seconds(180);
    AP_ActDiagLog("NIGHT_RESULT_ENTER active='%s' cached='%s' rank=%u recent=%d",
        activeStage.c_str(), cachedStage.c_str(), completedRank, recent);
    if (completedRank <= 4u) AP_NotifyTrapStageFinished();

    // Preserve the game result, records and original PPC register state.
    __imp__sub_82B113F8(ctx, base);

    std::string stage;
    if (apRankActLocations.find(activeStage) != apRankActLocations.end())
        stage = activeStage;
    else if (activeStage.empty() && recent &&
             apRankActLocations.find(cachedStage) != apRankActLocations.end())
        stage = cachedStage;

    // This hook handles the Werehog result path. Do not attribute a result
    // from another game mode to a previously cached stage.
    const bool isNight = stage.rfind("ActN_", 0) == 0 || stage == "Act_EggmanLand";
    if (!isNight || completedRank > 4)
    {
        AP_ActDiagLog("NIGHT_RESULT_SKIPPED stage='%s' rank=%u reason='%s'",
            stage.c_str(), completedRank,
            !isNight ? "not-recognized-night-act" : "no-valid-rank");
        return;
    }

    const auto& a = apRankActLocations.at(stage);
    const uint32_t ids[] = {a.actClear, a.rankD, a.rankC, a.rankB, a.rankA};
    APClient::QueueLocationCheck(ids[0]);
    for (uint32_t threshold = 4, i = 1; i < 5; ++i, --threshold)
        if (completedRank <= threshold)
            APClient::QueueLocationCheck(ids[i]);

    // ATTEMPTED, not guaranteed to be accepted: APClient may reject offline
    // checks. The client console's AP CHECK QUEUED/CONFIRMED is authoritative.
    AP_ActDiagLog("NIGHT_RESULT_CHECK_ATTEMPT stage='%s' rank=%u clear=%u",
        stage.c_str(), completedRank, ids[0]);
    LOGFN("[AP ACT V4] Night result: stage={} rank={} clearId={} (attempted)",
        stage, completedRank, ids[0]);
}

// AP ACT COVERAGE FIX V3: the final-results function is called even when the
// player does NOT beat an existing best rank. Its PPC body reads the current
// result rank from *( (*(r3 + 8) + 300) + 60 ). See ppc_recomp.27.cpp.
// Importantly, do not modify the original game's records or result logic.
PPC_FUNC_IMPL(__imp__sub_824A1B58);
PPC_FUNC(sub_824A1B58)
{
    const std::string activeStage = AP_ActDiagStage();
    const std::string cachedStage = apRankLastStage;
    const auto lastSeen = apRankStageSeenAt;
    const uint32_t resultsObject = ctx.r3.u32;
    uint32_t actualRank = 999;
    if (resultsObject >= 0x10000u && resultsObject < 0x80000000u)
    {
        const uint32_t resultsState = PPC_LOAD_U32(resultsObject + 8);
        if (resultsState >= 0x10000u && resultsState < 0x80000000u)
        {
            const uint32_t gradeState = PPC_LOAD_U32(resultsState + 300);
            if (gradeState >= 0x10000u && gradeState < 0x80000000u)
                actualRank = PPC_LOAD_U32(gradeState + 60);
        }
    }
    // The native result transition is an authoritative gameplay-trap cutoff.
    if (actualRank <= 4u) AP_NotifyTrapStageFinished();
    // The real game still handles the results with its original function.
    __imp__sub_824A1B58(ctx, base);

    std::string stage;
    if (apRankActLocations.find(activeStage) != apRankActLocations.end())
        stage = activeStage;
    else if ((activeStage.empty() || activeStage == cachedStage) &&
             apRankActLocations.find(cachedStage) != apRankActLocations.end() &&
             std::chrono::steady_clock::now() - lastSeen < std::chrono::seconds(180))
        stage = cachedStage;

    AP_ActDiagLog("RESULT_RANK stage='%s' active='%s' cached='%s' rank=%u",
        stage.c_str(), activeStage.c_str(), cachedStage.c_str(), actualRank);
    if (stage.empty() || actualRank > 4)
        return;

    const auto& a = apRankActLocations.at(stage);
    const uint32_t ids[] = {a.actClear, a.rankD, a.rankC, a.rankB, a.rankA};
    // Native thresholds: D<=4, C<=3, B<=2, A<=1; 0 also passes all.
    // All checks for thresholds met by this result are safe to retry because
    // APClient::QueueLocationCheck deduplicates checked and pending IDs.
    APClient::QueueLocationCheck(ids[0]);
    for (uint32_t t = 4, i = 1; i < 5; ++i, --t)
        if (actualRank <= t) APClient::QueueLocationCheck(ids[i]);
    AP_ActDiagLog("DAY_RESULT_CHECK_ATTEMPT stage='%s' rank=%u clear=%u",
        stage.c_str(), actualRank, ids[0]);
}

// AP TEST - Medal event + Stage ID + Anti-duplicates
static std::unordered_set<std::string> apCollectedMedals;

PPC_FUNC_IMPL(__imp__sub_8268AC88);
PPC_FUNC(sub_8268AC88)
{

static bool apCurlTestDone = false;

if (!apCurlTestDone)
{
    apCurlTestDone = true;
    AP_TestCurlWebSocket();
}
    uint32_t object = ctx.r3.u32;

    if (object != 0)
    {
        uint32_t type = PPC_LOAD_U32(object + 232);
        uint32_t extra = PPC_LOAD_U32(object + 236);

        if (type <= 1)
        {
            if (auto pGameDocument = SWA::CGameDocument::GetInstance())
            {
                auto member = pGameDocument->m_pMember;

                if (member && !member->m_StageName.empty())
                {
                    const std::string stage = member->m_StageName.c_str();
                    AP_RememberRankStage(stage);
                    const std::string medalType =
                        (type == 0) ? "Moon" : "Sun";

                    const std::string checkID =
                        stage + "_" + medalType + "_" +
                        std::to_string(extra);

                    AP_ActDiagLog("MEDAL_PICKUP stage='%s' type=%u index=%u key='%s' mapped=%d",
                        stage.c_str(), type, extra, checkID.c_str(),
                        apMedalLocationIds.find(checkID) != apMedalLocationIds.end());
                    const bool firstSeenInProcess = apCollectedMedals.insert(checkID).second;
                    const auto location = apMedalLocationIds.find(checkID);
                    if (location != apMedalLocationIds.end())
                    {
                        // APClient handles pending/confirmed dedup persistently.
                        // Do NOT block a second pickup because the first one
                        // may have happened while offline or before authentication.
                        APClient::QueueLocationCheck(location->second);
                        const char* apDisplayName = AP_MedalDisplayName(location->second);
                        LOGFN("AP LOCATION CHECK ATTEMPT: {} -> {} (name='{}' firstThisRun={})",
                            checkID, location->second,
                            apDisplayName ? apDisplayName : "Unknown Medal", firstSeenInProcess);
                        AP_ActDiagLog("MEDAL_CHECK_ATTEMPT key='%s' id=%lld name='%s' firstThisRun=%d",
                            checkID.c_str(), static_cast<long long>(location->second),
                            apDisplayName ? apDisplayName : "Unknown Medal", firstSeenInProcess);
                    }
                    else
                    {
                        LOGFN("AP LOCATION UNKNOWN: {}", checkID);
                    }
                }
                else
                {
                    AP_ActDiagLog("MEDAL_PICKUP_NO_STAGE type=%u index=%u", type, extra);
                    LOGFN("AP CHECK: stage unavailable");
                }
            }
        }
    }

    __imp__sub_8268AC88(ctx, base);
}

// AP_MEDAL_RADAR_DIAG_V1_BEGIN
// Read-only transform inspection. These offsets come from the recompiled
// sub_82515C40 -> sub_825902D0 -> sub_825904A0 transform-component path.
// The second vector is a world-matrix translation CANDIDATE until in-game
// measurements confirm its coordinate space. No Archipelago checks are changed.
static float AP_MedalRadarReadFloat(uint8_t* base, uint32_t guestAddress)
{
    const uint32_t bits = PPC_LOAD_U32(guestAddress);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static void AP_MedalRadarLogTransform(uint8_t* base, uint32_t object,
    const std::string& medalID)
{
    // Called after the original initialization routine completed.
    // The medal's base component is created in sub_82515C40.
    const uint32_t component = PPC_LOAD_U32(object + 196);
    if (component == 0)
    {
        LOGFN("AP MEDAL RADAR: {} status=NO_TRANSFORM_COMPONENT", medalID);
        return;
    }

    // +112: local-position vector (XYZ).
    const float lx = AP_MedalRadarReadFloat(base, component + 112);
    const float ly = AP_MedalRadarReadFloat(base, component + 116);
    const float lz = AP_MedalRadarReadFloat(base, component + 120);

    // +192: second 4x4 transform matrix. Its final column starts at +240.
    // Its world-space interpretation remains to be verified in-game.
    const float wx = AP_MedalRadarReadFloat(base, component + 240);
    const float wy = AP_MedalRadarReadFloat(base, component + 244);
    const float wz = AP_MedalRadarReadFloat(base, component + 248);
    if (!std::isfinite(lx) || !std::isfinite(ly) || !std::isfinite(lz) ||
        !std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz))
    {
        LOGFN("AP MEDAL RADAR: {} status=INVALID_POSITION component=0x{:08X}",
            medalID, component);
        return;
    }

    // One record per unique stage/type/index, not once per frame.
    // os::logger::ExportBugReport() (F8) includes normal LOGFN diagnostics.
    LOGFN("AP MEDAL RADAR: {} component=0x{:08X} local=({:.3f},{:.3f},{:.3f}) matrixTranslation=({:.3f},{:.3f},{:.3f})",
        medalID, component, lx, ly, lz, wx, wy, wz);
}
// AP_MEDAL_RADAR_DIAG_V1_END


// AP_MEDAL_RADAR_AUDIO_V1_BEGIN
// World positions for both Sonic forms and medals were confirmed in V3 logs.
// Publish only the closest uncollected medal distance to the SDL/UI thread.
// This code never plays audio, changes AP checks, or modifies the game save.
//
// Generated PPC evidence (from the user's ppc(4).zip):
//   sub_823082F8: context+0x10 -> sub_825904A0 (writes vector at +0x70)
//   sub_82308300: context+0x10 -> sub_825904E8 (writes rotation at +0x60)
//   sub_82308308: context+0x10 -> sub_82489960 (reads rotation at +0x60)
// Validated in-game 2026-10-29: Day and Werehog positions track movement.
struct AP_MedalRadarV3Position
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static std::string apMedalRadarV3Stage;
static std::unordered_map<std::string, AP_MedalRadarV3Position> apMedalRadarV3Medals;
static uint32_t apMedalRadarV3PlayerObject = 0;
static uint32_t apMedalRadarV3Samples = 0;
static bool apMedalRadarV3HasPreviousPosition = false;
static AP_MedalRadarV3Position apMedalRadarV3PreviousPosition;
static std::chrono::steady_clock::time_point apMedalRadarV3LastSample{};
static std::chrono::steady_clock::time_point apMedalRadarV3LastRealtimeSample{};

static bool AP_MedalRadarV3GuestPointer(uint32_t address)
{
    return address >= 0x10000 && address < 0x40000000;
}

static bool AP_MedalRadarV3Valid(const AP_MedalRadarV3Position& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::fabs(p.x) < 1000000.0f && std::fabs(p.y) < 1000000.0f &&
        std::fabs(p.z) < 1000000.0f;
}

static AP_MedalRadarV3Position AP_MedalRadarV3ReadXYZ(uint8_t* base, uint32_t address)
{
    return {
        AP_MedalRadarReadFloat(base, address),
        AP_MedalRadarReadFloat(base, address + 4),
        AP_MedalRadarReadFloat(base, address + 8)
    };
}

static void AP_MedalRadarV3SetStage(const std::string& stage)
{
    if (stage == apMedalRadarV3Stage)
        return;

    apMedalRadarV3Stage = stage;
    apMedalRadarV3Medals.clear();
    apMedalRadarV3PlayerObject = 0;
    apMedalRadarV3Samples = 0;
    apMedalRadarV3HasPreviousPosition = false;
    apMedalRadarV3LastSample = {};
    apMedalRadarV3LastRealtimeSample = {};
    APMedalRadarBridge::Clear();
}

static void AP_MedalRadarV3RememberMedal(uint8_t* base,
    uint32_t object, const std::string& stage, const std::string& medalID)
{
    AP_MedalRadarV3SetStage(stage);
    if (!AP_MedalRadarV3GuestPointer(object))
        return;

    const uint32_t component = PPC_LOAD_U32(object + 196);
    if (!AP_MedalRadarV3GuestPointer(component))
        return;

    // Only store a snapshot: medal objects can be destroyed on pickup.
    const auto medal = AP_MedalRadarV3ReadXYZ(base, component + 240);
    if (AP_MedalRadarV3Valid(medal))
        apMedalRadarV3Medals[medalID] = medal;
}

static void AP_MedalRadarV3PlayerSample(uint8_t* base,
    uint32_t playerObject, const char* character)
{
    if (!AP_MedalRadarV3GuestPointer(playerObject))
    {
        APMedalRadarBridge::Clear();
        return;
    }

    const auto* doc = SWA::CGameDocument::GetInstance();
    if (!doc || !doc->m_pMember)
    {
        APMedalRadarBridge::Clear();
        return;
    }

    const std::string stage = doc->m_pMember->m_StageName.c_str();
    if (stage.empty())
    {
        APMedalRadarBridge::Clear();
        return;
    }

    AP_MedalRadarV3SetStage(stage);
    if (apMedalRadarV3PlayerObject != playerObject)
    {
        apMedalRadarV3PlayerObject = playerObject;
        apMedalRadarV3Samples = 0;
        apMedalRadarV3HasPreviousPosition = false;
        apMedalRadarV3LastSample = {};
        apMedalRadarV3LastRealtimeSample = {};
        APMedalRadarBridge::Clear();
    }

    // The sound needs ~50 ms precision; retain the original one-second
    // diagnostics independently so the F8 bug report stays readable.
    const auto now = std::chrono::steady_clock::now();
    if (apMedalRadarV3LastRealtimeSample.time_since_epoch().count() != 0 &&
        now - apMedalRadarV3LastRealtimeSample < std::chrono::milliseconds(50))
        return;
    apMedalRadarV3LastRealtimeSample = now;

    const bool shouldLog = apMedalRadarV3Samples < 40 &&
        (apMedalRadarV3LastSample.time_since_epoch().count() == 0 ||
         now - apMedalRadarV3LastSample >= std::chrono::seconds(1));
    if (shouldLog)
    {
        apMedalRadarV3LastSample = now;
        ++apMedalRadarV3Samples;
    }

    const uint32_t context = PPC_LOAD_U32(playerObject + 196);
    if (!AP_MedalRadarV3GuestPointer(context))
    {
        APMedalRadarBridge::Clear();
        if (shouldLog) LOGFN("AP MEDAL RADAR V3: character={} stage={} status=NO_CONTEXT player=0x{:08X} context=0x{:08X}",
            character, stage, playerObject, context);
        return;
    }

    const uint32_t state = PPC_LOAD_U32(context + 0x10);
    if (!AP_MedalRadarV3GuestPointer(state))
    {
        APMedalRadarBridge::Clear();
        if (shouldLog) LOGFN("AP MEDAL RADAR V3: character={} stage={} status=NO_STATE player=0x{:08X} context=0x{:08X} state=0x{:08X}",
            character, stage, playerObject, context, state);
        return;
    }

    // Position (written by sub_825904A0) and orientation (read by
    // sub_82308308) are in the SAME live movement-state object.
    const auto position = AP_MedalRadarV3ReadXYZ(base, state + 0x70);
    const float qw = AP_MedalRadarReadFloat(base, state + 0x60);
    const float qx = AP_MedalRadarReadFloat(base, state + 0x64);
    const float qy = AP_MedalRadarReadFloat(base, state + 0x68);
    const float qz = AP_MedalRadarReadFloat(base, state + 0x6C);
    const double qLenSquared = double(qw) * qw + double(qx) * qx +
        double(qy) * qy + double(qz) * qz;

    if (!AP_MedalRadarV3Valid(position))
    {
        APMedalRadarBridge::Clear();
        if (shouldLog) LOGFN("AP MEDAL RADAR V3: character={} stage={} status=INVALID_POSITION state=0x{:08X} xyz=({:.3f},{:.3f},{:.3f})",
            character, stage, state, position.x, position.y, position.z);
        return;
    }

    double moved = 0.0;
    if (apMedalRadarV3HasPreviousPosition)
    {
        const double dx = double(position.x) - apMedalRadarV3PreviousPosition.x;
        const double dy = double(position.y) - apMedalRadarV3PreviousPosition.y;
        const double dz = double(position.z) - apMedalRadarV3PreviousPosition.z;
        moved = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    apMedalRadarV3PreviousPosition = position;
    apMedalRadarV3HasPreviousPosition = true;

    double nearestSquared = 1.0e30;
    std::string nearestID;
    size_t remaining = 0;
    for (const auto& [medalID, medal] : apMedalRadarV3Medals)
    {
        // AP_MEDAL_RADAR_PERSIST_V1: consult both the in-session pickup cache
        // and AP's seed/slot-specific saved or server-confirmed checks.
        // Do not remove the medal entity: object destruction is not validated.
        if (apCollectedMedals.find(medalID) != apCollectedMedals.end())
            continue;
        const auto mappedMedal = apMedalLocationIds.find(medalID);
        if (mappedMedal != apMedalLocationIds.end() &&
            APClient::IsLocationChecked(mappedMedal->second))
            continue;
        ++remaining;
        const double dx = double(position.x) - medal.x;
        const double dy = double(position.y) - medal.y;
        const double dz = double(position.z) - medal.z;
        const double distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared < nearestSquared)
        {
            nearestSquared = distanceSquared;
            nearestID = medalID;
        }
    }

    if (nearestID.empty())
    {
        APMedalRadarBridge::Clear();
        if (shouldLog) LOGFN("AP MEDAL RADAR V3: character={} stage={} state=0x{:08X} playerXYZ=({:.3f},{:.3f},{:.3f}) moved={:.3f} qLenSq={:.3f} remaining=0 status=NO_TRACKED_MEDAL",
            character, stage, state, position.x, position.y, position.z,
            moved, qLenSquared);
        return;
    }

    const float distance = static_cast<float>(std::sqrt(nearestSquared));
    APMedalRadarBridge::Publish(distance);
    if (shouldLog) LOGFN("AP MEDAL RADAR V3: character={} stage={} state=0x{:08X} playerXYZ=({:.3f},{:.3f},{:.3f}) moved={:.3f} qLenSq={:.3f} nearest={} distance={:.2f} remaining={}",
        character, stage, state, position.x, position.y, position.z,
        moved, qLenSquared, nearestID, distance, remaining);
}
// AP_MEDAL_RADAR_AUDIO_V1_END

// AP TEST - Detect medals before pickup
static std::unordered_set<std::string> apDetectedMedals;

PPC_FUNC_IMPL(__imp__sub_8268A788);
PPC_FUNC(sub_8268A788)
{
    // Save the original object pointer BEFORE calling the function.
    const uint32_t object = ctx.r3.u32;

// AP DEBUG - Save initialization arguments
const uint32_t apArg4 = ctx.r4.u32;
const uint32_t apArg5 = ctx.r5.u32;
const uint32_t apArg6 = ctx.r6.u32;

    static uint32_t callCount = 0;
    ++callCount;

    if (callCount <= 10)
    {
        LOGFN("AP INIT HOOK CALLED: count={} object={}",
            callCount, object);
    }

    // Let the game initialize the object normally.
    const uint32_t apTypeBefore = PPC_LOAD_U32(object + 232);
    const uint32_t apIndexBefore = PPC_LOAD_U32(object + 236);
    __imp__sub_8268A788(ctx, base);

    if (object == 0)
        return;

    const uint32_t type = PPC_LOAD_U32(object + 232);
    const uint32_t index = PPC_LOAD_U32(object + 236);
    LOGFN("AP MEDAL BEFORE/AFTER: type {} to {} index {} to {}", apTypeBefore, type, apIndexBefore, index);

LOGFN("AP MEDAL DEBUG: object={} type={} index={} r4={} r5={} r6={}",
    object, type, index, apArg4, apArg5, apArg6);

    if (type > 1 || index > 31)
        return;

    auto pGameDocument = SWA::CGameDocument::GetInstance();

    if (!pGameDocument || !pGameDocument->m_pMember)
        return;

    const std::string stage =
        pGameDocument->m_pMember->m_StageName.c_str();

    if (stage.empty())
        return;

    AP_RememberRankStage(stage);

    const std::string medalType =
        (type == 0) ? "Moon" : "Sun";

    const std::string medalID =
        stage + "_" + medalType + "_" + std::to_string(index);

    if (stage.find("Sub") != std::string::npos)
        AP_ActDiagLog("MEDAL_SPAWN stage='%s' type=%u index=%u key='%s' mapped=%d",
            stage.c_str(), type, index, medalID.c_str(),
            apMedalLocationIds.find(medalID) != apMedalLocationIds.end());
    if (apDetectedMedals.insert(medalID).second)
    {
        LOGFN("AP MEDAL DETECTED: {}", medalID);
        AP_MedalRadarLogTransform(base, object, medalID); // AP_MEDAL_RADAR_DIAG_V1
    }
    // Track even on a stage reload when the old one-time detection log
    // has already been emitted for this medal ID.
    AP_MedalRadarV3RememberMedal(base, object, stage, medalID);
}

// AP TEST - Medal constructor
PPC_FUNC_IMPL(__imp__sub_8268A608);

PPC_FUNC(sub_8268A608)
{
    const uint32_t object = ctx.r3.u32;
    const uint32_t constructorType = ctx.r4.u32;

    __imp__sub_8268A608(ctx, base);

    if (object == 0)
        return;

    const uint32_t type = PPC_LOAD_U32(object + 232);
    const uint32_t index = PPC_LOAD_U32(object + 236);

    LOGFN("AP MEDAL CONSTRUCTOR: object={} argType={} type={} index={}",
        object, constructorType, type, index);
}
// AP DEBUG - Medal progression requirement check
PPC_FUNC_IMPL(__imp__sub_8273EFC0);
PPC_FUNC(sub_8273EFC0)
{
    const uint32_t object = ctx.r3.u32;

    uint32_t requirementType = 0;
    uint32_t requiredLevel = 0;

    if (object != 0)
    {
        requirementType = PPC_LOAD_U32(object + 244);
        requiredLevel = PPC_LOAD_U32(object + 276);
    }

    __imp__sub_8273EFC0(ctx, base);

    const uint32_t result = ctx.r3.u32;

    LOGFN(
        "AP MEDAL DEBUG: type={} requiredLevel={} result={}",
        requirementType,
        requiredLevel,
        result
    );
}

// AP Progressive Medals - virtual progression level
PPC_FUNC_IMPL(__imp__sub_8251ACB8);
PPC_FUNC(sub_8251ACB8)
{
    const uint32_t medalType = ctx.r4.u32;

    // Original medal count: 0 = Moon, 1 = Sun.
    sub_8251AB70(ctx, base);

    const uint32_t physicalCount = ctx.r3.u32;

    uint32_t bonus = 0;

    if (medalType == 0)
        bonus = APClient::GetMoonMedalBonus();
    else if (medalType == 1)
        bonus = APClient::GetSunMedalBonus();

    const uint32_t total = bonus;

    uint32_t level = 1;

    if (total >= 120) level = 7;
    else if (total >= 80) level = 6;
    else if (total >= 60) level = 5;
    else if (total >= 45) level = 4;
    else if (total >= 30) level = 3;
    else if (total >= 15) level = 2;

    ctx.r3.u64 = level;


}





// AP : diagnostic du compteur Rings pendant un Act.
PPC_FUNC_IMPL(__imp__sub_82321640);
PPC_FUNC(sub_82321640)
{
const auto apRingObject = ctx.r3.u32;
    const auto apOriginalAmount = ctx.r4.u32;
    __imp__sub_82321640(ctx, base);
    // Only original positive gameplay gains count. AP and roulette bonuses are
    // injected separately below and MUST NOT advance the wheel counter.
    if (!g_apRouletteInjectingRings)
    {
        const auto* doc = SWA::CGameDocument::GetInstance();
        const char* stage = (doc && doc->m_pMember)
            ? doc->m_pMember->m_StageName.c_str() : nullptr;
        if (stage && std::strncmp(stage, "ActD_", 5) == 0)
        {
            AP_RouletteRememberRing(apRingObject, false, stage);
            APRingRoulette::RecordPickup(apOriginalAmount);
        }
    }

    // AP Ring Wipe Trap : Sonic Day.
    const auto* apDayWipeDoc = SWA::CGameDocument::GetInstance();
    const char* apDayWipeStage =
        (apDayWipeDoc && apDayWipeDoc->m_pMember)
        ? apDayWipeDoc->m_pMember->m_StageName.c_str()
        : "";

    const bool apDayWipeAllowedStage =
        std::strncmp(apDayWipeStage, "ActD_", 5) == 0 ||
        std::strcmp(apDayWipeStage, "BossEggBeetle") == 0 ||
        std::strcmp(apDayWipeStage, "BossEggLancer") == 0 ||
        std::strcmp(apDayWipeStage, "BossEggRayBird") == 0;

    if (apRingObject != 0 && apDayWipeAllowedStage)
    {
        const auto apCurrentRings =
            PPC_LOAD_U32(apRingObject + 1336);

        if (apCurrentRings <= 999999 &&
            APClient::GetPendingRingWipe() > 0)
        {
            if (apCurrentRings > 0)
            {
                ctx.r3.u32 = apRingObject;
                ctx.r4.u32 = 0u - apCurrentRings;
                __imp__sub_82321640(ctx, base);
            }

            APClient::ConfirmRingWipeApplied();
            LOGFN("AP RING WIPE APPLIED: SONIC DAY");
        }
    }


    const auto apRingBonus = APClient::GetPendingRingBonus();
    if (apRingBonus > 0)
    {
        ctx.r3.u32 = apRingObject;
        ctx.r4.u32 = apRingBonus;
        __imp__sub_82321640(ctx, base);
        APClient::ConfirmRingBonusApplied();
    }
}







// AP DEBUG : detecter le HUD des Acts Werehog.
PPC_FUNC_IMPL(__imp__sub_8249DAD0);
PPC_FUNC(sub_8249DAD0)
{
    const uint32_t apHudObject = ctx.r3.u32;

    __imp__sub_8249DAD0(ctx, base);

    static uint32_t apHudUpdateCount = 0;
    ++apHudUpdateCount;

    if (apHudUpdateCount <= 5 ||
        apHudUpdateCount % 600 == 0)
    {
        LOGFN("AP WEREHOG ACT HUD: count={} hud={:08X}",
            apHudUpdateCount, apHudObject);
    }
}
// AP Full Ring : restaurer les PV du Werehog dans les Acts de nuit.
// BEGIN AP_WEREHOG_BURN_DAMAGE_V1
#include <chrono>
#include <string>
#include "ap_werehog_burn_damage.h"

namespace
{
    bool apBurnDamageActive = false;
    uint32_t apBurnDamageStats = 0;
    unsigned int apBurnDamageTicks = 0;
    std::string apBurnDamageStage;
    std::chrono::steady_clock::time_point apBurnDamageStart;
    std::chrono::steady_clock::time_point apBurnDamageLastCall;
}

void AP_WerehogBurnStart(uint32_t stats)
{
    if (stats == 0)
        return;

    const auto* doc = SWA::CGameDocument::GetInstance();

    if (!doc || !doc->m_pMember)
        return;

    const char* stage = doc->m_pMember->m_StageName.c_str();

    if (!stage)
        return;

    const bool validStage =
        std::strncmp(stage, "ActN_", 5) == 0 ||
        std::strcmp(stage, "Act_EggmanLand") == 0 ||
        std::strcmp(stage, "BossDarkGaiaMoray") == 0 ||
        std::strcmp(stage, "BossPhoenix") == 0 ||
        std::strcmp(stage, "BossPetra") == 0 ||
        std::strcmp(stage, "BossEggDragoon") == 0;

    if (!validStage)
        return;

    apBurnDamageActive = true;
    apBurnDamageStats = stats;
    apBurnDamageTicks = 0;
    apBurnDamageStage = stage;
    apBurnDamageStart = std::chrono::steady_clock::now();
    apBurnDamageLastCall = apBurnDamageStart;

    LOGFN("[AP BURN DAMAGE] Started stats={:08X}", stats);
}
// END AP_WEREHOG_BURN_DAMAGE_V1

PPC_FUNC_IMPL(__imp__sub_823C6400);
PPC_FUNC(sub_823C6400)
{
    const uint32_t apWerehogObject = ctx.r3.u32;
    static unsigned int apClawsHookCount = 0;
    if (++apClawsHookCount % 60 == 0)
        {
    const auto* apTestDoc = SWA::CGameDocument::GetInstance();
    const char* apTestStage =
        (apTestDoc && apTestDoc->m_pMember)
        ? apTestDoc->m_pMember->m_StageName.c_str()
        : "UNKNOWN";

    LOGFN("AP PLAYABLE CHARACTER: count={} player={:08X} stage={}",
        apClawsHookCount, apWerehogObject, apTestStage);
}

    // Executer normalement la mise a jour du jeu.
    __imp__sub_823C6400(ctx, base);
    // AP_MEDAL_RADAR_DIAG_V3: movement-state player XYZ, read only.
    AP_MedalRadarV3PlayerSample(base, apWerehogObject, "Werehog");

    const auto* apDoc = SWA::CGameDocument::GetInstance();

    if (!apDoc || !apDoc->m_pMember)
        return;

    const char* apStageName =
        apDoc->m_pMember->m_StageName.c_str();

    const bool apAllowedWerehogStage =
        std::strncmp(apStageName, "ActN_", 5) == 0 ||
        std::strcmp(apStageName, "BossDarkGaiaMoray") == 0 ||
        std::strcmp(apStageName, "BossPhoenix") == 0 ||
        std::strcmp(apStageName, "BossPetra") == 0 ||
        std::strcmp(apStageName, "BossEggDragoon") == 0 ||
        std::strcmp(apStageName, "Act_EggmanLand") == 0;

    if (!apAllowedWerehogStage)
        return;

    if (apWerehogObject == 0)
        return;

    const uint32_t apStats =
        PPC_LOAD_U32(apWerehogObject + 196);

    if (apStats == 0)
        return;

    const uint32_t apCurrentHP =
        PPC_LOAD_U32(apStats + 1660);



    // AP WEREHOG DELIVERY DELAY
    // Attendre 3 secondes avant de distribuer les objets AP.
    static std::string apDelayStage;
    static uint32_t apDelayStats = 0;
    static std::chrono::steady_clock::time_point apDelayStart;
    static std::chrono::steady_clock::time_point apLastWerehogCall;

    const auto apWerehogNow = std::chrono::steady_clock::now();

    if (apLastWerehogCall.time_since_epoch().count() != 0 &&
        apWerehogNow - apLastWerehogCall > std::chrono::seconds(1))
    {
        apDelayStart = apWerehogNow;
    }

    apLastWerehogCall = apWerehogNow;

    const std::string apCurrentStage =
        apDoc->m_pMember->m_StageName.c_str();

    if (apCurrentStage != apDelayStage ||
        apStats != apDelayStats)
    {
        apDelayStage = apCurrentStage;
        apDelayStats = apStats;
        apDelayStart = std::chrono::steady_clock::now();
    }

    if (apCurrentHP == 0)
    {
        apDelayStart = std::chrono::steady_clock::now();
        return;
    }

    if (std::chrono::steady_clock::now() - apDelayStart <
        std::chrono::seconds(5))
    {
        return;
    }

    static unsigned int apClawsStatsCount = 0;
    if (++apClawsStatsCount % 600 == 0)
        LOGFN("AP CLAWS STATS: stats={:08X} hp={}", apStats, apCurrentHP);

    // AP Dark Gaia Energy : remplir la jauge Unleash.
    if (apCurrentHP != 0)
    {
        const uint32_t apRawEnergy = PPC_LOAD_U32(apStats + 1672);
        float apEnergy = 0.0f;
        std::memcpy(&apEnergy, &apRawEnergy, sizeof(apEnergy));
        if (std::isfinite(apEnergy) && apEnergy >= 0.0f && apEnergy < 99.99f &&
            APClient::GetPendingDarkGaiaEnergy() != 0)
        {
            auto apEnergyCtx = ctx;
            ctx.r3.u32 = apStats;
            ctx.f1.f64 = 100000.0;
            sub_823AF7A8(ctx, base);
            const uint32_t apRawNewEnergy = PPC_LOAD_U32(apStats + 1672);
            float apNewEnergy = 0.0f;
            std::memcpy(&apNewEnergy, &apRawNewEnergy, sizeof(apNewEnergy));
            ctx = apEnergyCtx;
            if (std::isfinite(apNewEnergy) && apNewEnergy >= 99.99f)
            {
                APClient::ConfirmDarkGaiaEnergyApplied();
                LOGFN("AP DARK GAIA ENERGY APPLIED: {} -> {}", apEnergy, apNewEnergy);
            }
            else
            {
                APClient::CancelDarkGaiaEnergy();
                LOGFN("AP DARK GAIA ENERGY WARNING: {} -> {}", apEnergy, apNewEnergy);
            }
        }
    }

    // AP Super Claws : attendre la creation de l'effet natif.
    static bool apClawsWaitingForEffect = false;
    static uint32_t apClawsPreviousStats = 0;
    static unsigned int apClawsWaitFrames = 0;

    if (apClawsWaitingForEffect)
    {
        const uint32_t apEffect =
            (apStats == apClawsPreviousStats)
                ? PPC_LOAD_U32(apStats + 1136) : 0;

        const uint32_t apTimerRaw = PPC_LOAD_U32(apStats + 1692);
        float apTimer = 0.0f;
        std::memcpy(&apTimer, &apTimerRaw, sizeof(apTimer));

        if (apCurrentHP != 0 &&
            apEffect != 0 &&
            PPC_LOAD_U8(apStats + 1696) == 1 &&
            std::isfinite(apTimer) && apTimer > 0.0f)
        {
            APClient::ConfirmSuperClawsApplied();
            LOGFN("AP SUPER CLAWS EFFECT CONFIRMED: effect={:08X} time={}",
                apEffect, apTimer);
            apClawsWaitingForEffect = false;
        }
        else if (++apClawsWaitFrames >= 120 ||
                 apCurrentHP == 0 ||
                 apStats != apClawsPreviousStats)
        {
            APClient::CancelSuperClaws();
            LOGFN("AP SUPER CLAWS EFFECT TIMEOUT");
            apClawsWaitingForEffect = false;
        }
    }

    const uint32_t apClawsQueueTimerRaw = PPC_LOAD_U32(apStats + 1692);
    float apClawsQueueTimer = 0.0f;
    std::memcpy(&apClawsQueueTimer, &apClawsQueueTimerRaw, sizeof(apClawsQueueTimer));

    const bool apClawsAlreadyActive =
        PPC_LOAD_U8(apStats + 1696) != 0 ||
        (std::isfinite(apClawsQueueTimer) && apClawsQueueTimer > 0.0f);

    if (!apClawsWaitingForEffect &&
        apCurrentHP != 0 &&
        !apClawsAlreadyActive &&
        APClient::GetPendingSuperClaws() != 0)
    {
        const float apClawsDuration = 30.0f;
        uint32_t apClawsRaw = 0;
        std::memcpy(&apClawsRaw, &apClawsDuration, sizeof(apClawsRaw));

        PPC_STORE_U32(apStats + 1692, apClawsRaw);
        PPC_STORE_U8(apStats + 1696, 1);

        apClawsPreviousStats = apStats;
        apClawsWaitFrames = 0;
        apClawsWaitingForEffect = true;

        LOGFN("AP SUPER CLAWS REQUESTED: 30 seconds");
    }
    // AP Power Shield : protection de 30 secondes dans les ActN_.
    static bool apShieldWaitingForEffect = false;
    static uint32_t apShieldPreviousStats = 0;
    static unsigned int apShieldWaitFrames = 0;

    if (apShieldWaitingForEffect)
    {
        const uint32_t apShieldRaw = PPC_LOAD_U32(apStats + 1700);
        float apShieldTimer = 0.0f;
        std::memcpy(&apShieldTimer, &apShieldRaw, sizeof(apShieldTimer));

        if (apCurrentHP != 0 &&
            apStats == apShieldPreviousStats &&
            PPC_LOAD_U8(apStats + 1704) == 1 &&
            std::isfinite(apShieldTimer) &&
            apShieldTimer > 0.0f)
        {
            APClient::ConfirmPowerShieldApplied();
            LOGFN("AP POWER SHIELD CONFIRMED: time={}", apShieldTimer);
            apShieldWaitingForEffect = false;
        }
        else if (++apShieldWaitFrames >= 120 ||
                 apCurrentHP == 0 ||
                 apStats != apShieldPreviousStats)
        {
            APClient::CancelPowerShield();
            LOGFN("AP POWER SHIELD ACTIVATION TIMEOUT");
            apShieldWaitingForEffect = false;
        }
    }

    const uint32_t apShieldQueueTimerRaw = PPC_LOAD_U32(apStats + 1700);
    float apShieldQueueTimer = 0.0f;
    std::memcpy(&apShieldQueueTimer, &apShieldQueueTimerRaw, sizeof(apShieldQueueTimer));

    const bool apShieldAlreadyActive =
        PPC_LOAD_U8(apStats + 1704) != 0 ||
        (std::isfinite(apShieldQueueTimer) && apShieldQueueTimer > 0.0f);

    if (!apShieldWaitingForEffect &&
        apCurrentHP != 0 &&
        !apShieldAlreadyActive &&
        APClient::GetPendingPowerShield() != 0)
    {
        const float apShieldDuration = 30.0f;
        uint32_t apShieldRaw = 0;
        std::memcpy(&apShieldRaw, &apShieldDuration, sizeof(apShieldRaw));

        PPC_STORE_U32(apStats + 1700, apShieldRaw);
        PPC_STORE_U8(apStats + 1704, 1);

        apShieldPreviousStats = apStats;
        apShieldWaitFrames = 0;
        apShieldWaitingForEffect = true;

        LOGFN("AP POWER SHIELD REQUESTED: 30 seconds");
    }
    // AP Shield Charge : restaurer le bouclier uniquement si necessaire.
    if (apCurrentHP != 0)
    {
        const uint32_t apCurrentShield =
            PPC_LOAD_U32(apStats + 1664);

        auto apShieldChargeCtx = ctx;

        // Recuperer le bouclier maximum avec la fonction native.
        ctx.r3.u32 = apStats;
        sub_823A9E28(ctx, base);

        const uint32_t apMaxShield = ctx.r3.u32;
        ctx = apShieldChargeCtx;

        if (apMaxShield > 0 &&
            apCurrentShield < apMaxShield &&
            APClient::GetPendingShieldCharge() != 0)
        {
            ctx.r3.u32 = apStats;
            ctx.r4.u32 = apMaxShield;

            sub_823AF958(ctx, base);

            const uint32_t apNewShield =
                PPC_LOAD_U32(apStats + 1664);

            ctx = apShieldChargeCtx;

            if (apNewShield >= apMaxShield)
            {
                APClient::ConfirmShieldChargeApplied();

                LOGFN(
                    "AP SHIELD CHARGE APPLIED: {} -> {} / {}",
                    apCurrentShield, apNewShield, apMaxShield);
            }
            else
            {
                APClient::CancelShieldCharge();

                LOGFN(
                    "AP SHIELD CHARGE WARNING: {} -> {} / {}",
                    apCurrentShield, apNewShield, apMaxShield);
            }
        }
    }
    // Ne jamais utiliser le Full Ring sur un Werehog mort.
    if (apCurrentHP == 0)
        return;

    // Conserver le contexte du hook original.
    auto apSavedCtx = ctx;

    // Recuperer les PV maximum avec la fonction du jeu.
    ctx.r3.u32 = apStats;
    sub_823A9DE8(ctx, base);

    const uint32_t apMaxHP = ctx.r3.u32;

    ctx = apSavedCtx;

    // Securites : PV maximum valides et sante manquante.
    if (apMaxHP == 0 || apCurrentHP >= apMaxHP)
        return;

    // Recuperer un Full Ring Archipelago en attente.
    if (APClient::GetPendingFullRing() == 0)
        return;

    // Appliquer le soin via la fonction originale du jeu.
    ctx.r3.u32 = apStats;
    ctx.r4.u32 = apMaxHP;

    sub_823AF658(ctx, base);

    const uint32_t apNewHP =
        PPC_LOAD_U32(apStats + 1660);

    // Restaurer le contexte avant de quitter le hook.
    ctx = apSavedCtx;

    if (apNewHP >= apMaxHP)
    {
        APClient::ConfirmFullRingApplied();

        LOGFN(
            "AP FULL RING APPLIED: HP {} -> {} / {}",
            apCurrentHP, apNewHP, apMaxHP);
    }
    else
    {
        APClient::CancelFullRing();

        LOGFN(
            "AP FULL RING WARNING: HP {} -> {} / {}",
            apCurrentHP, apNewHP, apMaxHP);
    }
}
// AP : bonus Rings Archipelago pour le Werehog.
PPC_FUNC_IMPL(__imp__sub_823AF8E8);
PPC_FUNC(sub_823AF8E8)
{
    const auto apRingObject = ctx.r3.u32;
    const auto apOriginalRingAmount = ctx.r4.u32;

    // Ajouter les Rings normaux.
    __imp__sub_823AF8E8(ctx, base);
    if (!g_apRouletteInjectingRings)
    {
        const auto* doc = SWA::CGameDocument::GetInstance();
        const char* stage = (doc && doc->m_pMember)
            ? doc->m_pMember->m_StageName.c_str() : nullptr;
        if (stage && (std::strncmp(stage, "ActN_", 5) == 0 ||
            std::strcmp(stage, "Act_EggmanLand") == 0))
        {
            AP_RouletteRememberRing(apRingObject, true, stage);
            APRingRoulette::RecordPickup(apOriginalRingAmount);
        }
    }

    // AP Ring Wipe Trap : Werehog.
    const auto* apWipeDoc = SWA::CGameDocument::GetInstance();
    const char* apWipeStage =
        (apWipeDoc && apWipeDoc->m_pMember)
        ? apWipeDoc->m_pMember->m_StageName.c_str()
        : "";

    const bool apWipeAllowedStage =
        std::strncmp(apWipeStage, "ActN_", 5) == 0 ||
        std::strcmp(apWipeStage, "BossDarkGaiaMoray") == 0 ||
        std::strcmp(apWipeStage, "BossPhoenix") == 0 ||
        std::strcmp(apWipeStage, "BossPetra") == 0 ||
        std::strcmp(apWipeStage, "BossEggDragoon") == 0 ||
        std::strcmp(apWipeStage, "Act_EggmanLand") == 0;

    if (apRingObject != 0 && apWipeAllowedStage)
    {
        const auto apCurrentRings =
            PPC_LOAD_U32(apRingObject + 1632);

        if (apCurrentRings <= 999999 &&
            APClient::GetPendingRingWipe() > 0)
        {
            if (apCurrentRings > 0)
            {
                // The game's ring-change function also converts the delta
                // to HP damage when the Werehog has a certain state flag set.
                // A negative delta can therefore kill him. Clear only the
                // ring counter, then call the original function with a ZERO
                // delta to refresh the game's UI/related bookkeeping safely.
                const auto apRingWipeSavedCtx = ctx;
                PPC_STORE_U32(apRingObject + 1632, 0u);
                ctx.r3.u32 = apRingObject;
                ctx.r4.u32 = 0u;
                __imp__sub_823AF8E8(ctx, base);
                ctx = apRingWipeSavedCtx;
            }

            APClient::ConfirmRingWipeApplied();
            LOGFN("AP RING WIPE APPLIED: WEREHOG (rings cleared, HP untouched)");
        }
    }


    // Ajouter les Rings Archipelago en attente.
    const auto apRingBonus = APClient::GetPendingRingBonus();

    if (apRingBonus > 0)
    {
        ctx.r3.u32 = apRingObject;
        ctx.r4.u32 = apRingBonus;

        __imp__sub_823AF8E8(ctx, base);

        APClient::ConfirmRingBonusApplied();
    }
}
// AP Ring Roulette payout on the game thread. Keeps the last observed ring
// manager for the current Act so a spin after a long idle can pay out without
// requiring another Ring pickup. AP_WatchActRank clears it on stage changes.
static void AP_RouletteGameTick(const std::string& stage, void* saveData)
{
    using namespace APRingRoulette;
    if (!AP_RouletteIsAct(stage.c_str())) return;
    const auto prize = static_cast<Prize>(pending.load(std::memory_order_acquire));

    // These are genuine game rings (not banked save rings). No ring change
    // callbacks are required after activating the roulette.
    if (prize == Prize::Ring1 || prize == Prize::Ring100)
    {
        const std::uint32_t amount = prize == Prize::Ring1 ? 1u : 100u;
        std::uint32_t manager = 0;
        bool night = false;
        {
            std::lock_guard<std::mutex> lock(g_apRouletteRingMutex);
            if (g_apRouletteRingObject && g_apRouletteRingStage == stage)
            {
                manager = g_apRouletteRingObject;
                night = g_apRouletteRingNight;
            }
        }
        if (manager)
        {
            const auto address = manager + (night ? 1632u : 1336u);
            const std::uint32_t before = __builtin_bswap32(*reinterpret_cast<volatile std::uint32_t*>(g_memory.base + address));
            if (before < 999999u && amount <= 999999u - before)
            {
                g_apRouletteInjectingRings = true;
                if (night)
                    GuestToHostFunction<void>(sub_823AF8E8, manager, amount);
                else
                    GuestToHostFunction<void>(sub_82321640, manager, amount);
                g_apRouletteInjectingRings = false;
                const std::uint32_t after = __builtin_bswap32(*reinterpret_cast<volatile std::uint32_t*>(g_memory.base + address));
                if (after > before)
                {
                    Finish(prize);
                    LOGFN("[RING ROULETTE] Rings: {} -> {}", before, after);
                }
            }
        }
    }
    else if (prize == Prize::Hotdog && saveData)
    {
        // The same native inventory functions already used in the inventory
        // menu can be called from this verified GAME-thread Act callback.
        // Never call them from the UI renderer or with an unvalidated guest manager.
        static auto nextAttempt = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now < nextAttempt) return;
        nextAttempt = now + std::chrono::milliseconds(350);

        std::uint32_t manager = 0;
        {
            std::lock_guard<std::mutex> lock(g_apRouletteInventoryMutex);
            if (g_apRouletteInventoryManager &&
                now - g_apRouletteInventorySeen <= std::chrono::seconds(3))
                manager = g_apRouletteInventoryManager;
        }
        if (!manager) return;
        const auto saveGuest = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(saveData) -
            reinterpret_cast<std::uintptr_t>(g_memory.base));
        // This is a native helper, not a PPC_FUNC(ctx, base) callback.
        // Read the guest big-endian save pointer explicitly.
        const std::uint32_t managerSaveGuest = __builtin_bswap32(
            *reinterpret_cast<volatile const std::uint32_t*>(
                g_memory.base + manager + 148u));
        if (managerSaveGuest != saveGuest) return;

        constexpr std::uint32_t kHotdogId = 111u;
        const std::uint32_t before = GuestToHostFunction<std::uint32_t>(
            sub_82519C38, manager, kHotdogId);
        if (before >= 99u) return; // Do not discard the reward if inventory is full.
        GuestToHostFunction<void>(sub_8251B810, manager, kHotdogId, 1);
        const std::uint32_t after = GuestToHostFunction<std::uint32_t>(
            sub_82519C38, manager, kHotdogId);
        if (after > before)
        {
            Finish(Prize::Hotdog);
            LOGFN("[RING ROULETTE] Gameplay Hotdog awarded: {} -> {}", before, after);
        }
    }
    else if (prize == Prize::Score50000)
    {
        // Experimental: native score info verified via SWA CGameDocument.
        auto* doc = SWA::CGameDocument::GetInstance();
        if (!doc || !doc->m_pMember) return;
        const std::uint32_t previous = doc->m_pMember->m_ScoreInfo.TrickScore;
        const std::uint32_t next = previous > UINT32_MAX - 50000u
            ? UINT32_MAX : previous + 50000u;
        doc->m_pMember->m_ScoreInfo.TrickScore = next;
        Finish(prize);
        LOGFN("[RING ROULETTE] Bonus score: {} -> {} (verify HUD/results)", previous, next);
    }
    else if (prize == Prize::Jackpot777 && saveData &&
        !appliedJackpotBank.load(std::memory_order_acquire))
    {
        // The saved Rings bank offset is from the existing AP Rings save hook.
        // 100,000 is deliberately credited to banked Rings rather than the
        // in-level HUD ring counter (which can have a much smaller cap).
        auto* bank = reinterpret_cast<std::uint8_t*>(saveData) + 0x85A8;
        const std::uint32_t before =
            (std::uint32_t(bank[0]) << 24) | (std::uint32_t(bank[1]) << 16) |
            (std::uint32_t(bank[2]) << 8) | std::uint32_t(bank[3]);
        const std::uint32_t after = before > UINT32_MAX - 100000u
            ? UINT32_MAX : before + 100000u;
        bank[0] = static_cast<std::uint8_t>(after >> 24);
        bank[1] = static_cast<std::uint8_t>(after >> 16);
        bank[2] = static_cast<std::uint8_t>(after >> 8);
        bank[3] = static_cast<std::uint8_t>(after);
        appliedJackpotBank.store(true, std::memory_order_release);
        LOGFN("[RING ROULETTE] JACKPOT saved ring bank: {} -> {}", before, after);
        TryFinishJackpot();
    }
    // Minute60 deliberately waits: timer structure is not yet verified.
    // Do not silently mutate unknown memory addresses.
}

// AP DEBUG : observer le gestionnaire de vies sans modifier les vies.
static thread_local int apLifeSource = 0;
// Death Link V0.8.2: time-based protection shared by Sonic Day and Werehog.
// Do not clear this protection as soon as a life is deducted: the death
// sequence and the subsequent respawn are not necessarily complete yet.
static bool apInjectedDeath = false;
static std::chrono::steady_clock::time_point apDeathLinkShieldUntil{};
static std::chrono::steady_clock::time_point apNextIncomingDeathLinkAt{};
static std::chrono::steady_clock::time_point apNextOutgoingDeathLinkAt{};

static bool AP_IsDeathLinkShieldActive()
{
    if (apInjectedDeath && std::chrono::steady_clock::now() >= apDeathLinkShieldUntil)
    {
        apInjectedDeath = false;
        LOGFN("[AP DEATH LINK GUARD] Respawn protection expired");
    }
    return apInjectedDeath;
}

static bool AP_CanInjectDeathLink()
{
    return !AP_IsDeathLinkShieldActive() &&
        std::chrono::steady_clock::now() >= apNextIncomingDeathLinkAt;
}

static void AP_StartIncomingDeathLinkProtection()
{
    const auto now = std::chrono::steady_clock::now();
    apInjectedDeath = true;
    apDeathLinkShieldUntil = now + std::chrono::seconds(8);
    apNextIncomingDeathLinkAt = apDeathLinkShieldUntil;
}
PPC_FUNC_IMPL(__imp__sub_8251A728);
PPC_FUNC(sub_8251A728)
{
    const uint32_t apLifeManager = ctx.r3.u32;
    const int32_t apLifeDelta = ctx.r4.s32;
    __imp__sub_8251A728(ctx, base);
    if (apLifeDelta < 0)
    {
        LOGFN("[AP LIFE SOURCE] source={} manager={:08X} delta={}",
            apLifeSource, apLifeManager, apLifeDelta);

        const auto now = std::chrono::steady_clock::now();
        const bool incomingShield = AP_IsDeathLinkShieldActive();
        if (incomingShield)
        {
            // The received death is still in progress: never echo it to AP.
            LOGFN("[AP DEATH LINK GUARD] Life deduction during received Death Link (suppressed)");
        }
        else if (apLifeSource == 0 && apLifeDelta == -1 &&
            now >= apNextOutgoingDeathLinkAt)
        {
            // Some death sequences subtract a life more than once. Send a
            // single Death Link for the death event, not one per hook call.
            apNextOutgoingDeathLinkAt = now + std::chrono::seconds(5);
            LOGFN("[AP DEATH LINK LOCAL] Natural death candidate");
            APClient::QueueDeathLink();
            LOGFN("[AP DEATH LINK RETURN] QueueDeathLink finished");
        }
        if (!incomingShield && apLifeDelta == -1)
        {
            // Wait until a local death's respawn has finished before
            // consuming pending remote deaths (the queue stays intact).
            const auto resumeAt = now + std::chrono::seconds(6);
            if (apNextIncomingDeathLinkAt < resumeAt)
                apNextIncomingDeathLinkAt = resumeAt;
        }
    }
}

// AP DEBUG : observer le gestionnaire de vies lors des lectures.
PPC_FUNC_IMPL(__imp__sub_8251A5B0);
PPC_FUNC(sub_8251A5B0)
{
    static uint32_t lastManager = 0;
    const uint32_t manager = ctx.r3.u32;
    // Cache only a manager observed through the game itself. The gameplay
    // payout also checks that manager+148 still points at the CURRENT save.
    if (manager != 0)
    {
        std::lock_guard<std::mutex> lock(g_apRouletteInventoryMutex);
        g_apRouletteInventoryManager = manager;
        g_apRouletteInventorySeen = std::chrono::steady_clock::now();
    }
    if (manager != lastManager)
    {
        LOGFN("AP LIFE READ: manager={:08X}", manager);
        if (manager != 0)
        {
            const uint32_t apData = PPC_LOAD_U32(manager + 152);
            if (apData != 0)
            {
                LOGFN("AP LIFE MODE TEST: flag={} temporaryLives={}",
                    PPC_LOAD_U8(apData + 32),
                    PPC_LOAD_U32(apData + 28));
            }
        }
        if (manager != 0) {
            const uint32_t data = PPC_LOAD_U32(manager + 152);
            if (data != 0) {
                const uint32_t flag = PPC_LOAD_U8(data + 32);
                const uint32_t value = PPC_LOAD_U32(data + 28);
                LOGFN("AP LIFE VALUES: flag={} value={} data={:08X}", flag, value, data);
                if (flag == 0) {
                    const uint32_t saveBase = PPC_LOAD_U32(manager + 148);
                    if (saveBase != 0) {
                        const uint32_t lives = PPC_LOAD_U32(saveBase + 32768);
                        LOGFN("AP LIFE ALTERNATE: lives={} base={:08X}", lives, saveBase);

                        auto* apDoc = SWA::CApplicationDocument::GetInstance();
                        auto* apParam = (apDoc && apDoc->m_pMember)
                            ? apDoc->m_pMember->m_spGameParameter.get()
                            : nullptr;
                        auto* apSave = apParam ? apParam->m_pSaveData.get() : nullptr;

                        if (apSave)
                        {
                            const auto apSaveAddress =
                                static_cast<uint32_t>(
                                    reinterpret_cast<uintptr_t>(apSave) -
                                    reinterpret_cast<uintptr_t>(g_memory.base));

                            LOGFN("AP LIFE ADDRESS TEST: managerBase={:08X} saveData={:08X} match={}",
                                saveBase, apSaveAddress, saveBase == apSaveAddress);
                        }
                    }
                }
            }
        }
        lastManager = manager;
    }
    static unsigned int calls = 0; ++calls; if (calls % 20 == 0) { LOGFN("AP LIFE READ COUNT: appel {}, manager={:08X}", calls, manager); }
    __imp__sub_8251A5B0(ctx, base);
    // AP DEBUG : identifier la categorie du stage courant.
    {
        static uint32_t apLastStageId = 0xFFFFFFFF;
        const uint32_t apStageId = ctx.r3.u32;

        if (apStageId != apLastStageId)
        {
            const uint32_t apStageCategory = (apStageId % 1000) / 10;

            LOGFN("AP STAGE DEBUG: id={} category={}",
                apStageId, apStageCategory);

            apLastStageId = apStageId;
        }
    }

    // AP Life: appliquer une vie recue d'Archipelago.
    if (manager != 0)
    {
        const uint32_t data = PPC_LOAD_U32(manager + 152);
        const uint32_t saveBase = PPC_LOAD_U32(manager + 148);

        if (data != 0 && saveBase != 0 &&
            PPC_LOAD_U8(data + 32) == 0)
        {
            const uint32_t lives = PPC_LOAD_U32(saveBase + 32768);

            // Eviter de consommer Life lorsque le compteur est au maximum.
            if (lives < 99 && APClient::GetPendingLifeBonus() > 0)
            {
                auto savedR3 = ctx.r3;
                auto savedR4 = ctx.r4;

                ctx.r3.u32 = manager;
                ctx.r4.s32 = 1;
                __imp__sub_8251A728(ctx, base);

                ctx.r3 = savedR3;
                ctx.r4 = savedR4;

                LOGFN("AP LIFE APPLIED: Life, lives={}",
                    PPC_LOAD_U32(saveBase + 32768));

                APClient::ConfirmLifeBonusApplied();
            }
        }
    }

    // Ring Roulette lives: same verified manager/save path as AP Bonus Life.
    const auto reward = static_cast<APRingRoulette::Prize>(
        APRingRoulette::pending.load(std::memory_order_acquire));
    if (manager != 0 &&
        (reward == APRingRoulette::Prize::Life1 ||
         reward == APRingRoulette::Prize::Life3 ||
         reward == APRingRoulette::Prize::Jackpot777))
    {
        const auto data = PPC_LOAD_U32(manager + 152);
        const auto saveBase = PPC_LOAD_U32(manager + 148);
        if (data != 0 && saveBase != 0 && PPC_LOAD_U8(data + 32) == 0)
        {
            const std::uint32_t before = PPC_LOAD_U32(saveBase + 32768);
            if (before <= 99u)
            {
                const std::uint32_t target = reward == APRingRoulette::Prize::Jackpot777
                    ? 99u : (std::min)(99u, before +
                        (reward == APRingRoulette::Prize::Life3 ? 3u : 1u));
                if (target > before)
                {
                    const auto oldR3 = ctx.r3;
                    const auto oldR4 = ctx.r4;
                    ctx.r3.u32 = manager;
                    ctx.r4.s32 = static_cast<std::int32_t>(target - before);
                    __imp__sub_8251A728(ctx, base);
                    ctx.r3 = oldR3;
                    ctx.r4 = oldR4;
                }
                const std::uint32_t after = PPC_LOAD_U32(saveBase + 32768);
                if (after == target)
                {
                    LOGFN("[RING ROULETTE] Lives {} -> {}", before, after);
                    if (reward == APRingRoulette::Prize::Jackpot777)
                    {
                        APRingRoulette::appliedJackpotLives.store(true, std::memory_order_release);
                        APRingRoulette::TryFinishJackpot();
                    }
                    else APRingRoulette::Finish(reward);
                }
            }
        }
    }
}





// AP DEATH LINK V1 - Sonic Day death diagnostic (H2)
PPC_FUNC_IMPL(__imp__sub_823176A0);
PPC_FUNC(sub_823176A0)
{
    const uint32_t apSonicContext = ctx.r3.u32;
    const int32_t apRestart = ctx.r4.s32;

    LOGFN("[AP DEATH H2] Enter: SonicContext={:08X} restart={}",
        apSonicContext, apRestart);

    __imp__sub_823176A0(ctx, base);

    LOGFN("[AP DEATH H2] Return: SonicContext={:08X} restart={}",
        apSonicContext, apRestart);
}

// AP DEATH LINK V1 - H1 state name diagnostic
PPC_FUNC_IMPL(__imp__sub_82E67958);
PPC_FUNC(sub_82E67958)
{
    static uint32_t apH1Calls = 0;
    ++apH1Calls;

    const uint32_t apStateObject = ctx.r5.u32;

    if (apStateObject != 0)
    {
        PPCContext apNameCtx = ctx;
        apNameCtx.r3.u64 = apStateObject;
        sub_82DFAED0(apNameCtx, base);

        const uint32_t apNameAddress = apNameCtx.r3.u32;

        if (apNameAddress >= 0x10000 && apNameAddress < 0x80000000)
        {
            const char* apName = reinterpret_cast<const char*>(base + apNameAddress);

            const bool apDeathState =
                std::strncmp(apName, "NormalDamageDead", 16) == 0 ||
                std::strncmp(apName, "DrowningDead", 12) == 0 ||
                std::strncmp(apName, "PressDead", 9) == 0 ||
                std::strncmp(apName, "Evil_Die", 8) == 0;

            if (apH1Calls <= 50 || apDeathState)
            {
                LOGFN("[AP DEATH H1 STATE] Call={} State={:.64}",
                    apH1Calls, apName);
            }
        }
    }

    __imp__sub_82E67958(ctx, base);
}


// AP DEATH LINK V1 - Life deduction source diagnostic
PPC_FUNC_IMPL(__imp__sub_8253B7C0);
PPC_FUNC(sub_8253B7C0)
{
    static uint32_t apSourceACalls = 0;
    ++apSourceACalls;

    if (apSourceACalls <= 3)
    {
        LOGFN("[AP DEATH SOURCE A] Call={} r3={:08X}",
            apSourceACalls, ctx.r3.u32);
    }
    const int previousSource = apLifeSource;
    apLifeSource = 1;
    __imp__sub_8253B7C0(ctx, base);
    apLifeSource = previousSource;
}

PPC_FUNC_IMPL(__imp__sub_8259A0C0);
PPC_FUNC(sub_8259A0C0)
{
    LOGFN("[AP DEATH SOURCE B] Enter r3={:08X}", ctx.r3.u32);
    const int previousSource = apLifeSource;
    apLifeSource = 2;
    __imp__sub_8259A0C0(ctx, base);
    apLifeSource = previousSource;
}

PPC_FUNC_IMPL(__imp__sub_82310F20);
void AP_QteOfDoomOnSonicUpdate(PPCContext& ctx, uint8_t* base);
void AP_SonicDayFreezeManualOnUpdate(PPCContext& ctx, uint8_t* base);
// BEGIN AP_FREEZE_TRACE decl
void AP_FreezeTraceUpdateEnter(PPCContext& ctx, uint8_t* base);
void AP_FreezeTraceUpdateLeave(PPCContext& ctx, uint8_t* base);
// END AP_FREEZE_TRACE decl
PPC_FUNC(sub_82310F20)
{
    static bool apLoggedSonicUpdate = false;

    const uint32_t sonicObject = ctx.r3.u32;

    if (!apLoggedSonicUpdate && sonicObject != 0)
    {
        LOGFN("[AP SONIC UPDATE] CSonic={:08X} Context={:08X}", sonicObject, PPC_LOAD_U32(sonicObject + 196));
        apLoggedSonicUpdate = true;
    }

    // Time-based guard also expires if the last Death Link happened at night.
    if (sonicObject != 0 && AP_CanInjectDeathLink())
    {
        const uint32_t sonicContext =
            PPC_LOAD_U32(sonicObject + 196);

        if (sonicContext != 0 &&
            Config::DeathLinkSonicDay.Value &&
            APClient::ConsumeIncomingDeathLink())
        {
            LOGFN("[AP DEATH LINK INJECT] Sonic Day Context={:08X}",
                sonicContext);

            AP_StartIncomingDeathLinkProtection();

            PPCContext deathCtx = ctx;
            deathCtx.r3.u32 = sonicContext;
            deathCtx.r4.u32 = 1;

            sub_823176A0(deathCtx, base);

            LOGFN("[AP DEATH LINK INJECT] Appel termine");
        }
    }

    // AP_SONIC_DAY_FREEZE_MANUAL_V1
    AP_SonicDayFreezeManualOnUpdate(ctx, base);    AP_QteOfDoomOnSonicUpdate(ctx, base);
    // BEGIN AP_FREEZE_TRACE upd-enter
    AP_FreezeTraceUpdateEnter(ctx, base);
    // END AP_FREEZE_TRACE upd-enter
    __imp__sub_82310F20(ctx, base);
    // BEGIN AP_FREEZE_TRACE upd-leave
    AP_FreezeTraceUpdateLeave(ctx, base);
    // END AP_FREEZE_TRACE upd-leave
    // AP_MEDAL_RADAR_DIAG_V3: supports Sonic Day and Werehog.
    AP_MedalRadarV3PlayerSample(base, sonicObject, "SonicDay");
}

// AP WEREHOG DEATH LINK - DIAGNOSTIC
// Verifie les appels de la fonction qui surveille les HP.
// Aucun Death Link consomme et aucune modification des HP.

PPC_FUNC_IMPL(__imp__sub_823B0168);
PPC_FUNC(sub_823B0168)
{
    static unsigned int apWerehogDeathCalls = 0;

    const uint32_t apStats = ctx.r3.u32;

    if (apStats != 0 && ++apWerehogDeathCalls % 600 == 0)
    {
        const uint32_t apHP = PPC_LOAD_U32(apStats + 1660);

        LOGFN("[AP WEREHOG DEATH HOOK] Calls={} Stats={:08X} HP={}",
            apWerehogDeathCalls, apStats, apHP);
    }

    // DEATH LINK ENTRANT - WEREHOG
    // Seulement pendant un niveau Werehog actif.

    const auto* apTestDoc = SWA::CGameDocument::GetInstance();

    const char* apTestStage =
        (apTestDoc && apTestDoc->m_pMember)
        ? apTestDoc->m_pMember->m_StageName.c_str()
        : nullptr;

    const bool apValidWerehogStage =
        apTestStage != nullptr &&
        (
            std::strncmp(apTestStage, "ActN_", 5) == 0 ||
            std::strcmp(apTestStage, "BossDarkGaiaMoray") == 0 ||
            std::strcmp(apTestStage, "BossPhoenix") == 0 ||
            std::strcmp(apTestStage, "BossPetra") == 0 ||
            std::strcmp(apTestStage, "BossEggDragoon") == 0 ||
            std::strcmp(apTestStage, "Act_EggmanLand") == 0
        );

    if (apStats != 0 &&
        apValidWerehogStage &&
        AP_CanInjectDeathLink())
    {
        const uint32_t apHPBefore =
            PPC_LOAD_U32(apStats + 1660);

        if (apHPBefore > 0 &&
            Config::DeathLinkWerehog.Value &&
            APClient::ConsumeIncomingDeathLink())
        {
            AP_StartIncomingDeathLinkProtection();

            PPC_STORE_U32(apStats + 1660, 0);

            LOGFN("[AP DEATH LINK WEREHOG] Incoming Death Link! Stats={:08X} HP={} -> 0",
                apStats, apHPBefore);
        }
    }
        // BEGIN AP_WEREHOG_BURN_DAMAGE_TICK_V1
    if (apBurnDamageActive)
    {
        const auto now = std::chrono::steady_clock::now();

        const auto* burnDoc = SWA::CGameDocument::GetInstance();
        const char* burnStage =
            (burnDoc && burnDoc->m_pMember)
            ? burnDoc->m_pMember->m_StageName.c_str()
            : nullptr;

        const bool burnInvalid =
            apStats != apBurnDamageStats ||
            burnStage == nullptr ||
            apBurnDamageStage != burnStage ||
            now - apBurnDamageLastCall > std::chrono::seconds(1);

        if (burnInvalid)
        {
            apBurnDamageActive = false;
            LOGFN("[AP BURN DAMAGE] Cancelled");
        }
        else
        {
            apBurnDamageLastCall = now;

            const uint32_t hp =
                PPC_LOAD_U32(apStats + 1660);

            if (hp == 0)
            {
                apBurnDamageActive = false;
            }
            else
            {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - apBurnDamageStart).count();

                const unsigned int dueTicks =
                    static_cast<unsigned int>(
                        std::min<int64_t>(3, elapsed / 1000));

                if (apBurnDamageTicks < dueTicks)
                {
                    const uint32_t newHP =
                        hp > 100 ? hp - 100 : 0;

                    PPC_STORE_U32(apStats + 1660, newHP);
                    ++apBurnDamageTicks;

                    LOGFN("[AP BURN DAMAGE] Tick={} HP {} -> {}",
                        apBurnDamageTicks, hp, newHP);
                }

                if (apBurnDamageTicks >= 3)
                {
                    apBurnDamageActive = false;
                    LOGFN("[AP BURN DAMAGE] Completed");
                }
            }
        }
    }
    // END AP_WEREHOG_BURN_DAMAGE_TICK_V1
    __imp__sub_823B0168(ctx, base);
}







