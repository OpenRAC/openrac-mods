"""The museum's exhibits: one per room about how OpenRAC is made, things cut from the game in the
atrium, and a gold tile before each exhibit. Standing on a tile, the OpenRAC Museum mod shows the
exhibit's words (STATIONS; tools/mod_data.py writes them into the mod).

Every game string is written exactly as the game has it. Sources: rac1-decomp docs/kb/datamining.md,
OpenRAC docs/port/speedrun/META.md, The Cutting Room Floor's Ratchet & Clank pages (cut content).
"""

import math

import numpy as np

import house
import park
import textures
from house import CX, CY, F0, F1, R_IN, R_OUT, WALL, ang, mid

# Pictures made here, by material name (make_museum adds them to the house's).
PICTURES = {}

# Classes from other levels: (class, its level, its name in the editor). Shown as they stand
# ("look": their code never runs); the player's disc gives their models back.
CLASSES = {
    164: (12, "Revolverator"),
}
LEVELS = sorted({level for level, _ in CLASSES.values()})

# Rooms: (storey, sector) as house.GROUND / house.UPPER list them.
LAUNCHER, RENDERER, HOSTGEN, PORT, MATCHING, DECOMP, WELCOME, MODS = ((0, s) for s in range(8))
MULTI, SPEEDRUN, ENHANCED, ANDROID, CUTSCENES, EDITOR, DEVLOG, THANKS = ((1, s) for s in range(8))
ROOMS = [LAUNCHER, RENDERER, HOSTGEN, PORT, MATCHING, DECOMP, WELCOME, MODS,
         MULTI, SPEEDRUN, ENHANCED, ANDROID, CUTSCENES, EDITOR, DEVLOG, THANKS]

# No room has the house's plinths: each has its own exhibit.
NO_PLINTHS = set(ROOMS)
PLAIN = set()
# Rooms without a rug (the floor has its own picture or shapes).
NO_RUG = {DECOMP, EDITOR}

# The exhibits' words, one tile each: key -> (title, text). Short, plain English; the mod wraps them.
# The HUD font has no & * = [ ] { } ~, so the words do without them.
WORDS = {
    "launcher": ("The launcher",
                 "OpenRAC never downloads the game. The launcher reads Ratchet and Clank from your own disc "
                 "and builds it for your computer, once. Your saves, mods and crash reports live here too."),
    "renderer": ("From the PS2 to your GPU",
                 "The PS2 drew with its Graphics Synthesizer. OpenRAC takes the very draw lists the game makes "
                 "and draws them with OpenGL or Vulkan, so the picture matches the PS2."),
    "hostgen": ("Hostgen",
                "The decomp team writes the game's code as C. Hostgen turns that C into C for your computer: "
                "each PS2 address becomes a place in OpenRAC's copy of the PS2's memory. The screen shows one "
                "function, before and after."),
    "port": ("I-5",
             "Insomniac's own debug menu is still in every level. Its title is I-5, the game's project code. "
             "No button opens it in the shop game. Stand on the teal pad and the museum mod opens it. "
             "Select leaves it."),
    "matching": ("Byte for byte",
                 "Matching means writing C that the original compiler turns into the very bytes of the game. "
                 "When they match, the C is proven right."),
    "decomp": ("The 20-button codes",
               "Found in the code: 20-button codes typed in the pause menu. 91 of them give items, planets "
               "and skill points, and what a code gives is saved with your game."),
    "welcome": ("Welcome",
                "This museum shows how OpenRAC runs Ratchet and Clank on your computer, one part in each room. "
                "Step on a gold tile to read about what you see."),
    "mods": ("Mods",
             "Mods change the game and never touch your disc. They are built on the OpenRAC SDK, and the "
             "launcher adds them from a link or a file. This museum is a mod too."),
    "multi": ("Co-op",
              "Ratchet and Clank was made for one player. OpenRAC's co-op puts two players in one galaxy, "
              "each with their own Ratchet."),
    "speedrun": ("Every bug kept",
                 "OpenRAC keeps the game's logic exactly as it was, bugs included, so every trick speedrunners "
                 "use still works. About eight minutes of the PS2 record is loading."),
    "enhanced": ("Effects",
                 "Stand on the teal pad: the museum mod starts the game's own sparks, smoke and bursts in the "
                 "case. Enhanced graphics adds light and sharper pictures. The base game stays as it was."),
    "android": ("In your pocket",
                "OpenRAC runs on Android phones and handhelds, with a gamepad or touch buttons. The same code "
                "builds for Windows, macOS and Linux."),
    "cutscenes": ("Cutscenes",
                  "OpenRAC plays the game's cutscenes as they are. OpenRAC Editor writes new ones: a camera, "
                  "actors and lines in one text file, as on the screen."),
    "editor": ("The test room",
               "Going Commando's museum had a room like this. Blocks 1, 2, 3 and 4 units high, and ramps of "
               "20, 35 and 50 degrees: try which ones Ratchet can climb. OpenRAC Editor built this island."),
    "devlog": ("The devkit",
               "The game still asks for files on the developers' PC, at host0:z:/i5/levels/. On a shop PS2 "
               "these paths lead nowhere."),
    "thanks": ("Thank you",
               "OpenRAC is made by the people on this wall, and by the decomp team before them. Every name is "
               "from the projects' own history."),
    "door21": ("Door 21/30",
               "Going Commando's Insomniac Museum is its level 30, stored as LEVEL21.WAD. This door opens "
               "only between 3 and 4 in the morning, on your own clock."),
    "cut_revolverator": ("The Revolverator",
                         "Cut from the game: a drill that spun enemies over Ratchet's head. Its model is still "
                         "on the disc. The Revolverator mod for OpenRAC brings it back."),
    "cut_e3": ("The E3 demo",
               "At E3 in 2002 the game was not finished. Rilgar was called Talos, and the Decoy Glove was the "
               "Inflato-Ratchet Glove. The I-5 menu still calls it Inflato R."),
    "cut_mackerel": ("The Mackerel 1000",
                     "A fish that was to replace the wrench. It was cut, and only its icon is left in the "
                     "game's files."),
    "cut_messages": ("Notes to themselves",
                     "The lines over the doors are messages the developers wrote for themselves. The game sends "
                     "them all to an empty printf, so nobody saw them, but the words are still on the disc."),
    "jetboard": ("The JET-Board",
                 "Jak II's hover board, made for Ratchet by RAC-Board, a mod for OpenRAC. Turn the mod on and "
                 "press D-pad Up to ride. Hold R1 with Cross by an edge to grind it."),
}

