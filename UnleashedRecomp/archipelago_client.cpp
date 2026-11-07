
#include "stdafx.h"
#include "archipelago_client.h"
#include "patches/ap_medal_display_names.h" // AP_MEDAL_CHECK_NAMES_V082
#include "os/logger.h"

#ifdef _WIN32

#include <Windows.h>
#include <winhttp.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdio>

#include <nlohmann/json.hpp>
#include <fstream>
#include "user/paths.h"
#include "user/config.h"

namespace
{
    using json = nlohmann::json;

    std::mutex apMutex;
// AP_MULTIWORLD_HUD_V082_BEGIN: protected by the existing apMutex.
std::vector<APClient::MultiplayerEvent> apMultiplayerQueue;
std::unordered_map<int, std::string> apMultiPlayerNames;
std::unordered_map<int, std::string> apMultiPlayerGames;
std::unordered_map<std::string, std::unordered_map<std::int64_t, std::string>> apMultiItemNames;
std::vector<std::string> apMultiWorldGames;
int apMultiOwnSlot = -1;
int apMedalGoalTarget = 338;
// AP_MULTIWORLD_HUD_V082_END
    std::condition_variable apWake;
// AP_GOAL_V1_BEGIN - protected by apMutex
int apSelectedGoal = -1; // 0 Dark Gaia, 1 Egg Dragoon, 2 medals
bool apGoalPending = false;
bool apGoalSent = false;
// AP_GOAL_V1_END
// AP_DLC_FILTER_BEGIN - protected by apMutex.
// Default ON for backward compatibility with older APWorlds.
bool apIncludeDlc = true;
// AP_RANK_THRESHOLD_BEGIN - protected by apMutex.
// 0=D, 1=C, 2=B, 3=A. Default A preserves old behavior.
int apRequiredRankForAllChecks = 3;

// Stable Rank D location IDs; every group is D,C,B,A (four consecutive IDs).
constexpr std::int64_t kRankGroupBases[] = {
    88000372LL, 88000377LL, 88000382LL, 88000387LL, 88000392LL, 88000397LL,
    88000402LL, 88000407LL, 88000412LL, 88000417LL, 88000422LL, 88000432LL,
    88000437LL, 88000442LL, 88000447LL, 88000452LL, 88000457LL, 88000462LL,
    88000467LL, 88000472LL, 88000477LL, 88000482LL, 88000487LL, 88000492LL,
    88000497LL, 88000502LL, 88000507LL, 88000512LL, 88000517LL, 88000522LL,
    88000527LL, 88000532LL, 88000537LL, 88000542LL, 88000547LL, 88000552LL,
    88000557LL, 88000562LL, 88000567LL, 88000572LL, 88000577LL, 88000582LL,
    88000587LL, 88000592LL, 88000597LL, 88000602LL, 88000607LL, 88000612LL,
    88000617LL, 88000622LL, 88000627LL, 88000632LL, 88000637LL, 88000642LL,
    88000647LL, 88000652LL, 88000657LL, 88000662LL, 88000667LL,

    // AP V0.8.3 DLC rank group hotfix
    88000805LL, 88000810LL, 88000815LL, 88000820LL, 88000825LL, 88000830LL,
    88000835LL, 88000840LL, 88000845LL, 88000850LL, 88000855LL, 88000860LL,
    88000865LL, 88000870LL, 88000875LL, 88000880LL, 88000885LL, 88000890LL,
    88000895LL, 88000900LL, 88000905LL, 88000910LL, 88000915LL, 88000920LL,
    88000925LL,
};

bool GetRankLocationGroup(std::int64_t id, std::int64_t& base, int& rankIndex)
{
    for (const auto candidate : kRankGroupBases)
    {
        if (id >= candidate && id < candidate + 4)
        {
            base = candidate;
            rankIndex = static_cast<int>(id - candidate);
            return true;
        }
    }
    return false;
}
// AP_RANK_THRESHOLD_END


bool IsDlcLocation(std::int64_t id)
{
    return
        (id >= 88000386LL && id <= 88000400LL) ||
        (id >= 88000441LL && id <= 88000445LL) ||
        (id >= 88000451LL && id <= 88000455LL) ||
        (id >= 88000461LL && id <= 88000470LL) ||
        (id >= 88000476LL && id <= 88000480LL) ||
        (id >= 88000486LL && id <= 88000490LL) ||
        (id >= 88000501LL && id <= 88000505LL) ||
        (id >= 88000511LL && id <= 88000525LL) ||
        (id >= 88000531LL && id <= 88000540LL) ||
        (id >= 88000551LL && id <= 88000560LL) ||
        (id >= 88000566LL && id <= 88000575LL) ||
        (id >= 88000581LL && id <= 88000590LL) ||
        (id >= 88000596LL && id <= 88000605LL) ||
        (id >= 88000616LL && id <= 88000630LL) ||
        (id >= 88000641LL && id <= 88000645LL) ||
        (id >= 88000651LL && id <= 88000655LL) ||
        (id >= 88000859LL && id <= 88000928LL)
    ;
}

bool IsLocationAllowed(std::int64_t id)
{
    return apIncludeDlc || !IsDlcLocation(id);
}
// AP_DLC_FILTER_END

    // Nombre de morts locales attendant un envoi Death Link.
    std::uint32_t apPendingDeathLinks = 0;

// Morts recues depuis Archipelago, protegees par apMutex.
std::uint32_t apPendingIncomingDeathLinks = 0;
// AP_DEATHLINK_HUB_GUARD_V1: all fields below are protected by apMutex.
// The stage flag is driven from game-thread player hooks, never inspected
// from the network thread through the game document's memory.
std::string apDeathLinkCurrentStage;
bool apDeathLinkStageEligible = false;
std::chrono::steady_clock::time_point apDeathLinkStageSeenAt{};


    std::unordered_set<std::int64_t> apPending;
    std::unordered_set<std::int64_t> apConfirmed;
    std::unordered_set<std::int64_t> apSent;

    // Protection contre l'ecrasement d'un historique de checks illisible.
    bool apCheckStateLoadedSuccessfully = false;

    bool apAuthenticated = false;
    bool apStarted = false;
    bool apDisconnectRequested = false;
    bool* apSenderStopFlag = nullptr;

    // WebSocket actif, protege par apMutex.
    HINTERNET apActiveSocket = nullptr;

    enum class APConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting
    };

    APConnectionState apConnectionState = APConnectionState::Disconnected;

    // IDs stables des quatre Shoes dans sonic_unleashed/Items.py.
    constexpr std::int64_t kStompItemId = 88001004;
    constexpr std::int64_t kWallJumpItemId = 88001005;
    constexpr std::int64_t kAirBoostItemId = 88001006;
    constexpr std::int64_t kLightSpeedDashItemId = 88001007;

    // Masque partage entre thread reseau et thread jeu. Protege par apMutex.
    std::uint8_t apReceivedShoes = 0;
    // AP_SHOES_SYNC_V082: synchronisation autorisee apres l'inventaire complet.
    bool apShoeInventoryReady = false;

    // AP Progressive Medals
    constexpr std::int64_t kSunMedalItemId = 88001001;
    constexpr std::int64_t kMoonMedalItemId = 88001002;

    // AP Rings items.
    constexpr std::int64_t kRing10ItemId = 88001008;
    constexpr std::int64_t kRing50ItemId = 88001009;
    constexpr std::int64_t kRing100ItemId = 88001010;

    // Index uniques des items recus depuis Archipelago.
    // Proteges par apMutex.
    std::unordered_set<std::int64_t> apReceivedMedalIndices;

    // AP Rings: indexes recus et bonus en attente (proteges par apMutex).
    std::unordered_set<std::int64_t> apReceivedRingIndices;
    std::unordered_set<std::int64_t> apAppliedRingIndices;
    std::unordered_map<std::int64_t, std::uint32_t> apRingAmounts;
    std::unordered_set<std::int64_t> apRingIndicesInFlight;
    std::uint32_t apPendingRings = 0;

    // AP Ring Wipe Trap : retire tous les Rings du joueur.
    constexpr std::int64_t kRingWipeTrapItemId = 88001024;

    // Chaque index correspond a un Trap recu.
    // Variables protegees par apMutex.
    std::unordered_set<std::int64_t> apReceivedRingWipeIndices;
    std::unordered_set<std::int64_t> apAppliedRingWipeIndices;
    std::unordered_set<std::int64_t> apRingWipeIndicesInFlight;
    std::uint32_t apPendingRingWipes = 0;
    bool apRingWipeStateLoadedSuccessfully = false;
    // AP Reverse Controls Trap : inversion du stick gauche pendant 10 secondes.
    constexpr std::int64_t kFreezeTrapItemId = 88001027;
constexpr std::int64_t kBurnTrapItemId = 88001028;
constexpr std::int64_t kParalyzedTrapItemId = 88001029;

std::unordered_set<std::int64_t> apReceivedBurnIndices;
std::unordered_set<std::int64_t> apAppliedBurnIndices;
std::unordered_set<std::int64_t> apBurnIndicesInFlight;
std::uint32_t apPendingBurn = 0;
bool apBurnStateLoadedSuccessfully = false;

std::unordered_set<std::int64_t> apReceivedParalyzedIndices;
std::unordered_set<std::int64_t> apAppliedParalyzedIndices;
std::unordered_set<std::int64_t> apParalyzedIndicesInFlight;
std::uint32_t apPendingParalyzed = 0;
bool apParalyzedStateLoadedSuccessfully = false;
std::unordered_set<std::int64_t> apReceivedFreezeIndices;
std::unordered_set<std::int64_t> apAppliedFreezeIndices;
std::unordered_set<std::int64_t> apFreezeIndicesInFlight;
std::uint32_t apPendingFreeze = 0;
bool apFreezeStateLoadedSuccessfully = false;
constexpr std::int64_t kReverseControlsTrapItemId = 88001025;

    // AP QTE of Doom Trap
    constexpr std::int64_t kQteOfDoomTrapItemId = 88001026;
    std::uint64_t apQteOfDoomGeneration = 0;

    // Protegees par apMutex.
    std::unordered_set<std::int64_t> apReceivedReverseControlsIndices;
    std::unordered_set<std::int64_t> apAppliedReverseControlsIndices;
    std::unordered_set<std::int64_t> apReverseControlsIndicesInFlight;
    std::uint32_t apPendingReverseControls = 0;
    bool apReverseControlsStateLoadedSuccessfully = false;
    // AP QTE of Doom : reception et execution persistantes.
    // Toutes ces variables sont protegees par apMutex.
    std::unordered_set<std::int64_t> apReceivedQteOfDoomIndices;
    std::unordered_set<std::int64_t> apAppliedQteOfDoomIndices;
    std::unordered_set<std::int64_t> apQteOfDoomIndicesInFlight;
    std::uint32_t apPendingQteOfDoom = 0;
    bool apQteOfDoomStateLoadedSuccessfully = false;

    // AP Life: recompenses recues, appliquees et en attente.
    constexpr std::int64_t kLifeItemId = 88001011;

    // AP Shield Charge : recharge du bouclier du Werehog.
    constexpr std::int64_t kShieldChargeItemId = 88001021;
    std::unordered_set<std::int64_t> apReceivedShieldChargeIndices;
    std::unordered_set<std::int64_t> apAppliedShieldChargeIndices;
    std::unordered_set<std::int64_t> apShieldChargeIndicesInFlight;
    std::uint32_t apPendingShieldCharges = 0;
    bool apShieldChargeStateLoadedSuccessfully = false;
    // AP Full Ring : soin complet du Werehog.
    constexpr std::int64_t kFullRingItemId = 88001020;

    // AP Dark Gaia Energy : recharge complete de la jauge Unleash.
    constexpr std::int64_t kDarkGaiaEnergyItemId = 88001016;
constexpr std::int64_t kSuperClawsItemId = 88001014;

// AP Power Shield : protection temporaire du Werehog.
constexpr std::int64_t kPowerShieldItemId = 88001015;
std::unordered_set<std::int64_t> apReceivedPowerShieldIndices;
std::unordered_set<std::int64_t> apAppliedPowerShieldIndices;
std::unordered_set<std::int64_t> apPowerShieldIndicesInFlight;
std::uint32_t apPendingPowerShield = 0;
bool apPowerShieldStateLoadedSuccessfully = false;
    std::unordered_set<std::int64_t> apReceivedDarkGaiaEnergyIndices;
    std::unordered_set<std::int64_t> apAppliedDarkGaiaEnergyIndices;
    std::unordered_set<std::int64_t> apDarkGaiaEnergyIndicesInFlight;
    std::uint32_t apPendingDarkGaiaEnergy = 0;
    bool apDarkGaiaEnergyStateLoadedSuccessfully = false;
