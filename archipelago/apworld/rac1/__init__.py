# SPDX-License-Identifier: GPL-3.0-or-later
# Ratchet & Clank (OpenRAC) for Archipelago.
#
# The locations, items and logic are the item randomizer's table (item-randomizer/src/logic.c),
# in data.py, which archipelago/tools/dump_table.c writes: the mod and the world use the same ids.
# Each planet is a region, opened by its coordinates (an item, from an infobot, or an event, from
# an NPC the game always has). The goal is beating Drek.
from typing import Any, Mapping

from BaseClasses import ItemClassification, Tutorial
from worlds.AutoWorld import WebWorld, World

from .data import GAME
from .defs import LocationData
from .items import FILLER, ITEM_NAME_GROUPS, ITEM_NAME_TO_ID, ITEM_TABLE, RAC1Item, classification
from .locations import EVENT_ROWS, LOCATION_NAME_GROUPS, LOCATION_NAME_TO_ID, LOCATION_ROWS, event_item
from .options import OPTION_GROUPS, OPTION_PRESETS, RAC1Options
from .regions import SHIP, create_regions
from .rules import set_rules


class RAC1Web(WebWorld):
    theme = "partyTime"
    rich_text_options_doc = True
    bug_report_page = "https://github.com/OpenRAC/openrac-mods/issues"
    option_groups = OPTION_GROUPS
    options_presets = OPTION_PRESETS
    tutorials = [
        Tutorial(
            "Multiworld Setup Guide",
            "How to play Ratchet & Clank in a multiworld with OpenRAC.",
            "English",
            "setup_en.md",
            "setup/en",
            ["OpenRAC contributors"],
        )
    ]


class RAC1World(World):
    """Ratchet & Clank (2002), played natively on PC through OpenRAC. A lombax and a robot cross
    the Solana galaxy to stop Chairman Drek: every gadget, weapon, vendor slot, gold bolt and
    infobot may hold another world's item."""

    game = GAME
    web = RAC1Web()
    options_dataclass = RAC1Options
    options: RAC1Options
    origin_region_name = SHIP

    item_name_to_id = ITEM_NAME_TO_ID
    location_name_to_id = LOCATION_NAME_TO_ID
    item_name_groups = ITEM_NAME_GROUPS
    location_name_groups = LOCATION_NAME_GROUPS

    def shuffled(self, row: LocationData) -> bool:
        # A row of a pool turned off, or the starting Bomb Glove kept, holds its own item.
        if row.kind == "start" and self.options.keep_starting_gear:
            return False
        return bool(getattr(self.options, row.pool).value)

    def create_item(self, name: str) -> RAC1Item:
        item = ITEM_TABLE[name]
        return RAC1Item(name, classification(item), item.id, self.player)

    def get_filler_item_name(self) -> str:
        return FILLER

    def create_regions(self) -> None:
        create_regions(self)
        for row in EVENT_ROWS:
            location = self.get_location(row.name)
            location.place_locked_item(RAC1Item(event_item(row), ItemClassification.progression, None, self.player))

    def create_items(self) -> None:
        # A pool turned off keeps the game's own item in place; the rest go to the multiworld.
        for row in LOCATION_ROWS:
            item = self.create_item(row.vanilla)
            if self.shuffled(row):
                self.multiworld.itempool.append(item)
            else:
                self.get_location(row.name).place_locked_item(item)

    def set_rules(self) -> None:
        set_rules(self)

    def fill_slot_data(self) -> Mapping[str, Any]:
        return self.options.as_dict(
            "logic",
            "shuffle_gadgets",
            "shuffle_weapons",
            "shuffle_vendor",
            "shuffle_gold_bolts",
            "shuffle_infobots",
            "keep_starting_gear",
        )
