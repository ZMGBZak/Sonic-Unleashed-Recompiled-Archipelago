"""Sonic Unleashed Archipelago progression logic (V0.8.2 group-test candidate).

The AP client handles ACTUAL goal completion.  These rules model the route and
required abilities for Archipelago's fill/progression-balancing algorithms.
No existing location or item IDs are changed by this module.
"""

import re
from BaseClasses import Item, ItemClassification, Location
from worlds.generic.Rules import set_rule
from .Locations import location_table

# Each received medal item grants 6 Sun or 4 Moon medals in the AP client.
# Thus 5 Sun items = 30 medals; 20 Sun = 120; 20 Moon = 80.
# Requirements are a *conservative story-route model*; optional stage and
# collectible shortcuts are not assumed to be exhaustively documented.
STORY = {
    "Windmill Isle - Day Act 1": ((), (), 0, 0),
    "Windmill Isle - Day Act 2": (("Windmill Isle - Day Act 1",), (), 0, 0),
    "Windmill Isle - Night Act 1": (("Windmill Isle - Day Act 2",), (), 0, 0),
    "Savannah Citadel - Night Act 1": (("Windmill Isle - Night Act 1",), (), 0, 0),
    "Savannah Citadel - Day Act 1": (("Savannah Citadel - Night Act 1",), (), 0, 0),
    "Egg Beetle": (("Savannah Citadel - Day Act 1",), (), 0, 0),
    "Cool Edge - Day Act 1": (("Egg Beetle",), ("Stomping Shoes",), 0, 0),
    "Rooftop Run - Night Act 1": (("Egg Beetle",), (), 0, 0),
    "Dragon Road - Night Act 1": (("Cool Edge - Day Act 1", "Rooftop Run - Night Act 1"), (), 0, 0),
    "Dark Gaia Phoenix": (("Dragon Road - Night Act 1",), (), 0, 0),
    "Rooftop Run - Day Act 1": (("Dark Gaia Phoenix",), ("Wall Jump Shoes",), 5, 0),
    "Dragon Road - Day Act 1": (("Dark Gaia Phoenix",), ("Air Boost Shoes",), 0, 0),
    "Egg Devil Ray": (("Rooftop Run - Day Act 1", "Dragon Road - Day Act 1"), (), 0, 0),
    "Cool Edge - Night Act 1": (("Egg Devil Ray",), (), 0, 0),
    "Dark Moray": (("Cool Edge - Night Act 1",), (), 0, 0),
    "Arid Sands - Day Act 1": (("Dark Moray",), ("Light Speed Dash Shoes",), 0, 0),
    "Skyscraper Scamper - Night Act 1": (("Dark Moray",), (), 0, 0),
    "Arid Sands - Night Act 1": (("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"), (), 0, 0),
    "Dark Guardian": (("Arid Sands - Night Act 1",), (), 0, 0),
    "Skyscraper Scamper - Day Act 1": (("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"), (), 0, 0),
    "Jungle Joyride - Night Act 1": (("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"), (), 0, 0),
    "Jungle Joyride - Day Act 1": (("Dark Guardian", "Skyscraper Scamper - Day Act 1", "Jungle Joyride - Night Act 1"), (), 20, 0),
    "Egg Lancer": (("Jungle Joyride - Day Act 1",), (), 0, 0),
    "Eggmanland": (("Egg Lancer",), (), 20, 20),
    "Egg Dragoon": (("Eggmanland",), (), 0, 0),
    "Final Dark Gaia": (("Egg Dragoon",), (), 0, 0),
}

# Optional Gaia Gates must account for the actual in-game medal requirements,
# otherwise fill may hide a required Shoe behind an inaccessible Act.
OPTIONAL_ACT_MEDAL_REQUIREMENTS = {
    "Savannah Citadel - Day Act 3": (5, 0),  # Sun Medal level 3 (30 medals).
}