std::unordered_set<std::int64_t> apReceivedSuperClawsIndices;
std::unordered_set<std::int64_t> apAppliedSuperClawsIndices;
std::unordered_set<std::int64_t> apSuperClawsIndicesInFlight;
std::uint32_t apPendingSuperClaws = 0;
bool apSuperClawsStateLoadedSuccessfully = false;

    std::unordered_set<std::int64_t> apReceivedFullRingIndices;
    std::unordered_set<std::int64_t> apAppliedFullRingIndices;
    std::unordered_set<std::int64_t> apFullRingIndicesInFlight;
    std::uint32_t apPendingFullRings = 0;
    bool apFullRingStateLoadedSuccessfully = false;

    // AP Crystal XP : identifiants provisoires.
    constexpr std::int64_t kCrystalXP50ItemId = 88001017;
    constexpr std::int64_t kCrystalXP100ItemId = 88001018;
    constexpr std::int64_t kCrystalXP200ItemId = 88001019;
    // AP Consumables : IDs des objets Archipelago.
    constexpr std::int64_t kHotdogItemId = 88001013;
    constexpr std::int64_t kFriedChickenItemId = 88001022;
    constexpr std::int64_t kHeroSandwichItemId = 88001023;
    std::unordered_set<std::int64_t> apReceivedLifeIndices;
    std::unordered_set<std::int64_t> apAppliedLifeIndices;
    std::unordered_set<std::int64_t> apLifeIndicesInFlight;
    std::uint32_t apPendingLives = 0;

    // AP Crystal XP : recompenses recues et appliquees.
    // Toutes ces variables sont protegees par apMutex.
    std::unordered_set<std::int64_t> apReceivedCrystalXPIndices;
    std::unordered_set<std::int64_t> apAppliedCrystalXPIndices;
    std::unordered_set<std::int64_t> apCrystalXPIndicesInFlight;
    std::unordered_map<std::int64_t, std::uint32_t> apCrystalXPAmounts;

    // AP Consumables : historique et distribution par index.
    std::unordered_set<std::int64_t> apReceivedConsumableIndices;
    std::unordered_set<std::int64_t> apAppliedConsumableIndices;
    std::unordered_set<std::int64_t> apConsumableIndicesInFlight;
    std::unordered_map<std::int64_t, std::uint32_t> apConsumableItemIds;
    bool apConsumableStateLoadedSuccessfully = false;
    std::uint32_t apPendingCrystalXP = 0;
    bool apCrystalXPStateLoadedSuccessfully = false;
    bool apLifeStateLoadedSuccessfully = false;
    std::string apSeedName;
    // Vrai uniquement apres validation complete du fichier de Rings.
    bool apRingStateLoadedSuccessfully = false;

    // A appeler uniquement lorsque apMutex est verrouille.
    json BuildRingStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["rings"] = json::array();

        for (const auto& [index, amount] : apRingAmounts)
        {
            state["rings"].push_back({
                {"index", index},
                {"amount", amount},
                {"applied", apAppliedRingIndices.contains(index)}
            });
        }

        return state;
    }

    // AP Life: historique des recompenses par seed.
    json BuildLifeStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["lives"] = json::array();

        for (const auto index : apReceivedLifeIndices)
        {
            state["lives"].push_back({
                {"index", index},
                {"applied", apAppliedLifeIndices.contains(index)}
            });
        }

        return state;
    }

    // AP Full Ring : historique des recompenses par seed.
    // A appeler uniquement avec apMutex verrouille.
    // AP Dark Gaia Energy : historique des items par seed.
    // A appeler uniquement avec apMutex verrouille.
    json BuildDarkGaiaEnergyStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["dark_gaia_energy"] = json::array();

        for (const auto index : apReceivedDarkGaiaEnergyIndices)
        {
            state["dark_gaia_energy"].push_back({
                {"index", index},
                {"applied", apAppliedDarkGaiaEnergyIndices.contains(index)}
            });
        }

        return state;
    }
    json BuildPowerShieldStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["power_shield"] = json::array();

        for (const auto index : apReceivedPowerShieldIndices)
        {
            state["power_shield"].push_back({
                {"index", index},
                {"applied", apAppliedPowerShieldIndices.contains(index)}
            });
        }

        return state;
    }
    json BuildSuperClawsStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["super_claws"] = json::array();
        for (const auto index : apReceivedSuperClawsIndices)
        {
            state["super_claws"].push_back({
                {"index", index},
                {"applied", apAppliedSuperClawsIndices.contains(index)}
            });
        }
        return state;
    }
    json BuildShieldChargeStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["shield_charges"] = json::array();

        for (const auto index : apReceivedShieldChargeIndices)
        {
            state["shield_charges"].push_back({
                {"index", index},
                {"applied", apAppliedShieldChargeIndices.contains(index)}
            });
        }

        return state;
    }
    json BuildFullRingStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["full_rings"] = json::array();

        for (const auto index : apReceivedFullRingIndices)
        {
            state["full_rings"].push_back({
                {"index", index},
                {"applied", apAppliedFullRingIndices.contains(index)}
            });
        }

        return state;
    }
    // AP Crystal XP : historique des recompenses par seed.
    // A appeler uniquement avec apMutex verrouille.
    json BuildCrystalXPStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["crystal_xp"] = json::array();

        for (const auto& [index, amount] : apCrystalXPAmounts)
        {
            state["crystal_xp"].push_back({
                {"index", index},
                {"amount", amount},
                {"applied", apAppliedCrystalXPIndices.contains(index)}
            });
        }

        return state;
    }

    // AP Consumables : historique des objets recus par seed et slot.
    // A appeler uniquement avec apMutex verrouille.
    json BuildConsumableStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["consumables"] = json::array();

        for (const auto& [index, itemId] : apConsumableItemIds)
        {
            state["consumables"].push_back({
                {"index", index},
                {"item_id", itemId},
                {"applied", apAppliedConsumableIndices.contains(index)}
            });
        }

        return state;
    }

    std::filesystem::path GetConsumableStatePath()
    {
        return GetUserPath() /
            ("archipelago_consumables_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetCrystalXPStatePath()
    {
        return GetUserPath() /
            ("archipelago_crystal_xp_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetLifeStatePath()
    {
        return GetUserPath() / ("archipelago_lives_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }

    // Sauvegarde des checks propre a chaque seed et slot.
    std::filesystem::path GetCheckStatePath()
    {
        return GetUserPath() /
            ("archipelago_checks_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }
    // AP Ring Wipe Trap : historique distinct par seed et slot.
    std::filesystem::path GetRingWipeStatePath()
    {
        return GetUserPath() /
            ("archipelago_ring_wipe_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetRingStatePath()
    {
        return GetUserPath() / ("archipelago_rings_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }

    std::uint32_t apSunMedalItems = 0;
    std::uint32_t apMoonMedalItems = 0;

    std::uint8_t ShoeBitForItem(std::int64_t itemId)
    {
        if (kStompItemId >= 0 && itemId == kStompItemId) return 0x01;
        if (kWallJumpItemId >= 0 && itemId == kWallJumpItemId) return 0x02;
        if (kAirBoostItemId >= 0 && itemId == kAirBoostItemId) return 0x04;
        if (kLightSpeedDashItemId >= 0 && itemId == kLightSpeedDashItemId) return 0x08;
        return 0;
    }

    // A appeler uniquement avec apMutex verrouille.
    // A appeler uniquement avec apMutex verrouille.
    // AP Ring Wipe Trap : construire l'historique JSON.
    // A appeler uniquement avec apMutex verrouille.
    json BuildRingWipeStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["traps"] = json::array();

        for (const auto index : apReceivedRingWipeIndices)
        {
            state["traps"].push_back({
                {"index", index},
                {"applied", apAppliedRingWipeIndices.contains(index)}
            });
        }

        return state;
    }
    // AP Ring Wipe Trap : restaurer les traps recus.
    // A appeler uniquement avec apMutex verrouille.
    bool LoadRingWipeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetRingWipeStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("traps") ||
                !state["traps"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& trap : state["traps"])
            {
                const auto index = trap.at("index").get<std::int64_t>();
                const bool wasApplied = trap.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedRingWipeIndices = std::move(received);
            apAppliedRingWipeIndices = std::move(applied);
            apPendingRingWipes = pending;
            apRingWipeStateLoadedSuccessfully = true;

            std::printf(
                "AP RING WIPE LOADED: %zu traps, pending=%u\n",
                apReceivedRingWipeIndices.size(),
                apPendingRingWipes
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP RING WIPE LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Reverse Controls Trap : historique par seed et slot.
    std::filesystem::path GetReverseControlsStatePath()
    {
        return GetUserPath() /
            ("archipelago_reverse_controls_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }

    // A appeler uniquement avec apMutex verrouille.
    json BuildReverseControlsStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["traps"] = json::array();

        for (const auto index : apReceivedReverseControlsIndices)
        {
            state["traps"].push_back({
                {"index", index},
                {"applied", apAppliedReverseControlsIndices.contains(index)}
            });
        }

        return state;
    }

    // A appeler uniquement avec apMutex verrouille.
    bool LoadReverseControlsState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetReverseControlsStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("traps") ||
                !state["traps"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& trap : state["traps"])
            {
                const auto index = trap.at("index").get<std::int64_t>();
                const bool wasApplied = trap.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedReverseControlsIndices = std::move(received);
            apAppliedReverseControlsIndices = std::move(applied);
            apPendingReverseControls = pending;
            apReverseControlsStateLoadedSuccessfully = true;

            std::printf(
                "AP REVERSE LOADED: %zu traps, pending=%u\n",
                apReceivedReverseControlsIndices.size(),
                apPendingReverseControls
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP REVERSE LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Freeze Trap : historique par seed et slot.
    std::filesystem::path GetFreezeStatePath()
    {
        return GetUserPath() /
            ("archipelago_freeze_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }

    // A appeler uniquement avec apMutex verrouille.
    json BuildFreezeStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["traps"] = json::array();

        for (const auto index : apReceivedFreezeIndices)
        {
            state["traps"].push_back({
                {"index", index},
                {"applied", apAppliedFreezeIndices.contains(index)}
            });
        }

        return state;
    }

    // A appeler uniquement avec apMutex verrouille.
    bool LoadFreezeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetFreezeStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("traps") ||
                !state["traps"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& trap : state["traps"])
            {
                const auto index = trap.at("index").get<std::int64_t>();
                const bool wasApplied = trap.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedFreezeIndices = std::move(received);
            apAppliedFreezeIndices = std::move(applied);
            apPendingFreeze = pending;
            apFreezeStateLoadedSuccessfully = true;

            std::printf(
                "AP FREEZE LOADED: %zu traps, pending=%u\n",
                apReceivedFreezeIndices.size(),
                apPendingFreeze
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP FREEZE LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Burn Trap : historique par seed et slot.
    std::filesystem::path GetBurnStatePath()
    {
        return GetUserPath() /
            ("archipelago_burn_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }

    // A appeler uniquement avec apMutex verrouille.
    json BuildBurnStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["traps"] = json::array();

        for (const auto index : apReceivedBurnIndices)
        {
            state["traps"].push_back({
                {"index", index},
                {"applied", apAppliedBurnIndices.contains(index)}
            });
        }

        return state;
    }

    // A appeler uniquement avec apMutex verrouille.
    bool LoadBurnState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetBurnStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("traps") ||
                !state["traps"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& trap : state["traps"])
            {
                const auto index = trap.at("index").get<std::int64_t>();
                const bool wasApplied = trap.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedBurnIndices = std::move(received);
            apAppliedBurnIndices = std::move(applied);
            apPendingBurn = pending;
            apBurnStateLoadedSuccessfully = true;

            std::printf(
                "AP BURN LOADED: %zu traps, pending=%u\n",
                apReceivedBurnIndices.size(),
                apPendingBurn
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP BURN LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Paralyzed Trap : historique par seed et slot.
    std::filesystem::path GetParalyzedStatePath()
    {
        return GetUserPath() /
            ("archipelago_paralyzed_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }

    // A appeler uniquement avec apMutex verrouille.
    json BuildParalyzedStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["traps"] = json::array();

        for (const auto index : apReceivedParalyzedIndices)
        {
            state["traps"].push_back({
                {"index", index},
                {"applied", apAppliedParalyzedIndices.contains(index)}
            });
        }

        return state;
    }

    // A appeler uniquement avec apMutex verrouille.
    bool LoadParalyzedState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetParalyzedStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("traps") ||
                !state["traps"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& trap : state["traps"])
            {
                const auto index = trap.at("index").get<std::int64_t>();
                const bool wasApplied = trap.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedParalyzedIndices = std::move(received);
            apAppliedParalyzedIndices = std::move(applied);
            apPendingParalyzed = pending;
            apParalyzedStateLoadedSuccessfully = true;

            std::printf(
                "AP PARALYZED LOADED: %zu traps, pending=%u\n",
                apReceivedParalyzedIndices.size(),
                apPendingParalyzed
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP PARALYZED LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP QTE of Doom : historique par seed et slot.

    std::filesystem::path GetQteOfDoomStatePath()
    {
        return GetUserPath() /
            ("archipelago_qte_of_doom_" + apSeedName + "_" +
             Config::ArchipelagoSlot.Value + ".json");
    }

    // A appeler uniquement avec apMutex verrouille.
    json BuildQteOfDoomStateJson()
    {
        json state;
        state["version"] = 1;
        state["seed_name"] = apSeedName;
        state["slot"] = Config::ArchipelagoSlot.Value;
        state["traps"] = json::array();

        for (const auto index : apReceivedQteOfDoomIndices)
        {
            state["traps"].push_back({
                {"index", index},
                {"applied", apAppliedQteOfDoomIndices.contains(index)}
            });
        }

        return state;
    }

    // A appeler uniquement avec apMutex verrouille.
    bool LoadQteOfDoomState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetQteOfDoomStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("traps") ||
                !state["traps"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& trap : state["traps"])
            {
                const auto index = trap.at("index").get<std::int64_t>();
                const bool wasApplied = trap.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedQteOfDoomIndices = std::move(received);
            apAppliedQteOfDoomIndices = std::move(applied);
            apPendingQteOfDoom = pending;
            apQteOfDoomStateLoadedSuccessfully = true;

            std::printf(
                "AP QTE DOOM LOADED: %zu traps, pending=%u\n",
                apReceivedQteOfDoomIndices.size(),
                apPendingQteOfDoom
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP QTE DOOM LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    bool LoadRingState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetRingStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("rings") ||
                !state["rings"].is_array())
                return false;

            std::unordered_map<std::int64_t, std::uint32_t> amounts;
            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& ring : state["rings"])
            {
                const auto index = ring.at("index").get<std::int64_t>();
                const auto amount = ring.at("amount").get<std::uint32_t>();
                const bool wasApplied = ring.at("applied").get<bool>();

                if (index < 0 || (amount != 10 && amount != 50 && amount != 100))
                    return false;

                if (!amounts.emplace(index, amount).second)
                    return false;

                received.insert(index);

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending > UINT32_MAX - amount)
                        return false;
                    pending += amount;
                }
            }

            apRingAmounts = std::move(amounts);
            apReceivedRingIndices = std::move(received);
            apAppliedRingIndices = std::move(applied);
            apPendingRings = pending;
            // Historique JSON valide : autoriser les futures sauvegardes.
            apRingStateLoadedSuccessfully = true;

            std::printf("AP RINGS LOADED: %zu items, pending=%u\n",
                apRingAmounts.size(), apPendingRings);
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP RINGS LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Life: recharger les recompenses de cette seed.
    // AP Full Ring : fichier de sauvegarde par seed.
    // AP Dark Gaia Energy : sauvegarde separee pour chaque seed.
    std::filesystem::path GetPowerShieldStatePath()
    {
        return GetUserPath() /
            ("archipelago_power_shield_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetSuperClawsStatePath()
    {
        return GetUserPath() / ("archipelago_super_claws_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetDarkGaiaEnergyStatePath()
    {
        return GetUserPath() /
            ("archipelago_dark_gaia_energy_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetShieldChargeStatePath()
    {
        return GetUserPath() /
            ("archipelago_shield_charge_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }
    std::filesystem::path GetFullRingStatePath()
    {
        return GetUserPath() /
            ("archipelago_full_rings_" + apSeedName + "_" + Config::ArchipelagoSlot.Value + ".json");
    }
    bool LoadLifeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetLifeStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("lives") ||
                !state["lives"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& life : state["lives"])
            {
                const auto index = life.at("index").get<std::int64_t>();
                const bool wasApplied = life.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;
                    ++pending;
                }
            }

            apReceivedLifeIndices = std::move(received);
            apAppliedLifeIndices = std::move(applied);
            apLifeIndicesInFlight.clear();
            apPendingLives = pending;
            apLifeStateLoadedSuccessfully = true;

            std::printf("AP LIFE LOADED: %zu items, pending=%u\n",
                apReceivedLifeIndices.size(), apPendingLives);
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP LIFE LOAD ERROR: %s\n", e.what());
            return false;
        }
    }

    // AP Full Ring : restaurer les recompenses de cette seed.
    // AP Dark Gaia Energy : restaurer les recompenses de cette seed.
    // A appeler uniquement avec apMutex verrouille.
    bool LoadDarkGaiaEnergyState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetDarkGaiaEnergyStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("dark_gaia_energy") ||
                !state["dark_gaia_energy"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& item : state["dark_gaia_energy"])
            {
                const auto index = item.at("index").get<std::int64_t>();
                const bool wasApplied = item.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedDarkGaiaEnergyIndices = std::move(received);
            apAppliedDarkGaiaEnergyIndices = std::move(applied);
            apDarkGaiaEnergyIndicesInFlight.clear();
            apPendingDarkGaiaEnergy = pending;
            apDarkGaiaEnergyStateLoadedSuccessfully = true;

            std::printf(
                "AP DARK GAIA ENERGY LOADED: %zu items, pending=%u\n",
                apReceivedDarkGaiaEnergyIndices.size(),
                apPendingDarkGaiaEnergy
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP DARK GAIA ENERGY LOAD ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool LoadPowerShieldState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetPowerShieldStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("power_shield") ||
                !state["power_shield"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& item : state["power_shield"])
            {
                const auto index = item.at("index").get<std::int64_t>();
                const bool wasApplied = item.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedPowerShieldIndices = std::move(received);
            apAppliedPowerShieldIndices = std::move(applied);
            apPowerShieldIndicesInFlight.clear();
            apPendingPowerShield = pending;
            apPowerShieldStateLoadedSuccessfully = true;

            std::printf(
                "AP POWER SHIELD LOADED: %zu items, pending=%u\n",
                apReceivedPowerShieldIndices.size(),
                apPendingPowerShield
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP POWER SHIELD LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    bool LoadSuperClawsState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetSuperClawsStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("super_claws") ||
                !state["super_claws"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& item : state["super_claws"])
            {
                const auto index = item.at("index").get<std::int64_t>();
                const bool wasApplied = item.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedSuperClawsIndices = std::move(received);
            apAppliedSuperClawsIndices = std::move(applied);
            apSuperClawsIndicesInFlight.clear();
            apPendingSuperClaws = pending;
            apSuperClawsStateLoadedSuccessfully = true;

            std::printf(
                "AP SUPER CLAWS LOADED: %zu items, pending=%u\n",
                apReceivedSuperClawsIndices.size(),
                apPendingSuperClaws
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP SUPER CLAWS LOAD ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool LoadShieldChargeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetShieldChargeStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("shield_charges") ||
                !state["shield_charges"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& charge : state["shield_charges"])
            {
                const auto index = charge.at("index").get<std::int64_t>();
                const bool wasApplied = charge.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedShieldChargeIndices = std::move(received);
            apAppliedShieldChargeIndices = std::move(applied);
            apShieldChargeIndicesInFlight.clear();
            apPendingShieldCharges = pending;
            apShieldChargeStateLoadedSuccessfully = true;

            std::printf(
                "AP SHIELD CHARGE LOADED: %zu items, pending=%u\n",
                apReceivedShieldChargeIndices.size(),
                apPendingShieldCharges
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP SHIELD CHARGE LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    bool LoadFullRingState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetFullRingStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("full_rings") ||
                !state["full_rings"].is_array())
                return false;

            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& ring : state["full_rings"])
            {
                const auto index = ring.at("index").get<std::int64_t>();
                const bool wasApplied = ring.at("applied").get<bool>();

                if (index < 0 || !received.insert(index).second)
                    return false;

                if (wasApplied)
                    applied.insert(index);
                else
                {
                    if (pending == UINT32_MAX)
                        return false;

                    ++pending;
                }
            }

            apReceivedFullRingIndices = std::move(received);
            apAppliedFullRingIndices = std::move(applied);
            apFullRingIndicesInFlight.clear();
            apPendingFullRings = pending;
            apFullRingStateLoadedSuccessfully = true;

            std::printf(
                "AP FULL RING LOADED: %zu items, pending=%u\n",
                apReceivedFullRingIndices.size(),
                apPendingFullRings
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP FULL RING LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Crystal XP : recharger les recompenses de cette seed.
    // A appeler uniquement avec apMutex verrouille.
    // AP Consumables : chargement de l'historique par seed et slot.
    // A appeler uniquement avec apMutex verrouille.
    bool LoadConsumableState()
    {
        apConsumableStateLoadedSuccessfully = false;

        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetConsumableStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("consumables") ||
                !state["consumables"].is_array())
                return false;

            std::unordered_map<std::int64_t, std::uint32_t> itemIds;
            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;

            for (const auto& consumable : state["consumables"])
            {
                const auto index = consumable.at("index").get<std::int64_t>();
                const auto itemId = consumable.at("item_id").get<std::uint32_t>();
                const bool wasApplied = consumable.at("applied").get<bool>();

                if (index < 0 ||
                    (itemId != 111 && itemId != 104 && itemId != 101) ||
                    !received.insert(index).second)
                    return false;

                itemIds.emplace(index, itemId);

                if (wasApplied)
                    applied.insert(index);
            }

            apConsumableItemIds = std::move(itemIds);
            apReceivedConsumableIndices = std::move(received);
            apAppliedConsumableIndices = std::move(applied);
            apConsumableIndicesInFlight.clear();
            apConsumableStateLoadedSuccessfully = true;

            std::printf(
                "AP CONSUMABLES LOADED: %zu items\n",
                apConsumableItemIds.size()
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP CONSUMABLE LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    bool LoadCrystalXPState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            std::ifstream file(GetCrystalXPStatePath(), std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string("")) != apSeedName ||
                state.value("slot", std::string("")) != Config::ArchipelagoSlot.Value ||
                !state.contains("crystal_xp") ||
                !state["crystal_xp"].is_array())
                return false;

            std::unordered_map<std::int64_t, std::uint32_t> amounts;
            std::unordered_set<std::int64_t> received;
            std::unordered_set<std::int64_t> applied;
            std::uint32_t pending = 0;

            for (const auto& crystal : state["crystal_xp"])
            {
                const auto index = crystal.at("index").get<std::int64_t>();
                const auto amount = crystal.at("amount").get<std::uint32_t>();
                const bool wasApplied = crystal.at("applied").get<bool>();

                if (index < 0 ||
                    (amount != 50 && amount != 100 && amount != 200) ||
                    !received.insert(index).second)
                    return false;

                amounts.emplace(index, amount);

                if (wasApplied)
                {
                    applied.insert(index);
                }
                else
                {
                    if (pending > UINT32_MAX - amount)
                        return false;

                    pending += amount;
                }
            }

            apCrystalXPAmounts = std::move(amounts);
            apReceivedCrystalXPIndices = std::move(received);
            apAppliedCrystalXPIndices = std::move(applied);
            apCrystalXPIndicesInFlight.clear();
            apPendingCrystalXP = pending;
            apCrystalXPStateLoadedSuccessfully = true;

            std::printf(
                "AP CRYSTAL XP LOADED: %zu items, pending=%u\n",
                apCrystalXPAmounts.size(),
                apPendingCrystalXP
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP CRYSTAL XP LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Crystal XP : sauvegarde atomique par seed.
    // A appeler uniquement avec apMutex verrouille.
    // AP Consumables : sauvegarde persistante par seed et slot.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveConsumableState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetConsumableStatePath();

            // Ne jamais ecraser un historique existant non charge.
            if (std::filesystem::exists(path) &&
                !apConsumableStateLoadedSuccessfully)
                return false;
            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildConsumableStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return false;
            }

            apConsumableStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP CONSUMABLE SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool SaveCrystalXPState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetCrystalXPStatePath();

            // Ne pas ecraser un historique existant invalide.
            if (std::filesystem::exists(path) &&
                !apCrystalXPStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildCrystalXPStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return false;
            }

            apCrystalXPStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP CRYSTAL XP SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    // A appeler avec apMutex deja verrouille.
    // A appeler avec apMutex deja verrouille.
    bool LoadCheckState()
    {
        apCheckStateLoadedSuccessfully = false;
        apPending.clear();

        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetCheckStatePath();

            if (!std::filesystem::exists(path))
            {
                apCheckStateLoadedSuccessfully = true;
                return true;
            }

            std::ifstream file(path, std::ios::binary);
            if (!file)
                return false;

            const json state = json::parse(file);

            if (!state.is_object() ||
                state.value("version", 0) != 1 ||
                state.value("seed_name", std::string()) != apSeedName ||
                state.value("slot", std::string()) !=
                    Config::ArchipelagoSlot.Value ||
                !state.contains("pending") ||
                !state["pending"].is_array())
                return false;

            std::unordered_set<std::int64_t> restored;

            for (const auto& entry : state["pending"])
            {
                if (!entry.is_number_integer())
                    return false;

                restored.insert(entry.get<std::int64_t>());
            }

            apPending = std::move(restored);
            apCheckStateLoadedSuccessfully = true;

            std::printf(
                "AP CHECK LOAD: %zu pending checks\n",
                apPending.size()
            );

            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP CHECK LOAD ERROR: %s\n", e.what());
            return false;
        }
    }
    bool SaveCheckState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetCheckStatePath();

            // Ne pas ecraser un historique existant illisible.
            if (std::filesystem::exists(path) &&
                !apCheckStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            json state;
            state["version"] = 1;
            state["seed_name"] = apSeedName;
            state["slot"] = Config::ArchipelagoSlot.Value;
            state["pending"] = json::array();

            for (const auto id : apPending)
                state["pending"].push_back(id);

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << state.dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apCheckStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP CHECK SAVE ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Ring Wipe Trap : sauvegarde atomique par seed et slot.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveRingWipeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetRingWipeStatePath();

            // Ne jamais ecraser un historique dont le chargement a echoue.
            if (std::filesystem::exists(path) &&
                !apRingWipeStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildRingWipeStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apRingWipeStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP RING WIPE SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    // AP Reverse Controls Trap : sauvegarde atomique par seed et slot.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveReverseControlsState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetReverseControlsStatePath();

            // Ne jamais ecraser un historique dont le chargement a echoue.
            if (std::filesystem::exists(path) &&
                !apReverseControlsStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildReverseControlsStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apReverseControlsStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP REVERSE SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    // AP Freeze Trap : sauvegarde atomique.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveFreezeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetFreezeStatePath();

            // Ne jamais ecraser un historique dont le chargement a echoue.
            if (std::filesystem::exists(path) &&
                !apFreezeStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildFreezeStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apFreezeStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP FREEZE SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    // AP Burn Trap : sauvegarde atomique.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveBurnState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetBurnStatePath();

            // Ne jamais ecraser un historique dont le chargement a echoue.
            if (std::filesystem::exists(path) &&
                !apBurnStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildBurnStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apBurnStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP BURN SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    // AP Paralyzed Trap : sauvegarde atomique.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveParalyzedState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetParalyzedStatePath();

            // Ne jamais ecraser un historique dont le chargement a echoue.
            if (std::filesystem::exists(path) &&
                !apParalyzedStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildParalyzedStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apParalyzedStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP PARALYZED SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    // AP QTE of Doom : sauvegarde atomique.
    // A appeler uniquement avec apMutex verrouille.

    bool SaveQteOfDoomState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetQteOfDoomStatePath();

            // Ne jamais ecraser un historique dont le chargement a echoue.
            if (std::filesystem::exists(path) &&
                !apQteOfDoomStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildQteOfDoomStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apQteOfDoomStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP QTE DOOM SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool SaveRingState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetRingStatePath();
            // Ne jamais ecraser un historique existant si son chargement a echoue.
            if (std::filesystem::exists(path) && !apRingStateLoadedSuccessfully)
                return false;
            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file)
                    return false;

                file << BuildRingStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            // Premiere sauvegarde valide : autoriser la distribution des Rings.
            apRingStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP RINGS SAVE ERROR: %s\n", e.what());
            return false;
        }
    }
    // AP Life: sauvegarde atomique de l'historique par seed.
    bool SaveLifeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetLifeStatePath();
            if (std::filesystem::exists(path) && !apLifeStateLoadedSuccessfully)
                return false;
            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file)
                    return false;

                file << BuildLifeStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apLifeStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf("AP LIFE SAVE ERROR: %s\n", e.what());
            return false;
        }
    }

    // AP Full Ring : sauvegarde atomique par seed.
    // A appeler uniquement avec apMutex verrouille.
    // AP Dark Gaia Energy : sauvegarde atomique par seed.
    // A appeler uniquement avec apMutex verrouille.
    bool SaveDarkGaiaEnergyState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetDarkGaiaEnergyStatePath();

            if (std::filesystem::exists(path) &&
                !apDarkGaiaEnergyStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildDarkGaiaEnergyStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apDarkGaiaEnergyStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP DARK GAIA ENERGY SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool SavePowerShieldState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetPowerShieldStatePath();

            if (std::filesystem::exists(path) &&
                !apPowerShieldStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildPowerShieldStateJson().dump(2);
                file.flush();

                if (!file.good())
                    return false;
            }

            std::filesystem::rename(temporary, path);

            apPowerShieldStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP POWER SHIELD SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool SaveSuperClawsState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetSuperClawsStatePath();

            if (std::filesystem::exists(path) &&
                !apSuperClawsStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildSuperClawsStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apSuperClawsStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP SUPER CLAWS SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool SaveShieldChargeState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetShieldChargeStatePath();

            if (std::filesystem::exists(path) &&
                !apShieldChargeStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildShieldChargeStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apShieldChargeStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP SHIELD CHARGE SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    bool SaveFullRingState()
    {
        if (apSeedName.empty())
            return false;

        try
        {
            const auto path = GetFullRingStatePath();

            if (std::filesystem::exists(path) &&
                !apFullRingStateLoadedSuccessfully)
                return false;

            std::filesystem::create_directories(path.parent_path());

            const auto temporary = path.string() + ".tmp";

            {
                std::ofstream file(
                    temporary,
                    std::ios::binary | std::ios::trunc
                );

                if (!file)
                    return false;

                file << BuildFullRingStateJson().dump(2);
                file.flush();

                if (!file)
                    return false;
            }

            if (!MoveFileExA(
                temporary.c_str(),
                path.string().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return false;

            apFullRingStateLoadedSuccessfully = true;
            return true;
        }
        catch (const std::exception& e)
        {
            std::printf(
                "AP FULL RING SAVE ERROR: %s\n",
                e.what()
            );
            return false;
        }
    }
    void Print(const char* message)
    {
        std::printf("%s\n", message);
        std::fflush(stdout);
    }

    std::mutex apSendMutex;

    bool SendJson(HINTERNET socket, const json& packet)
    {
        std::lock_guard<std::mutex> sendLock(apSendMutex);

        const std::string data = packet.dump();

        DWORD result = WinHttpWebSocketSend(
            socket,
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            (PVOID)data.data(),
            static_cast<DWORD>(data.size())
        );

        if (result != NO_ERROR)
        {
            std::printf("AP SEND FAILED: %lu\n", result);
            std::fflush(stdout);
            return false;
        }

        return true;
    }

    void ConfirmLocations(const json& locations)
    {
        if (!locations.is_array())
            return;

        std::lock_guard<std::mutex> lock(apMutex);

        for (const auto& location : locations)
        {
            if (!location.is_number_integer())
                continue;

            std::int64_t id = location.get<std::int64_t>();

            if (apConfirmed.insert(id).second)
            {
                if (const char* medalName = AP_MedalDisplayName(id))
                    std::printf("AP CHECK CONFIRMED: %lld (%s)\n",
                        static_cast<long long>(id), medalName);
                else
                    std::printf("AP CHECK CONFIRMED: %lld\n",
                        static_cast<long long>(id));
            }

            apPending.erase(id);
            apSent.erase(id);
        }

        // AP_GOAL_V1_BEGIN: only server-confirmed boss checks count.
        const std::int64_t bossGoalLocation =
            apSelectedGoal == 0 ? 88000666LL :
            apSelectedGoal == 1 ? 88000661LL : -1LL;
        // AP_MEDAL_GOAL_EXPANDED: include all mapped medal IDs, including side Acts above 88000370.
        if (apSelectedGoal == 2 && !apGoalSent)
        {
            int medalCount = 0;
            for (const auto medalId : apConfirmed)
                if (AP_MedalDisplayName(medalId) != nullptr) ++medalCount;
            if (medalCount >= apMedalGoalTarget) { apGoalPending = true; apWake.notify_all(); }
        }
        if (bossGoalLocation >= 0 &&
            apConfirmed.contains(bossGoalLocation) && !apGoalSent)
        {
            apGoalPending = true;
            apWake.notify_all();
        }
        // AP_GOAL_V1_END
        // Mettre a jour la sauvegarde apres les confirmations.
        if (apCheckStateLoadedSuccessfully && !SaveCheckState())
        {
            std::printf("AP CHECK CONFIRM SAVE FAILED\n");
        }

        std::fflush(stdout);
    }

    void SenderThread(HINTERNET socket, bool& stop)
    {
        while (true)
        {
            std::vector<std::int64_t> batch;
            std::uint32_t deathLinks = 0;

            {
                std::unique_lock<std::mutex> lock(apMutex);

                apWake.wait(lock, [&]()
                {
                    if (stop)
                        return true;

                    if (!apCheckStateLoadedSuccessfully)
                        return false;

                    // AP_GOAL_V1_BEGIN
                    if (apGoalPending && !apGoalSent)
                        return true;
                    // AP_GOAL_V1_END
                    if (apPendingDeathLinks > 0)
                        return true;

                    for (auto id : apPending)
                    {
                        if (IsLocationAllowed(id) &&
                            !apConfirmed.contains(id) &&
                            !apSent.contains(id))
                            return true;
                    }

                    return false;
                });

                if (stop)
                    return;

                if (!apCheckStateLoadedSuccessfully)
                    continue;

                deathLinks = apPendingDeathLinks;
                apPendingDeathLinks = 0;

                for (auto id : apPending)
                {
                    if (IsLocationAllowed(id) &&
                        !apConfirmed.contains(id) &&
                        !apSent.contains(id))
                    {
                        batch.push_back(id);
                        apSent.insert(id);
                    }
                }
            }

            for (std::uint32_t i = 0; i < deathLinks; ++i)
            {
                const auto now = std::chrono::system_clock::now();
                const double timestamp =
                    std::chrono::duration<double>(
                        now.time_since_epoch()).count();

                json deathPacket = json::array({
                    {
                        {"cmd", "Bounce"},
                        {"tags", json::array({"DeathLink"})},
                        {"data", {
                            {"time", timestamp},
                            {"source", Config::ArchipelagoSlot.Value},
                            {"cause", "Sonic has died!"}
                        }}
                    }
                });

                if (!SendJson(socket, deathPacket))
                {
                    LOGFN("[AP DEATH LINK SEND FAILED]");
                    std::fflush(stdout);
                    return;
                }

                LOGFN("[AP DEATH LINK SENT]");
                std::fflush(stdout);
            }

            // AP_GOAL_V1_BEGIN: send victory on the existing socket.
            bool sendGoal = false;
            {
                std::lock_guard<std::mutex> lock(apMutex);
                sendGoal = apGoalPending && !apGoalSent;
            }
            if (sendGoal)
            {
                const json goalPacket = json::array({
                    {{"cmd", "StatusUpdate"}, {"status", 30}}
                });
                if (!SendJson(socket, goalPacket))
                {
                    LOGFN("[AP GOAL SEND FAILED]");
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    apGoalSent = true;
                    apGoalPending = false;
                }
                LOGFN("[AP GOAL SENT]");
            }
            // AP_GOAL_V1_END
            if (batch.empty())
                continue;

            json packet = json::array({
                {
                    {"cmd", "LocationChecks"},
                    {"locations", batch}
                }
            });

            if (!SendJson(socket, packet))
            {
                std::lock_guard<std::mutex> lock(apMutex);

                for (auto id : batch)
                    apSent.erase(id);

                return;
            }

            for (auto id : batch)
            {
                std::printf(
                    "AP CHECK SENT: %lld\n",
                    static_cast<long long>(id)
                );
            }

            std::fflush(stdout);
        }
    }
// AP_MULTIWORLD_HUD_V082_NETWORK_HELPERS
    std::string BoundedName(const std::string& text, std::size_t limit=120)
    {
        if (text.size() <= limit) return text;
        std::size_t n = limit - 3;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0u) == 0x80u) --n;
        return text.substr(0, n) + "...";
    }

    void RememberSlotNames(const json& packet)
    {
        if (packet.contains("players") && packet["players"].is_array())
        {
            for (const auto& player : packet["players"])
            {
                if (!player.is_object() || !player.contains("slot") ||
                    !player["slot"].is_number_integer()) continue;
                const int id = player["slot"].get<int>();
                if (player.contains("alias") && player["alias"].is_string())
                    apMultiPlayerNames[id] = BoundedName(player["alias"].get<std::string>(), 65);
                else if (player.contains("name") && player["name"].is_string())
                    apMultiPlayerNames[id] = BoundedName(player["name"].get<std::string>(), 65);
            }
        }
        if (packet.contains("slot_info") && packet["slot_info"].is_object())
        {
            for (auto it = packet["slot_info"].begin(); it != packet["slot_info"].end(); ++it)
            {
                if (!it.value().is_object()) continue;
                int id = 0;
                try { id = std::stoi(it.key()); } catch (...) { continue; }
                if (it.value().contains("name") && it.value()["name"].is_string() &&
                    !apMultiPlayerNames.contains(id))
                    apMultiPlayerNames[id] = BoundedName(it.value()["name"].get<std::string>(), 65);
                if (it.value().contains("game") && it.value()["game"].is_string())
                {
                    const std::string game = it.value()["game"].get<std::string>();
                    apMultiPlayerGames[id] = game;
                    if (!game.empty() && std::find(apMultiWorldGames.begin(), apMultiWorldGames.end(), game) == apMultiWorldGames.end())
                        apMultiWorldGames.push_back(game);
                }
            }
        }
    }

    void ReceiveDataPackage(const json& packet)
    {
        if (!packet.contains("data") || !packet["data"].is_object()) return;
        const auto& data = packet["data"];
        if (!data.contains("games") || !data["games"].is_object()) return;
        std::lock_guard<std::mutex> lock(apMutex);
        for (auto it = data["games"].begin(); it != data["games"].end(); ++it)
        {
            const auto& gameData = it.value();
            if (!gameData.is_object() || !gameData.contains("item_name_to_id") ||
                !gameData["item_name_to_id"].is_object()) continue;
            auto& names = apMultiItemNames[it.key()];
            for (auto item = gameData["item_name_to_id"].begin(); item != gameData["item_name_to_id"].end(); ++item)
            {
                if (item.value().is_number_integer())
                    names[item.value().get<std::int64_t>()] = BoundedName(item.key(), 145);
            }
        }
    }

    void ReceiveItemAnnouncement(const json& packet)
    {
        if (!packet.contains("item") || !packet["item"].is_object() ||
            !packet.contains("receiving") || !packet["receiving"].is_number_integer()) return;
        const auto& info = packet["item"];
        if (!info.contains("item") || !info["item"].is_number_integer() ||
            !info.contains("player") || !info["player"].is_number_integer()) return;
        APClient::MultiplayerEvent event;
        event.itemId = info["item"].get<std::int64_t>();
        event.sourceSlot = info["player"].get<int>();
        event.destinationSlot = packet["receiving"].get<int>();
        event.isProgression = info.contains("flags") && info["flags"].is_number_integer() &&
            ((info["flags"].get<int>() & 1) != 0);
        // PrintJSON data can sometimes supply pre-resolved item names.
        // IDs remain fallback; official DataPackage resolves names across games.
        if (packet.contains("data") && packet["data"].is_array())
        {
            for (const auto& part : packet["data"])
            {
                if (!part.is_object() || !part.contains("type") || !part["type"].is_string() ||
                    part["type"].get<std::string>() != "item_name" ||
                    !part.contains("text") || !part["text"].is_string()) continue;
                event.itemName = BoundedName(part["text"].get<std::string>(), 145);
                break;
            }
        }
        std::lock_guard<std::mutex> lock(apMutex);
        const auto source = apMultiPlayerNames.find(event.sourceSlot);
        const auto dest = apMultiPlayerNames.find(event.destinationSlot);
        event.sourceName = source != apMultiPlayerNames.end() ? source->second : "Player " + std::to_string(event.sourceSlot);
        event.destinationName = dest != apMultiPlayerNames.end() ? dest->second : "Player " + std::to_string(event.destinationSlot);
        event.receivedLocally = (apMultiOwnSlot >= 0 && event.destinationSlot == apMultiOwnSlot);
        apMultiplayerQueue.emplace_back(std::move(event));
        if (apMultiplayerQueue.size() > 96) apMultiplayerQueue.erase(apMultiplayerQueue.begin(), apMultiplayerQueue.begin() + 32);
    }
// AP_MULTIWORLD_HUD_V082_NETWORK_HELPERS_END

    void HandleMessage(
        const std::string& text,
        HINTERNET socket,
        bool& authenticated,
        bool& senderStarted,
        std::thread& sender,
        bool& stopSender)
    {
        json packets = json::parse(text, nullptr, false);

        if (!packets.is_array())
        {
            Print("AP: Invalid JSON packet");
            return;
        }

        for (const auto& packet : packets)
        {
            if (!packet.is_object())
                continue;

            std::string cmd = packet.value("cmd", "");

            if (cmd == "RoomInfo")
            {
                Print("AP ROOM INFO RECEIVED");
                                {
                    std::lock_guard<std::mutex> lock(apMutex);

                    // Chaque partie Archipelago possede son propre historique.
                    apSeedName = packet.value("seed_name", std::string(""));
                    apMultiplayerQueue.clear();
                    apMultiPlayerNames.clear();
                    apMultiPlayerGames.clear();
                    apMultiItemNames.clear();
                    apMultiWorldGames.clear();
                    apMultiOwnSlot = -1;
                    apMedalGoalTarget = 338;

                    // Invalider les anciennes reservations QTE a chaque RoomInfo.
                    ++apQteOfDoomGeneration;
                    std::printf(
                        "AP QTE DOOM GENERATION: %llu\n",
                        static_cast<unsigned long long>(apQteOfDoomGeneration)
                    );

                    // Attendre l'authentification avant de charger les checks.
                    apConfirmed.clear();
                    apSent.clear();
                    apPending.clear();
                    apCheckStateLoadedSuccessfully = false;
                    apRingStateLoadedSuccessfully = false;

                    // Reinitialiser les donnees temporaires avant le chargement.
                    apReceivedRingIndices.clear();
                    apAppliedRingIndices.clear();
                    apRingAmounts.clear();
                    apRingIndicesInFlight.clear();
                    apPendingRings = 0;

                    // Restaurer les Rings recus et leur etat de distribution.
                    LoadRingState();

                    // AP Ring Wipe Trap : restaurer les traps de cette seed.
                    apReceivedRingWipeIndices.clear();
                    apAppliedRingWipeIndices.clear();
                    apRingWipeIndicesInFlight.clear();
                    apPendingRingWipes = 0;
                    apRingWipeStateLoadedSuccessfully = false;
                    LoadRingWipeState();
                    // AP Reverse Controls Trap : restaurer cette seed.
                    apReceivedReverseControlsIndices.clear();
                    apAppliedReverseControlsIndices.clear();
                    apReverseControlsIndicesInFlight.clear();
                    apPendingReverseControls = 0;
                    apReverseControlsStateLoadedSuccessfully = false;
LoadReverseControlsState();
apReceivedFreezeIndices.clear();
apAppliedFreezeIndices.clear();
apFreezeIndicesInFlight.clear();
apPendingFreeze = 0;
apFreezeStateLoadedSuccessfully = false;
LoadFreezeState();

apReceivedBurnIndices.clear();
apAppliedBurnIndices.clear();
apBurnIndicesInFlight.clear();
apPendingBurn = 0;
apBurnStateLoadedSuccessfully = false;
LoadBurnState();

apReceivedParalyzedIndices.clear();
apAppliedParalyzedIndices.clear();
apParalyzedIndicesInFlight.clear();
apPendingParalyzed = 0;
apParalyzedStateLoadedSuccessfully = false;
LoadParalyzedState();

                    // AP QTE of Doom : restaurer les traps de cette seed/slot.
                    apReceivedQteOfDoomIndices.clear();
                    apAppliedQteOfDoomIndices.clear();
                    apQteOfDoomIndicesInFlight.clear();
                    apPendingQteOfDoom = 0;
                    apQteOfDoomStateLoadedSuccessfully = false;
                    LoadQteOfDoomState();

                    // AP Life: reinitialiser et restaurer l'historique de cette seed.
                    apLifeStateLoadedSuccessfully = false;
                    apReceivedLifeIndices.clear();
                    apAppliedLifeIndices.clear();
                    apLifeIndicesInFlight.clear();
                    apPendingLives = 0;
                    LoadLifeState();
                    // AP Full Ring : restaurer les objets de cette seed.
                    apReceivedFullRingIndices.clear();
                    apAppliedFullRingIndices.clear();
                    apFullRingIndicesInFlight.clear();
                    apPendingFullRings = 0;
                    apFullRingStateLoadedSuccessfully = false;
                    LoadFullRingState();

                    // AP Shield Charge : restaurer les charges de cette seed.
                    apReceivedShieldChargeIndices.clear();
                    apAppliedShieldChargeIndices.clear();
                    apShieldChargeIndicesInFlight.clear();
                    apPendingShieldCharges = 0;
                    apShieldChargeStateLoadedSuccessfully = false;
                    LoadShieldChargeState();
                    
                    // AP Dark Gaia Energy : restaurer les items de cette seed.
                    apReceivedDarkGaiaEnergyIndices.clear();
                    apAppliedDarkGaiaEnergyIndices.clear();
                    apDarkGaiaEnergyIndicesInFlight.clear();
                    apPendingDarkGaiaEnergy = 0;
                    apDarkGaiaEnergyStateLoadedSuccessfully = false;
                    LoadDarkGaiaEnergyState();
                    // AP Super Claws : restaurer les recompenses de cette seed.
                    apReceivedSuperClawsIndices.clear();
                    apAppliedSuperClawsIndices.clear();
                    apSuperClawsIndicesInFlight.clear();
                    apPendingSuperClaws = 0;
                    apSuperClawsStateLoadedSuccessfully = false;
                    LoadSuperClawsState();

// AP Power Shield : restaurer les recompenses de cette seed.
apReceivedPowerShieldIndices.clear();
apAppliedPowerShieldIndices.clear();
apPowerShieldIndicesInFlight.clear();
apPendingPowerShield = 0;
apPowerShieldStateLoadedSuccessfully = false;
LoadPowerShieldState();

                    // AP Crystal XP : restaurer les recompenses de cette seed.
                    apCrystalXPStateLoadedSuccessfully = false;
                    apReceivedCrystalXPIndices.clear();
                    apAppliedCrystalXPIndices.clear();
                    apCrystalXPIndicesInFlight.clear();
                    apCrystalXPAmounts.clear();
                    apPendingCrystalXP = 0;
                    LoadCrystalXPState();
                    // AP Consumables : restaurer uniquement la seed actuelle.
                    apConsumableStateLoadedSuccessfully = false;
                    apReceivedConsumableIndices.clear();
                    apAppliedConsumableIndices.clear();
                    apConsumableIndicesInFlight.clear();
                    apConsumableItemIds.clear();
                    LoadConsumableState();
                }
                std::printf("AP SEED NAME: %s\n", packet.value("seed_name", std::string("UNKNOWN")).c_str());

                json connect = json::array({
                    {
                        {"cmd", "Connect"},
                        {"game", "Sonic Unleashed"},
                        {"name", Config::ArchipelagoSlot.Value},
                        {"password", ""},
                        {"uuid", "SonicUnleashedRecompPrototype"},
                        {"version", {
                            {"major", 0},
                            {"minor", 6},
                            {"build", 7},
                            {"class", "Version"}
                        }},
                        {"items_handling", 7},
                        {"tags", json::array({"DeathLink"})}
                    }
                });

                if (SendJson(socket, connect))
                    Print("AP CONNECT PACKET SENT");
            }
            else if (cmd == "Connected")
            {
                // Diagnostic de l'identite confirmee par le serveur.
                std::printf(
                    "AP CONNECTED IDENTITY: slot=%s players=%s\n",
                    packet.contains("slot") ? packet["slot"].dump().c_str() : "MISSING",
                    packet.contains("players") ? packet["players"].dump().c_str() : "MISSING"
                );
                std::fflush(stdout);

                // Verifier que le slot confirme correspond au joueur configure.
                bool slotIdentityValid = false;

                if (packet.contains("slot") &&
                    packet["slot"].is_number_integer() &&
                    packet.contains("players") &&
                    packet["players"].is_array())
                {
                    const auto confirmedSlot = packet["slot"].get<std::int64_t>();

                    for (const auto& player : packet["players"])
                    {
                        if (!player.is_object() ||
                            !player.contains("slot") ||
                            !player["slot"].is_number_integer() ||
                            !player.contains("name") ||
                            !player["name"].is_string())
                            continue;

                        if (player["slot"].get<std::int64_t>() == confirmedSlot &&
                            player["name"].get<std::string>() ==
                                Config::ArchipelagoSlot.Value)
                        {
                            slotIdentityValid = true;
                            break;
                        }
                    }
                }

                if (!slotIdentityValid)
                {
                    Print("AP SECURITY: authenticated slot identity mismatch");
                    continue;
                }

                authenticated = true;

                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    apMultiOwnSlot = packet["slot"].get<int>();
                    RememberSlotNames(packet);
                }

                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    // Restaurer les checks seulement apres authentification.
                    if (!LoadCheckState())
                    {
                        std::printf(
                            "AP CHECK LOAD FAILED: pending checks unavailable\n"
                        );
                    }

                    // AP_GOAL_V1_BEGIN: parse authenticated slot data.
                    apSelectedGoal = -1;
                    apGoalPending = false;
                    apGoalSent = false;
                    if (packet.contains("slot_data") &&
                        packet["slot_data"].is_object() &&
                        packet["slot_data"].contains("goal") &&
                        packet["slot_data"]["goal"].is_number_integer())
                    {
                        const int value = packet["slot_data"]["goal"].get<int>();
                        if (value >= 0 && value <= 2)
                            apSelectedGoal = value;
                    }
                    // Read DLC setting from authenticated slot data.
                    apIncludeDlc = true;
                    if (packet.contains("slot_data") && packet["slot_data"].is_object())
                    {
                        const auto& slotData = packet["slot_data"];
                        if (slotData.contains("include_dlc"))
                        {
                            const auto& option = slotData["include_dlc"];
                            if (option.is_boolean())
                                apIncludeDlc = option.get<bool>();
                            else if (option.is_number_integer())
                                apIncludeDlc = option.get<int>() != 0;
                        }
                    }
                    // Prefer slot data target (367 for this APWorld); older rooms retain 338 as fallback.
                    apMedalGoalTarget = 338;
                    if (packet.contains("slot_data") && packet["slot_data"].is_object() &&
                        packet["slot_data"].contains("medal_goal_target") &&
                        packet["slot_data"]["medal_goal_target"].is_number_integer())
                    {
                        const int expected = packet["slot_data"]["medal_goal_target"].get<int>();
                        if (expected > 0 && expected <= 370) apMedalGoalTarget = expected;
                    }
                    // Rank threshold sent by the APWorld: 0=D, 1=C, 2=B, 3=A.
                    apRequiredRankForAllChecks = 3;
                    if (packet.contains("slot_data") && packet["slot_data"].is_object())
                    {
                        const auto& slotData = packet["slot_data"];
                        if (slotData.contains("required_rank_for_all_checks") &&
                            slotData["required_rank_for_all_checks"].is_number_integer())
                        {
                            const int selected = slotData["required_rank_for_all_checks"].get<int>();
                            if (selected >= 0 && selected <= 3)
                                apRequiredRankForAllChecks = selected;
                        }
                    }
                    std::printf("AP RANK ALL-CHECK THRESHOLD: %c\n",
                        "DCBA"[apRequiredRankForAllChecks]);
                    std::printf("AP DLC CHECKS: %s\n", apIncludeDlc ? "ON" : "OFF");
                    std::printf("AP GOAL SELECTED: %d\n", apSelectedGoal);
                    // AP_GOAL_V1_END
                    // AP_SHOES_SYNC_V082: nouvelle room => inventaire a reconstruire.
                    apReceivedShoes = 0;
                    apShoeInventoryReady = false;
                    apAuthenticated = true;
                    apConnectionState = APConnectionState::Connected;
                    apSent.clear();
                }

                std::printf("AP SLOT AUTHENTICATED: %s\n", Config::ArchipelagoSlot.Value.c_str());

                if (packet.contains("checked_locations"))
                    ConfirmLocations(packet["checked_locations"]);

                if (!senderStarted)
                {
                    senderStarted = true;
                    sender = std::thread(
                        SenderThread,
                        socket,
                        std::ref(stopSender)
                    );
                }

                apWake.notify_all();
                // Resolve item names for every game in this room. No guessing IDs.
                // A failure affects labels only; the Archipelago client keeps working.
                json games = json::array();
                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    for (const auto& game : apMultiWorldGames) games.push_back(game);
                }
                if (!games.empty())
                    SendJson(socket, json::array({{{"cmd", "GetDataPackage"}, {"games", games}}}));
            }
            else if (cmd == "DataPackage")
            {
                ReceiveDataPackage(packet);
            }
            else if (cmd == "PrintJSON")
            {
                // All players' exchanges; progression chime is limited to local recipient.
                const std::string messageType = packet.value("type", std::string(""));
                if (authenticated && (messageType == "ItemSend" || messageType == "ItemCheat"))
                    ReceiveItemAnnouncement(packet);
            }
            else if (cmd == "RoomUpdate")
            {
                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    RememberSlotNames(packet);
                }
                if (packet.contains("checked_locations"))
                    ConfirmLocations(packet["checked_locations"]);
            }
            else if (cmd == "ConnectionRefused")
            {
                std::printf(
                    "AP CONNECTION REFUSED: %s\n",
                    packet.dump().c_str()
                );
                std::fflush(stdout);
            }
            else if (cmd == "Bounced")
            {
                const auto tagsIt = packet.find("tags");
                const auto dataIt = packet.find("data");

                if (tagsIt != packet.end() &&
                    tagsIt->is_array() &&
                    dataIt != packet.end() &&
                    dataIt->is_object())
                {
                    bool isDeathLink = false;

                    for (const auto& tag : *tagsIt)
                    {
                        if (tag.is_string() &&
                            tag.get<std::string>() == "DeathLink")
                        {
                            isDeathLink = true;
                            break;
                        }
                    }

                    if (isDeathLink &&
                        dataIt->value("source", std::string("Unknown")) != Config::ArchipelagoSlot.Value)
                    {
                        const std::string source =
                            dataIt->value("source", std::string("Unknown"));

                        const std::string cause =
                            dataIt->value("cause", std::string("Unknown"));

                        std::printf(
                            "[AP DEATH LINK RECEIVED] source=%s cause=%s\n",
                            source.c_str(),
                            cause.c_str()
                        );

                        std::fflush(stdout);

                        bool accepted = false;
                        {
                            std::lock_guard<std::mutex> lock(apMutex);
                            if (apDeathLinkStageEligible &&
                                std::chrono::steady_clock::now() - apDeathLinkStageSeenAt < std::chrono::seconds(2))
                            {
                                ++apPendingIncomingDeathLinks;
                                accepted = true;
                            }
                        }

                        if (accepted)
                            LOGFN("[AP DEATH LINK INCOMING] Pending death queued in playable Act");
                        else
                            LOGFN("[AP DEATH LINK HUB GUARD] Incoming Death Link ignored outside playable Act");
                    }
                }
            }
            else if (cmd == "ReceivedItems")
            {
                // Archipelago renvoie aussi l'historique apres reconnexion.
                // Un masque de bits rend l'application des Shoes idempotente.
                const auto itemsIt = packet.find("items");
                const auto indexIt = packet.find("index");
                if (itemsIt == packet.end() || !itemsIt->is_array() ||
                    indexIt == packet.end() || !indexIt->is_number_integer())
                {
                    Print("AP RECEIVED ITEMS: malformed packet");
                    continue;
                }

                const auto startIndex = indexIt->get<std::int64_t>();
                std::printf("AP RECEIVED ITEMS: start=%lld count=%zu\n",
                    static_cast<long long>(startIndex), itemsIt->size());

                for (std::size_t i = 0; i < itemsIt->size(); ++i)
                {
                    const auto& item = (*itemsIt)[i];
                    if (!item.is_object()) continue;
                    const auto idIt = item.find("item");
                    if (idIt == item.end() || !idIt->is_number_integer()) continue;

                    const auto itemId = idIt->get<std::int64_t>();
                    const auto locationIt = item.find("location");
                    const auto playerIt = item.find("player");
                    const auto locationId = (locationIt != item.end() && locationIt->is_number_integer())
                        ? locationIt->get<std::int64_t>() : -1;
                    const auto playerId = (playerIt != item.end() && playerIt->is_number_integer())
                        ? playerIt->get<std::int64_t>() : -1;
                    const auto shoeBit = ShoeBitForItem(itemId);

                    std::printf("AP ITEM: index=%lld id=%lld location=%lld from_player=%lld shoe_bit=0x%02X\n",
                        static_cast<long long>(startIndex + static_cast<std::int64_t>(i)),
                        static_cast<long long>(itemId),
                        static_cast<long long>(locationId),
                        static_cast<long long>(playerId),
                        static_cast<unsigned>(shoeBit));

                    if (shoeBit)
                    {
                        std::lock_guard<std::mutex> lock(apMutex);
                        apReceivedShoes |= shoeBit;
                    }

                    // AP Progressive Medals - anti-doublons par index
                    if (itemId == kSunMedalItemId || itemId == kMoonMedalItemId)
                    {
                        const auto itemIndex =
                            startIndex + static_cast<std::int64_t>(i);

                        std::lock_guard<std::mutex> lock(apMutex);

                        if (itemIndex >= 0 &&
                            apReceivedMedalIndices.insert(itemIndex).second)
                        {
                            if (itemId == kSunMedalItemId && apSunMedalItems < 20)
                                ++apSunMedalItems;
                            else if (itemId == kMoonMedalItemId && apMoonMedalItems < 20)
                                ++apMoonMedalItems;

                            std::printf(
                                "AP MEDALS: Sun=%u (+%u) Moon=%u (+%u)\n",
                                apSunMedalItems,
                                apSunMedalItems * 6,
                                apMoonMedalItems,
                                apMoonMedalItems * 4
                            );
                        }
                    }
                }
                // AP Consumables : reception et sauvegarde par index.
                for (std::size_t i = 0; i < itemsIt->size(); ++i)
                {
                    const auto& item = (*itemsIt)[i];
                    if (!item.is_object()) continue;

                    const auto idIt = item.find("item");
                    if (idIt == item.end() || !idIt->is_number_integer())
                        continue;

                    const auto itemId = idIt->get<std::int64_t>();

                    const std::uint32_t sonicItemId =
                        itemId == kHotdogItemId ? 111u :
                        itemId == kFriedChickenItemId ? 104u :
                        itemId == kHeroSandwichItemId ? 101u : 0u;

                    if (sonicItemId == 0)
                        continue;

                    const auto index =
                        startIndex + static_cast<std::int64_t>(i);

                    std::lock_guard<std::mutex> lock(apMutex);

                    if (apSeedName.empty() || index < 0 ||
                        apReceivedConsumableIndices.contains(index))
                        continue;

                    // Refuser la reception si un historique existant
                    // n'a pas pu etre charge correctement.
                    if (!apConsumableStateLoadedSuccessfully &&
                        std::filesystem::exists(GetConsumableStatePath()))
                        continue;

                    apReceivedConsumableIndices.insert(index);
                    apConsumableItemIds[index] = sonicItemId;

                    if (!SaveConsumableState())
                    {
                        apConsumableItemIds.erase(index);
                        apReceivedConsumableIndices.erase(index);

                        std::printf(
                            "AP CONSUMABLE SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP CONSUMABLE QUEUED: index=%lld item=%u\n",
                            static_cast<long long>(index),
                            sonicItemId
                        );
                    }
                }
                // AP Crystal XP : reception et sauvegarde par index.
                for (std::size_t i = 0; i < itemsIt->size(); ++i)
                {
                    const auto& item = (*itemsIt)[i];
                    if (!item.is_object()) continue;

                    const auto idIt = item.find("item");
                    if (idIt == item.end() || !idIt->is_number_integer())
                        continue;

                    const auto itemId = idIt->get<std::int64_t>();

                    const std::uint32_t amount =
                        itemId == kCrystalXP50ItemId ? 50u :
                        itemId == kCrystalXP100ItemId ? 100u :
                        itemId == kCrystalXP200ItemId ? 200u : 0u;

                    if (amount == 0)
                        continue;

                    const auto index =
                        startIndex + static_cast<std::int64_t>(i);

                    std::lock_guard<std::mutex> lock(apMutex);

                    if (apSeedName.empty() || index < 0 ||
                        apReceivedCrystalXPIndices.contains(index))
                        continue;

                    // Bloquer toute reception si un historique existant
                    // n'a pas pu etre charge correctement.
                    if (!apCrystalXPStateLoadedSuccessfully &&
                        std::filesystem::exists(GetCrystalXPStatePath()))
                        continue;

                    if (apPendingCrystalXP > UINT32_MAX - amount)
                        continue;

                    apReceivedCrystalXPIndices.insert(index);
                    apCrystalXPAmounts[index] = amount;
                    apPendingCrystalXP += amount;

                    if (!SaveCrystalXPState())
                    {
                        apPendingCrystalXP -= amount;
                        apCrystalXPAmounts.erase(index);
                        apReceivedCrystalXPIndices.erase(index);

                        std::printf(
                            "AP CRYSTAL XP SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP CRYSTAL XP QUEUED: index=%lld +%u XP pending=%u\n",
                            static_cast<long long>(index),
                            amount,
                            apPendingCrystalXP
                        );
                    }
                }
                // AP Rings - reception uniquement, aucune ecriture en sauvegarde.
                // Les index evitent les doublons pendant cette session.
                // TODO: persistance avant application reelle.
                for (std::size_t i = 0; i < itemsIt->size(); ++i)
                {
                    const auto& item = (*itemsIt)[i];
                    if (!item.is_object()) continue;
                    const auto idIt = item.find("item");
                    if (idIt == item.end() || !idIt->is_number_integer()) continue;
                    const auto itemId = idIt->get<std::int64_t>();
                    const std::uint32_t amount =
                        itemId == kRing10ItemId ? 10u :
                        itemId == kRing50ItemId ? 50u :
                        itemId == kRing100ItemId ? 100u : 0u;
                    if (itemId == kDarkGaiaEnergyItemId) { const auto index = startIndex + static_cast<std::int64_t>(i); std::lock_guard<std::mutex> lock(apMutex); if (index >= 0 && apReceivedDarkGaiaEnergyIndices.insert(index).second) { ++apPendingDarkGaiaEnergy; if (!SaveDarkGaiaEnergyState()) { --apPendingDarkGaiaEnergy; apReceivedDarkGaiaEnergyIndices.erase(index); std::printf("AP DARK GAIA ENERGY SAVE FAILED: index=%lld\n", static_cast<long long>(index)); } else { std::printf("AP ITEM RECEIVED: Dark Gaia Energy, index=%lld\n", static_cast<long long>(index)); } } }
                    if (itemId == kPowerShieldItemId)
{
    const auto index = startIndex + static_cast<std::int64_t>(i);
    std::lock_guard<std::mutex> lock(apMutex);

    if (index >= 0 &&
        apReceivedPowerShieldIndices.insert(index).second)
    {
        ++apPendingPowerShield;

        if (!SavePowerShieldState())
        {
            --apPendingPowerShield;
            apReceivedPowerShieldIndices.erase(index);
            std::printf(
                "AP POWER SHIELD SAVE FAILED: index=%lld\n",
                static_cast<long long>(index)
            );
        }
        else
        {
            std::printf(
                "AP ITEM RECEIVED: Power Shield, index=%lld\n",
                static_cast<long long>(index)
            );
        }
    }
}
if (itemId == kSuperClawsItemId) { const auto index = startIndex + static_cast<std::int64_t>(i); std::lock_guard<std::mutex> lock(apMutex); if (index >= 0 && apReceivedSuperClawsIndices.insert(index).second) { ++apPendingSuperClaws; if (!SaveSuperClawsState()) { --apPendingSuperClaws; apReceivedSuperClawsIndices.erase(index); std::printf("AP SUPER CLAWS SAVE FAILED: index=%lld\n", static_cast<long long>(index)); } else { std::printf("AP ITEM RECEIVED: Super Claws, index=%lld\n", static_cast<long long>(index)); } } }
                    if (itemId == kShieldChargeItemId)
{
    const auto index = startIndex + static_cast<std::int64_t>(i);
    std::lock_guard<std::mutex> lock(apMutex);

    if (index >= 0 &&
        apReceivedShieldChargeIndices.insert(index).second)
    {
        if (apPendingShieldCharges == UINT32_MAX)
        {
            apReceivedShieldChargeIndices.erase(index);
            std::printf("AP SHIELD CHARGE LIMIT REACHED\n");
        }
        else
        {
            ++apPendingShieldCharges;

            if (!SaveShieldChargeState())
            {
                --apPendingShieldCharges;
                apReceivedShieldChargeIndices.erase(index);

                std::printf(
                    "AP SHIELD CHARGE SAVE FAILED: index=%lld\n",
                    static_cast<long long>(index)
                );
            }
            else
            {
                std::printf(
                    "AP ITEM RECEIVED: Shield Charge, index=%lld, pending=%u\n",
                    static_cast<long long>(index),
                    apPendingShieldCharges
                );
            }
        }
    }
}
if (itemId == kFullRingItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedFullRingIndices.insert(index).second)
                {
                    ++apPendingFullRings;

                    if (!SaveFullRingState())
                    {
                        --apPendingFullRings;
                        apReceivedFullRingIndices.erase(index);

                        std::printf(
                            "AP FULL RING SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP ITEM RECEIVED: Full Ring, index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                }
            }
                        // AP Ring Wipe Trap : reception et sauvegarde.
            // AP Reverse Controls Trap : reception et sauvegarde.
            // AP QTE of Doom Trap : reception persistante.
            if (itemId == kQteOfDoomTrapItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedQteOfDoomIndices.insert(index).second)
                {
                    ++apPendingQteOfDoom;

                    if (!SaveQteOfDoomState())
                    {
                        --apPendingQteOfDoom;
                        apReceivedQteOfDoomIndices.erase(index);

                        std::printf(
                            "AP QTE DOOM SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP QTE DOOM RECEIVED: index=%lld, pending=%u\n",
                            static_cast<long long>(index),
                            apPendingQteOfDoom
                        );
                    }
                }

                continue;
            }
            if (itemId == kReverseControlsTrapItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedReverseControlsIndices.insert(index).second)
                {
                    ++apPendingReverseControls;

                    if (!SaveReverseControlsState())
                    {
                        --apPendingReverseControls;
                        apReceivedReverseControlsIndices.erase(index);

                        std::printf(
                            "AP REVERSE SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP REVERSE RECEIVED: index=%lld, pending=%u\n",
                            static_cast<long long>(index),
                            apPendingReverseControls
                        );
                    }
                }

                continue;
            }
            if (itemId == kFreezeTrapItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedFreezeIndices.insert(index).second)
                {
                    ++apPendingFreeze;

                    if (!SaveFreezeState())
                    {
                        --apPendingFreeze;
                        apReceivedFreezeIndices.erase(index);

                        std::printf(
                            "AP FREEZE SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP FREEZE RECEIVED: index=%lld, pending=%u\n",
                            static_cast<long long>(index),
                            apPendingFreeze
                        );
                    }
                }

                continue;
            }
            
            if (itemId == kBurnTrapItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedBurnIndices.insert(index).second)
                {
                    ++apPendingBurn;

                    if (!SaveBurnState())
                    {
                        --apPendingBurn;
                        apReceivedBurnIndices.erase(index);

                        std::printf(
                            "AP BURN SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP BURN RECEIVED: index=%lld, pending=%u\n",
                            static_cast<long long>(index),
                            apPendingBurn
                        );
                    }
                }

                continue;
            }
            
            if (itemId == kParalyzedTrapItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedParalyzedIndices.insert(index).second)
                {
                    ++apPendingParalyzed;

                    if (!SaveParalyzedState())
                    {
                        --apPendingParalyzed;
                        apReceivedParalyzedIndices.erase(index);

                        std::printf(
                            "AP PARALYZED SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP PARALYZED RECEIVED: index=%lld, pending=%u\n",
                            static_cast<long long>(index),
                            apPendingParalyzed
                        );
                    }
                }

                continue;
            }
            
if (itemId == kRingWipeTrapItemId)
            {
                const auto index = startIndex + static_cast<std::int64_t>(i);
                std::lock_guard<std::mutex> lock(apMutex);

                if (index >= 0 &&
                    apReceivedRingWipeIndices.insert(index).second)
                {
                    ++apPendingRingWipes;

                    if (!SaveRingWipeState())
                    {
                        --apPendingRingWipes;
                        apReceivedRingWipeIndices.erase(index);

                        std::printf(
                            "AP RING WIPE SAVE FAILED: index=%lld\n",
                            static_cast<long long>(index)
                        );
                    }
                    else
                    {
                        std::printf(
                            "AP RING WIPE RECEIVED: index=%lld, pending=%u\n",
                            static_cast<long long>(index),
                            apPendingRingWipes
                        );
                    }
                }

                continue;
            }
            if (itemId == kLifeItemId)
                    {
                        const auto index = startIndex + static_cast<std::int64_t>(i);
                        std::lock_guard<std::mutex> lock(apMutex);

                        if (index >= 0 && apReceivedLifeIndices.insert(index).second)
                        {
                            ++apPendingLives;

                            if (!SaveLifeState())
                            {
                                --apPendingLives;
                                apReceivedLifeIndices.erase(index);
                                std::printf("AP LIFE SAVE FAILED: index=%lld\n",
                                    static_cast<long long>(index));
                            }
                            else
                            {
                                std::printf("AP ITEM RECEIVED: Life, index=%lld\n",
                                    static_cast<long long>(index));
                            }
                        }
                        continue;
                    }

                    if (!amount) continue;
                    const auto index = startIndex + static_cast<std::int64_t>(i);
                    std::lock_guard<std::mutex> lock(apMutex);
                    if (index >= 0 && apReceivedRingIndices.insert(index).second)
                    {
                        apRingAmounts[index] = amount;
                        apPendingRings += amount;
                        if (!SaveRingState())
                        {
                            apPendingRings -= amount;
                            apRingAmounts.erase(index);
                            apReceivedRingIndices.erase(index);
                            std::printf("AP RINGS SAVE FAILED: index=%lld (rollback)\n", static_cast<long long>(index));
                        }
                        // Une recompense est confirmee dans les logs uniquement si son etat a ete sauvegarde.
                        if (apReceivedRingIndices.contains(index))
                            std::printf("AP RINGS QUEUED: index=%lld +%u pending=%u\n",
                            static_cast<long long>(index), amount, apPendingRings);
                    }
                }
                // AP_SHOES_SYNC_V082: l'index 0 inclut le debut de l'historique.
                if (startIndex == 0)
                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    apShoeInventoryReady = true;
                }
                std::fflush(stdout);
            }
        }
    }

    void Connect()
    {
        const std::string server = Config::ArchipelagoServer.Value;
        const int port = Config::ArchipelagoPort.Value;

        if (server.empty() || port < 1 || port > 65535)
        {
            Print("AP: Invalid server configuration");
            return;
        }

        const std::wstring wideServer(server.begin(), server.end());

        std::printf("AP: Connecting to %s:%d...\n", server.c_str(), port);

        HINTERNET session = WinHttpOpen(
            L"SonicUnleashed-Archipelago/0.2",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );

        if (!session)
        {
            Print("AP: WinHttpOpen failed");
            return;
        }

        WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

        HINTERNET connection = WinHttpConnect(
            session,
            wideServer.c_str(),
            static_cast<INTERNET_PORT>(port),
            0
        );

        HINTERNET request = nullptr;
        HINTERNET socket = nullptr;

        if (!connection)
        {
            Print("AP: WinHttpConnect failed");
            goto cleanup;
        }

        request = WinHttpOpenRequest(
            connection,
            L"GET",
            L"/",
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );

        if (!request)
        {
            Print("AP: WinHttpOpenRequest failed");
            goto cleanup;
        }

        if (!WinHttpSetOption(
                request,
                WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                nullptr,
                0) ||
            !WinHttpSendRequest(
                request,
                WINHTTP_NO_ADDITIONAL_HEADERS,
                0,
                WINHTTP_NO_REQUEST_DATA,
                0,
                0,
                0) ||
            !WinHttpReceiveResponse(request, nullptr))
        {
            std::printf(
                "AP: Handshake failed: %lu\n",
                GetLastError()
            );
            goto cleanup;
        }

        socket = WinHttpWebSocketCompleteUpgrade(request, 0);

        if (!socket)
        {
            Print("AP: WebSocket upgrade failed");
            goto cleanup;
        }

        WinHttpCloseHandle(request);
        request = nullptr;

        {
            std::lock_guard<std::mutex> lock(apMutex);
            apActiveSocket = socket;
        }


        Print("AP WEBSOCKET CONNECTED");

        {
            bool authenticated = false;
            bool senderStarted = false;
            bool stopSender = false;
            std::thread sender;

            {
                std::lock_guard<std::mutex> lock(apMutex);
                apSenderStopFlag = &stopSender;
            }
            std::string message;

            while (true)
            {
                {
                    std::lock_guard<std::mutex> lock(apMutex);
                    if (apDisconnectRequested)
                    {
                        Print("AP DISCONNECT REQUESTED");
                        break;
                    }
                }

                char buffer[8192];
                DWORD bytesRead = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};

                DWORD result = WinHttpWebSocketReceive(
                    socket,
                    buffer,
                    sizeof(buffer),
                    &bytesRead,
                    &type
                );

                if (result != NO_ERROR ||
                    type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                {
                    std::printf(
                        "AP CONNECTION CLOSED: %lu\n",
                        result
                    );
                    break;
                }

                if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                    type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                    continue;

                message.append(buffer, bytesRead);

                if (message.size() > 16 * 1024 * 1024)
                {
                    Print("AP: Message too large");
                    break;
                }

                if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)
                    continue;

                HandleMessage(
                    message,
                    socket,
                    authenticated,
                    senderStarted,
                    sender,
                    stopSender
                );

                message.clear();
            }

            {
                std::lock_guard<std::mutex> lock(apMutex);
                stopSender = true;
                apAuthenticated = false;
                apSent.clear();
            }

            apWake.notify_all();

            {
                std::lock_guard<std::mutex> lock(apMutex);
                apSenderStopFlag = nullptr;
            }

            if (sender.joinable())
                sender.join();
        }

    cleanup:
        {
            std::lock_guard<std::mutex> sendLock(apSendMutex);
            std::lock_guard<std::mutex> lock(apMutex);

            if (apActiveSocket == socket)
                apActiveSocket = nullptr;

            if (socket)
            {
                WinHttpCloseHandle(socket);
                socket = nullptr;
            }
        }

        if (request)
            WinHttpCloseHandle(request);

        if (connection)
            WinHttpCloseHandle(connection);

        WinHttpCloseHandle(session);
    }
}

// AP_SHOES_SYNC_V082: lecture atomique pour le thread de jeu.
// Pas d'ecriture de sauvegarde sans authentification + historique d'items.
bool AP_TryGetReceivedShoeMask(std::uint8_t* outMask)
{
    if (!outMask) return false;
    std::lock_guard<std::mutex> lock(apMutex);
    if (!apAuthenticated || !apShoeInventoryReady || apDisconnectRequested)
        return false;
    *outMask = apReceivedShoes;
    return true;
}
// Appelee uniquement par le thread jeu : ne touche jamais a la RAM ici.
std::uint8_t AP_GetReceivedShoeMask()
{
    std::lock_guard<std::mutex> lock(apMutex);
    return apReceivedShoes;
}

std::uint32_t APClient::GetSunMedalBonus()
{
    std::lock_guard<std::mutex> lock(apMutex);
    return apSunMedalItems * 6;
}

std::uint32_t APClient::GetMoonMedalBonus()
{
    std::lock_guard<std::mutex> lock(apMutex);
    return apMoonMedalItems * 4;
}

void AP_TestLocalRings()
{
    std::lock_guard<std::mutex> lock(apMutex);
    apPendingRings += 100;
    std::printf("AP LOCAL TEST: +100 Rings, pending=%u\n", apPendingRings);
    std::fflush(stdout);
}

    void APClient::CancelRingWipe()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apRingWipeIndicesInFlight.empty())
            return;

        apRingWipeIndicesInFlight.clear();

        std::printf("AP RING WIPE RESERVATION CANCELLED\n");
    }
    void APClient::ConfirmRingWipeApplied()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apRingWipeIndicesInFlight.empty())
            return;

        for (const auto index : apRingWipeIndicesInFlight)
            apAppliedRingWipeIndices.insert(index);

        if (!SaveRingWipeState())
        {
            for (const auto index : apRingWipeIndicesInFlight)
                apAppliedRingWipeIndices.erase(index);

            std::printf(
                "AP RING WIPE CONFIRM SAVE FAILED\n"
            );

            // Garder la reservation pour eviter une application immediate
            // du meme Trap pendant cette session.
            return;
        }

        if (apPendingRingWipes > 0)
            --apPendingRingWipes;

        apRingWipeIndicesInFlight.clear();

        std::printf("AP RING WIPE CONFIRMED\n");
    }
    std::uint32_t APClient::GetPendingRingWipe()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apRingWipeStateLoadedSuccessfully &&
            !apSeedName.empty() &&
            std::filesystem::exists(GetRingWipeStatePath()))
            return 0;

        if (!apRingWipeIndicesInFlight.empty())
            return 0;

        if (apPendingRingWipes == 0)
            return 0;

        for (const auto index : apReceivedRingWipeIndices)
        {
            if (!apAppliedRingWipeIndices.contains(index))
            {
                apRingWipeIndicesInFlight.insert(index);

                std::printf(
                    "AP RING WIPE RESERVED: index=%lld\n",
                    static_cast<long long>(index)
                );

                return 1;
            }
        }

        return 0;
    }

    // AP Reverse Controls Trap : annuler une reservation.
    void APClient::CancelReverseControls()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apReverseControlsIndicesInFlight.empty())
            return;

        apReverseControlsIndicesInFlight.clear();
        std::printf("AP REVERSE RESERVATION CANCELLED\n");
    }

    // AP Reverse Controls Trap : confirmer son application.
    void APClient::ConfirmReverseControlsApplied()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apReverseControlsIndicesInFlight.empty())
            return;

        for (const auto index : apReverseControlsIndicesInFlight)
            apAppliedReverseControlsIndices.insert(index);

        if (!SaveReverseControlsState())
        {
            for (const auto index : apReverseControlsIndicesInFlight)
                apAppliedReverseControlsIndices.erase(index);

            std::printf("AP REVERSE CONFIRM SAVE FAILED\n");

            // Garder la reservation pour eviter un nouveau declenchement
            // immediat pendant cette session.
            return;
        }

        if (apPendingReverseControls > 0)
            --apPendingReverseControls;

        apReverseControlsIndicesInFlight.clear();
        std::printf("AP REVERSE CONFIRMED\n");
    }

    // AP Reverse Controls Trap : reserver un piege en attente.
    std::uint32_t APClient::GetPendingReverseControls()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apReverseControlsStateLoadedSuccessfully &&
            !apSeedName.empty() &&
            std::filesystem::exists(GetReverseControlsStatePath()))
            return 0;

        if (!apReverseControlsIndicesInFlight.empty())
            return 0;

        if (apPendingReverseControls == 0)
            return 0;

        for (const auto index : apReceivedReverseControlsIndices)
        {
            if (!apAppliedReverseControlsIndices.contains(index))
            {
                apReverseControlsIndicesInFlight.insert(index);

                std::printf(
                    "AP REVERSE RESERVED: index=%lld\n",
                    static_cast<long long>(index)
                );

                return 1;
            }
        }

        return 0;
    }
    // AP Freeze Trap : annuler une reservation.
    void APClient::CancelFreeze()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apFreezeIndicesInFlight.empty())
            return;

        apFreezeIndicesInFlight.clear();
        std::printf("AP FREEZE RESERVATION CANCELLED\n");
    }

    // AP Freeze Trap : confirmer son application.
    bool APClient::ConfirmFreezeApplied()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apFreezeIndicesInFlight.empty())
            return false;

        for (const auto index : apFreezeIndicesInFlight)
            apAppliedFreezeIndices.insert(index);

        if (!SaveFreezeState())
        {
            for (const auto index : apFreezeIndicesInFlight)
                apAppliedFreezeIndices.erase(index);

            std::printf("AP FREEZE CONFIRM SAVE FAILED\n");

            // Garder la reservation pour eviter un nouveau declenchement
            // immediat pendant cette session.
            return false;
        }

        if (apPendingFreeze > 0)
            --apPendingFreeze;

        apFreezeIndicesInFlight.clear();
        std::printf("AP FREEZE CONFIRMED\n");
        return true;
    }

    // AP Freeze Trap : reserver un piege en attente.
    std::uint32_t APClient::GetPendingFreeze()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apFreezeStateLoadedSuccessfully &&
            !apSeedName.empty() &&
            std::filesystem::exists(GetFreezeStatePath()))
            return 0;

        if (!apFreezeIndicesInFlight.empty())
            return 0;

        if (apPendingFreeze == 0)
            return 0;

        for (const auto index : apReceivedFreezeIndices)
        {
            if (!apAppliedFreezeIndices.contains(index))
            {
                apFreezeIndicesInFlight.insert(index);

                std::printf(
                    "AP FREEZE RESERVED: index=%lld\n",
                    static_cast<long long>(index)
                );

                return 1;
            }
        }

        return 0;
    }
    // AP Burn Trap : annuler une reservation.
    void APClient::CancelBurn()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apBurnIndicesInFlight.empty())
            return;

        apBurnIndicesInFlight.clear();
        std::printf("AP BURN RESERVATION CANCELLED\n");
    }

    // AP Burn Trap : confirmer son application.
    bool APClient::ConfirmBurnApplied()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apBurnIndicesInFlight.empty())
            return false;

        for (const auto index : apBurnIndicesInFlight)
            apAppliedBurnIndices.insert(index);

        if (!SaveBurnState())
        {
            for (const auto index : apBurnIndicesInFlight)
                apAppliedBurnIndices.erase(index);

            std::printf("AP BURN CONFIRM SAVE FAILED\n");

            // Garder la reservation pour eviter un nouveau declenchement
            // immediat pendant cette session.
            return false;
        }

        if (apPendingBurn > 0)
            --apPendingBurn;

        apBurnIndicesInFlight.clear();
        std::printf("AP BURN CONFIRMED\n");
        return true;
    }

    // AP Burn Trap : reserver un piege en attente.
    std::uint32_t APClient::GetPendingBurn()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apBurnStateLoadedSuccessfully &&
            !apSeedName.empty() &&
            std::filesystem::exists(GetBurnStatePath()))
            return 0;

        if (!apBurnIndicesInFlight.empty())
            return 0;

        if (apPendingBurn == 0)
            return 0;

        for (const auto index : apReceivedBurnIndices)
        {
            if (!apAppliedBurnIndices.contains(index))
            {
                apBurnIndicesInFlight.insert(index);

                std::printf(
                    "AP BURN RESERVED: index=%lld\n",
                    static_cast<long long>(index)
                );

                return 1;
            }
        }

        return 0;
    }
    // AP Paralyzed Trap : annuler une reservation.
    void APClient::CancelParalyzed()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apParalyzedIndicesInFlight.empty())
            return;

        apParalyzedIndicesInFlight.clear();
        std::printf("AP PARALYZED RESERVATION CANCELLED\n");
    }

    // AP Paralyzed Trap : confirmer son application.
    bool APClient::ConfirmParalyzedApplied()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apParalyzedIndicesInFlight.empty())
            return false;

        for (const auto index : apParalyzedIndicesInFlight)
            apAppliedParalyzedIndices.insert(index);

        if (!SaveParalyzedState())
        {
            for (const auto index : apParalyzedIndicesInFlight)
                apAppliedParalyzedIndices.erase(index);

            std::printf("AP PARALYZED CONFIRM SAVE FAILED\n");

            // Garder la reservation pour eviter un nouveau declenchement
            // immediat pendant cette session.
            return false;
        }

        if (apPendingParalyzed > 0)
            --apPendingParalyzed;

        apParalyzedIndicesInFlight.clear();
        std::printf("AP PARALYZED CONFIRMED\n");
        return true;
    }

    // AP Paralyzed Trap : reserver un piege en attente.
    std::uint32_t APClient::GetPendingParalyzed()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apParalyzedStateLoadedSuccessfully &&
            !apSeedName.empty() &&
            std::filesystem::exists(GetParalyzedStatePath()))
            return 0;

        if (!apParalyzedIndicesInFlight.empty())
            return 0;

        if (apPendingParalyzed == 0)
            return 0;

        for (const auto index : apReceivedParalyzedIndices)
        {
            if (!apAppliedParalyzedIndices.contains(index))
            {
                apParalyzedIndicesInFlight.insert(index);

                std::printf(
                    "AP PARALYZED RESERVED: index=%lld\n",
                    static_cast<long long>(index)
                );

                return 1;
            }
        }

        return 0;
    }
    // Generation de la partie Archipelago pour le QTE of Doom.

    std::uint64_t APClient::GetQteOfDoomGeneration()
    {
        std::lock_guard<std::mutex> lock(apMutex);
        return apQteOfDoomGeneration;
    }
    void APClient::CancelQteOfDoom()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apQteOfDoomIndicesInFlight.empty())
            return;

        apQteOfDoomIndicesInFlight.clear();
        std::printf("AP QTE DOOM RESERVATION CANCELLED\n");
    }

    // AP Reverse Controls Trap : confirmer son application.
    bool APClient::ConfirmQteOfDoomApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);
    if (apQteOfDoomIndicesInFlight.empty()) return false;
    for (const auto index : apQteOfDoomIndicesInFlight)
        apAppliedQteOfDoomIndices.insert(index);
    if (!SaveQteOfDoomState())
    {
        for (const auto index : apQteOfDoomIndicesInFlight)
            apAppliedQteOfDoomIndices.erase(index);
        std::printf("AP QTE DOOM CONFIRM SAVE FAILED\n");
        return false;
    }
    if (apPendingQteOfDoom > 0) --apPendingQteOfDoom;
    apQteOfDoomIndicesInFlight.clear();
    std::printf("AP QTE DOOM CONFIRMED\n");
    return true;
}

    // AP Reverse Controls Trap : reserver un piege en attente.
    std::uint32_t APClient::GetPendingQteOfDoom()
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apQteOfDoomStateLoadedSuccessfully &&
            !apSeedName.empty() &&
            std::filesystem::exists(GetQteOfDoomStatePath()))
            return 0;

        if (!apQteOfDoomIndicesInFlight.empty())
            return 0;

        if (apPendingQteOfDoom == 0)
            return 0;

        for (const auto index : apReceivedQteOfDoomIndices)
        {
            if (!apAppliedQteOfDoomIndices.contains(index))
            {
                apQteOfDoomIndicesInFlight.insert(index);

                std::printf(
                    "AP QTE DOOM RESERVED: index=%lld\n",
                    static_cast<long long>(index)
                );

                return 1;
            }
        }

        return 0;
    }

