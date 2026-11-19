# SPDX-License-Identifier: GPL-3.0-or-later
from ..items import ITEM_NAME_GROUPS
from ..locations import LOCATION_ROWS
from .bases import RAC1TestBase

MENU_ITEMS = {"Hoverboard", "Persuader", "Bolt Grabber", "Map-o-Matic",
              "Platinum Zoomerator", "Raritanium", "CodeBot", "Premium Nanotech", "Ultra Nanotech"}

class TestItemsMenuOn(RAC1TestBase):
    options = {"shuffle_items": True}

    def test_complete_pool(self) -> None:
        self.assertEqual(ITEM_NAME_GROUPS["Items"], MENU_ITEMS)
        rows = [row for row in LOCATION_ROWS if row.pool == "shuffle_items"]
        self.assertEqual({row.vanilla for row in rows}, MENU_ITEMS)
        self.assertEqual(len(rows), 9)
        for row in rows:
            self.assertFalse(self.world.get_location(row.name).locked)
        self.assertEqual(self.world.fill_slot_data()["shuffle_items"], 1)

class TestItemsMenuOff(RAC1TestBase):
    options = {"shuffle_items": False, "shuffle_gadgets": True}

    def test_all_menu_items_stay_vanilla(self) -> None:
        for row in LOCATION_ROWS:
            if row.pool == "shuffle_items":
                spot = self.world.get_location(row.name)
                self.assertTrue(spot.locked)
                self.assertEqual(spot.item.name, row.vanilla)
        self.assertEqual(self.world.fill_slot_data()["shuffle_items"], 0)

class TestItemsOnly(TestItemsMenuOn):
    options = {"shuffle_items": True, "shuffle_gadgets": False,
               "shuffle_weapons": False, "shuffle_vendor": False,
               "shuffle_infobots": False, "shuffle_gold_bolts": False}
