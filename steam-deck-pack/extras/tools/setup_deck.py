#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Create an isolated native SteamOS shortcut from an existing, extracted RAC1 install."""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import tempfile


PROFILES = {
    "balanced": {
        "display_mode": "borderless", "window_size": "1280x800", "aspect": "16:10",
        "hud": "centre", "render_scale": "100", "msaa": "1", "vsync": "on",
        "frame_rate": "60", "motion_blur": "0",
    },
    "battery": {
        "display_mode": "borderless", "window_size": "1280x800", "aspect": "16:10",
        "hud": "centre", "render_scale": "75", "msaa": "1", "vsync": "on",
        "frame_rate": "40", "motion_blur": "0", "enhanced_graphics": "0", "gtao": "0",
    },
}
MOD_ID = "steam-deck-pack"


def safe_path(value: Path) -> Path:
    path = value.expanduser().resolve()
    if any(ord(char) < 32 for char in str(path)):
        raise ValueError("Paths must not contain control characters")
    return path


def validate_binary(path: Path, executable: bool = False) -> None:
    with path.open("rb") as source:
        header = source.read(20)
    # ELFCLASS64, little-endian, x86-64 (Steam Deck / native SteamOS).
    if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\x00":
        raise ValueError(f"Not a Linux x86-64 ELF binary: {path}")
    if executable and not os.access(path, os.X_OK):
        raise ValueError(f"Game binary is not executable: {path}")


def overlay_settings(original: str, updates: dict[str, str]) -> str:
    # Keep all unrelated preferences (especially language/audio) and remove duplicate changed keys.
    lines = []
    for line in original.splitlines():
        key = line.split("=", 1)[0].strip() if "=" in line and not line.lstrip().startswith("#") else None
        if key not in updates:
            lines.append(line)
    lines.append("# Steam Deck Pack starting profile; edit in the game's Video menu.")
    lines.extend(f"{key} = {value}" for key, value in updates.items())
    return "\n".join(lines) + "\n"


def desktop_quote(value: str) -> str:
    # Desktop Entry Exec has its own quoting rules, separate from a shell's.
    value = value.replace("%", "%%")
    for char in ('\\', '"', '`', '$'):
        value = value.replace(char, "\\" + char)
    # The .desktop file's general string-escape layer is decoded before Exec quoting.
    return '"' + value.replace("\\", "\\\\") + '"'


def plan(game: Path, data: Path, package: Path, output: Path, version: str,
         settings: Path | None = None, disable_keyboard_mouse: bool = False) -> dict:
    game, data, package, output = map(safe_path, (game, data, package, output))
    if output.exists():
        raise ValueError(f"Output already exists; choose a new folder: {output}")
    try:
        output.relative_to(package)
    except ValueError:
        pass
    else:
        raise ValueError("Output must be outside the package folder")
    if any(part.startswith(".mount_") for part in game.parts):
        raise ValueError("Game is inside a temporary AppImage mount; extract it to a permanent folder first")
    validate_binary(game, executable=True)
    levels = data / "port" / version
    if not (levels / "level_99" / "manifest.json").is_file():
        if version == "pal" and (data / "port/level_99/manifest.json").is_file():
            levels = data / "port"
        else:
            raise ValueError("Extract and check the game's levels in the launcher first")
    if not (data / "iso_data/rac1/disc.iso").is_file():
        raise ValueError("The existing installation has no iso_data/rac1/disc.iso")
    manifest = json.loads((package / "openrac-mod.json").read_text(encoding="utf-8"))
    if manifest.get("id") != MOD_ID or version not in manifest.get("games", {}).get("rac1", []):
        raise ValueError("Package is not a compatible Steam Deck Pack")
    relative = manifest.get("plugin", {}).get("linux-x86_64")
    if not relative or not isinstance(relative, str):
        raise ValueError("Package has no Linux x86-64 build; a macOS build cannot run on the Deck")
    library = (package / relative).resolve()
    try:
        library.relative_to(package)
    except ValueError:
        raise ValueError("Package library path leaves its own folder") from None
    validate_binary(library)
    source_list = data / "plugins/plugins.json"
    plugins = json.loads(source_list.read_text(encoding="utf-8")) if source_list.exists() else {"format": 1, "mods": []}
    if plugins.get("format") != 1 or not isinstance(plugins.get("mods"), list):
        raise ValueError("Unsupported existing plugins.json")
    plugins = copy.deepcopy(plugins)
    keep, seen = [], set()
    for entry in plugins["mods"]:
        if not isinstance(entry, dict) or not re.fullmatch(r"[a-z0-9][a-z0-9-]*", str(entry.get("id", ""))):
            raise ValueError("Invalid installed mod ID")
        if entry["id"] in seen:
            raise ValueError("Duplicate installed mod ID")
        seen.add(entry["id"])
        if entry["id"] == MOD_ID:
            continue
        if entry["id"] == "keyboard-mouse" and entry.get("enabled", True):
            if not disable_keyboard_mouse:
                raise ValueError("keyboard-mouse also owns the mouse. Use --disable-keyboard-mouse to disable it only in the pack's isolated mod list")
            entry["enabled"] = False
        if entry["id"] == "xbox-icons":
            # Built into the pack now; disable only this isolated copy of the legacy entry.
            entry["enabled"] = False
        if entry.get("enabled", True) and not (source_list.parent / entry["id"] / "openrac-mod.json").is_file():
            raise ValueError(f"Enabled mod is missing: {entry['id']}")
        keep.append(entry)
    plugins["mods"] = keep + [{"id": MOD_ID, "enabled": True, "settings": {}}]
    if settings is None:
        base = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
        settings = base / "openrac/settings" / f"rac1-{version}.ini"
    settings = safe_path(settings)
    original = settings.read_text(encoding="utf-8") if settings.exists() else ""
    return {"game": game, "data": data, "package": package, "output": output,
            "levels": levels, "plugins": plugins, "original": original, "version": version}