std::uint32_t APClient::GetPendingRingBonus()
{
    std::lock_guard<std::mutex> lock(apMutex);
    // Ne pas distribuer de Rings si un historique existant est invalide.
    if (!apRingStateLoadedSuccessfully && !apSeedName.empty() && std::filesystem::exists(GetRingStatePath())) return 0;
    if (!apRingIndicesInFlight.empty()) return 0;
    const std::uint32_t amount = apPendingRings;
    if (amount == 0) return 0;
    apRingIndicesInFlight.clear();
    for (const auto& [index, ringAmount] : apRingAmounts)
        if (!apAppliedRingIndices.contains(index)) apRingIndicesInFlight.insert(index);
    std::uint64_t expected = 0;
    for (const auto index : apRingIndicesInFlight) expected += apRingAmounts.at(index);
    if (expected != amount) {
        std::printf("AP RINGS AMOUNT MISMATCH: expected=%llu pending=%u\n", static_cast<unsigned long long>(expected), amount);
        apRingIndicesInFlight.clear();
        return 0;
    }
    apPendingRings = 0;
    return amount;
}

void APClient::ConfirmRingBonusApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    for (const auto index : apRingIndicesInFlight)
        apAppliedRingIndices.insert(index);

    if (!apRingIndicesInFlight.empty() && !SaveRingState())
    {
        for (const auto index : apRingIndicesInFlight)
            apAppliedRingIndices.erase(index);
        std::printf("AP RINGS CONFIRM SAVE FAILED: confirmation non sauvegardee\n");
    }

    apRingIndicesInFlight.clear();
}
std::uint32_t APClient::GetPendingConsumable()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apConsumableStateLoadedSuccessfully &&
        std::filesystem::exists(GetConsumableStatePath()))
        return 0;

    if (!apConsumableIndicesInFlight.empty())
        return 0;

    // Choisir le plus petit index Archipelago non distribue.
    std::int64_t nextIndex = INT64_MAX;
    std::uint32_t nextItemId = 0;

    for (const auto& [index, itemId] : apConsumableItemIds)
    {
        if (!apAppliedConsumableIndices.contains(index) &&
            index < nextIndex)
        {
            nextIndex = index;
            nextItemId = itemId;
        }
    }

    if (nextItemId != 0)
        apConsumableIndicesInFlight.insert(nextIndex);

    return nextItemId;
}
void APClient::ConfirmConsumableApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apConsumableIndicesInFlight.empty())
        return;

    for (const auto index : apConsumableIndicesInFlight)
        apAppliedConsumableIndices.insert(index);

    if (!SaveConsumableState())
    {
        for (const auto index : apConsumableIndicesInFlight)
            apAppliedConsumableIndices.erase(index);

        // Garder la recompense reservee pendant cette session
        // pour ne pas la distribuer une seconde fois.
        std::printf(
            "AP CONSUMABLE CONFIRM SAVE FAILED: retry needed\n"
        );
        return;
    }

    for (const auto index : apConsumableIndicesInFlight)
    {
        std::printf(
            "AP CONSUMABLE APPLIED: index=%lld\n",
            static_cast<long long>(index)
        );
    }

    apConsumableIndicesInFlight.clear();
}
void APClient::CancelConsumable()
{
    std::lock_guard<std::mutex> lock(apMutex);
    apConsumableIndicesInFlight.clear();
}
std::uint32_t APClient::GetPendingCrystalXPBonus()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apCrystalXPStateLoadedSuccessfully &&
        std::filesystem::exists(GetCrystalXPStatePath()))
        return 0;

    if (!apCrystalXPIndicesInFlight.empty())
        return 0;

    for (const auto& [index, amount] : apCrystalXPAmounts)
    {
        if (!apAppliedCrystalXPIndices.contains(index))
        {
            apCrystalXPIndicesInFlight.insert(index);
            return amount;
        }
    }

    return 0;
}
void APClient::ConfirmCrystalXPBonusApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apCrystalXPIndicesInFlight.empty())
        return;

    for (const auto index : apCrystalXPIndicesInFlight)
        apAppliedCrystalXPIndices.insert(index);

    if (!SaveCrystalXPState())
    {
        for (const auto index : apCrystalXPIndicesInFlight)
            apAppliedCrystalXPIndices.erase(index);

        // Garder la recompense reservee pour eviter
        // une nouvelle distribution pendant cette session.
        std::printf(
            "AP CRYSTAL XP CONFIRM SAVE FAILED: retry needed\n"
        );
        return;
    }

    for (const auto index : apCrystalXPIndicesInFlight)
    {
        const auto it = apCrystalXPAmounts.find(index);

        if (it != apCrystalXPAmounts.end())
        {
            apPendingCrystalXP -= it->second;

            std::printf(
                "AP CRYSTAL XP CONFIRMED: index=%lld +%u XP\n",
                static_cast<long long>(index),
                it->second
            );
        }
    }

    apCrystalXPIndicesInFlight.clear();
}
std::uint32_t APClient::GetPendingDarkGaiaEnergy() {
    std::lock_guard<std::mutex> lock(apMutex);
    if (apSeedName.empty()) return 0;
    if (!apDarkGaiaEnergyStateLoadedSuccessfully && std::filesystem::exists(GetDarkGaiaEnergyStatePath())) return 0;
    if (!apDarkGaiaEnergyIndicesInFlight.empty() || apPendingDarkGaiaEnergy == 0) return 0;
    for (const auto index : apReceivedDarkGaiaEnergyIndices) {
        if (!apAppliedDarkGaiaEnergyIndices.contains(index)) {
            apDarkGaiaEnergyIndicesInFlight.insert(index);
            --apPendingDarkGaiaEnergy;
            return 1;
        }
    }
    return 0;
}

