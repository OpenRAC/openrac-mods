"""Builds OpenRAC Museum's own models into a level project of the editor.

    python3 make_museum.py OUT

OUT gets:
  terrain.glb, textures/om_*.png   the island, the sea and the islands on the horizon
  models/house.glb                 the museum (drawn), its signs and empty exhibit boards
  models/landmarks.glb             the waterfalls' rocks, the shrines and easter eggs
  models/falls_<n>.glb             each waterfall's water (an animated prop)
  models/logo.glb                  the atrium's turning logo (an animated prop)
  collision/ground.glb             the island's ground (surface 0x1f)
  collision/house.glb              the house's floors, walls, roofs, bridges
  layout.json                      where the scene's objects go (see place_museum.py)

Every model here is drawn by these scripts; the ground and wood pictures are Pokitaru's own, read
from the extracted level (DISC_TEXTURES) when it is given, else drawn too.
"""

import json
import math
import os
import sys
import types
from pathlib import Path

import numpy as np

import exhibits
import gate
import glb
import house
import island
import landmarks
import ground
import rocks
import textures
from build import Geo


def editor_terrain():
    """The editor's terrain tools (openrac/tools/terrain_tools.py; terrain.py in older editors),
    found beside its static_assets.py on sys.path or in OPENRAC_EDITOR_TOOLS. Loaded by its path:
    OpenRAC's extractor has a terrain module of its own."""
    import importlib.util
    folders = [os.environ.get("OPENRAC_EDITOR_TOOLS")] + sys.path
    for folder, name in [(f, n) for f in folders for n in ("terrain_tools.py", "terrain.py")]:
        if folder and (Path(folder) / name).is_file() and (Path(folder) / "static_assets.py").is_file():
            spec = importlib.util.spec_from_file_location("openrac_editor_terrain", Path(folder) / name)
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            spec.loader.exec_module(module)
            return module
    raise SystemExit("OpenRAC Editor's terrain tools (openrac/tools/terrain_tools.py) not found: pass --editor "
                     "with an editor that has them, or set OPENRAC_EDITOR_TOOLS")


# The rock's detail: the island's rock at STEP units, the islets and stacks further out coarser.
ROCK_STEP, FAR_STEP, COLLISION_STEP = 3.5, 5.0, 8.0
NEAR = 330.0


def rock_mesh(terrain, recipe, step):
    """Surface nets over RECIPE's rocks: vertices, faces (outward), faces under the sea dropped."""
    boxes = [terrain.rock_bounds(r) for r in recipe["rocks"]]
    lo = np.min([b[0] for b in boxes], 0) - step * 2
    hi = np.max([b[1] for b in boxes], 0) + step * 2
    V, F = terrain.surface_nets(lambda P: terrain.recipe_density(P, recipe), lo, hi, step)
    F = F[V[F].mean(1)[:, 2] > recipe["sea"] - recipe.get("keep_below_sea", 5.0)]
    used, inverse = np.unique(F.reshape(-1), return_inverse=True)
    return V[used], inverse.reshape(-1, 3)


def split_recipe(recipe):
    """The island's rocks (and the bottle's islet, which the hero reaches), and the rest."""
    def near(r):
        return math.hypot(r["x"] - island.CENTRE[0], r["y"] - island.CENTRE[1]) < NEAR
    bottle = island.ISLETS[3]
    reached = [r for r in recipe["rocks"] if near(r) or (r["x"], r["y"]) == (bottle[0], bottle[1])]
    return (dict(recipe, rocks=[r for r in recipe["rocks"] if near(r)]),
            dict(recipe, rocks=[r for r in recipe["rocks"] if not near(r)], caves=[]),
            dict(recipe, rocks=reached))


def reachable(c):
    """Where the hero can meet the rock (the level's collision must fit Pokitaru's memory): its
    foot, up to a jump over the low ground, the bottle's islet, and round the Swingshot chain up the
    volcano. The high walls and tops nobody reaches have none."""
    low = c[:, 2] < island.SEA + 16
    ix, iy, ir, _ = island.ISLETS[3]
    islet = np.hypot(c[:, 0] - ix, c[:, 1] - iy) < ir * 1.6
    a = np.array(island.LOOKOUT); b = np.array(island.VOLCANO_LEDGE)
    d = b - a
    t = np.clip(((c[:, :2] - a) @ d) / (d @ d), 0, 1)
    chain = np.hypot(*(c[:, :2] - (a + t[:, None] * d)).T) < 24
    return low | islet | chain


def normals(V, F):
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    N = np.zeros_like(V)
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    return N / np.maximum(np.linalg.norm(N, axis=1), 1e-9)[:, None]


