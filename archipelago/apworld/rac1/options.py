# SPDX-License-Identifier: GPL-3.0-or-later
# The player's options: the item randomizer's settings (item-randomizer/openrac-mod.json), as
# Archipelago options. A pool turned off keeps its game's own items where they are.
from dataclasses import dataclass

from Options import Choice, DefaultOnToggle, OptionGroup, PerGameCommonOptions, StartInventoryPool, Toggle


class Logic(Choice):
    """What the logic expects of you.
    Standard: a place counts only with the gadgets the game's own route has there; no progression
    in an expensive purchase.
    Relaxed: as standard, and no progression in a purchase to save up for either.
    Expert: progression may be in any purchase, and the weapons a place suggests do not count."""
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


class ShuffleSkillPoints(Toggle):
    """The 30 skill points are locations too, and as many gold bolts join the items. The game's own
    challenges, and optional: a skill point never holds progression, of any world."""
    display_name = "Shuffle skill points"


class StartingWeapons(Choice):
    """Choose your starting weapon.
    Vanilla: Bomb Glove (unless keep_starting_gear is off).
    Non-Broken: one random weapon besides the R.Y.N.O.
    Unrestricted: any one weapon.
    Random modes override keep_starting_gear and swap the chosen weapon with the Bomb Glove."""
    display_name = "Starting weapons"
    option_vanilla = 0
    option_non_broken = 1
    option_unrestricted = 2
    default = 0


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
    shuffle_skill_points: ShuffleSkillPoints
    keep_starting_gear: KeepStartingGear
    starting_weapons: StartingWeapons
    start_inventory_from_pool: StartInventoryPool


OPTION_GROUPS = [
    OptionGroup(
        "Pools",
        [ShuffleGadgets, ShuffleWeapons, ShuffleVendor, ShuffleGoldBolts, ShuffleInfobots, ShuffleSkillPoints,
         KeepStartingGear, StartingWeapons],
    ),
]

OPTION_PRESETS = {
    "Only weapons and gold bolts": {"shuffle_gadgets": False, "shuffle_infobots": False},
    "Expert": {"logic": "expert"},
}
