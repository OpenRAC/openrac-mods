"""Builds OpenRAC Museum as a shared map: one .openrac-map file that players add in the launcher.

    python3 build_map.py DISC OUT_FOLDER [--editor OPENRAC_EDITOR_CHECKOUT]

DISC is a Ratchet & Clank disc image (.iso). Level 11 (Pokitaru), level 2 (Aridia) and the levels
the exhibits come from are read from it into a temporary folder, the museum is written into that copy of level 11, and the copy is
exported with OpenRAC Editor's `map export`. Your own level projects are never touched.

The map holds only what these scripts made. Textures and models of the game are named by level,
path and sha256, and the player's own disc gives them back when they add the map.
OPENRAC_ROOT must point at an OpenRAC checkout (the editor's tools use its extractor).
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import exhibits  # noqa: E402
import museum  # noqa: E402

MAP_ID = "openrac-museum"
ABOUT = ("A new planet in the ship's menu: a wooden museum on a tropical island, with rooms about how "
         "OpenRAC is made. Pokitaru and every other planet stay as they are.")


def editor_tools(given):
    candidates = [given] if given else [HERE.parents[2] / "OpenRAC-Editor", HERE.parents[2] / "openrac-editor"]
    for candidate in candidates:
        if candidate and (candidate / "openrac" / "tools" / "shared_map.py").is_file():
            return (candidate / "openrac" / "tools").resolve()
    raise SystemExit("OpenRAC Editor's tools not found: pass --editor")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("disc", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--editor", type=Path, default=None, help="OpenRAC Editor's checkout or install")
    args = parser.parse_args()
    tools = editor_tools(args.editor)
    disc = args.disc.resolve()
    with tempfile.TemporaryDirectory(prefix="openrac-museum-") as temp:
        projects = Path(temp) / "levels"
        levels = sorted({museum.LEVEL, 2} | set(exhibits.LEVELS))
        subprocess.run([sys.executable, str(tools / "openrac_editor.py"), "projects", str(disc), str(projects),
                        "--split", "--anywhere"] + [a for n in levels for a in ("--level", str(n))], check=True)
        museum.write_museum(projects, tools)
        sys.path.insert(0, str(tools))
        import planet
        import shared_map
        project = projects / f"level_{museum.LEVEL:02}"
        planet.set_setting(project, planet.ID, MAP_ID)
        args.out.mkdir(parents=True, exist_ok=True)
        version = disc_version(disc)
        out = args.out.resolve() / f"openrac-museum-{version}{shared_map.SUFFIX}"
        done = shared_map.export(project, out, author="OpenRAC", about=ABOUT, disc_path=disc)
    print(f"{done['out']}: {done['scenes']} scene(s), {done['patches']} patch(es), {done['game']} game file(s) "
          f"named, {len(done['own'])} of our own files")


def disc_version(disc):
    from disc import Disc
    with Disc(disc) as d:
        return d.version.key


if __name__ == "__main__":
    main()
