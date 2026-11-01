# Multiplayer: decisions

The calls the co-op mod still needs made, and the ones already made, so anyone picking the work up
knows what is settled and what is open. Each open one says what the mod does today, the choices
and a recommendation; once it is decided, move it down to **Decided** with the date and who
decided it.

How the mod works is in [README.md](README.md). The design notes behind it (the world sync, the
puppets, the interpolation) are kept with the project's planning notes, not in this repository.

## Open

### 8. Release

Nothing merges before the user's play test. Then, in this order:

1. Merge OpenRAC PR #160, the rac1 sync, into `develop`.
2. The OpenRAC branch (`fix/coop-sync` and `round3`, the SDK calls, the port, the launcher) as a PR
   into `develop`, squashed into a few meaningful commits. `master` only by a release PR: a mod of
   interface 1.12 loads only on a port of 1.12, so players need that OpenRAC before the mod.
3. Release SDK 1.12.0 (`sdk/VERSION` says it, the tag is `sdk-v1.12.0`).
4. Pin it: `sdk.lock` still names 1.11.0, and stays so until that release. Set its version, url and
   sha256 to 1.12.0's. Until then this repository's CI cannot build (multiplayer's manifest and the
   top CMakeLists.txt ask for 1.12): build with `-DOPENRAC_SDK=<OpenRAC>/sdk`.
5. Land `round3` on rac1-mods master and tag `multiplayer-v0.10.0`. Difficulty and Item Randomizer
   gained tests and a missing include, nothing a player sees, and keep their versions.
6. Deploy the Server (`r3/migrate`): TCP and UDP 7770, TCP 7771 for mod codes, RESUME, takeover and
   KICK. Against an older server the mod leaves those out by the features it offers: no kept places
   and no takeover, a removed player leaves by itself, and no short mod codes.

### Asked for and not built

- **Hold the party**, the host's optional hold of the ship's warning: only the 2.5 seconds are there.
- **Checks on a member's HIT.** The host takes a HIT for any moby still there, as it did before: no
  check of the damage or the distance yet. DAMAGE has none either.
- **Auto-join after a mod code applies**: `openrac://modset/CODE` opens the review in the launcher,
  and the player then joins by hand. A later step, by the plan.
- **`play_scene_from`** is a kept slot of interface 1.12 that answers "unknown": a scene cannot start
  part way, so a scene in progress is waited out, as decided.

## Decided

### 6. The host leaving (decided by the user, 2026-10-31)

In order. **Reconnect**: a dropped player keeps its slot for 2 minutes, with a stable id and a token,
and comes back by the late-join path. **Host drop**: a pause-and-wait of 15 seconds with a line on
every screen, not 60. **Takeover**: every member keeps a small reliable party ledger; on takeover the
successor, chosen by link quality, takes the newest ledger by epoch and generation, reconciles its
game with it, and the members get the world by the late-join path. A bumped epoch rejects the old
host's traffic. Continue as host from the co-op slot stays the fallback when nobody can take over.

Settled in the building (2026-10-31):
- A host that leaves on purpose is followed at once; before the game starts, the party ends as ever.
- An old host back within its slot comes in as a member through the title: its world is no one's now.
- A new host goes on by the rules and the mod list the party was checked against, not its own.
- The ledger never holds a position or an enemy's mind: a new host's enemies start fresh where they
  stood. A state's fate on takeover is in `src/migrate.hpp`, each and every one.
- Direct connect keeps slots and has no takeover: its host is the party.

### Enemies that grab (decided by the user, 2026-10-31)

Authority stays on the host. The enemy keeps a target with a band and a stick while attacking; a grab
is an exclusive lock on its victim plus a host-commanded event with an animation id, and the victim's
game plays a local grabbed state that the host checks against the player's pose. Every moby update
that writes the hero was read on both discs: three grab.

- The Rilgar sharkigator (35) has no health and takes whoever is in the water: each game's own, as
  water that kills.
