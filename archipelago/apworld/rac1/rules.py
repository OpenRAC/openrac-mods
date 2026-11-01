# SPDX-License-Identifier: GPL-3.0-or-later
# The logic. A planet opens with its coordinates (the shuffled item, or the NPC's event); a
# location needs its row's hard items, and in standard and relaxed logic its soft items too.
# Standard and relaxed keep progression out of an expensive purchase, relaxed out of gold bolts.
from typing import TYPE_CHECKING

from rule_builder.options import OptionFilter
from rule_builder.rules import Has, HasAll, HasAny, Rule, True_
from worlds.generic.Rules import add_item_rule

from .data import PLANETS
from .defs import LocationData
from .items import ITEM_TABLE
from .locations import EVENT_ROWS, LOCATION_ROWS, VICTORY, event_item
from .options import Logic
from .regions import SHIP

if TYPE_CHECKING:
    from . import RAC1World

# Expert logic counts only what a place cannot be done without: `rule | EXPERT` is the rule, or
# true in expert logic.
EXPERT = OptionFilter(Logic, Logic.option_expert)


def coordinates(planet: str) -> list[str]:
    # The items that open a planet: its shuffled coordinates and the NPC's, whichever exist.
    names = [f"{planet} Coordinates", f"{planet} Coordinates (NPC)"]
    events = {event_item(row) for row in EVENT_ROWS}
    return [name for name in names if name in ITEM_TABLE or name in events]


def row_rule(row: LocationData) -> Rule:
    rule: Rule = HasAll(*row.hard) if row.hard else True_()
    extra = [name for name in row.soft if name not in row.hard]
    if extra:
        rule = rule & (HasAll(*extra) | EXPERT)
    return rule


def set_rules(world: "RAC1World") -> None:
    ship = world.get_region(SHIP)
    for index, planet in enumerate(PLANETS):
        region = world.get_region(planet)
        # Veldin, where the game starts, needs nothing.
        rule = True_() if index == 0 else HasAny(*coordinates(planet))
        world.create_entrance(ship, region, rule, name=f"Fly to {planet}", force_creation=True)

    logic = world.options.logic
    for row in LOCATION_ROWS + EVENT_ROWS:
        location = world.get_location(row.name)
        world.set_rule(location, row_rule(row))
        costly = row.costly and logic != logic.option_expert
        gold = row.kind == "gold" and logic == logic.option_relaxed
        if costly or gold:
            # An item rule, not EXCLUDED: EXCLUDED also keeps useful items out, and needs a filler
            # item for each such place, which a pool with the gold bolts off does not have.
            add_item_rule(location, lambda item: not item.advancement)

    world.set_completion_rule(Has(VICTORY))
