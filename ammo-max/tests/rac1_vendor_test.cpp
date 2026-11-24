// SPDX-License-Identifier: MIT
#include "rac1_vendor.hpp"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

struct Memory {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(8192);
    template<class T> T read(std::uint32_t at) const {
        if (at + sizeof(T) > bytes.size()) std::abort();
        T value; std::memcpy(&value, bytes.data() + at, sizeof value); return value;
    }
    template<class T> void write(std::uint32_t at, T value) {
        if (at + sizeof(T) > bytes.size()) std::abort();
        std::memcpy(bytes.data() + at, &value, sizeof value);
    }
};
void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(EXIT_FAILURE); }
}
int main() {
    constexpr std::uint32_t vendor=256, table=2048, ammo=4096, owned=5120;
    Memory m;
    std::array<buy_all_ammo::Candidate, buy_all_ammo::kItems> out{};
    const auto collect = [&] { return vendor_buy_all::rac1::candidates(m, vendor, table, ammo, owned, out); };
    const auto row = [&](int index, int item, int kind) {
        m.write<std::int32_t>(vendor+0xD0u+index*0x14u, item);
        m.write<std::int32_t>(vendor+0xD4u+index*0x14u, kind);
    };
    m.write<std::int32_t>(vendor, 1);
    m.write<std::int32_t>(vendor+0x210, 7);
    row(0, 2, 1); row(1, 3, 0); row(2, 4, 1); row(3, 2, 1);
    row(4, 37, 1); row(5, -1, 1); row(6, 5, 1);
    m.write<std::uint8_t>(owned+2, 1);
    m.write<std::uint8_t>(owned+3, 1);
    m.write<std::uint8_t>(owned+5, 1);
    m.write<std::int32_t>(ammo+2*4, 3);
    m.write<std::uint16_t>(table+2*0x18+0xE, 10);
    m.write<std::uint16_t>(table+2*0x18+8, 5);
    m.write<std::uint16_t>(table+2*0x18+0xA, 10);
    // Free ammo (e.g. an unsold RYNO) must never become a purchasable refill.
    m.write<std::uint16_t>(table+5*0x18+0xE, 150);
    int count=collect();
    check(count==2 && out[0].item==2 && out[1].item==5,
          "only valid owned ammo rows, duplicates skipped; item purchases untouched");
    auto quote=buy_all_ammo::make_quote(out,count,100);
    check(quote.affordable.count==1 && quote.affordable.cost==35 &&
          quote.affordable.entries[0].amount==7, "normal Gadgetron pricing, free ammo skipped");
    m.write<std::int32_t>(vendor+0x40, 1);
    count=collect(); quote=buy_all_ammo::make_quote(out,count,55);
    check(quote.full_cost==70 && quote.affordable.cost==50 && quote.affordable.entries[0].amount==5,
          "PDA alternate pricing and bounded partial purchase");
    m.write<std::int32_t>(vendor+0x5C, 2);
    check(collect()==0, "normal purchase dialog is excluded");
    m.write<std::int32_t>(vendor+0x5C, 0);
    m.write<std::int32_t>(vendor, 2);
    check(collect()==0, "opening and closing vendor states are excluded");
    m.write<std::int32_t>(vendor, 1);
    m.write<std::int32_t>(vendor+0x210, 17);
    check(collect()==0, "invalid stock count excluded before reading rows");
    m.write<std::int32_t>(vendor+0x210, 7);
    m.write<std::int32_t>(vendor+0x58, 7);
    check(collect()==0, "invalid cursor excluded");
    std::cout << "RAC1 vendor stock, state, ownership and PDA price tests passed\n";
}
