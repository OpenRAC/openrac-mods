# SPDX-License-Identifier: GPL-3.0-or-later
# The player's options: the item randomizer's settings (item-randomizer/openrac-mod.json), as
# Archipelago options. A pool turned off keeps its game's own items where they are.
from dataclasses import dataclass

from Options import Choice, DefaultOnToggle, OptionGroup, PerGameCommonOptions, StartInventoryPool


class Logic(Choice):
    """What the logic expects of you.
    Standard: a place counts only with the gadgets the game's own route has there; no progression
    in an expensive purchase.
    Relaxed: as standard, and no progression in a gold bolt either.
    Expert: only what a place cannot be done without counts."""
    display_name = "Logic"
    option_relaxed = 0
    option_standard = 1
    option_expert = 2
    default = 1


class ShuffleGadgets(DefaultOnToggle):
    """The gadgets NPCs and pickups give: the Swingshot, the Heli-Pack, the Magneboots, ..."""
    display_name = "Shuffle gadgets"


class ShuffleWeapons(DefaultOnToggle):
    """The weapons NPCs and pickups give: the Suck Cannon, the Morph-o-Ray, the R.Y.N.O."""
    display_name = "Shuffle weapons"


class ShuffleVendor(DefaultOnToggle):
    """Each weapon the Gadgetron vendor sells is a location, at the weapon's price."""
    display_name = "Shuffle the vendor"


class ShuffleGoldBolts(DefaultOnToggle):
    """The gold bolts are locations, and gold bolts are items."""
    display_name = "Shuffle gold bolts"


class ShuffleInfobots(DefaultOnToggle):
    """The planets' coordinates the infobots give are items."""
    display_name = "Shuffle infobots"


class KeepStartingGear(DefaultOnToggle):
    """Start with the Bomb Glove, as the game does. Off: what Veldin gives at the start is
    shuffled too."""
    display_name = "Keep the starting gear"


@dataclass
class RAC1Options(PerGameCommonOptions):
    logic: Logic
    shuffle_gadgets: ShuffleGadgets
    shuffle_weapons: ShuffleWeapons
    shuffle_vendor: ShuffleVendor
    shuffle_gold_bolts: ShuffleGoldBolts
    shuffle_infobots: ShuffleInfobots
    keep_starting_gear: KeepStartingGear
    start_inventory_from_pool: StartInventoryPool


OPTION_GROUPS = [
    OptionGroup(
        "Pools",
        [ShuffleGadgets, ShuffleWeapons, ShuffleVendor, ShuffleGoldBolts, ShuffleInfobots, KeepStartingGear],
    ),
]

OPTION_PRESETS = {
    "Only weapons and gold bolts": {"shuffle_gadgets": False, "shuffle_infobots": False},
    "Expert": {"logic": "expert"},
}
