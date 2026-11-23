"""The museum's exhibits: the boards' words, the game's own objects on the plinths (classes from other
levels, which the player's disc gives back), the wall of messages, and the small sets the rooms
need (the help desk, the test room, the devkit desk, the code tiles).

Every game string on a board is written exactly as the game has it. Sources: rac1-decomp
docs/kb/datamining.md, OpenRAC docs/port/speedrun/META.md.
"""

import math

import numpy as np

import house
import island
import textures
from house import CX, CY, F0, F1, R_IN, R_OUT, WALL, ang, mid

# Pictures made here, by material name (make_museum adds them to the house's).
PICTURES = {}

# Imported classes: (class, its level, its name in the editor).
CLASSES = {
    1377: (16, "Help desk lady"), 827: (6, "Frog"), 563: (4, "Woodcut Bot"), 217: (4, "Big Axebot"),
    340: (4, "Circular saw enemy"), 623: (5, "Extermibot"), 998: (5, "Taxi"), 1048: (6, "Alien Snapper"),
    750: (1, "Infobot"), 1465: (1, "Gold Morph-o-Ray"), 1462: (1, "Gold Decoy Glove"),
    1456: (1, "Gold Bomb Glove"), 1463: (1, "Gold Tesla Claw"), 774: (1, "The Plumber"),
    925: (5, "Shady Salesman"), 919: (5, "Qwark's Bouncer"), 1105: (6, "Fred"), 1184: (9, "Airship part"),
    1210: (3, "Train carriage"), 1432: (18, "Class 1432"), 541: (16, "Test dummy"),
}
LEVELS = sorted({level for level, _ in CLASSES.values()})

# Rooms: (storey, sector) as house.GROUND / house.UPPER list them.
LAUNCHER, RENDERER, HOSTGEN, PORT, MATCHING, DECOMP, WELCOME, MODS = ((0, s) for s in range(8))
MULTI, SPEEDRUN, ENHANCED, ANDROID, CUTSCENES, EDITOR, DEVLOG, THANKS = ((1, s) for s in range(8))

# Rooms whose three plinths go: their exhibits stand on their own.
NO_PLINTHS = {HOSTGEN, MATCHING, DECOMP, EDITOR, THANKS}
# Plinths that carry an exhibit, so no ornament: (storey, sector, k) with k in -1, 0, 1.
PLAIN = {(*RENDERER, 0), (*CUTSCENES, 0), (*MODS, -1), (*MODS, 0), (*MODS, 1), (*PORT, 0), (*DEVLOG, 0)}
# Rooms without a rug (the floor has its own picture).
NO_RUG = {DECOMP, EDITOR}

