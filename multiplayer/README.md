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

## Playing

1. Install the mod from the launcher's **Mods** tab, switch it on, and set **Server** to the
   OpenRAC server you play on (`localhost:7770` by default: a server on your own computer).
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
| Browse games | **Refresh**, then a row for each game being hosted that has room: its host's name, its save ("Pokitaru, 12 gold bolts", or "New game") and how many are in it (`2/4`). Cross on a row joins it. The list is asked again every 3 seconds while the page shows; with none, a line says so and points at Create match |
| Choose a save | **New game**, then your memory card's five saves, each with its planet, gold bolts and time played (an empty slot is dim). The one chosen is what the party plays; your card is never written by the party. Create match and Host directly both come here first |
| Join by code | A row per character of the party's code: Left and Right (or Cross) step it, Up and Down move between them; **Join** joins. Codes use the letters and digits that do not read alike (no 0, O, 1 or I). Once in the party, the lobby takes this page's place |
| Direct connect | **Host directly**: your game hosts (below); the lobby opens. **Join by address**: the address's rows take this page's place: **Address part 1** to **4** (each 0 to 255), **Port** (by 1) and **Port, by 100**, all stepped with Left and Right (held, they repeat, and after a moment step ten at a time), then **Join**; the title shows the address as it stands (`Join 192.168.1.1:7770` at first; the last one joined is kept until the game ends). Triangle goes back from the rows to the two buttons (giving up a join still connecting), then to Home |
| Lobby | The party's code in the title (a direct party: **Hosting on** your address and port, every address of yours that fits, or **Party at** the host's address), **Ready** (a member: it is ready, or no longer; once the game is on, **Join the game**) or **Start** (the host: the game starts for everyone), **Leave**, what the party plays (**Playing:** the save), then the members (up to 4) with the host and the ready ones marked, and the round trip to the server. **Leave** and Triangle ask first (**Stop hosting?** for a direct host); when the host leaves, the party is over for everyone. Leaving a direct party goes back to Home and to the server |
| Pause menu (in a level) | **Options**, then **Multiplayer** (after Video): the party's code (a direct party: where it is), the host's level, each member with the host and you marked and its round trip to the server and whether its game state goes by UDP or TCP (updated every 2 seconds), then **Leave party**, which asks first (**End the party?** for the host, **Stop hosting?** for a direct host). Out of a party it says **Not in a party**. A member that leaves in the host's game goes back to the title once the menu is closed |

## Playing together

1. The host: **Create match** (or **Direct connect**, **Host directly**), then a save or **New game**.
2. The others: **Browse games** and Cross on the host's game (or **Join by code**, or **Join by
   address** for a direct one). Each says **Ready**.
3. The host: **Start**. Every game leaves the menu and loads the host's game in the host's level:
   a new game starts on Veldin, a save where it was saved. The others' cards are not touched.
4. In a level each game shows the others' Ratchets where they are, running, jumping and standing
   as they do, about a tenth of a second behind. They walk through you and nothing aims at them.
5. Only the host flies the ship: when it picks another planet in the ship's planet select, every
   member's game leaves its level at that moment, from wherever it is (playing, a scene, the pause
   menu, a vendor), and flies there. Nobody gets control on the new planet, and nothing of the
   host's world shows there, until everyone has arrived ("Waiting for the other players to arrive"
   says why; at most 90 seconds for one that never comes). A member's own pick of another planet
   lands the ship again where it is ("Only the host flies the ship to another planet"). Someone who joins
   while the game is on gets the host's progress as it is then and starts in the host's level;
   someone who quits to the title finds **Join the game** in the lobby in Ready's place, which
   asks the host for its game again.

