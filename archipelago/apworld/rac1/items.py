# SPDX-License-Identifier: GPL-3.0-or-later
# The items: every gadget, weapon, planet's coordinates and gold bolt some location gives.
from BaseClasses import Item, ItemClassification

from .data import GAME, ITEMS
from .defs import ItemData

ITEM_TABLE: dict[str, ItemData] = {item.name: item for item in ITEMS}

ITEM_NAME_TO_ID: dict[str, int] = {item.name: item.id for item in ITEMS}

ITEM_NAME_GROUPS: dict[str, set[str]] = {}
for _item in ITEMS:
    ITEM_NAME_GROUPS.setdefault(_item.group, set()).add(_item.name)

FILLER = "Gold Bolt"


class RAC1Item(Item):
    game = GAME


def classification(item: ItemData) -> ItemClassification:
    if item.progression:
        return ItemClassification.progression
    if item.name == FILLER:
        return ItemClassification.filler
    return ItemClassification.useful
