# Multiplayer: decisions

The calls the co-op mod still needs made, and the ones already made, so anyone picking the work up
knows what is settled and what is open. Each open one says what the mod does today, the choices
and a recommendation; once it is decided, move it down to **Decided** with the date and who
decided it.

How the mod works is in [README.md](README.md). The design notes behind it (the world sync, the
puppets, the interpolation) are kept with the project's planning notes, not in this repository.

## Open

### 1. Saving each player's progress

Today the host's save is what the party plays. It is sent to the others at the start, and they
play it without it being written to their cards. Nothing saves the party's progress for anyone
but the host, and only when the host's own game saves (see 2).

Choices:

- **(a) Host only.** The host's card holds the session; the others keep nothing. Simplest, and
  what a dropped-in friend expects.
- **(b) A co-op copy for everyone.** When the host's game saves, every member gets the same
  progress written to a separate co-op slot on their own card (never over their own story
  saves), so they can host the session later, or continue it with the same friends.
- **(c) Merged into each player's own save.** Members bring their own save; what they collect in
  the party (items, gold bolts, planets) is added to it. The most "it all counts", but two
  players' stories can disagree (planets, cutscenes seen), and that needs rules of its own.

Recommendation: **(b)**. It keeps each player's own story saves safe and lets anyone resume the
session. It needs a chosen slot (or a separate co-op card folder), a "save the party" moment the
host triggers (the game's own saves, or a Save item on the pause page), and the mod writing the
host's save to each member when it happens.

### 2. The game's own saves on members' cards

The game saves by itself at some moments (a gold bolt pickup, story points). In a party these
still write each **member's** card with the party's progress, over whatever save slot the game
last used, even though the party never meant to.

Choices:

- **(a) Block them on members:** a port hook on the game's save routine (`memcard_Save`) that does
  nothing in a member's game. Still unknown: whether that routine also keeps checkpoint state, so
  skipping it may break respawning. That has to be checked in the decomp first.
- **(b) Redirect them** to the co-op slot of 1(b).
- **(c) Leave them**, and say so in the README (the current state).

Recommendation: **(b)** if 1(b) is chosen, otherwise **(a)**, once the checkpoint question is
answered. Until then the README warns that a member's card can be written.

### 5. What progress is shared

Shared today: owned items (weapons and gadgets), gold bolts and unlocked planets, merged across
the party; bolts are each player's own (the collector gets them). Not shared:

- nanotech and ammo (each player's own, as the game credits whoever picks them up),
- gold weapons,
- skill points,
- per-level story flags (talked to, doors opened, events done).

Open: whether gold weapons, skill points and story flags should be shared, and whether nanotech
and ammo should stay each player's own (recommended: yes, they are per-hero survival).

### 6. The host leaving

Today, when the host leaves or drops, the party is over: everyone goes back to the Multiplayer
page. Choice: keep it (simple, and the session is the host's save anyway), or **host migration**
(another member takes over, from a full world snapshot). Migration is a large piece of work;
recommended only after 1 is settled, since it decides whose save the session lives on.

### 7. PAL and NTSC players in one party

The version handshake refuses a different build. Whether PAL and NTSC-U players may play together
is open: their level data and timings are close, but the world sync's keys and the shared-progress
addresses have only been checked on PAL (the NTSC addresses for shared progress have not been run
on that disc). Recommendation: refuse mixed regions until the NTSC side is tested, with a message
that says why, without naming regions to players (only "a different disc").

### 8. Release

The port side is PR #99 into `develop`; the mod is on rac1-mods master. Before merging, once the
co-op is confirmed working in play: squash PR #99 into a few meaningful commits (the SDK calls,
the port, the tests), then tag the SDK interface (1.9) and the mod (multiplayer-v0.7.0 or later),
and pin the new SDK in `sdk.lock` so CI builds the mod.

### 9. What a player's death does to the world

The game's respawn remakes the level in one frame (LoadLevelGameplay): alone, enemies and crates come
back. In a party the mod keeps what is dead for the party: a member's game gets it from the host, and
a host's game has it deleted again, so one player's death brings nothing back for the others. Open:

- **The host's spawned mobys** go with its respawn, for everyone (bolts lying about included).
- **Missions.** A placed moby deleted because a mission moved on stays deleted in a host's game
  whose checkpoint is before that point. Nothing in testing got stuck on it, but only level 0 and 3
  were played that way. If one does, `World::remade` is the place to spare that class.

Choices: (a) keep this, (b) a host's death also respawns every member at the same checkpoint, which
is what a single world would do but pulls everyone back for one player's fall.

Recommendation: **(a)**.

### 10. Where a late joiner lands

A joiner comes in where the level's start or the ship puts it, with the game's own landing passed by
itself. Choices: (a) keep this, (b) put its Ratchet beside the host once it has the world: nicer, but
a host on a moving platform or in a vehicle has no good spot beside it, and the SDK has no call for it
yet.

### 11. A member's metal detector and class 152

The classes that act on the player standing in them now run in each player's game (`world.hpp`). Two
things follow. Buried bolts (605) are found by each player, each getting the bolt from its own game,
so two players can dig the same one; they could go through the host as a claim. Class 152
(`bolt_pickup_update`, Batalia) adds to the count directly, with no call the mod sees, and no spawner
was found: it stays the host's, and a member cannot collect it.

## Decided

| What | Decision | When |
|---|---|---|
| Network model | Hybrid: the server links players and relays; the host's game is the authority | 2026-10-27 |
| Transport | Raw TCP for everything reliable, UDP for game state with TCP as the fallback | 2026-10-27 |
| Without a server | Direct connect: the host's game embeds the server's party routing | 2026-10-27 |
| Pause in a party | The menu opens over a running world; nothing pauses underneath | 2026-10-28 |
| Cutscenes | Everyone plays the host's scenes together and waits for all to leave them; only the host can skip | 2026-10-28 |
| Gold bolt cutscene | Plays only for the player who picked it up; the gold bolt counts for everyone | 2026-10-28 |
| Other players in cutscenes | Hidden while a scene plays | 2026-10-28 |
| Ship travel | Only the host flies; everyone follows and waits for all to arrive | 2026-10-28 |
| Bolts | The host's game owns every dropped and placed bolt; the first to collect one is credited | 2026-10-28 |
| Start | Only when every member is Ready | 2026-10-28 |
| Leaving | Whoever leaves goes back to the Multiplayer page; when the host leaves, everyone does | 2026-10-28 |
| Friendly fire | Off | 2026-10-27 |
| Party size | Up to 4 players | 2026-10-27 |
| Verbose logging | A `verbose_log` setting, off unless turned on | 2026-10-28 |
| Gameplay mods that differ from the host's (3) | The join is refused: a gameplay mod's id, version and gameplay settings must match the host's, also an extra one a member has. Input and cosmetic mods don't count, nor settings that are a player's own or the host's to give. Each mod declares this in its `openrac-mod.json`; undeclared counts as gameplay. A `solo` mod (Archipelago) cannot be played in a party. Decided by the user | 2026-10-31 |
| Host-owned rules (3) | A mod may publish party rules; each member's same mod takes the host's before any level loads. Difficulty's members use the host's seed and values and make no enemy copies or removals of their own; the host decides Chaos events and the party shares them. Decided by the user | 2026-10-31 |
| Item randomizer seed (4) | Members play the host's seed, so every game has the same placement; an item passed on from another player stays that item. Decided by the user | 2026-10-31 |
| Mod sync codes | A short 8-character code on the server's HTTP side-port (7771), living 30 days from its last read, and a long self-contained code for offline. A code names catalogue mods at exact versions with hashes and never carries a download address. OpenRAC's own repositories need no asking; another asks the player once. Nothing applies without the launcher's review, a backup is kept to restore, and one code serves both discs. A game shows the code from `plugins/modset.json` only while its mods match. Decided by the user | 2026-10-31 |
| Other players' shots | Every weapon's shots are shown, and only shown: damage is the shooter's game's to tell and the host's to apply. A shot is state, not an event: it may be lost, and one more than 250 ms late is dropped. What flies by itself goes through the game's own spawn; the Pyrocitor, the beams, the Visibomb and the drones are drawn apart, because their code belongs to the hero. Decided by the user | 2026-10-31 |
| A figure's weapon classes (G4) | One weapon class besides the hero's, as the game has: the two decode buffers stay, since the heap has no room for more on every level. The player nearest the hero keeps the buffer, with a margin of 4 units and at least 2 seconds held; the others hold the wrench and their shots still show. Decided by the user | 2026-10-31 |