Each player sees the others' Ratchets holding what they hold: the wrench, the weapon or gadget,
Clank on the back, with their swings and throws, and their Blaster shots and bombs (shown only:
what they hit is told by the shooter's game).

The world is the host's:

- **Enemies, doors, platforms, pickups** run in the host's game; the others' games show them where
  the host's are (about a tenth of a second behind) and do not run them. A member's wrench or
  weapon hitting one hits the host's, so it takes the damage there and dies for everyone.
- **Enemies go for the nearest player**: in the host's game each one sees the player nearest it
  as the hero, so it chases, aims at and hits that player; the hit hurts that player in their own
  game (knocked back as the game does it).
- **Crates** run in every game: a crate broken in one game breaks in all, with its debris and
  sound, and its bolts are there once, for whoever broke it.
- **Pickups** (bolts lying about, gold bolts, ammo, nanotech) run in every game: anyone can collect
  one, and the first to collect it takes it from every game. Ammo and health an enemy drops in the
  host's game are dropped in every game, for each player to collect.
- **Bolts** from the host's enemies fly to the nearest player and are theirs: each player has
  their own bolts.
- A placed enemy or pickup gone in the host's game is gone in everyone's, also for someone who
  arrives in the level later. A moby goes only when a game says it died, never because a snapshot
  is missing: a game that is loading or in a menu changes nothing in the others.
- **Vendors, the ship, people, help desks**: each game's own, for its own player; the level a
  member is in follows the host's, and only the host's ship leaves for another planet.
- **Menus do not pause** while the party plays: the pause menu and the game's dialogs open over a
  world that goes on (enemies, platforms, the other players); your Ratchet waits where he is.
- **Scenes end together**: the host's skip counts (a member's Start does nothing while the host's
  scene plays). When the host is out of its scene, a member still in one is skipped, and nobody
  gets control back until everyone is out (at most 10 seconds for the host; a member waits for
  the host's scene to end). The ship's landing and take-off count, and so do level intros.

When the party ends (the host leaves, or the connection goes) while a member plays the host's
game, that member's game goes back to the title, whose lobby says why.

Cross chooses, Triangle goes back a page (from Home, to the main menu). The footer's first line says
what the connection is doing: connecting, connected, **Server unreachable** (the mod tries again
every few seconds), or **The server went away**, and the server's answers (a code no party has, a
full party, the host leaving). Only Find match, Create match and Join by code need the server (they
are dim until it said hello); **Direct connect**, Host directly and Join by address are always
selectable. Choosing Direct connect stops the server retries, and the Direct pages never show the
server's state; leaving a direct party goes back to Home, which tries the server again.

