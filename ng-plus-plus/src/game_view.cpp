// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The game as NG++ sees it (game_view.h): the reads and writes behind its accessors, the game's
// text lookup and the port's text slots.

#include "game_view.h"

#include <algorithm>
#include <cstring>

#include "hooks.h"

namespace ng_plus_plus {
namespace {

// Main RAM's size: game addresses 1 to this hold the game's own memory (documented: the PS2 has
// 32 MB).
constexpr std::uint32_t kMainRamBytes = 0x02000000u;

// The port's own region of game memory, where it keeps its statics and the text slots (measured on
// the port: 0xE0000000, 16 MB; negative as an i32).
constexpr std::uint32_t kPortRegion = 0xE0000000u;
constexpr std::uint32_t kPortRegionEnd = 0xE1000000u;

// The longest game string NG++ reads; the game's own are far shorter.
constexpr std::size_t kTextLimit = 512;

// The size of an item's experience and ammo, one s32 each per item.
constexpr std::uint32_t kWordBytes = 4u;

// Returns whether game address `address` holds `size` bytes of game memory: main RAM or the port's
// own region.
bool in_memory(std::uint32_t address, std::uint32_t size) {
    const bool in_ram = address != 0 && address <= kMainRamBytes - size;
    const bool in_port = address >= kPortRegion && address <= kPortRegionEnd - size;
    return in_ram || in_port;
}

// Returns `value` as eight upper-case hex digits, for the log.
std::string hex32(std::uint32_t value) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out(8, '0');

    // From the last digit to the first, four bits each.
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[out.size() - 1 - n] = kDigits[(value >> (4 * n)) & 15u];
    }

    return out;
}

}  // namespace

std::int32_t Row::text_id(const RowTextField& field) const {
    // The field's width decides how much of the row it takes.
    if (field.width == TextWidth::S16) {
        return m_memory.read<std::int16_t>(m_address + field.offset);
    }

    return m_memory.read<std::int32_t>(m_address + field.offset);
}

bool Row::has_text(std::int32_t id) const {
    // Id 0 is the game's "no text": a row's empty field never claims it.
    if (id == 0) {
        return false;
    }

    // Each field the game looks up as a text.
    for (const RowTextField& field : kRowTextFields) {
        // Found in this field.
        if (text_id(field) == id) {
            return true;
        }
    }

    return false;
}

GameView::GameView(const openrac::Mod& mod, const Disc& disc)
    : m_mod(mod),
      m_disc(disc),
      m_text(mod.function<TextFn>(disc.text)),
      m_banner(mod.function<BannerFn>(disc.banner)),
      m_message(mod.function<MessageFn>(disc.message)),
      m_upgrade_fx(mod.function<UpgradeFxFn>(disc.upgrade_fx)) {}

openrac::gaddr GameView::at(std::uint32_t address) const {
    return m_mod.relocate(address);
}

Row GameView::row(int index) const {
    const auto offset = static_cast<std::uint32_t>(index) * kRowBytes;
    return Row(memory(), at(m_disc.weapon_table) + offset, index);
}

int GameView::slot(int item) const {
    return memory().read<std::uint8_t>(at(m_disc.slot) + static_cast<std::uint32_t>(item));
}

bool GameView::owned(int item) const {
    return memory().read<std::uint8_t>(at(m_disc.owned) + static_cast<std::uint32_t>(item)) != 0;
}

std::int32_t GameView::raw_experience(int item) const {
    return memory().read<std::int32_t>(
        at(m_disc.exp) + kWordBytes * static_cast<std::uint32_t>(item)
    );
}

void GameView::clear_experience(int item) const {
    memory().write(at(m_disc.exp) + kWordBytes * static_cast<std::uint32_t>(item), std::int32_t{0});
}

void GameView::set_ammo(int item, std::int32_t rounds) const {
    memory().write(at(m_disc.ammo) + kWordBytes * static_cast<std::uint32_t>(item), rounds);
}

int GameView::held() const {
    return memory().read<std::int32_t>(at(m_disc.hero) + kHeroHeld);
}

bool GameView::hero_ready() const {
    return memory().read<std::int32_t>(at(m_disc.hero) + kHeroReady) != 0;
}

std::uint32_t GameView::hero_moby() const {
    return memory().read<std::uint32_t>(at(m_disc.hero) + kHeroMoby);
}

std::optional<int> GameView::moby_class(std::uint32_t moby) const {
    // No moby, or an address the game's memory does not hold.
    if (!in_memory(moby, kMobyClass + sizeof(std::uint16_t))) {
        return std::nullopt;
    }

    return memory().read<std::uint16_t>(moby + kMobyClass);
}

Clock GameView::clock() const {
    return memory().read<Clock>(at(m_disc.clock));
}

SavePrint GameView::print() const {
    SavePrint print;
    print.bolts = memory().read<std::int32_t>(at(m_disc.progress));

    // All below the programs: the same words at the title and in every level.
    for (int item = 0; item < kItems; ++item) {
        const auto n = static_cast<std::size_t>(item);
        print.exp[n] = raw_experience(item);
        print.row[n] = static_cast<std::uint8_t>(slot(item));
    }

    return print;
}

int GameView::cycle() const {
    return memory().read<std::int16_t>(at(m_disc.progress) + kProgressCycle);
}

std::int32_t GameView::bolts() const {
    return memory().read<std::int32_t>(at(m_disc.progress) + kProgressBolts);
}