# The tiles: (key, x, y, z), filled by build(); the mod's data comes from them.
STATIONS = []
# The mod's other places: name -> (x, y, z, radius).
ZONES = {}

# The museum's own words on the walls: one line from the game's code over each way in from the atrium.
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

# Each room's two wall boards: (title, paragraphs, a game string or None), or None for no board.
# A board says what the room's tile does not: the tile tells the story, the boards add facts.
BOARDS = {
    LAUNCHER: [("Your own disc", ["PAL v2.00 and v1.00 (SCES_509.16), NTSC-U (SCUS_971.99), Japan (SCPS_150.37).",
                                  "Going Commando, Up Your Arsenal and Deadlocked: early access."], None), None],
    RENDERER: [("Only when you look", ["Many mobys update only while they are drawn. Turn the camera away "
                                       "and they wait. OpenRAC keeps this: change it and the game plays "
                                       "differently."], None), None],
    HOSTGEN: [None, ("update/mobyNNN.cpp", ["Insomniac kept each kind of moby in its own file, named by its "
                                            "number. The names are still in the game's messages."], None)],
    PORT: [("I-5", ["Pages: UPDATE, MODE, DRAW, AMMO, FOG, MISC."], None),
           ("DRAW", ["The DRAW page shows what you never see: Darken, Env Coll, Moby Coll, Hero Coll, Bspheres, "
                     "Cameras, Cuboids, Samples, Text."], None)],
    MATCHING: [("Inflato R", ["The I-5 menu's AMMO page names some weapons as the team called them.",
                              "Morpha-Ray: the Morph-o-Ray. D. Droids: the Drone Device. Agents: the Decoy "
                              "Glove."], None), None],
    DECOMP: [("One code", ["Hold R2 and L1 (not L2 or R1) in the pause menu.",
                           "Down, right, left, right, five times: skill point 1.",
                           "Try it on a spare save."], None), None],
    WELCOME: [("Ratchett", ["The debug menu's first row says Ratchett, and the memory map says:"], "ratchett seqs:"),
              None],
    MODS: [None, ("A planet as a mod", ["This museum is a map file: the launcher adds it like any mod, and your "
                                        "disc gives back the game's pictures and models."], None)],
    MULTI: [None, ("Co-op saves", ["A party's progress is kept in co-op saves. Your own saves stay as they "
                                   "are."], None)],
    SPEEDRUN: [("NG+", ["On the NG+ board, 4th place: a runner called Insomniac, 28:07.687."], None), None],
    ENHANCED: [None, None],
    ANDROID: [None, None],
    CUTSCENES: [("Paradox", ["When the game asks for a message it does not have, it shows this:"],
                 "Paradox: This message does not exist"), None],
    EDITOR: [None, ("This island", ["The museum, the island, its rocks and its light were all made with "
                                    "OpenRAC Editor and its tools."], None)],
    DEVLOG: [None, ("File names", ["Names from Insomniac's own code, left in the game: hud.cpp, loaders.cpp, "
                                   "hero1.cpp to hero8.cpp, walk.h."], "cdrom0:\\CODE\\I5\\PARAM.TXT;1")],
    THANKS: [None, None],
}