- The Umbris sharkigators (1059) and the snagglebeast (1106) can be fought: the host's. The first
  kills in its leap, the second holds on its rope until it lets go or dies.
- Nothing that an update does while a member stands in reaches the host's hero. A kill of the hero
  who stood in goes to that member's game, as a respawn.

### 9. What a player's death does to the world (decided by the user, 2026-10-31)

Keep (a): a player's death is that player's own, and what is dead for the party stays dead. On top:

- A full wipe (the host's level made again with every member dead, or dead within 5 seconds, or
  everyone down under the revive rule) reloads the host's last checkpoint: only what its reloaded
  level has dead stays dead, every member goes through its own respawn and is then put beside the host.
- Stand-over revive: a host rule, off by default (the `revive` setting, shown on the Rules page). A
  player who dies on his feet stays down up to 30 seconds; a living player standing within 2.5 units
  for 1.5 seconds brings him back where he lay, with half his health. Drowning, sinking, falling or
  dying on a ride goes to the checkpoint as always: nobody could stand over him.
- A placed moby of a mission the host has not done comes back with the host's level, for everyone (a
  member, whose copy is gone, is given one made from the host's); a local class comes back in the
  host's game only.
- The host's bolts lying about are made again where they lay. The enemies it had spawned go with its
  level, whose spawners make new ones.

### 11. Buried bolts and class 152 (decided by the user, 2026-10-31)

Buried bolts go through a host claim: a member's dig waits for the host's word, the first to ask of a
pile still there digs it, and it goes from every other game. Bolts dug in a member's game are that
member's own. Class 152: no level of either disc places it and no code spawns it (measured
2026-10-31), so it reaches no player today. The route is there all the same: shared when spawned, and
what it adds under a member's stand-in is that member's.


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

### 5. What progress is shared (2026-10-31, decided by the user)

