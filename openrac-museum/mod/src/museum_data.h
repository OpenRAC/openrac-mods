/* SPDX-License-Identifier: GPL-3.0-or-later
 * Written by tools/mod_data.py from tools/exhibits.py: do not edit by hand. */
#ifndef MUSEUM_DATA_H
#define MUSEUM_DATA_H

#include "museum_types.h"

#define MUSEUM_LINES_MAX 5

static const museum_station MUSEUM_STATIONS[] = {
    {"launcher", 643.00f, 523.40f, 231.00f, "The launcher", 5, {"OpenRAC never downloads the game. The", "launcher reads Ratchet and Clank from your", "own disc and builds it for your computer,", "once. Your saves, mods and crash reports", "live here too."}},
    {"renderer", 633.86f, 533.86f, 231.00f, "From the PS2 to your GPU", 4, {"The PS2 drew with its Graphics Synthesizer.", "OpenRAC takes the very draw lists the game", "makes and draws them with OpenGL or Vulkan,", "so the picture matches the PS2."}},
    {"hostgen", 620.00f, 541.40f, 231.00f, "Hostgen", 5, {"The decomp team writes the game's code as C.", "Hostgen turns that C into C for your", "computer: each PS2 address becomes a place", "in OpenRAC's copy of the PS2's memory. The", "screen shows one function, before and after."}},
    {"matching", 598.60f, 520.00f, 231.00f, "Byte for byte", 4, {"Matching means writing C that the original", "compiler turns into the very bytes of the", "game. When they match, the C is proven", "right."}},
    {"mods", 635.13f, 504.87f, 231.00f, "Mods", 4, {"Mods change the game and never touch your", "disc. They are built on the OpenRAC SDK, and", "the launcher adds them from a link or a", "file. This museum is a mod too."}},
    {"speedrun", 633.86f, 533.86f, 238.50f, "Every bug kept", 4, {"OpenRAC keeps the game's logic exactly as it", "was, bugs included, so every trick", "speedrunners use still works. About eight", "minutes of the PS2 record is loading."}},
    {"cutscenes", 598.60f, 520.00f, 238.50f, "Cutscenes", 4, {"OpenRAC plays the game's cutscenes as they", "are. OpenRAC Editor writes new ones: a", "camera, actors and lines in one text file,", "as on the screen."}},
    {"multi", 641.40f, 520.00f, 238.50f, "Co-op", 3, {"Ratchet and Clank was made for one player.", "OpenRAC's co-op puts two players in one", "galaxy, each with their own Ratchet."}},
    {"thanks", 635.13f, 504.87f, 238.50f, "Thank you", 3, {"OpenRAC is made by the people on this wall,", "and by the decomp team before them. Every", "name is from the projects' own history."}},
    {"welcome", 616.60f, 497.00f, 231.00f, "Welcome", 4, {"This museum shows how OpenRAC runs Ratchet", "and Clank on your computer, one part in each", "room. Step on a gold tile to read about what", "you see."}},
    {"port", 604.87f, 535.13f, 231.00f, "I-5", 5, {"Insomniac's own debug menu is still in every", "level. Its title is I-5, the game's project", "code. No button opens it in the shop game.", "Stand on the teal pad and the museum mod", "opens it. Select leaves it."}},
    {"decomp", 607.98f, 507.98f, 231.00f, "The 20-button codes", 4, {"Found in the code: 20-button codes typed in", "the pause menu. 91 of them give items,", "planets and skill points, and what a code", "gives is saved with your game."}},
    {"enhanced", 620.00f, 541.40f, 238.50f, "Effects", 5, {"Stand on the teal pad: the museum mod starts", "the game's own sparks, smoke and bursts in", "the case. Enhanced graphics adds light and", "sharper pictures. The base game stays as it", "was."}},
    {"android", 604.87f, 535.13f, 238.50f, "In your pocket", 4, {"OpenRAC runs on Android phones and", "handhelds, with a gamepad or touch buttons.", "The same code builds for Windows, macOS and", "Linux."}},
    {"editor", 609.89f, 499.71f, 238.50f, "The test room", 5, {"Going Commando's museum had a room like", "this. Blocks 1, 2, 3 and 4 units high, and", "ramps of 20, 35 and 50 degrees: try which", "ones Ratchet can climb. OpenRAC Editor built", "this island."}},
    {"devlog", 620.00f, 498.60f, 238.50f, "The devkit", 3, {"The game still asks for files on the", "developers' PC, at host0:z:/i5/levels/. On a", "shop PS2 these paths lead nowhere."}},
    {"door21", 635.84f, 495.39f, 238.50f, "Door 21/30", 4, {"Going Commando's Insomniac Museum is its", "level 30, stored as LEVEL21.WAD. This door", "opens only between 3 and 4 in the morning,", "on your own clock."}},
    {"cut_revolverator", 624.29f, 530.35f, 231.00f, "The Revolverator", 4, {"Cut from the game: a drill that spun enemies", "over Ratchet's head. Its model is still on", "the disc. The Revolverator mod for OpenRAC", "brings it back."}},
    {"cut_e3", 609.65f, 524.29f, 231.00f, "The E3 demo", 4, {"At E3 in 2002 the game was not finished.", "Rilgar was called Talos, and the Decoy Glove", "was the Inflato-Ratchet Glove. The I-5 menu", "still calls it Inflato R."}},
    {"cut_mackerel", 615.71f, 509.65f, 231.00f, "The Mackerel 1000", 3, {"A fish that was to replace the wrench. It", "was cut, and only its icon is left in the", "game's files."}},
    {"cut_messages", 630.35f, 515.71f, 231.00f, "Notes to themselves", 5, {"The lines over the doors are messages the", "developers wrote for themselves. The game", "sends them all to an empty printf, so nobody", "saw them, but the words are still on the", "disc."}},
    {"jetboard", 740.00f, 471.40f, 231.70f, "The JET-Board", 4, {"Jak II's hover board, made for Ratchet by", "RAC-Board, a mod for OpenRAC. Turn the mod", "on and press D-pad Up to ride. Hold \025 with \020", "by an edge to grind it."}},
};

