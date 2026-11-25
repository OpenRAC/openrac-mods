// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Ice Gun's look: Up Your Arsenal's Liquid Nitrogen Gun (the Lava Gun's V5) drawn natively in
// Going Commando. docs/rac3-liquid-nitrogen-gun.md says what Up Your Arsenal does; this is what
// NG++ makes of it with the mod SDK's props (a mesh of the mod's, drawn in the level):
//
//   * The Lava Gun's meteors are not drawn. Each flies as a stream of liquid nitrogen instead: a
//     ribbon through its last points (a white core in a bright band in a wide glow, drawn added
//     to the picture, as the ribbon of the Lava Gun's stream is), a glow at its head, and cold
//     puffs where it lands with a cold glow on the ground in place of the lava pool.
//   * An enemy the gun freezes is set in a block of ice, which fades in over half a second and,
//     when the freeze ends or the enemy dies in it, shatters into shards. Cold puffs rise from it
//     as the freeze begins.
//
// The game's meteor is still what hits (its damage, its blast, its physics: moby_status freezes);
// only how it is drawn changes. Everything drawn is made by tools/make_ice_meshes.py, nothing is
// taken from a game's files. A port without props (older than 1.12) draws nothing of this and the
// gun is the paint's (perks.h).
//
// Only the game's thread uses it.

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game_view.h"
#include "moby_scan.h"
#include "openrac/mod.hpp"
#include "weapons.h"

namespace ng_plus_plus {

// The Meteor Gun's meteor, the shot of the Lava Gun's tiers (weapon_visuals.h).
inline constexpr int kMeteorClass = 2862;

// The Lava Gun's lingering blobs (the burning puddles a meteor leaves, lava_gun_update_blobs).
inline constexpr int kBlobClass = 4406;

// The meshes of the Ice Gun's look (assets/, in the mod's folder).
enum class IceMesh : int { Sprite = 0, Streak, Block, Shard, Count };

// How many props each mesh may take at once, of the port's 256 for all mods.
inline constexpr std::array<int, 4> kIceMeshCaps = {56, 44, 8, 40};

// The Ice Gun's drawing.
class IceFx {
public:
    IceFx(openrac::Mod& mod, const GameView& view, const Weapons& weapons, MobyScan& scan)
        : m_mod(mod),
          m_view(view),
          m_weapons(weapons),
          m_scan(scan) {}

    // Once a game tick, after the scan of the level's mobys was read: the meteors' streams, the
    // blocks, the puffs and the shards.
    void tick(std::uint32_t tick);

    // The enemy at `moby` (class `moby_class`) was frozen for `ticks` of the game's, by the Ice
    // Gun's tier `tier`: set in ice (a fresh block, or the one it has kept longer).
    void freeze(std::uint32_t moby, int moby_class, int tier, int ticks);

    // The enemy at `moby` died: the block it was in shatters. Does nothing for one with none.
    void killed(std::uint32_t moby);

    // Returns whether the mod draws a block on the enemy at `moby` now.
    bool blocked(std::uint32_t moby) const { return m_blocks.count(moby) != 0; }

    // Returns how many props the effects hold now (for the tests' and the runs' logs).
    int props_in_use() const;

    // Returns how many streams (meteors flying or fading) there are now.
    int streams() const { return static_cast<int>(m_streams.size()); }

    // Forgets everything (a level left: its props went with it).
    void reset();

private:
    // One prop of a pool.
    struct Prop {
        int id = -1;
        bool used = false;
    };

    struct PropPool {
        std::vector<Prop> props;
    };

    // A drawn thing's handle: the mesh and the index in its pool, -1 for none.
    struct Handle {
        int mesh = -1;
        int index = -1;
    };