Shared and credited to the party: items, gold bolts, planets, gold weapons and skill points; the
story's flags are the host's save's, and the special items they go with. Bolts, ammo and nanotech
(the upgrades too) stay each player's. Progress only merges upward. Flags a player's own copy of a
person or pickup sets as its scene ends stay out of a game still to play that scene: a member's
never reach the host, the host's reach a member when it leaves that level (`src/shared.cpp`,
`kSceneFlags`, read from the game's code and both discs' levels). A late joiner gets all of it
silently: no banners, no rewards replayed. The dialogue records (`race_gates`) are not shared: taxis
write them too, so they do not only go up, and nothing in the story waits on them for a member.

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
| Leaving | Whoever leaves goes back to the Multiplayer page; when the host leaves, the party follows item 6 | 2026-10-28, 2026-10-31 |
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
| Removing a player | Host only, no vote, from the lobby and the pause menu's page. The server's KICK (11) and KICKED (12), Direct's host alike; a kick is no ban: the code still works, match finding skips that party | 2026-10-31 |
| Bans | Per co-op save, against the launcher's key (not a name or an address), kept in the save's note and in every member's copy, so they hold when a member hosts it; Party rules lifts them | 2026-10-31 |
| Data packs in a party | A pack that changes the game is compared like a gameplay mod, in order, by what it holds, so one pack is one number on either disc; a pack of looks alone is each player's own | 2026-10-31 |
| Keybinds and the player's name | Each player's own, never compared, whatever a manifest leaves unsaid: mod codes leave them out, so a code always makes two games match | 2026-10-31 |
| Vendors | Each player's own: a member at one leaves with the host's ship by the game's own travel (`play_travel` takes it out of a menu) | 2026-10-31 |
| A mod code in the game | Shown on the host's Rules page and a refused player's page, only while the running gameplay mods are the ones it names; a member gets it from the host in MODS, as a code's shape only, and the link is made from that | 2026-10-31 |
| Where a late joiner lands | Held on the joining line until the host is at a safe point, then put beside the host with 3 seconds of safety; a host not safe for 10 seconds gives its last safe spot, and with none, the ship or the level's start. A scene in progress is waited out, not replayed. **Go to host** on the pause page uses the same terms, for any member. Late joiners take progress silently. Decided by the user | 2026-10-31 |
| Waits and travel | Start in a wait for others: the host's goes on at once, a member's is a vote, more than half of those waiting. A 2.5 second warning before the host's ship takes a member along. Decided by the user | 2026-10-31 |
| A host that starts again | A member in a level leaves for the title and starts the new game from there. Decided by the user | 2026-10-31 |
| Camera shots that are not scenes | Who sees one goes by its kind, below. Only the camera moves, for a player touching nothing and not just hurt, and anything pressed or a hit brings it back at once. A gold bolt's stays the collector's. Sent as views, so nothing depends on a disc's data. Decided by the user | 2026-10-31 |
| Other players' shots | Every weapon's shots are shown, and only shown: damage is the shooter's game's to tell and the host's to apply. A shot is state, not an event: it may be lost, and one more than 250 ms late is dropped. What flies by itself goes through the game's own spawn; the Pyrocitor, the beams, the Visibomb and the drones are drawn apart, because their code belongs to the hero. Decided by the user | 2026-10-31 |
| A figure's weapon classes (G4) | One weapon class besides the hero's, as the game has: the two decode buffers stay, since the heap has no room for more on every level. The player nearest the hero keeps the buffer, with a margin of 4 units and at least 2 seconds held; the others hold the wrench and their shots still show. Decided by the user | 2026-10-31 |
| Keeping the world in step | The host counts what each member has heard of WORLD, by acks on its poses, and sends again what was lost: no blind repeats, and a far moby's lost change comes back in a round trip or two, not at the next refresh. What goes first is a priority per member and moby, by distance and relevance, with a floor so nothing starves. Every stream keeps at least two of its send intervals of margin. Decided by the user | 2026-10-31 |
| The playback cursor's rate | 10 % fast or slow, measured against 5 and 15 %: 5 takes 2.3 s for what 10 does in 0.9 s, and 15 gains nothing to see and runs an animation faster. Decided by the user | 2026-10-31 |

## Camera scripts, by class (2026-10-31)

The user's rule: a gold bolt's scene stays the collector's, and a camera must never move in a way
that gets a player killed. The game has no list of scripts: each is a moby calling the game's camera,
so its class says what it is. Any class not named is the player's own.

| Who sees it | Classes | Why |
|---|---|---|
| Those within 60 of its moby | 737 camera triggers; 1028 Blarg's drawbridge; 1083, 1108, 1118 Blarg's shots and red button; 1156, 1157 Pokitaru's controllers; 654 Kalebo's objective; 701 Novalis's falling rock | The level shows something change, for whoever is there, and nothing there is about to attack because of the shot |
| Those on it (within 4 when it starts) | 877, 21, 244, 1270 lifts | The shot follows the ride. Anyone else would look at a lift they are not on |
| Its own player | 1134 gold bolt | Settled |
| Its own player | 440, 1201, 1267 cannons and turrets; 1242, 69, 1379 fighters; 713, 1109, 1448 pods and shuttles | A seat: the view is how its player aims or steers, and it would be useless and blinding to anyone else |
| Its own player | 280 crank, 1251 hydrodisplacer, 615 and 1005 Trespasser | A machine one player works. The hydrodisplacer's also moves water under others, the worst time to lose sight of it |
| Its own player | 459 birdbot, 688 bomber jet, 1051 alien queen | They open a fight. A camera pulled away just as an enemy wakes is what the rule forbids |
| Its own player | 11 and 644 vendors, 1105 Fred | A person's or a shop's business with one player |
| Its own player | 1422 Drek | It leads straight into a scene, which plays for everyone anyway |

A member's stand-in can set off a script in the host's game. That script is the member's: the
host's camera and Ratchet stay out of it, and the host is shown it only by the same rules as anyone.