| Setting | Default | |
|---|---|---|
| Server | `localhost:7770` | The OpenRAC server, as host:port (`[::1]:7770` for an IPv6 address); 7770 when no port is given |
| Name | `${player_name}` (the launcher's Multiplayer name) | Your name in multiplayer, 3 to 16 letters, digits or underscores; empty, `${player_name}` or not valid: the launcher's Multiplayer name |

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
party (or the host ending it) goes back to Home and to your server, so Find match, Create match and
Join by code work again.

On a LAN this works as it is: give the others the address the lobby shows. Over the internet the
host has to forward the port (**TCP and UDP 7770**) on their router to their computer, and give out
their public address (the lobby shows only the LAN ones). Addresses are visible: everyone in the
party sees the host's address, and the host sees theirs. When the host leaves or stops hosting,
the party is over for everyone.

The host's game runs a small copy of the server's party routing (`src/host.cpp`), so it speaks the
same packets and datagrams, and the lobby and the game data behave the same in both modes. Its
differences from the server: one party, whose code is `DIRECT` (a lobby shows the host's address
instead); a client is put in it right after its hello (the party's INFO comes unasked), and Find
match, Create match or Join by code (any code) put a client back in after it left; the host's own
game is linked in-process, not by a socket. It keeps the server's limits: frames of 64 KiB, names
of 1 to 32 characters and chat of 1 to 256, a party of 4, the same rates per connection with
strikes, hello within 10 seconds, 8 connections an address (and 8 clients at once).

## How it works

The mod needs OpenRAC's mod interface 1.9: 1.7's pages of the game's own menus put the
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
| 0 HELLO | its version (`rac1-multiplayer/0.6.0`) and the player's name, on every connection | the session id |
| 1 PARTY | CREATE (0) with the party's description (the save's words), JOIN code (1), LEAVE (2), LIST (8); FIND (7) is still understood | INFO (3, with the description), JOINED (4), LEFT (5), END (6), LIST (8: per party its code, host's name, members, size and description) |
| 2 CHAT | (nothing yet) | sender and message (not shown yet) |
| 3 DATA | the lobby's Ready and Start, below | the same, and anything else for the game data to come |
| 4 ERROR | | the reason: no such party, full, already in one, not in one |
| 5 TIME | its monotonic clock in ms, once after hello | the echo and the server's clock: the round trip, and the server's clock offset, smoothed (5 % a sample) |
| 6 UDP | | the session's UDP token and the server's UDP port (an older server sends none) |

**UDP**, for game state only (`Session::send_state`), when the server offers it: the connection
binds a UDP socket to the server's address and that port, sends `BIND` (session id, token, kind 0)
every 3 seconds to keep a NAT mapping open, and sends state as `STATE` datagrams (kind 1, the
target, the bytes; at most 1,200 bytes a datagram, larger state goes as DATA). The server's STATE
datagrams (kind 1, the sender, the bytes) arrive as the DATA they stand for. Until datagrams come
back, state also goes as DATA, so blocked UDP loses nothing (state may then arrive twice, so it is
stamped and the latest wins); once they come back within 15 seconds, UDP only. Lobby messages and
game events always go as DATA.

The lobby and the game are the mod's own, carried in DATA (the server only forwards it; a member's
DATA reaches the host only, the host's goes to everyone or to one), a kind byte first:

| Kind | From | Bytes after the kind | |
|---|---|---|---|
| 1 READY | a member | flag u8 | ready, or no longer |
| 2 STATE | the host | count u8, then id i32 + flag u8 each | who is ready |
| 3 START | the host | | the lobby's Start |
| 4 PLAY | the host, to all or one | play i32, mode u8 (0 new game, 1 save), level i32 (-1: the save's), save size i32, build string | play this game; a save follows in CHUNKs. `play` names the host's game. A member whose build (the mod's version, OpenRAC's, the disc) differs refuses it: "Incompatible version" |
| 5 CHUNK | the host | play i32, offset i32, up to 16,000 bytes | a piece of PLAY's save (a Ratchet & Clank save is 59,912 bytes: four pieces) |
| 6 LEVEL | the host, to all | play i32, level i32, scene u8, travel u16, dest i32, arriving u8 | the level the host plays (-1 none) and whether it plays a scene, once a second and at each change; its last ship travel (a count and the planet) and whether the party still arrives there. A member in another level travels there, a member in a scene the host is out of skips it, a member that has not left for the host's newest travel leaves at once (a LEVEL without the travel fields, an older mod, travels as before) |
| 7 POSE | everyone, as state | owner i32, sequence u16, stamp i64 (the server's clock, ms), level i8, flags u8 (1 blending, 2 teleport, 4 in a scene), position, rotation and velocity (3 x f32 each), state, health and item (i16 each), seq, frame and next frame (u8 each), anim time u8 (/255), anim speed i16 (/1000); then what it wears and holds: count u8, per piece slot u8, part u8, item u8, class i16, joint u8, flags u8 (1 hidden, 2 blending, 4 loose), seq, frame, next frame, anim time (u8 each), anim speed i16, and for a loose piece its position and rotation (3 x f32 each) | a player's Ratchet, 20 a second (65 bytes, then 13 a piece); a member's goes to the host, who passes it on to everyone. A pose that ends after 65 bytes (an older mod) wears nothing |
| 8 WANT | a member | play i32 | in the title without the host's game: asks for it again |
| 9 WORLD | the host to a member, as state | level i8, stamp i64, count u8, then per moby: id u16 (its key; 0x8000 \| a net id for one spawned), position 3 x f32, rotation 3 x i16 (1/10,000 rad), seq, frame, anim time and speed (u8 each) | the 48 mobys nearest that member that moved or animated, 15 times a second |
| 10 KILLED | the host | level i8, count u16, keys u16... | placed mobys gone in the host's game |
| 11 SPAWN, 12 DESPAWN | the host | level i8, net u16, class i16, position, yaw; or a list of nets | creatures and bolts the host's game made, and their end |
| 13 HIT | a member | id u16, mask u32, damage f32, position, velocity | its hit on the host's moby |
| 14 DAMAGE | the host | attacker id u16, mask u32, damage f32, position, velocity | an enemy hit that member |
| 15 BOLTS | the host | count i32 | bolts that flew to that member in the host's game |
| 16 DEAD | the host | level i8, records u16, a bit each | the placed mobys dead in the host's level, for one arriving there |
| 17 BROKE | a member | level i8, key u16 | it broke a crate or took a pickup |
| 19 SHOT | everyone | owner i32, level i8, item i16, position, velocity (3 x f32 each), aim 4 x f32 | a shot a player fired (the Blaster's, a bomb); a member's goes to the host, who passes it on. Its figure fires it as far behind as its poses play, hitting nothing in that game |
| 64 LINK | a member | ping i32 (ms, -1 not known), udp u8 | its link to the server, every 2 seconds |
| 65 LINKS | the host, to all | count u8, then id i32 + ping i32 + udp u8 each | every member's link (the host's own among them), for the pause menu's Multiplayer page |

A member's game is started from the title with the host's save (`play_start`), its own card left
alone; one who joins while the game is on gets the host's progress as it is (`save_capture`) and
starts in the host's level. Each game shows the others' poses through a buffer per player
(`src/play.hpp`, after INTERPOLATION.md): played back about 100 ms behind (more with jitter),
the position along a curve through each pose's velocity, the cursor running up to 15 % fast or slow
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

- Progress is each game's: the host's save is where the party starts, and what a member collects
  (gold bolts, items) and buys stays in that member's game; its card is never written by the party.
- Vendors and the ship are each game's own. A member's flight to the new planet is the game's
  flight between planets, not the host's take-off from the pad (members leave when the host's
  ship does, and usually land first, then wait). A cutscene a moby of the host's world starts plays in the host's game
  only; members wait for it (the barrier).
- Under the pause menu the world goes on but the picture behind the menu stays as it was.
- A member's crates drop their bolts in the member's game only.
- An enemy that keeps a pointer to the hero across ticks, grabs him or rides him may act on the
  host's hero instead of the player it went for; WORLD.md (in the plans) lists the classes tried.
- A figure holds what its player holds (the wrench, the weapon or gadget, Clank and the packs,
  the boots), each loaded when this game can have it beside its own hero's (two weapon classes fit
  at once): while it cannot (two other players with two other weapons, or under a menu), the
  figure holds the wrench, or nothing when even that cannot be had.
- Of the shots, the Blaster's and the Bomb Glove's are shown; the other weapons' (the Pyrocitor's
  flame, the Devastator's missiles, the Visibomb, the gloves of Doom and mines, the Decoy, the
  Tesla Claw, the R.Y.N.O.) are not yet. A figure's Blaster shot flies on its heading without
  homing on the shooter's target. A thrown wrench is shown where it is 20 times a second, not
  smoothed.
- Names are not drawn over the figures (the mod interface has no drawing over the game yet).
- A member already in a level when the host starts again keeps its own progress and only follows
  the level.
- Everyone needs the same disc and the same OpenRAC and mod versions.

## Building

Built with the repository's other native mods (the top README), with an SDK of interface 1.9 or
newer: until that SDK is released (`sdk.lock` pins an older one, and the top CMakeLists.txt then
skips this mod), build against OpenRAC's own `sdk/`:

```sh
cmake -S . -B build -G Ninja -DOPENRAC_SDK=<OpenRAC>/sdk
cmake --build build && ctest --test-dir build -R multiplayer
python3 <OpenRAC>/sdk/tools/package_mod.py build/package/multiplayer -o dist
```

`tests/play_test.cpp` checks the poses' bytes, the interpolation buffer (a steady stream, repeats
and late poses, teleports, a stall then a burst, extrapolation), and the co-op of a host and members
through a router that does what the server does with DATA and fake games: Start with a save and
with a new game, the level followed, a late joiner and a member that asks again, figures made in
the same level only and gone with the level, the silence or the party; what a player holds on its
figure and its shots fired there (a delay behind, in its level only); a build refused and the
title after the party's end; and the world: WORLD's bytes, the member's drives (not the crates,
not the static mobys), the host's spawned bolt, an enemy followed, the stand-in and its bolts, a
member's hit on the host's enemy, an enemy's hit on a member, a crate broken by a member, and a
kill and a pickup on the host.

