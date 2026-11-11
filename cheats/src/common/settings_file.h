// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The cheats kept in step with the launcher's list of mods (README.md, "In step with the
// launcher"): plugins.json, which the launcher writes and the port reads at start, holds each mod's
// settings. A cheat switched in the game is written there, so the launcher shows it and the next
// start begins with it; a setting changed in the launcher while the game runs is read back.
//
// OpenRAC's interface has no call for a mod to write its own settings (docs/port/MODS.md, section
// 12, "settings in the game"); until it has one, the mod edits the file as the launcher does (a
// whole file written beside it, then renamed over it), keeping every other line of it.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "cheats.h"

namespace cheats {

// Returns the cheats that mod `id` has in `text` (plugins.json), over `base`: a setting missing or
// of another type keeps base's value. Nothing when the text is not a list of mods, or has no entry
// for `id`.
std::optional<State> read_settings(
    const std::string& text, const std::string& id, const State& base
);

// Returns `text` (plugins.json) with mod `id`'s cheats set to `state`, everything else as it was.
// Nothing when the text is not a list of mods, or has no entry for `id`.
std::optional<std::string> write_settings(
    const std::string& text, const std::string& id, const State& state
);

// The launcher's list of mods on disk, for one mod.
class SettingsFile {
public:
    // Follows the file at `path` for mod `id`, from its state now.
    void open(const std::filesystem::path& path, const std::string& id);

    // Returns whether a file is followed.
    bool ready() const { return !m_path.empty(); }

    // Returns the file followed.
    const std::filesystem::path& path() const { return m_path; }

    // Writes `state` into the file. Returns false and says why in `error` when it cannot.
    bool save(const State& state, std::string& error);

    // Returns true, with `state` set to the file's, when the file changed since it was last seen
    // and holds other cheats than `state`.
    bool poll(State& state);

private:
    // When the file was last seen changed, or nothing when it was not.
    std::optional<std::filesystem::file_time_type> stamp() const;

    std::filesystem::path m_path;
    std::string m_id;
    std::optional<std::filesystem::file_time_type> m_seen;
};

// Returns the plugins.json beside the mod's own folder: the library at `library`
// (<plugins>/<id>/lib/<platform>/<file>), when that folder is mod `id`'s and the list is there;
// nothing otherwise.
std::optional<std::filesystem::path> list_beside(
    const std::filesystem::path& library, const std::string& id
);

// Returns the path of the library this code is in, or nothing when the system does not say.
std::optional<std::filesystem::path> own_library();

}  // namespace cheats