    // A flying meteor's stream.
    struct Stream {
        std::uint32_t moby = 0;
        std::vector<std::array<float, 3>> points;  // Oldest first.
        std::vector<std::uint32_t> born;            // The tick each point was laid.
        std::uint32_t seen = 0;                     // The last tick the meteor flew.
        bool ended = false;
        std::uint32_t ended_at = 0;
        int tier = 1;
        std::vector<Handle> segments;  // The props of its ribbon, in use this tick.
        Handle head;
        Handle glow;
    };

    // A lingering blob's cold pool: a glow lying on the ground where the lava puddle burns.
    struct Pool {
        Handle handle;
        std::uint32_t seen = 0;
        std::uint32_t born = 0;
        std::array<float, 3> at{};
        int tier = 1;
    };

    // A cold puff, a glow on the ground or a shard: one sprite or shard prop moving and fading.
    struct Bit {
        Handle handle;
        std::array<float, 3> at{};
        std::array<float, 3> velocity{};  // Per tick.
        std::array<float, 3> axis{};      // A shard's long side.
        float size0 = 1.0f;
        float size1 = 1.0f;
        float alpha = 1.0f;
        float gravity = 0.0f;  // Per tick, taken off the velocity's z.
        float drag = 1.0f;     // Per tick, multiplied into the velocity.
        std::uint32_t born = 0;
        int life = 1;
        std::array<float, 3> rgb{0.8f, 0.93f, 1.0f};
        bool flat = false;   // Lying on the ground.
        bool shard = false;  // A shard (else a sprite).
        float spin = 0.0f;   // A shard's turn per tick (radians).
        float angle = 0.0f;
        bool hot = false;    // Added to the picture (else blended).
    };

    // An enemy's block of ice.
    struct Block {
        Handle handle;
        int moby_class = 0;
        std::uint32_t born = 0;
        std::uint32_t until = 0;
        float yaw = 0.0f;
        float height = 2.0f;
        float width = 1.6f;
        int tier = 1;
        std::array<float, 3> at{};  // Where the enemy was last.
        std::uint32_t puffs_until = 0;
    };

    // The camera's vectors, for a sprite's billboard.
    struct View {
        std::array<float, 3> eye{};
        std::array<float, 3> right{};
        std::array<float, 3> up{};
        std::array<float, 3> forward{};
        bool have = false;
    };

    void begin(std::uint32_t tick);
    void meteors();
    void stream_tick(Stream& stream);
    void stream_draw(Stream& stream);
    void stream_free(Stream& stream);
    void splash(const std::array<float, 3>& at, int tier);
    void pools_tick();
    void blocks_tick();
    void block_draw(Block& block);
    void burst(const Block& block, int pieces);
    void puffs(const Block& block, int count);
    void bits_tick();
    void bit_draw(Bit& bit);
    void sprite(Handle& handle, const std::array<float, 3>& at, float size, const std::array<float, 3>& rgb, float alpha, bool hot);

    // The pools: take one, give it back.
    Handle take(IceMesh mesh);
    void give(Handle& handle);
    bool pose(const Handle& handle, const float matrix[16], const float tint[4], std::uint32_t blend);

    // Returns the tier colour of the Ice Gun's streams.
    std::array<float, 3> tier_rgb(int tier) const;
    float random();  // 0 up to 1.

    openrac::Mod& m_mod;
    const GameView& m_view;
    const Weapons& m_weapons;
    MobyScan& m_scan;
    std::uint32_t m_tick = 0;
    std::uint32_t m_loads = 0;
    bool m_loaded = false;
    bool m_logged_hide = false;
    bool m_refused = false;  // A prop was refused (logged once a level).
    std::array<PropPool, 4> m_pools;
    std::unordered_map<std::uint32_t, Stream> m_streams;  // By the meteor's address.
    std::unordered_map<std::uint32_t, Block> m_blocks;    // By the enemy's address.
    std::unordered_map<std::uint32_t, Pool> m_pools_cold;  // By the blob's address.
    std::vector<Bit> m_bits;
    View m_camera;
    std::uint32_t m_seed = 0x9E3779B9u;
};

}  // namespace ng_plus_plus
