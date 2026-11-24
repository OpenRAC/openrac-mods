# SPDX-License-Identifier: GPL-3.0-or-later
from unittest import mock
from ..data import PLANETS
from ..starting_planet import route_open, starting_candidates
from ..locations import EVENT_ROWS
from .bases import RAC1TestBase

class TestRandomStart(RAC1TestBase):
    options = {"random_starting_planet": True}
    place_progression = True

    def test_seed_sweep(self):
        for seed in range(12):
            with self.subTest(seed=seed):
                self.world_setup(seed=seed)
                self.test_start_route_and_coordinates()
                self.test_fill()

    def test_start_route_and_coordinates(self):
        world = self.world
        self.assertNotEqual(world.starting_planet, "Veldin")
        self.assertEqual(world.fill_slot_data()["starting_planet"], PLANETS.index(world.starting_planet))
        self.assertTrue(self.can_reach_region(world.starting_planet))
        items = set()
        if world.starting_vendor:
            location = world.get_location(world.starting_vendor)
            self.assertTrue(location.locked)
            self.assertEqual(location.item.player, self.player)
            self.assertEqual(location.item.name, world.starting_vendor_item)
            items.add(location.item.name)
        rows = world.location_rows() + EVENT_ROWS
        self.assertTrue(any(row.planet == world.starting_planet
                            and route_open(row, world.options.logic, items) for row in rows))
        flight = world.get_location("Veldin: the way to Novalis")
        self.assertEqual(flight.item.name, world.starting_planet + " Coordinates")
        if world.starting_planet != "Novalis":
            items = [location.item for location in self.multiworld.get_locations(self.player) if location.item]
            items += self.multiworld.itempool
            self.assertEqual(sum(item.name == "Novalis Coordinates" and item.player == self.player
                                 for item in items), 1)

class TestRandomStartExpert(TestRandomStart):
    options = {"random_starting_planet": True, "logic": "expert"}

class TestRandomStartFixed(TestRandomStart):
    options = {"random_starting_planet": True, "shuffle_infobots": False}

class TestVendorStart(TestRandomStart):
    options = {"random_starting_planet": True, "logic": "standard"}

    def world_setup(self, seed=None):
        def vendor_only(world):
            return [candidate for candidate in starting_candidates(world) if candidate[0] == "Gaspar"]
        with mock.patch("worlds.rac1.starting_candidates", vendor_only):
            super().world_setup(seed)

    def test_reserved_key(self):
        self.assertEqual(self.world.starting_planet, "Gaspar")
        self.assertEqual(self.world.starting_vendor_item, "Heli-Pack")
        self.assertIsNotNone(self.world.starting_vendor)

class TestRandomStartRelaxed(TestRandomStart):
    options = {"random_starting_planet": True, "logic": "relaxed"}

class TestRandomStartVanilla(TestRandomStart):
    options = {"random_starting_planet": True, "shuffle_gadgets": False,
               "shuffle_weapons": False, "shuffle_vendor": False,
               "shuffle_gold_bolts": False, "shuffle_infobots": False}

class TestRandomStartItems(TestRandomStart):
    options = {"random_starting_planet": True, "shuffle_items": True}
