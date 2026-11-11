// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The cheats in step with the launcher's list of mods (settings_file.h).
#include "settings_file.h"

#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include <nlohmann/json.hpp>

namespace cheats {

namespace {

// The list keeps its keys in the order it has them, as the launcher writes them.
using Json = nlohmann::ordered_json;

// The settings' keys, as the manifest names them.
constexpr const char* kInvincible = "invincible";
constexpr const char* kInfiniteAmmo = "infinite_ammo";
constexpr const char* kInfiniteAir = "infinite_air";
constexpr const char* kMultiplier = "bolt_multiplier";

// Returns mod `id`'s entry in the parsed list, or null when the list has none.
Json* entry_of(Json& list, const std::string& id) {
    // Not a list of mods.
    if (!list.is_object() || !list.contains("mods") || !list["mods"].is_array()) {
        return nullptr;
    }

    // Each mod listed, until the one with this id.
    for (Json& m : list["mods"]) {
        // This mod.
        if (m.is_object() && m.contains("id") && m["id"].is_string() && m["id"] == id) {
            return &m;
        }
    }

    return nullptr;
}

// Returns the parsed text, discarded (null) when it is not JSON.
Json parse(const std::string& text) {
    return Json::parse(text, nullptr, false);
}

// Sets `value` from `settings[key]` when it is a bool there.
void take_bool(const Json& settings, const char* key, bool& value) {
    // A bool under this key.
    if (settings.contains(key) && settings[key].is_boolean()) {
        value = settings[key].get<bool>();
    }
}

// Returns the whole of a file, or nothing when it cannot be read.
std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);

    // Not there, or not readable.
    if (!in) {
        return std::nullopt;
    }

    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

std::optional<State> read_settings(
    const std::string& text, const std::string& id, const State& base
) {
    Json list = parse(text);
    const Json* m = entry_of(list, id);

    // Not a list, or the mod is not in it.
    if (m == nullptr) {
        return std::nullopt;
    }

    State state = base;

    // No settings object: the mod's defaults are the launcher's, the base's here.
    if (!m->contains("settings") || !(*m)["settings"].is_object()) {
        return state;
    }

    const Json& settings = (*m)["settings"];
    take_bool(settings, kInvincible, state.invincible);
    take_bool(settings, kInfiniteAmmo, state.infinite_ammo);
    take_bool(settings, kInfiniteAir, state.infinite_air);

    // The multiplier, a choice's value (a string).
    if (settings.contains(kMultiplier) && settings[kMultiplier].is_string()) {
        state.multiplier = parse_multiplier(settings[kMultiplier].get<std::string>());
    }

    return state;
}

std::optional<std::string> write_settings(
    const std::string& text, const std::string& id, const State& state
) {
    Json list = parse(text);
    Json* m = entry_of(list, id);

    // Not a list, or the mod is not in it.
    if (m == nullptr) {
        return std::nullopt;
    }

    // No settings object yet: one, as the launcher makes it.
    if (!m->contains("settings") || !(*m)["settings"].is_object()) {
        (*m)["settings"] = Json::object();
    }

    Json& settings = (*m)["settings"];
    settings[kInvincible] = state.invincible;
    settings[kInfiniteAmmo] = state.infinite_ammo;
    settings[kInfiniteAir] = state.infinite_air;
    settings[kMultiplier] = std::to_string(state.multiplier);
    // Pretty, two spaces and a last newline, as the launcher writes it.
    return list.dump(2) + "\n";
}

void SettingsFile::open(const std::filesystem::path& path, const std::string& id) {
    m_path = path;
    m_id = id;
    m_seen = stamp();
}

std::optional<std::filesystem::file_time_type> SettingsFile::stamp() const {
    std::error_code e;
    const auto t = std::filesystem::last_write_time(m_path, e);
    return e ? std::nullopt : std::optional<std::filesystem::file_time_type>(t);
}

bool SettingsFile::save(const State& state, std::string& error) {
    // No file followed.
    if (!ready()) {
        error = "no list of mods";
        return false;
    }

    const std::optional<std::string> text = read_file(m_path);

    // The list is gone, or cannot be read.
    if (!text) {
        error = "cannot read " + m_path.string();
        return false;
    }

    const std::optional<std::string> next = write_settings(*text, m_id, state);

    // Not a list of mods, or this mod is not in it.
    if (!next) {
        error = m_path.string() + " has no entry for " + m_id;
        return false;
    }

    // Already as it should be: nothing to write.
    if (*next == *text) {
        return true;
    }

    // The whole file beside it, then renamed over it, so the launcher and the port never read half
    // of it.
    std::filesystem::path temporary = m_path;
    temporary += ".cheats.tmp";

    // Written whole, or not at all.
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << *next;

        // The disk refused it.
        if (!out.flush()) {
            error = "cannot write " + temporary.string();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }

    std::error_code e;
    std::filesystem::rename(temporary, m_path, e);

    // The rename was refused (the launcher writing it at that moment, say).
    if (e) {
        error = "cannot replace " + m_path.string() + ": " + e.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }

    // The mod's own change is not one to read back.
    m_seen = stamp();
    return true;
}

bool SettingsFile::poll(State& state) {
    // No file followed.
    if (!ready()) {
        return false;
    }

    const auto now = stamp();

    // Unchanged since it was last seen (or still gone).
    if (now == m_seen) {
        return false;
    }

    m_seen = now;
    const std::optional<std::string> text = read_file(m_path);

    // Gone, or not readable at the moment: looked at again when it changes.
    if (!text) {
        return false;
    }

    const std::optional<State> read = read_settings(*text, m_id, state);

    // Not a list (half written by another program), or no entry for the mod.
    if (!read) {
        m_seen.reset();
        return false;
    }

    // The same cheats: a change to something else in the file.
    if (read->invincible == state.invincible && read->infinite_ammo == state.infinite_ammo
        && read->infinite_air == state.infinite_air && read->multiplier == state.multiplier) {
        return false;
    }

    state = *read;
    return true;
}

std::optional<std::filesystem::path> list_beside(
    const std::filesystem::path& library, const std::string& id
) {
    // <plugins>/<id>/lib/<platform>/<file>: three folders up is the mod's own.
    const std::filesystem::path folder = library.parent_path().parent_path().parent_path();
    const std::filesystem::path list = folder.parent_path() / "plugins.json";
    std::error_code e;

    // The folder is this mod's, with its manifest, and the list is beside it.
    if (folder.filename() == id && std::filesystem::is_regular_file(folder / "openrac-mod.json", e)
        && std::filesystem::is_regular_file(list, e)) {
        return list;
    }

    return std::nullopt;
}

}  // namespace cheats
