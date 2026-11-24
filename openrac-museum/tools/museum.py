"""Writes OpenRAC Museum into a fresh Pokitaru project (level 11): build_map.py calls it on a copy
read from the disc, never on the player's own levels.

Aridia's palm (level_02) and Pokitaru's own plants, rocks, lamps and gadget targets come from the
disc; the island, the house, the signs and every other model are drawn by these scripts.
"""

import json
import math
import os
import re
import shutil
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import exhibits  # noqa: E402
import island  # noqa: E402
import landmarks  # noqa: E402
import museum_light  # noqa: E402
import make_museum  # noqa: E402
import scatter  # noqa: E402

LEVEL = 11
PLANET = "OpenRAC Museum"
FOLDER = "openrac_museum"     # inside the project: res://openrac_museum/

# Pokitaru's mobys kept as they are: Ratchet, the vendor, the ship's landing helper and the ocean.
KEEP_CLASSES = {0, 11, 832, 1111}
VENDOR = 11
# Pokitaru mobys to copy: one of each class is kept as the model for the copies.
TEMPLATES = {"gold_bolt": 1134, "bolt_crate": 500, "swing_target": 758, "swing_swinging": 803,
             "lamp": 1859, "nanotech_crate": 501, "ammo_crate": 511, "bolt": 13, "bolt_hexnut": 14,
             "puffoid": 1246, "psytcopus": 1231, "sandmouse_house": 604, "sandmouse": 1818}
# Enemies: their models are not kept under the sea (they would live there); only their copies are
# placed. The sandmouse stays, far off: the houses make sandmice of its class.
ENEMIES = {"puffoid", "psytcopus", "sandmouse_house"}
# The pvar word that holds an enemy's zone: a closed path it guards; it fights only while Ratchet
# is inside (psytcopus f13C, puffoid f230 in the decomp).
ZONE_WORD = {"psytcopus": 79, "puffoid": 140}


# The level's fog: a light sea haze (the sky's colour at the horizon) that starts past the island's
# middle and deepens towards the far jungle, in place of Pokitaru's dark teal. Distances in 1/1024 units.
HAZE = {"fog_colour": "[96, 160, 184]", "fog_near": "61440.0", "fog_far": "1843200.0",
        "fog_near_intensity": "255.0", "fog_far_intensity": "110.0"}


def set_pvar(section, word, value):
    def put(m):
        vals = m.group(1).split(", ")
        vals[word] = str(value)
        return "metadata/rc1_pvar = [" + ", ".join(vals) + "]"
    return re.sub(r"^metadata/rc1_pvar = \[(.*)\]$", put, section, count=1, flags=re.M)


def path_node(name, parent, curve_id, pts, meta):
    """A Path3D node and its curve through PTS (game coordinates)."""
    flat = ", ".join(f"0, 0, 0, 0, 0, 0, {x:.4f}, {y:.4f}, {z:.4f}" for x, y, z in pts)
    sub = (f'[sub_resource type="Curve3D" id="{curve_id}"]\n_data = {{\n"points": PackedVector3Array({flat}),\n'
           f'"tilts": PackedFloat32Array({", ".join("0" for _ in pts)})\n}}\npoint_count = {len(pts)}\n')
    node = f'[node name="{name}" type="Path3D" parent="{parent}"]\ncurve = SubResource("{curve_id}")\n' + meta
    return sub, node


def zone_path(index, points):
    """A closed zone (an enemy's) as a path after the level's own."""
    pts = list(points) + [points[0]]
    return path_node(f"Path_{index:04}", "Game/Paths", f"om_zone_{index}", pts,
                     f'metadata/rc1_index = {index}\nmetadata/rc1_w = [{", ".join("-1.0" for _ in pts)}]\n')


def grind_path(k, points):
    """A rail of ours in the level's grind table: Pokitaru's own hero code never grinds, but the
    RAC-Board mod's JET-Board grinds every rail in that table."""
    pts = [tuple(float(v) for v in p[:3]) for p in points]
    centre = [(min(p[a] for p in pts) + max(p[a] for p in pts)) / 2 for a in range(3)]
    radius = max(math.dist(centre, p) for p in pts) + 1.0
    sphere = ", ".join(f"{v:.4f}" for v in centre + [radius])
    return path_node(f"OM_Rail_{k:02}", "Game/GrindPaths", f"om_rail_{k}", pts,
                     f"metadata/rc1_bounding_sphere = [{sphere}]\n")


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


