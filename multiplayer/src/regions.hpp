// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The regions an OpenRAC hub lists game servers under, as a player reads them. The hub sends only
// a code (the Server's docs/configuration.md, Region codes): every ISO 3166-1 alpha-2 country code,
// EU for Europe as a whole and LOCAL for a LAN or development box. Here each code has a name
// ("United States") and a short label ("USA", the ISO alpha-3 code; "EU" and "LAN" for the two
// others). A code the table does not know reads as it came, so a newer server still shows.

#ifndef MULTIPLAYER_REGIONS_H
#define MULTIPLAYER_REGIONS_H

#include <cstddef>
#include <string>

namespace mp {

// One region: the code on the wire, its short label and its name.
struct Region {
    const char* code;
    const char* label;
    const char* name;
};

// Returns the region `text` names, in any case and with spaces around it: a code ("us"), a label
// ("USA") or a name ("United States"); nullptr when it names none.
const Region* find_region(const std::string& text);

// Returns how the region of `code` reads: its name, or its label when `short_form`; a code the
// table does not know, as it came.
std::string region_text(const std::string& code, bool short_form = false);

// Returns how a listed server reads: "<name> (<region>)", or the region alone for an empty name.
// With `max_chars`, the region's label stands in for a name that does not fit, then the server's
// name is cut to fit.
std::string server_text(
    const std::string& name, const std::string& code, std::size_t max_chars = 0
);

// Returns how many regions the table holds (the tests walk it).
std::size_t region_count();

// Returns the table's region `index`, below region_count().
const Region& region_at(std::size_t index);

}  // namespace mp

#endif