`tests/multiplayer_test.cpp` checks the frames byte for byte against the server's encoding, the
lobby's logic against a recorder, the connection against a small server on the loopback, and
Direct connect: a host and two clients on the loopback (the party, chat, DATA both ways, UDP state,
leaving, the host stopping) and the server's limits held by the host; and the pages' own logic:
the address rows stepping around their ends and faster when held, the hosting lines, and the name
chosen from the setting and the launcher's.

| File | |
|---|---|
| `src/multiplayer.cpp` | the mod: the main-menu item and its pages |
| `src/session.hpp`, `.cpp` | the session and the party: hello, the clock, create, list, join, leave, Ready and Start |
| `src/coop.hpp`, `.cpp` | playing together: PLAY and its save, LEVEL, WANT, poses sent and figures shown |
| `src/play.hpp`, `.cpp` | the poses' bytes and the interpolation buffer |
| `src/world.hpp`, `.cpp` | the shared world: snapshots, kills, spawns, hits, damage, bolts, stand-ins |
| `src/connection.hpp`, `.cpp` | the socket on its thread, reconnecting, UDP; the link to a server, a direct host or the embedded one |
| `src/host.hpp`, `.cpp` | Direct connect's embedded host: the server's party routing for one party |
| `src/direct.hpp`, `.cpp` | Direct connect's page logic: the address's rows (64-choice rings), held steps, the hosting lines |
| `src/sockets.hpp` | the socket calls the connection and the host share (Winsock and POSIX) |
| `src/protocol.hpp`, `.cpp` | the server's frames, packets and datagrams, both sides |
