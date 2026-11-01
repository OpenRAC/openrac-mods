#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What a disc's levels hold of the item randomizer's table: every infobot (its level, mission slot
and planet) and every gold bolt (its level and slot), read from the levels an OpenRAC extraction
left. tests/table_test.c holds logic.c to these rows; they are numbers, no game data.

    python3 tools/disc_facts.py OPENRAC_CHECKOUT pal LEVELS_DIR ntsc LEVELS_DIR > tests/data/discs.tsv

OPENRAC_CHECKOUT is where editor/gameplay.py is; LEVELS_DIR holds level_NN/bake-inputs/gameplay.bin.
"""
import json
import struct
import sys
from pathlib import Path

CLASS_INFOBOT = 750
CLASS_GOLD_BOLT = 1134
LEVELS = 19
RECORD_MISSION = 4  # the moby record's mission slot
INFOBOT_PLANET = 4  # the infobot's pvars: the planet it unlocks
GOLD_BOLT_SLOT = 0  # the gold bolt's pvars: its slot


def rows(gameplay, disc, folder):
    for level in range(LEVELS):
        g = (folder / f"level_{level:02d}/bake-inputs/gameplay.bin").read_bytes()
        placed = json.loads((folder / f"level_{level:02d}/placements.json").read_text())["mobys"]
        records = gameplay.records(g)
        used = [m["fields"]["pvar_index"] for m in placed]
        table = gameplay.pvar_table(g, gameplay.pvar_count(g, used))
        data = gameplay.offset(g, gameplay.DATA)

        for moby in placed:
            pvar = moby["fields"]["pvar_index"]

            if moby["class"] not in (CLASS_INFOBOT, CLASS_GOLD_BOLT) or pvar < 0:
                continue

            at = data + table[pvar][0]

            if moby["class"] == CLASS_INFOBOT:
                mission = records[moby["index"]][RECORD_MISSION]
                planet = struct.unpack_from("<i", g, at + INFOBOT_PLANET)[0]
                yield f"{disc}\tinfobot\t{level}\t{mission}\t{planet}"
            else:
                slot = struct.unpack_from("<i", g, at + GOLD_BOLT_SLOT)[0]
                yield f"{disc}\tgold\t{level}\t{slot}\t0"


def main():
    sys.path.insert(0, str(Path(sys.argv[1]) / "editor"))
    import gameplay

    print("# disc\tkind\tlevel\tkey\tvalue: made by tools/disc_facts.py")

    for i in range(2, len(sys.argv), 2):
        for row in sorted(rows(gameplay, sys.argv[i], Path(sys.argv[i + 1]))):
            print(row)


if __name__ == "__main__":
    main()