def has_board(room, which):
    return room in BOARDS and BOARDS[room][which] is not None


# The people on the Thank You wall: the authors in the projects' git history (bots left out).
CONTRIBUTORS = ["Flavius", "ネオン神", "Kryštof Malinda", "Pezza", "Mateusz Kłysz", "Louis-Philippe Le Sieur",
                "Adrien Girardeau", "Henning Berge", "SavedbyBlood9", "iborrowcheckbtw", "Clank700",
                "Eric Cameracci", "Lynder063", "Ben Perry", "Doogie", "Lore", "momo1212221"]


def board_picture(key, title, seed):
    """The picture of exhibit board KEY (house.rooms names them om_sign_<storey>_<sector>_b<edge><k>)."""
    parts = key.split("_")
    room = (int(parts[2]), int(parts[3]))
    which = 0 if parts[4] == "b01" else 1
    if room in BOARDS:
        head, lines, quote = BOARDS[room][which]
        return textures.board(head, lines, seed, quote=quote)
    return textures.poster(title, [], seed)


def frame_of(room):
    storey, s = room
    z0 = F0 if storey == 0 else F1
    out = ang(45 * s)
    tang = np.array([-out[1], out[0]])
    rc = mid(s, (R_IN + R_OUT) / 2 + 1.0)
    return z0, out, tang, rc


def facing(room):
    """The yaw (degrees) that looks from the room towards the atrium."""
    return 45 * room[1] + 180.0


def yaw(v):
    return math.degrees(math.atan2(v[1], v[0]))


def sign(geo, signs, key, picture, centre, face, w, h):
    PICTURES[key] = picture
    signs.append((key, "", "", centre, face, w, h))


def pedestal(geo, p, z0, half=1.4, height=0.35):
    geo.box("om_white", (p[0] - half, p[1] - half, z0), (p[0] + half, p[1] + half, z0 + height))
    geo.box("om_gold", (p[0] - half - 0.1, p[1] - half - 0.1, z0 + height), (p[0] + half + 0.1, p[1] + half + 0.1, z0 + height + 0.08))
    return z0 + height + 0.08


def floor_picture(geo, key, c, up, size, z):
    """A square picture flat on the floor at C, its top towards UP (a unit 2D vector)."""
    right = np.array([up[1], -up[0]])
    q = np.array([[*(c - right * size / 2 - up * size / 2), z], [*(c + right * size / 2 - up * size / 2), z],
                  [*(c + right * size / 2 + up * size / 2), z], [*(c - right * size / 2 + up * size / 2), z]])
    uv = np.array([[0, 1], [1, 1], [1, 0], [0, 0]], np.float32)
    if np.cross(q[1] - q[0], q[3] - q[0])[2] < 0:
        q = q[[0, 3, 2, 1]]
        uv = uv[[0, 3, 2, 1]]
    geo.tri_quad(key, q, [0, 0, 1], uv, solid=False, glow=[0.7, 0.7, 0.7])


def tile(geo, key, c, up, z0):
    """An exhibit's gold tile: standing on it shows WORDS[key]."""
    PICTURES.setdefault("om_info", textures.info_tile())
    floor_picture(geo, "om_info", np.asarray(c, float), up, 1.6, z0 + 0.1)
    STATIONS.append((key, float(c[0]), float(c[1]), float(z0)))


