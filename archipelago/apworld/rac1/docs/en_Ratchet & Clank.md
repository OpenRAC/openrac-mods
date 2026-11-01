# Ratchet & Clank

## Where is the options page?

The [player options page for this game](../player-options) contains all the options you need to
configure and export a config file.

## What is randomized?

The gadgets and weapons NPCs and pickups give, the Gadgetron vendor's weapons, the gold bolts and
the planets' coordinates the infobots give. Each may hold an item of any world in the multiworld.

## What is the goal?

Beat Drek on Veldin.

## What does another world's item look like in the game?

The place gives nothing, and a line at the top of the screen says what was found and for whom.

## What does the logic expect?

The `logic` option. **Standard**: a place counts only with the gadgets the game's own route has
there, and an expensive purchase holds no progression. **Relaxed**: the same, and no progression in
a gold bolt either. **Expert**: only what a place cannot be done without counts. Every location's
rule is in the world's `data.py`.
