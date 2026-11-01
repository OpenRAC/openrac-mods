# SPDX-License-Identifier: GPL-3.0-or-later
# The shapes of data.py's rows.
from typing import NamedTuple


class ItemData(NamedTuple):
    id: int
    name: str
    group: str  # "Gadgets", "Weapons", "Coordinates" or "Gold Bolts"
    progression: bool  # some location's rule needs it


class LocationData(NamedTuple):
    name: str
    id: int | None  # None: an event (an NPC's coordinates, the Persuader, the goal)
    kind: str  # "give", "vendor", "start", "infobot", "gold" or "fixed"
    planet: str
    pool: str | None  # the option that shuffles it; None for an event
    vanilla: str | None  # the item the game gives here; None for the goal
    hard: tuple[str, ...]  # what the place cannot be done without
    soft: tuple[str, ...]  # what the game's own route has there
    costly: bool  # bought for more bolts than a player has early on
    goal: bool  # beating Drek