def add_rocks(terrain, t, V, F, recipe):
    """The rock into the editor's terrain T as its Rocks node, textured by the editor's rules."""
    prim = terrain.Prim(t.material_for(recipe["textures"]["wall"]),
                        {"POSITION": V, "NORMAL": normals(V, F), "TEXCOORD_0": np.zeros((len(V), 2)),
                         "COLOR_0": np.ones((len(V), 4))}, F)
    t.nodes = [n for n in t.nodes if n[0] != terrain.ROCKS_NODE]
    t.nodes.append([terrain.ROCKS_NODE, None, [prim]])
    terrain.auto_texture(t, recipe, types.SimpleNamespace())


ROCKY = {"om_rock", "om_cliff", "om_basalt", "om_jungle", "om_wet_sand"}


MODEL_TINT = 0.58     # the models' colours are half the terrain's scale in the port
SUBDIVIDE = 3.0        # model faces split to this size, so their light shows shadows


def strips(p, size):
    """Splits P's long thin faces (beams, posts, rails, trims) into a strip of pieces along their
    length: the light still changes along them, at a tenth of the triangles an even split makes.
    Faces it leaves are split evenly afterwards (terrain.subdivide)."""
    F, P = p.faces, p.pos
    step = size * 0.7          # so a piece's diagonal stays under SIZE: the even split leaves it
    corner = P[F]                                                    # (T, 3, 3)
    edge = np.stack([np.linalg.norm(corner[:, (k + 1) % 3] - corner[:, k], axis=1) for k in range(3)], 1)
    short = edge.argmin(1)                    # the short edge runs from corner k to k + 1
    longest = edge.max(1)
    n = np.clip(np.ceil(longest / step), 1, 64).astype(int)
    thin = (edge.min(1) < step) & (n > 1)
    if not thin.any():
        return
    new_attrs = {k: [] for k in p.attrs}
    new_faces = [F[~thin]]
    mask = getattr(p, "keep", None)
    new_keep = [] if mask is not None else None
    base = len(P)
    for f in np.nonzero(thin)[0]:
        k = short[f]
        u, v, w = F[f, k], F[f, (k + 1) % 3], F[f, (k + 2) % 3]     # u-v short, w the far corner
        m = n[f]
        t = np.arange(m + 1) / m
        # Rungs from the short edge to the far corner: u..w on one side, v..w on the other.
        for key, val in p.attrs.items():
            side_u = val[u][None] * (1 - t[:, None]) + val[w][None] * t[:, None]
            side_v = val[v][None] * (1 - t[:, None]) + val[w][None] * t[:, None]
            new_attrs[key].append(np.concatenate([side_u, side_v[:-1]]))   # the far corner once
        iu = base + np.arange(m + 1)
        iv = np.concatenate([base + m + 1 + np.arange(m), [base + m]])     # v side ends at w
        tris = []
        for i in range(m):
            tris.append((iu[i], iv[i], iu[i + 1]))
            if i < m - 1:
                tris.append((iv[i], iv[i + 1], iu[i + 1]))
        new_faces.append(np.array(tris))
        if new_keep is not None:
            new_keep.append(np.repeat(mask[u], 2 * m + 1))
        base += 2 * m + 1
    p.attrs = {k: np.concatenate([v] + new_attrs[k]) for k, v in p.attrs.items()}
    p.faces = np.concatenate(new_faces)
    if new_keep is not None:
        p.keep = np.concatenate([mask] + new_keep)


def bake(terrain, t, models=(), lights=()):
    """Sun, shadows, sky light and point lights into the ground's and the models' colours (the
    editor's bake: all of them together, so the house shades the deck and the deck the stilts),
    each picture's own brightness kept as a tint (Pokitaru's pictures are dark, its ground lit up
    to 2). Glowing parts of the models (flames, lamps) keep their colours."""
    for _, _, p in t.prims():
        name = t.materials[p.material].get("name", "")
        tint = ground.BRIGHT.get(name, 1.0) * (0.85 if name in ROCKY else 0.6)
        warm = np.array([1.1, 0.96, 0.76]) if name in ROCKY else np.ones(3)   # Pokitaru's rock is warm grey
        # Big soft patches of lighter and darker ground (tens of units wide): the eye stops
        # seeing the picture repeat.
        P = p.pos
        patch = 0.8 + 0.4 * (terrain.value_noise3(P / 38.0, 21, 3) * 0.7 + terrain.value_noise3(P / 11.0, 22, 2) * 0.3)
        col = tint * warm[None] * patch[:, None]
        p.attrs["COLOR_0"] = np.concatenate([col, np.ones((len(P), 1))], 1)
    args = types.SimpleNamespace(sun=[-35.0, 40.0], region=[230.0, -20.0, 1100.0, 920.0], ao=20, cell=0.9, bias=0.8,
                                 strength=1.0, ambient=0.22, sun_colour=[1.25, 1.1, 0.82], sky_colour=[0.72, 0.7, 0.68],
                                 bounce_colour=[0.32, 0.27, 0.16], keep_tint=True, lights=list(lights))
    for mt in models:
        for _, _, p in mt.prims():
            c = p.attrs["COLOR_0"]
            p.keep = c[:, :3].max(1) > 0.9
            strips(p, SUBDIVIDE)
            terrain.subdivide(p, SUBDIVIDE)
            c = p.attrs["COLOR_0"]
            c[~p.keep, :3] = MODEL_TINT
    return terrain.bake_light(t, args, models)


