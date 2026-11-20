// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The collectors' pay (collectors.h).

#include "collectors.h"

#include <cmath>
#include <limits>

namespace ng_plus_plus {
namespace {

// The levels the collectors are in: Tabora and Grelbin.
constexpr int kTabora = 8;
constexpr int kGrelbin = 19;

// The printf number the dialogs print the count and the total with.
constexpr std::string_view kNumber = "%d";

}  // namespace

std::optional<std::string> with_total(std::string_view format, std::int32_t total) {
    const std::size_t first = format.find(kNumber);

    // No count.
    if (first == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t second = format.find(kNumber, first + kNumber.size());

    // No total.
    if (second == std::string_view::npos) {
        return std::nullopt;
    }

    std::string out(format.substr(0, second));
    out += std::to_string(total);
    out += format.substr(second + kNumber.size());
    return out;
}

std::int32_t collector_total(std::int32_t count, std::int32_t pay, float scale) {
    // Nothing handed in.
    if (count <= 0 || pay <= 0) {
        return 0;
    }

    const double total = std::floor(static_cast<double>(count) * pay * scale + 0.5);
    constexpr double kMost = std::numeric_limits<std::int32_t>::max();
    return total >= kMost ? std::numeric_limits<std::int32_t>::max()
                          : static_cast<std::int32_t>(total);
}

float Collectors::scale() const {
    return run_scale(m_view.cycle(), m_scaling);
}

std::int32_t Collectors::pay_now(std::int32_t pay) const {
    return m_view.cycle() != 0 ? pay * kChallengePayTimes : pay;
}

std::optional<std::string> Collectors::dialog(std::int32_t id) const {
    std::int32_t count = 0;
    std::int32_t pay = 0;

    // Only the two dialogs, each with what it trades.
    if (id == kCrystalTradeText) {
        count = m_view.crystals();
        pay = kCrystalPay;
    } else if (id == kMoonstoneTradeText) {
        count = m_view.moonstones();
        pay = kMoonstonePay;
    } else {
        return std::nullopt;
    }

    const float run = scale();

    // The game's pay, or nothing to trade: the game's text.
    if (run == 1.0f || count <= 0) {
        return std::nullopt;
    }

    return with_total(m_view.text(id), collector_total(count, pay_now(pay), run));
}

void Collectors::tick() {
    const int level = m_view.loaded_level();
    const std::int32_t bolts = m_view.bolts();
    const std::int32_t crystals = m_view.crystals();
    const std::int32_t moonstones = m_view.moonstones();
    const float run = scale();

    // A trade: in the collector's level since the last tick, its count down to 0, the bolts up by
    // at least the game's pay for it. The game paid count x pay; the rest of the scaled pay here.
    if (m_seen && level == m_level && run != 1.0f) {
        const std::int32_t gained = bolts - m_bolts;
        std::int32_t paid = 0;
        std::int32_t wanted = 0;

        // Tabora's crystals.
        if (level == kTabora && m_crystals > 0 && crystals == 0) {
            paid = m_crystals * pay_now(kCrystalPay);
            wanted = collector_total(m_crystals, pay_now(kCrystalPay), run);
        }

        // Grelbin's moonstones.
        if (level == kGrelbin && m_moonstones > 0 && moonstones == 0) {
            paid = m_moonstones * pay_now(kMoonstonePay);
            wanted = collector_total(m_moonstones, pay_now(kMoonstonePay), run);
        }

        // The game's pay came in: the rest.
        if (paid > 0 && gained >= paid && wanted > paid) {
            m_mod.bolts(wanted - paid);
            m_mod.info(
                "collector paid " + std::to_string(wanted) + " bolts (the game's "
                + std::to_string(paid) + ")"
            );
        }
    }

    m_seen = true;
    m_level = level;
    m_bolts = m_view.bolts();
    m_crystals = crystals;
    m_moonstones = moonstones;
}

}  // namespace ng_plus_plus
