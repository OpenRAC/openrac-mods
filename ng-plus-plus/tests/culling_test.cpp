// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// NG++ 0.5.1's off-screen picks (src/culling.cpp): weapon_ignore_culling is found right after
// 1.29's seven weapon calls on a port of 1.30, and not on an older port or a table that ends first.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "check.h"
#include "culling.h"
#include "weapon_api.h"

namespace {

using namespace ng_plus_plus;

int host_culling(openrac_mod* /*mod*/, std::int32_t item, std::int32_t /*enabled*/) {
    return item == 43 || item == 28 ? OPENRAC_HOOK_OK : OPENRAC_HOOK_UNKNOWN;
}

// A port's table of `minor` that ends `size` bytes in, the call at its place.
std::vector<unsigned char> table(std::uint16_t minor, std::size_t size) {
    const std::size_t at = weapon_calls_offset() + sizeof(WeaponCallsTable);
    std::vector<unsigned char> bytes(std::max(size, at + sizeof(void*)) + sizeof(openrac_api), 0);
    openrac_api api{};
    api.api_major = OPENRAC_MOD_API_MAJOR;
    api.api_minor = minor;
    api.size = static_cast<std::uint32_t>(size);
    std::memcpy(bytes.data(), &api, sizeof api);
    const IgnoreCullingFn fn = host_culling;
    std::memcpy(bytes.data() + at, &fn, sizeof fn);
    return bytes;
}

void test_found() {
    const std::size_t end = weapon_calls_offset() + sizeof(WeaponCallsTable) + sizeof(void*);

    // A port of 1.30: the call.
    std::vector<unsigned char> bytes = table(kCullingMinor, end);
    const auto& api = *reinterpret_cast<const openrac_api*>(bytes.data());
    const IgnoreCullingFn fn = ignore_culling_of(api);
    CHECK(fn == host_culling);
    CHECK(fn != nullptr && fn(nullptr, 43, 1) == OPENRAC_HOOK_OK);

    // A port of 1.29, or a table that ends before the call: none.
    bytes = table(29, end);
    CHECK(ignore_culling_of(*reinterpret_cast<const openrac_api*>(bytes.data())) == nullptr);
    bytes = table(kCullingMinor, end - sizeof(void*));
    CHECK(ignore_culling_of(*reinterpret_cast<const openrac_api*>(bytes.data())) == nullptr);
}

}  // namespace

int main() {
    test_found();
    return ng_plus_plus::test::result("ng_plus_plus_culling");
}