def light_nodes(text, layout):
    """The level's light sets and light zones (museum_light.py, measured from the bake) into the scene,
    and each placed moby's light set and colour from the zone it stands in."""
    sets, zones = layout.get("light_sets"), layout.get("light_zones")
    if not sets or not zones:
        return text
    scene = Scene(text)
    keep = []
    for s in scene.sections:
        head = s.split("\n")[0]
        if s.startswith("[node ") and (Scene.attr(s, "name") in ("LightSets", "LightZones")
                                       and Scene.attr(s, "parent") == "Game"
                                       or Scene.attr(s, "parent") == "Game/LightZones"):
            continue
        if s.startswith("[node ") and Scene.attr(s, "parent") == "Game/Mobys" and "moby_0.tscn" not in head:
            m = re.search(r"^transform = Transform3D\(([^)]*)\)", s, re.M)
            got = museum_light.moby_light(zones, [float(v) for v in m.group(1).split(",")][9:12]) if m else None
            if got is not None:
                light_set, rgb = got
                s = re.sub(r"^metadata/rc1_colour = .*\n", "", s, flags=re.M)
                s = re.sub(r"^metadata/rc1_light = .*\n", "", s, flags=re.M)
                s = s.rstrip("\n") + f"\nmetadata/rc1_colour = [{rgb[0]}, {rgb[1]}, {rgb[2]}]\nmetadata/rc1_light = {light_set}\n"
        keep.append(s)
    nodes = ['[node name="LightSets" parent="Game" type="Node"]\n'
             + "".join(f"metadata/rc1_set_{i} = [{', '.join(f'{v:.6g}' for v in values)}]\n" for i, values in enumerate(sets)),
             '[node name="LightZones" parent="Game" type="Node3D"]\nvisible = false\n']
    for z in zones:
        x, y, zz, c = z["x"], z["y"], z["z"], z["centre"]
        rows = [x[0], y[0], zz[0], x[1], y[1], zz[1], x[2], y[2], zz[2], *c]
        nodes.append(f'[node name="{z["name"]}" parent="Game/LightZones" type="Node3D"]\n'
                     f"transform = Transform3D({', '.join(f'{v:.6g}' for v in rows)})\n"
                     f"metadata/rc1_colour_a = {z['colour_a']}\nmetadata/rc1_colour_b = {z['colour_b']}\n"
                     f"metadata/rc1_set_a = {z['set_a']}\nmetadata/rc1_set_b = {z['set_b']}\n"
                     f"metadata/rc1_on = true\nmetadata/rc1_reach = {float(z['reach']):.6g}\n")
    scene.sections = keep + nodes
    return scene.text()


def settle(project, scene_path):
    """Sets every thing we placed down on the ground with OpenRAC Editor's placement (its whole
    underside: none floats, sinks or stands in a wall; a crate off a slope goes to level ground
    near by). A crate with no good spot near is left out."""
    try:
        import placement
    except ImportError:
        raise SystemExit("OpenRAC Editor's tools have no placement.py: update OpenRAC Editor")
    text = scene_path.read_text(encoding="utf-8")
    report = []
    for _ in range(3):                                    # crates on crates settle in turn
        text, report = placement.place_scene(project, text, names=["OM_*"], water=island.SEA, move=True)
        if not any(r["done"] for r in report):
            break
    dropped = [r["node"] for r in report if r["problem"] and "Crate" in r["node"]]
    if dropped:
        parts = re.split(r"(?m)^(?=\[)", text)
        text = "".join(t for t in parts if not any(t.startswith(f'[node name="{n}"') for n in dropped))
    for r in report:
        if r["problem"] and r["node"] not in dropped:
            print(f"{r['node']}: {r['problem']}")
    scene_path.write_text(text, encoding="utf-8", newline="\n")
    print(f"Set down on the ground; {len(dropped)} crate(s) with no level spot left out.")


