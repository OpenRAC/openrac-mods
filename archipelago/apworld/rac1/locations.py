# SPDX-License-Identifier: GPL-3.0-or-later
# The locations: every gadget, weapon, vendor slot, gold bolt and infobot of the table. The rows
# without an id (an NPC's coordinates, the Persuader, Drek) are events.
from BaseClasses import Location

from .data import GAME, LOCATIONS
from .defs import LocationData

LOCATION_ROWS: list[LocationData] = [row for row in LOCATIONS if row.id is not None]
EVENT_ROWS: list[LocationData] = [row for row in LOCATIONS if row.id is None]

LOCATION_NAME_TO_ID: dict[str, int] = {row.name: row.id for row in LOCATION_ROWS}

# A group per planet, and per kind of place.
KIND_GROUPS = {"vendor": "Vendor", "infobot": "Infobots", "gold": "Gold Bolts"}

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