static const museum_flame MUSEUM_FLAMES[] = {
    {MUSEUM_FLAME_FIRE, 394.61f, 93.49f, 233.84f},
    {MUSEUM_FLAME_FIRE, 409.83f, 479.36f, 236.79f},
    {MUSEUM_FLAME_FIRE, 420.45f, 446.27f, 249.18f},
    {MUSEUM_FLAME_FIRE, 442.33f, 423.53f, 246.98f},
    {MUSEUM_FLAME_FIRE, 453.82f, 566.17f, 228.63f},
    {MUSEUM_FLAME_FIRE, 466.82f, 427.57f, 226.29f},
    {MUSEUM_FLAME_FIRE, 471.20f, 446.50f, 226.20f},
    {MUSEUM_FLAME_FIRE, 485.85f, 559.59f, 226.22f},
    {MUSEUM_FLAME_FIRE, 498.32f, 323.41f, 239.79f},
    {MUSEUM_FLAME_FIRE, 499.44f, 341.57f, 240.45f},
    {MUSEUM_FLAME_FIRE, 501.10f, 432.92f, 231.22f},
    {MUSEUM_FLAME_FIRE, 509.86f, 591.42f, 226.23f},
    {MUSEUM_FLAME_FIRE, 521.16f, 571.66f, 231.14f},
    {MUSEUM_FLAME_FIRE, 531.01f, 419.13f, 231.54f},
    {MUSEUM_FLAME_FIRE, 578.26f, 423.87f, 230.93f},
    {MUSEUM_FLAME_FIRE, 590.99f, 143.62f, 227.20f},
    {MUSEUM_FLAME_FIRE, 613.13f, 134.74f, 227.12f},
    {MUSEUM_FLAME_FIRE, 634.74f, 346.70f, 226.03f},
    {MUSEUM_FLAME_FIRE, 683.52f, 602.42f, 226.82f},
    {MUSEUM_FLAME_FIRE, 722.54f, 698.62f, 285.00f},
    {MUSEUM_FLAME_FIRE, 751.40f, 712.40f, 327.31f},
    {MUSEUM_FLAME_FIRE, 757.83f, 427.72f, 231.11f},
    {MUSEUM_FLAME_FIRE, 772.89f, 721.70f, 328.46f},
    {MUSEUM_FLAME_FIRE, 779.77f, 61.39f, 228.51f},
    {MUSEUM_FLAME_FIRE, 794.15f, 425.74f, 230.81f},
    {MUSEUM_FLAME_FIRE, 803.34f, 401.75f, 226.78f},
    {MUSEUM_FLAME_FIRE, 805.63f, 583.72f, 257.61f},
    {MUSEUM_FLAME_FIRE, 807.60f, 103.68f, 229.13f},
    {MUSEUM_FLAME_FIRE, 813.49f, 483.11f, 226.20f},
    {MUSEUM_FLAME_FIRE, 817.16f, 634.01f, 294.31f},
    {MUSEUM_FLAME_FIRE, 817.94f, 423.62f, 245.41f},
    {MUSEUM_FLAME_FIRE, 832.88f, 623.58f, 294.46f},
    {MUSEUM_FLAME_FIRE, 850.23f, 555.93f, 271.86f},
    {MUSEUM_FLAME_FIRE, 876.18f, 739.38f, 309.39f},
    {MUSEUM_FLAME_FIRE, 1017.93f, 677.57f, 234.94f},
    {MUSEUM_FLAME_MIST, 553.80f, 644.50f, 223.60f},
    {MUSEUM_FLAME_MIST, 674.32f, 654.44f, 223.60f},
    {MUSEUM_FLAME_TORCH, 579.20f, 503.10f, 235.50f},
    {MUSEUM_FLAME_TORCH, 579.20f, 536.90f, 235.50f},
    {MUSEUM_FLAME_TORCH, 579.40f, 517.40f, 242.20f},
    {MUSEUM_FLAME_TORCH, 579.40f, 522.60f, 242.20f},
    {MUSEUM_FLAME_TORCH, 603.10f, 479.20f, 235.50f},
    {MUSEUM_FLAME_TORCH, 603.10f, 560.80f, 235.50f},
    {MUSEUM_FLAME_TORCH, 608.30f, 418.50f, 235.20f},
    {MUSEUM_FLAME_TORCH, 631.70f, 418.50f, 235.20f},
    {MUSEUM_FLAME_TORCH, 636.90f, 479.20f, 235.50f},
    {MUSEUM_FLAME_TORCH, 636.90f, 560.80f, 235.50f},
    {MUSEUM_FLAME_TORCH, 660.80f, 503.10f, 235.50f},
    {MUSEUM_FLAME_TORCH, 660.80f, 536.90f, 235.50f},
};

static const museum_zone MUSEUM_ZONE_ARCHIVE = {642.00f, 542.00f, 268.50f, 0.00f};
static const museum_zone MUSEUM_ZONE_CODES = {603.74f, 503.74f, 231.00f, 12.00f};
static const museum_zone MUSEUM_ZONE_DOOR21 = {637.18f, 494.05f, 238.50f, 1.40f};
static const museum_zone MUSEUM_ZONE_EFFECTS = {620.00f, 544.00f, 238.62f, 1.10f};
static const museum_zone MUSEUM_ZONE_EFFECTS_CASE = {620.00f, 546.60f, 238.98f, 1.20f};
static const museum_zone MUSEUM_ZONE_I5 = {602.82f, 537.18f, 231.12f, 1.20f};

#endif