def write_museum(projects, tools):
    """Turn PROJECTS/level_11, a fresh Pokitaru project, into the museum. TOOLS is OpenRAC
    Editor's openrac/tools (its static imports)."""
    project = projects / f"level_{LEVEL:02}"
    level_dir = project / "levels" / f"level_{LEVEL:02}"
    scene_path = level_dir / f"level_{LEVEL:02}.tscn"
    if not scene_path.is_file():
        raise SystemExit(f"{scene_path} not found")
    if str(tools) not in sys.path:
        sys.path.insert(0, str(tools))
    import static_assets  # noqa: E402

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
    palm_source = projects / "level_02" / "levels" / "level_02" / "ties" / "tie_586.glb"
    palms = scatter.palms()
    if palm_source.is_file():
        static_assets.import_asset(palm_source, project, LEVEL, "Palm_000", palms[0][:3])

    scene = Scene(scene_path.read_text(encoding="utf-8"))
    for k, sec in enumerate(scene.sections):
        if sec.startswith('[node name="LevelSettings"'):
            for key, value in HAZE.items():
                sec = re.sub(rf"^metadata/rc1_{key} = .*$", f"metadata/rc1_{key} = {value}", sec, count=1, flags=re.M)
            scene.sections[k] = sec

    # 3. Remove Pokitaru: its ties and shrubs (but one of each kept as a model), its collision,
    #    its mobys (but the kept classes and one of each template).
    ext = {}
    for s in scene.sections:
        if s.startswith("[ext_resource"):
            ext[Scene.attr(s, "id")] = Scene.attr(s, "path")
    template_nodes, tie_models, shrub_models = {}, {}, {}
    keep_ties = set(scatter.TIES.values()) | set(scatter.TOWERS)
    keep_shrubs = set(scatter.SHRUBS.values())
    props = layout.get("props", [])
    keep_ties |= {p[1] for p in props if p[0] == "tie"}
    keep_shrubs |= {p[1] for p in props if p[0] == "shrub"}
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
                if cls == VENDOR:
                    # Stood on our ground, which is not Pokitaru's where it stood.
                    b = basis_of(s)
                    m = re.search(r"^transform = Transform3D\(([^)]*)\)", s, re.M)
                    x, y, z = [float(v) for v in m.group(1).split(",")][9:12]
                    ground = float(island.height(x, y))
                    print(f"vendor at ({x:.1f}, {y:.1f}): z {z:.2f} -> {ground:.2f}")
                    scene.sections[i] = copy_node(s, Scene.attr(s, "name"), transform(x, y, ground, basis=b))
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
        if kind in ENEMIES:
            continue
        new_nodes.append(copy_node(s, Scene.attr(s, "name"), transform(*hidden, basis=basis_of(s))))

    # 5. The enemies' zones: new paths after Pokitaru's own.
    first_path = 1 + max([int(v) for s in sections if 'parent="Game/Paths"' in s.split("\n")[0]
                          for v in re.findall(r"^metadata/rc1_index = (\d+)$", s, re.M)] or [-1])
    for k, poly in enumerate(layout.get("zones", [])):
        sub, node = zone_path(first_path + k, poly)
        new_subs.append(sub)
        new_nodes.append(node)

    for k, rail in enumerate(layout.get("grind_paths", [])):
        sub, node = grind_path(k, rail["points"])
        new_subs.append(sub)
        new_nodes.append(node)

    # 6. Copies of the game's own objects.
    counts = {}
    for kind, at, yaw in exhibits.crates():
        layout["mobys"].append({"kind": kind, "position": at, "yaw": yaw})
    for kind, number, x, y, z, yaw, scale in props:
        if kind not in ("tie", "shrub"):
            layout["mobys"].append({"kind": kind, "position": [x, y, z], "yaw": yaw})
    for m in layout["mobys"]:
        kind = m["kind"]
        if kind not in template_nodes:
            print(f"no {kind} in Pokitaru to copy: skipped")
            continue
        s = template_nodes[kind][0]
        counts[kind] = counts.get(kind, 0) + 1
        name = f"OM_{''.join(w.title() for w in kind.split('_'))}_{counts[kind]:03}"
        x, y, z = m["position"]
        node = copy_node(s, name, transform(x, y, z, basis=turned(basis_of(s), m["yaw"])))
        if "zone" in m:
            node = set_pvar(node, ZONE_WORD[kind], first_path + m["zone"])
        new_nodes.append(node)
    if "lamp" in template_nodes:
        s = template_nodes["lamp"][0]
        for k, (x, y, z) in enumerate(layout["lamps"]):
            new_nodes.append(copy_node(s, f"OM_Lamp_{k + 1:03}", transform(x, y, z, basis=basis_of(s))))

    # 7. Pokitaru's plants and rocks, scattered on the island.
    tie_count = shrub_count = 0
    # None growing through a small scene: a campfire wants room round it, the rest a little.
    scenes = [(x, y, 6.0 if name == "campfire" else 3.0) for name, x, y, _ in layout.get("vignettes", [])]

    def in_scene(x, y):
        return any(math.hypot(x - sx, y - sy) < r for sx, sy, r in scenes)

    # Props of the small scenes are part of them, so never filtered out.
    scene_props = [tuple(p) + (True,) for p in props if p[0] in ("tie", "shrub")]
    for kind, number, x, y, z, yaw, scale, *own in (
            [p + (False,) for p in scatter.plants() + list(scatter.sea_islets()) + scatter.town() + scatter.decor()]
            + scene_props):
        if not own[0] and in_scene(x, y):
            continue
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

    # 8. Our collision.
    for name in ("ground", "rocks", "house"):
        rid = f"om_collision_{name}"
        new_ext.append(f'[ext_resource type="PackedScene" path="res://{FOLDER}/collision/{name}.glb" id="{rid}"]\n\n')
        new_nodes.append(f'[node name="OM_{name.title()}" parent="Game/Collision" instance=ExtResource("{rid}")]\n'
                         f'metadata/rc1_surface = 31\n\n')

    # 9. The animated props: the waterfalls flow, the foam turns, the logo turns.
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

    # 10. The exhibits' objects from other levels.
    for k, item in enumerate(exhibits.placements()):
        rid = foreign_model(project, projects, item["level"], item["class"])
        if rid is None:
            print(f"level {item['level']:02} has no class {item['class']}: {item['name']} skipped")
            continue
        if rid not in {e for e in new_ext}:
            new_ext.append(rid)
        new_nodes.append(foreign_node(item, k))

    # The rails are in the grind table (5. above): Pokitaru's own code has no grinding, so on foot
    # they are handrails; the RAC-Board mod's board grinds them.

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
    settle(project, scene_path)
    # After the mobys are set down: the light each takes from where it stands.
    scene_path.write_text(light_nodes(scene_path.read_text(encoding="utf-8"), layout), encoding="utf-8",
                          newline="\n")

    set_planet(project)
    print(f"OpenRAC Museum written into {project}: {len(layout['mobys'])} objects, {tie_count} rocks, "
          f"{shrub_count} plants, {len(palms)} palms.")


