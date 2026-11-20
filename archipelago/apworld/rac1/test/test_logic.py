# SPDX-License-Identifier: GPL-3.0-or-later
# The world's logic: the planets open with their coordinates, Drek needs his gadgets, and a pool
# turned off keeps its own items.
import unittest
from collections import Counter
from random import Random
from types import SimpleNamespace
from unittest.mock import Mock

from BaseClasses import CollectionState

from ..data import LOCATIONS, PLANETS
from ..items import ITEM_TABLE
from ..options import OPTION_PRESETS, RAC1Options
from ..rules import coordinates
from .bases import RAC1TestBase
from .. import RAC1World
from ..items import ITEM_NAME_GROUPS
from ..options import StartingWeapons
from ..gold_weapons import GOLD_WEAPONS, GOLD_ITEMS


class TestStartingWeapons(unittest.TestCase):
    def test_gold_starts_with_and_without_case_shuffle(self) -> None:
        from ..gold_weapons import GOLD_LOCATION_ROWS
        base_rows = [row for row in LOCATIONS if row.id is not None]
        for enabled in (False, True):
            rows = base_rows + (GOLD_LOCATION_ROWS if enabled else [])
            expected = Counter(row.vanilla or "Gold Bolt" for row in rows)
            for gold, base in GOLD_WEAPONS.items():
                world = SimpleNamespace(starting_weapon=gold,
                                        options=SimpleNamespace(shuffle_gold_weapons=enabled))
                want = expected.copy()
                if not enabled:
                    want[base] -= 1
                    want[gold] += 1
                self.assertEqual(Counter(RAC1World.vanilla_item(world, row) for row in rows), +want)

    def test_single_starting_option(self) -> None:
        self.assertIn("starting_weapons", RAC1Options.type_hints)
        self.assertNotIn("keep_starting_gear", RAC1Options.type_hints)

    def test_random_candidates(self) -> None:
        for mode in ("vanilla", "shuffled", "non_broken", "unrestricted"):
            world = SimpleNamespace(
                options=SimpleNamespace(starting_weapons=StartingWeapons.from_any(mode)),
                random=Mock(wraps=Random(123)),
            )
            RAC1World.generate_early(world)
            if mode in ("vanilla", "shuffled"):
                self.assertEqual(world.starting_weapon, "Bomb Glove")
                world.random.choice.assert_not_called()
            else:
                candidates = world.random.choice.call_args.args[0]
                expected = ITEM_NAME_GROUPS["Weapons"] - ({"R.Y.N.O."} if mode == "non_broken" else set())
                self.assertEqual(set(candidates), expected)

    def test_every_weapon_swap_preserves_items(self) -> None:
        rows = [row for row in LOCATIONS if row.id is not None]
        expected = Counter(row.vanilla or "Gold Bolt" for row in rows)
        for weapon in ITEM_NAME_GROUPS["Weapons"] - set(GOLD_WEAPONS):
            world = SimpleNamespace(starting_weapon=weapon)
            self.assertEqual(Counter(RAC1World.vanilla_item(world, row) for row in rows), expected)


class TestRandomStartingWeapon(RAC1TestBase):
    options = {"starting_weapons": "non_broken"}

    def test_guaranteed_start(self) -> None:
        location = self.world.get_location("Veldin: the starting Bomb Glove")
        self.assertTrue(location.locked)
        self.assertEqual(location.item.name, self.world.starting_weapon)
        self.assertNotEqual(location.item.name, "R.Y.N.O.")
        self.assertTrue(location.can_reach(self.multiworld.state))


class TestUnrestrictedStartingWeapon(TestRandomStartingWeapon):
    options = {"starting_weapons": "unrestricted", "shuffle_weapons": False, "shuffle_vendor": False}

    def test_guaranteed_start(self) -> None:
        location = self.world.get_location("Veldin: the starting Bomb Glove")
        self.assertTrue(location.locked)
        self.assertIn(location.item.name, ITEM_NAME_GROUPS["Weapons"])
        self.assertTrue(location.can_reach(self.multiworld.state))


