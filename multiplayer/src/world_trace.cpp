/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The world's verbose log (world.hpp, trace.hpp): each tick, after the world's own, what the game
 * is doing (pause), how its run list treated the mobys (run), what changed of each (vis, drive),
 * and every second a census.
 */
#include <cstdio>

#include "world.hpp"

namespace mp {
namespace {

const char* menu_name(int menu) {
    switch (menu) {
        case 1:
            return "held";
        case 2:
            return "first";
        case 3:
            return "level-camera";
        case 4:
            return "menu-camera";
        case 5:
            return "freeze";
        default:
            return "none";
    }
}

const char* ran_name(int ran) {
    switch (ran) {
        case 0:
            return "far";
        case 1:
            return "drawn";
        case 2:
            return "always";
        case 3:
            return "near";
        case 4:
            return "eye";
        case 5:
            return "group";
        default:
            return "none";
    }
}

std::string id_name(std::uint16_t id) {
    return (id & kSpawnedId) != 0 ? "n" + std::to_string(id & ~kSpawnedId) : std::to_string(id);
}

/* At most 200 of `names`, then how many more. */
std::string names_of(const std::vector<std::string>& names) {
    std::string out;

    for (std::size_t i = 0; i < names.size() && i < 200; ++i) {
        out += (i != 0 ? " " : "") + names[i];
    }

    if (names.size() > 200) {
        out += " +" + std::to_string(names.size() - 200) + " more";
    }

    return "[" + out + "]";
}

std::string metres(float d) {
    char b[24];
    std::snprintf(b, sizeof b, "%.1f", d);
    return b;
}

/* A moby's census id: its key, or its slot and class for a spawned one. */
std::string census_id(const MobyState& m) {
    return m.key >= 0 ? "k" + std::to_string(m.key)
                      : "s" + std::to_string(m.slot) + "c" + std::to_string(m.oclass);
}

}  // namespace

std::string World::moby_name(int slot) const {
    const auto it = traced_.listed.find(slot);
    const auto now = mobys_.find(slot);
    const MobyState* m = now != mobys_.end()          ? &now->second
                         : it != traced_.listed.end() ? &it->second
                                                      : nullptr;

    if (m == nullptr) {
        return "slot " + std::to_string(slot);
    }

    return "slot " + std::to_string(slot) + " class " + std::to_string(m->oclass) + " key "
           + std::to_string(m->key) + " at " + xyz(m->pose.position);
}

bool World::kill(int slot, const std::string& why) {
    const bool killed = game_.moby_kill(slot);
    trace_line(
        "despawn",
        std::string("moby_kill ") + (killed ? "" : "(refused) ") + moby_name(slot) + " by sync ("
            + why + ")"
    );
    return killed;
}

void World::trace_snapshot(
    const std::string& how, std::set<std::uint16_t>& last, const std::set<std::uint16_t>& ids
) {
    std::vector<std::string> added;
    std::vector<std::string> missing;

    for (const std::uint16_t id : ids) {
        if (last.count(id) == 0) {
            added.push_back(id_name(id));
        }
    }

    for (const std::uint16_t id : last) {
        if (ids.count(id) == 0) {
            missing.push_back(id_name(id));
        }
    }

    trace_line(
        "snap",
        how + " n=" + std::to_string(ids.size()) + " added " + names_of(added) + " missing "
            + names_of(missing)
    );
    last = ids;
}

void World::trace_tick(std::uint64_t now) {
    const GameStatus status = game_.status();
    const bool level = in_level(status.phase);
    MobyRun run;
    const bool has_run = level && game_.moby_run(-1, run);
    float camera[3] = {0, 0, 0};
    const bool has_camera = level && game_.camera(camera);
    const bool menu = status.phase == kPhaseMenu;

    // A new level load: nothing of the last one is compared.
    if (status.loads != traced_.loads) {
        trace_line(
            "pause",
            "load " + std::to_string(status.loads) + " level " + std::to_string(status.level)
        );
        traced_.loads = status.loads;
        traced_.listed.clear();
        traced_.in_range.clear();
        traced_.census.clear();
    }

    // What the game does, and the cameras: the one mods read, the run list's, the level's that the
    // pause menu puts up for the world.
    std::string state = "phase " + std::to_string(status.phase) + " level "
                        + std::to_string(status.level) + " mode "
                        + (has_run ? std::to_string(run.level_mode) : std::string("?")) + " scene "
                        + std::to_string(status.scene);
    std::string cameras = " camera_position " + (has_camera ? xyz(camera) : std::string("?"));

    if (has_run) {
        cameras += " run_camera " + xyz(run.camera) + " level_camera " + xyz(run.level_camera)
                   + " menu " + menu_name(run.menu) + " menu_ticks "
                   + std::to_string(run.menu_ticks) + " context " + std::to_string(run.context)
                   + " runs " + std::to_string(run.runs);
    }

    if (status.phase != traced_.phase) {
        trace_line(
            "pause",
            "phase " + std::to_string(traced_.phase) + " -> " + std::to_string(status.phase) + " "
                + state + cameras
        );
    }

    if (has_run && run.level_mode != traced_.level_mode) {
        trace_line(
            "pause",
            "mode " + std::to_string(traced_.level_mode) + " -> " + std::to_string(run.level_mode)
                + " " + state
        );
        traced_.level_mode = run.level_mode;
    }

    if (menu != traced_.menu) {
        trace_line("pause", std::string(menu ? "enter " : "exit ") + state + cameras);
        traced_.menu_listed = false;
    } else if (menu) {
        trace_line("pause", "frame " + state + cameras);
    }

    traced_.phase = status.phase;
    traced_.menu = menu;

    std::vector<MobyState> listing = level ? game_.mobys() : std::vector<MobyState>();
    std::map<int, MobyState> listed;
    int far = 0;
    int ran = 0;

    for (const MobyState& m : listing) {
        listed[m.slot] = m;
    }

    for (const auto& [slot, m] : listed) {
        const bool dead = (m.flags & kMobyDead) != 0;
        const auto was = traced_.listed.find(slot);
        const bool same = was != traced_.listed.end() && was->second.oclass == m.oclass
                          && was->second.key == m.key;
        const std::string name = "slot " + std::to_string(slot) + " class "
                                 + std::to_string(m.oclass) + " key " + std::to_string(m.key)
                                 + " at " + xyz(m.pose.position);

        // Hidden or shown, dead, drawn or not while a menu is open.
        if (same) {
            const std::uint32_t changed = was->second.flags ^ m.flags;

            if ((changed & kMobyHidden) != 0) {
                trace_line(
                    "vis",
                    std::string((m.flags & kMobyHidden) != 0 ? "hidden " : "shown ") + name
                        + " state " + std::to_string(m.state)
                );
            }

            if ((changed & kMobyDead) != 0 && dead) {
                trace_line("vis", "dead " + name + " state " + std::to_string(m.state));
            }

            if ((changed & kMobyDrawn) != 0 && menu) {
                trace_line(
                    "vis", std::string((m.flags & kMobyDrawn) != 0 ? "drawn " : "undrawn ") + name
                );
            }
        }

        if (dead || !has_run) {
            continue;
        }

        // The run list: left out by distance or run, and the change between the two.
        MobyRun r;

        if (!game_.moby_run(slot, r) || r.ran == kRunNone) {
            continue;
        }

        const bool in_range = r.ran != kRunFar;
        far += in_range ? 0 : 1;
        ran += in_range ? 1 : 0;
        const std::string detail = name + " distance " + metres(r.distance) + " update_dist "
                                   + std::to_string(r.update_dist) + " draw_dist "
                                   + std::to_string(r.draw_dist) + " drawn "
                                   + std::to_string(r.drawn) + " ran " + ran_name(r.ran) + " menu "
                                   + menu_name(r.menu);
        const auto before = traced_.in_range.find(slot);

        if (before != traced_.in_range.end() && before->second != in_range) {
            trace_line("run", std::string(in_range ? "in " : "out ") + detail);
        } else if (menu && !traced_.menu_listed && !in_range) {
            trace_line("run", "far " + detail);
        }

        traced_.in_range[slot] = in_range;
    }

    if (menu && has_run) {
        traced_.menu_listed = true;
    }

    // While a menu is open, once a second, how many ran.
    if (menu && has_run && now - traced_.run_ms >= 1000) {
        traced_.run_ms = now;
        trace_line(
            "run",
            "menu ran " + std::to_string(ran) + " far " + std::to_string(far) + " runs "
                + std::to_string(run.runs) + " menu " + menu_name(run.menu)
        );
    }

    // Gone from the listing (its cause, in a party, is the despawn line's).
    for (const auto& [slot, m] : traced_.listed) {
        const auto now_at = listed.find(slot);

        if ((m.flags & kMobyDead) == 0
            && (now_at == listed.end() || now_at->second.oclass != m.oclass
                || now_at->second.key != m.key)) {
            trace_line(
                "vis",
                "gone slot " + std::to_string(slot) + " class " + std::to_string(m.oclass) + " key "
                    + std::to_string(m.key) + " at " + xyz(m.pose.position)
                    + (now_at == listed.end() ? " (left the listing)" : " (its slot reused)")
            );
            traced_.in_range.erase(slot);
        }
    }

    // A level that lists nothing (a load) keeps what was seen.
    if (!listed.empty()) {
        traced_.listed = std::move(listed);
    }

    // Driven from the host's, or let go.
    for (const int slot : driven_) {
        if (traced_.driven.count(slot) == 0) {
            trace_line("drive", "on " + moby_name(slot));
        }
    }

    for (const int slot : traced_.driven) {
        if (driven_.count(slot) == 0) {
            trace_line("drive", "off " + moby_name(slot));
        }
    }

    traced_.driven = driven_;

    // The census: every second, the live mobys by class, and the ones that came and went.
    if (now - traced_.census_ms < 1000 || traced_.listed.empty() || !level) {
        return;
    }

    traced_.census_ms = now;
    std::map<int, int> classes;
    std::set<std::string> ids;

    for (const auto& [slot, m] : traced_.listed) {
        if ((m.flags & kMobyDead) == 0) {
            ++classes[m.oclass];
            ids.insert(census_id(m));
        }
    }

    std::string by_class;

    for (const auto& [oclass, n] : classes) {
        by_class += " " + std::to_string(oclass) + ":" + std::to_string(n);
    }

    std::vector<std::string> came;
    std::vector<std::string> went;

    for (const std::string& id : ids) {
        if (traced_.census.count(id) == 0) {
            came.push_back(id);
        }
    }

    for (const std::string& id : traced_.census) {
        if (ids.count(id) == 0) {
            went.push_back(id);
        }
    }

    trace_line(
        "census", "live " + std::to_string(ids.size()) + " by class" + by_class + " " + state
    );

    // The first census of a level names none as come.
    if (!traced_.census.empty() && (!came.empty() || !went.empty())) {
        trace_line("census", "came " + names_of(came) + " went " + names_of(went));
    }

    traced_.census = std::move(ids);
}

}  // namespace mp
