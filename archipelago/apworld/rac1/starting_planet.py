# SPDX-License-Identifier: GPL-3.0-or-later
from .locations import EVENT_ROWS
from .rules import holds_progression

SHIP_VENDORS = {"Eudora", "Gaspar", "Pokitaru", "Hoven"}

def route_open(row, logic, items):
    needs = set(row.hard) | (set(row.soft) if logic != 2 else set())
    return (row.kind not in ("start", "vendor", "gold_vendor", "gold", "skill")
            and not row.goal and not row.costly and not row.planets
            and needs <= items and (not row.any or set(row.any) & items))

def can_begin_progression(world, planet, initial_items, reserved_vendor=None):
    # A fixed reward alone (for example Skid's Hoverboard with Items shuffle off)
    # must not qualify a dead-end start that has no place for an exit item.
    items, reachable, seen = set(initial_items), {planet}, set()
    rows = world.location_rows() + EVENT_ROWS
    for _ in rows:
        changed = False
        for row in rows:
            needs = set(row.hard) | (set(row.soft) if world.options.logic != 2 else set())
            if (row.name in seen or row.planet not in reachable
                    or not set(row.planets) <= reachable or not needs <= items
                    or (row.any and not set(row.any) & items)):
                continue
            seen.add(row.name)
            changed = True
            if (row.pool and world.shuffled(row) and row.name != reserved_vendor
                    and holds_progression(row, world.options.logic)):
                return True
            if row.goal:
                return True
            reward = world.vanilla_item(row)
            if reward.endswith(" Coordinates"):
                destination = reward.removesuffix(" Coordinates")
                reachable.add("Novalis" if destination == planet else destination)
            else:
                items.add(reward)
        if not changed:
            break
    return False

def starting_candidates(world):
    rows = world.location_rows() + EVENT_ROWS
    planets = sorted({row.planet for row in rows} - {"Veldin", "Veldin (return)"})
    candidates = []
    for planet in planets:
        if not world.options.shuffle_infobots and planet != "Novalis":
            # With fixed coordinates, the exchange must not put Novalis behind itself.
            reachable = {planet}
            for _ in planets:
                for row in rows:
                    if row.planet in reachable and set(row.planets) <= reachable and row.vanilla:
                        destination = row.vanilla.removesuffix(" Coordinates")
                        if row.vanilla.endswith(" Coordinates"):
                            reachable.add("Novalis" if destination == planet else destination)
            if "Novalis" not in reachable:
                continue
        routes = [row for row in rows if row.planet == planet]
        if (any(route_open(row, world.options.logic, set()) for row in routes)
                and can_begin_progression(world, planet, set())):
            candidates.append((planet, None, None))
            continue
        if planet not in SHIP_VENDORS:
            continue
        vendors = [row for row in routes if row.kind == "vendor"
                   and world.shuffled(row) and holds_progression(row, world.options.logic)]
        # Restrict this exception to actual local gadget/weapon items still in the pool.
        items = sorted({world.vanilla_item(row) for row in world.location_rows()
                        if world.shuffled(row) and row.kind in ("give", "vendor", "start")})
        keys = [item for item in items if any(route_open(row, world.options.logic, {item}) for row in routes)]
        keys = [key for key in keys if vendors
                and can_begin_progression(world, planet, {key}, vendors[0].name)]
        if vendors and keys:
            candidates.append((planet, vendors[0].name, keys))
    return candidates