class TestDefault(RAC1TestBase):
    def test_new_game_plus_default(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["start_in_new_game_plus"], 0)

    def test_vanilla_start(self) -> None:
        location = self.world.get_location("Veldin: the starting Bomb Glove")
        self.assertTrue(location.locked)
        self.assertEqual(location.item.name, "Bomb Glove")

    def test_story_planets_from_the_start(self) -> None:
        # Only the first flight is fixed; the two Novalis NPC rewards are now checks.
        self.sweep()
        self.assertTrue(self.can_reach_region("Novalis"))
        for planet in ("Aridia", "Kerwan", "Eudora"):
            self.assertFalse(self.can_reach_region(planet), planet)
        self.collect_by_name(["Aridia Coordinates", "Kerwan Coordinates"])
        self.assertTrue(self.can_reach_region("Aridia"))
        self.assertTrue(self.can_reach_region("Kerwan"))

    def test_planet_needs_its_coordinates(self) -> None:
        self.collect_by_name("Eudora Coordinates")
        self.sweep()
        self.assertTrue(self.can_reach_region("Eudora"))

    def test_drek_needs_the_magneboots(self) -> None:
        location = "Veldin (return): Drek"
        self.collect_by_name("Veldin (return) Coordinates")
        self.collect_by_name(["Heli-Pack", "O2 Mask", "Swingshot", "Trespasser", "Hydrodisplacer", "Thruster-Pack"])
        self.sweep()
        self.assertFalse(self.can_reach_location(location))
        self.collect_by_name("Magneboots")
        self.assertTrue(self.can_reach_location(location))


class TestNewGamePlus(RAC1TestBase):
    options = {"start_in_new_game_plus": True, "starting_weapons": "non_broken"}

    def test_mode_and_starting_weapon(self) -> None:
        self.assertEqual(self.world.fill_slot_data()["start_in_new_game_plus"], 1)
        location = self.world.get_location("Veldin: the starting Bomb Glove")
        self.assertEqual(location.item.name, self.world.starting_weapon)
        self.assertTrue(location.locked)


class TestGoldWeapons(RAC1TestBase):
    options = {"shuffle_gold_weapons": True, "start_in_new_game_plus": True}

    def test_ten_cases_and_weapons(self) -> None:
        cases = [row for row in self.world.location_rows() if row.kind == "gold_vendor"]
        self.assertEqual(len(cases), 10)
        self.assertEqual({row.vanilla for row in cases}, set(GOLD_WEAPONS))
        self.assertEqual(sum(item.name in GOLD_WEAPONS for item in self.multiworld.itempool), 10)
        self.assertEqual(self.world.fill_slot_data()["shuffle_gold_weapons"], 1)

    def test_currency_and_optional_case_rules(self) -> None:
        case = next(row for row in self.world.location_rows() if row.kind == "gold_vendor")
        spot = self.world.get_location(case.name)
        self.assertTrue(self.world.create_item("Gold Bolt").advancement)
        self.assertFalse(spot.item_rule(self.world.create_item("Gold Bolt")))
        self.assertFalse(spot.item_rule(self.world.create_item("Gold Devastator")))
        state = CollectionState(self.multiworld)
        state.add_item("Novalis Coordinates (NPC)", self.player)
        self.assertFalse(spot.can_reach(state))
        state.add_item("Gold Bolt", self.player, 39)
        self.assertFalse(spot.can_reach(state))
        state.add_item("Gold Bolt", self.player)
        self.assertTrue(spot.can_reach(state))

    def test_gold_weapons_satisfy_base_requirements(self) -> None:
        state = CollectionState(self.multiworld)
        item = self.world.create_item("Gold Devastator")
        state.collect(item, True)
        self.assertTrue(state.has("Devastator", self.player))
        state.remove(item)
        self.assertFalse(state.has("Devastator", self.player))


