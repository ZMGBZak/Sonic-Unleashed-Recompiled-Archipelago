from BaseClasses import Region, Location, Entrance
from .Locations import location_table, location_to_region

def create_regions(world):
    multiworld = world.multiworld
    player = world.player
    menu = Region("Menu", player, multiworld)
    multiworld.regions.append(menu)
    regions = {}
    include_dlc = bool(world.options.include_dlc.value)
    include_collectibles = bool(world.options.include_collectibles.value)
    enabled_names = [
        name for name in location_table
        if (include_dlc or "(DLC)" not in name)
        and (include_collectibles or not name.startswith(("CD #", "Art Book #")))
    ]
    for region_name in sorted({location_to_region[name] for name in enabled_names}):
        region = Region(region_name, player, multiworld)
        multiworld.regions.append(region)
        regions[region_name] = region
        entrance = Entrance(player, "Go to " + region_name, menu)
        menu.exits.append(entrance)
        entrance.connect(region)
    for name in enabled_names:
        location_id = location_table[name]
        region = regions[location_to_region[name]]
        region.locations.append(Location(player, name, location_id, region))