# Each room's two boards: (title, paragraphs, a game string or None).
BOARDS = {
    LAUNCHER: [
        ("Your own disc", ["OpenRAC reads Ratchet & Clank from your own disc. It never downloads the game.",
                           "PAL v2.00 and v1.00 (SCES_509.16), NTSC-U (SCUS_971.99), Japan (SCPS_150.37).",
                           "Going Commando, Up Your Arsenal and Deadlocked: early access."], None),
        ("Before the discs", ["Going Commando: three 2003 builds measured. 6 Aug (NTSC preview), 7 Sep "
                              "(NTSC review), 9 Sep (PAL review).",
                              "Up Your Arsenal: a beta disc of 12 July 2004."], None),
    ],
    RENDERER: [
        ("Only when you look", ["Many mobys update only while they are drawn. Turn the camera away and they wait, "
                                "like the frogs of Blarg.",
                                "OpenRAC keeps this: change it and the game plays differently."], None),
        ("GS to OpenGL and Vulkan", ["The PS2 drew with its Graphics Synthesizer. OpenRAC turns the same "
                                     "draw lists into OpenGL or Vulkan, so the picture matches the original."], None),
    ],
    HOSTGEN: [
        ("The moby file cabinet", ["Insomniac kept each moby class in its own file: update/mobyNNN.cpp.",
                                   "Six of them stand here, under their numbers. Hostgen turns their decompiled "
                                   "code into C that runs on your PC."], None),
        ("Who is who", ["563 Woodcut Bot (Eudora)", "217 Big Axebot (Eudora)", "340 Circular saw (Eudora)",
                        "623 Extermibot (Rilgar)", "998 Taxi (Rilgar)", "1048 Alien Snapper (Blarg)"], None),
    ],
    PORT: [
        ("I-5", ["Every level has Insomniac's own debug menu. Its title is I-5, the game's project code.",
                 "Pages: UPDATE, MODE, DRAW, AMMO, FOG, MISC.",
                 "No button opens it in the retail game: it waits for game mode -1."], None),
        ("DRAW", ["The DRAW page shows what you never see: Darken, Env Coll, Moby Coll, Hero Coll, Bspheres, "
                  "Cameras, Cuboids, Samples, Text.",
                  "The terminal opens it with the OpenRAC Museum mod."], None),
    ],
    MATCHING: [
        ("Inflato R", ["The I-5 menu's AMMO page names some weapons as the team called them.",
                       "Morpha-Ray: the Morph-o-Ray.", "D. Droids: probably the Drone Device.",
                       "Agents: probably the Decoy Glove.",
                       "Inflato R: no weapon has this name. The empty plinth is for it."], None),
        ("Byte for byte", ["Matching means writing C that compiles to exactly the bytes of the game's own "
                           "program. Each matched function is proof that the C is right."], None),
    ],
    DECOMP: [
        ("The 20-button codes", ["Pause, hold R2 and L1 (not L2 or R1), then press 20 buttons.",
                                 "The tiles show one code: down, right, left, right, five times. It gives "
                                 "skill point 1.",
                                 "91 codes give items, planets and skill points. They were found in the code.",
                                 "Warning: what a code gives is saved with your game. Try it on a spare save."], None),
        ("printf does nothing", ["The messages the developers wrote for themselves all go into an empty "
                                 "printf. The words are still on the disc: read them in the atrium."], None),
    ],
    WELCOME: [
        ("Ratchett", ["The name plate on the desk is spelled the way the developers spelled it.",
                      "The debug menu's first row says Ratchett, and the memory map says:"], "ratchett seqs:"),
        ("Welcome", ["OpenRAC runs Ratchet & Clank natively, from your own disc.",
                     "Each room shows one part of how. Upstairs: speedruns, the editor, cutscenes and more."], None),
    ],
    MODS: [
        ("Archipelago", ["With the Archipelago mod, a crate can hold an item from another player's game, "
                         "and yours can end up in theirs.", "The crate on the left is waiting for one."], None),
        ("Barlow's gift", ["Going Commando gives you weapons from the first game when your memory card has "
                           "a Ratchet & Clank save.",
                           "Three of those weapons, in gold, are on the plinths."], None),
    ],
    MULTI: [
        ("Together", ["Co-op puts two players in one galaxy, each with their own Ratchet."], None),
        ("Big head booth", ["The game has a big head cheat. Stand on the gold plate and the OpenRAC Museum mod "
                            "turns it on. Step off and it goes."], None),
    ],
    SPEEDRUN: [
        ("The record", ["PS2 Any%: 36:36.196 by CptGallant (29 June 2025).",
                        "About eight minutes of it is loading.",
                        "On the NG+ board, 4th place: a runner called Insomniac, 28:07.687."], None),
        ("Every bug kept", ["OpenRAC keeps the game's logic exactly as it was, bugs included, so every "
                            "trick runners use still works."], None),
    ],
    ENHANCED: [
        ("Beyond the PS2", ["Enhanced adds what the PS2 could not: sharper pictures, more light, longer "
                            "views. The base game stays as it was."], None),
        ("Effects", ["The game's own explosions, sparks and smoke, fired in the glass box with the "
                     "OpenRAC Museum mod."], None),
    ],
    CUTSCENES: [
        ("Paradox", ["The Infobot plays the game's movies. When the game asks for a message it does not "
                     "have, it shows this instead:"], "Paradox: This message does not exist"),
        ("Lights, camera, Clank", ["OpenRAC plays the game's cutscenes as they are, and OpenRAC Editor can "
                                   "make new ones."], None),
    ],
    EDITOR: [
        ("The test room", ["Going Commando's museum had a room like this: blocks a jump apart and ramps of "
                           "rising slope.",
                           "Here: blocks 1, 2, 3 and 4 units high, and ramps of 20, 35 and 50 degrees."], None),
        ("Test dummies", ["Kalebo III's test dummies, brought here from their own level, where they "
                          "wait for the level's own start."], None),
    ],
    DEVLOG: [
        ("The devkit", ["The game still asks for files on the developers' PC, paths that lead nowhere on a "
                        "retail PS2:"], "host0:z:/i5/levels/level%d/"),
        ("File names", ["Names from Insomniac's own code, left in the game: hud.cpp, loaders.cpp, "
                        "hero1.cpp to hero8.cpp, walk.h."], "cdrom0:\\CODE\\I5\\PARAM.TXT;1"),
    ],
    THANKS: [
        ("Name tags", ["The game's own names for its people:",
                       "The Plumber: Mystery 774", "Shady Salesman: Shifty Salesman",
                       "Qwark's Bouncer: Bouncer", "Fred on Blarg: Scrawny Scientist"], None),
        ("Door 21/30", ["Going Commando's Insomniac Museum is its level 30, stored as LEVEL21.WAD. "
                        "This door is closed, for now."], None),
    ],
}

