// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The names over the other players' figures (nametags.hpp).

#include "nametags.hpp"

#include <algorithm>
#include <cmath>

namespace mp {
namespace {

// The picture's width in pixels, as nametags.hpp says (512 x 448); tags are kept inside it.
constexpr float kPictureWidth = 512.0f;
constexpr float kMargin = 2.0f;  // around the text, and to the picture's edge
constexpr float kMark = 4.0f;    // the lagging mark's size
constexpr float kSeen = 0.004f;  // a tag this faint draws nothing
// The colours (0xRRGGBBAA) of a tag's backing plate and of the lagging mark.
constexpr std::uint32_t kBack = 0x000000A0u;
constexpr std::uint32_t kLate = 0xFFB020FFu;

// Returns `colour` (0xRRGGBBAA) with its alpha scaled by `a` (0 to 1).
std::uint32_t faded(std::uint32_t colour, float a) {
    const float alpha = static_cast<float>(colour & 0xFFu) * std::clamp(a, 0.0f, 1.0f);
    return (colour & 0xFFFFFF00u) | static_cast<std::uint32_t>(alpha);
}

}  // namespace

std::uint32_t tag_colour(int slot) {
    // Bright on any level, told apart at a glance.
    constexpr std::uint32_t kColours[] = {0x66CCFFFFu, 0xFFD24DFFu, 0x7CE36BFFu, 0xFF8FB8FFu};
    constexpr int kCount = static_cast<int>(sizeof kColours / sizeof kColours[0]);
    return kColours[((slot % kCount) + kCount) % kCount];
}

void TagSight::asked(bool clear, double now_ms) {
    asked_ms_ = now_ms;

    // Only a clear line renews how long the tag holds.
    if (clear) {
        clear_ms_ = now_ms;
    }
}

float TagSight::advance(double now_ms) {
    const double since = advanced_ms_ < 0.0 ? 0.0 : now_ms - advanced_ms_;
    const double step = std::clamp(since, 0.0, kTagSightFadeMs) / kTagSightFadeMs;
    const bool seen = now_ms - clear_ms_ < kTagHoldMs;
    advanced_ms_ = now_ms;
    strength_ = std::clamp(strength_ + static_cast<float>(seen ? step : -step), 0.0f, 1.0f);
    return strength_;
}

TagStats draw_name_tags(
    TagPainter& painter, const float camera[3], const std::vector<NameTag>& tags
) {
    TagStats stats;

    // Each player gets a tag when it is on the picture and not faded out.
    for (const NameTag& tag : tags) {
        ++stats.tags;
        const float at[3] = {tag.position[0], tag.position[1], tag.position[2] + kTagAbove};
        float x = 0;
        float y = 0;

        // A figure the camera does not see has nowhere to put a tag.
        if (!painter.project(at, x, y)) {
            continue;
        }

        ++stats.on_picture;
        stats.x = x;
        stats.y = y;
        const float dx = at[0] - camera[0];
        const float dy = at[1] - camera[1];
        const float dz = at[2] - camera[2];
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const std::uint32_t colour = tag_colour(tag.slot);
        const float name = painter.fade(distance, kTagFadeFrom, kTagGone) * tag.sight;

        // A tag too faint to see draws nothing.
        if (name <= kSeen) {
            continue;
        }

        ++stats.drawn;
        const float size =
            kTagTextFar
            + (kTagTextNear - kTagTextFar) * painter.fade(distance, kTagNear, kTagFadeFrom);
        const float width = painter.text_width(size, tag.name);
        const float mark = tag.late ? kMark + kMargin : 0.0f;
        const float whole = width + mark;
        // The tag is centred over the figure and kept inside the picture.
        const float left =
            std::clamp(x - whole / 2, kMargin, std::max(kMargin, kPictureWidth - whole - kMargin));
        const float top = std::max(kMargin, y - size - kMargin);
        // The backing plate is the text plus a margin all round.
        painter.rect(
            left - kMargin,
            top - kMargin,
            whole + 2 * kMargin,
            size + 2 * kMargin,
            faded(kBack, name)
        );
        painter.text(left, top, size, faded(colour, name), tag.name);

        // A lagging player gets a mark after its name.
        if (tag.late) {
            painter.rect(
                left + width + kMargin, top + (size - kMark) / 2, kMark, kMark, faded(kLate, name)
            );
        }
    }

    return stats;
}

}  // namespace mp
