# Open Museum

A new map for Ratchet & Clank on OpenRAC: a wooden museum house on a Hawaiian island, with a
lagoon, mountains, two waterfalls, a volcano and small islets. Like the Insomniac Museum of the
later games, but about how OpenRAC is made. The rooms are empty frames, ready for exhibits.

It is not a mod with a manifest: it is a set of Python scripts that turn your own Pokitaru level
project into the museum. No game data is in this folder; the plants, rocks, rock islets, turbine
towers, lamps, gadget targets and the ground and wood pictures come from your disc when you
install it (Pokitaru's own, so the island looks like the game's planets).

## Install

1. In the OpenRAC launcher, extract the levels of your Europe disc (Ratchet & Clank PAL) into a
   projects folder. Level 11 (Pokitaru) and level 2 (Aridia, for its palm) are used.
2. Run the installer with OpenRAC Editor's checkout and OpenRAC's (for the level tools):

   ```
   OPENRAC_ROOT=path/to/OpenRAC python3 tools/install.py PROJECTS --editor path/to/OpenRAC-Editor
   ```

   The editor needs its terrain tools (`openrac/tools/terrain_tools.py`): the island's rock is built
   with them from `tools/rocks.py` (stacks and mesas with overhangs, caves, baked sun and shade).

   It backs up Pokitaru's scene once (`level_11/.openrac-backups/open-museum/`) and starts from that
   backup every time, so running it again gives the same museum.
3. Pack level 11 (OpenRAC Editor's Pack, or `openrac_editor.py pack DISC PROJECTS/level_11 OUT`)
   and play it, or add it as a new planet in the launcher (its name is set: Open Museum).

Needs Python 3 with numpy and Pillow.

## What is in it

- **The island** in Pokitaru's layered style: rock walls with grass hanging over every ledge,
  rocky knolls, sea stacks round the coast, Pokitaru's rock and grass islets out at sea, and its
  turbine towers on the far islands.
- **The house** in the middle of the lagoon: an octagonal pavilion on stilts, two rings of 8 rooms
  round a tall atrium (a fountain under the turning logo, high windows, a thatched spire), a
  gallery round the upper storey reached by a double stair on the west, a colonnade, dormers, and
  a gateway at the south bridge with a carved sign: raised gold letters, a cog with a glowing bolt
  (`tools/gate.py`). Each room has its sign, framed paintings,
  exhibit boards, a rug, plinths with exhibits, flower pots and a bench; the windows have frames,
  shutters and flower boxes. Ground floor: Launcher, Renderer, Hostgen, The Port, Matching,
  Decompilation, Welcome, Mods and SDK. Upper floor: Multiplayer, Speedrun Corner, Enhanced,
  Android, Cutscenes, OpenRAC Editor, Devlog, Thank You (`tools/house.py`, `GROUND`, `UPPER`).
- **Little scenes everywhere** (`tools/vignettes.py`): huts, canoes, campfires, totems, hammocks,
  sunshades, signposts, fences, flower beds, tide pools, driftwood, stone lanterns, one per patch
  of land, and Pokitaru's bushes and trees on every rock's top.
- **Gadget routes** (Swingshot, each tested in the game from start to end):
  - north jetty to the tower's top room (the secret archive);
  - the coast to the message-in-a-bottle islet, over the sea;
  - the lookout up the volcano's side to its shrine.
- **Easter eggs**: the Decomp Cave behind the second waterfall, the rubber duck in the tower, the
  bottle, a sunken chest marked 0x1F, the volcano's "undefined behaviour" shrine, and bolt crates
  that spell OPENRAC on the east beach (read them from the tower). Four gold bolts hide in the cave, the tower, on the islet and at the shrine.
- Two grind rails (the lookout to the tower, and round the lagoon). They are in the level but the
  game does not catch them yet (an editor issue being fixed); the tower is reached by Swingshot.

## Notes

- The light is baked: the island, the house and the landmarks together with the editor's
  `bake-light` (sun, shadows, sky, and every torch, lantern and campfire as a point light). It
  costs nothing in the game and never flickers.

- A new-planet session starts without a save, so it has no gadgets: start it with your own save,
  or with `--unlock-all-gadgets`.
- The island's collision covers where you can walk and where the gadget routes go. The high
  mountains away from the routes have none, so the level's data stays in its place in memory.
- The terrain node is named `Island`, not `Terrain_000`: the port hides a `Terrain_NNN` node with
  the game's own terrain fragment of that number.
