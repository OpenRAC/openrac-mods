// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors

#include "ice_fx.h"

#include <algorithm>
#include <cmath>

#include "perk_rules.h"
#include "tiers.h"

namespace ng_plus_plus {

namespace {

using Vec = std::array<float, 3>;

constexpr const char* kMeshPaths[4] = {
    "assets/ice_sprite.glb", "assets/ice_streak.glb", "assets/ice_block.glb", "assets/ice_shard.glb"
};

// The stream, in sixtieths of a second (the game's ticks scale with its disc, m_view.ticks).
constexpr int kPointGap100 = 90;     // A new point once the meteor moved this far (hundredths).
constexpr int kMostPoints = 13;      // The ribbon runs through this many points at most.
constexpr int kStreamLife = 26;      // A point of the ribbon lasts this long.
constexpr int kStreamFade = 12;      // A stream whose meteor ended fades this long.
constexpr float kRibbonWidth = 0.75f;  // The ribbon's width at its head, in the game's units.
constexpr float kHeadSize = 1.7f;
constexpr float kGlowSize = 3.6f;

// The block, in sixtieths.
constexpr int kBlockFadeIn = 30;     // RAC3's ice fades in over 30 frames.
constexpr float kBlockAlpha = 0.52f;  // ...to about half (RAC3's 60 of 128).
constexpr int kPuffTicks = 8;        // Cold puffs rise from a fresh block this long.

// The shatter and the splash.
constexpr int kShards = 16;
constexpr int kKillShards = 26;
constexpr int kSplashPuffs = 9;
constexpr int kBitsMost = 120;

constexpr float kPi = 3.14159265f;

Vec sub(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float length(const Vec& a) {
    return std::sqrt(dot(a, a));
}

Vec scaled(const Vec& a, float s) {
    return {a[0] * s, a[1] * s, a[2] * s};
}

Vec unit(const Vec& a) {
    const float l = length(a);
    return l > 1e-6f ? scaled(a, 1.0f / l) : Vec{0.0f, 0.0f, 1.0f};
}

// Fills a column-major matrix from three axes and a place.
void matrix(float m[16], const Vec& x, const Vec& y, const Vec& z, const Vec& at) {
    const Vec* columns[3] = {&x, &y, &z};

    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            m[c * 4 + r] = (*columns[c])[static_cast<std::size_t>(r)];
        }

        m[c * 4 + 3] = 0.0f;
    }

    m[12] = at[0];
    m[13] = at[1];
    m[14] = at[2];
    m[15] = 1.0f;
}

constexpr std::uint32_t kAlpha = 1u;  // OPENRAC_PROP_ALPHA
constexpr std::uint32_t kAdd = 2u;    // OPENRAC_PROP_ADD

}  // namespace

