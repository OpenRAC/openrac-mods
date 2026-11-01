# Multiplayer

Online multiplayer for Ratchet & Clank on OpenRAC's native port (PAL and NTSC-U discs): a
**Multiplayer** item in the game's main menu that connects to an OpenRAC server and puts you in a
party with other players, or, with **Direct connect**, plays without a server: one player's game
hosts and the others join it by its address. The host picks a save (or a new game), and its
**Start** starts that game for the whole party. Everyone plays in the host's level, sees the
others' Ratchets running about in it, and shares its world: the host's game runs the enemies,
doors and platforms, the others show them and fight them, and an enemy goes for whichever player
is nearest.

With the mod off, the game is the PlayStation 2 game.

Each other player nearby and in view has a name over their figure, in a colour of their own that is
the same in every game: smaller further away, fading out by 25 metres, gone behind a wall, with a
small amber mark while they lag. Under the pause menu the world goes on, in the picture too.

Friends match their mods with a mod code: **Share these mods** on the launcher's Mods page makes
one, and entering it there shows what would change before anything does (OpenRAC's
`docs/port/MODS.md`, section 10).

What is still to be decided, and what already is, is in [DECISIONS.md](DECISIONS.md).

## Playing

1. Install the mod from the launcher's **Mods** tab, switch it on, and set **Server** to the
   OpenRAC server you play on (`game.openrac.dev:7770` by default, the public OpenRAC server;
   `localhost:7770` for a server on your own computer).
2. Your name is the mod's **Name** setting, or, while that is empty, `${player_name}` (the default) or not a valid name, the
   launcher's **Multiplayer name** (Settings, General), which the launcher makes up at its first
   start. Either way it is 3 to 16 letters, digits and underscores. Without either (the game started
   without the launcher), the mod makes one up (`Player` and three digits).
3. Start the game. The title's main menu reads **New Game**, **Load Game**, **Multiplayer**,
   **Options**: choose **Multiplayer**.

Its pages are the game's own menus: its frames, its list and font, its prompt box and sounds.

| Page | What it does |
|---|---|
| Home | **Browse games**: the games others host. **Create match**: a party of your own, from a save (the next page), which Browse games lists and its code invites to. **Join by code**: the next page. The three wait for the server's hello (the cursor moves up to Browse games when it comes). **Direct connect**: the next page, no server needed. Below them, your name |
| Browse games | **Refresh**, then a row for each game being hosted that has room: its host's name, its save ("Pokitaru, 12 gold bolts", or "New game"), how many are in it (`2/4`), and **(differs)** when your game would not fit it: other gameplay mods, settings or versions; the footer says what that means. Cross on a row joins it, and a game marked (differs) then says how. The list is asked again every 3 seconds while the page shows; with none, a line says so and points at Create match |
| Choose a save | **New game**, then your memory card's five saves, each with its planet, gold bolts and time played (an empty slot is dim). The party plays a copy of the one chosen; your own saves are never written by a party. **Continue as host** lists the party saves on this card (below). Create match and Host directly both come here first |
| Join by code | A row per character of the party's code: Left and Right (or Cross) step it, Up and Down move between them; **Join** joins. Codes use the letters and digits that do not read alike (no 0, O, 1 or I). Once in the party, the lobby takes this page's place |
| Direct connect | **Host directly**: your game hosts (below); the lobby opens. **Join by address**: the address's rows take this page's place: **Address part 1** to **4** (each 0 to 255), **Port** (by 1) and **Port, by 100**, all stepped with Left and Right (held, they repeat, and after a moment step ten at a time), then **Join**; the title shows the address as it stands (`Join 192.168.1.1:7770` at first; the last one joined is kept until the game ends). Triangle goes back from the rows to the two buttons (giving up a join still connecting), then to Home |
| Lobby | The party's code in the title (a direct party: **Hosting on** your address and port, every address of yours that fits, or **Party at** the host's address), **Ready** (a member: it is ready; chosen again, **Not ready**; once the game is on, **Join the game**; **Checking your mods...** until the host answers; **See why you can't join** when your mods differ) or **Start** (the host: the game starts for everyone; dim, and refused with the game's refusal sound, until every member is ready, and the count line says **not all ready**), **Leave**, **Party rules**, what the party plays (**Playing:** the save), then the members (up to 4), the host marked and each other one **(ready)**, **(not ready, mods match)**, **(mods differ)** or **(checking mods)**, for the host a line saying why for each one that differs, and the round trip to the server. **Leave** and Triangle ask first (**Stop hosting?** for a direct host); when the host leaves, another player takes over once the game is on (the question says so), and before that the party is over for everyone. Leaving a direct party goes back to Home and to the server |
| Remove | The host's Cross on a member in the lobby, or on a member's line of the pause menu's Multiplayer page: **Remove from the party** (the code still works, so it may come back) or **Ban from this save**, then back. See [Removing a player](#removing-a-player) |
| Refused | Shows by itself when your game cannot join the host's, and from the lobby's button: how yours differs, and what to do. See [Mods in a party](#mods-in-a-party) |
| Party rules | From the lobby and the pause menu: what the party plays by, and what stays your own. A host with a mod code has **Copy mod code**, a refused player too, and a host whose save has bans **Lift 2 bans** |
| Pause menu (in a level) | **Options**, then **Multiplayer** (after Video): the party's code (a direct party: where it is), the host's level, each member with the host and you marked and its round trip to the server and whether its game state goes by UDP or TCP (updated every 2 seconds), then **Party rules** and **Leave party**, which asks first (**End the party?** for the host, **Stop hosting?** for a direct host). Out of a party it says **Not in a party**. Leaving goes back to the title's Multiplayer page (below) |

## Playing together

1. The host: **Create match** (or **Direct connect**, **Host directly**), then a save or **New game**.
2. The others: **Browse games** and Cross on the host's game (or **Join by code**, or **Join by
   address** for a direct one). Each says **Ready**.
3. The host: **Start**, once everyone is Ready (one who joins later is not, until it says so). Every game leaves the menu and loads the host's game in the host's level:
   a new game starts on Veldin, a save where it was saved. The others' cards are not touched.
4. In a level each game shows the others' Ratchets where they are, running, jumping and standing
   as they do, about a tenth of a second behind. They walk through you and nothing aims at them.
5. Only the host flies the ship: when it picks another planet in the ship's planet select, every
   member is told ("The host is leaving for Pokitaru") and its game leaves its level 2.5 seconds
   later, from wherever it is (playing, a scene, the pause menu, a vendor), and flies there. Nobody
   gets control on the new planet, and nothing of the host's world shows there, until everyone has
   arrived ("Waiting for the other players to arrive" says why; at most 90 seconds for one that
   never comes). Start in that wait goes on: the host's at once, a member's as a vote, more than
   half of those waiting needed, the line counting them. A member's own pick of another planet
   lands the ship again where it is ("Only the host flies the ship to another planet"). Someone who
   joins while the game is on gets the host's progress as it is then and goes straight to the
   host's level ("Joining the game"): whatever would play on the way (the game's movies, the ship's
   landing, a story scene of the level's) is passed by itself, no scene of the party's is that
   player's to play, nobody waits for it, and once it stands in the level it has the host's world
   as it is, broken crates, dead enemies, bolts on the ground and all. It then stays held, on that
   line, until the host stands somewhere safe, and is put beside the host, unharmed for 3 seconds;
   what the party has reached it silently, with no banners. A scene the host plays is waited out,
   never replayed (Start goes in at once, where the player stands). A host that is not safe for 10
   seconds gives its last safe spot, and with none in this level the player stays where the ship
   or the level's start put it. Someone who quits to the title finds **Join the game** in the lobby
   in Ready's place, which asks the host for its game again.

