"""A small GLB writer: triangle meshes with normals, UVs, vertex colours and PNG textures."""

import json
import os
import struct

import numpy as np


class Mesh:
    """Triangles grouped by material. Positions are the game's (x, y across, z up)."""

    def __init__(self):
        self.parts = {}  # material name -> lists of arrays

    def add(self, material, positions, normals, uvs, colours, indices):
        part = self.parts.setdefault(material, {"p": [], "n": [], "t": [], "c": [], "i": [], "count": 0})
        part["i"].append(np.asarray(indices, np.uint32) + part["count"])
        part["p"].append(np.asarray(positions, np.float32))
        part["n"].append(np.asarray(normals, np.float32))
        part["t"].append(np.asarray(uvs, np.float32))
        c = np.asarray(colours, np.float32)
        if c.shape[1] == 3:
            c = np.concatenate([c, np.ones((len(c), 1), np.float32)], 1)
        part["c"].append(c)
        part["count"] += len(positions)

    def merge(self, other):
        for name, part in other.parts.items():
            for k in range(len(part["p"])):
                start = sum(len(a) for a in part["p"][:k])
                self.add(name, part["p"][k], part["n"][k], part["t"][k], part["c"][k], part["i"][k] - start)


def write(path, meshes, materials, textures, node_names=None, external=None):
    """MESHES: list of Mesh (one node each). MATERIALS: name -> dict(texture=name, alpha=mode,
    double=bool, colour=[r,g,b,a]). TEXTURES: name -> PNG bytes."""
    blob = bytearray()
    views, accessors = [], []

    def view(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
                     | ({"target": target} if target else {}))
        blob.extend(data)
        return len(views) - 1

    def accessor(array, kind, component, target=None, bounds=False):
        v = view(array.tobytes(), target)
        a = {"bufferView": v, "componentType": component, "count": int(len(array)), "type": kind}
        if bounds:
            a["min"] = [float(x) for x in array.min(0)]
            a["max"] = [float(x) for x in array.max(0)]
        accessors.append(a)
        return len(accessors) - 1

    images, gl_textures, image_index = [], [], {}
    for name, png in textures.items():
        image_index[name] = len(images)
        if external:
            folder = os.path.join(os.path.dirname(path), external)
            os.makedirs(folder, exist_ok=True)
            with open(os.path.join(folder, name + ".png"), "wb") as f:
                f.write(png)
            images.append({"uri": f"{external}/{name}.png"})
        else:
            images.append({"bufferView": view(png), "mimeType": "image/png", "name": name})
        gl_textures.append({"source": len(images) - 1, "sampler": 0})

    gl_materials, material_index = [], {}
    for name, m in materials.items():
        pbr = {"metallicFactor": 0.0, "roughnessFactor": 1.0,
               "baseColorFactor": m.get("colour", [1, 1, 1, 1])}
        if m.get("texture"):
            pbr["baseColorTexture"] = {"index": image_index[m["texture"]]}
        entry = {"name": name, "pbrMetallicRoughness": pbr}
        if m.get("alpha"):
            entry["alphaMode"] = m["alpha"]
            if m["alpha"] == "MASK":
                entry["alphaCutoff"] = 0.5
        if m.get("double"):
            entry["doubleSided"] = True
        material_index[name] = len(gl_materials)
        gl_materials.append(entry)

    gl_meshes, nodes = [], []
    for n, mesh in enumerate(meshes):
        prims = []
        for name, part in mesh.parts.items():
            if not part["p"]:
                continue
            p = np.concatenate(part["p"]); nn = np.concatenate(part["n"])
            t = np.concatenate(part["t"]); c = np.concatenate(part["c"])
            i = np.concatenate(part["i"]).astype(np.uint32).reshape(-1)
            norm = np.linalg.norm(nn, axis=1, keepdims=True)
            nn = nn / np.where(norm == 0, 1, norm)
            prims.append({"attributes": {
                "POSITION": accessor(p, "VEC3", 5126, 34962, True),
                "NORMAL": accessor(nn.astype(np.float32), "VEC3", 5126, 34962),
                "TEXCOORD_0": accessor(t, "VEC2", 5126, 34962),
                "COLOR_0": accessor(np.clip(c, 0, 2).astype(np.float32), "VEC4", 5126, 34962)},
                "indices": accessor(i, "SCALAR", 5125, 34963), "material": material_index[name]})
        gl_meshes.append({"primitives": prims})
        nodes.append({"mesh": n, "name": (node_names[n] if node_names else f"Mesh_{n:03}")})

    doc = {"asset": {"version": "2.0", "generator": "openrac-museum"},
           "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes,
           "meshes": gl_meshes, "accessors": accessors, "bufferViews": views,
           "buffers": [{"byteLength": len(blob)}]}
    if gl_materials:
        doc["materials"] = gl_materials
    if images:
        doc["images"] = images
        doc["textures"] = gl_textures
        doc["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    while len(blob) % 4:
        blob.append(0)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js
    out += struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
    with open(path, "wb") as f:
        f.write(out)
