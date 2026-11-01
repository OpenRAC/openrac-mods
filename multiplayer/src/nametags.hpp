/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The names over the other players' figures: what a tag says and looks like, up close to far away,
 * drawn through a TagPainter so that it is tested without a game. multiplayer.cpp's painter is the
 * port's overlay (the SDK's openrac_overlay). Only figures that are shown have a tag, so a scene or
 * a figure out of the level has none, and the port draws none under a menu.
 */
#ifndef MULTIPLAYER_NAMETAGS_H
#define MULTIPLAYER_NAMETAGS_H

#include <cstdint>
#include <string>
#include <vector>

namespace mp {

/* How high over a figure's feet a tag is; where its text starts to shrink from; where it starts to
 * fade, and where only a dot is left (the level's units, metres). */
constexpr float kTagAbove = 1.9f;
constexpr float kTagNear = 8.0f;
constexpr float kTagFadeFrom = 30.0f;
constexpr float kTagDot = 40.0f;

/* The text's height up close and far away, on the 512 x 448 picture, and a far player's dot. */
constexpr float kTagTextNear = 10.0f;
constexpr float kTagTextFar = 7.0f;
constexpr float kTagDotSize = 4.0f;

/* When a tag's mark says the player is lagging: no pose for this long, or this round trip. */
constexpr double kTagLateMs = 500.0;
constexpr std::int32_t kTagPingMs = 250;

/* A player to show. `slot` is its place in the party, the same in every game: its colour. */
struct NameTag {
    std::string name;
    int slot = 0;
    float position[3] = {0, 0, 0};
    bool late = false;
};

/* What the port gives to draw with. Colours are 0xRRGGBBAA. */
class TagPainter {
public:
    virtual ~TagPainter() = default;
    virtual bool project(const float world[3], float& x, float& y) = 0;
    virtual void rect(float x, float y, float w, float h, std::uint32_t colour) = 0;
    virtual void text(float x, float y, float height, std::uint32_t colour, const std::string& line) = 0;
    virtual float text_width(float height, const std::string& line) = 0;
    virtual float fade(float distance, float near_distance, float far_distance) = 0;
};

/* What the last frame's tags came to: for the bot (bot.hpp). */
struct TagStats {
    int tags = 0;       // players who could have one
    int on_picture = 0; // those in front of the camera, on the picture
    float x = 0;        // where the last of those is
    float y = 0;
};

/* A slot's colour, opaque. */
std::uint32_t tag_colour(int slot);

/* Draws `tags` as seen from `camera`. */
TagStats draw_name_tags(TagPainter& painter, const float camera[3], const std::vector<NameTag>& tags);

}  // namespace mp

#endif
