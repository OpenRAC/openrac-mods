"""Turns a fresh Pokitaru project (level 11, Europe disc) into Open Museum.

    python3 install.py PROJECTS [--editor OPENRAC_EDITOR_CHECKOUT]

PROJECTS is the folder of level projects the launcher made (Extract the levels): Open Museum is
written into PROJECTS/level_11, after a backup of its scene (.openrac-backups/open-museum). Aridia's palms (PROJECTS/level_02) and
Pokitaru's own plants, rocks, lamps and gadget targets come from your disc; the island, the house,
the signs and every other model are drawn by these scripts.

Then open level 11 in OpenRAC Editor, and Add as a new planet in the launcher (the planet's name is
set here: Open Museum).
"""

import argparse
import json
import math
import os
import re
import shutil
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import island  # noqa: E402
import landmarks  # noqa: E402
import make_museum  # noqa: E402
import scatter  # noqa: E402

LEVEL = 11
PLANET = "Open Museum"
FOLDER = "open_museum"     # inside the project: res://open_museum/

# Pokitaru's mobys kept as they are: Ratchet, the vendor, the ship's landing helper and the ocean.
KEEP_CLASSES = {0, 11, 832, 1111}
# Pokitaru mobys to copy: one of each class is kept as the model for the copies.
TEMPLATES = {"gold_bolt": 1134, "bolt_crate": 500, "swing_target": 758, "swing_swinging": 803,
             "lamp": 1859, "nanotech_crate": 501, "ammo_crate": 511}


class Scene:
    """A Godot .tscn as its header, its resources and its node sections, in order."""

    def __init__(self, text):
        parts = re.split(r"(?m)^(?=\[)", text)
        self.header = parts[0] + (parts[1] if parts[1].startswith("[gd_scene") else "")
        self.sections = parts[2:] if parts[1].startswith("[gd_scene") else parts[1:]

    def text(self):
        return self.header + "".join(s if s.endswith("\n\n") else s.rstrip("\n") + "\n\n" for s in self.sections)

    @staticmethod
    def attr(section, key):
        m = re.match(r'\[[^\]]*\b' + key + r'="([^"]*)"', section)
        return m.group(1) if m else None

    def nodes(self):
        for i, s in enumerate(self.sections):
            if s.startswith("[node "):
                yield i, s

    def path_of(self, s):
        name, parent = self.attr(s, "name"), self.attr(s, "parent")
        if parent in (None, "."):
            return name if parent == "." else ""
        return f"{parent}/{name}"


def transform(x, y, z, yaw_deg=0.0, scale=1.0, basis=None):
    if basis is None:
        c, s = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
        basis = [c * scale, s * scale, 0, -s * scale, c * scale, 0, 0, 0, scale]
    return "Transform3D(" + ", ".join(f"{v:.6g}" for v in list(basis) + [x, y, z]) + ")"


def basis_of(section):
    m = re.search(r"^transform = Transform3D\(([^)]*)\)", section, re.M)
    if not m:
        return [1, 0, 0, 0, 1, 0, 0, 0, 1]
    return [float(v) for v in m.group(1).split(",")][:9]


def turned(basis, yaw_deg, scale=1.0):
    """BASIS (three columns) turned about z by YAW_DEG and scaled."""
    c, s = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
    out = []
    for k in range(3):
        x, y, z = basis[3 * k:3 * k + 3]
        out += [(c * x - s * y) * scale, (s * x + c * y) * scale, z * scale]
    return out


