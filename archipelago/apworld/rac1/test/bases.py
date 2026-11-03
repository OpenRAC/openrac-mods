# SPDX-License-Identifier: GPL-3.0-or-later
from test.bases import WorldTestBase


class RAC1TestBase(WorldTestBase):
    game = "Ratchet & Clank"

    def sweep(self) -> None:
        # The story's events (an NPC's coordinates) as the player would have them.
        self.multiworld.state.sweep_for_advancements()