def screen_stand(geo, signs, key, picture, c, n, z0, w, h, bottom=0.9):
    """A framed picture on two legs at C, facing N: the exhibits' screens."""
    c = np.asarray(c, float)
    n = np.asarray(n, float) / np.linalg.norm(n)
    d = np.array([-n[1], n[0]])
    zb = z0 + bottom
    geo.oriented_box("om_planks_dark", c - d * (w / 2 + 0.2) - n * 0.15, c + d * (w / 2 + 0.2) - n * 0.15, n, 0.2,
                     zb - 0.2, zb + h + 0.2)
    geo.oriented_box("om_gold", c - d * (w / 2 + 0.3) - n * 0.15, c + d * (w / 2 + 0.3) - n * 0.15, n, 0.3,
                     zb + h + 0.2, zb + h + 0.35, solid=False)
    for t in (-1, 1):
        leg = c + d * t * (w / 2 - 0.2) - n * 0.15
        geo.box("om_planks_dark", (leg[0] - 0.12, leg[1] - 0.12, z0), (leg[0] + 0.12, leg[1] + 0.12, zb - 0.2))
    sign(geo, signs, key, picture, (c[0], c[1], zb + h / 2), yaw(n), w, h)


def room_exhibit(room):
    """Where a room's exhibit stands, which way it faces, and where its tile is. The two door rooms
    keep their middle free: their exhibit stands at one side, facing the way through."""
    z0, out, tang, rc = frame_of(room)
    if room in (LAUNCHER, WELCOME):
        side = 1 if room == LAUNCHER else -1
        return z0, rc + tang * side * 6.2, -tang * side, rc + tang * side * 3.4, out
    if room in (RENDERER, SPEEDRUN):    # the tower stands in the back of these two rooms
        return z0, rc + out * 0.2, -out, rc - out * 3.4, out
    return z0, rc + out * 3.6, -out, rc - out * 1.6, out


def build(geo, signs):
    """The exhibits' shapes, pictures and tiles; what they place is in placements()."""
    STATIONS.clear()
    ZONES.clear()
    wall_of_messages(geo, signs)
    screens(geo, signs)
    welcome(geo, signs)
    terminal(geo, signs)
    code_tiles(geo, signs)
    effects_case(geo, signs)
    handheld(geo, signs)
    test_room(geo, signs)
    devkit(geo, signs)
    thanks(geo, signs)
    cut_content(geo, signs)
    ledge(geo, signs)
    STATIONS.append(park.station())        # drawn with the park (landmarks)


def wall_of_messages(geo, signs):
    for s, (text, where) in enumerate(MESSAGES):
        c = mid(s, R_IN - WALL / 2 - 0.08)
        sign(geo, signs, f"om_msg_{s}", textures.message(text, where), (c[0], c[1], F0 + 6.55), 45 * s + 180, 5.6, 1.05)


