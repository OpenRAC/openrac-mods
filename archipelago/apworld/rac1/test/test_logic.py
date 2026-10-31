# SPDX-License-Identifier: GPL-3.0-or-later
# The world's logic: the planets open with their coordinates, Drek needs his gadgets, and a pool
# turned off keeps its own items.
from . import RAC1TestBase


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