class TestGoldWeaponsCanBeFilled(TestGoldWeapons):
    place_progression = True

    def test_ten_cases_and_weapons(self) -> None:
        self.assertEqual(sum(row.kind == "gold_vendor" for row in self.world.location_rows()), 10)
        self.assertTrue(self.multiworld.can_beat_game(CollectionState(self.multiworld)))


class TestGoldWeaponsWithOtherPoolsOff(RAC1TestBase):
    place_progression = True
    options = {"shuffle_gold_weapons": True, "shuffle_gadgets": False, "shuffle_weapons": False,
               "shuffle_vendor": False, "shuffle_infobots": False, "shuffle_gold_bolts": False}

    def test_optional_case_fill(self) -> None:
        self.assertTrue(self.multiworld.can_beat_game(CollectionState(self.multiworld)))
        self.assertFalse(self.world.create_item("Gold Devastator").advancement)


class TestExpert(RAC1TestBase):
    options = {"logic": "expert"}

    def test_drek_needs_only_his_hard_items(self) -> None:
        self.collect_by_name(["Veldin (return) Coordinates", "Swingshot", "Magneboots"])
        self.collect_by_name(["Heli-Pack", "Trespasser", "Hydrodisplacer", "Thruster-Pack"])
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
    options = {"logic": "expert", "shuffle_items": True}

    def test_resort_owner_needs_pilots_helmet(self) -> None:
        self.collect_all_but(["Pilot's Helmet"])
        self.assertFalse(self.can_reach_location("Pokitaru: the resort owner"))
        self.collect_by_name("Pilot's Helmet")
        self.assertTrue(self.can_reach_location("Pokitaru: the resort owner"))

    def test_gadgetron_ceo_needs_hoverboard(self) -> None:
        self.collect_all_but(["Hoverboard"])
        self.assertFalse(self.can_reach_location("Kalebo III: the Gadgetron CEO's race"))
        self.collect_by_name("Hoverboard")
        self.assertTrue(self.can_reach_location("Kalebo III: the Gadgetron CEO's race"))


class TestOrxon(RAC1TestBase):
    def test_o2_mask_is_for_ratchets_part_only(self) -> None:
        # The mod plays Clank's part first, however the mask stands.
        self.collect_all_but(["O2 Mask"])
        self.assertTrue(self.can_reach_location("Orxon: Clank finds the Magneboots"))
        self.assertTrue(self.can_reach_location("Orxon: Clank's infobot"))
        self.assertFalse(self.can_reach_location("Orxon: Ratchet's infobot"))
        self.collect_by_name("O2 Mask")
        self.assertTrue(self.can_reach_location("Orxon: Ratchet's infobot"))


class TestRelaxed(RAC1TestBase):
    options = {"logic": "relaxed"}

    def test_no_progression_in_a_purchase_to_save_up_for(self) -> None:
        swingshot = self.world.create_item("Swingshot")
        rows = [row for row in LOCATIONS if row.id is not None and row.pricey]
        self.assertTrue(rows)
        for row in rows:
            location = self.multiworld.get_location(row.name, self.player)
            self.assertFalse(location.item_rule(swingshot), row.name)
            self.assertTrue(location.item_rule(self.world.create_item("Gold Bolt")), row.name)


class TestGoldBolts(RAC1TestBase):
    def test_gold_bolts_hold_no_progression(self) -> None:
        key = self.world.create_item("Swingshot")
        for row in LOCATIONS:
            if row.kind == "gold":
                self.assertFalse(self.multiworld.get_location(row.name, self.player).item_rule(key), row.name)


class TestFewPlacesForProgression(RAC1TestBase):
    options = {"shuffle_weapons": False, "shuffle_vendor": False, "shuffle_gold_bolts": False}
    place_progression = True

    def test_pre_fill_places_the_progression_and_the_game_can_be_beaten(self) -> None:
        self.assertFalse([item for item in self.multiworld.itempool if item.advancement])
        self.assertTrue(self.multiworld.can_beat_game(CollectionState(self.multiworld)))


