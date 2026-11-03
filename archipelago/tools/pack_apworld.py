#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Packs the APWorld: the apworld/rac1 folder into rac1.apworld, the zip Archipelago installs (its
# folder rac1/ inside). The manifest gets the container fields Archipelago's own "Build APWorlds"
# adds (version, compatible_version); the source manifest must not have them.
#
#   pack_apworld.py <apworld/rac1> <out/rac1.apworld>
import json
import pathlib
import sys
import zipfile

# Archipelago's APWorldContainer: the version of the .apworld format it writes and reads.
CONTAINER_VERSION = 7


def main() -> int:
    source, out = (pathlib.Path(a) for a in sys.argv[1:3])
    manifest = json.loads((source / "archipelago.json").read_text(encoding="utf-8"))
    manifest["version"] = CONTAINER_VERSION
    manifest["compatible_version"] = CONTAINER_VERSION
    out.parent.mkdir(parents=True, exist_ok=True)

    # A fixed time for every entry, so the same sources give the same bytes.
    def add(z: zipfile.ZipFile, name: str, text: bytes) -> None:
        info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, text)

    with zipfile.ZipFile(out, "w") as z:
        for path in sorted(source.rglob("*")):
            relative = path.relative_to(source).as_posix()
            if not path.is_file() or "__pycache__" in path.parts or relative == "archipelago.json":
                continue
            add(z, "rac1/" + relative, path.read_bytes())
        add(z, "rac1/archipelago.json", json.dumps(manifest, indent=1).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
