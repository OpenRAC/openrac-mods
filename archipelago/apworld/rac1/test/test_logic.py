# SPDX-License-Identifier: GPL-3.0-or-later
# The world's logic: the planets open with their coordinates, Drek needs his gadgets, and a pool
# turned off keeps its own items.
import unittest

from ..data import LOCATIONS, PLANETS
from ..items import ITEM_TABLE
from ..locations import LOCATION_NAME_GROUPS
from ..options import OPTION_PRESETS, RAC1Options
from ..rules import coordinates
from .bases import RAC1TestBase


class TestDefault(RAC1TestBase):
    def test_story_planets_from_the_start(self) -> None:
        # Veldin opens Novalis, whose NPCs open Aridia and Kerwan.
        self.sweep()
        for planet in ("Novalis", "Aridia", "Kerwan"):
            self.assertTrue(self.can_reach_region(planet), planet)
        self.assertFalse(self.can_reach_region("Eudora"))

    def test_planet_needs_its_coordinates(self) -> None:
        self.collect_by_name("Eudora Coordinates")
        self.sweep()
        self.assertTrue(self.can_reach_region("Eudora"))

    def test_drek_needs_swingshot_and_magneboots(self) -> None:
        location = "Veldin (return): Drek"
        self.collect_by_name("Veldin (return) Coordinates")
        self.collect_by_name(["Heli-Pack", "Grindboots", "O2 Mask", "Swingshot"])
        self.sweep()
        self.assertFalse(self.can_reach_location(location))
        self.collect_by_name("Magneboots")
        self.assertTrue(self.can_reach_location(location))


class TestExpert(RAC1TestBase):
    options = {"logic": "expert"}

    def test_drek_needs_only_his_hard_items(self) -> None:
        self.collect_by_name(["Veldin (return) Coordinates", "Swingshot", "Magneboots"])
        self.sweep()
        self.assertTrue(self.can_reach_location("Veldin (return): Drek"))


class TestPoolsOff(RAC1TestBase):
    options = {
        "shuffle_gadgets": False,
        "shuffle_weapons": False,
        "shuffle_vendor": False,
        "shuffle_gold_bolts": False,
        "shuffle_infobots": False,
    }

    def test_vanilla_items_stay(self) -> None:
        location = self.multiworld.get_location("Veldin: the starting Bomb Glove", self.player)
        self.assertEqual(location.item.name, "Bomb Glove")


class TestHardNeeds(RAC1TestBase):
    # Even expert logic: the O2 Mask's jet fight needs the Pilot's Helmet, the Hologuise's
    # challenge the Hoverboard.
    options = {"logic": "expert"}

    def test_resort_owner_needs_pilots_helmet(self) -> None:
        self.collect_all_but(["Pilot's Helmet"])
        self.assertFalse(self.can_reach_location("Pokitaru: the resort owner"))
        self.collect_by_name("Pilot's Helmet")
        self.assertTrue(self.can_reach_location("Pokitaru: the resort owner"))

    def test_gadgetron_ceo_needs_hoverboard(self) -> None:
        self.collect_all_but(["Hoverboard"])
        self.assertFalse(self.can_reach_location("Kalebo III: the Gadgetron CEO"))
        self.collect_by_name("Hoverboard")
        self.assertTrue(self.can_reach_location("Kalebo III: the Gadgetron CEO"))


class TestRelaxed(RAC1TestBase):
    options = {"logic": "relaxed"}

    def test_no_progression_in_gold_bolts(self) -> None:
        swingshot = self.world.create_item("Swingshot")
        for name in LOCATION_NAME_GROUPS["Gold Bolts"]:
            location = self.multiworld.get_location(name, self.player)
            self.assertFalse(location.item_rule(swingshot), name)
            self.assertTrue(location.item_rule(self.world.create_item("Gold Bolt")), name)


class TestStandardGoldBoltsOff(RAC1TestBase):
    # No filler in the pool: the expensive purchases still fill.
    options = {"shuffle_gold_bolts": False}


class TestStartingGearShuffled(RAC1TestBase):
    options = {"keep_starting_gear": False}

    def test_bomb_glove_in_the_pool(self) -> None:
        self.assertIn("Bomb Glove", [item.name for item in self.multiworld.itempool])


class TestPresets(unittest.TestCase):
    def test_presets_name_options(self) -> None:
        for preset, values in OPTION_PRESETS.items():
            for option in values:
                self.assertIn(option, RAC1Options.type_hints, preset)


class TestData(unittest.TestCase):
    def test_rules_name_known_items(self) -> None:
        # Every item a rule asks for is in the pool, so a rule can never be impossible.
        for row in LOCATIONS:
            for name in row.hard + row.soft:
                self.assertIn(name, ITEM_TABLE, row.name)

    def test_every_planet_opens(self) -> None:
        for planet in PLANETS[1:]:
            self.assertTrue(coordinates(planet), planet)