Each player sees the others' Ratchets holding what they hold: the wrench, the weapon or gadget,
Clank on the back, with their swings and throws, and the shots of their weapons (shown only: what
they hit is told by the shooter's game).

The world is the host's:

- **Enemies, doors, platforms, pickups** run in the host's game; the others' games show them where
  the host's are (about a tenth of a second behind) and do not run them. A member's wrench or
  weapon hitting one hits the host's, so it takes the damage there and dies for everyone.
- **Enemies go for the nearest player**: in the host's game each one sees the player nearest it
  as the hero, so it chases, aims at and hits that player; the hit hurts that player in their own
  game (knocked back as the game does it).
- **Crates** run in every game: a crate broken in one game breaks in all, with its debris and
  sound.
- **Bolts are the host's, once in the world.** Only the host's game drops them: its enemies' and
  its crates', a crate a member broke too (the host's copy of it breaks and drops). The members'
  games drop none of their own. Every player sees the same bolts fly out of the same crate or enemy
  and land in the same places. A bolt flies to the player nearest it (in the host's game that
  player's figure stands in for the hero, as for an enemy), and what it brings is that player's:
  break a crate next to it and its bolts are yours. Once landed, each one hovers and flies to
  whichever Ratchet comes near, in every game.
- **A bolt is the first collector's.** A member's collected bolt (one dropped or one lying about
  in the level) adds nothing in that member's game: it asks the host, who gives it to the first
  to ask while it is still there, takes it from every game and sends that player its bolts. A
  later collector of the same bolt gets nothing (its bolt has gone, without a count). The host's
  own are the host's at once. Each player has their own bolt count.
- **Other pickups** (gold bolts, ammo, nanotech) run in every game: anyone can collect one, and the
  first to collect it takes it from every game. Ammo and health an enemy drops in the host's game,
  and what a nanotech or ammo crate gives, are made in every game, for each player to collect.
- **Progress is the party's**: a gold bolt anyone collects counts for everyone (marked collected
  in every game, so it is not there on a reload or the next visit either), and a weapon, gadget or
  other item anyone gets (found, given or bought) and a planet anyone unlocks, everyone has. The
  host's game keeps the party's progress: a member's goes to it, and from it to every game, given
  as the game gives it (its banner) once that player is in control. Nothing is ever taken away.
- **The gold weapons and the skill points are the party's too**, and so are the story's other
  things had (a Codebot, the Raritanium, the Zoomerator): anyone's is everyone's, set without a
  banner. The nanotech upgrades stay each player's own. A gold weapon in hand when it arrives turns
  gold the next time it is taken out.
- **The story's flags are the host's**: a door, a lift or a person follows what the host's game has
  done, in every game and also for someone who comes later. A checkpoint a member reaches counts for
  the party. A person or an infobot a member gets to first stays for the host, who plays that scene
  for the party when it gets there; and one the host has finished goes from a member's game when that
  member next leaves the level, not while it may be talking to them.
- **Nothing replays for a late joiner**: what the party had by the time it arrives is simply there,
  with no banner, sound or reward. What comes after that comes as it does for anyone.
- **A gold bolt's scene is the collector's**: it plays in that player's game only; the others
  keep playing, and nobody waits for it.
- A placed enemy or pickup gone in the host's game is gone in everyone's, also for someone who
  arrives in the level later. A moby goes only when a game says it died, never because a snapshot
  is missing: a game that is loading or in a menu changes nothing in the others.
- **A death puts nothing back for the party.** The game's own respawn makes the level again in one
  frame: every moby as the level file has it. A member who dies gets the party's world again as
  soon as its game is back, so what was broken stays broken for it. A host who dies keeps it too:
  what was dead for the party is deleted again in its game. Bolts lying about are made again where
  they lay; the enemies the host had spawned go with its respawn. A mission's enemies come back when
  the host's mission starts again. If everyone dies at once, the party goes back to the host's last
  checkpoint and you land beside the host.