def copy_node(section, name, xform):
    out = re.sub(r'name="[^"]*"', f'name="{name}"', section, count=1)
    if re.search(r"^transform = ", out, re.M):
        out = re.sub(r"^transform = Transform3D\([^)]*\)", "transform = " + xform, out, count=1, flags=re.M)
    else:
        out = out.replace("]\n", "]\ntransform = " + xform + "\n", 1)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("projects", type=Path)
    parser.add_argument("--editor", type=Path, default=None,
                        help="OpenRAC Editor's checkout or install (its openrac/tools); default: beside OpenRAC")
    args = parser.parse_args()
    project = args.projects / f"level_{LEVEL:02}"
    level_dir = project / "levels" / f"level_{LEVEL:02}"
    scene_path = level_dir / f"level_{LEVEL:02}.tscn"
    if not scene_path.is_file():
        raise SystemExit(f"{scene_path} not found: extract the levels first (Mods > Level editor)")
    tools = (args.editor / "openrac" / "tools") if args.editor else None
    if tools is None:
        for candidate in (HERE.parents[2] / "OpenRAC-Editor", HERE.parents[2] / "openrac-editor"):
            if (candidate / "openrac" / "tools").is_dir():
                tools = candidate / "openrac" / "tools"
    if tools is None or not (tools / "static_assets.py").is_file():
        raise SystemExit("OpenRAC Editor's tools not found: pass --editor")
    sys.path.insert(0, str(tools))
    import static_assets  # noqa: E402

    # In a folder of its own: the editor's import rotates the level_11-*.tscn files next to it.
    backup = project / ".openrac-backups" / "open-museum" / "level_11-pokitaru.tscn"
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copyfile(scene_path, backup)
    original_terrain = level_dir / "terrain-pokitaru.glb"
    if not original_terrain.exists():
        shutil.copyfile(level_dir / "terrain.glb", original_terrain)
    # Always start from Pokitaru as extracted, so running this again gives the same museum.
    scene_path.write_text(backup.read_text(encoding="utf-8"), encoding="utf-8", newline="\n")

    # 1. Our own models, collision and terrain.
    built = project / FOLDER
    if built.exists():
        shutil.rmtree(built)
    make_museum.main(built, level_dir / "textures")
    shutil.copyfile(built / "terrain.glb", level_dir / "terrain.glb")
    (level_dir / "textures").mkdir(exist_ok=True)
    for png in (built / "textures").glob("*.png"):
        shutil.copyfile(png, level_dir / "textures" / png.name)
    layout = json.loads((built / "layout.json").read_text(encoding="utf-8"))

    # 2. Static models: the house, the landmarks, and Aridia's palm (from your disc).
    cx, cy = island.CENTRE
    static_assets.import_asset(built / "models" / "house.glb", project, LEVEL, "Museum", [0, 0, 0])
    static_assets.import_asset(built / "models" / "landmarks.glb", project, LEVEL, "Landmarks", [0, 0, 0])
    palm_source = args.projects / "level_02" / "levels" / "level_02" / "ties" / "tie_586.glb"
    palms = scatter.palms()
    if palm_source.is_file():
        static_assets.import_asset(palm_source, project, LEVEL, "Palm_000", palms[0][:3])

    scene = Scene(scene_path.read_text(encoding="utf-8"))

    # 3. Remove Pokitaru: its ties and shrubs (but one of each kept as a model), its collision,
    #    its mobys (but the kept classes and one of each template).
    ext = {}
    for s in scene.sections:
        if s.startswith("[ext_resource"):
            ext[Scene.attr(s, "id")] = Scene.attr(s, "path")
    template_nodes, tie_models, shrub_models = {}, {}, {}
    keep_ties = set(scatter.TIES.values()) | set(scatter.TOWERS)
    keep_shrubs = set(scatter.SHRUBS.values())
    drop = set()
    for i, s in scene.nodes():
        path = scene.path_of(s)
        parent = Scene.attr(s, "parent")
        inst = re.search(r'instance=ExtResource\("([^"]+)"\)', s.split("\n")[0])
        res = ext.get(inst.group(1), "") if inst else ""
        if parent == "Game/Ties":
            number = int(re.search(r"tie_(\d+)\.glb", res).group(1)) if "tie_" in res else -1
            if number in keep_ties and number not in tie_models:
                tie_models[number] = s
            drop.add(i)
        elif parent == "Game/Shrubs":
            number = int(re.search(r"shrub_(\d+)\.glb", res).group(1)) if "shrub_" in res else -1
            if number in keep_shrubs and number not in shrub_models:
                shrub_models[number] = s
            drop.add(i)
        elif parent == "Game/Collision":
            drop.add(i)
        elif parent == "Game/Mobys":
            m = re.search(r"moby_(\d+)", inst.group(1)) if inst else None
            cls = int(m.group(1)) if m else -1
            if cls in KEEP_CLASSES:
                continue
            for kind, tcls in TEMPLATES.items():
                group = re.search(r"^metadata/rc1_group = (-?\d+)", s, re.M)
                ungrouped = group is None or group.group(1) == "-1"
                if tcls == cls and (kind not in template_nodes or (ungrouped and template_nodes[kind][1] is False)):
                    template_nodes[kind] = (s, ungrouped)
            drop.add(i)
        elif path.startswith("Game/MobyGroups"):
            pass
    sections = [s for i, s in enumerate(scene.sections) if i not in drop]

    # The groups named the deleted mobys: keep only the names still in the scene.
    names = {Scene.attr(s, "name") for s in sections if s.startswith("[node ")}
    names |= {Scene.attr(t[0], "name") for t in template_nodes.values()}

    def tidy_groups(s):
        if not s.startswith('[node name="MobyGroups"'):
            return s
        lines = []
        for line in s.split("\n"):
            m = re.match(r"^(metadata/rc1_group_\d+) = \[(.*)\]$", line)
            if m:
                kept = [n for n in re.findall(r'"([^"]+)"', m.group(2)) if n in names]
                line = f"{m.group(1)} = [" + ", ".join(f'"{n}"' for n in kept) + "]"
            lines.append(line)
        return "\n".join(lines)
    sections = [tidy_groups(s) for s in sections]

    new_nodes, new_subs, new_ext = [], [], []

    # 4. The templates themselves stay (moved under the sea, out of sight): the copies take after them.
    hidden = (island.CENTRE[0] - 380, island.CENTRE[1] - 380, island.SEA - 30)
    for kind, (s, _) in template_nodes.items():
        new_nodes.append(copy_node(s, Scene.attr(s, "name"), transform(*hidden, basis=basis_of(s))))

    # 5. Copies of the game's own objects.
    counts = {}
    for m in layout["mobys"]:
        kind = m["kind"]
        if kind not in template_nodes:
            print(f"no {kind} in Pokitaru to copy: skipped")
            continue
        s = template_nodes[kind][0]
        counts[kind] = counts.get(kind, 0) + 1
        name = f"OM_{''.join(w.title() for w in kind.split('_'))}_{counts[kind]:03}"
        x, y, z = m["position"]
        new_nodes.append(copy_node(s, name, transform(x, y, z, basis=turned(basis_of(s), m["yaw"]))))
    if "lamp" in template_nodes:
        s = template_nodes["lamp"][0]
        for k, (x, y, z) in enumerate(layout["lamps"]):
            new_nodes.append(copy_node(s, f"OM_Lamp_{k + 1:03}", transform(x, y, z, basis=basis_of(s))))

    # 6. Pokitaru's plants and rocks, scattered on the island.
    tie_count = shrub_count = 0
    for kind, number, x, y, z, yaw, scale in scatter.plants() + list(scatter.sea_islets()) + scatter.town() + scatter.decor():
        model = (tie_models if kind == "tie" else shrub_models).get(number)
        if model is None:
            continue
        if kind == "tie":
            tie_count += 1
            name = f"OM_Tie_{tie_count:04}"
        else:
            shrub_count += 1
            name = f"OM_Shrub_{shrub_count:04}"
        new_nodes.append(copy_node(model, name, transform(x, y, z, basis=turned(basis_of(model), yaw, scale))))
    # The models themselves are not kept where Pokitaru had them.
    # (Their copies keep the model's index, so they pack as copies of it; the model node goes.)

    # 7. Our collision.
    for name in ("ground", "house"):
        rid = f"om_collision_{name}"
        new_ext.append(f'[ext_resource type="PackedScene" path="res://{FOLDER}/collision/{name}.glb" id="{rid}"]\n\n')
        new_nodes.append(f'[node name="OM_{name.title()}" parent="Game/Collision" instance=ExtResource("{rid}")]\n'
                         f'metadata/rc1_surface = 31\n\n')

    # 8. The animated props: the waterfalls flow, the foam turns, the logo turns.
    anim = [("logo", layout["animated"]["logo"]["position"], dict(spin_z=1.0, spin_speed=0.6))]
    for k in range(len(island.FALLS)):
        anim.append((f"falls_{k}", [0, 0, 0], dict(spin_z=1.0, spin_speed=0.0, move_z=-1.0,
                                                   move_distance=1.2, move_speed=2.5 + k * 0.4)))
        anim.append((f"foam_{k}", [0, 0, 0], dict(spin_z=1.0, spin_speed=0.0, move_z=1.0,
                                                  move_distance=0.15, move_speed=1.5)))
    new_nodes.append('[node name="AnimatedAssets" type="Node3D" parent="Game"]\n\n')
    for name, at, meta in anim:
        rid = f"om_anim_{name}"
        new_ext.append(f'[ext_resource type="PackedScene" path="res://{FOLDER}/models/{name}.glb" id="{rid}"]\n\n')
        body = f'[node name="OM_{name.title().replace("_", "")}" parent="Game/AnimatedAssets" instance=ExtResource("{rid}")]\n'
        body += "transform = " + transform(*at) + "\n"
        full = dict(spin_x=0.0, spin_y=0.0, spin_z=1.0, spin_speed=1.0, spin_phase=0.0, move_x=0.0, move_y=0.0,
                    move_z=0.0, move_distance=0.0, move_speed=1.0, move_phase=0.0)
        full.update(meta)
        body += "".join(f"metadata/rc1_{k} = {float(v)}\n" for k, v in full.items()) + "\n"
        new_nodes.append(body)

    # The house and landmarks were placed at the origin by import_asset: their vertices are in place.
    # More palms: copies of the imported palm node.
    palm_node = next((s for s in sections if s.startswith('[node name="Palm_000"')), None)
    if palm_node is not None:
        for k, (x, y, z, yaw, scale) in enumerate(palms[1:], 1):
            new_nodes.append(copy_node(palm_node, f"Palm_{k:03}", transform(x, y, z, yaw, scale)))
        i = sections.index(palm_node)
        x, y, z, yaw, scale = palms[0]
        sections[i] = copy_node(palm_node, "Palm_000", transform(x, y, z, yaw, scale))

    # 9. The grind rails.
    for k, rail in enumerate(layout["grind_paths"]):
        pts = resample(rail["points"], 0.5)
        sid = f"om_grind_{k}"
        flat = ", ".join(f"0, 0, 0, 0, 0, 0, {p[0]:.4f}, {p[1]:.4f}, {p[2]:.4f}" for p in pts)
        new_subs.append(f'[sub_resource type="Curve3D" id="{sid}"]\n_data = {{\n"points": PackedVector3Array({flat}),\n'
                        f'"tilts": PackedFloat32Array({", ".join("0" for _ in pts)})\n}}\npoint_count = {len(pts)}\n\n')
        w = [math.dist(a, b) for a, b in zip(pts, pts[1:])]
        w.append(w[-1])
        new_nodes.append(f'[node name="OM_Grind_{rail["name"]}" type="Path3D" parent="Game/GrindPaths"]\n'
                         f'curve = SubResource("{sid}")\nmetadata/rc1_w = [{", ".join(f"{v:.5f}" for v in w)}]\n'
                         f'metadata/rc1_flag = 0\n\n')

    # Put the resources before the first sub_resource/node, the nodes at the end.
    first = next(i for i, s in enumerate(sections) if s.startswith("[sub_resource") or s.startswith("[node "))
    first_node = next(i for i, s in enumerate(sections) if s.startswith("[node "))
    sections = sections[:first] + new_ext + sections[first:first_node] + new_subs + sections[first_node:] + new_nodes
    scene.sections = sections
    text = scene.text()
    # Drop ext_resources no node uses any more (Pokitaru's ties, shrubs, tiles and moby classes).
    used = set(re.findall(r'ExtResource\("([^"]+)"\)', text))
    keep_ids = used
    out = []
    for s in re.split(r"(?m)^(?=\[)", text):
        if s.startswith("[ext_resource"):
            rid = Scene.attr(s, "id")
            if rid not in keep_ids:
                continue
        out.append(s)
    text = "".join(out)
    scene_path.write_text(text, encoding="utf-8", newline="\n")

    set_planet(project)
    print(f"Open Museum written into {project}: {len(layout['mobys'])} objects, {tie_count} rocks, "
          f"{shrub_count} plants, {len(palms)} palms, {len(layout['grind_paths'])} grind rails.")


def resample(points, step):
    pts = [np.asarray(p, np.float64) for p in points]
    out = [pts[0]]
    carry = 0.0
    for a, b in zip(pts, pts[1:]):
        seg = np.linalg.norm(b - a)
        if seg == 0:
            continue
        t = step - carry
        while t <= seg:
            out.append(a + (b - a) * (t / seg))
            t += step
        carry = seg - (t - step)
    if np.linalg.norm(out[-1] - pts[-1]) > 1e-3:
        out.append(pts[-1])
    return [list(map(float, p)) for p in out]


def set_planet(project):
    godot = project / "project.godot"
    text = godot.read_text(encoding="utf-8")
    text = re.sub(r'(?m)^planet/name=.*\n', "", text)
    if "[openrac]" not in text:
        text = text.rstrip("\n") + "\n\n[openrac]\n"
    text = text.replace("[openrac]\n", f'[openrac]\n\nplanet/name="{PLANET}"\n', 1)
    godot.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