def append(terrain, t, path):
    """Another glb's nodes into T, their materials matched by picture."""
    other = terrain.Terrain(path)
    for name, tr, prims in other.nodes:
        for p in prims:
            p.material = t.material_for(other.texture_uri(p.material))
        t.nodes.append([name, tr, prims])

EFFECTS = Path(__file__).resolve().parent / "effects.json"

MATERIALS = {name: {"texture": name, "double": True} for name in (
    "om_sand", "om_grass", "om_rock", "om_basalt", "om_planks", "om_planks_dark", "om_planks_light",
    "om_thatch", "om_water", "om_foam", "om_falls", "om_stone", "om_lava", "om_gold", "om_white",
    "om_rail", "om_red", "om_teal", "om_glass", "om_yellow", "om_orange", "om_cliff", "om_jungle", "om_wet_sand", "om_path")}


def collision_glb(path, triangles):
    """TRIANGLES: [((p0, p1, p2), surface)]. Upward faces keep their winding (made to face up);
    walls get both sides, so the game's one-sided faces stop the hero from either side."""
    by_surface = {}
    for (p0, p1, p2), surface in triangles:
        p = np.array([p0, p1, p2], np.float64)
        n = np.cross(p[1] - p[0], p[2] - p[0])
        length = np.linalg.norm(n)
        if length < 1e-6:
            continue
        n /= length
        out = by_surface.setdefault(surface, [])
        if n[2] > 0.3:
            out.append(p)
        elif n[2] < -0.3:
            out.append(p[[0, 2, 1]])          # a ceiling: walkable from above too (thin slabs)
        else:
            out.append(p)
            out.append(p[[0, 2, 1]])
    mesh = glb.Mesh()
    mats = {}
    for surface, tris in by_surface.items():
        name = f"rc1_surface_{surface:02x}"
        mats[name] = {"colour": [0.5, 0.5, 0.5, 1]}
        t = np.array(tris, np.float32).reshape(-1, 3)
        n = np.tile([0, 0, 1], (len(t), 1))
        mesh.add(name, t, n, np.zeros((len(t), 2)), np.ones((len(t), 3)) * 0.5, np.arange(len(t)).reshape(-1, 3))
    glb.write(str(path), [mesh], mats, {})


