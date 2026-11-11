# SPDX-License-Identifier: GPL-3.0-or-later
# The ten retail gold upgrades; ids preserve the base weapon's item id.
from .data import ITEMS
from .defs import ItemData, LocationData

GOLD_BASE_IDS = (0x0A, 0x10, 0x0F, 0x14, 0x11, 0x0B, 0x19, 0x13, 0x09, 0x15)
BASE_ITEMS = {item.id & 0xFF: item for item in ITEMS if item.group == "Weapons"}
GOLD_WEAPONS = {"Gold " + BASE_ITEMS[i].name: BASE_ITEMS[i].name for i in GOLD_BASE_IDS}
GOLD_ITEMS = tuple(ItemData(0x52430300 + i, "Gold " + BASE_ITEMS[i].name, "Weapons",
                            BASE_ITEMS[i].progression) for i in GOLD_BASE_IDS)
GOLD_LOCATION_ROWS = [LocationData(
    "Gold weapon case: " + BASE_ITEMS[i].name, 0x52550000 + i, "gold_vendor", "Novalis",
    "shuffle_gold_weapons", "Gold " + BASE_ITEMS[i].name, (), (), (), (), True, True, False)
    for i in GOLD_BASE_IDS]
