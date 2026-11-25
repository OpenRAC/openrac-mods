// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Ice Gun's meshes (assets/, made by tools/make_ice_meshes.py): each is there, a binary glTF 2
// whose length matches its header, naming a picture that is there too; and the pool sizes leave
// the port's 256 props room for the other mods.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"
#include "ice_fx.h"

#ifndef NGPP_ASSETS_DIR
#error "NGPP_ASSETS_DIR must name the assets folder"
#endif

namespace {

std::vector<char> read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    const std::string dir = NGPP_ASSETS_DIR;
    const char* const meshes[] = {"ice_sprite", "ice_streak", "ice_block", "ice_shard"};
    const char* const pictures[] = {"ice_glow.png", "ice_streak.png", "ice_facets.png", "ice_facets.png"};

    for (int i = 0; i < 4; ++i) {
        const std::vector<char> glb = read(dir + "/" + meshes[i] + ".glb");
        CHECK(glb.size() > 20);

        if (glb.size() <= 20) {
            continue;
        }

        std::uint32_t version = 0;
        std::uint32_t total = 0;
        std::memcpy(&version, glb.data() + 4, 4);
        std::memcpy(&total, glb.data() + 8, 4);
        CHECK(std::memcmp(glb.data(), "glTF", 4) == 0);
        CHECK(version == 2);
        CHECK(total == glb.size());

        // The picture it names is beside it.
        const std::string text(glb.begin(), glb.end());
        CHECK(text.find(pictures[i]) != std::string::npos);
        CHECK(!read(dir + "/" + pictures[i]).empty());
    }

    // The pools together take well under the port's 256 props.
    int total = 0;

    for (const int cap : ng_plus_plus::kIceMeshCaps) {
        total += cap;
    }

    CHECK(total <= 160);
    return ng_plus_plus::test::result("ng_plus_plus_ice_assets");
}