class TestMultiworldKeepsProgressionShared(RAC1TestBase):
    def test_default_options_leave_it_to_the_main_fill(self) -> None:
        self.multiworld.players = 2
        self.assertFalse(self.world.places_own_progression())


class TestMultiworldShortOfPlaces(RAC1TestBase):
    options = {"shuffle_vendor": False}
    # Short of places, the main fill can dead-end (seed 49127562165134269560): the base tests run
    # the world as it plays, placing its own progression.
    place_progression = True

    def test_it_places_its_own(self) -> None:
        self.multiworld.players = 2
        self.assertTrue(self.world.places_own_progression())


class TestStandardGoldBoltsOff(RAC1TestBase):
    # No filler in the pool: the expensive purchases still fill.
    options = {"shuffle_gold_bolts": False}


class TestStartingGearShuffled(RAC1TestBase):
    options = {"starting_weapons": "shuffled"}

    def test_bomb_glove_in_the_pool(self) -> None:
        self.assertIn("Bomb Glove", [item.name for item in self.multiworld.itempool])
        location = self.world.get_location("Veldin: the starting Bomb Glove")
        self.assertFalse(location.locked)
        self.assertIsNone(location.item)


class TestShuffledStartWithWeaponPoolOff(TestStartingGearShuffled):
    options = {"starting_weapons": "shuffled", "shuffle_weapons": False}


class TestSkillPointsOff(RAC1TestBase):
    def test_no_skill_points_by_default(self) -> None:
        names = [location.name for location in self.multiworld.get_locations(self.player)]
        self.assertFalse([name for name in names if "skill point" in name])


class TestSkillPointsOn(RAC1TestBase):
    options = {"shuffle_skill_points": True}

    def test_thirty_skill_points_and_as_many_items(self) -> None:
        rows = [row for row in LOCATIONS if row.kind == "skill"]
        self.assertEqual(len(rows), 30)
        names = [location.name for location in self.multiworld.get_locations(self.player)]
        for row in rows:
            self.assertIn(row.name, names)
        self.assertEqual(sum(item.name == "Gold Bolt" for item in self.multiworld.itempool), 40 + 30)

    def test_skill_points_hold_no_progression(self) -> None:
        coordinates_item = self.world.create_item("Veldin (return) Coordinates")
        for row in LOCATIONS:
            if row.kind == "skill":
                location = self.multiworld.get_location(row.name, self.player)
                self.assertFalse(location.item_rule(coordinates_item), row.name)
                self.assertTrue(location.item_rule(self.world.create_item("Gold Bolt")), row.name)

    def test_morph_o_ray_needed_only_for_its_skill_point(self) -> None:
        self.collect_all_but(["Morph-o-Ray"])
        self.assertFalse(self.can_reach_location("Rilgar: skill point 2"))
        self.assertTrue(self.can_reach_location("Rilgar: skill point 1"))
        self.collect_by_name("Morph-o-Ray")
        self.assertTrue(self.can_reach_location("Rilgar: skill point 2"))


class TestPresets(unittest.TestCase):
    def test_presets_name_options(self) -> None:
        for preset, values in OPTION_PRESETS.items():
            for option in values:
                self.assertIn(option, RAC1Options.type_hints, preset)


class TestData(unittest.TestCase):
    def test_rules_name_known_items(self) -> None:
        # Every item a rule asks for is in the pool, so a rule can never be impossible.
        for row in LOCATIONS:
            for name in row.hard + row.soft + row.any:
                self.assertIn(name, ITEM_TABLE, row.name)
                self.assertTrue(ITEM_TABLE[name].progression, f"{row.name}: {name}")

    def test_every_planet_opens(self) -> None:
        for planet in PLANETS[1:]:
            self.assertTrue(coordinates(planet), planet)


