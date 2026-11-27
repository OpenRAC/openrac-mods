"""Collision for the scattered ties (Pokitaru's rocks, trees and bushes, Aridia's palms): the game's
ties have none of their own, so the hero would walk through them.

Each gets a low-poly prism round its outline from the ground up to most of its height (rocks a
little higher, so the hero stands on their tops), palms a post round the trunk. Grass, flowers,
the sea islets and the far towers get none: the collision must stay small."""

import math

import numpy as np

import island

# Pokitaru's rock ties.
ROCKS = {1513, 1514, 1515, 1519, 2395}
# Ties nobody reaches or that are only leaves: none.
SKIP = {880, 1509, 1600, 1481, 1484, 1485, 1486, 1487, 1488, 1491, 1492, 1493}


def hull(points):
    """The 2D convex hull of POINTS (N, 2), anticlockwise (Andrew's monotone chain)."""
    p = sorted(set(map(tuple, np.round(points, 3))))
    if len(p) < 3:
        return np.array(p)

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower, upper = [], []
    for q in p:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], q) <= 0:
            lower.pop()
        lower.append(q)
    for q in reversed(p):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], q) <= 0:
            upper.pop()
        upper.append(q)
    return np.array(lower[:-1] + upper[:-1])


def cross2(a, b):
    return float(a[0] * b[1] - a[1] * b[0])


def simplify(h, count=8):
    """At most COUNT corners of hull H: the ones that cut off the most area stay."""
    h = list(map(np.asarray, h))
    while len(h) > count:
        areas = [abs(cross2(h[i] - h[i - 1], h[(i + 1) % len(h)] - h[i - 1])) for i in range(len(h))]
        h.pop(int(np.argmin(areas)))
    return np.array(h)


def prism(outline, z0, z1):
    """Walls round OUTLINE (anticlockwise) from Z0 to Z1 and a flat top: triangles."""
    tris = []
    n = len(outline)
    for i in range(n):
        a, b = outline[i], outline[(i + 1) % n]
        tris.append(([a[0], a[1], z0], [b[0], b[1], z0], [b[0], b[1], z1]))
        tris.append(([a[0], a[1], z0], [b[0], b[1], z1], [a[0], a[1], z1]))
    c = outline.mean(0)
    for i in range(n):
        a, b = outline[i], outline[(i + 1) % n]
        tris.append(([c[0], c[1], z1], [a[0], a[1], z1], [b[0], b[1], z1]))
    return tris


def place(V, basis, at):
    """Model vertices V (N, 3) by a scene's basis (three columns) and position."""
    B = np.array(basis, np.float64).reshape(3, 3)       # rows are the columns
    return V @ B + np.asarray(at, np.float64)


def triangles(model, number, basis, at, palm=False):
    """Collision triangles [((p0, p1, p2), surface)] for tie NUMBER (MODEL: its (V, F)) placed by
    BASIS at AT; none for leaves and the far towers."""
    if number in SKIP:
        return []
    ground = float(island.height(at[0], at[1]))
    if palm:
        # The trunk: a post at the foot, as tall as a jump.
        return [(t, 0x1F) for t in prism(simplify(circle(at[:2], 0.45), 6), ground - 1.0, ground + 4.0)]
    V, _ = model
    P = place(V, basis, at)
    rock = number in ROCKS
    above = P[P[:, 2] > ground - 0.5]
    if len(above) < 3:
        return []
    zs = above[:, 2]
    top = float(zs.min() + (zs.max() - zs.min()) * (0.85 if rock else 0.7))
    if top < ground + 0.4:
        return []
    h = hull(above[:, :2])
    if len(h) < 3:
        return []
    h = simplify(h, 6 if rock else 5)
    # A little inside the outline: leaves and the rock's rounded sides stick out past what stops the hero.
    c = h.mean(0)
    h = c + (h - c) * (0.85 if rock else 0.7)
    return [(t, 0x1F) for t in prism(h, ground - 1.0, top)]


def circle(centre, r, n=12):
    return np.array([[centre[0] + math.cos(2 * math.pi * k / n) * r, centre[1] + math.sin(2 * math.pi * k / n) * r]
                     for k in range(n)])


def load(terrain, path):
    """A tie model's vertices and faces from its glb (the editor's terrain tools read it)."""
    t = terrain.Terrain(path)
    V, F = [], []
    count = 0
    for _, off, p in t.prims():
        V.append(p.pos + off)
        F.append(p.faces + count)
        count += len(p.pos)
    return np.concatenate(V), np.concatenate(F)
