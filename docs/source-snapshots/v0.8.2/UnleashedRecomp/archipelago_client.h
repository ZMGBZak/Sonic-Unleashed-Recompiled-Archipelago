
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace APClient
{
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting
    };

    // AP_MULTIWORLD_HUD_V082: per-room live exchanges (never ReceivedItems replay).
    struct MultiplayerEvent
    {
        std::int64_t itemId = -1;
        int sourceSlot = -1;
        int destinationSlot = -1;
        bool isProgression = false;
        bool receivedLocally = false;
        std::string sourceName;
        std::string destinationName;
        std::string itemName;
    };
    std::vector<MultiplayerEvent> DrainMultiplayerEvents();
    std::string ResolveMultiplayerItemName(const MultiplayerEvent& event);
    int GetMedalGoalCount();
    int GetMedalGoalTarget();

    void Start();
    void Disconnect();
    ConnectionState GetConnectionState();

    // Ajoute un check ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â  la file d'attente rÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â©seau.
    // True for pending/saved or server-confirmed checks of the active AP slot.
    bool IsLocationChecked(std::int64_t locationId);
    void QueueLocationCheck(std::int64_t locationId);

    // Enregistre une mort locale pour l'envoi Death Link.
    void QueueDeathLink();

    // Recupere une mort Archipelago en attente.
    bool ConsumeIncomingDeathLink();

    // Bonus virtuels des medailles recues d'Archipelago.
    std::uint32_t GetSunMedalBonus();
    std::uint32_t GetMoonMedalBonus();

    // Bonus Rings Archipelago en attente d'application.
    std::uint32_t GetPendingRingBonus();
    void ConfirmRingBonusApplied();

    // AP Ring Wipe Trap.
    std::uint32_t GetPendingRingWipe();
    void ConfirmRingWipeApplied();
    void CancelRingWipe();
    // AP Reverse Controls Trap : inversion pendant 10 secondes.
    std::uint32_t GetPendingReverseControls();
    void ConfirmReverseControlsApplied();
    std::uint32_t GetPendingFreeze();
bool ConfirmFreezeApplied();
void CancelFreeze();

std::uint32_t GetPendingBurn();
bool ConfirmBurnApplied();
void CancelBurn();

std::uint32_t GetPendingParalyzed();
bool ConfirmParalyzedApplied();
void CancelParalyzed();
void CancelReverseControls();
    // AP QTE of Doom Trap
    std::uint32_t GetPendingQteOfDoom();
    bool ConfirmQteOfDoomApplied();
    void CancelQteOfDoom();
    std::uint64_t GetQteOfDoomGeneration();

    // Bonus Life Archipelago en attente d'application.
    std::uint32_t GetPendingDarkGaiaEnergy();
    std::uint32_t GetPendingPowerShield();
    void ConfirmPowerShieldApplied();
    void CancelPowerShield();
    std::uint32_t GetPendingSuperClaws();
    void ConfirmSuperClawsApplied();
    void CancelSuperClaws();
    void ConfirmDarkGaiaEnergyApplied();
    void CancelDarkGaiaEnergy();
    std::uint32_t GetPendingFullRing();
    std::uint32_t GetPendingShieldCharge();
    void ConfirmShieldChargeApplied();
    void CancelShieldCharge();
void ConfirmFullRingApplied();
    void CancelFullRing();
std::uint32_t GetPendingLifeBonus();
    void ConfirmLifeBonusApplied();

    // AP Crystal XP : recuperation et confirmation.
    std::uint32_t GetPendingCrystalXPBonus();
    void ConfirmCrystalXPBonusApplied();

    // Consommables Archipelago : Hotdog, Fried Chicken, Hero Sandwich.
    std::uint32_t GetPendingConsumable();
    void ConfirmConsumableApplied();
    void CancelConsumable();
}

// Test local temporaire des Rings Archipelago.
void AP_TestLocalRings();