class TestNPCInfobots(RAC1TestBase):
    def test_npc_checks_and_coordinate_items(self) -> None:
        from ..npc_infobots import NPC_LOCATION_ROWS
        self.assertEqual(len(NPC_LOCATION_ROWS), 13)
        self.assertEqual(len({row.id for row in NPC_LOCATION_ROWS}), 13)
        self.assertEqual(self.world.fill_slot_data()["npc_infobot_checks"], 1)
        for name in ("Novalis: the coordinates of Aridia",
                     "Batalia: the deserter, the coordinates of Gaspar",
                     "Batalia: the commando, the coordinates of Orxon"):
            location = self.world.get_location(name)
            self.assertIsNotNone(location.address)
            self.assertFalse(location.locked)
        self.sweep()
        self.assertFalse(self.can_reach_region("Gaspar"))
        self.collect_by_name("Gaspar Coordinates")
        self.assertTrue(self.can_reach_region("Gaspar"))


class TestNPCInfobotsOff(RAC1TestBase):
    options = {"shuffle_infobots": False}

    def test_off_keeps_npc_rewards_vanilla(self) -> None:
        from ..npc_infobots import NPC_LOCATION_ROWS
        for row in NPC_LOCATION_ROWS:
            spot = self.world.get_location(row.name)
            self.assertTrue(spot.locked)
            self.assertEqual(spot.item.name, row.vanilla)
        self.sweep()
        self.assertTrue(self.can_reach_region("Aridia"))
        self.assertTrue(self.can_reach_region("Kerwan"))


class TestUniqueRewards(unittest.TestCase):
    def test_one_source_per_coordinate(self) -> None:
        from ..locations import LOCATION_ROWS, EVENT_ROWS
        counts = Counter(row.vanilla for row in [*LOCATION_ROWS, *EVENT_ROWS]
                         if row.vanilla and row.vanilla.endswith(" Coordinates"))
        self.assertTrue(counts)
        self.assertEqual({name: count for name, count in counts.items() if count != 1}, {})