float IceFx::random() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return static_cast<float>(m_seed & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

std::array<float, 3> IceFx::tier_rgb(int tier) const {
    const std::optional<std::uint32_t> rgb = ice_shot_rgb(tier, m_view.sixtieths(m_tick));
    const std::uint32_t value = rgb.value_or(0x48C8FFu);
    return {
        static_cast<float>((value >> 16) & 0xFFu) / 255.0f,
        static_cast<float>((value >> 8) & 0xFFu) / 255.0f,
        static_cast<float>(value & 0xFFu) / 255.0f
    };
}

IceFx::Handle IceFx::take(IceMesh mesh) {
    const auto m = static_cast<std::size_t>(mesh);
    PropPool& pool = m_pools[m];

    for (std::size_t i = 0; i < pool.props.size(); ++i) {
        if (!pool.props[i].used) {
            pool.props[i].used = true;
            return Handle{static_cast<int>(m), static_cast<int>(i)};
        }
    }

    // A new prop, within this mesh's share.
    if (static_cast<int>(pool.props.size()) >= kIceMeshCaps[m]) {
        return Handle{};
    }

    const int id = m_mod.prop_create(kMeshPaths[m]);

    if (id < 0) {
        if (!m_refused) {
            m_refused = true;
            m_mod.info(std::string("the Ice Gun's look: no prop for ") + kMeshPaths[m] + " (none left, or the file is missing)");
        }

        return Handle{};
    }

    pool.props.push_back(Prop{id, true});
    return Handle{static_cast<int>(m), static_cast<int>(pool.props.size()) - 1};
}

void IceFx::give(Handle& handle) {
    if (handle.mesh < 0) {
        return;
    }

    Prop& prop = m_pools[static_cast<std::size_t>(handle.mesh)].props[static_cast<std::size_t>(handle.index)];
    m_mod.prop_hide(prop.id, true);
    prop.used = false;
    handle = Handle{};
}

bool IceFx::pose(const Handle& handle, const float m[16], const float tint[4], std::uint32_t blend) {
    if (handle.mesh < 0) {
        return false;
    }

    const Prop& prop = m_pools[static_cast<std::size_t>(handle.mesh)].props[static_cast<std::size_t>(handle.index)];
    const bool ok = m_mod.prop_pose(prop.id, m, tint, blend);
    m_mod.prop_hide(prop.id, !ok);
    return ok;
}

int IceFx::props_in_use() const {
    int n = 0;

    for (const PropPool& pool : m_pools) {
        for (const Prop& prop : pool.props) {
            n += prop.used ? 1 : 0;
        }
    }

    return n;
}

void IceFx::reset() {
    m_streams.clear();
    m_blocks.clear();
    m_pools_cold.clear();
    m_bits.clear();

    for (PropPool& pool : m_pools) {
        pool.props.clear();
    }

    m_refused = false;
}

void IceFx::begin(std::uint32_t tick) {
    m_tick = tick;
    const std::uint32_t loads = m_mod.play_status().loads;

    // Another level: its props are gone.
    if (!m_loaded || loads != m_loads) {
        m_loaded = true;
        m_loads = loads;
        reset();
    }

    openrac_camera cam{};
    m_camera.have = m_mod.camera_view(cam);

    if (m_camera.have) {
        for (std::size_t i = 0; i < 3; ++i) {
            m_camera.eye[i] = cam.eye[i];
            m_camera.right[i] = -cam.left[i];
            m_camera.up[i] = cam.up[i];
            m_camera.forward[i] = cam.forward[i];
        }
    }
}

void IceFx::tick(std::uint32_t tick) {
    // A port without props draws none of this (nor a table whose calls are not all there).
    const openrac_api& api = m_mod.api();

    if (!m_mod.has_props() || api.prop_create == nullptr || api.prop_pose == nullptr
        || api.prop_hide == nullptr || api.camera_view == nullptr || api.play_status == nullptr
        || api.moby_hide == nullptr) {
        return;
    }

    begin(tick);
    meteors();

    for (auto it = m_streams.begin(); it != m_streams.end();) {
        stream_tick(it->second);

        if (it->second.ended && tick > it->second.ended_at + static_cast<std::uint32_t>(m_view.ticks(kStreamFade))) {
            stream_free(it->second);
            it = m_streams.erase(it);
            continue;
        }

        stream_draw(it->second);
        ++it;
    }

    pools_tick();
    blocks_tick();
    bits_tick();
}

void IceFx::meteors() {
    const WeaponView weapon = m_weapons.weapon(kLavaGun);
    const bool on = ice_gun_on() && weapon.has_tier();
    const int tier = on ? tier_of(weapon.kind, weapon.level) : 0;

    for (const ScannedMoby& moby : m_scan.mobys()) {
        // A lingering blob: no lava, a cold pool in its place.
        if (on && moby.moby_class == kBlobClass) {
            auto pool = m_pools_cold.find(moby.address);

            if (pool == m_pools_cold.end() && m_pools_cold.size() < 10) {
                Pool fresh;
                fresh.born = m_tick;
                fresh.tier = std::max(tier, 1);
                pool = m_pools_cold.emplace(moby.address, fresh).first;
            }

            if (pool != m_pools_cold.end()) {
                m_mod.moby_hide(moby.slot, true);
                pool->second.seen = m_tick;
                pool->second.at = moby.position;
            }

            continue;
        }

        if (!on || moby.moby_class != kMeteorClass) {
            continue;
        }

        auto found = m_streams.find(moby.address);

        // A new meteor: its model is not drawn, its stream begins.
        if (found == m_streams.end()) {
            if (m_streams.size() >= 8) {
                continue;
            }

            Stream stream;
            stream.moby = moby.address;
            stream.tier = std::max(tier, 1);
            found = m_streams.emplace(moby.address, std::move(stream)).first;
        }

        Stream& stream = found->second;

        // Its model is not drawn: hidden again each tick, as the game may show it.
        const bool hidden = m_mod.moby_hide(moby.slot, true);

        if (!m_logged_hide) {
            m_logged_hide = true;
            m_mod.info(std::string("the Ice Gun's meteor hidden: ") + (hidden ? "yes" : "no"));
        }

        stream.seen = m_tick;
        stream.ended = false;
        const float gap = static_cast<float>(kPointGap100) / 100.0f;

        // A point once it moved far enough, the last one following it.
        if (stream.points.empty() || length(sub(moby.position, stream.points.back())) >= gap) {
            stream.points.push_back(moby.position);
            stream.born.push_back(m_tick);
        } else {
            stream.points.back() = moby.position;
        }
    }
}

void IceFx::stream_tick(Stream& stream) {
    // The meteor flew in the last tick, or it ended here: a splash where it was.
    if (!stream.ended && stream.seen != m_tick) {
        stream.ended = true;
        stream.ended_at = m_tick;

        if (!stream.points.empty()) {
            splash(stream.points.back(), stream.tier);
        }
    }

    // Points that outlived their time, and the oldest past the most.
    const std::uint32_t life = static_cast<std::uint32_t>(m_view.ticks(kStreamLife));

    while (!stream.points.empty()
           && (m_tick - stream.born.front() > life || stream.points.size() > static_cast<std::size_t>(kMostPoints))) {
        stream.points.erase(stream.points.begin());
        stream.born.erase(stream.born.begin());
    }
}

void IceFx::stream_free(Stream& stream) {
    for (Handle& handle : stream.segments) {
        give(handle);
    }

    stream.segments.clear();
    give(stream.head);
    give(stream.glow);
}

void IceFx::stream_draw(Stream& stream) {
    if (!m_camera.have) {
        return;
    }

    const std::array<float, 3> rgb = tier_rgb(stream.tier);
    const std::size_t pairs = stream.points.size() > 1 ? stream.points.size() - 1 : 0;
    const float life = static_cast<float>(m_view.ticks(kStreamLife));
    const float fade_ticks = static_cast<float>(m_view.ticks(kStreamFade));
    const float end_fade =
        stream.ended ? std::max(0.0f, 1.0f - static_cast<float>(m_tick - stream.ended_at) / fade_ticks) : 1.0f;

    // The ribbon: a streak from each point to the next, thinner and dimmer the older it is.
    while (stream.segments.size() < pairs) {
        Handle handle = take(IceMesh::Streak);

        if (handle.mesh < 0) {
            break;
        }

        stream.segments.push_back(handle);
    }

    while (stream.segments.size() > pairs) {
        give(stream.segments.back());
        stream.segments.pop_back();
    }

    for (std::size_t i = 0; i < stream.segments.size() && i < pairs; ++i) {
        const Vec a = stream.points[i];
        const Vec b = stream.points[i + 1];
        const float age = static_cast<float>(m_tick - stream.born[i + 1]) / life;
        const float strength = std::clamp(1.0f - age, 0.0f, 1.0f) * end_fade;
        const Vec d = sub(b, a);
        const Vec to_eye = sub(m_camera.eye, scaled({a[0] + b[0], a[1] + b[1], a[2] + b[2]}, 0.5f));
        Vec side = cross(d, to_eye);
        side = scaled(unit(side), kRibbonWidth * (0.35f + 0.65f * strength));
        float m[16];
        matrix(m, d, side, unit(to_eye), a);
        const float tint[4] = {
            0.2f + 0.5f * rgb[0], 0.42f + 0.45f * rgb[1], 1.0f, 0.8f * strength
        };
        pose(stream.segments[i], m, tint, kAdd);
    }

    // The head: a bright core and a wide glow on the meteor, while it flies.
    if (!stream.ended && !stream.points.empty()) {
        if (stream.head.mesh < 0) {
            stream.head = take(IceMesh::Sprite);
        }

        if (stream.glow.mesh < 0) {
            stream.glow = take(IceMesh::Sprite);
        }

        sprite(stream.head, stream.points.back(), kHeadSize, {0.85f, 0.95f, 1.0f}, 1.0f, true);
        sprite(stream.glow, stream.points.back(), kGlowSize, rgb, 0.55f, true);
    } else {
        give(stream.head);
        give(stream.glow);
    }
}

void IceFx::sprite(Handle& handle, const Vec& at, float size, const Vec& rgb, float alpha, bool hot) {
    if (handle.mesh < 0 || !m_camera.have) {
        return;
    }

    float m[16];
    matrix(m, scaled(m_camera.right, size), scaled(m_camera.up, size), scaled(m_camera.forward, -size), at);
    const float tint[4] = {rgb[0], rgb[1], rgb[2], alpha};
    pose(handle, m, tint, hot ? kAdd : kAlpha);
}

void IceFx::splash(const Vec& at, int tier) {
    const Vec rgb = tier_rgb(tier);

    // The cold pool: a glow on the ground in place of the lava pool, lasting a second.
    if (m_bits.size() < static_cast<std::size_t>(kBitsMost)) {
        Bit pool;
        pool.handle = take(IceMesh::Sprite);
        pool.at = {at[0], at[1], at[2] + 0.12f};
        pool.size0 = 2.6f;
        pool.size1 = 4.8f;
        pool.alpha = 0.7f;
        pool.born = m_tick;
        pool.life = m_view.ticks(30);
        pool.rgb = {0.45f + 0.55f * rgb[0], 0.6f + 0.4f * rgb[1], 1.0f};
        pool.flat = true;
        pool.hot = true;

        if (pool.handle.mesh >= 0) {
            m_bits.push_back(pool);
        }
    }

    // The cold splash: puffs thrown up and out.
    for (int i = 0; i < kSplashPuffs && m_bits.size() < static_cast<std::size_t>(kBitsMost); ++i) {
        Bit puff;
        puff.handle = take(IceMesh::Sprite);

        if (puff.handle.mesh < 0) {
            break;
        }

        const float a = random() * 2.0f * kPi;
        const float out = m_view.per_tick(0.9f + 1.6f * random()) * 0.1f;
        puff.at = {at[0], at[1], at[2] + 0.3f};
        puff.velocity = {std::cos(a) * out, std::sin(a) * out, m_view.per_tick(0.9f + 1.3f * random()) * 0.1f};
        puff.size0 = 0.9f + 0.6f * random();
        puff.size1 = 2.6f + 1.4f * random();
        puff.alpha = 0.6f;
        puff.drag = 0.94f;
        puff.born = m_tick;
        puff.life = m_view.ticks(26 + static_cast<int>(random() * 14.0f));
        puff.rgb = {0.6f + 0.4f * rgb[0], 0.8f + 0.2f * rgb[1], 1.0f};
        puff.hot = true;
        m_bits.push_back(puff);
    }
}

void IceFx::freeze(std::uint32_t moby, int moby_class, int tier, int ticks) {
    // Nothing to draw it with: tick() found the port without props (m_loaded is set then).
    if (!m_loaded || !m_mod.has_props() || ticks <= 0) {
        return;
    }

    const std::uint32_t until = m_tick + static_cast<std::uint32_t>(ticks);
    auto found = m_blocks.find(moby);

    // A block it has: kept longer.
    if (found != m_blocks.end()) {
        found->second.until = std::max(found->second.until, until);
        found->second.tier = tier;
        return;
    }

    if (m_blocks.size() >= static_cast<std::size_t>(kIceMeshCaps[static_cast<std::size_t>(IceMesh::Block)])) {
        return;
    }

    Block block;
    block.handle = take(IceMesh::Block);

    if (block.handle.mesh < 0) {
        return;
    }

    const float aim = m_scan.aim_height(moby);
    block.moby_class = moby_class;
    block.born = m_tick;
    block.until = until;
    block.tier = tier;
    block.yaw = random() * 2.0f * kPi;
    block.height = std::clamp(aim > 0.0f ? aim * 2.2f : 2.4f, 1.4f, 9.0f);
    block.width = std::clamp(block.height * 0.85f, 1.3f, 8.0f);
    block.at = m_scan.position(moby);
    block.puffs_until = m_tick + static_cast<std::uint32_t>(m_view.ticks(kPuffTicks));
    puffs(block, 6);
    m_blocks.emplace(moby, block);
}

void IceFx::killed(std::uint32_t moby) {
    const auto found = m_blocks.find(moby);

    if (found == m_blocks.end()) {
        return;
    }

    burst(found->second, kKillShards);
    give(found->second.handle);
    m_blocks.erase(found);
}

void IceFx::pools_tick() {
    for (auto it = m_pools_cold.begin(); it != m_pools_cold.end();) {
        Pool& pool = it->second;

        // The blob is gone (its fire went out): the pool fades with it.
        if (pool.seen != m_tick) {
            give(pool.handle);
            it = m_pools_cold.erase(it);
            continue;
        }

        if (pool.handle.mesh < 0) {
            pool.handle = take(IceMesh::Sprite);
        }

        if (pool.handle.mesh >= 0 && m_camera.have) {
            const Vec rgb = tier_rgb(pool.tier);
            const float age = static_cast<float>(m_tick - pool.born);
            const float in = std::min(1.0f, age / static_cast<float>(m_view.ticks(12)));
            const float pulse = 0.85f + 0.15f * std::sin(age * 0.2f);
            const float size = 5.0f * (0.6f + 0.4f * in);
            float m[16];
            matrix(m, {size, 0.0f, 0.0f}, {0.0f, size, 0.0f}, {0.0f, 0.0f, 1.0f}, {pool.at[0], pool.at[1], pool.at[2] + 0.15f});
            const float tint[4] = {0.3f + 0.4f * rgb[0], 0.5f + 0.4f * rgb[1], 1.0f, 0.5f * in * pulse};
            pose(pool.handle, m, tint, kAdd);
        }

        ++it;
    }
}

void IceFx::blocks_tick() {
    for (auto it = m_blocks.begin(); it != m_blocks.end();) {
        Block& block = it->second;

        // Gone, or its slot holds another moby: the block goes with a shatter where it stood.
        if (!m_scan.alive(it->first, block.moby_class)) {
            burst(block, kShards);
            give(block.handle);
            it = m_blocks.erase(it);
            continue;
        }

        block.at = m_scan.position(it->first);

        // The freeze ended: the block shatters.
        if (m_tick >= block.until) {
            burst(block, kShards);
            give(block.handle);
            it = m_blocks.erase(it);
            continue;
        }

        // The puffs of a fresh freeze.
        if (m_tick < block.puffs_until) {
            puffs(block, 5);
        }

        block_draw(block);
        ++it;
    }
}

void IceFx::block_draw(Block& block) {
    const Vec rgb = tier_rgb(block.tier);
    const float in = std::min(1.0f, static_cast<float>(m_tick - block.born) / static_cast<float>(m_view.ticks(kBlockFadeIn)));
    // A faint glint that moves over it.
    const float glint = 0.9f + 0.1f * std::sin(static_cast<float>(m_tick) * 0.18f);
    const float c = std::cos(block.yaw);
    const float s = std::sin(block.yaw);
    float m[16];
    matrix(
        m,
        {c * block.width, s * block.width, 0.0f},
        {-s * block.width, c * block.width, 0.0f},
        {0.0f, 0.0f, block.height / 1.24f},
        {block.at[0], block.at[1], block.at[2] + block.height * 0.5f}
    );
    const float tint[4] = {0.72f + 0.28f * rgb[0] * glint, 0.88f + 0.12f * rgb[1], 1.0f, kBlockAlpha * in};
    pose(block.handle, m, tint, kAlpha);
}

void IceFx::puffs(const Block& block, int count) {
    for (int i = 0; i < count && m_bits.size() < static_cast<std::size_t>(kBitsMost); ++i) {
        Bit puff;
        puff.handle = take(IceMesh::Sprite);

        if (puff.handle.mesh < 0) {
            return;
        }

        const float a = random() * 2.0f * kPi;
        const float r = 0.5f * block.width * std::sqrt(random());
        puff.at = {block.at[0] + std::cos(a) * r, block.at[1] + std::sin(a) * r, block.at[2] + block.height * random()};
        puff.velocity = {std::cos(a) * m_view.per_tick(0.12f), std::sin(a) * m_view.per_tick(0.12f), m_view.per_tick(0.22f + 0.2f * random())};
        puff.size0 = block.height * (0.18f + 0.1f * random());
        puff.size1 = puff.size0 * 2.2f;
        puff.alpha = 0.42f;
        puff.drag = 0.97f;
        puff.born = m_tick;
        puff.life = m_view.ticks(34 + static_cast<int>(random() * 16.0f));
        puff.rgb = {0.75f, 0.9f, 1.0f};
        puff.hot = true;
        m_bits.push_back(puff);
    }
}

void IceFx::burst(const Block& block, int pieces) {
    const Vec centre{block.at[0], block.at[1], block.at[2] + block.height * 0.5f};

    for (int i = 0; i < pieces && m_bits.size() < static_cast<std::size_t>(kBitsMost); ++i) {
        Bit shard;
        shard.handle = take(IceMesh::Shard);

        if (shard.handle.mesh < 0) {
            break;
        }

        const float a = random() * 2.0f * kPi;
        const float out = m_view.per_tick(0.8f + 2.4f * random()) * 0.1f * std::max(1.0f, block.height / 2.4f);
        shard.at = {
            centre[0] + std::cos(a) * block.width * 0.3f * random(),
            centre[1] + std::sin(a) * block.width * 0.3f * random(),
            centre[2] + (random() - 0.5f) * block.height * 0.8f
        };
        shard.velocity = {std::cos(a) * out, std::sin(a) * out, m_view.per_tick(1.6f + 2.4f * random()) * 0.1f};
        shard.axis = unit({random() - 0.5f, random() - 0.5f, random() - 0.5f});
        shard.size0 = block.height * (0.22f + 0.2f * random());
        shard.size1 = shard.size0 * 0.8f;
        shard.alpha = 0.85f;
        shard.gravity = m_view.per_tick(m_view.per_tick(0.08f));
        shard.drag = 0.985f;
        shard.born = m_tick;
        shard.life = m_view.ticks(46 + static_cast<int>(random() * 18.0f));
        shard.shard = true;
        shard.spin = (random() - 0.5f) * 0.5f;
        shard.angle = random() * 2.0f * kPi;
        shard.rgb = {0.8f, 0.93f, 1.0f};
        m_bits.push_back(shard);
    }

    // A flash of cold where it broke.
    Bit flash;
    flash.handle = take(IceMesh::Sprite);

    if (flash.handle.mesh >= 0) {
        flash.at = centre;
        flash.size0 = block.height * 0.9f;
        flash.size1 = block.height * 2.4f;
        flash.alpha = 0.7f;
        flash.born = m_tick;
        flash.life = m_view.ticks(18);
        flash.rgb = {0.8f, 0.94f, 1.0f};
        flash.hot = true;
        m_bits.push_back(flash);
    }
}

void IceFx::bits_tick() {
    for (std::size_t i = 0; i < m_bits.size();) {
        Bit& bit = m_bits[i];
        const int age = static_cast<int>(m_tick - bit.born);

        if (age >= bit.life) {
            give(bit.handle);
            m_bits[i] = m_bits.back();
            m_bits.pop_back();
            continue;
        }

        for (std::size_t k = 0; k < 3; ++k) {
            bit.at[k] += bit.velocity[k];
            bit.velocity[k] *= bit.drag;
        }

        bit.velocity[2] -= bit.gravity;
        bit.angle += bit.spin;
        bit_draw(bit);
        ++i;
    }
}

void IceFx::bit_draw(Bit& bit) {
    if (!m_camera.have) {
        return;
    }

    const float t = static_cast<float>(m_tick - bit.born) / static_cast<float>(bit.life);
    const float size = bit.size0 + (bit.size1 - bit.size0) * t;
    // Quick in, slow out.
    const float fade = std::clamp(std::min(t * 6.0f, 1.0f) * (1.0f - t), 0.0f, 1.0f);
    const float tint[4] = {bit.rgb[0], bit.rgb[1], bit.rgb[2], bit.alpha * fade};
    float m[16];

    if (bit.shard) {
        // Long side along its axis turned by its angle about the world's up, a shard tumbling.
        const float c = std::cos(bit.angle);
        const float s = std::sin(bit.angle);
        const Vec x = unit({bit.axis[0] * c - bit.axis[1] * s, bit.axis[0] * s + bit.axis[1] * c, bit.axis[2]});
        const Vec y = unit(cross(x, {0.0f, 0.0f, 1.0f}));
        const Vec z = cross(x, y);
        matrix(m, scaled(x, size), scaled(y, size * 0.8f), scaled(z, size * 0.8f), bit.at);
        pose(bit.handle, m, tint, kAlpha);
        return;
    }

    if (bit.flat) {
        matrix(m, {size, 0.0f, 0.0f}, {0.0f, size, 0.0f}, {0.0f, 0.0f, 1.0f}, bit.at);
        pose(bit.handle, m, tint, bit.hot ? kAdd : kAlpha);
        return;
    }

    matrix(m, scaled(m_camera.right, size), scaled(m_camera.up, size), scaled(m_camera.forward, -size), bit.at);
    pose(bit.handle, m, tint, bit.hot ? kAdd : kAlpha);
}

}  // namespace ng_plus_plus
