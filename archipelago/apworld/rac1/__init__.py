# SPDX-License-Identifier: GPL-3.0-or-later
# Ratchet & Clank (OpenRAC) for Archipelago.
#
# The locations, items and logic are the item randomizer's table (item-randomizer/src/logic.c),
# read from table.json, which the mod's build prints (archipelago/tools/dump_table.c) and packs
# into rac1.apworld: the mod and the world use the same ids. Each planet is a region, opened by its
# coordinates (an item, from an infobot, or an event, from an NPC the game always has); a location
# needs the gadgets its row asks for (hard, and soft but in expert logic). The goal is beating Drek.
import json
import pkgutil
from typing import Any, Mapping

from BaseClasses import Item, ItemClassification, Location, Region, Tutorial
from worlds.AutoWorld import WebWorld, World

from .options import RAC1Options

GAME = "Ratchet & Clank"

# logic.c's pools (rando.h's POOL_*) and the option that shuffles each.
POOL_OPTIONS = {
    0x01: "shuffle_weapons",
    0x02: "shuffle_gadgets",
    0x04: "shuffle_gold_bolts",
    0x08: "shuffle_infobots",
    0x10: "shuffle_vendor",
}

LOGIC_RELAXED, LOGIC_STANDARD, LOGIC_EXPERT = 0, 1, 2

# An event's item: what a fixed row gives (NPC coordinates, the Persuader), and the goal.
EVENT_SUFFIX = " (story)"
VICTORY = "Victory"

TABLE: dict = json.loads(pkgutil.get_data(__name__, "table.json").decode("utf-8"))
ITEMS = {item["name"]: item for item in TABLE["items"]}
LOCATIONS = [row for row in TABLE["locations"] if row["id"] is not None]
EVENTS = [row for row in TABLE["locations"] if row["id"] is None]
PLANETS: list = TABLE["planets"]


class RAC1Item(Item):
    game = GAME


class RAC1Location(Location):
    game = GAME


class RAC1Web(WebWorld):
    theme = "partyTime"
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


def coordinates(planet: int) -> str:
    return f"{PLANETS[planet]} Coordinates"


class RAC1World(World):
    """Ratchet & Clank (2002), played natively on PC through OpenRAC. A lombax and a robot cross
    the Solana galaxy to stop Chairman Drek: every gadget, weapon, vendor slot, gold bolt and
    infobot may hold another world's item."""

    game = GAME
    web = RAC1Web()
    options_dataclass = RAC1Options
    options: RAC1Options
    origin_region_name = "Ship"

    item_name_to_id = {name: item["id"] for name, item in ITEMS.items()}
    location_name_to_id = {row["name"]: row["id"] for row in LOCATIONS}

    def shuffled(self, row: dict) -> bool:
        # A row of a pool turned off, or the starting Bomb Glove kept, holds its own item.
        if row["kind"] == "start" and self.options.keep_starting_gear:
            return False
        return bool(getattr(self.options, POOL_OPTIONS[row["pool"]]).value)

    def requirement(self, row: dict) -> list:
        # Expert counts only what a place cannot be done without.
        if self.options.logic == LOGIC_EXPERT:
            return row["hard"]
        return sorted(set(row["hard"]) | set(row["soft"]))

    def create_item(self, name: str) -> RAC1Item:
        item = ITEMS[name]
        if item["progression"]:
            classification = ItemClassification.progression
        elif item["kind"] == "gold_bolt":
            classification = ItemClassification.filler
        else:
            classification = ItemClassification.useful
        return RAC1Item(name, classification, item["id"], self.player)

    def create_event(self, name: str) -> RAC1Item:
        return RAC1Item(name, ItemClassification.progression, None, self.player)

    def get_filler_item_name(self) -> str:
        return "Gold Bolt"

    def create_regions(self) -> None:
        ship = Region("Ship", self.player, self.multiworld)
        self.multiworld.regions.append(ship)
        planets = []

        # A region per planet, reached from the ship with its coordinates (Veldin from the start).
        for planet, name in enumerate(PLANETS):
            region = Region(name, self.player, self.multiworld)
            self.multiworld.regions.append(region)
            planets.append(region)
            if planet == 0:
                ship.connect(region)
            else:
                ship.connect(region, f"Fly to {name}", self.planet_rule(planet))

        # The locations, each in its planet; the fixed rows are events.
        for row in LOCATIONS:
            region = planets[row["level"]]
            location = RAC1Location(self.player, row["name"], row["id"], region)
            location.access_rule = self.items_rule(self.requirement(row))
            region.locations.append(location)

            # The logic's places for progression: not an expensive purchase (standard, relaxed),
            # nor a gold bolt (relaxed).
            costly = row["costly"] and self.options.logic != LOGIC_EXPERT
            gold = row["kind"] == "gold" and self.options.logic == LOGIC_RELAXED
            if costly or gold:
                location.item_rule = lambda item: not item.advancement

        for row in EVENTS:
            region = planets[row["level"]]
            location = RAC1Location(self.player, row["name"], None, region)
            location.access_rule = self.items_rule(self.requirement(row))
            region.locations.append(location)
            name = VICTORY if row["goal"] else (row["vanilla"] or "") + EVENT_SUFFIX
            location.place_locked_item(self.create_event(name))

        self.multiworld.completion_condition[self.player] = lambda state: state.has(VICTORY, self.player)

    def planet_rule(self, planet: int):
        names = [coordinates(planet), coordinates(planet) + EVENT_SUFFIX]
        return lambda state: state.has_any(names, self.player)

    def items_rule(self, required: list):
        player = self.player
        pairs = [(name, name + EVENT_SUFFIX) for name in required]
        return lambda state: all(state.has(a, player) or state.has(b, player) for a, b in pairs)

    def create_items(self) -> None:
        # A pool turned off keeps the game's own item in place; the rest go to the multiworld.
        for row in LOCATIONS:
            item = self.create_item(row["vanilla"])
            if self.shuffled(row):
                self.multiworld.itempool.append(item)
            else:
                self.get_location(row["name"]).place_locked_item(item)

    def fill_slot_data(self) -> Mapping[str, Any]:
        return {
            "world_version": "1.0.0",
            **self.options.as_dict(
                "logic",
                "shuffle_gadgets",
                "shuffle_weapons",
                "shuffle_vendor",
                "shuffle_gold_bolts",
                "shuffle_infobots",
                "keep_starting_gear",
            ),
        }
