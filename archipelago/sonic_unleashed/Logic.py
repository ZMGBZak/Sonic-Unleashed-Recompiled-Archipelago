"""Sonic Unleashed Archipelago progression logic (V0.8.2 medal-gate hotfix).

The AP client handles ACTUAL goal completion.  These rules model the route and
required abilities for Archipelago's fill/progression-balancing algorithms.
No existing location or item IDs are changed by this module.
"""

import re
from BaseClasses import Item, ItemClassification, Location
from worlds.generic.Rules import set_rule
from .Locations import location_table, MEDAL_LOCATION_IDS

# Each received AP medal item grants 6 Sun or 4 Moon medals in the client.
# Requirements below are counts of *received AP items*, not physical checks:
#   physical medals: 15 -> Sun 3 or Moon 4; 30 -> Sun 5 or Moon 8;
#   45 -> Sun 8 or Moon 12; 60 -> Sun 10 or Moon 15;
#   80 Moon -> Moon 20; 120 Sun -> Sun 20.
# Use ceil(required_medals / medals_per_item), so the item alone suffices.
# AP cannot infer actual collected/check-count medals from CollectionState.
# This is conservative where physical medals can also unlock Gaia Gates.
# Country regions unlock with story events; Gaia Gates with medal items.
# Optional Act Gaia Gate requirements still need separate verification.
STORY = {
    "Windmill Isle - Day Act 1": ((), (), 0, 0),
    "Windmill Isle - Day Act 2": (("Windmill Isle - Day Act 1",), (), 0, 0),
    "Windmill Isle - Night Act 1": (("Windmill Isle - Day Act 2",), (), 0, 0),
    "Savannah Citadel - Night Act 1": (("Windmill Isle - Night Act 1",), (), 0, 0),
    "Savannah Citadel - Day Act 1": (("Savannah Citadel - Night Act 1",), (), 0, 0),
    "Egg Beetle": (("Savannah Citadel - Day Act 1",), (), 0, 0),
    "Cool Edge - Day Act 1": (("Egg Beetle",), ("Stomping Shoes",), 3, 0),
    "Rooftop Run - Night Act 1": (("Egg Beetle",), ("Stomping Shoes", "Wall Jump Shoes"), 0, 4),
    "Dragon Road - Night Act 1": (("Cool Edge - Day Act 1", "Rooftop Run - Night Act 1"), (), 0, 8),
    "Dark Gaia Phoenix": (("Dragon Road - Night Act 1",), (), 0, 0),
    "Rooftop Run - Day Act 1": (("Dark Gaia Phoenix",), ("Stomping Shoes", "Wall Jump Shoes"), 5, 0),
    "Dragon Road - Day Act 1": (("Dark Gaia Phoenix",), ("Air Boost Shoes",), 8, 0),
    "Egg Devil Ray": (("Rooftop Run - Day Act 1", "Dragon Road - Day Act 1"), (), 0, 0),
    "Cool Edge - Night Act 1": (("Egg Devil Ray",), (), 0, 12),
    "Dark Moray": (("Cool Edge - Night Act 1",), (), 0, 0),
    "Arid Sands - Day Act 1": (("Dark Moray",), ("Light Speed Dash Shoes",), 10, 0),
    "Skyscraper Scamper - Night Act 1": (("Dark Moray",), (), 0, 15),
    "Arid Sands - Night Act 1": (("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"), (), 0, 20),
    "Dark Guardian": (("Arid Sands - Night Act 1",), (), 0, 0),
    "Skyscraper Scamper - Day Act 1": (("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"), (), 10, 0),
    "Jungle Joyride - Night Act 1": (("Arid Sands - Day Act 1", "Skyscraper Scamper - Night Act 1"), (), 0, 15),
    "Jungle Joyride - Day Act 1": (("Dark Guardian", "Skyscraper Scamper - Day Act 1", "Jungle Joyride - Night Act 1"), (), 20, 0),
    "Egg Lancer": (("Jungle Joyride - Day Act 1",), (), 0, 0),
    "Eggmanland": (("Egg Lancer",), (), 20, 20),
    "Egg Dragoon": (("Eggmanland",), (), 0, 0),
    "Final Dark Gaia": (("Egg Dragoon",), (), 0, 0),
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

# Spagonia City stays reachable at the normal story point.  The mission
# Entrance Stage is a distinct physical area requiring BOTH progression Shoes.
SPAGONIA_ENTRANCE_SHOES = ("Stomping Shoes", "Wall Jump Shoes")


def _spagonia_entrance_access(state, player):
    return all(state.has(shoe, player) for shoe in SPAGONIA_ENTRANCE_SHOES)


ACT_RE = re.compile(r"^(?P<stage>.+?) - (?P<time>Day|Night) Act (?P<act>\d+)(?: - \d+)?(?: \(DLC\))?$")
ACT_IN_COLLECTIBLE_RE = re.compile(r"^(.+?) - (Day|Night) Act (\d+)")
RANK_OR_CLEAR_RE = re.compile(r" - (?:Act Clear|Boss Clear|Rank [DCBA])$")
MEDAL_RE = re.compile(r" - (?:Sun|Moon) Medal(?: \(\d+\))?$")



# AP V0.8.3 shoes/multiworld safety: user-verified DLC acts that can
# contain each Shoe before the first story Act requiring it.  This is a
# PLACEMENT policy. It is intentionally NOT a claim that every DLC has
# a particular medal-level requirement.
FIRST_SHOES_DLC = {
    "Windmill Isle - Day Act 1 - 2 (DLC)",
    "Windmill Isle - Day Act 4 (DLC)",
    "Savannah Citadel - Day Act 1 - 2 (DLC)",
    "Savannah Citadel - Day Act 3 - 2 (DLC)",
    "Savannah Citadel - Day Act 5 (DLC)",
}

AIR_BOOST_EXTRA_DLC = {
    "Savannah Citadel - Day Act 4 (DLC)",
    "Rooftop Run - Day Act 1 - 2 (DLC)",
    "Rooftop Run - Day Act 2 - 2 (DLC)",
    "Rooftop Run - Day Act 4 (DLC)",
    "Cool Edge - Day Act 1 - 2 (DLC)",
    "Cool Edge - Day Act 2 - 2 (DLC)",
    "Cool Edge - Day Act 3 (DLC)",
}

LIGHT_SPEED_EXTRA_DLC = {
    "Rooftop Run - Day Act 5 (DLC)",
    "Cool Edge - Day Act 4 (DLC)",
    "Dragon Road - Day Act 4 (DLC)",
    "Dragon Road - Day Act 5 (DLC)",
}

# Get the real DLC names from Locations.py instead of guessing indices.
_ALL_DLC_STEMS = {
    name.removesuffix(" - Act Clear")
    for name in location_table
    if "(DLC)" in name and name.endswith(" - Act Clear")
}
FIRST_SHOES_DLC |= {
    stem for stem in _ALL_DLC_STEMS
    if stem.startswith(("Windmill Isle - Night Act", "Savannah Citadel - Night Act"))
}
AIR_BOOST_DLC = FIRST_SHOES_DLC | AIR_BOOST_EXTRA_DLC | {
    stem for stem in _ALL_DLC_STEMS
    if stem.startswith(("Rooftop Run - Night Act", "Cool Edge - Night Act"))
}
LIGHT_SPEED_DLC = AIR_BOOST_DLC | LIGHT_SPEED_EXTRA_DLC | {
    stem for stem in _ALL_DLC_STEMS if stem.startswith("Dragon Road - Night Act")
}
assert LIGHT_SPEED_DLC <= _ALL_DLC_STEMS, "DLC shoe whitelist contains unknown Acts"

SHOE_DLC_ALLOWED = {
    "Stomping Shoes": FIRST_SHOES_DLC,
    "Wall Jump Shoes": FIRST_SHOES_DLC,
    "Air Boost Shoes": AIR_BOOST_DLC,
    "Light Speed Dash Shoes": LIGHT_SPEED_DLC,
}

SHOE_DEADLINE = {
    "Stomping Shoes": "Cool Edge - Day Act 1",
    "Wall Jump Shoes": "Cool Edge - Day Act 1",
    "Air Boost Shoes": "Dragon Road - Day Act 1",
    "Light Speed Dash Shoes": "Arid Sands - Day Act 1",
}

# Named optional vanilla Acts that are NOT safe for the first two Shoes:
# Windmill Day 3 and Savannah Day 2/3 need Sun level 3 (30 physical
# medals), whereas Cool Edge Day 1 needs only Sun level 2 (15).
_OPTIONAL_BEFORE_AIR_BOOST = {
    "Windmill Isle - Day Act 3",
    "Savannah Citadel - Day Act 2",
    "Savannah Citadel - Day Act 3",
}

# Restrict Shoes to vetted non-DLC check families and to the exact DLC
# whitelists above. CD/Art Book locations are not included because their
# physical reachability has not been verified against these deadlines.
def shoe_location_allowed(location_name, shoe):
    if shoe not in SHOE_DLC_ALLOWED:
        return True
    if location_name.startswith(("CD #", "Art Book #")):
        return False

    stem = RANK_OR_CLEAR_RE.sub("", MEDAL_RE.sub("", location_name))
    if "(DLC)" in location_name:
        return stem in SHOE_DLC_ALLOWED[shoe]

    # Main story clears/ranks/physical medals, including the boss stages.
    if stem.endswith(" - Boss"):
        stem = stem.removesuffix(" - Boss")
    story = list(STORY)
    if stem in STORY:
        return story.index(stem) < story.index(SHOE_DEADLINE[shoe])
    if stem in _OPTIONAL_BEFORE_AIR_BOOST:
        return shoe in ("Air Boost Shoes", "Light Speed Dash Shoes")

    # Early hub medals: no Spagonia Entrance before Stomp and Wall Jump.
    for region in ("Apotos", "Mazuri", "Spagonia", "Holoska", "Chun-nan"):
        if not stem.startswith(region + " - "):
            continue
        if region == "Spagonia" and stem.startswith("Spagonia - Entrance"):
            return shoe not in ("Stomping Shoes", "Wall Jump Shoes")
        if region == "Chun-nan":
            return shoe in ("Air Boost Shoes", "Light Speed Dash Shoes")
        return True
    return False


def _dlc_access(state, player, stem, root):
    # This models a CONSERVATIVE earliest sphere for DLC checks, never a
    # presumed exact Gaia Gate level. All DLC checks keep their root Act
    # progression requirements; later pools cannot hide early Medal items.
    if not state.has(event_item(root), player):
        return False
    if stem in FIRST_SHOES_DLC:
        return True
    if stem in AIR_BOOST_DLC:
        return state.has("Stomping Shoes", player) and state.has("Wall Jump Shoes", player)
    if stem in LIGHT_SPEED_DLC:
        return (state.has("Air Boost Shoes", player)
                and state.has("Sun Medal", player, 8))
    # Unknown DLCs are reserved for the later story progression rather than
    # treated as free checks. Their real medal thresholds still need audit.
    return (state.has("Light Speed Dash Shoes", player)
            and state.has("Sun Medal", player, 10))

def event_item(stage):
    return "Cleared: " + stage


def _stage_req(state, player, stage):
    prereqs, shoes, sun, moon = STORY[stage]
    return (all(state.has(event_item(prev), player) for prev in prereqs)
            and all(state.has(shoe, player) for shoe in shoes)
            and state.has("Sun Medal", player, sun)
            and state.has("Moon Medal", player, moon))



# Vanilla secondary Acts: level requirements supplied by the project maintainer.
# Map *medal levels* to conservative AP item-pack counts (Sun +6, Moon +4).
# This models item access even when no physical medals have been collected.
SUN_PACKS_FOR_LEVEL = {3: 5, 4: 8, 5: 10}
MOON_PACKS_FOR_LEVEL = {3: 8, 4: 12}
OPTIONAL_ACT_LEVELS = {
    'Windmill Isle - Day Act 3': (3, 0),
    'Windmill Isle - Night Act 2': (0, 3),
    'Savannah Citadel - Night Act 2': (0, 3),
    'Savannah Citadel - Day Act 2': (3, 0),
    'Savannah Citadel - Day Act 3': (3, 0),
    'Rooftop Run - Day Act 2': (4, 0),
    'Rooftop Run - Day Act 3': (4, 0),
    'Cool Edge - Day Act 2': (4, 0),
    'Dragon Road - Day Act 2': (3, 0),
    'Dragon Road - Day Act 3': (5, 0),
    'Dragon Road - Night Act 2': (0, 4),
    'Arid Sands - Day Act 2': (4, 0),
    'Skyscraper Scamper - Day Act 2': (5, 0),
    'Jungle Joyride - Night Act 2': (5, 0),
    'Jungle Joyride - Day Act 2': (4, 0),
    'Jungle Joyride - Day Act 3': (5, 0),
}

# Preserve the original Egg Beetle gate for the early Sun Level 3 side Acts.
OPTIONAL_ADDITIONAL_STORY = {
    "Windmill Isle - Day Act 3": ("Egg Beetle",),
    "Savannah Citadel - Day Act 2": ("Egg Beetle",),
    "Savannah Citadel - Day Act 3": ("Egg Beetle",),
}

def _optional_act_access(state, player, stem):
    sun_level, moon_level = OPTIONAL_ACT_LEVELS[stem]
    match = ACT_RE.fullmatch(stem)
    if match is None:
        return False
    root = f"{match.group('stage')} - {match.group('time')} Act 1"
    country = STAGE_COUNTRIES.get(match.group('stage'))
    if root not in STORY or country not in COUNTRY_ACCESS:
        return False
    # Having enough medals must never bypass story-unlocking the continent.
    if not _region_access(state, player, country):
        return False
    if not state.has(event_item(root), player):
        return False
    if any(not state.has(event_item(req), player)
           for req in OPTIONAL_ADDITIONAL_STORY.get(stem, ())):
        return False
    if sun_level and not state.has("Sun Medal", player, SUN_PACKS_FOR_LEVEL[sun_level]):
        return False
    if moon_level and not state.has("Moon Medal", player, MOON_PACKS_FOR_LEVEL[moon_level]):
        return False
    return True


def _stage_access(state, player, stem):
    if stem in STORY:
        return _stage_req(state, player, stem)

    if stem in OPTIONAL_ACT_LEVELS:
        return _optional_act_access(state, player, stem)

    match = ACT_RE.match(stem)
    if match:
        stage, time, act = match.group("stage"), match.group("time"), int(match.group("act"))
        root = f"{stage} - {time} Act 1"
        if root in STORY:
            if "(DLC)" in stem:
                return _dlc_access(state, player, stem, root)
            # Optional non-DLC Gaia Gates must not be treated as free checks:
            # apply conservative base medal levels until precise per-Act
            # requirements are verified. Known special cases are above.
            country = STAGE_COUNTRIES.get(stage)
            if country not in COUNTRY_ACCESS or not _region_access(state, player, country):
                return False
            if not state.has(event_item(root), player):
                return False
            if act >= 2:
                if time == "Day":
                    return state.has("Sun Medal", player, 5)
                return state.has("Moon Medal", player, 8)
            return True
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
    # Entrance-stage medals are NOT city medals.  Spagonia's mission Entrance
    # cannot be reached without both Shoes, even if the city is accessible.
    if stem.startswith("Spagonia - Entrance"):
        return _spagonia_entrance_access(state, player)
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
    # CD #2 and Art Books #30/#31 are physically inside Spagonia Entrance.
    # Unlike Spagonia City items, they require both Shoes.
    if source.startswith("Spagonia - Entrance Stage"):
        return (_region_access(state, player, "Spagonia")
                and _spagonia_entrance_access(state, player))
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
                        if i in MEDAL_LOCATION_IDS
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
