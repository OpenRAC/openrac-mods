# SPDX-License-Identifier: GPL-3.0-or-later
# The logic. A planet opens with its coordinates (the shuffled item, or the NPC's event); a
# location needs its row's hard items, and in standard and relaxed logic its soft items too.
# Standard and relaxed keep progression out of an expensive purchase, relaxed out of one to save up for.
# No logic puts progression on a skill point; a gold bolt holds it only with gold_bolt_progression
# (the marked ones only on expert).
from typing import TYPE_CHECKING

from rule_builder.options import OptionFilter
from rule_builder.rules import Has, HasAll, HasAny, Rule, True_
from worlds.generic.Rules import add_item_rule

from .data import PLANETS
from .defs import LocationData
from .items import ITEM_TABLE
from .locations import EVENT_ROWS, VICTORY, event_item
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


def is_progression(name: str) -> bool:
    return ITEM_TABLE[name].progression


def needs_filler(row: LocationData) -> bool:
    # A state holds only progression items, so a rule cannot ask for another (a gold bolt's
    # Taunter): such a place is ruled without it, and holds no progression.
    return any(not is_progression(name) for name in row.hard + row.soft)


def row_rule(row: LocationData) -> Rule:
    hard = [name for name in row.hard if is_progression(name)]
    rule: Rule = HasAll(*hard) if hard else True_()
    # One of a row's `any` that is not progression (the Bomb Glove) is left out: it only asks for
    # more.
    if row.any:
        rule = rule & HasAny(*[name for name in row.any if is_progression(name)])
    for planet in row.planets:
        rule = rule & HasAny(*coordinates(planet))
    extra = [name for name in row.soft if name not in row.hard and is_progression(name)]
    if extra:
        rule = rule & (HasAll(*extra) | EXPERT)
    if row.name == "Oltanis: the PDA's seller":
        rule = rule & (Has("Heli-Pack") | OptionFilter(Logic, Logic.option_relaxed, "ne"))
    if row.name == "Rilgar: the bouncer, the coordinates of Umbris":
        rule = rule & (Has("Hydrodisplacer") | OptionFilter(Logic, Logic.option_relaxed, "ne"))
    return rule


def holds_progression(row: LocationData, logic: Logic, gold_progression: bool = False) -> bool:
    costly = row.costly and logic != logic.option_expert
    pricey = row.pricey and logic == logic.option_relaxed
    # A skill point stays optional: no world's way on, nor the goal, waits behind a challenge.
    if row.kind == "skill":
        return False
    # A gold bolt holds progression only with gold_bolt_progression; one marked no_progression
    # (its hardest corners) only on expert.
    if row.kind == "gold" and not (
            gold_progression and (not row.no_progression or logic == logic.option_expert)):
        return False
    return not (costly or pricey)


def set_rules(world: "RAC1World") -> None:
    ship = world.get_region(SHIP)
    for index, planet in enumerate(PLANETS):
        region = world.get_region(planet)
        # Veldin, where the game starts, needs nothing.
        rule = True_() if index == 0 or planet == world.starting_planet else HasAny(*coordinates(planet))
        world.create_entrance(ship, region, rule, name=f"Fly to {planet}", force_creation=True)

    logic = world.options.logic
    for row in world.location_rows() + EVENT_ROWS:
        location = world.get_location(row.name)
        rule = row_rule(row)
        if row.kind == "gold_vendor":
            rule = rule & Has("Gold Bolt", 40)
            if not world.options.start_in_new_game_plus:
                rule = rule & Has(VICTORY)
        world.set_rule(location, rule)
        if row.kind == "gold_vendor":
            add_item_rule(location, lambda item: not (item.player == world.player and item.name == "Gold Bolt"))
        if needs_filler(row) or not holds_progression(row, logic, bool(world.options.gold_bolt_progression)):
            # An item rule, not EXCLUDED: EXCLUDED also keeps useful items out, and needs a filler
            # item for each such place, which a pool with the gold bolts off does not have.
            add_item_rule(location, lambda item, row=row: not item.advancement
                          or (world.options.shuffle_gold_weapons and row.kind in ("gold", "skill")
                              and item.player == world.player and item.name == "Gold Bolt"))

    world.set_completion_rule(Has(VICTORY))