# The wall of messages: one line from the game's code over each way in from the atrium.
MESSAGES = [
    ("Why doesn't this work?", "every level"),
    ("main loop squirm! should never get here!", "every level"),
    ("HAS NO PATH!!!  Moron.", "Orxon, moby 947"),
    ("Get Pilot Hat!", "Pokitaru and Quartu"),
    ("Equilizer Ammo Spawner has no trigger cuboid!", "Umbris, moby 1142"),
    ("train engine has no cars!", "Kerwan"),
    ("*** MEMORY MAP ***", "every level"),
    ("Will just float there.", "Gaspar, the floating ship piece"),
]


def board_picture(key, title, seed):
    """The picture of exhibit board KEY (house.rooms names them om_sign_<storey>_<sector>_b<edge><k>)."""
    parts = key.split("_")
    room = (int(parts[2]), int(parts[3]))
    which = 0 if parts[4] == "b01" else 1
    if room in BOARDS:
        head, lines, quote = BOARDS[room][which]
        return textures.board(head, lines, seed, quote=quote)
    return textures.poster(title, ["Exhibit " + str(seed % 4 + 1)], seed)


def frame_of(room):
    storey, s = room
    z0 = F0 if storey == 0 else F1
    out = ang(45 * s)
    tang = np.array([-out[1], out[0]])
    rc = mid(s, (R_IN + R_OUT) / 2 + 1.0)
    return z0, out, tang, rc


def plinth_top(room, k):
    z0, out, tang, rc = frame_of(room)
    p = rc + tang * k * 3.6 + out * 0.5
    return (float(p[0]), float(p[1]), z0 + 1.22)


def facing(room):
    """The yaw (degrees) that looks from the room towards the atrium."""
    return 45 * room[1] + 180.0


def sign(geo, signs, key, picture, centre, face, w, h):
    PICTURES[key] = picture
    signs.append((key, "", "", centre, face, w, h))


def label(geo, signs, key, text, sub, base, room, z):
    """A brass plate on the atrium side of a stand at BASE (its middle), at height Z."""
    out = ang(45 * room[1])
    c = np.asarray(base[:2]) - out * 0.93
    sign(geo, signs, key, textures.plate(text, sub), (c[0], c[1], z), facing(room), 1.6, 0.4)


