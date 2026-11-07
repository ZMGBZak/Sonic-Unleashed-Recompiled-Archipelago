from BaseClasses import Item
from worlds.AutoWorld import World
from .Items import item_table, item_classifications
from .Locations import location_table, internal_to_id, MEDAL_LOCATION_IDS
from .Regions import create_regions
from .Options import SonicUnleashedOptions
from .Logic import create_event_locations, install_rules, shoe_location_allowed
from worlds.generic.Rules import add_item_rule

class SonicUnleashedWorld(World):
    game = "Sonic Unleashed"
    topology_present = True
    options_dataclass = SonicUnleashedOptions
    item_name_to_id = item_table
    location_name_to_id = location_table

    def create_regions(self):
        create_regions(self)
        create_event_locations(self)

    def create_item(self, name):
        return Item(name, item_classifications[name], self.item_name_to_id[name], self.player)

    def create_items(self):
        # 20 Sun Medal items (+6 each) and 20 Moon Medal items (+4 each).
        # Physical medal locations remain separate Archipelago checks.
        item_counts = {
            "Sun Medal": 20,
            "Moon Medal": 20,
            "Stomping Shoes": 1,
            "Wall Jump Shoes": 1,
            "Air Boost Shoes": 1,
            "Light Speed Dash Shoes": 1,
        }
        for name, count in item_counts.items():
            for _ in range(count):
                self.multiworld.itempool.append(self.create_item(name))

        # Match the item pool to the locations actually created for this slot.
        available_locations = sum(
            1 for name in location_table
            if (self.options.include_dlc.value or "(DLC)" not in name)
            and (self.options.include_collectibles.value or not name.startswith(("CD #", "Art Book #")))
        )
        remaining = available_locations - sum(item_counts.values())
        if remaining < 0:
            raise ValueError("Not enough locations for Sonic Unleashed item pool")
        # User-configurable trap counts; preserve a fixed item pool size.
        trap_counts = (
            ("Ring Wipe Trap", self.options.ring_wipe_trap_count.value),
            ("Reverse Controls Trap", self.options.reverse_controls_trap_count.value),
            ("QTE of Doom Trap", self.options.qte_of_doom_trap_count.value),
            ("Freeze Trap", self.options.freeze_trap_count.value),
            ("Paralyzed Trap", self.options.paralyzed_trap_count.value),
        )
        total_traps = sum(count for _, count in trap_counts)
        if total_traps > remaining:
            raise ValueError(
                f"Sonic Unleashed: requested {total_traps} traps but only "
                f"{remaining} item slots remain. Reduce the trap counts in your YAML."
            )
        for name, count in trap_counts:
            for _ in range(count):
                self.multiworld.itempool.append(self.create_item(name))
        remaining -= total_traps

        # Distribute remaining locations among fourteen filler rewards.
        filler_names = ("+10 Rings", "+50 Rings", "+100 Rings", "Bonus Life",
                        "Crystal XP +50", "Crystal XP +100", "Crystal XP +200",
                        "Full Ring", "Dark Gaia Energy", "Super Claws", "Shield Charge",
                        "Hotdog", "Fried Chicken", "Hero Sandwich")
        filler_counts = [remaining // len(filler_names)] * len(filler_names)
        for i in range(remaining % len(filler_names)):
            filler_counts[i] += 1
        for name, count in zip(filler_names, filler_counts):
            for _ in range(count):
                self.multiworld.itempool.append(self.create_item(name))

    def set_rules(self):
        # Real story/Gaia Gate logic: stage-clear events, medal packs and Shoes.
        # The client still reports the actual victory via CLIENT_GOAL.
        regions = {r.name: r for r in self.multiworld.regions if r.player == self.player}
        install_rules(self, regions)
        # Different shoes can appear in different DLC Acts, exactly as
        # specified in the progression whitelist.  Non-shoe rewards (including
        # Sun/Moon Medal packs) remain eligible if a location is reachable.
        for region in regions.values():
            for location in region.locations:
                if location.address is not None:
                    add_item_rule(location, lambda item, name=location.name:
                                  item.player != self.player or
                                  shoe_location_allowed(name, item.name))
        self.multiworld.completion_condition[self.player] = (
            lambda state: state.has("Victory", self.player)
        )

    def fill_slot_data(self):
        return {"version": 4, "apworld_version": "0.8.2", "medal_location_ids": internal_to_id, "goal": self.options.goal.value, "include_dlc": bool(self.options.include_dlc.value), "include_collectibles": bool(self.options.include_collectibles.value), "required_rank_for_all_checks": self.options.required_rank_for_all_checks.value, "medal_goal_target": len(MEDAL_LOCATION_IDS)}