std::uint32_t APClient::GetPendingShieldCharge()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apShieldChargeStateLoadedSuccessfully &&
        std::filesystem::exists(GetShieldChargeStatePath()))
        return 0;

    if (!apShieldChargeIndicesInFlight.empty() ||
        apPendingShieldCharges == 0)
        return 0;

    for (const auto index : apReceivedShieldChargeIndices)
    {
        if (!apAppliedShieldChargeIndices.contains(index))
        {
            apShieldChargeIndicesInFlight.insert(index);
            --apPendingShieldCharges;
            return 1;
        }
    }

    return 0;
}
std::uint32_t APClient::GetPendingFullRing()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apFullRingStateLoadedSuccessfully &&
        std::filesystem::exists(GetFullRingStatePath()))
        return 0;

    if (!apFullRingIndicesInFlight.empty() || apPendingFullRings == 0)
        return 0;

    for (const auto index : apReceivedFullRingIndices)
    {
        if (!apAppliedFullRingIndices.contains(index))
        {
            apFullRingIndicesInFlight.insert(index);
            --apPendingFullRings;
            return 1;
        }
    }

    return 0;
}
std::uint32_t APClient::GetPendingPowerShield()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apPowerShieldStateLoadedSuccessfully &&
        std::filesystem::exists(GetPowerShieldStatePath()))
        return 0;

    if (apPendingPowerShield == 0 ||
        !apPowerShieldIndicesInFlight.empty())
        return 0;

    for (const auto index : apReceivedPowerShieldIndices)
    {
        if (!apAppliedPowerShieldIndices.contains(index))
        {
            apPowerShieldIndicesInFlight.insert(index);
            --apPendingPowerShield;
            return 1;
        }
    }

    return 0;
}