def pedestal(geo, p, z0, half=1.4, height=0.35):
    geo.box("om_white", (p[0] - half, p[1] - half, z0), (p[0] + half, p[1] + half, z0 + height))
    geo.box("om_gold", (p[0] - half - 0.1, p[1] - half - 0.1, z0 + height), (p[0] + half + 0.1, p[1] + half + 0.1, z0 + height + 0.08))
    return z0 + height + 0.08


def build(geo, signs):
    """The exhibits' shapes and pictures; what they place is in placements()."""
    wall_of_messages(geo, signs)
    welcome(geo, signs)
    hostgen(geo, signs)
    matching(geo, signs)
    code_tiles(geo, signs)
    terminal(geo, signs)
    test_room(geo, signs)
    devkit(geo, signs)
    thanks(geo, signs)
    plates(geo, signs)
    booth(geo, signs)
    ledge(geo, signs)


def wall_of_messages(geo, signs):
    for s, (text, where) in enumerate(MESSAGES):
        c = mid(s, R_IN - WALL / 2 - 0.08)
        sign(geo, signs, f"om_msg_{s}", textures.message(text, where), (c[0], c[1], F0 + 6.55), 45 * s + 180, 5.6, 1.05)


def welcome(geo, signs):
    """The help desk by the south door, its name plate spelled Ratchett."""
    z0, out, tang, rc = frame_of(WELCOME)
    d = mid(WELCOME[1], R_OUT - 7.0) + tang * 5.5
    geo.oriented_box("om_planks_dark", d - tang * 2.2, d + tang * 2.2, out, 1.2, z0, z0 + 1.3)
    geo.oriented_box("om_gold", d - tang * 2.3, d + tang * 2.3, out, 1.4, z0 + 1.3, z0 + 1.4, solid=False)
    c = d - out * 0.75
    sign(geo, signs, "om_plate_ratchett", textures.plate("RATCHETT", "museum of the game"), (c[0], c[1], z0 + 0.8),
         facing(WELCOME), 2.6, 0.65)


def hostgen_spots():
    z0, out, tang, rc = frame_of(HOSTGEN)
    return [(rc + tang * t * 5.0 + out * o, z0) for o in (3.2, -2.8) for t in (-1, 0, 1)]


HOSTGEN_ROW = [563, 217, 340, 623, 998, 1048]


def hostgen(geo, signs):
    for k, (p, z0) in enumerate(hostgen_spots()):
        pedestal(geo, p, z0, 1.5)
        cls = HOSTGEN_ROW[k]
        label(geo, signs, f"om_plate_hostgen_{k}", f"update/moby{cls}.cpp", CLASSES[cls][1], (*p, z0), HOSTGEN, z0 + 0.22)


def matching_spots():
    z0, out, tang, rc = frame_of(MATCHING)
    return [(rc + tang * t + out * 0.5, z0) for t in (-5.4, -1.8, 1.8, 5.4)]


MATCHING_ROW = [(1465, "Morpha-Ray"), (None, "D. Droids"), (1462, "Agents"), (None, "Inflato R")]


def matching(geo, signs):
    for k, (p, z0) in enumerate(matching_spots()):
        house.plinth(geo, p, z0, 0, ornament=False)
        cls, name = MATCHING_ROW[k]
        label(geo, signs, f"om_plate_matching_{k}", name, "AMMO page" if cls else "", (*p, z0), MATCHING, z0 + 0.75)
        if k == 1:      # no gold Drone Device: a picture of its name stands on the plinth
            c = np.asarray(p)
            sign(geo, signs, "om_card_droids", textures.plate("D. Droids", "the Drone Device, no gold model"),
                 (c[0], c[1], z0 + 1.9), facing(MATCHING), 1.6, 0.5)


