# SPDX-License-Identifier: GPL-3.0-or-later
from unittest import mock

from test.bases import WorldTestBase

from .. import RAC1World


class RAC1TestBase(WorldTestBase):
    game = "Ratchet & Clank"
    # The rule tests collect items from the pool, which would hold no progression, and a sweep
    # would collect what pre_fill had placed.
    place_progression = False

    def world_setup(self, seed: int | None = None) -> None:
        if self.place_progression:
            super().world_setup(seed)
            return
        with mock.patch.object(RAC1World, "places_own_progression", lambda world: False):
            super().world_setup(seed)

    def sweep(self) -> None:
        # The story's events (an NPC's coordinates) as the player would have them.
        self.multiworld.state.sweep_for_advancements()