def screens(geo, signs):
    """The rooms whose exhibit is a picture on a stand."""
    decomp_c = ["void draw_page_contents(void) {", "    if (skip_frame == 0)",
                "        draw_pause_menu_pages(0);", "}"]
    host_c = ["void func_L00_001F92C0(void)", "{", "  if ((GREF(int,", "       OPENRAC_LDATA(0,",
              "       0x0015F6BCu)) == 0))", "  {", "    func_L00_002781D8((int)(0));", "  }", "}"]
    shows = {
        LAUNCHER: ("om_scr_launcher", textures.photo("launcher-saves.jpg", 1024, 512, "Saves, help and crash reports"), 4.8, 2.4),
        RENDERER: ("om_scr_renderer", textures.card("PS2 Graphics Synthesizer", [], big="GS  ->  OpenGL / Vulkan"), 4.8, 3.0),
        HOSTGEN: ("om_scr_hostgen", textures.code_card([("The decomp's C", decomp_c), ("Hostgen's C", host_c)],
                                                       title="One function, twice",
                                                       note="Same function: the pause menu's pages."), 4.8, 3.0),
        MATCHING: ("om_scr_matching", textures.code_card([("Written by hand", ["void frame_roll_counter(void) {",
                                                                               "    char *p = frame_roll_counters_base;",
                                                                               "    int t = *(int *)(p + 0x364);",
                                                                               "    *(int *)(p + 0x364) = 0;",
                                                                               "    *(int *)(p + 0x1B0) = 0;",
                                                                               "    *(int *)(p + 0x368) = t;", "}"])],
                                                         title="MATCH: the same bytes as the game",
                                                         note="Compiled with the original compiler, byte for byte."),
                     4.8, 3.0),
        MODS: ("om_scr_mods", textures.photo("launcher-mods.jpg", 1024, 900, "The launcher's Mods page"), 3.8, 3.34),
        SPEEDRUN: ("om_scr_speedrun", textures.card("PS2 Any% world record", ["CptGallant, 29 June 2025."],
                                                    big="36:36.196"), 4.8, 3.0),
        CUTSCENES: ("om_scr_cutscenes", textures.code_card([("meet-the-vendor.json", [
            '"format": "openrac-cutscene",', '"level": 3,', '"name": "Meet the vendor",', '"length": 720,',
            '"camera": { "keys": [', '  { "t": 0, "eye": [260, 147, 52],', '    "look_at": [262, 150, 50] }',
            "] },", '"actors": [ ... ],', '"subtitles": [ ... ]'])], title="A cutscene is a text file"), 4.8, 3.0),
        MULTI: ("om_scr_multi", textures.card("Co-op", [], big="Player 1 + Player 2"), 4.8, 3.0),
        THANKS: ("om_scr_thanks", textures.names_wall("Made by", CONTRIBUTORS), 3.6, 3.6),
    }
    for room, (key, picture, w, h) in shows.items():
        z0, c, n, t, up = room_exhibit(room)
        screen_stand(geo, signs, key, picture, c, n, z0, w, h)
        tile(geo, room_key(room), t, -n, z0)


def room_key(room):
    return {LAUNCHER: "launcher", RENDERER: "renderer", HOSTGEN: "hostgen", PORT: "port", MATCHING: "matching",
            DECOMP: "decomp", WELCOME: "welcome", MODS: "mods", MULTI: "multi", SPEEDRUN: "speedrun",
            ENHANCED: "enhanced", ANDROID: "android", CUTSCENES: "cutscenes", EDITOR: "editor", DEVLOG: "devlog",
            THANKS: "thanks"}[room]


def welcome(geo, signs):
    """The help desk by the south door, its name plate spelled Ratchett, and the museum's map."""
    from house import GROUND, UPPER
    z0, out, tang, rc = frame_of(WELCOME)
    d = mid(WELCOME[1], R_OUT - 7.0) + tang * 5.5
    geo.oriented_box("om_planks_dark", d - tang * 2.2, d + tang * 2.2, out, 1.2, z0, z0 + 1.3)
    geo.oriented_box("om_gold", d - tang * 2.3, d + tang * 2.3, out, 1.4, z0 + 1.3, z0 + 1.4, solid=False)
    c = d - out * 0.75
    sign(geo, signs, "om_plate_ratchett", textures.plate("RATCHETT", "museum of the game"), (c[0], c[1], z0 + 0.8),
         facing(WELCOME), 2.6, 0.65)
    z0, c, n, t, up = room_exhibit(WELCOME)
    screen_stand(geo, signs, "om_scr_map", textures.floor_plan(GROUND, UPPER, here=WELCOME), c, n, z0, 4.8, 2.7)
    tile(geo, "welcome", t, -n, z0)


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
            floor_picture(geo, f"om_tile_{ord(seq[col])}", c, out, size, z0 + 0.05)
    tile(geo, "decomp", rc - out * 6.0, out, z0)
    ZONES["codes"] = (float(rc[0]), float(rc[1]), float(z0), 12.0)


