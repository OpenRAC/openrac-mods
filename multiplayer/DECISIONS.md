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

### 3. Gameplay mods that differ from the host's

Each game runs its own mods. Mods that change how the game plays (difficulty, item randomizer,
ammo-max, ...) make the games disagree: different enemy counts, damage, prices, items. Bolt drops
already follow the host's settings, because only the host's game drops them.

Choices:

- **(a) Refuse the join** when the gameplay-affecting mods, their versions or their settings differ
  from the host's ("Your mods don't match the host's", listing which). Mods that only change input
  or looks (keyboard-mouse, dualsense, xbox-icons, lock-strafe) don't count. Each mod declares
  whether it is gameplay-affecting in its `openrac-mod.json`.
- **(b) The host's settings win:** the host sends its gameplay mods' settings and the members' mods
  apply them for the party. Nicer, but every such mod must be able to take settings from outside.
- **(c) Allow it** (today), and accept the drift.

Recommendation: **(a)** now, **(b)** later for the mods that support it.

### 4. Item randomizer seed

With item-randomizer on, a shared item can be randomized differently in each game. If 3(a) or
3(b) is chosen, the party should also use the host's seed so every game rolls the same items.

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

The port side is PR #99 into `develop`; the mod is on openrac-mods master. Before merging, once the
co-op is confirmed working in play: squash PR #99 into a few meaningful commits (the SDK calls,
the port, the tests), then tag the SDK interface (1.9) and the mod (multiplayer-v0.7.0 or later),
and pin the new SDK in `sdk.lock` so CI builds the mod.

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