std::uint32_t APClient::GetPendingSuperClaws() {
    std::lock_guard<std::mutex> lock(apMutex);
    if (apSeedName.empty()) return 0;
    if (!apSuperClawsStateLoadedSuccessfully && std::filesystem::exists(GetSuperClawsStatePath())) return 0;
    if (!apSuperClawsIndicesInFlight.empty() || apPendingSuperClaws == 0) return 0;
    for (const auto index : apReceivedSuperClawsIndices) {
        if (!apAppliedSuperClawsIndices.contains(index)) {
            apSuperClawsIndicesInFlight.insert(index);
            --apPendingSuperClaws;
            return 1;
        }
    }
    return 0;
}


void APClient::ConfirmDarkGaiaEnergyApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);
    if (apDarkGaiaEnergyIndicesInFlight.empty()) return;
    for (const auto index : apDarkGaiaEnergyIndicesInFlight)
        apAppliedDarkGaiaEnergyIndices.insert(index);
    if (!SaveDarkGaiaEnergyState())
    {
        for (const auto index : apDarkGaiaEnergyIndicesInFlight)
            apAppliedDarkGaiaEnergyIndices.erase(index);
        std::printf("AP DARK GAIA ENERGY CONFIRM SAVE FAILED\n");
        return;
    }
    apDarkGaiaEnergyIndicesInFlight.clear();
    std::printf("AP DARK GAIA ENERGY CONFIRMED\n");
}