- **Revive** (a host's setting, off by default): a fallen player stays down for up to 30 seconds, and
  standing over them for a moment brings them back.
- **Enemies** keep their target until another player is clearly nearer, and finish an attack first.
- **Grabs.** An enemy that grabs stays on its victim until it lets go, however long, and nobody else
  can grab that player meanwhile. The victim's own game holds the hero, controls gone; the others
  can hit the enemy. The hold ends with the enemy's death, the player's, or the player leaving, and
  by itself if the host stops talking. Rilgar's sharkigators are each player's own, like the water.
- **Crates that come back** (Rilgar, Kalebo III, the last level's ammo crates): a break in any game
  breaks them all and its bolts are shared; each game's copy comes back by itself, out of sight.
- **Buried bolts**: one pile, one digger. The first to dig gets it, and it goes for everyone else.
- **Go to host**: on the pause menu's Multiplayer page, for any member in the host's level. Your
  Ratchet is put beside the host, unharmed for 3 seconds, when you close the menu, on the same
  terms as a late joiner; with no safe spot near the host, where you landed in this level.
- **Vendors, the ship, people, help desks**: each game's own, for its own player; the level a
  member is in follows the host's, and only the host's ship leaves for another planet.
- **Menus do not pause** while the party plays: the pause menu and the game's dialogs open over a
  world that goes on (enemies, platforms, the other players); your Ratchet waits where he is.
- **Scenes play for everyone**: a scene of the level's own that starts in the host's game (a
  person spoken to, a place reached, Veldin's opening) starts in each member's game in that level
  too, from its start, as soon as that member has control (a member in a menu starts it once the
  menu is closed), with its own actors. A member who started it late sees it to the end, even
  when its menu or a load kept it until after the host's had ended.
- **Only the host skips**: the host's Start in a scene or a movie skips it in every game. A
  member's Start in a scene or a movie does nothing but show "Only the host can skip" for a few
  seconds; the scene plays on everywhere.
- **The level's camera shots**: a shot of a door opening or of something happening (a camera shot
  that is not a scene) is shown to the players within about 60 metres who are touching nothing and
  have not just been hurt, and a lift's to those on it, whoever set it off. Only the camera moves:
  nobody is held, and pressing anything or being hit brings the camera back to you at once. A
  member's shot leaves the host's camera and Ratchet alone.
- **Scenes hide the others**: while a scene plays in your game, the other players' Ratchets, what
  they hold and their shots are not shown; they are back as soon as the scene is over.
- **Scenes end together**: when the host is out of its scene, a member still in one is skipped
  (one that started the host's scene late only once it had as long as it was late), and nobody
  gets control back until everyone is out (at most 10 seconds for the host; a member waits for
  the host's scene to end, and for its own copy of it to play). Start in that wait goes on without
  them, as for an arrival: nobody's scene is skipped by it. The ship's landing and take-off
  count, and so do level intros. A skip ends only the scene or movie it was pressed in, never
  the next one.

Starting again. The host back in the title with its party still there has **Start again** in the
lobby: a new game for everyone, from the save the party was made with, in a co-op slot of its own
(the last game's stays as it was). A member in a level leaves it for the title and starts the new
game from there.

Leaving. A player whose party goes while it plays (it chose Leave party, the host ended the party,
or its connection went) leaves its level at once, from wherever it is (playing, a scene, the pause
menu), for the title, which then opens the Multiplayer page; its footer says why when the player
did not leave by itself (**The host left: Continue as host from Saves**, **The host left: the party
is over**, **The host went away**, **The server went away**). A member leaving goes there alone, and
the party plays on without it. A player already in the title's pages (the lobby) goes back to Home.

Losing the connection (DECISIONS 6). A dropped player keeps its place for 2 minutes, under the same
id, while the mod reconnects (**Connection lost: getting back in**): its figure stands where it was,
its enemies turn to someone else, and it holds up no wait. Back in time it comes in as a late joiner:
the world again, beside the host, what the party got meanwhile without banners.

A host that drops: everyone is told at once (**The host lost its connection: waiting for it (12)**)
and stands still for 15 seconds. Back in time, it plays on, and every member is given the world
again. If not, or if the host left on purpose (at once, with no wait), the member with the best
link who holds the world takes over: **You are the party's host now**, **Kero is the party's host
now**. What the party did stands, kept by every member; the enemies pick up where they stood, their
minds fresh, a scene or a ship's flight on the way ends where it is, and the host's rules (Difficulty,
a seed) stay the party's, whoever hosts. A new host on the other disc changes whose enemies the Rules
page speaks of. An old host that comes back within its 2 minutes comes in as a member. If nobody can
take over (before the game starts, or no member holds the world), the party is over, and any member
can Continue as host from its party save. A direct host is the party: its members keep their places
if they drop, but with the host gone, it ends.

Cross chooses, Triangle goes back a page (from Home, to the main menu). The footer's first line says
what the connection is doing: connecting, connected, **Server unreachable** (the mod tries again
every few seconds), or **The server went away**, and the server's answers (a code no party has, a
full party, the host leaving). Only Browse games, Create match and Join by code need the server (they
are dim until it said hello); **Direct connect**, Host directly and Join by address are always
selectable. Choosing Direct connect stops the server retries, and the Direct pages never show the
server's state; leaving a direct party goes back to Home, which tries the server again.

| Setting | Default | |
|---|---|---|
| Server | `game.openrac.dev:7770` | The OpenRAC server, as host:port (`[::1]:7770` for an IPv6 address); 7770 when no port is given |
| Name | `${player_name}` (the launcher's Multiplayer name) | Your name in multiplayer, 3 to 16 letters, digits or underscores; empty, `${player_name}` or not valid: the launcher's Multiplayer name |
| Verbose log | off | For reports of the party's world going wrong (mobys lost when a player pauses): an `[mp-trace]` line for each event, one a line, in `mp-trace.log` in the game's working directory (beside its `run.log`; the game's log says where, once), on either side; also on with `OPENRAC_MP_TRACE=1`. Each line is `[mp-trace] f=<frame> t=<ms> <host, member or solo> <what> ...`, `what` one of: `pause` (the phase and the level's game mode changing, a menu entered and left, and each tick a menu is open: the camera mods read, the camera the game's run list measured from, the level's camera the pause menu puts up and what the menu's update did with the world), `run` (a moby the run list left out by distance while a menu is open, one going in or out of range, with its distance and its update and draw distances; once a second under a menu how many ran), `vis` (a moby hidden, shown, dead, gone from the listing, or drawn or not under a menu), `net` (every 5 seconds, what went through the session so far: DATA frames and state sent, what the buckets held back, the most events queued, then per DATA kind `e` events, `s` state and `r` received as `e7=messages/bytes`), `despawn` (a moby gone or hidden: its class, key, slot, driven, shared or local, who removed it, the sync for a message and its sender, this game's own update or another moby's, a mod, or vanished with no deletion seen, its last place, its distance from the hero and the camera, its state and flags, and on a member when the host last sent it; each moby_kill of the sync's), `eyes` (the points the host's game runs the world around), `snap` (each snapshot sent or received: its count, the ids added and missing since the last), `drive` (a moby driven from the host's or let go), `msg` (KILLED, DEAD, DESPAWN, BROKE and SPAWN sent or received) and `census` (every second, the live mobys by class and the ones that came and went). At most 2,000 lines a second, the rest counted in one line. Off: none of them |

## Mods in a party

Every game of a party plays the same game. So a gameplay mod must be the same in every game: its
version and its gameplay settings. The check runs when a player joins, in the lobby and for someone
joining a game that is on. Mods declare what they are in their `openrac-mod.json`:

| `multiplayer` | Mods | In a party |
|---|---|---|
| `gameplay` (also a mod that says nothing) | Difficulty, Item Randomizer, Ammo Max | Must match the host's. A player with one the host lacks differs too |
| `input` | Keyboard and Mouse, DualSense, Lock-strafe, Button Swap | Each player's own |
| `cosmetic` | Xbox Icons, Live Splits | Each player's own |
| `solo` | Archipelago | Cannot be played in a party: its checks and items belong to one Archipelago slot, and a party shares its items. It can neither host nor join: turn it off to play |

A setting of a gameplay mod may say `"multiplayer": "own"`, each player's (Detailed log, Spoiler
log), or `"multiplayer": "host"`, which the host's rules bring (the seeds). Neither is compared.

**A player who does not match** can't say Ready, and the host can't Start while that player is in
the party: the player leaves or fixes it, or the host removes it. The page says how, and shows by
itself the first time:

```
You can't join this game
  Leave party
  Difficulty: the host has 1.3.0, you have 1.2.3
  Your Difficulty settings differ:
  Enemy count, Damage to Ratchet
  Host has Item Randomizer, you don't
  You have Ammo Max, the host doesn't
Match the host in the launcher's Mods tab,
then restart the game and join again.
```

The other lines it may have: `You have a different disc from the host`, `OpenRAC: the host has
0.21.0, you have 0.20.0`, `Multiplayer: the host has 0.10.0, you have 0.9.0`, and `Archipelago
can't be played in a party`, with `Turn it off in the launcher's Mods tab, then restart the game.`
Settings count from the game's next start, so changing them needs a restart.

The host's lobby has a line for each player who differs: `Bob: Difficulty settings, 1 more`,
`Bob: no Item Randomizer`, `Bob: extra Ammo Max`, `Bob: Difficulty version`, `Bob: a different disc`,
`Bob: an older Multiplayer`. **Browse games** marks the games that would not fit with **(differs)**:
the party's listing carries a short digest of its host's build and gameplay mods.

**A player's keys and name are its own.** A `keybind` setting, and a string that defaults to the
player's name, are never compared, whatever the mod's manifest leaves unsaid. A mod code leaves out
the same settings, so a code applied always matches what the party checks.

**Data packs.** A pack that changes the game (gameplay data, collision, a map) has to be the same in
every game, in the same order, as a gameplay mod does: `Your New walls pack differs from the host's`.
A pack is compared by what it holds, so one pack is the same on either disc. A pack of looks alone
(textures, models, terrain) is each player's own. A mod code names the packs it cannot fetch.

**The host's rules.** Party rules shows them before Start and in a level, for instance:

```
Party rules
  Back
  Difficulty 1.3.0
  Bolts x2, Enemy count x1.5, Chaos on
  Item Randomizer 1.1.0
  Logic: standard, Shuffles gadgets, Shuffles weapons
  Stays yours: your controls and looks
  Detailed log, Keyboard and Mouse, Xbox Icons
```

A mod may publish rules for the party, through the SDK, and each member's same mod takes the host's
before any level loads, also in a game that is on, and keeps them through every load and respawn
until the party goes. With no party they change nothing.

- **Difficulty**: members use the host's sliders, Chaos, resolved seed and each level's rolls. Only
  the host's game makes copies of enemies or leaves some out: copies reach the others as the host's
  spawned mobys, removals as dead placed mobys. Enemy speed is the host's, since its game runs the
  enemies. Bolts are the host's too, only its game drops them. The host decides the Chaos events,
  which reach everyone as they start and end.
- **Item Randomizer**: members play the host's seed, so every game has the same placement. An item
  passed on from another player is that item, and uses up the place it was found in. A blank seed on a member is
  never used.

## Removing a player

Only the host removes a player, and there is no vote: at two to four players a vote is noise. The
one removed goes back to the Multiplayer page, reading `The host removed you from the party`, from
the lobby or from the middle of a game, and the party plays on. It can come back with the code, but
the server's own match finding does not put it in that party again.

A ban is of the party's co-op save: `Ban from this save` keeps the player out of every game of that
save, whoever hosts it, since the ban goes beside the save and to the members' copies of it. It
holds the launcher's key, not a name or an address, so a new name does not get round it. Party rules
lifts a save's bans. A new game has none. A game started without the launcher has no key and can
only be removed.

A server from before kicks takes no such request, so the host's game does not send it: the removed
player's game leaves by itself. Direct connect's host does the same as the server.

## Two discs in one party

Players on different discs of the same game play together: nothing is said about it, and the party
behaves as one. The port ticks both discs at 60 Hz, so no time needs converting, and the keys,
classes, items, scene numbers and saves mean the same on both. What differs follows who runs it:

- Enemies, doors, placed behaviour: the host's game, so the host's disc. Party rules say `Enemies
  follow the host's game` when the discs differ.
- Each Ratchet, his gadgets and his collision: that player's own disc.
- Text, speech and language: each player's own.
- Progress and the save: the host's. A save crosses as it is; both discs write one layout, and it is
  checked as untrusted bytes before it loads (`src/save_check.cpp`: size, checksums, every block's
  id and size, the values the game indexes with). A save that does not pass is refused with `You
  can't join: the host's save can't be used`, or `That save can't be used: it may be damaged` to the
  host choosing it.

The check compares the disc's world family (`openrac_api.world_family`), not the disc: `mods_digest`,
`compare_mods` and PLAY's build all follow it, and a port that says none falls back on the disc. As
each level loads, HELLO carries the level's sim digest (`openrac_api.sim_digest`), which a member
and the host must agree on: a number over the level's moby records, leaving out what each game runs
by itself. On 2026-10-31 all 19 levels agreed on the two discs (OpenRAC's
`port/tools/versionmap/sim_digest.py`); Hoven's six spawn point records differ in their update
distance alone, which is left out. A disc that did not agree would leave the party at that level
with `You have a different disc from the host`.

## Direct connect

Playing without a server: one player's game hosts, and the others join it by its address. The
default stays the server; Direct connect is for a LAN, or for friends who can reach each other.

Home, **Direct connect**:

- **Host directly**: your game listens on port **7770** (TCP, and UDP on the same port), and you are
  the host of its one party. The lobby's title shows your address and port to give the others
  (`Hosting on 192.168.1.20:7770`; more lines when the computer has more than one address). A port
  already taken (another host, or a server on the same computer) is refused: the page stays, and
  its footer says "Port 7770 is in use". **Leave** asks "Stop hosting?" and ends the party.
- **Join by address**: set the host's address with the rows (four parts and the port), then
  **Join**. You are in the host's party as soon as you are connected: there is no code to type.
  While it cannot reach the host, the footer says so and it keeps trying; Triangle gives up.

The game has no text entry, so the address is stepped in with Left and Right. Leaving a direct
party (or the host ending it) goes back to Home and to your server, so Browse games, Create match and
Join by code work again.

On a LAN this works as it is: give the others the address the lobby shows. Over the internet the
host has to forward the port (**TCP and UDP 7770**) on their router to their computer, and give out
their public address (the lobby shows only the LAN ones). Addresses are visible: everyone in the
party sees the host's address, and the host sees theirs. When the host leaves or stops hosting,
the party is over for everyone.

The host's game runs a small copy of the server's party routing (`src/host.cpp`), so it speaks the
same packets and datagrams, and the lobby and the game data behave the same in both modes. Its
differences from the server: one party, whose code is `DIRECT` (a lobby shows the host's address
instead); a client is put in it right after its hello (the party's INFO comes unasked), and
Create match or Join by code (any code) put a client back in after it left; the host's own
game is linked in-process, not by a socket. It keeps the server's limits: frames of 64 KiB, names
of 1 to 32 characters and chat of 1 to 256, a party of 4, the same rates per connection with
strikes, hello within 10 seconds, 8 connections an address (and 8 clients at once).

## Party saves

Everyone in a party, the host too, plays in a co-op slot, a folder of its own beside the game's on
the memory card: the party's saves never touch a player's own. The party starts from a copy of the
save chosen, the first moment of a new game for a new one. The lobby says so, the pause menu's
Multiplayer page shows which slot it is, and **Party saved in Co-op N** shows over the game at each
save.

| When | What happens |
|---|---|
| The game saves (a gold bolt, a story moment, leaving by ship, the pause menu's Save) | it lands in that player's co-op slot. The host's also goes to every member's |
| **Save the party** (pause menu, Multiplayer) | the host saves now and it reaches everyone; a member's press asks the host |
| A player leaves, or the party is lost | a last save of what that game holds. Until it is back at the title it keeps saving to the co-op slot |
| **Continue as host** (Saves page) | any co-op slot starts a new party from it, on the server or directly. The same party comes back to the same slots |

A save is written whole or not at all: a crash or a dropped connection in the middle leaves the last
one, and the one before stays beside it. A late joiner gets its copy without a word. The launcher
lists co-op saves on their own, under Saves.

## How it works

The mod needs OpenRAC's mod interface 1.12: 1.7's pages of the game's own menus put the
**Multiplayer** item in the main menu under Load Game and let the mod say what its pages list
(the port draws them with the game's widgets), and 1.8's play calls tell it what the game is doing,
give it the hero's pose, make the figures that stand for the others (mobys of the hero's class
that run none of his logic, collide with nothing and are no target), start a game or a save from
the title, travel to a level, and read the memory card's saves; 1.9's world calls list the level's
mobys, drive, pose, kill and make them, gate each moby's update (with the hero standing in for
another player), watch and post hits, and keep mobys near other players running. It keeps one TCP connection to the server
(TCP_NODELAY), opened the first time Home shows and kept until the game ends, on a thread of its own: the game's thread only queues frames
and reads what came. The server is the OpenRAC server (the `Server` repository): it links players
into parties and forwards their traffic, and the host's game is to be the authoritative one.

The wire format is the server's own: every packet is a frame, a 4-byte big-endian length, the
packet id, an opcode for PARTY, then the payload, big-endian, strings behind a 16-bit length (-1
for an empty one). The mod speaks all of it:

| Packet | The mod sends | The mod reads |
|---|---|---|
| 0 HELLO | its version (`rac1-multiplayer/0.10.0`) and the player's name, on every connection | the session id |
| 1 PARTY | CREATE (0) with the party's description (the save's words, then `|` and four hex digits of the host's mods' digest), JOIN code (1), LEAVE (2), LIST (8), HOST_CHANGED (9: who may follow the host, or a new host that cannot), RESUME (10: back in a kept place), KICK (11); FIND (7) is still understood | INFO (3, with the description), JOINED (4), LEFT (5), END (6), LIST (8: per party its code, host's name, members, size and description), HOST_CHANGED (9), RESUME (10: a member gone or back), KICKED (12) |
| 2 CHAT | (nothing yet) | sender and message (not shown yet) |
| 3 DATA | the lobby's Ready and Start, below | the same, and anything else for the game data to come |
| 4 ERROR | | the reason: no such party, full, already in one, not in one, not the host, no place to come back to |
| 5 TIME | its monotonic clock in ms, once after hello | the echo and the server's clock: the round trip, and the server's clock offset, smoothed (5 % a sample) |
| 6 UDP | | the session's UDP token and the server's UDP port (an older server sends none), then what the server takes: KICK, RESUME and a resume token (an older server says none, and the mod asks for neither) |

**UDP**, for game state only (`Session::send_state`), when the server offers it: the connection
binds a UDP socket to the server's address and that port, sends `BIND` (session id, token, kind 0)
every 3 seconds to keep a NAT mapping open, and sends state as `STATE` datagrams (kind 1, the
target, the bytes; at most 1,200 bytes a datagram, larger state goes as DATA). The server's STATE
datagrams (kind 1, the sender, the bytes) arrive as the DATA they stand for. Until datagrams come
back, state also goes as DATA, so blocked UDP loses nothing (state may then arrive twice, so it is
stamped and the latest wins); once they come back within 15 seconds, UDP only. Lobby messages and
game events always go as DATA.

**The server's rates.** The server takes 60 DATA and 60 state datagrams a second from a session,
over a burst of 120. Over that, state is dropped without a word; DATA is dropped and counted against
the connection, which enough drops close. So nothing leaves the mod the server would drop
(`src/session.hpp`): events queue, in order, and go as the session's bucket (50 a second, a burst of
100) allows, those for one target side by side as one DATA (BATCH), so a burst comes late, never
lost; state goes by its own bucket (52 a second, a burst of 60), and one it refuses is not taken as
sent. The state itself stays within that: the host's Ratchet and the members' go out together as one
POSES datagram, 20 a second however many players there are, and each member's WORLD has its share of
the 30 a second left (15 each for one or two members, 10 each for three). Shots go as state too, one
SHOTX of any number of them each 50 ms from a member and each 100 ms from the host, and only while
someone fires: a held trigger's track is ten shots a second, and a queue held back keeps only each
track's newest. While the party fires, the host's WORLD takes 22 a second, 7 each for three members. State as DATA, while UDP is
unconfirmed or blocked, takes only what events leave of the DATA bucket, so a blocked UDP slows the
world and never closes the connection.

**WORLD and lost datagrams.** The host keeps count of what each member has. Each WORLD has a
sequence, and each pose a member sends says which of the last 33 it heard (the host keeps that to
itself when it passes the pose on). A moby goes while it differs from what that member is known to
have, a change that was delivered costing one entry; a datagram the member did not hear (two newer
ones heard, or twice the round trip gone by) puts what it carried back in line, and a moby that
stands goes at the next snapshot. Nothing repeats blindly and nothing waits for a refresh, though a
moby that stands goes once more every 8 seconds for whatever the host cannot see, and with no word
back at all each one goes again each time-out. A late datagram changes nothing about a moby a newer
one spoke of.

**What goes first.** Each snapshot adds to each moby waiting, per member, its weight for that member,
and it goes once that reaches 1, the highest first, then starts again. The weight is distance first
(1 within 30 units, falling with the square of the distance to a floor of about a quarter), more
for a creature, which can hurt, for what the player faces, for what it hit in the last 2 seconds and
for a bolt on its way, less for what nothing draws. So a close moby goes each snapshot and a far one 4
times a second, and with more moving than a datagram holds a far moby waits but never starves: 300
moving by a member leave one far moby unheard for 4 seconds at the most (the rank this replaced made
that 9). A moby that hid or showed goes at the next snapshot. WORLD's share falls from 30 to 22
a second while SHOTX goes, so POSES and shots lose nothing to it.

The lobby and the game are the mod's own, carried in DATA (the server only forwards it; a member's
DATA reaches the host only, the host's goes to everyone or to one), a kind byte first:

| Kind | From | Bytes after the kind | |
|---|---|---|---|
| 1 READY | a member | flag u8 | ready, or no longer |
| 2 STATE | the host | count u8, then id i32 + flag u8 each | who is ready |
| 3 START | the host | | the lobby's Start |
| 4 PLAY | the host, to all or one | play i32, mode u8 (0 new game, 1 save), level i32 (-1: the save's), save size i32, build string, joining u8 | play this game; a save follows in CHUNKs. `play` names the host's game. A member whose build (the mod's version, OpenRAC's, the disc's world family, the gameplay mods' digest) differs refuses it: "You can't join: your game differs from the host's". `joining`: the game is on and this member comes into it |
| 5 CHUNK | the host | play i32, offset i32, up to 16,000 bytes | a piece of PLAY's save (a Ratchet & Clank save is 59,912 bytes: four pieces) |
| 6 LEVEL | the host, to all | play i32, level i32, scene u8, travel u16, dest i32, arriving u8 | the level the host plays (-1 none) and whether it plays a scene, once a second and at each change; its last ship travel (a count and the planet) and whether the party still arrives there. A member in another level travels there, a member in a scene the host is out of skips it, a member that has not left for the host's newest travel leaves at once (a LEVEL without the travel fields, an older mod, travels as before) |
| 7 POSE | a member, as state; the host's own with one member in the party | owner i32, sequence u16, stamp i64 (the server's clock, ms), level i8, flags u8 (1 blending, 2 teleport, 4 in a scene), position, rotation and velocity (3 x f32 each), state, health and item (i16 each), seq, frame and next frame (u8 each), anim time u8 (/255), anim speed i16 (/1000); then what it wears and holds: count u8, per piece slot u8, part u8, item u8, class i16, joint u8, flags u8 (1 hidden, 2 blending, 4 loose), seq, frame, next frame, anim time (u8 each), anim speed i16, and for a loose piece its position and rotation (3 x f32 each) | a player's Ratchet, 20 a second (65 bytes, then 13 a piece); a member's goes to the host, who passes it on in POSES. A pose that ends after 65 bytes (an older mod) wears nothing |
| 8 WANT | a member | play i32 | in the title without the host's game: asks for it again |
| 9 WORLD | the host to a member, as state | level i8, stamp i64, sequence u16, count u8, then per moby: id u16 (its key; 0x8000 \| a net id for one spawned), position 3 x f32, rotation 3 x i16 (1/10,000 rad), seq, frame, anim time and speed (u8 each), flags u8 (1: hidden; 2: loose, a bolt landed in the host's game, which the member's game now runs as its own) | the 46 mobys that member most needs of those it does not have yet, 7 to 15 times a second (WORLD and lost datagrams, What goes first, above; a landed bolt until it is heard) |
| 10 KILLED | the host | level i8, count u16, keys u16... | placed mobys gone in the host's game |
| 11 SPAWN, 12 DESPAWN | the host, to all | level i8, net u16, class i16, position, yaw; or a list of nets | creatures and bolts the host's game made, and their end |
| 13 HIT | a member | id u16, mask u32, damage f32, position, velocity | its hit on the host's moby |
| 14 DAMAGE | the host | attacker id u16, mask u32, damage f32, position, velocity | an enemy hit that member |
| 15 BOLTS | the host | count i32 | bolts that are that member's: a claim granted (the bolt's value), or what flew to its figure in the host's game |
| 16 DEAD | the host | level i8, records u16, a bit each | the placed mobys dead for the party in the host's level, for a member that said HELLO |
| 17 BROKE | a member; the host, to all | level i8, key u16; a member's has back u8, 1 for a crate that comes back | it broke a crate or took a pickup (not a bolt: CLAIM); from the host, a crate that comes back broke there |
| 18 SKIP | the host, to all | origin i32, sequence u16, play i32, what u8 (0 a scene of the game's own, 1 the host's scene, 2 a movie), scene u16 (the host's scene by SCENE's sequence, else 0) | the host's Start pressed in a scene or a movie: each game skips that one only, while it plays (a member's SKIP, from an older mod, the host ignores); a skip of an earlier scene of the host's, of another play, or without these fields (an older mod) skips nothing |
| 19 SHOT | everyone | owner i32, level i8, item i16, position, velocity (3 x f32 each), aim 4 x f32 | a shot as interface 1.9 has it. No longer sent; still read |
| 20 SCENE | the host, to all | play i32, level i32, scene i32, sequence u16, elapsed i32 (ms) | an in-level scene the host's game plays (the level's number of it; a new sequence each start), when it starts, once a second while it plays and when it ends (scene -1). A member in that level starts the same scene once it has control (the SDK's `play_scene`) |
| 21 SCENE_STATE | a member to the host | play i32, sequence u16, state u8 (0 still to play, 1 playing, 2 out) | where a member is with the host's scene of that sequence, at each change and once a second; the host waits for those of its newest scene only |
| 22 CLAIM | a member | level i8, id u16 (a placed bolt's key; 0x8000 \| a net id for one the host dropped), what u8 (absent: a bolt; 1: a pile of buried bolts by key) | its Ratchet collected that bolt (its game added nothing): the host's first claim of a bolt still there gets BOLTS and the bolt's end everywhere (DESPAWN or KILLED); any other, nothing |
| 36 SHARED | a member to the host; the host to all | u8 count, then per section u8 tag, u16 length, its bits (tags: 1 gold weapons, 40; 2 skill points, 32; 3 flags, 320: level x 16 + flag; 4 special items, 128; a tag not known is skipped) | as PROGRESS: at each change, every 5 s and at each member that comes. Silent, upward only; a game is not given back what it once had and lost |
| 37 MISSION | the host, to all or one | level u8, 16 bytes | the row of flags of the host's level, at once at a change; of a level a member arrived in, when it asks |
| 38 SHARED_ASK | a member to the host | level u8 | it arrived in that level |
| 23 PROGRESS | a member to the host; the host to all | items u8 count + a byte each, gold bolts u8 count + a byte each (level x 4 + its number), planets u8 count + a byte each (non-zero had) | a member: what its game has, at each change and every 5 s; the host: what the party has (its own game's, given what the members have), likewise and at each member that comes. Each game is given what it lacks; nothing is taken |
| 25 POSES | the host, to all, as state | count u8, then length u16 + a POSE each | the host's Ratchet and the members' newest, every 50 ms; a game skips its own |
| 26 HELLO | a member | level i8, load u32, sim digest u32 (0 not known) | its game has new mobys of that level (it arrived, or a death made them again): asked each second until SYNCED. A digest other than the host's gets RESET with differs set, not the world |
| 27 RESET | the host, to all | level i8, how u8 (absent: 0; 1 differs, 2 a full wipe) | its level was made again: what it spawned is gone everywhere, every member says HELLO. To one member with differs 1: its level does not agree with the host's, and it leaves the party |
| 28 SYNCED | the host | level i8, load u32, rules u8 (1 revive) | all of the world came, ahead of this on the stream: DEAD, then a SPAWN for each spawned moby; WORLD follows |
| 29 MODS | a member; the host to that member | multiplayer's version, OpenRAC's and the disc (strings), count u8, per mod id, name, version (strings), kind u8 (0 gameplay, 1 input, 2 cosmetic, 3 solo), settings u8, per setting key, name, value, shown (strings), kind u8 (0 shared, 1 own, 2 host), default u8, then the world family (string), the mod code (string), the sender's launcher key (string, "" from the host), count u8, per data pack id, name, version (strings), digest i32 | a member's game and mods, every 2 seconds until the host's come back (a player's own mods without their settings) |
| 30 FITS | the host, to all | count u8, per member id i32, fit u8 (0 unknown, 1 they match, 2 they differ), why (string) | what the host found, at each change; one silent for 6 seconds is an older game |
| 31 RULES | the host, to a member or all | count u8, per mod id (string), length u16 + its bytes, its words (string) | the rules of the host's mods, to a member found matching, on the stream ahead of PLAY, and to all at each change (a Chaos event) |
| 32 COSAVE | the host, to all | tag u8, total i32, index i32, bytes | a piece of the host's save, for each member's co-op slot (cosave.hpp) |
| 33 COSAVE_ACK | a member | tag u8, ok u8 | that save is in its slot |
| 34 SAVE_NOW | a member | | asks the host to save the party |
| 35 COPARTY | the host, before PLAY, and to all when the bans change | key i32, count u8, each ban (string) | the party a slot belongs to, and who is banned from it |
| 40 SUCCESSOR | the host, to all | count u8, ids i32... | who follows it, in order; the same list goes to the server (HOST_CHANGED) |
| 41 LEDGER | the host, to all, at each change; a member, to a new host that asks | version u8, epoch u16, generation i32, play i32, travel u16, scene u16, level i8, revive u8, next net u16, the dead, dug and spared keys (each count u16 + keys u16), count u16 + each spawned moby (net u16, class i16, position, yaw f32) | what every member keeps of the host's world, whole; only a newer one by epoch and generation stands (`src/migrate.hpp`) |
| 42 LEDGER_ASK | a new host, to all | | each member's ledger, in case it is newer |
| 43 EPOCH | a host after the first, all it sends | epoch u16, then the message | anything of an older epoch is refused; the first host stamps nothing |
| 44 PLACE | the host, to a member who asked | play i32, ticket u16, answer u8 (0 not now, 1 beside here, 2 the host's last safe spot, 3 none), position 3 x f32, yaw f32 | where a member goes: the host's last safe pose. Only the newest asking's ticket counts |
| 45 PLACE_ASK | a member, twice a second | play i32, ticket u16, last u8 | it is to be put beside the host (a late joiner, or Go to host); `last`: 10 seconds of waiting are up, the last safe spot will do |
| 46 WAIT_VOTE | a member; the host, to all | play i32, wait u16; from the host then kind u8 (0 none, 1 an arrival, 2 a scene), votes u8, voters u8, passed u8 | a vote to go on without the players waited for; the host's word of the wait, at each change and once a second |
| 48 SHOTX | everyone, as state | count u8, then per shot owner i32, level i8, item u8, flags u8, what follows u8, stamp u32 (the low bits of the server's clock), position 3 x f32, and what it has of velocity 3 x f32, aim 4 x f32, velocity2 3 x f32, aux 4 x f32, target key i16 | shots a player fired; a member's go to the host, who passes them on. Each is fired when its figure is where the shooter was, hitting nothing in that game, or dropped when 250 ms late. A flag makes it part of a track (a flame, a beam, a Visibomb in flight): START, a MOVE ten times a second, STOP; aux[0] tells the player's tracks apart |
| 52 CAMSCRIPT | the host, to all | play i32, level i32, sequence u16, id i16 (the class of the moby running it), started u8; started: where the moby is, the first eye, the first rotation (3 x f32 each) | a camera script of the level's that others may be shown, when it starts and ends (`camscripts.hpp`) |
| 53 CAMSTATE | the host, as state | sequence u16, stamp i64 (the server's clock, ms), eye and rotation (3 x f32 each) | what that script's camera shows, 20 a second, played 100 ms back; an older one is dropped |
| 64 LINK | a member | ping i32 (ms, -1 not known), udp u8 | its link to the server, every 2 seconds |
| 65 LINKS | the host, to all | count u8, then id i32 + ping i32 + udp u8 each | every member's link (the host's own among them), for the pause menu's Multiplayer page |
| 66 BATCH | anyone | length u16 + a message, each | messages of the game for one target in one DATA, read as if each came alone |
| 67 KICK | the host, to the member removed | reason u8 (0 removed, 1 banned) | why, ahead of the server's KICKED; with a server that takes no KICK, the member leaves by itself |
| 71 REVIVE | the host, to a player down | how u8 (0 up where it lies, 1 to its checkpoint) | the revive rule's word |
| 72 RESTORE | the host | level i8, key u16, class i16, position, yaw i16 | a placed moby back in its level; a game whose copy is gone makes one |

A member's game is started from the title with the host's save (`play_start`), its own saves left
alone; one who joins while the game is on gets the host's progress as it is (`save_capture`),
starts in the host's level and has the world from the host once it says it is there (HELLO). Each game shows the others' poses through a buffer per player
(`src/play.hpp`, after INTERPOLATION.md): played back about 100 ms behind (more with jitter, and
never less than two of that stream's send intervals, so one lost pose still leaves one to play),
the position along a curve through each pose's velocity, the cursor running up to 10 % fast or slow
to keep its delay, 150 ms of extrapolation past the newest pose, and a jump for a teleport (a level
change, a respawn, a step longer than 8 units). A figure is made when a player is in the same level
and removed when it leaves the level, dies, goes silent for 5 seconds or leaves the party.

The shared world (`src/world.hpp`) stands on interface 1.9's world calls. A placed moby is named by
its key, its record in the level's file, the same in every game whatever was killed before. A member
in the host's level drives every placed moby that has an update of its own (its game no longer
runs it; its animation still plays and its collision follows it) and poses it from the host's
WORLD snapshots through a buffer per moby. In the host's game the gate around each moby's update
has the nearest player stand in for the hero (his position and his moby, the figure's), so the
game's own enemy code chases and hits that player, and the bolts collected during it are moved to
that player. Mobys near the members run in the host's game as if near its camera (eyes).

## Known limits

- A new host's enemies start their minds afresh where they stood, and a placed moby only a game that
  never heard it died still has is dealt with by the ledger; one the ledger has alive and this game
  lost stays gone. Enemies mid-attack stop, a scene or a flight ends, an unanswered bolt claim or dig
  is dropped.
- A place in the party lasts as long as the game does: a game closed and started again joins by the
  party's code, with its old place taken until the 2 minutes are up.
- The host's save is where the party starts. Bolts, ammo, health and what a member spends are
  each player's own; the items, gold bolts, planets, gold weapons, skill points and story flags are
  shared (above). A co-op slot the
  host's last save reached holds the host's bolts, ammo and nanotech, so a member who hosts from it
  takes those on.
- Five co-op slots: a sixth party takes the slot saved longest ago (the save before stays beside
  it, as `.bak`, until that slot saves again).
- The game's own Save page in a party still lists the player's own slots; whichever is picked, the
  save goes to the party's slot, and the line over the game says so.
- What a person last said to a player stays each game's (the record behind a person's first line and
  some of the pause map's marks, which a taxi also writes): a member may hear someone's first line
  again, or see a mark the host no longer has. It changes nothing in the story.
- A gold weapon a case still offers when it arrives can be bought again until the level is next
  loaded (the case reads who owns it only when the level starts).
- Vendors and the ship are each game's own. A member's flight to the new planet is the game's
  flight between planets, not the host's take-off from the pad (members leave when the host's
  ship does, and usually land first, then wait).
- A scene that started before a member came is not shown to it (a scene is read from the disc as
  it plays: it cannot start part way).
- A camera shot that is not a scene plays in full only for those it was shown to from its start: a
  player who comes near later, or who was busy, plays on and sees none of it. A seat's, a ride's, a
  machine's and a gold bolt's stay their player's, as do the shots that open a fight.
- A crate a member broke drops its bolts a round trip later (the host's copy breaks when the
  member's word gets there); one the host's copy has not broken within 3 seconds is deleted
  without a drop.
- A landed bolt a member collects leaves its game at once, and its count comes a round trip later
  (BOLTS); one another player took first in the meantime brings nothing. The member's own collect
  sparkle and counter do not show for it.
- What the host's game runs of an enemy never reaches the host's hero while it goes for a member:
  a state, a kill, health. Two enemies grab in a way the world has to share: the Umbris
  sharkigators and the snagglebeast (see **Grabs** above). A moby update that plays a scripted moment on the
  hero for a member standing near it (a trigger's lock, a camera's hold) does nothing for that
  member's hero: only a kill reaches him.
- A figure holds what its player holds (the wrench, the weapon or gadget, Clank and the packs,
  the boots), each loaded when this game can have it beside its own hero's: one weapon class
  besides the hero's fits at once, and the game has no memory for more. While it cannot be had
  (two other players with two other weapons, or under a menu), the player nearest this game's
  hero keeps it and the other holds the wrench, or nothing when even that cannot be had. Only the
  weapon in the figure's hand is affected: its shots show all the same.
- What each weapon shows, and what it leaves out:
  - The Blaster, Bomb Glove, Devastator, R.Y.N.O., Glove of Doom, Mine Glove and Decoy Glove fire
    through the game's own code, so they look and sound as they do for the shooter, their
    effects included. A shot homes on what the shooter aimed at when that is one of the level's
    placed mobys; on a spawned one it flies on its heading. Gold weapons are as this game's player
    has them.
  - The Pyrocitor's flame, the Tesla Claw's and Morph-O-Ray's beams and the Suck Cannon's pull are
    drawn from the figure's hand as particles: no sound and no light; the flame goes through walls,
    and the beams are sparks along a line, not the game's arcs.
  - The Visibomb in flight and the drones are their models where the shooter's are, without their
    sound.
  - The Walloper's swing is the figure's hand; the Taunter shows only the figure's hand.
- A shot that would come more than 250 ms late is dropped rather than shown late, so on a bad
  link some are missing. A decoy a member throws draws the enemies through the host's copy; if it
  is destroyed there the member's copy stays until it runs out.
- A name tag shows a player nearby and in plain view: it fades out from 18 metres and is gone at 25,
  and it goes when the level's walls or ground stand between the camera and the figure's head and
  middle (the game's collision, asked about 8 times a second a player, held 0.3 s so a corner does
  not blink it). A door, a crate or any other moby does not hide it, since the line leaves those out.
  It shows in a level only: not under a menu, not in a scene. The font is the port's capitals, and a
  character it lacks is left blank.
- Particles under the pause menu are drawn but stand still (frozen sparks).
- Everyone needs the same OpenRAC and mod versions (Mods in a party). Discs of one world family play
  together: each player's Ratchet plays by that player's disc, and the enemies, doors and everything
  else the host's game runs by the host's. Tested by unit tests; see Two discs in one party.
- A host's death brings back the enemies its Difficulty copied, as a death does alone: they are made
  again with the level, and the members get them from the host. Only placed mobys stay dead for the
  party.
- A ban holds a launcher's key, which is the player's own word: a player who changes the key by hand
  gets past it. A game started without the launcher has no key, and can only be removed.

## Building

Built with the repository's other native mods (the top README), with an SDK of interface 1.12 or
newer: until that SDK is released (`sdk.lock` still names 1.11.0), build against OpenRAC's own `sdk/`:

```sh
cmake -S . -B build -G Ninja -DOPENRAC_SDK=<OpenRAC>/sdk
cmake --build build && ctest --test-dir build -R multiplayer
python3 <OpenRAC>/sdk/tools/package_mod.py build/package/multiplayer -o dist
```

`tests/play_test.cpp` runs the per-area files (`play_*_test.cpp`, sharing `test_support.hpp`); together they check the poses' bytes, the interpolation buffer (a steady stream, repeats
and late poses, teleports, a stall then a burst, extrapolation), and the co-op of a host and members
through a router that does what the server does with DATA and fake games: Start with a save and
with a new game, the level followed, a late joiner and a member that asks again, figures made in
the same level only and gone with the level, the silence or the party; what a player holds on its
figure and its shots fired there (a delay behind, in its level only); a build refused and the
title after the party's end; and the world: WORLD's bytes, the member's drives (not the crates,
not the static mobys), the host's spawned bolt, an enemy followed, the stand-in and its bolts, a
member's hit on the host's enemy, an enemy's hit on a member, a crate broken by a member, and a
kill and a pickup on the host; and the bolts: the host's crate's and a member's crate's drops made
once, by the host, two claims of one bolt (the first credited), the host's own pickup, and the
placed bolts; and the progress: a member's gold bolt and the host's, gone and counted in every game
with no scene shared or held, an item and a planet reaching every game (once in control), a late
member given it all.

The last part (`tests/play_sim_test.cpp`) plays the party on a seeded network and a model of the
server's limits (`SimNet`): every mix of 0, 1, 5 and 15 % loss and 30, 100 and 250 ms, with
reordering and repeats, with no moby left anywhere else or hidden differently, no crate left
different and nothing dropped by the server; bursts, UDP blocked; ids through a respawn that moves
the slots; a host's death; a late member's world and its passing what plays on its way.

One run of each mix proves little: a loss that bites one run in thirty passes it by luck.
`tests/sim_sweep_test.cpp` (`multiplayer_sweep`) plays those mixes for 2, 3 and 4 players over many
seeds, with a level where everything moves, one that mostly stands and a party firing, and says when
the world came right, the longest a moving moby went unheard and what went out by kind. The test
runs a few seeds a mix; `-DMULTIPLAYER_HEAVY_SWEEP=ON` adds the sweep a change of the sync is judged
by, or run it by hand (`--seeds`, `--base`, `--shard i/n`, `--csv`). `tests/play_sync_test.cpp` has
WORLD's delivery, one chosen datagram at a time: a change delivered goes once, a lost one goes again,
at once or by the time-out; a word from before a load says nothing; what was not taken is not
said heard; the priority, the shots' share and each stream's margin.

It also checks the mods a party agrees on: a list's bytes and digest, every kind of difference and
its words, each compared setting refusing and each exempt one not, the lines fitting a page, a
differing member refused its Ready and the game (with the gates off, stopped by the digest at PLAY),
a differing late member, the rules reaching a member before any level and again at an event, a
respawn, the party's end, and an older game on either side.

`tests/e2e` does the same with the real game: see its README.

`tests/multiplayer_test.cpp` checks the frames byte for byte against the server's encoding, the
lobby's logic against a recorder, the connection against a small server on the loopback, and
Direct connect: a host and two clients on the loopback (the party, chat, DATA both ways, UDP state,
leaving, the host stopping) and the server's limits held by the host; and the pages' own logic:
the address rows stepping around their ends and faster when held, the hosting lines, and the name
chosen from the setting and the launcher's.

| File | |
|---|---|
| `src/multiplayer.cpp` | the mod: its entry, hooks and the glue that ties the session, the connection and the pages together |
| `src/mod_state.hpp` | the mod's shared state and the functions its files call each other by (mod-internal) |
| `src/pages_home.cpp`, `pages_browse.cpp`, `pages_saves.cpp`, `pages_lobby.cpp`, `pages_refused.cpp`, `pages_rules.cpp`, `pages_remove.cpp` | the pages in the game's own menus: Home with Code, Direct connect and the pause menu's page; Browse; Saves; Lobby; Refused; Rules |
| `src/game_adapter.hpp`, `.cpp` | the co-op's Game on the SDK's play and world calls |
| `src/kinds.hpp` | the wire registry: every DATA kind, who owns it, the ranges reserved for coming features, and the router that sends a received DATA to its owner (`tests/kinds_test.cpp` holds the table to it) |
| `src/session.hpp`, `.cpp` | the session and the party: hello, the clock, create, list, join, leave, Ready and Start |
| `src/coop.hpp`, `.cpp` | playing together: PLAY and its save, LEVEL, WANT, poses sent and figures shown |
| `src/play.hpp`, `.cpp` | the poses' bytes and the interpolation buffer |
| `src/party_mods.hpp`, `.cpp` | the mods a party agrees on: the lists, their comparison and digest, the words for a player, the host's rules carried to the members |
| `src/world.hpp`, `.cpp` | the shared world: snapshots, kills, spawns, hits, damage, bolts, stand-ins |
| `src/progress.hpp`, `.cpp` | the shared progress: items, gold bolts, planets |
| `src/shared.hpp`, `.cpp` | the shared progress past those: gold weapons, skill points, the story's flags, special items, and the scene flags kept out |
| `src/camscripts.hpp`, `.cpp` | camera scripts shown to the players near: who sees which, and what takes the camera back |
| `src/trace.hpp`, `.cpp`, `src/world_trace.cpp`, `src/despawn.hpp`, `.cpp` | the verbose log: its lines and their cap; the world's pause, run, vis, drive and census lines; the despawn lines, who removed a moby and why |
| `src/connection.hpp`, `.cpp` | the socket on its thread, reconnecting, UDP; the link to a server, a direct host or the embedded one |
| `src/role.hpp` | this game's part in its party: who runs the world, one value a change of host swaps |
| `src/migrate.hpp`, `.cpp` | a connection lost and found, a dropped host's wait, the ledger, the takeover, and what becomes of every piece of state in it |
| `src/host.hpp`, `.cpp` | Direct connect's embedded host: the server's party routing for one party |
| `src/direct.hpp`, `.cpp` | Direct connect's page logic: the address's rows (64-choice rings), held steps, the hosting lines |
| `src/bot.hpp`, `.cpp` | the end-to-end tests' driver of the real game (only with `-DMULTIPLAYER_BOT=ON`, never in a release) |
| `src/sockets.hpp` | the socket calls the connection and the host share (Winsock and POSIX) |
| `src/protocol.hpp`, `.cpp` | the server's frames, packets and datagrams, both sides |