def code_tiles(geo, signs):
    """Down, right, left, right five times: four tiles a row, rows from the atrium outwards."""
    z0, out, tang, rc = frame_of(DECOMP)
    seq = ["↓", "→", "←", "→"]
    for sym in set(seq):
        PICTURES[f"om_tile_{ord(sym)}"] = textures.arrow(sym)
    size = 1.7
    for row in range(5):
        for col in range(4):
            # Seen from the atrium (facing OUT), +tang is to the left: the first column there.
            c = rc + out * ((row - 2) * (size + 0.2)) + tang * ((1.5 - col) * (size + 0.2))
            q = np.array([[*(c - tang * size / 2 - out * size / 2), z0 + 0.05], [*(c + tang * size / 2 - out * size / 2), z0 + 0.05],
                          [*(c + tang * size / 2 + out * size / 2), z0 + 0.05], [*(c - tang * size / 2 + out * size / 2), z0 + 0.05]])
            # Upright for someone walking in from the atrium (facing OUT): the picture's top is OUT.
            uv = np.array([[1, 1], [0, 1], [0, 0], [1, 0]], np.float32)
            if np.cross(q[1] - q[0], q[3] - q[0])[2] < 0:
                q = q[[0, 3, 2, 1]]
                uv = uv[[0, 3, 2, 1]]
            key = f"om_tile_{ord(seq[col])}"
            geo.tri_quad(key, q, [0, 0, 1], uv, solid=False, glow=[0.6, 0.6, 0.6])


def terminal(geo, signs):
    """The I-5 terminal: a console on the middle plinth's spot, and a pad before it."""
    z0, out, tang, rc = frame_of(PORT)
    p = np.asarray(plinth_top(PORT, 0)[:2])
    geo.oriented_box("om_rail", p - tang * 1.2 + out * 0.6, p + tang * 1.2 + out * 0.6, out, 0.6, z0 + 1.22, z0 + 3.4)
    c = p + out * 0.25
    sign(geo, signs, "om_screen_i5", textures.screen(["I-5", "", "UPDATE  MODE  DRAW", "AMMO  FOG  MISC", "",
                                                       "Ratchett   on", "Mobys     on", "Parts     on",
                                                       "Camera    on", "Step      off"]),
         (c[0], c[1], z0 + 2.4), facing(PORT), 2.0, 1.5)
    pad = p - out * 3.0
    geo.cylinder("om_teal", (pad[0], pad[1]), 1.3, z0, z0 + 0.12, 16, solid=True)


def test_room(geo, signs):
    """Blocks 1-4 units high along the outer wall, and three ramps from the atrium side."""
    z0, out, tang, rc = frame_of(EDITOR)
    for k, h in enumerate((1.0, 2.0, 3.0, 4.0)):
        c = mid(EDITOR[1], R_OUT - 3.5) + tang * (k - 1.5) * 3.4
        geo.oriented_box("om_white" if k % 2 else "om_teal", c - tang * 1.2, c + tang * 1.2, out, 2.4, z0, z0 + h)
        f = c - out * 1.22
        sign(geo, signs, f"om_plate_block_{k}", textures.plate(f"{h:g} unit{'s' if h > 1 else ''}"),
             (f[0], f[1], z0 + h - 0.3), facing(EDITOR), 1.2, 0.3)
    for k, deg in enumerate((20, 35, 50)):
        length = 4.5
        base = mid(EDITOR[1], R_IN + 5.0) + tang * (k - 1) * 4.2
        top = base + out * length
        rise = length * math.tan(math.radians(deg))
        q = np.array([[*(base - tang * 1.4), z0], [*(base + tang * 1.4), z0],
                      [*(top + tang * 1.4), z0 + rise], [*(top - tang * 1.4), z0 + rise]])
        n = np.cross(q[1] - q[0], q[3] - q[0])
        if n[2] < 0:
            q = q[[0, 3, 2, 1]]
            n = -n
        n /= np.linalg.norm(n)
        geo.tri_quad("om_planks_light", q, list(n), np.array([[0, 0], [1, 0], [1, 2], [0, 2]], np.float32), solid=True)
        geo.oriented_box("om_planks_dark", top - tang * 1.4, top + tang * 1.4, out, 0.3, z0, z0 + rise, solid=True)
        for side in (-1, 1):
            a, b = base + tang * 1.4 * side, top + tang * 1.4 * side
            tri = np.array([[*a, z0], [*b, z0], [*b, z0 + rise]])
            geo.tri_quad("om_planks_dark", np.array([tri[0], tri[1], tri[2], tri[2]]), [*(tang * side), 0],
                         np.zeros((4, 2), np.float32), solid=False)
        f = base - out * 0.3
        sign(geo, signs, f"om_plate_ramp_{k}", textures.plate(f"{deg} degrees"), (f[0], f[1], z0 + 0.5), facing(EDITOR), 1.2, 0.3)