void APClient::ConfirmPowerShieldApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apPowerShieldIndicesInFlight.empty())
        return;

    for (const auto index : apPowerShieldIndicesInFlight)
        apAppliedPowerShieldIndices.insert(index);

    if (!SavePowerShieldState())
    {
        for (const auto index : apPowerShieldIndicesInFlight)
            apAppliedPowerShieldIndices.erase(index);

        std::printf("AP POWER SHIELD CONFIRM SAVE FAILED\n");
        return;
    }

    apPowerShieldIndicesInFlight.clear();
    std::printf("AP POWER SHIELD APPLIED AND SAVED\n");
}

void APClient::ConfirmSuperClawsApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);
    if (apSuperClawsIndicesInFlight.empty()) return;
    for (const auto index : apSuperClawsIndicesInFlight)
        apAppliedSuperClawsIndices.insert(index);
    if (!SaveSuperClawsState())
    {
        for (const auto index : apSuperClawsIndicesInFlight)
            apAppliedSuperClawsIndices.erase(index);
        std::printf("AP SUPER CLAWS CONFIRM SAVE FAILED\n");
        return;
    }
    apSuperClawsIndicesInFlight.clear();
    std::printf("AP SUPER CLAWS CONFIRMED\n");
}

void APClient::CancelDarkGaiaEnergy()
{
    std::lock_guard<std::mutex> lock(apMutex);
    if (apDarkGaiaEnergyIndicesInFlight.empty()) return;
    apPendingDarkGaiaEnergy += static_cast<std::uint32_t>(apDarkGaiaEnergyIndicesInFlight.size());
    apDarkGaiaEnergyIndicesInFlight.clear();
    std::printf("AP DARK GAIA ENERGY CANCELLED\n");
}