class TestEdwinaHydrodisplacer(RAC1TestBase):
    def test_kalebo_race_needs_no_magneboots(self) -> None:
        self.collect_by_name(["Kalebo III Coordinates", "Heli-Pack", "Swingshot", "Grindboots"])
        self.assertFalse(self.can_reach_location("Kalebo III: the Gadgetron CEO's race"))
        self.collect(self.world.create_item("Hoverboard"))
        self.assertTrue(self.can_reach_location("Kalebo III: the Gadgetron CEO's race"))

    def test_kalebo_help_desk_needs_no_heli_pack(self) -> None:
        self.collect_by_name(["Kalebo III Coordinates", "Swingshot", "Grindboots"])
        self.assertTrue(self.can_reach_location("Kalebo III: the help desk"))

    def test_fleet_infobot_heli_pack_is_required_outside_expert(self) -> None:
        self.collect_by_name(["Drek's Fleet Coordinates", "Magneboots", "O2 Mask", "Swingshot"])
        self.assertEqual(self.can_reach_location("Drek's Fleet: the infobot"),
                         self.world.options.logic == "expert")
        self.collect_by_name("Heli-Pack")
        self.assertEqual(self.can_reach_location("Drek's Fleet: the infobot"),
                         self.world.options.logic == "expert")
        self.collect_by_name("Hydro-Pack")  # the tanker's hint: soft
        self.assertTrue(self.can_reach_location("Drek's Fleet: the infobot"))

    def test_fleet_codebot_needs_only_water_equipment(self) -> None:
        self.collect_by_name(["Drek's Fleet Coordinates", "Hydro-Pack"])
        self.assertFalse(self.can_reach_location("Drek's Fleet: the CodeBot"))
        self.collect_by_name("O2 Mask")
        self.assertTrue(self.can_reach_location("Drek's Fleet: the CodeBot"))

    def test_drek_o2_mask_is_required_outside_expert(self) -> None:
        self.collect_by_name(["Veldin (return) Coordinates", "Magneboots", "Heli-Pack", "Swingshot",
                              "Trespasser", "Hydrodisplacer", "Thruster-Pack"])
        self.assertEqual(self.can_reach_location("Veldin (return): Drek"),
                         self.world.options.logic == "expert")
        self.collect_by_name("O2 Mask")
        self.assertTrue(self.can_reach_location("Veldin (return): Drek"))

    def test_oltanis_pda_requires_only_magneboots(self) -> None:
        self.collect_by_name("Oltanis Coordinates")
        self.assertFalse(self.can_reach_location("Oltanis: the PDA's seller"))
        self.collect_by_name("Magneboots")
        self.assertTrue(self.can_reach_location("Oltanis: the PDA's seller"))

    def test_oltanis_morph_o_ray_requires_only_swingshot(self) -> None:
        self.collect_by_name("Oltanis Coordinates")
        self.assertFalse(self.can_reach_location("Oltanis: the Morph-o-Ray's pickup"))
        self.collect_by_name("Swingshot")
        self.assertTrue(self.can_reach_location("Oltanis: the Morph-o-Ray's pickup"))

    def test_oltanis_sam_requires_swingshot_and_grindboots(self) -> None:
        self.collect_by_name(["Oltanis Coordinates", "Swingshot"])
        self.assertFalse(self.can_reach_location("Oltanis: Sam, the coordinates of Quartu"))
        self.collect_by_name("Grindboots")
        self.assertTrue(self.can_reach_location("Oltanis: Sam, the coordinates of Quartu"))

    def test_quartu_bolt_grabber_requires_hydro_pack_and_o2_mask(self) -> None:
        self.collect_by_name("Quartu Coordinates")
        self.assertFalse(self.can_reach_location("Quartu: the Bolt Grabber's pickup"))
        self.collect_by_name("Hydro-Pack")
        self.assertFalse(self.can_reach_location("Quartu: the Bolt Grabber's pickup"))
        self.collect_by_name("O2 Mask")
        self.assertTrue(self.can_reach_location("Quartu: the Bolt Grabber's pickup"))

    def test_quartu_scientist_requires_only_swingshot(self) -> None:
        self.collect_by_name("Quartu Coordinates")
        self.assertFalse(self.can_reach_location("Quartu: the scientist, the coordinates of Kalebo III"))
        self.collect_by_name("Swingshot")
        self.assertTrue(self.can_reach_location("Quartu: the scientist, the coordinates of Kalebo III"))

    def test_quartu_disguise_route_requires_thruster_and_hinted_hydro_pack(self) -> None:
        location = "Quartu: in disguise, the coordinates of Drek's Fleet"
        self.collect_by_name(["Quartu Coordinates", "Hologuise", "Heli-Pack", "Swingshot"])
        self.assertFalse(self.can_reach_location(location))
        self.collect_by_name("Thruster-Pack")
        self.assertEqual(self.can_reach_location(location), self.world.options.logic == "expert")
        self.collect_by_name("Hydro-Pack")  # the Tri-Rings' hint: soft
        self.assertTrue(self.can_reach_location(location))

    def test_hoven_gemlik_coordinates_need_no_swingshot_or_grindboots(self) -> None:
        self.collect_by_name(["Hoven Coordinates", "Heli-Pack"])
        self.assertEqual(self.can_reach_location("Hoven: the coordinates of Gemlik"),
                         self.world.options.logic == "expert")
        self.collect_by_name(["Devastator", "Hydro-Pack"])  # both hinted on Hoven: soft
        self.assertTrue(self.can_reach_location("Hoven: the coordinates of Gemlik"))

    def test_hoven_raritanium_needs_hydro_pack_only_outside_expert(self) -> None:
        self.collect_by_name(["Hoven Coordinates", "Heli-Pack", "Swingshot"])
        self.assertEqual(self.can_reach_location("Hoven: the Foreman's Raritanium"),
                         self.world.options.logic == "expert")
        self.collect_by_name("Hydro-Pack")  # Hoven's hint: soft
        self.assertTrue(self.can_reach_location("Hoven: the Foreman's Raritanium"))

    def test_gemlik_trespasser_is_required_outside_expert(self) -> None:
        self.collect_by_name(["Gemlik Coordinates", "Heli-Pack", "Swingshot",
                              "Magneboots", "Pilot's Helmet", "Devastator"])
        self.assertEqual(self.can_reach_location("Gemlik: the coordinates of Oltanis"),
                         self.world.options.logic == "expert")
        self.collect_by_name("Trespasser")
        self.assertTrue(self.can_reach_location("Gemlik: the coordinates of Oltanis"))

    def test_pokitaru_bob_needs_heli_pack_without_swingshot(self) -> None:
        self.collect_by_name("Pokitaru Coordinates")
        self.assertFalse(self.can_reach_location("Pokitaru: Bob"))
        self.collect_by_name("Heli-Pack")
        self.assertTrue(self.can_reach_location("Pokitaru: Bob"))

    def test_pokitaru_owner_needs_no_swingshot(self) -> None:
        self.collect_by_name(["Pokitaru Coordinates", "Heli-Pack", "Thruster-Pack"])
        self.assertFalse(self.can_reach_location("Pokitaru: the resort owner"))
        self.collect_by_name("Pilot's Helmet")
        self.assertTrue(self.can_reach_location("Pokitaru: the resort owner"))

    def test_pokitaru_persuader_needs_trespasser_without_swingshot(self) -> None:
        self.collect_by_name(["Pokitaru Coordinates", "Heli-Pack"])
        self.collect(self.world.create_item("Raritanium"))
        self.assertFalse(self.can_reach_location("Pokitaru: the Persuader"))
        self.collect_by_name("Trespasser")
        self.assertTrue(self.can_reach_location("Pokitaru: the Persuader"))

    def test_gaspar_helmet_requires_heli_pack_without_swingshot(self) -> None:
        self.collect_by_name("Gaspar Coordinates")
        self.assertFalse(self.can_reach_location("Gaspar: the Pilot's Helmet's pickup"))
        self.collect_by_name("Heli-Pack")
        self.assertTrue(self.can_reach_location("Gaspar: the Pilot's Helmet's pickup"))

    def test_batalia_turret_requires_only_magneboots(self) -> None:
        self.collect_by_name("Batalia Coordinates")
        self.assertFalse(self.can_reach_location("Batalia: the turret's commando"))
        self.collect_by_name("Magneboots")
        self.assertEqual(self.can_reach_location("Batalia: the turret's commando"), self.world.options.logic == "expert")
        self.collect_by_name("Grindboots")  # Batalia's hint: soft
        self.assertTrue(self.can_reach_location("Batalia: the turret's commando"))

    def test_batalia_orxon_coordinates_need_only_hinted_grindboots(self) -> None:
        location = "Batalia: the commando, the coordinates of Orxon"
        self.collect_by_name("Batalia Coordinates")
        self.assertEqual(self.can_reach_location(location), self.world.options.logic == "expert")
        self.collect_by_name("Grindboots")  # Batalia's hint: soft
        self.assertTrue(self.can_reach_location(location))

    def test_batalia_deserter_requires_only_grindboots(self) -> None:
        self.collect_by_name("Batalia Coordinates")
        self.assertFalse(self.can_reach_location("Batalia: the deserter, the coordinates of Gaspar"))
        self.collect_by_name("Grindboots")
        self.assertTrue(self.can_reach_location("Batalia: the deserter, the coordinates of Gaspar"))

    def test_blarg_shuttle_needs_no_movement_gadgets(self) -> None:
        # The NPC and the infobot it spawns are one check: the shuttle's.
        self.collect_by_name("Blarg Coordinates")
        self.assertTrue(self.can_reach_location("Blarg: the shuttle, the coordinates of Rilgar"))

    def test_blarg_hydrodisplacer_requires_only_trespasser(self) -> None:
        self.collect_by_name("Blarg Coordinates")
        self.assertFalse(self.can_reach_location("Blarg: the Hydrodisplacer's pickup"))
        self.collect_by_name("Trespasser")
        self.assertTrue(self.can_reach_location("Blarg: the Hydrodisplacer's pickup"))

    def test_rilgar_salesman_requires_only_heli_pack(self) -> None:
        self.collect_by_name("Rilgar Coordinates")
        self.assertFalse(self.can_reach_location("Rilgar: the shady salesman"))
        self.collect_by_name("Heli-Pack")
        self.assertTrue(self.can_reach_location("Rilgar: the shady salesman"))

    def test_rilgar_bouncer_does_not_require_hoverboard(self) -> None:
        self.collect_by_name(["Rilgar Coordinates", "Heli-Pack", "Swingshot"])
        self.assertTrue(self.can_reach_location("Rilgar: the bouncer, the coordinates of Umbris"))

    def test_rilgar_race_does_not_require_swingshot(self) -> None:
        self.collect_by_name(["Rilgar Coordinates", "Heli-Pack"])
        self.assertFalse(self.can_reach_location("Rilgar: the Hoverboard race"))
        # Items-menu shuffle defaults off, so Hoverboard may be a locked reward.
        self.collect(self.world.create_item("Hoverboard"))
        self.assertTrue(self.can_reach_location("Rilgar: the Hoverboard race"))

    def test_eudora_warp_pad_requires_only_heli_pack(self) -> None:
        self.collect_by_name("Eudora Coordinates")
        self.assertFalse(self.can_reach_location("Eudora: the warp pad"))
        self.collect_by_name("Heli-Pack")
        self.assertEqual(self.can_reach_location("Eudora: the warp pad"), self.world.options.logic == "expert")
        self.collect_by_name("Trespasser")  # the security door's hint: soft
        self.assertTrue(self.can_reach_location("Eudora: the warp pad"))

    def test_helga_does_not_require_heli_pack(self) -> None:
        self.collect_by_name("Kerwan Coordinates")
        self.assertTrue(self.can_reach_location("Kerwan: Helga"))

    def test_aridia_gold_bolt_3_requires_magneboots(self) -> None:
        self.collect_all_but(["Magneboots"])
        self.assertFalse(self.can_reach_location("Aridia: gold bolt 3"))
        self.collect_by_name("Magneboots")
        self.assertTrue(self.can_reach_location("Aridia: gold bolt 3"))

    def test_novalis_gold_bolt_2_requires_hydro_pack(self) -> None:
        self.collect_all_but(["Hydro-Pack"])
        self.assertFalse(self.can_reach_location("Novalis: gold bolt 2"))
        self.collect_by_name("Hydro-Pack")
        self.assertTrue(self.can_reach_location("Novalis: gold bolt 2"))

    def test_hoven_gold_bolt_2_requires_hydrodisplacer(self) -> None:
        self.collect_all_but(["Hydrodisplacer"])
        self.assertFalse(self.can_reach_location("Hoven: gold bolt 2"))
        self.collect_by_name("Hydrodisplacer")
        self.assertTrue(self.can_reach_location("Hoven: gold bolt 2"))

    def test_edwina_requires_hydrodisplacer(self) -> None:
        self.collect_by_name(["Hoven Coordinates", "Heli-Pack"])
        self.assertFalse(self.can_reach_location("Hoven: Edwina"))
        self.collect_by_name("Hydrodisplacer")
        self.assertTrue(self.can_reach_location("Hoven: Edwina"))


class TestEdwinaHydrodisplacerRelaxed(TestEdwinaHydrodisplacer):
    options = {"logic": "relaxed"}


class TestEdwinaHydrodisplacerExpert(TestEdwinaHydrodisplacer):
    options = {"logic": "expert"}
