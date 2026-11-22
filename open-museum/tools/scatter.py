"""Where the island's palms, plants and rocks grow: fixed seeds, so every install is the same."""

import math

import numpy as np

import house
import island

# Pokitaru's own models, by kind: the class numbers of the ties and shrubs used.
TIES = {"mossy_dark_boulder": 1513, "mossy_dark_rock": 1519, "green_leafy_sprig": 880,
        "grey_rock": 1514, "grey_rock_small": 1515, "flat_dark_rock": 2395, "leafy_tree": 1729, "round_bush": 1456,
        "rock_islet": 1509, "grass_islet": 1600}
# Pokitaru's turbine towers, for the town on the far islands (scale as Pokitaru has them).
TOWERS = [1493, 1487, 1481, 1486, 1485, 1488, 1491, 1492, 1484]
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
    for _ in range(7000):
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
            if pick < 0.12:
                out.append(("shrub", SHRUBS["brown_starfish"], x, y, z, yaw, 1.0))
            elif pick < 0.24:
                out.append(("shrub", SHRUBS["sand_coral_rings"], x, y, z, yaw, 1.0))
            elif pick < 0.3:
                out.append(("tie", TIES["grey_rock_small"], x, y, z - 1.0, yaw, float(rng.uniform(0.4, 0.8))))
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
            elif pick < 0.73:
                out.append(("tie", TIES["leafy_tree"], x, y, z - 0.3, yaw, float(rng.uniform(0.9, 1.5))))
            elif pick < 0.78:
                out.append(("tie", TIES["round_bush"], x, y, z - 0.5, yaw, float(rng.uniform(0.7, 1.2))))
        elif n[2] < 0.85 and r < 300:                 # the cliffs: rocks at their feet and on the ledges
            below = float(island.height(x + n[0] * 5, y + n[1] * 5))
            foot = island.normal(x + n[0] * 5, y + n[1] * 5)[2] > 0.85   # a ledge or flat ground below
            if foot and z - below < 6 and rng.random() < 0.6:
                pick = rng.random()
                kind = (TIES["grey_rock"] if pick < 0.45 else TIES["grey_rock_small"] if pick < 0.7
                        else TIES["mossy_dark_boulder"] if pick < 0.85 else TIES["flat_dark_rock"])
                out.append(("tie", kind, x + n[0] * 4, y + n[1] * 4, below - 1.5, yaw, float(rng.uniform(0.6, 1.2))))
    # Lilies on the lagoon, round the house.
    for _ in range(60):
        a = rng.uniform(0, 360)
        r = rng.uniform(house.HALF * 1.5 + 6, island.LAGOON_R - 6)
        x, y = C[0] + math.cos(math.radians(a)) * r, C[1] + math.sin(math.radians(a)) * r
        if clear(x, y):
            out.append(("shrub", SHRUBS["white_lily"], x, y, SEA + 0.05, float(rng.uniform(0, 360)), 1.0))
    return out


def sea_islets():
    """Pokitaru's own rock and grass islets out in the sea, between the island and the horizon."""
    rng = np.random.default_rng(11)
    out = []
    routes = [((411, 744), (300, 800))]
    while len(out) < 22:
        a = rng.uniform(0, 360)
        r = rng.uniform(330, 560)
        x, y = C[0] + math.cos(math.radians(a)) * r, C[1] + math.sin(math.radians(a)) * r
        if island.height(x, y) > SEA - 12 or any(math.hypot(x - p[0], y - p[1]) < 70 for p in out):
            continue
        if any(math.hypot(x - (p0[0] + p1[0]) / 2, y - (p0[1] + p1[1]) / 2) < 120 for p0, p1 in routes):
            continue
        if math.hypot(x - 490, y - 403) < 120:      # the ship's way in
            continue
        kind = TIES["rock_islet"] if rng.random() < 0.6 else TIES["grass_islet"]
        out.append((x, y))
        yield ("tie", kind, float(x), float(y), SEA - 3.0, float(rng.uniform(0, 360)), float(rng.uniform(0.6, 1.1)))


def town():
    """Pokitaru's turbine towers on the far islands (island.TOWN)."""
    rng = np.random.default_rng(5)
    out = []
    for (tx, ty, tr, th) in island.TOWN:
        n = 3 if tr > 42 else 2
        for k in range(n):
            a = rng.uniform(0, 360)
            d = rng.uniform(0, tr * 0.35)
            x, y = tx + math.cos(math.radians(a)) * d, ty + math.sin(math.radians(a)) * d
            z = float(island.height(x, y))
            out.append(("tie", int(rng.choice(TOWERS)), x, y, z - 0.5, float(rng.uniform(0, 360)), 1.0))
    return out


def decor():
    """Pokitaru's bushes and flowers in the house's planters."""
    out = []
    for k, (x, y) in enumerate(house.planters()):
        z = house.F0 + 0.95
        out.append(("tie", TIES["leafy_tree"] if k % 2 == 0 else TIES["round_bush"], x, y, z, k * 47.0, 0.55))
        out.append(("shrub", SHRUBS["red_fruit"], x + 0.5, y - 0.4, z, k * 31.0, 0.8))
    return out
