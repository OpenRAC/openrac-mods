"""Builds Open Museum's own models into a level project of the editor.

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
import sys
from pathlib import Path

import numpy as np

import glb
import house
import island
import landmarks
import ground
import textures
from build import Geo

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

    # The terrain: island, sea, horizon.
    land = ground.island_terrain()
    land.merge(ground.sea())
    land.merge(ground.far_islands())
    used = {m for m in land.parts}
    glb.write(str(out / "terrain.glb"), [land], {k: MATERIALS[k] for k in used},
              {k: tex[k] for k in used}, ["Island"], external="textures")
    # Named Island, not Terrain_NNN: the port hides a Terrain_NNN node with the game's own terrain
    # fragment of that number (its far cull and occlusion), which is not ours.

    # The house.
    geo = Geo()
    signs, posters = [], []
    house.build(geo, signs, posters)
    pictures = {}
    mats = dict(MATERIALS)
    for key, title, sub, centre, facing, w, h in signs:
        pictures[key] = textures.sign(title, sub)
        mats[key] = {"texture": key, "double": True}
        geo.sign(key, centre, facing, w, h)
    for n, (key, title, centre, facing, w, h) in enumerate(posters):
        pictures[key] = textures.poster(title, ["Exhibit " + str(n % 4 + 1)], n)
        mats[key] = {"texture": key, "double": True}
        geo.sign(key, centre, facing, w, h)
    # The big welcome sign over the south door, outside.
    welcome = "om_sign_welcome"
    pictures[welcome] = textures.sign("OPEN MUSEUM", "how OpenRAC is made", 1024, 320)
    mats[welcome] = {"texture": welcome, "double": True}
    geo.sign(welcome, (house.CX, house.CY - house.HALF - house.WALL, house.F0 + 9.5), -90, 22, 6.9)

    marks = Geo()
    layout = landmarks.build(marks, pictures, mats)

    all_tex = dict(tex)
    all_tex.update(pictures)

    def save(name, g):
        used = set(g.mesh.parts)
        glb.write(str(out / "models" / f"{name}.glb"), [g.mesh], {k: mats[k] for k in used},
                  {k: all_tex[k] for k in used})

    save("house", geo)
    save("landmarks", marks)
    for name, g in landmarks.animated(mats).items():
        save(name, g)

    collision_glb(out / "collision" / "house.glb", geo.solid + marks.solid)
    P, tris = ground.collision_mesh()
    collision_glb(out / "collision" / "ground.glb", [((P[t[0]], P[t[1]], P[t[2]]), 0x1F) for t in tris])

    layout["house"] = {"centre": [house.CX, house.CY, 0.0]}
    layout["sea"] = island.SEA
    (out / "layout.json").write_text(json.dumps(layout, indent=1) + "\n", encoding="utf-8")
    print(f"house: {geo.count} shapes, {len(geo.solid)} solid triangles; ground: {len(tris)} triangles")


if __name__ == "__main__":
    main(sys.argv[1])