def terminal(geo, signs):
    """The I-5 terminal: a console on a desk at the back, and the teal pad before it that opens the menu."""
    z0, c, n, t, out = room_exhibit(PORT)
    d = np.array([-n[1], n[0]])
    geo.oriented_box("om_white", c - d * 1.4, c + d * 1.4, n, 1.4, z0, z0 + 1.1)
    geo.oriented_box("om_gold", c - d * 1.5, c + d * 1.5, n, 1.5, z0 + 1.1, z0 + 1.22, solid=False)
    p = c - n * 0.1
    geo.oriented_box("om_rail", p - d * 1.2, p + d * 1.2, n, 0.6, z0 + 1.22, z0 + 3.4)
    s = p + n * 0.32
    sign(geo, signs, "om_screen_i5", textures.screen(["I-5", "", "UPDATE  MODE  DRAW", "AMMO  FOG  MISC", "",
                                                       "Ratchett   on", "Mobys     on", "Parts     on",
                                                       "Camera    on", "Step      off"]),
         (s[0], s[1], z0 + 2.4), yaw(n), 2.0, 1.5)
    pad = c + n * 2.3
    geo.cylinder("om_teal", (pad[0], pad[1]), 1.2, z0, z0 + 0.12, 16, solid=True)
    ZONES["i5"] = (float(pad[0]), float(pad[1]), float(z0 + 0.12), 1.2)
    tile(geo, "port", t, -n, z0)


def effects_case(geo, signs):
    """A glass case for the effects: gold posts on a low pedestal, and the teal pad before it."""
    z0, c, n, t, out = room_exhibit(ENHANCED)
    top = pedestal(geo, c, z0, 1.3, 0.4)
    for dx, dy in ((-1.2, -1.2), (1.2, -1.2), (1.2, 1.2), (-1.2, 1.2)):
        geo.box("om_gold", (c[0] + dx - 0.07, c[1] + dy - 0.07, top), (c[0] + dx + 0.07, c[1] + dy + 0.07, top + 2.6), solid=False)
    geo.box("om_gold", (c[0] - 1.27, c[1] - 1.27, top + 2.6), (c[0] + 1.27, c[1] + 1.27, top + 2.7), solid=False)
    ZONES["effects_case"] = (float(c[0]), float(c[1]), float(top), 1.2)
    pad = c + n * 2.6
    geo.cylinder("om_teal", (pad[0], pad[1]), 1.1, z0, z0 + 0.12, 16, solid=True)
    ZONES["effects"] = (float(pad[0]), float(pad[1]), float(z0 + 0.12), 1.1)
    tile(geo, "enhanced", t, -n, z0)


def handheld(geo, signs):
    """A handheld on a stand: a dark body with two sticks round the launcher on its screen."""
    z0, c, n, t, out = room_exhibit(ANDROID)
    d = np.array([-n[1], n[0]])
    zb = z0 + 1.0
    geo.box("om_planks_dark", (c[0] - 0.15, c[1] - 0.15, z0), (c[0] + 0.15, c[1] + 0.15, zb))
    geo.oriented_box("om_rail", c - d * 2.6, c + d * 2.6, n, 0.35, zb, zb + 2.2)
    for side in (-1, 1):
        stick = c + d * side * 2.05 + n * 0.2
        geo.cylinder("om_planks_dark", (stick[0], stick[1]), 0.22, zb + 1.3, zb + 1.5, 10)
        button = c + d * side * 2.05 + n * 0.2
        geo.box("om_gold", (button[0] - 0.1, button[1] - 0.1, zb + 0.55), (button[0] + 0.1, button[1] + 0.1, zb + 0.75), solid=False)
    s = c + n * 0.18
    sign(geo, signs, "om_scr_android", textures.photo("launcher-saves.jpg", 1024, 576), (s[0], s[1], zb + 1.1),
         yaw(n), 3.4, 1.9)
    tile(geo, "android", t, -n, z0)


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
        nn = np.cross(q[1] - q[0], q[3] - q[0])
        if nn[2] < 0:
            q = q[[0, 3, 2, 1]]
            nn = -nn
        nn /= np.linalg.norm(nn)
        geo.tri_quad("om_planks_light", q, list(nn), np.array([[0, 0], [1, 0], [1, 2], [0, 2]], np.float32), solid=True)
        geo.oriented_box("om_planks_dark", top - tang * 1.4, top + tang * 1.4, out, 0.3, z0, z0 + rise, solid=True)
        for side in (-1, 1):
            a, b = base + tang * 1.4 * side, top + tang * 1.4 * side
            tri = np.array([[*a, z0], [*b, z0], [*b, z0 + rise]])
            geo.tri_quad("om_planks_dark", np.array([tri[0], tri[1], tri[2], tri[2]]), [*(tang * side), 0],
                         np.zeros((4, 2), np.float32), solid=False)
        f = base - out * 0.3
        sign(geo, signs, f"om_plate_ramp_{k}", textures.plate(f"{deg} degrees"), (f[0], f[1], z0 + 0.5), facing(EDITOR), 1.2, 0.3)
    tile(geo, "editor", rc + tang * 7.2 - out * 1.5, out, z0)


