# SPDX-License-Identifier: GPL-3.0-or-later
# The locations: every gadget, weapon, vendor slot, gold bolt, infobot and skill point. The rows
# without an id (an NPC's coordinates, the Persuader, Drek) are events.
from BaseClasses import Location

from .data import GAME, LOCATIONS
from .defs import LocationData
from .gold_weapons import GOLD_LOCATION_ROWS
from .npc_infobots import NPC_LOCATION_ROWS, NPC_SOURCE_ROWS

LOCATION_ROWS: list[LocationData] = [row for row in LOCATIONS if row.id is not None] + GOLD_LOCATION_ROWS + NPC_LOCATION_ROWS
EVENT_ROWS: list[LocationData] = [row for row in LOCATIONS if row.id is None and row not in NPC_SOURCE_ROWS]

LOCATION_NAME_TO_ID: dict[str, int] = {row.name: row.id for row in LOCATION_ROWS}

# A group per planet, and per kind of place.
KIND_GROUPS = {"vendor": "Vendor", "infobot": "Infobots", "npc_infobot": "Infobots", "gold": "Gold Bolts", "skill": "Skill Points", "gold_vendor": "Gold Weapon Cases"}

LOCATION_NAME_GROUPS: dict[str, set[str]] = {}
for _row in LOCATION_ROWS:
    LOCATION_NAME_GROUPS.setdefault(_row.planet, set()).add(_row.name)
    if _row.kind in KIND_GROUPS:
        LOCATION_NAME_GROUPS.setdefault(KIND_GROUPS[_row.kind], set()).add(_row.name)

VICTORY = "Victory"


class RAC1Location(Location):
    game = GAME


def event_item(row: LocationData) -> str:
    # What a fixed row gives: "Victory" for Drek, else its item marked as the NPC's, e.g.
    # "Aridia Coordinates (NPC)", so it never shares a name with the shuffled item.
    if row.goal:
        return VICTORY
    return f"{row.vanilla} (NPC)"