void APClient::ConfirmShieldChargeApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apShieldChargeIndicesInFlight.empty())
        return;

    for (const auto index : apShieldChargeIndicesInFlight)
        apAppliedShieldChargeIndices.insert(index);

    if (!SaveShieldChargeState())
    {
        for (const auto index : apShieldChargeIndicesInFlight)
            apAppliedShieldChargeIndices.erase(index);

        std::printf(
            "AP SHIELD CHARGE CONFIRM SAVE FAILED: retry needed\n"
        );
        return;
    }

    apShieldChargeIndicesInFlight.clear();

    std::printf("AP SHIELD CHARGE CONFIRMED\n");
}
void APClient::ConfirmFullRingApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apFullRingIndicesInFlight.empty())
        return;

    for (const auto index : apFullRingIndicesInFlight)
        apAppliedFullRingIndices.insert(index);

    if (!SaveFullRingState())
    {
        for (const auto index : apFullRingIndicesInFlight)
            apAppliedFullRingIndices.erase(index);

        std::printf(
            "AP FULL RING CONFIRM SAVE FAILED: Full Ring already granted, retry needed\n"
        );
        return;
    }

    apFullRingIndicesInFlight.clear();

    std::printf("AP FULL RING CONFIRMED\n");
}

void APClient::CancelShieldCharge()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apShieldChargeIndicesInFlight.empty())
        return;

    apPendingShieldCharges +=
        static_cast<std::uint32_t>(apShieldChargeIndicesInFlight.size());

    apShieldChargeIndicesInFlight.clear();

    std::printf(
        "AP SHIELD CHARGE CANCELLED: item returned to pending queue\n"
    );
}
void APClient::CancelFullRing()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apFullRingIndicesInFlight.empty())
        return;

    apPendingFullRings +=
        static_cast<std::uint32_t>(apFullRingIndicesInFlight.size());

    apFullRingIndicesInFlight.clear();

    std::printf("AP FULL RING CANCELLED: item returned to pending queue\n");
}
std::uint32_t APClient::GetPendingLifeBonus()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apLifeStateLoadedSuccessfully &&
        std::filesystem::exists(GetLifeStatePath()))
        return 0;

    if (!apLifeIndicesInFlight.empty() || apPendingLives == 0)
        return 0;

    for (const auto index : apReceivedLifeIndices)
    {
        if (!apAppliedLifeIndices.contains(index))
        {
            apLifeIndicesInFlight.insert(index);
            --apPendingLives;
            return 1;
        }
    }

    return 0;
}
void APClient::ConfirmLifeBonusApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apLifeIndicesInFlight.empty())
        return;

    for (const auto index : apLifeIndicesInFlight)
        apAppliedLifeIndices.insert(index);

    if (!SaveLifeState())
    {
        for (const auto index : apLifeIndicesInFlight)
            apAppliedLifeIndices.erase(index);

        // Conserver les index en cours pour bloquer une redistribution.
        std::printf("AP LIFE CONFIRM SAVE FAILED: Life already granted, retry needed\n");
        return;
    }

    apLifeIndicesInFlight.clear();
    std::printf("AP LIFE CONFIRMED: Life\n");
}
void APClient::QueueDeathLink()
{
    {
        std::lock_guard<std::mutex> lock(apMutex);

        std::printf(
            "[AP DEATH LINK DEBUG] authenticated=%d disconnect=%d\n",
            apAuthenticated ? 1 : 0,
            apDisconnectRequested ? 1 : 0
        );
        std::fflush(stdout);

        LOGFN("[AP DEATH LINK STATE] authenticated={} disconnect={} pending={} checksLoaded={}",
            apAuthenticated ? 1 : 0,
            apDisconnectRequested ? 1 : 0,
            apPendingDeathLinks,
            apCheckStateLoadedSuccessfully ? 1 : 0);

        if (!apAuthenticated || apDisconnectRequested)
            return;

        ++apPendingDeathLinks;
    }

    std::printf("[AP DEATH LINK QUEUED] Local death\n");
    std::fflush(stdout);

    apWake.notify_all();
}


