/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The names over the other players' figures (nametags.hpp).
 */
#include "nametags.hpp"

#include <algorithm>
#include <cmath>

namespace mp {
namespace {

constexpr float kPictureWidth = 512.0f;
constexpr float kMargin = 2.0f;   // around the text, and to the picture's edge
constexpr float kMark = 4.0f;     // the lagging mark's size
constexpr std::uint32_t kBack = 0x000000A0u;
constexpr std::uint32_t kLate = 0xFFB020FFu;

/* A colour with its alpha scaled by `a` (0 to 1). */
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

TagStats draw_name_tags(TagPainter& painter, const float camera[3], const std::vector<NameTag>& tags) {
    TagStats stats;

    for (const NameTag& tag : tags) {
        ++stats.tags;
        const float at[3] = {tag.position[0], tag.position[1], tag.position[2] + kTagAbove};
        float x = 0;
        float y = 0;

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
        // The name gives way to a dot as it fades, so a player far off is still there.
        const float name = painter.fade(distance, kTagFadeFrom, kTagDot);

        if (name < 1.0f) {
            painter.rect(x - kTagDotSize / 2, y - kTagDotSize / 2, kTagDotSize, kTagDotSize, faded(colour, 1.0f - name));
        }

        if (name <= 0.0f) {
            continue;
        }

        const float size = kTagTextFar + (kTagTextNear - kTagTextFar) * painter.fade(distance, kTagNear, kTagFadeFrom);
        const float width = painter.text_width(size, tag.name);
        const float mark = tag.late ? kMark + kMargin : 0.0f;
        const float whole = width + mark;
        const float left = std::clamp(x - whole / 2, kMargin, std::max(kMargin, kPictureWidth - whole - kMargin));
        const float top = std::max(kMargin, y - size - kMargin);
        painter.rect(left - kMargin, top - kMargin, whole + 2 * kMargin, size + 2 * kMargin, faded(kBack, name));
        painter.text(left, top, size, faded(colour, name), tag.name);

        if (tag.late) {
            painter.rect(left + width + kMargin, top + (size - kMark) / 2, kMark, kMark, faded(kLate, name));
        }
    }

    return stats;
}

}  // namespace mp
