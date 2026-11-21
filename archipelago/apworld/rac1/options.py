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


class ShuffleItems(Toggle):
    """Shuffle every entry in the game's Items menu: Hoverboard, Persuader, Bolt Grabber,
    Map-o-Matic, Platinum Zoomerator, Raritanium, CodeBot, Premium Nanotech and Ultra Nanotech.
    When off, all nine stay at their original locations. Requires OpenRAC interface 1.16+."""
    display_name = "Shuffle items"


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
    """Shuffle Infobots and NPC coordinate rewards, including the Plumber and Batalia purchases.
    The first flight from Veldin to Novalis remains fixed."""
    display_name = "Shuffle infobots"


class ShuffleSkillPoints(Toggle):
    """The 30 skill points are locations too, and as many gold bolts join the items. The game's own
    challenges. With gold-weapon cases shuffled, their gold bolts can finance those purchases;
    otherwise skill points never hold progression."""
    display_name = "Shuffle skill points"


class ShuffleGoldWeapons(Toggle):
    """Shuffle the ten gold-weapon case purchases into the multiworld. Each case still costs
    its retail bolts and four gold bolts. Gold weapons grant their base weapon and upgrade."""
    display_name = "Shuffle gold weapons"


class StartInNewGamePlus(Toggle):
    """Enable the game's New Game+ (challenge mode) after connecting to this slot.
    Start with a fresh Archipelago inventory; this does not import a completed save."""
    display_name = "Start in New Game+"


class StartingWeapons(Choice):
    """Choose your starting weapon.
    Vanilla: start with Bomb Glove.
    Shuffled: shuffle the starting location, with no guaranteed weapon.
    Non-Broken: one random normal or gold weapon besides the R.Y.N.O.
    Unrestricted: any one normal or gold weapon.
    Non-Broken and Unrestricted include gold weapons even when gold cases are not shuffled."""
    display_name = "Starting weapons"
    option_vanilla = 0
    option_shuffled = 3
    option_non_broken = 1
    option_unrestricted = 2
    default = 0


class RandomStartingPlanet(Toggle):
    """Skip Veldin and fly to a seeded planet with an item-free route. A ship-side
    vendor may instead hold a guaranteed local item that opens a route.
    The starting planet is fixed when the world is generated, including on reconnect."""
    display_name = "Random starting planet"


@dataclass
class RAC1Options(PerGameCommonOptions):
    logic: Logic
    shuffle_gadgets: ShuffleGadgets
    shuffle_items: ShuffleItems
    shuffle_weapons: ShuffleWeapons
    shuffle_vendor: ShuffleVendor
    shuffle_gold_bolts: ShuffleGoldBolts
    shuffle_infobots: ShuffleInfobots
    shuffle_skill_points: ShuffleSkillPoints
    shuffle_gold_weapons: ShuffleGoldWeapons
    random_starting_planet: RandomStartingPlanet
    starting_weapons: StartingWeapons
    start_in_new_game_plus: StartInNewGamePlus
    start_inventory_from_pool: StartInventoryPool


OPTION_GROUPS = [
    OptionGroup("Starting game", [StartingWeapons, StartInNewGamePlus, RandomStartingPlanet]),
    OptionGroup(
        "Pools",
        [ShuffleGadgets, ShuffleItems, ShuffleWeapons, ShuffleVendor, ShuffleGoldBolts, ShuffleInfobots, ShuffleSkillPoints, ShuffleGoldWeapons],
    ),
]

OPTION_PRESETS = {
    "Only weapons and gold bolts": {"shuffle_gadgets": False, "shuffle_infobots": False},
    "Expert": {"logic": "expert"},
}
