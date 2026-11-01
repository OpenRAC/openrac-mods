/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The names over the other players' figures: what a tag says and looks like, drawn through a
 * TagPainter so that it is tested without a game. multiplayer.cpp's painter is the port's overlay
 * (the SDK's openrac_overlay). Only figures that are shown have a tag, so a scene or a figure out
 * of the level has none, and the port draws none under a menu. A tag is for a player nearby in
 * plain view: it fades out with distance, and goes when the level stands between the camera and the
 * figure (TagSight).
 */
#ifndef MULTIPLAYER_NAMETAGS_H
#define MULTIPLAYER_NAMETAGS_H

#include <cstdint>
#include <string>
#include <vector>

namespace mp {

/* How high over a figure's feet a tag is; where its text starts to shrink from; where it starts to
 * fade, and where it is gone (the level's units, metres). */
constexpr float kTagAbove = 1.9f;
constexpr float kTagNear = 8.0f;
constexpr float kTagFadeFrom = 18.0f;
constexpr float kTagGone = 25.0f;

/* The text's height up close and at the far end, on the 512 x 448 picture. */
constexpr float kTagTextNear = 10.0f;
constexpr float kTagTextFar = 7.0f;

/* The points of a figure the camera is asked to see, over its feet: its head and its middle. Either
 * one in view is enough, so a player behind a low wall keeps its name. */
constexpr float kTagSeeHead = 1.2f;
constexpr float kTagSeeMiddle = 0.6f;

/* How often a figure's sight is asked (a straight line through the level's collision, twice a
 * figure), how long a tag stays after the last clear line, so a corner or a post going past does not
 * blink it, and how long it takes to come or go. */
constexpr double kTagSightMs = 120.0;
constexpr double kTagHoldMs = 300.0;
constexpr double kTagSightFadeMs = 180.0;

/* When a tag's mark says the player is lagging: no pose for this long, or this round trip. */
constexpr double kTagLateMs = 500.0;
constexpr std::int32_t kTagPingMs = 250;

/* A player to show. `slot` is its place in the party, the same in every game: its colour. */
struct NameTag {
    std::string name;
    int slot = 0;
    float position[3] = {0, 0, 0};
    bool late = false;
    float sight = 1.0f;  // how much of it the level leaves (TagSight), 0 to 1
};

/* Whether a figure can be seen, over time: what the lines through the level said, held a little and
 * eased, so that it costs a few lines a second and never flickers. A new one starts unseen. */
class TagSight {
public:
    /* It is time to ask again. */
    bool due(double now_ms) const { return now_ms - asked_ms_ >= kTagSightMs; }

    /* What the lines said: at least one was clear. */
    void asked(bool clear, double now_ms);

    /* The tag's strength now, 0 to 1. Called once a tick. */
    float advance(double now_ms);

    float strength() const { return strength_; }

private:
    double asked_ms_ = -1.0e12;
    double clear_ms_ = -1.0e12;
    double advanced_ms_ = -1.0;
    float strength_ = 0.0f;
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
    int drawn = 0;      // those near enough and in view: a name is on the picture
    float x = 0;        // where the last of those is
    float y = 0;
};

/* A slot's colour, opaque. */
std::uint32_t tag_colour(int slot);

/* Draws `tags` as seen from `camera`. */
TagStats draw_name_tags(TagPainter& painter, const float camera[3], const std::vector<NameTag>& tags);

}  // namespace mp

#endif
