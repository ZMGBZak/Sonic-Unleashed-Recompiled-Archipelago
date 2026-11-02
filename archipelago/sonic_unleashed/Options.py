from dataclasses import dataclass

from Options import PerGameCommonOptions, Range, Choice, Toggle


class RingWipeTrapCount(Range):
    """Number of Ring Wipe Traps in the item pool."""
    display_name = "Ring Wipe Traps"
    range_start = 0
    range_end = 100
    default = 15


class ReverseControlsTrapCount(Range):
    """Number of Reverse Controls Traps in the item pool."""
    display_name = "Reverse Controls Traps"
    range_start = 0
    range_end = 100
    default = 15


class QteOfDoomTrapCount(Range):
    """Number of QTE of Doom Traps in the item pool."""
    display_name = "QTE of Doom Traps"
    range_start = 0
    range_end = 100
    default = 15


class FreezeTrapCount(Range):
    """Number of Freeze Traps in the item pool."""
    display_name = "Freeze Traps"
    range_start = 0
    range_end = 100
    default = 15



class ParalyzedTrapCount(Range):
    """Number of Paralyzed Traps in the item pool."""
    display_name = "Paralyzed Traps"
    range_start = 0
    range_end = 100
    default = 0


class Goal(Choice):
    """Choose how to complete your Archipelago game:
    Defeat Dark Gaia: defeat the final Dark Gaia boss.
    Defeat Egg Dragoon: defeat Egg Dragoon in Eggmanland.
    Collect All Medals: collect all 338 unique Archipelago medal checks (after the 32 shared Entrance duplicates were removed).
    """
    display_name = "Goal"
    option_defeat_dark_gaia = 0
    option_defeat_egg_dragoon = 1
    option_collect_all_medals = 2
    default = 0


class IncludeDLC(Toggle):
    """Include DLC Acts and their rank checks in the location pool."""
    display_name = "Include DLC Acts"
    default = 1


class IncludeCollectibles(Toggle):
    """Include all 62 CDs (Records) and 69 Art Books as Archipelago checks.
    Disable this to remove these 131 checks and their corresponding item slots.
    You can still collect CDs and Art Books normally in the game.
    """
    display_name = "Include CDs and Art Books"
    default = 1


class RequiredRankForAllChecks(Choice):
    """Minimum rank that completes all D, C, B and A checks for an Act."""
    display_name = "Required Rank for All Rank Checks"
    option_D = 0
    option_C = 1
    option_B = 2
    option_A = 3
    default = 3


@dataclass
class SonicUnleashedOptions(PerGameCommonOptions):
    goal: Goal
    include_dlc: IncludeDLC
    include_collectibles: IncludeCollectibles
    required_rank_for_all_checks: RequiredRankForAllChecks
    ring_wipe_trap_count: RingWipeTrapCount
    reverse_controls_trap_count: ReverseControlsTrapCount
    qte_of_doom_trap_count: QteOfDoomTrapCount
    freeze_trap_count: FreezeTrapCount
    paralyzed_trap_count: ParalyzedTrapCount