def class_scale(projects, level, cls):
    """The class scale its level's class scene gives the model (as the editor's Library reads it)."""
    scene = projects / f"level_{level:02}" / "levels" / f"level_{level:02}" / "mobys" / f"moby_{cls}.tscn"
    text = scene.read_text(encoding="utf-8")
    m = re.search(r'\[node name="Model"[^\n]*\]\ntransform = Transform3D\(([^,]+),', text)
    return float(m.group(1)) if m else 1.0


def foreign_model(project, projects, level, cls):
    """Copies class CLS's model and its pictures from PROJECTS/level_LEVEL into PROJECT's library,
    byte for byte (a shared map then names them from the disc), and gives its ext_resource."""
    source_dir = projects / f"level_{level:02}" / "levels" / f"level_{level:02}" / "mobys"
    source = source_dir / f"moby_{cls}.glb"
    if not source.is_file():
        return None
    target_dir = project / "library" / "rac1" / "pal" / f"level_{level:02}" / "mobys"
    target = target_dir / source.name
    if not target.is_file():
        target_dir.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        shutil.copyfile(source, target)
        length = int.from_bytes(data[12:16], "little")
        for image in json.loads(data[20:20 + length]).get("images", []):
            uri = image.get("uri", "")
            if uri and not uri.startswith("data:"):
                to = (target_dir / uri).resolve()
                to.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile((source_dir / uri).resolve(), to)
        note = {"format": 1, "game": "rac1", "disc": "pal", "level": level, "kind": "mobys", "class": cls}
        target.with_suffix(".source.json").write_text(json.dumps(note, indent=2) + "\n", encoding="utf-8")
    FOREIGN_SCALE[cls] = class_scale(projects, level, cls)
    return (f'[ext_resource type="PackedScene" path="res://library/rac1/pal/level_{level:02}/mobys/moby_{cls}.glb" '
            f'id="om_foreign_{cls}"]\n\n')


FOREIGN_SCALE = {}


def foreign_node(item, k):
    cls = item["class"]
    scale = FOREIGN_SCALE[cls]
    name = "OM_" + re.sub(r"[^A-Za-z0-9]", "", item["name"].title()) + f"_{k:02}"
    body = f'[node name="{name}" parent="Game/Mobys" instance=ExtResource("om_foreign_{cls}")]\n'
    body += "transform = " + transform(*item["at"], item["yaw"], scale * item["scale"]) + "\n"
    body += f"metadata/rc1_foreign_level = {item['level']}\nmetadata/rc1_foreign_class_scale = {scale:.7g}\n"
    if item["look"]:
        body += 'metadata/rc1_behaviour = "spin"\nmetadata/rc1_behaviour_speed = 0.0\n'
    return body + "\n"


def set_planet(project):
    godot = project / "project.godot"
    text = godot.read_text(encoding="utf-8")
    text = re.sub(r'(?m)^planet/name=.*\n', "", text)
    if "[openrac]" not in text:
        text = text.rstrip("\n") + "\n\n[openrac]\n"
    text = text.replace("[openrac]\n", f'[openrac]\n\nplanet/name="{PLANET}"\n', 1)
    godot.write_text(text, encoding="utf-8", newline="\n")
