# SPDX-License-Identifier: GPL-3.0-or-later
# Direct coordinate rewards become AP checks; the first flight remains a fixed event.
from .data import LOCATIONS, ITEMS, PLANETS
from .defs import ItemData
NPC_KIND = 0x12
NPC_SOURCE_ROWS = [row for row in LOCATIONS if row.id is None and row.vanilla
                   and row.vanilla.endswith(" Coordinates") and row.planet != "Veldin"]
NPC_LOCATION_ROWS = [row._replace(
    id=0x52440000 + (NPC_KIND << 16) + (PLANETS.index(row.planet) << 8)
       + PLANETS.index(row.vanilla.removesuffix(" Coordinates")),
    kind="npc_infobot", pool="shuffle_infobots") for row in NPC_SOURCE_ROWS]
existing = {item.name for item in ITEMS}
NPC_ITEMS = tuple(ItemData(0x52430100 + PLANETS.index(name.removesuffix(" Coordinates")),
                          name, "Coordinates", True)
                  for name in sorted({row.vanilla for row in NPC_SOURCE_ROWS} - existing))