def devkit(geo, signs):
    """The devkit desk: an old monitor with the dead paths on its screen."""
    z0, out, tang, rc = frame_of(DEVLOG)
    p = np.asarray(plinth_top(DEVLOG, 0)[:2])
    z = z0 + 1.22
    geo.oriented_box("om_planks_dark", p - tang * 0.9 + out * 0.2, p + tang * 0.9 + out * 0.2, out, 1.3, z, z + 1.4)
    c = p - out * 0.47
    sign(geo, signs, "om_screen_devkit", textures.screen(["host0:z:/i5/levels/", "  level%d/...", "",
                                                           "cdrom0:\\CODE\\I5\\", "  PARAM.TXT;1", "",
                                                           "file not found_"]),
         (c[0], c[1], z + 0.75), facing(DEVLOG), 1.5, 1.1)


def thanks_spots():
    z0, out, tang, rc = frame_of(THANKS)
    return [(rc + tang * t + out * 1.5, z0) for t in (-5.4, -1.8, 1.8, 5.4)]


THANKS_ROW = [(774, "Mystery 774", "The Plumber"), (925, "Shifty Salesman", "Shady Salesman"),
              (919, "Bouncer", "Qwark's Bouncer"), (1105, "Scrawny Scientist", "Fred")]


def thanks(geo, signs):
    for k, (p, z0) in enumerate(thanks_spots()):
        pedestal(geo, p, z0, 1.1)
        _, tag, name = THANKS_ROW[k]
        label(geo, signs, f"om_plate_thanks_{k}", tag, name, (*p, z0), THANKS, z0 + 0.22)
    # Door 21/30: a closed door in the outer wall's inside, right of the windows.
    z0, out, tang, rc = frame_of(THANKS)
    d = mid(THANKS[1], R_OUT - WALL / 2 - 0.1) - tang * 6.2
    geo.oriented_box("om_planks_dark", d - tang * 1.3, d + tang * 1.3, out, 0.2, z0, z0 + 4.0, solid=False)
    geo.oriented_box("om_gold", d - tang * 1.5, d + tang * 1.5, out, 0.25, z0 + 4.0, z0 + 4.2, solid=False)
    c = d - out * 0.15
    sign(geo, signs, "om_plate_door21", textures.plate("LEVEL21.WAD", "level 30"), (c[0], c[1], z0 + 3.2),
         facing(THANKS), 1.6, 0.4)


def plates(geo, signs):
    """Name plates under the exhibits on plinths."""
    for room, k, text, sub in ((RENDERER, 0, "Frog", "Blarg"),
                               (CUTSCENES, 0, "Infobot", "Paradox"),
                               (MODS, -1, "Archipelago crate", ""), (MODS, 0, "Gold Bomb Glove", "Barlow's gift"),
                               (MODS, 1, "Gold Tesla Claw", "Barlow's gift"),
                               (SPEEDRUN, 0, "Insomniac", "NG+ 28:07.687, 4th")):
        p = plinth_top(room, k)
        label(geo, signs, f"om_plate_{room[0]}{room[1]}_{k + 1}", text, sub, p, room, p[2] - 0.5)
    # The frog's glass case: four gold posts and a frame.
    x, y, z = plinth_top(RENDERER, 0)
    for dx, dy in ((-0.8, -0.8), (0.8, -0.8), (0.8, 0.8), (-0.8, 0.8)):
        geo.box("om_gold", (x + dx - 0.06, y + dy - 0.06, z), (x + dx + 0.06, y + dy + 0.06, z + 1.6), solid=False)
    geo.box("om_gold", (x - 0.86, y - 0.86, z + 1.6), (x + 0.86, y + 0.86, z + 1.68), solid=False)