STAGE_COUNTRIES = {
    "Windmill Isle": "Apotos",
    "Savannah Citadel": "Mazuri",
    "Rooftop Run": "Spagonia",
    "Cool Edge": "Holoska",
    "Dragon Road": "Chun-nan",
    "Arid Sands": "Shamar",
    "Skyscraper Scamper": "Empire City",
    "Jungle Joyride": "Adabat",
    "Eggmanland": "Eggmanland",
    "Egg Beetle": "Mazuri",
    "Dark Gaia Phoenix": "Chun-nan",
    "Egg Devil Ray": "Spagonia",
    "Dark Moray": "Holoska",
    "Dark Guardian": "Shamar",
    "Egg Lancer": "Adabat",
    "Egg Dragoon": "Eggmanland",
    "Final Dark Gaia": "Eggmanland",
}

# Countries can be visited before some of their Gaia Gates open.  Keep the
# regional threshold separate from the stage/medal/ability requirements.
COUNTRY_ACCESS = {
    "Apotos": (),
    "Spagonia": ("Windmill Isle - Night Act 1",),
    "Mazuri": ("Windmill Isle - Night Act 1",),
    "Holoska": ("Egg Beetle",),
    "Chun-nan": ("Cool Edge - Day Act 1", "Rooftop Run - Night Act 1"),
    "Shamar": ("Dark Moray",),
    "Empire City": ("Dark Moray",),
    "Adabat": ("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"),
    "Eggmanland": ("Egg Lancer",),
}

ACT_RE = re.compile(r"^(?P<stage>.+?) - (?P<time>Day|Night) Act (?P<act>\d+)(?: - \d+)?(?: \(DLC\))?$")
ACT_IN_COLLECTIBLE_RE = re.compile(r"^(.+?) - (Day|Night) Act (\d+)")
RANK_OR_CLEAR_RE = re.compile(r" - (?:Act Clear|Boss Clear|Rank [DCBA])$")
MEDAL_RE = re.compile(r" - (?:Sun|Moon) Medal(?: \(\d+\))?$")


def event_item(stage):
    return "Cleared: " + stage


def _stage_req(state, player, stage):
    prereqs, shoes, sun, moon = STORY[stage]
    return (all(state.has(event_item(prev), player) for prev in prereqs)
            and all(state.has(shoe, player) for shoe in shoes)
            and state.has("Sun Medal", player, sun)
            and state.has("Moon Medal", player, moon))


def _stage_access(state, player, stem):
    if stem in STORY:
        return _stage_req(state, player, stem)

    # This specific optional gate is documented: it opens only after Egg
    # Beetle and Sun Level 3 (30 physical medals = five +6 AP packages).
    if stem == "Windmill Isle - Day Act 3":
        return (state.has(event_item("Egg Beetle"), player)
                and state.has("Sun Medal", player, 5))

    match = ACT_RE.match(stem)
    if match:
        stage, time, act = match.group("stage"), match.group("time"), int(match.group("act"))
        root = f"{stage} - {time} Act 1"
        if root in STORY:
            # The additional Acts are accessed after their first main Act.
            # Some optional Gaia Gates may need MORE medals; those can be added
            # individually once their exact requirements are verified in game.
            sun, moon = OPTIONAL_ACT_MEDAL_REQUIREMENTS.get(stem, (0, 0))
            return (state.has(event_item(root), player)
                    and state.has("Sun Medal", player, sun)
                    and state.has("Moon Medal", player, moon))
        return False  # Do not accidentally declare unknown stages accessible.

    return False


def _region_for_stage(stem):
    for prefix, region in STAGE_COUNTRIES.items():
        if stem == prefix or stem.startswith(prefix + " -"):
            return region
    return None


def _region_access(state, player, region):
    return all(state.has(event_item(stage), player) for stage in COUNTRY_ACCESS.get(region, ()))


def _medal_access(state, player, name):
    stem = MEDAL_RE.sub("", name)
    if stem.startswith("Apotos - Village (Night)"):
        return state.has(event_item("Windmill Isle - Day Act 2"), player)
    if " - Village " in stem or " - Town " in stem or " - City " in stem or " - Entrance" in stem:
        return True  # The location's actual country region already gates it.
    if " - Boss" in stem:
        boss = stem.split(" - Boss")[0]
        return _stage_access(state, player, boss)
    if " - Day Side Act" in stem:
        return _stage_access(state, player, stem.split(" - Day Side Act")[0] + " - Day Act 2")
    if " - Night Side Act" in stem:
        return _stage_access(state, player, stem.split(" - Night Side Act")[0] + " - Night Act 2")
    if " - Night Mission" in stem:
        return _stage_access(state, player, stem.split(" - Night Mission")[0] + " - Night Act 1")
    if stem.endswith(" - Day"):
        return _stage_access(state, player, stem + " Act 1")
    if stem.endswith(" - Night"):
        return _stage_access(state, player, stem + " Act 1")
    if stem.endswith(" - Night Act"):
        return _stage_access(state, player, stem + " 1")
    return _stage_access(state, player, stem)