def main(out, disc_textures=None):
    out = Path(out)
    (out / "models").mkdir(parents=True, exist_ok=True)
    (out / "collision").mkdir(parents=True, exist_ok=True)
    tex = textures.all_textures()
    tex.update(textures.from_disc(disc_textures))

    # The rock (the editor's terrain tools), then the low ground round it, the sea, the horizon.
    terrain = editor_terrain()
    recipe = rocks.recipe()
    near, far, reached = split_recipe(recipe)
    V1, F1 = rock_mesh(terrain, near, ROCK_STEP)
    V2, F2 = rock_mesh(terrain, far, FAR_STEP)
    V = np.concatenate([V1, V2]); F = np.concatenate([F1, F2 + len(V1)])
    island.set_rock_top(rocks.top_raster(V, F))

    def inside(P):
        return terrain.recipe_density(P, recipe)
    land = ground.island_terrain(inside)
    names = set(MATERIALS) & (set(land.parts) | {Path(u).stem for u in recipe["textures"].values()})
    (out / "textures").mkdir(exist_ok=True)
    for name in names:
        (out / "textures" / f"{name}.png").write_bytes(tex[name])
    glb.write(str(out / "terrain.glb"), [land], {k: MATERIALS[k] for k in land.parts},
              {k: tex[k] for k in land.parts}, ["Island"], external="textures")
    # Named Island, not Terrain_NNN: the port hides a Terrain_NNN node with the game's own terrain
    # fragment of that number (its far cull and occlusion), which is not ours.
    t = terrain.Terrain(out / "terrain.glb")
    add_rocks(terrain, t, V, F, recipe)
    (out / "rocks.json").write_text(json.dumps(recipe, indent=1) + "\n", encoding="utf-8")

    # The house.
    geo = Geo()
    signs, posters, art = [], [], []
    house.build(geo, signs, posters, art)
    pictures = {}
    mats = dict(MATERIALS)
    for key, kind, seed in art:
        pictures[key] = textures.painting(seed) if kind == "painting" else textures.rug(seed)
        mats[key] = {"texture": key, "double": True}
    pictures.update(exhibits.PICTURES)
    for key in exhibits.PICTURES:
        mats[key] = {"texture": key, "double": True}
    for key, title, sub, centre, facing, w, h in signs:
        if centre is None:            # a picture for a shape of its own (the gate's board)
            pictures[key] = gate.picture(key, title, sub)
            mats[key] = {"texture": key, "double": True}
            continue
        if key not in pictures:
            pictures[key] = (textures.welcome_sign(title, sub) if key.startswith("om_sign_welcome")
                             else textures.sign(title, sub))
        mats[key] = {"texture": key, "double": True}
        geo.sign(key, centre, facing, w, h)
    for n, (key, title, centre, facing, w, h) in enumerate(posters):
        pictures[key] = exhibits.board_picture(key, title, n)
        mats[key] = {"texture": key, "double": True}
        geo.sign(key, centre, facing, w, h)

    marks = Geo()
    layout = landmarks.build(marks, pictures, mats)

    all_tex = dict(tex)
    all_tex.update(pictures)

    def save(name, g):
        used = set(g.mesh.parts)
        # Pictures beside the model, not inside it: the disc's own then stay byte for byte the
        # disc's files, which a shared map names instead of holding.
        glb.write(str(out / "models" / f"{name}.glb"), [g.mesh], {k: mats[k] for k in used},
                  {k: all_tex[k] for k in used}, external="textures")

    save("house", geo)
    save("landmarks", marks)
    # The light: terrain, house and landmarks baked together, with the flames and lamps.
    lights = geo.lights + marks.lights + [{"at": [x, y, z + 4.0], "colour": [1.2, 0.95, 0.6], "radius": 9.0}
                                          for x, y, z in layout["lamps"]]
    models = [terrain.Terrain(out / "models" / f"{n}.glb") for n in ("house", "landmarks")]
    light = bake(terrain, t, models, lights)
    for mt in models:
        mt.save()
    horizon = ground.sea()
    horizon.merge(ground.far_islands())
    glb.write(str(out / "horizon.glb"), [horizon], {k: MATERIALS[k] for k in horizon.parts},
              {k: tex[k] for k in horizon.parts}, ["Horizon"], external="textures")
    append(terrain, t, out / "horizon.glb")
    (out / "horizon.glb").unlink()
    t.save(out / "terrain.glb")
    print(f"rock: {len(F)} triangles; light: {light}")
    for name, g in landmarks.animated(mats).items():
        save(name, g)

    collision_glb(out / "collision" / "house.glb", geo.solid + marks.solid)
    # Where the mod's live effects play (embers, smoke, mist): mod_data.py puts them in the mod.
    flames = sorted(set(geo.flames + marks.flames))
    EFFECTS.write_text(json.dumps(flames, indent=0) + "\n", encoding="utf-8")
    P, tris = ground.collision_mesh(inside)
    collision_glb(out / "collision" / "ground.glb", [((P[t[0]], P[t[1]], P[t[2]]), 0x1F) for t in tris])
    # The rock's collision: its own mesh again on a coarser grid, faces kept as they are (outward:
    # the rock is closed, so the hero only ever meets their fronts).
    cV, cF = rock_mesh(terrain, reached, COLLISION_STEP)
    cF = cF[reachable(cV[cF].mean(1)) & (cV[cF][:, :, 2].max(1) > island.SEA - 3)]
    rock_tris = cV[cF].astype(np.float32)
    mesh = glb.Mesh()
    mesh.add("rc1_surface_1f", rock_tris.reshape(-1, 3), np.tile([0, 0, 1], (len(rock_tris) * 3, 1)),
             np.zeros((len(rock_tris) * 3, 2)), np.ones((len(rock_tris) * 3, 3)) * 0.5, np.arange(len(rock_tris) * 3).reshape(-1, 3))
    glb.write(str(out / "collision" / "rocks.glb"), [mesh], {"rc1_surface_1f": {"colour": [0.5, 0.5, 0.5, 1]}}, {})

    layout["house"] = {"centre": [house.CX, house.CY, 0.0]}
    layout["sea"] = island.SEA
    (out / "layout.json").write_text(json.dumps(layout, indent=1) + "\n", encoding="utf-8")
    print(f"house: {geo.count} shapes, {len(geo.solid)} solid triangles; ground: {len(tris)} + rock {len(cF)} collision triangles")


if __name__ == "__main__":
    main(sys.argv[1])
