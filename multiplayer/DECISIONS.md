# Multiplayer: decisions

The calls the co-op mod still needs made, and the ones already made, so anyone picking the work up
knows what is settled and what is open. Each open one says what the mod does today, the choices
and a recommendation; once it is decided, move it down to **Decided** with the date and who
decided it.

How the mod works is in [README.md](README.md). The design notes behind it (the world sync, the
puppets, the interpolation) are kept with the project's planning notes, not in this repository.

## Open

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

### 1. Saving each player's progress (decided by the user, 2026-10-31)

**A co-op slot for everyone, the host too.** Whatever a party's game saves goes to a slot of its
own, in a folder beside the game's on each player's card, never over a player's own saves. A
session starts from a copy of the save chosen. When the host's game saves, every member gets the
same save in its slot, and anyone can later Continue as host from theirs. A slot is the newest of
the host's save and that player's game's own: a slot the host's save reached last holds the host's
bolts, ammo and nanotech, not that player's. Save the party on the pause page saves now.

### 2. The game's own saves (decided by the user, 2026-10-31)

**Redirected**, by a filter on the card's writes where both discs meet (`mc.c`), not by skipping
`memcard_Save`. Read on both discs: `memcard_Save` holds no respawn state (the clock, the map, the
level objects' positions, the slot's record and the two parts serialised for the card), so
filtering is safe. It also turned out a party's game did not save at all unless its player had
been through the game's own load or save menus first, because the routine does nothing without
them: the port now sets what it needs for as long as a party plays. The filter stays until the game
is back at the title, because a game left in a level still holds the party's progress.

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
| PAL and NTSC players in one party | Allowed: discs of one world family play together, keyed by what they share, and the host's disc rules what the host runs. Anything genuinely different is still refused, as "a different disc". Decided by the user | 2026-10-31 |
| Gameplay mods that differ from the host's (3) | The join is refused: a gameplay mod's id, version and gameplay settings must match the host's, also an extra one a member has. Input and cosmetic mods don't count, nor settings that are a player's own or the host's to give. Each mod declares this in its `openrac-mod.json`; undeclared counts as gameplay. A `solo` mod (Archipelago) cannot be played in a party. Decided by the user | 2026-10-31 |
| Host-owned rules (3) | A mod may publish party rules; each member's same mod takes the host's before any level loads. Difficulty's members use the host's seed and values and make no enemy copies or removals of their own; the host decides Chaos events and the party shares them. Decided by the user | 2026-10-31 |
| Item randomizer seed (4) | Members play the host's seed, so every game has the same placement; an item passed on from another player stays that item. Decided by the user | 2026-10-31 |
| Mod sync codes | A short 8-character code on the server's HTTP side-port (7771), living 30 days from its last read, and a long self-contained code for offline. A code names catalogue mods at exact versions with hashes and never carries a download address. OpenRAC's own repositories need no asking; another asks the player once. Nothing applies without the launcher's review, a backup is kept to restore, and one code serves both discs. A game shows the code from `plugins/modset.json` only while its mods match. Decided by the user | 2026-10-31 |
| Name tags | Over each figure, in a colour per party place, a dot past 40 metres, a mark while lagging, seen through walls, none under a menu or in a scene | 2026-10-31 |
| The picture under the pause menu | The world goes on in the picture too (`play_live_picture`); particles drawn but not moved | 2026-10-31 |
| A mod code in the game | Shown on the host's Rules page and a refused player's page, only while the running gameplay mods are the ones it names; a member gets it from the host in MODS, as a code's shape only, and the link is made from that | 2026-10-31 |
| Other players' shots | Every weapon's shots are shown, and only shown: damage is the shooter's game's to tell and the host's to apply. A shot is state, not an event: it may be lost, and one more than 250 ms late is dropped. What flies by itself goes through the game's own spawn; the Pyrocitor, the beams, the Visibomb and the drones are drawn apart, because their code belongs to the hero. Decided by the user | 2026-10-31 |
| A figure's weapon classes (G4) | One weapon class besides the hero's, as the game has: the two decode buffers stay, since the heap has no room for more on every level. The player nearest the hero keeps the buffer, with a margin of 4 units and at least 2 seconds held; the others hold the wrench and their shots still show. Decided by the user | 2026-10-31 |
