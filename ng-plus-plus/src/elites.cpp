// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Elite enemies and the modifiers (elites.h).

#include "elites.h"

#include <string>

#include "discs.h"
#include "weapon_visuals.h"

namespace ng_plus_plus {
namespace {

// An elite's glow where the port has energy but no obsidian (1.25 without 1.24 is no port, but a
// refused obsidian falls back to it): violet.
constexpr std::uint32_t kEliteRgb = 0xA040FFu;

}  // namespace

void Elites::start() {
    m_material = moby_material_of(m_mod.api());
    m_obsidian = has_obsidian(m_mod.api());

    // What this port lacks, for a player's log.
    if (m_on && m_material == nullptr) {
        m_mod.info("elite enemies are not drawn in this build (they need OpenRAC 1.24)");
    }
}

Modifier Elites::modifier() const {
    return m_modifiers ? modifier_of(m_view.cycle()) : Modifier::None;
}

void Elites::copy_made(int slot, int index, std::uint32_t copy) {
    const int percent = elite_percent(m_view.cycle(), modifier());

    // The setting off, or this copy's roll says no.
    if (!m_on || copy == 0
        || !ng_plus_plus::is_elite(m_view.loaded_level(), slot, index, percent)) {
        return;
    }

    mark(copy);
}

void Elites::arena_spawn(std::uint32_t moby) {
    // Only from the second challenge mode.
    if (!m_on || m_view.cycle() < 2) {
        return;
    }

    // Every kArenaEliteEvery-th.
    if (++m_arena_seen % kArenaEliteEvery == 0) {
        mark(moby);
    }
}

bool Elites::is_elite(std::uint32_t moby) const {
    const auto found = m_elites.find(moby);

    // Not one, or its slot holds another class now.
    if (found == m_elites.end()) {
        return false;
    }

    const std::optional<int> now = m_view.moby_class(moby);
    return now && *now == found->second;
}

float Elites::drops(std::uint32_t moby) const {
    const float bounty = modifier() == Modifier::Bounty ? kBountyDrops : 1.0f;
    return (is_elite(moby) ? kEliteDrops : 1.0f) * bounty;
}

void Elites::mark(std::uint32_t moby) {
    const std::optional<int> moby_class = m_view.moby_class(moby);

    // Not a moby.
    if (!moby_class) {
        return;
    }

    enter(m_view.loaded_level());
    m_elites[moby] = *moby_class;
    redraw(moby);
}

void Elites::redraw(std::uint32_t moby) const {
    // A port without materials draws it as the game does.
    if (m_material == nullptr || !is_elite(moby)) {
        return;
    }

    const bool drawn =
        m_obsidian
        && m_material(m_mod.handle(), moby, kMaterialObsidian, kNoTint) == OPENRAC_HOOK_OK;

    // Obsidian refused: violet energy.
    if (!drawn && m_mod.api().api_minor >= kEnergyMinor) {
        m_material(m_mod.handle(), moby, energy_at(10), kEliteRgb);
    }
}

void Elites::died(std::uint32_t moby) {
    m_elites.erase(moby);
}

void Elites::tick() {
    enter(m_view.loaded_level());
}

void Elites::enter(int level) {
    // Another level: its elites are gone with it.
    if (level != m_level) {
        m_elites.clear();
        m_arena_seen = 0;
        m_level = level;

        const Modifier now = modifier();

        // The run's modifier, named once a run as its first level loads.
        if (level >= 0 && now != Modifier::None && m_named_cycle != m_view.cycle()) {
            m_named_cycle = m_view.cycle();
            const std::string text = std::string("Challenge: ") + modifier_name(now);
            m_mod.info("this run's modifier: " + std::string(modifier_name(now)));
            m_mod.api().play_notice(m_mod.handle(), text.c_str());
        }
    }
}

}  // namespace ng_plus_plus