std::int32_t GameView::crystals() const {
    return memory().read<std::int16_t>(at(m_disc.progress) + kProgressCrystals);
}

std::int32_t GameView::moonstones() const {
    return memory().read<std::int32_t>(at(m_disc.progress) + kProgressMoonstones);
}

int GameView::streak() const {
    return memory().read<std::uint8_t>(at(m_disc.progress) + kProgressStreak);
}

void GameView::add_bolt_credit(std::int32_t bolts) const {
    const openrac::gaddr credit = at(m_disc.progress) + kProgressBoltCredit;
    memory().write(credit, memory().read<std::int32_t>(credit) + bolts);
}

std::optional<std::int32_t> GameView::enemy_bolts(std::uint32_t moby) const {
    // No moby, or one the game's memory does not hold.
    if (!in_memory(moby, kMobyBolts + sizeof(std::int16_t))) {
        return std::nullopt;
    }

    const auto info = memory().read<std::uint32_t>(moby + kMobyClassInfo);

    // Only an enemy's class record says kind 5; a moby without one is no enemy.
    if (!in_memory(info, kClassKind + sizeof(std::int16_t))
        || memory().read<std::int16_t>(info + kClassKind) != kKindEnemy) {
        return std::nullopt;
    }

    return memory().read<std::int16_t>(moby + kMobyBolts);
}

int GameView::vendor_cursor() const {
    return memory().read<std::int32_t>(at(m_disc.vendor) + kVendorCursor);
}

int GameView::vendor_count() const {
    return memory().read<std::int32_t>(at(m_disc.vendor) + kVendorCount);
}

void GameView::set_vendor_count(int count) const {
    memory().write(at(m_disc.vendor) + kVendorCount, static_cast<std::int32_t>(count));
}

openrac::gaddr GameView::entry_address(int place) const {
    const auto offset = static_cast<std::uint32_t>(place) * kVendorEntryBytes;
    return at(m_disc.vendor) + kVendorEntries + offset;
}

VendorEntry GameView::vendor_entry(int place) const {
    const openrac::gaddr entry = entry_address(place);
    VendorEntry out;
    out.item = memory().read<std::int32_t>(entry + kEntryItem);
    out.upgrade = memory().read<std::int32_t>(entry + kEntryUpgrade) == 1;
    return out;
}

void GameView::put_upgrade_entry(int place, int item, const Row& row) const {
    const openrac::gaddr entry = entry_address(place);

    // The fields of the game's own Mega entry (func_002F6608): no ammo kind, its price's text, not
    // marked, an upgrade.
    memory().write(entry + kEntryItem, static_cast<std::int32_t>(item));
    memory().write(entry + kEntryKind, std::int32_t{0});
    memory().write(entry + kEntryClass, row.vendor_class());
    memory().write(entry + kEntryPrice, kPriceText);
    memory().write(entry + kEntryMarked, std::int32_t{0});
    memory().write(entry + kEntryUpgrade, std::int32_t{1});
}

void GameView::open_vendor_place(int place, int count) const {
    const auto moved = static_cast<std::size_t>(count - place) * kVendorEntryBytes;
    std::memmove(
        memory().pointer(entry_address(place + 1)), memory().pointer(entry_address(place)), moved
    );
}

std::string GameView::text(std::int32_t id) const {
    // A build with no lookup has no texts to read.
    if (m_text == nullptr) {
        return {};
    }

    // The mod's own replacement of the lookup sees this one and leaves it to the game.
    ++m_own_lookups;
    const auto address = static_cast<std::uint32_t>(m_text(id));
    --m_own_lookups;

    std::string out;

    // Byte by byte, each checked to be game memory, up to the string's end.
    for (std::uint32_t n = 0; n < kTextLimit && in_memory(address + n, 1); ++n) {
        const char c = memory().read<char>(address + n);

        // The string's terminator.
        if (c == '\0') {
            break;
        }

        out += c;
    }

    return out;
}

std::optional<std::uint32_t> GameView::slot_address(int slot) const {
    // A build with no lookup has no slots either.
    if (m_text == nullptr) {
        return std::nullopt;
    }

    ++m_own_lookups;
    const auto address = static_cast<std::uint32_t>(m_text(hooks::kTextSlot + slot));
    --m_own_lookups;

    // A port without the slots answers the id with nothing it can hold.
    if (!in_memory(address, hooks::kTextSlotBytes)) {
        return std::nullopt;
    }

    return address;
}

bool GameView::has_slots() const {
    return slot_address(0).has_value();
}

std::optional<std::uint32_t> GameView::write_slot(int slot, std::string_view text) {
    const std::optional<std::uint32_t> address = slot_address(slot);

    // No slot to write.
    if (!address) {
        return std::nullopt;
    }

    // The text, cut to leave room for its terminator.
    const std::size_t length = std::min<std::size_t>(text.size(), hooks::kTextSlotBytes - 1);
    std::memcpy(memory().pointer(*address), text.data(), length);
    memory().write(*address + static_cast<std::uint32_t>(length), '\0');

    // The first one, for a player's log: proof the port's slots work.
    if (!m_slot_logged) {
        const std::string where = std::to_string(slot) + " (0x" + hex32(*address) + ")";
        m_mod.info("text \"" + std::string(text) + "\" in slot " + where);
        m_slot_logged = true;
    }

    return address;
}

}  // namespace ng_plus_plus
