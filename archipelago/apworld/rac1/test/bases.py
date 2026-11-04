# SPDX-License-Identifier: GPL-3.0-or-later
from unittest import mock

from test.bases import WorldTestBase

from .. import RAC1World


class RAC1TestBase(WorldTestBase):
    game = "Ratchet & Clank"
    # The rule tests collect items from the pool, where pre_fill would leave no progression, and
    # a sweep would collect what it had placed.
    place_progression = False

    def world_setup(self, seed: int | None = None) -> None:
        if self.place_progression:
            super().world_setup(seed)
            return
        with mock.patch.object(RAC1World, "pre_fill", lambda world: None):
            super().world_setup(seed)

    def sweep(self) -> None:
        # The story's events (an NPC's coordinates) as the player would have them.
        self.multiworld.state.sweep_for_advancements()
