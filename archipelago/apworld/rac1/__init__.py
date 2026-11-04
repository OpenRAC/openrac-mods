# SPDX-License-Identifier: GPL-3.0-or-later
# Ratchet & Clank (OpenRAC) for Archipelago.
#
# The locations, items and logic are the item randomizer's table (item-randomizer/src/logic.c),
# in data.py, which archipelago/tools/dump_table.c writes: the mod and the world use the same ids.
# Each planet is a region, opened by its coordinates (an item, from an infobot, or an event, from
# an NPC the game always has). The goal is beating Drek.
from typing import Any, Mapping

from BaseClasses import CollectionState, ItemClassification, Tutorial
from Fill import fill_restrictive
from worlds.AutoWorld import WebWorld, World

from .data import GAME
from .defs import LocationData
from .items import FILLER, ITEM_NAME_GROUPS, ITEM_NAME_TO_ID, ITEM_TABLE, RAC1Item, classification
from .locations import EVENT_ROWS, LOCATION_NAME_GROUPS, LOCATION_NAME_TO_ID, LOCATION_ROWS, event_item
from .options import OPTION_GROUPS, OPTION_PRESETS, RAC1Options
from .regions import SHIP, create_regions
from .rules import set_rules


# In a multiworld, a world with fewer spare places for its progression than this places it itself.
# Every option set has at most 7 or at least 10: two players of 7 failed 1 seed in 1000, of 10 none.
ROOM_FOR_KEYS = 8
KEY_FILL_ATTEMPTS = 100


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

    def location_rows(self) -> list[LocationData]:
        # The rows that are locations in this slot: the skill points only with their option.
        if self.options.shuffle_skill_points:
            return LOCATION_ROWS
        return [row for row in LOCATION_ROWS if row.kind != "skill"]

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
        for row in self.location_rows():
            item = self.create_item(row.vanilla or FILLER)
            if self.shuffled(row):
                self.multiworld.itempool.append(item)
            else:
                self.get_location(row.name).place_locked_item(item)

    def set_rules(self) -> None:
        set_rules(self)

    def pre_fill(self) -> None:
        # The main fill never retries, and it can dead-end when the progression has few places to
        # go. Alone, this world's locations are all it has anyway, so nothing is lost placing it
        # here, where it can try again; in a multiworld, only a world short of places does this.
        pool = self.multiworld.itempool
        keys = [item for item in pool if item.player == self.player and item.advancement]
        if not keys:
            return
        spots = [spot for spot in self.multiworld.get_unfilled_locations(self.player) if spot.item_rule(keys[0])]
        if self.multiworld.players > 1 and len(spots) - len(keys) >= ROOM_FOR_KEYS:
            return
        for _ in range(KEY_FILL_ATTEMPTS):
            left = self.random.sample(keys, len(keys))
            fill_restrictive(
                self.multiworld,
                CollectionState(self.multiworld),
                self.random.sample(spots, len(spots)),
                left,
                single_player_placement=True,
                lock=True,
                allow_partial=True,
                name=f"{self.game} progression",
            )
            if not left:
                pool[:] = [item for item in pool if item.location is None]
                return
            for spot in spots:
                if spot.item is not None:
                    spot.item.location = None
                    spot.item = None
                    spot.locked = False

    def fill_slot_data(self) -> Mapping[str, Any]:
        return self.options.as_dict(
            "logic",
            "shuffle_gadgets",
            "shuffle_weapons",
            "shuffle_vendor",
            "shuffle_gold_bolts",
            "shuffle_infobots",
            "shuffle_skill_points",
            "keep_starting_gear",
        )
