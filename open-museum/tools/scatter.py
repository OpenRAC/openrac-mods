"""Where the island's palms, plants and rocks grow: fixed seeds, so every install is the same."""

import math

import numpy as np

import house
import island

# Pokitaru's own models, by kind: the class numbers of the ties and shrubs used.
TIES = {"sand_egg_stone": 1516, "mossy_dark_boulder": 1513, "mossy_dark_rock": 1519, "green_leafy_sprig": 880}
SHRUBS = {"green_leafy_plant": 307, "white_lily": 308, "green_grass_bush": 369, "yellow_green_fern": 372,
          "red_fruit": 306, "brown_starfish": 373, "sand_coral_rings": 371}

SEA = island.SEA
C = island.CENTRE


def clear(x, y, margin=0.0):
    """False where things must not grow: the house, the bridges, the lagoon, the ship's pad."""
    if abs(x - house.CX) < house.HALF + house.DECK + 6 + margin and abs(y - house.CY) < house.HALF + house.DECK + 6 + margin:
        return False
    if math.hypot(x - 490, y - 403) < 22 + margin:        # the ship's landing place
        return False
    if abs(x - house.CX) < 6 and house.CY - 140 < y < house.CY:   # the south bridge
        return False
    if abs(y - house.CY) < 6 and house.CX < x < house.CX + 140:   # the east bridge
        return False
    return True


def palms():
    rng = np.random.default_rng(42)
    out = []
    tries = 0
    while len(out) < 70 and tries < 5000:
        tries += 1
        a = rng.uniform(0, 360)
        r = rng.choice([rng.uniform(island.LAGOON_R + 10, island.LAGOON_R + 30), rng.uniform(170, 238)])
        x = C[0] + math.cos(math.radians(a)) * r
        y = C[1] + math.sin(math.radians(a)) * r
        z = float(island.height(x, y))
        n = island.normal(x, y)
        if z < SEA + 1.0 or z > SEA + 14 or n[2] < 0.9 or not clear(x, y, 2):
            continue
        if any(math.hypot(x - p[0], y - p[1]) < 9 for p in out):
            continue
        out.append((x, y, z - 0.3, float(rng.uniform(0, 360)), float(rng.uniform(0.85, 1.3))))
    return out


def plants():
    """(kind, number, x, y, z, yaw, scale) for every plant and rock."""
    rng = np.random.default_rng(7)
    out = []
    for _ in range(4000):
        x, y = rng.uniform(C[0] - 300, C[0] + 300), rng.uniform(C[1] - 300, C[1] + 330)
        if not clear(x, y):
            continue
        z = float(island.height(x, y))
        n = island.normal(x, y)
        r = math.hypot(x - C[0], y - C[1])
        yaw = float(rng.uniform(0, 360))
        if z < SEA - 0.5:
            continue
        if z < SEA + 4.5 and n[2] > 0.95:            # beach
            pick = rng.random()
            if pick < 0.3:
                out.append(("shrub", SHRUBS["brown_starfish"], x, y, z, yaw, 1.0))
            elif pick < 0.55:
                out.append(("shrub", SHRUBS["sand_coral_rings"], x, y, z, yaw, 1.0))
            elif pick < 0.62:
                out.append(("tie", TIES["sand_egg_stone"], x, y, z - 0.3, yaw, float(rng.uniform(0.6, 1.4))))
        elif n[2] > 0.85 and z < SEA + 40:            # grass
            pick = rng.random()
            if pick < 0.28:
                out.append(("shrub", SHRUBS["green_grass_bush"], x, y, z, yaw, float(rng.uniform(0.8, 1.5))))
            elif pick < 0.45:
                out.append(("shrub", SHRUBS["green_leafy_plant"], x, y, z, yaw, float(rng.uniform(0.8, 1.4))))
            elif pick < 0.58:
                out.append(("shrub", SHRUBS["yellow_green_fern"], x, y, z, yaw, float(rng.uniform(0.8, 1.6))))
            elif pick < 0.63:
                out.append(("shrub", SHRUBS["red_fruit"], x, y, z, yaw, 1.0))
            elif pick < 0.67:
                out.append(("tie", TIES["green_leafy_sprig"], x, y, z, yaw, float(rng.uniform(0.8, 1.4))))
        elif n[2] < 0.85 and r < 260:                 # the mountains' slopes: rocks
            if rng.random() < 0.18:
                kind = TIES["mossy_dark_boulder"] if rng.random() < 0.5 else TIES["mossy_dark_rock"]
                out.append(("tie", kind, x, y, z - 1.0, yaw, float(rng.uniform(1.0, 2.5))))
    # Lilies on the lagoon, round the house.
    for _ in range(60):
        a = rng.uniform(0, 360)
        r = rng.uniform(house.HALF * 1.5 + 6, island.LAGOON_R - 6)
        x, y = C[0] + math.cos(math.radians(a)) * r, C[1] + math.sin(math.radians(a)) * r
        if clear(x, y):
            out.append(("shrub", SHRUBS["white_lily"], x, y, SEA + 0.05, float(rng.uniform(0, 360)), 1.0))
    return out
