# SPDX-License-Identifier: GPL-3.0-or-later
# The regions: the ship, and a planet per region reached from it. A planet's locations sit in it.
from typing import TYPE_CHECKING

from BaseClasses import Region

from .data import PLANETS
from .locations import EVENT_ROWS, LOCATION_ROWS, RAC1Location

if TYPE_CHECKING:
    from . import RAC1World

SHIP = "Ship"


def create_regions(world: "RAC1World") -> None:
    ship = Region(SHIP, world.player, world.multiworld)
    world.multiworld.regions.append(ship)

    for name in PLANETS:
        world.multiworld.regions.append(Region(name, world.player, world.multiworld))

    for row in LOCATION_ROWS:
        region = world.get_region(row.planet)
        region.locations.append(RAC1Location(world.player, row.name, row.id, region))

    for row in EVENT_ROWS:
        region = world.get_region(row.planet)
        region.locations.append(RAC1Location(world.player, row.name, None, region))