def booth(geo, signs):
    """The big head booth's plate in the Multiplayer room."""
    z0, out, tang, rc = frame_of(MULTI)
    p = rc + out * 4.5
    geo.cylinder("om_gold", (p[0], p[1]), 1.4, z0, z0 + 0.1, 20, solid=True)


def ledge(geo, signs):
    """The tower's balcony: a board over the lagoon."""
    tx, ty = house.TOWER
    z0 = house.TOWER_TOP
    c = (tx - house.TOWER_HALF - 2.6, ty - 2.5, z0 + 1.2)
    sign(geo, signs, "om_msg_fall", textures.message("no final place for hero during victory, so just fall",
                                                      "Drek's Fleet"), c, 0.0, 3.4, 0.64)


def placements():
    """The game's objects the exhibits place: dicts of class, level, at (x, y, z), yaw, scale
    (over the class's own) and how it moves ("look": stands, its code never runs)."""
    out = []

    def put(cls, at, yaw, scale=1.0, look=True, **more):
        out.append(dict({"class": cls, "level": CLASSES[cls][0], "name": CLASSES[cls][1], "at": [float(v) for v in at],
                         "yaw": float(yaw), "scale": scale, "look": look}, **more))

    # Welcome: the help desk lady behind her desk, facing the room.
    z0, o, tang, rc = frame_of(WELCOME)
    d = mid(WELCOME[1], R_OUT - 7.0) + tang * 5.5 + o * 1.6
    put(1377, (*d, z0), facing(WELCOME))
    put(827, plinth_top(RENDERER, 0), facing(RENDERER))
    for k, (p, z) in enumerate(hostgen_spots()):
        put(HOSTGEN_ROW[k], (*p, z + 0.43), facing(HOSTGEN), scale=0.5 if HOSTGEN_ROW[k] in (217, 998) else 1.0)
    for k, (p, z) in enumerate(matching_spots()):
        cls = MATCHING_ROW[k][0]
        if cls:
            put(cls, (*p, z + 1.5), facing(MATCHING))
    put(750, plinth_top(CUTSCENES, 0), facing(CUTSCENES))
    put(1456, np.array(plinth_top(MODS, 0)) + [0, 0, 0.3], facing(MODS))
    put(1463, np.array(plinth_top(MODS, 1)) + [0, 0, 0.3], facing(MODS))
    for k, (p, z) in enumerate(thanks_spots()):
        put(THANKS_ROW[k][0], (*p, z + 0.43), facing(THANKS))
    # The test room's dummies, between the blocks and the ramps.
    z0, o, tang, rc = frame_of(EDITOR)
    for t in (-4.5, 4.5):
        p = rc + o * 2.0 + tang * t
        put(541, (*p, z0), facing(EDITOR))
    # Over the fountain, high in the atrium: the floating ship piece.
    put(1184, (CX, CY, F1 + 5.0), 30.0)
    # The train carriage alone at the end of the pier.
    pier = np.array([island.CENTRE[0] + math.cos(math.radians(-95)) * 205, island.CENTRE[1] + math.sin(math.radians(-95)) * 205])
    put(1210, (pier[0] + 10, pier[1] - 6, island.SEA + 3.5), 90.0)
    # The tower's secret room: the laziest moby.
    tx, ty = house.TOWER
    put(1432, (tx + 2.0, ty + 2.0, house.TOWER_TOP), 225.0)
    return out


def crates():
    """Our own copies of Pokitaru's objects for the exhibits: (kind, position, yaw)."""
    return [("bolt_crate", list(plinth_top(MODS, -1)), facing(MODS))]