def _collectible_access(state, player, name):
    # Naming format: 'CD #N - Location - ... (Record Title)'
    # Covers exact stage and Entrance Stage labels (from V0.8.2 wiki pass).
    _, _, source = name.partition(" - ")
    country = None
    stage_prefix = None
    for prefix, region in STAGE_COUNTRIES.items():
        if source == prefix or source.startswith(prefix + " -"):
            country = region
            stage_prefix = prefix
            break
    if country is None:
        for region in COUNTRY_ACCESS:
            if source == region or source.startswith(region + " -"):
                country = region
                break
    if country is None:
        # Professor Pickle's Lab / Traveling Salesman Wentos: appear after
        # the early story visit to Spagonia.
        return _region_access(state, player, "Spagonia")
    if not _region_access(state, player, country):
        return False
    if source.startswith("Eggmanland - Eggshop"):
        return state.has(event_item("Final Dark Gaia"), player)
    if country == "Eggmanland":
        return _stage_access(state, player, "Eggmanland")
    if stage_prefix:
        match = ACT_IN_COLLECTIBLE_RE.match(source)
        if match:
            stem = f"{match.group(1)} - {match.group(2)} Act {match.group(3)}"
            return _stage_access(state, player, stem)
    return True


def location_access(state, player, name):
    if name.startswith(("CD #", "Art Book #")):
        return _collectible_access(state, player, name)
    if " Medal" in name:
        return _medal_access(state, player, name)
    stem = RANK_OR_CLEAR_RE.sub("", name)
    return _stage_access(state, player, stem)


def create_event_locations(world):
    """Must run during create_regions, BEFORE create_items (Archipelago API)."""
    player = world.player
    regions = {r.name: r for r in world.multiworld.regions if r.player == player}
    world._sonic_story_events = {}
    for stage, country in STORY_STAGE_REGION.items():
        region = regions[country]
        loc = Location(player, "Story Event: " + stage, None, region)
        loc.place_locked_item(Item(event_item(stage), ItemClassification.progression, None, player))
        region.locations.append(loc)
        world._sonic_story_events[stage] = loc

    menu = regions["Menu"]
    victory = Location(player, "Story Event: Victory", None, menu)
    victory.place_locked_item(Item("Victory", ItemClassification.progression, None, player))
    menu.locations.append(victory)
    world._sonic_victory_event = victory


def install_rules(world, regions):
    """Only set access rules during set_rules; don't create any new locations."""
    player = world.player
    menu = regions["Menu"]
    for entrance in menu.exits:
        name = entrance.connected_region.name
        if name in COUNTRY_ACCESS:
            set_rule(entrance, lambda state, name=name: _region_access(state, player, name))

    for stage, loc in world._sonic_story_events.items():
        set_rule(loc, lambda state, stage=stage: _stage_access(state, player, stage))

    for region in regions.values():
        for loc in region.locations:
            if loc.address is not None and loc.name in location_table:
                set_rule(loc, lambda state, name=loc.name: location_access(state, player, name))

    medal_names = tuple(n for n, i in location_table.items()
                        if 88000001 <= i <= 88000370
                        and (world.options.include_dlc.value or "(DLC)" not in n))
    goal = world.options.goal.value
    victory = world._sonic_victory_event
    if goal == 0:
        set_rule(victory, lambda state: state.has(event_item("Final Dark Gaia"), player))
    elif goal == 1:
        set_rule(victory, lambda state: state.has(event_item("Egg Dragoon"), player))
    else:
        set_rule(victory, lambda state: all(state.can_reach(name, "Location", player)
                                            for name in medal_names))


STORY_STAGE_REGION = {stage: _region_for_stage(stage) for stage in STORY}
assert all(STORY_STAGE_REGION.values()), "Story stage missing region"
