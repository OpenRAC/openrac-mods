#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs the APWorld: apworld/rac1's Python files and the table the mod's tool printed
# (dump_table.c) into rac1.apworld, the zip Archipelago installs (its folder rac1/ inside).
#
#   pack_apworld.py <apworld/rac1> <table.json> <out/rac1.apworld>
import json
import pathlib
import sys
import zipfile

# The world's manifest (Archipelago 0.6.4+ reads it from the .apworld).
WORLD_VERSION = "1.0.0"
MINIMUM_AP_VERSION = "0.6.4"


def main() -> int:
    source, table, out = (pathlib.Path(a) for a in sys.argv[1:4])
    data = json.loads(table.read_text(encoding="utf-8"))
    manifest = {
        "game": data["game"],
        "world_version": WORLD_VERSION,
        "minimum_ap_version": MINIMUM_AP_VERSION,
        "authors": ["OpenRAC contributors"],
    }
    out.parent.mkdir(parents=True, exist_ok=True)

    # A fixed time for every entry, so the same sources give the same bytes.
    def add(z: zipfile.ZipFile, name: str, text: bytes) -> None:
        info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, text)

    with zipfile.ZipFile(out, "w") as z:
        for path in sorted(source.rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts:
                add(z, "rac1/" + path.relative_to(source).as_posix(), path.read_bytes())
        add(z, "rac1/table.json", json.dumps(data, indent=1).encode("utf-8"))
        add(z, "rac1/archipelago.json", json.dumps(manifest, indent=1).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