def install(config: dict) -> Path:
    output, data = config["output"], config["data"]
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".deck-pack-", dir=output.parent))
    try:
        (stage / "profiles").mkdir()
        (stage / "plugins").mkdir()
        for name, updates in PROFILES.items():
            (stage / "profiles" / f"{name}.ini").write_text(overlay_settings(config["original"], updates), encoding="utf-8")
        shutil.copytree(config["package"], stage / "plugins" / MOD_ID, symlinks=False)
        for entry in config["plugins"]["mods"]:
            if entry["id"] == MOD_ID:
                continue
            source = data / "plugins" / entry["id"]
            if source.is_dir():
                (stage / "plugins" / entry["id"]).symlink_to(source, target_is_directory=True)
        (stage / "plugins/plugins.json").write_text(json.dumps(config["plugins"], indent=2) + "\n", encoding="utf-8")
        argv = [str(config["game"]), "--data", str(data), "--levels", str(config["levels"]),
                "--plugins", str(output / "plugins/plugins.json")]
        mods = data / "mods" / config["version"]
        if mods.is_dir():
            argv += ["--mods", str(mods)]
        probes = data / "probes" / config["version"]
        if probes.is_dir():
            argv += ["--probes", str(probes)]
        script = ("#!/bin/sh\nset -eu\n"
                  + "PACK_DIR=" + shlex.quote(str(output)) + "\n"
                  + 'preset=${OPENRAC_DECK_PRESET:-balanced}\n'
                  + 'case "$preset" in balanced|battery) ;; *) echo "Unknown Deck preset" >&2; exit 64 ;; esac\n'
                  + "cd " + shlex.quote(str(data)) + "\n"
                  + "exec " + shlex.join(argv)
                  + ' --settings "$PACK_DIR/profiles/$preset.ini" "$@"\n')
        (stage / "start.sh").write_text(script, encoding="utf-8")
        (stage / "start.sh").chmod(0o755)
        desktop = ("[Desktop Entry]\nType=Application\nName=Ratchet & Clank 1 (Deck Pack)\n"
                   + "Exec=/bin/sh " + desktop_quote(str(output / "start.sh")) + "\n"
                   + "Icon=applications-games\nTerminal=false\nCategories=Game;\n")
        (stage / "Ratchet & Clank 1.desktop").write_text(desktop, encoding="utf-8")
        # Do not replace an existing pack or write into the original installation.
        if output.exists():
            raise ValueError("Output was created during setup; nothing replaced")
        stage.rename(output)
    except BaseException:
        shutil.rmtree(stage)
        raise
    return output / "start.sh"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, required=True, help="Existing native Linux x86-64 OpenRAC executable")
    parser.add_argument("--data", type=Path, required=True, help="Existing active/rac1/data folder")
    parser.add_argument("--package", type=Path, required=True, help="Built Steam Deck Pack directory with Linux .so")
    parser.add_argument("--output", type=Path, required=True, help="New directory for isolated pack files")
    parser.add_argument("--version", choices=("pal", "ntsc"), default="pal")
    parser.add_argument("--settings", type=Path, help="Copy language/audio and other preferences from this file")
    parser.add_argument("--disable-keyboard-mouse", action="store_true")
    parser.add_argument("--dry-run", action="store_true", help="Validate and display the plan without writing files")
    args = parser.parse_args()
    try:
        config = plan(args.game, args.data, args.package, args.output, args.version,
                      args.settings, args.disable_keyboard_mouse)
        if args.dry_run:
            print(f"Ready: {config['output']}/start.sh (16:10, balanced/battery); no files written")
        else:
            start = install(config)
            print(f"Created {start}\nAdd this file as a non-Steam game, then configure its controller layout (see README).")
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Steam Deck setup: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