// AP_DEATHLINK_HUB_GUARD_V1 - called from the actual player update hooks.
// Changing Acts discards stale Death Links received during scene transitions.
// Hub/menu stages are NEVER eligible and their pending Death Links are cleared.
namespace APClient {
void UpdateDeathLinkStage(const char* currentStage, bool isPlayableAct)
{
    const std::string nextStage = currentStage ? currentStage : "";
    std::lock_guard<std::mutex> lock(apMutex);
    const auto now = std::chrono::steady_clock::now();
    const bool sceneUpdateGap = apDeathLinkStageSeenAt.time_since_epoch().count() == 0 ||
        now - apDeathLinkStageSeenAt >= std::chrono::seconds(2);
    if (nextStage != apDeathLinkCurrentStage || !isPlayableAct || sceneUpdateGap)
    {
        if (apPendingIncomingDeathLinks != 0)
        {
            LOGFN("[AP DEATH LINK HUB GUARD] Discarded {} pending deaths (stage='{}')",
                apPendingIncomingDeathLinks, nextStage);
            apPendingIncomingDeathLinks = 0;
        }
    }
    apDeathLinkCurrentStage = nextStage;
    apDeathLinkStageEligible = isPlayableAct;
    apDeathLinkStageSeenAt = now;
}
}

// Recupere une mort entrante depuis le thread du jeu.
bool APClient::ConsumeIncomingDeathLink()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (!apDeathLinkStageEligible || apPendingIncomingDeathLinks == 0)
        return false;

    --apPendingIncomingDeathLinks;
    return true;
}
// AP_MEDAL_RADAR_PERSIST_V1 - queried by gameplay radar; no mutations.
bool APClient::IsLocationChecked(std::int64_t locationId)
{
    std::lock_guard<std::mutex> lock(apMutex);
    if (!apAuthenticated || apDisconnectRequested || !apCheckStateLoadedSuccessfully)
        return false;
    return apConfirmed.contains(locationId) || apPending.contains(locationId);
}

void APClient::QueueLocationCheck(std::int64_t locationId)
{
    std::size_t queuedCount = 0;
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apAuthenticated || apDisconnectRequested || !IsLocationAllowed(locationId))
            return;

        if (!apCheckStateLoadedSuccessfully)
        {
            std::printf("AP CHECK BLOCKED: save history unavailable\n");
            return;
        }

        // A qualifying rank unlocks all D/C/B/A checks for this SAME Act.
        // The existing hooks still decide which rank location IDs to report.
        std::int64_t rankBase = 0;
        int rankIndex = -1;
        const bool unlockAllRanks =
            GetRankLocationGroup(locationId, rankBase, rankIndex) &&
            rankIndex >= apRequiredRankForAllChecks;

        std::vector<std::int64_t> newChecks;
        if (unlockAllRanks)
        {
            for (int i = 0; i < 4; ++i)
            {
                const auto id = rankBase + i;
                if (IsLocationAllowed(id) && !apConfirmed.contains(id) &&
                    apPending.insert(id).second)
                    newChecks.push_back(id);
            }
        }
        else if (!apConfirmed.contains(locationId) &&
                 apPending.insert(locationId).second)
        {
            newChecks.push_back(locationId);
        }

        if (newChecks.empty())
            return;

        // Save all newly queued rank checks together. Roll back on failure.
        if (!SaveCheckState())
        {
            for (const auto id : newChecks)
                apPending.erase(id);
            std::printf("AP CHECK BLOCKED: save failed\n");
            return;
        }
        queuedCount = newChecks.size();
    }

    if (const char* medalName = AP_MedalDisplayName(locationId))
        std::printf("AP CHECK QUEUED: %lld (%s, %zu new checks)\n",
            static_cast<long long>(locationId), medalName, queuedCount);
    else
        std::printf("AP CHECK QUEUED: %lld (%zu new checks)\n",
            static_cast<long long>(locationId), queuedCount);
    std::fflush(stdout);
    apWake.notify_all();
}

void APClient::Start()
{
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (apStarted)
            return;

        apStarted = true;
        apDisconnectRequested = false;
        apConnectionState = APConnectionState::Connecting;
    }

    std::thread([]()
    {
        Connect();

        {
            std::lock_guard<std::mutex> lock(apMutex);
            apAuthenticated = false;
            apConnectionState = APConnectionState::Disconnected;
            apStarted = false;
        }

        apWake.notify_all();
    }).detach();
}

APClient::ConnectionState APClient::GetConnectionState()
{
    std::lock_guard<std::mutex> lock(apMutex);

    switch (apConnectionState)
    {
    case APConnectionState::Connecting:
        return ConnectionState::Connecting;
    case APConnectionState::Connected:
        return ConnectionState::Connected;
    case APConnectionState::Disconnecting:
        return ConnectionState::Disconnecting;
    default:
        return ConnectionState::Disconnected;
    }
}

// AP_MULTIWORLD_HUD_V082: UI thread drains a bounded queue, never blocks network I/O.
std::vector<APClient::MultiplayerEvent> APClient::DrainMultiplayerEvents()
{
    std::lock_guard<std::mutex> lock(apMutex);
    std::vector<MultiplayerEvent> result;
    result.swap(apMultiplayerQueue);
    return result;
}

std::string APClient::ResolveMultiplayerItemName(const MultiplayerEvent& event)
{
    // AP_PROGRESSIVE_MEDAL_NAMES_V1: the IDs did not change (88001001/88001002).
    // Normalize only items received by a Sonic Unleashed slot.  An older
    // Archipelago room may still advertise the original item names, while
    // the client should display the new names consistently in send/receive
    // notifications.  Never change names belonging to other games.
    std::lock_guard<std::mutex> lock(apMutex);
    const auto game = apMultiPlayerGames.find(event.destinationSlot);
    const bool isUnleashedItem =
        (game != apMultiPlayerGames.end() && game->second == "Sonic Unleashed") ||
        (apMultiOwnSlot >= 0 && event.destinationSlot == apMultiOwnSlot);

    if (isUnleashedItem)
    {
        if (event.itemId == kSunMedalItemId) return "Progressive Sun Medal";
        if (event.itemId == kMoonMedalItemId) return "Progressive Moon Medal";
    }

    if (!event.itemName.empty()) return event.itemName;
    if (game != apMultiPlayerGames.end())
    {
        const auto items = apMultiItemNames.find(game->second);
        if (items != apMultiItemNames.end())
        {
            const auto found = items->second.find(event.itemId);
            if (found != items->second.end()) return found->second;
        }
    }
    // Preserve pre-DataPackage fallbacks from the old client for its own slot.
    if (isUnleashedItem)
    {
        if (event.itemId == kStompItemId) return "Stomping Shoes";
        if (event.itemId == kWallJumpItemId) return "Wall Jump Shoes";
        if (event.itemId == kAirBoostItemId) return "Air Boost Shoes";
        if (event.itemId == kLightSpeedDashItemId) return "Light Speed Dash Shoes";
    }
    return "Item #" + std::to_string(event.itemId);
}
int APClient::GetMedalGoalTarget()
{
    std::lock_guard<std::mutex> lock(apMutex);
    return apMedalGoalTarget;
}

// AP_MEDALS_HUD_V1: confirmed medal checks, not received medal items.
namespace APClient {
    int GetMedalGoalCount() {
        std::lock_guard<std::mutex> lock(apMutex);
        if (!apAuthenticated || apSelectedGoal != 2) return -1;
        int count = 0;
        for (const auto id : apConfirmed)
            if (AP_MedalDisplayName(id) != nullptr) ++count;
        return count;
    }
}

void APClient::Disconnect()
{
    {
        std::lock_guard<std::mutex> lock(apMutex);

        if (!apStarted || apDisconnectRequested)
            return;

        apDisconnectRequested = true;
        apConnectionState = APConnectionState::Disconnecting;

        if (apSenderStopFlag != nullptr)
            *apSenderStopFlag = true;
    }

    apWake.notify_all();

    std::thread([]()
    {
        std::lock_guard<std::mutex> sendLock(apSendMutex);

        HINTERNET socket = nullptr;

        {
            std::lock_guard<std::mutex> lock(apMutex);

            if (apDisconnectRequested)
                socket = apActiveSocket;
        }

        if (socket != nullptr)
        {
            WinHttpWebSocketShutdown(
                socket,
                WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,
                nullptr,
                0
            );
        }
    }).detach();
}
void APClient::CancelPowerShield()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apPowerShieldIndicesInFlight.empty())
        return;

    apPendingPowerShield +=
        static_cast<std::uint32_t>(apPowerShieldIndicesInFlight.size());

    apPowerShieldIndicesInFlight.clear();

    std::printf("AP POWER SHIELD CANCELLED: item returned to pending\n");
}

void APClient::CancelSuperClaws()
{
    std::lock_guard<std::mutex> lock(apMutex);
    if (apSuperClawsIndicesInFlight.empty()) return;
    apPendingSuperClaws += static_cast<std::uint32_t>(apSuperClawsIndicesInFlight.size());
    apSuperClawsIndicesInFlight.clear();
    std::printf("AP SUPER CLAWS CANCELLED\n");
}

#else
// AP_DEATHLINK_HUB_GUARD_V1: native Linux client uses its own transport layer.
namespace APClient { void UpdateDeathLinkStage(const char*, bool) {} }
// AP_MULTIWORLD_HUD_V082: non-Windows build fallback.
std::vector<APClient::MultiplayerEvent> APClient::DrainMultiplayerEvents() { return {}; }
std::string APClient::ResolveMultiplayerItemName(const MultiplayerEvent& event)
{
    return !event.itemName.empty() ? event.itemName : "Item #" + std::to_string(event.itemId);
}
int APClient::GetMedalGoalTarget() { return 338; }

bool APClient::ConsumeIncomingDeathLink()
{
    return false;
}


// AP Crystal XP : implementations de secours hors Windows.
std::uint32_t APClient::GetPendingConsumable()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apConsumableStateLoadedSuccessfully &&
        std::filesystem::exists(GetConsumableStatePath()))
        return 0;

    if (!apConsumableIndicesInFlight.empty())
        return 0;

    // Choisir le plus petit index Archipelago non distribue.
    std::int64_t nextIndex = INT64_MAX;
    std::uint32_t nextItemId = 0;

    for (const auto& [index, itemId] : apConsumableItemIds)
    {
        if (!apAppliedConsumableIndices.contains(index) &&
            index < nextIndex)
        {
            nextIndex = index;
            nextItemId = itemId;
        }
    }

    if (nextItemId != 0)
        apConsumableIndicesInFlight.insert(nextIndex);

    return nextItemId;
}
void APClient::ConfirmConsumableApplied()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apConsumableIndicesInFlight.empty())
        return;

    for (const auto index : apConsumableIndicesInFlight)
        apAppliedConsumableIndices.insert(index);

    if (!SaveConsumableState())
    {
        for (const auto index : apConsumableIndicesInFlight)
            apAppliedConsumableIndices.erase(index);

        // Garder la recompense reservee pendant cette session
        // pour ne pas la distribuer une seconde fois.
        std::printf(
            "AP CONSUMABLE CONFIRM SAVE FAILED: retry needed\n"
        );
        return;
    }

    for (const auto index : apConsumableIndicesInFlight)
    {
        std::printf(
            "AP CONSUMABLE APPLIED: index=%lld\n",
            static_cast<long long>(index)
        );
    }

    apConsumableIndicesInFlight.clear();
}
void APClient::CancelConsumable()
{
    std::lock_guard<std::mutex> lock(apMutex);
    apConsumableIndicesInFlight.clear();
}
std::uint32_t APClient::GetPendingCrystalXPBonus()
{
    return 0;
}

void APClient::ConfirmCrystalXPBonusApplied()
{
}

std::uint32_t APClient::GetSunMedalBonus()
{
    return 0;
}

std::uint32_t APClient::GetMoonMedalBonus()
{
    return 0;
}

bool AP_TryGetReceivedShoeMask(std::uint8_t* outMask)
{
    if (outMask) *outMask = 0;
    return false; // AP_SHOES_SYNC_V082: version sans client AP
}

std::uint8_t AP_GetReceivedShoeMask()
{
    return 0;
}

void APClient::Start()
{
}

    // AP Ring Wipe Trap : implementations stub.
    std::uint32_t APClient::GetPendingRingWipe()
    {
        return 0;
    }

    void APClient::ConfirmRingWipeApplied()
    {
    }

    void APClient::CancelRingWipe()
    {
    }

std::uint32_t APClient::GetPendingReverseControls()
{
    return 0;
}

void APClient::ConfirmReverseControlsApplied()
{
}

void APClient::CancelReverseControls()
{
}
std::uint32_t APClient::GetPendingFreeze()
{
    return 0;
}

bool APClient::ConfirmFreezeApplied()
{
    return false;
}

void APClient::CancelFreeze()
{
}
std::uint32_t APClient::GetPendingBurn()
{
    return 0;
}

bool APClient::ConfirmBurnApplied()
{
    return false;
}

void APClient::CancelBurn()
{
}
std::uint32_t APClient::GetPendingParalyzed()
{
    return 0;
}

bool APClient::ConfirmParalyzedApplied()
{
    return false;
}

void APClient::CancelParalyzed()
{
}
std::uint32_t APClient::GetPendingRingBonus()
{
    return 0;
}

std::uint32_t APClient::GetPendingFullRing()
{
    std::lock_guard<std::mutex> lock(apMutex);

    if (apSeedName.empty())
        return 0;

    if (!apFullRingStateLoadedSuccessfully &&
        std::filesystem::exists(GetFullRingStatePath()))
        return 0;

    if (!apFullRingIndicesInFlight.empty() || apPendingFullRings == 0)
        return 0;

    for (const auto index : apReceivedFullRingIndices)
    {
        if (!apAppliedFullRingIndices.contains(index))
        {
            apFullRingIndicesInFlight.insert(index);
            --apPendingFullRings;
            return 1;
        }
    }

    return 0;
}
std::uint32_t APClient::GetPendingDarkGaiaEnergy() { return 0; }
std::uint32_t APClient::GetPendingSuperClaws() { return 0; }
std::uint32_t APClient::GetPendingPowerShield() { return 0; }
void APClient::ConfirmPowerShieldApplied() {}
APClient::ConnectionState APClient::GetConnectionState()
{
    return ConnectionState::Disconnected;
}

// AP_MEDALS_HUD_V1: confirmed medal checks, not received medal items.
namespace APClient {
    int GetMedalGoalCount() {
        std::lock_guard<std::mutex> lock(apMutex);
        if (!apAuthenticated || apSelectedGoal != 2) return -1;
        int count = 0;
        for (const auto id : apConfirmed)
            if (AP_MedalDisplayName(id) != nullptr) ++count;
        return count;
    }
}

void APClient::Disconnect() {}
void APClient::CancelPowerShield() {}
void APClient::ConfirmSuperClawsApplied() {}
void APClient::CancelSuperClaws() {}
void APClient::ConfirmDarkGaiaEnergyApplied() {}
void APClient::CancelDarkGaiaEnergy() {}

std::uint32_t APClient::GetPendingFullRing()
{
    return 0;
}

std::uint32_t APClient::GetPendingShieldCharge()
{
    return 0;
}

void APClient::ConfirmShieldChargeApplied()
{
}

void APClient::CancelShieldCharge()
{
}
void APClient::ConfirmFullRingApplied()
{
}

void APClient::CancelFullRing()
{
}
std::uint32_t APClient::GetPendingLifeBonus()
{
    return 0;
}

void APClient::ConfirmLifeBonusApplied()
{
}

void APClient::QueueDeathLink()
{
}

// AP_MEDAL_RADAR_PERSIST_V1 - non-Windows stub.
bool APClient::IsLocationChecked(std::int64_t)
{
    return false;
}

void APClient::QueueLocationCheck(std::int64_t)
{
}

#endif






