def devkit(geo, signs):
    """The devkit desk: an old monitor with the dead paths on its screen."""
    z0, c, n, t, out = room_exhibit(DEVLOG)
    d = np.array([-n[1], n[0]])
    geo.oriented_box("om_white", c - d * 1.6, c + d * 1.6, n, 1.6, z0, z0 + 1.1)
    geo.oriented_box("om_gold", c - d * 1.7, c + d * 1.7, n, 1.7, z0 + 1.1, z0 + 1.22, solid=False)
    z = z0 + 1.22
    geo.oriented_box("om_planks_dark", c - d * 0.9, c + d * 0.9, n, 1.3, z, z + 1.4)
    s = c + n * 0.67
    sign(geo, signs, "om_screen_devkit", textures.screen(["host0:z:/i5/levels/", "  level%d/...", "",
                                                           "cdrom0:\\CODE\\I5\\", "  PARAM.TXT;1", "",
                                                           "file not found_"]),
         (s[0], s[1], z + 0.75), yaw(n), 1.5, 1.1)
    tile(geo, "devlog", t, -n, z0)


def thanks(geo, signs):
    """Door 21/30: a closed door in the outer wall's inside, right of the windows; it opens at 3 AM."""
    z0, out, tang, rc = frame_of(THANKS)
    d = mid(THANKS[1], R_OUT - WALL / 2 - 0.1) - tang * 6.2
    geo.oriented_box("om_planks_dark", d - tang * 1.3, d + tang * 1.3, out, 0.2, z0, z0 + 4.0, solid=False)
    geo.oriented_box("om_gold", d - tang * 1.5, d + tang * 1.5, out, 0.25, z0 + 4.0, z0 + 4.2, solid=False)
    c = d - out * 0.15
    sign(geo, signs, "om_plate_door21", textures.plate("LEVEL21.WAD", "level 30"), (c[0], c[1], z0 + 3.2),
         facing(THANKS), 1.6, 0.4)
    door = d - out * 1.1
    ZONES["door21"] = (float(door[0]), float(door[1]), float(z0), 1.4)
    tile(geo, "door21", d - out * 3.0, out, z0)
    # Where the door leads: the tower's secret room.
    tx, ty = house.TOWER
    ZONES["archive"] = (float(tx - 2.0), float(ty - 2.0), float(house.TOWER_TOP + 0.5), 0.0)


def cut_content(geo, signs):
    """Things cut from the game, round the fountain: each on a plinth, between the ways to the rooms."""
    items = [("cut_revolverator", None), ("cut_e3", textures.renames([("Talos", "Rilgar"),
                                                                       ("Inflato-Ratchet Glove", "Decoy Glove"),
                                                                       ("Inflato R", "still in the I-5 menu")])),
             ("cut_mackerel", textures.card("Mackerel 1000", [], big="><(((('>")),
             ("cut_messages", textures.code_card([("The game's printf", ["printf(const char *fmt, ...)", "{",
                                                                                 "    /* nothing */", "}"])]))]
    for k, (key, picture) in enumerate(items):
        a = 45 * (2 * k + 1) + 22.5
        p = C2 + ang(a) * 9.0
        n = ang(a)                                 # facing out, towards the rooms' doors
        house.plinth(geo, p, F0, 0, ornament=False)
        if picture is not None:
            screen_stand(geo, signs, f"om_scr_{key}", picture, p - n * 0.2, n, F0 + 1.22, 2.4, 1.5, bottom=0.25)
        tile(geo, key, p + n * 2.2, -n, F0)


C2 = np.array([CX, CY])


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
    for k in range(4):
        a = 45 * (2 * k + 1) + 22.5
        if k == 0:
            p = C2 + ang(a) * 9.0
            out.append({"class": 164, "level": CLASSES[164][0], "name": CLASSES[164][1],
                        "at": [float(p[0]), float(p[1]), F0 + 1.22 + 0.9], "yaw": a + 90.0, "scale": 2.5, "look": True})
    return out


def crates():
    """Our own copies of Pokitaru's objects for the exhibits: (kind, position, yaw)."""
    return []
