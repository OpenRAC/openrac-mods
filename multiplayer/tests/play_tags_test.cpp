// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Playing together, without the game or a socket: the names over the figures, and the mod code a
// party's pages show.
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "modcode.hpp"
#include "nametags.hpp"
#include "test_support.hpp"

namespace mpt {
namespace {

struct Drawn {
    float x, y, w, h;
    std::uint32_t colour;
    std::string text;
};

/* The middle of the picture for every point, unless told otherwise; the port's fade; a character
 * 6 wide at height 8. */
class FakePainter : public mp::TagPainter {
public:
    bool on_picture = true;
    float at_x = 256;
    float at_y = 224;
    std::vector<Drawn> drawn;

    bool project(const float[3], float& x, float& y) override {
        x = at_x;
        y = at_y;
        return on_picture;
    }

    void rect(float x, float y, float w, float h, std::uint32_t colour) override {
        drawn.push_back({x, y, w, h, colour, ""});
    }

    void text(float x, float y, float height, std::uint32_t colour, const std::string& line) override {
        drawn.push_back({x, y, 0, height, colour, line});
    }

    float text_width(float height, const std::string& line) override {
        return height * 0.75f * static_cast<float>(line.size());
    }

    float fade(float distance, float near_distance, float far_distance) override {
        return distance >= far_distance  ? 0.0f
               : distance <= near_distance ? 1.0f
                                           : (far_distance - distance) / (far_distance - near_distance);
    }

    const Drawn* text_drawn() const {
        for (const Drawn& d : drawn) {
            if (!d.text.empty()) {
                return &d;
            }
        }

        return nullptr;
    }
};

/* A tag `distance` from a camera at the origin. */
mp::NameTag tag_at(float distance, int slot = 1) {
    mp::NameTag tag;
    tag.name = "RATCHET";
    tag.slot = slot;
    tag.position[0] = distance;
    tag.position[2] = -mp::kTagAbove;  // the tag itself is level with the camera
    return tag;
}

const float kCamera[3] = {0, 0, 0};

void a_tag_up_close_says_the_name_in_the_players_colour() {
    FakePainter p;
    const mp::TagStats stats = mp::draw_name_tags(p, kCamera, {tag_at(5.0f)});
    CHECK(stats.tags == 1 && stats.on_picture == 1 && stats.x == 256 && stats.y == 224);
    const Drawn* text = p.text_drawn();
    CHECK(text != nullptr && text->text == "RATCHET");
    CHECK(text->colour == mp::tag_colour(1) && (text->colour & 0xFFu) == 0xFFu);
    CHECK(text->h == mp::kTagTextNear);
    // Centred over the figure, above the point, on a dark backing and no dot.
    CHECK(std::fabs(text->x - (256 - 10 * 0.75f * 7 / 2)) < 1e-3f && text->y < 224);
    CHECK(p.drawn.size() == 2 && p.drawn[0].text.empty() && (p.drawn[0].colour >> 8) == 0u);
    CHECK(p.drawn[0].y <= text->y && p.drawn[0].h >= text->h);
}

void far_away_only_a_dot_is_left() {
    FakePainter p;
    mp::draw_name_tags(p, kCamera, {tag_at(mp::kTagDot + 1)});
    CHECK(p.text_drawn() == nullptr && p.drawn.size() == 1);
    CHECK(p.drawn[0].w == mp::kTagDotSize && p.drawn[0].colour == mp::tag_colour(1));

    // Between the two, the text is at its smallest as the name fades into the dot.
    FakePainter between;
    mp::draw_name_tags(between, kCamera, {tag_at((mp::kTagFadeFrom + mp::kTagDot) / 2)});
    const Drawn* text = between.text_drawn();
    CHECK(text != nullptr && text->h == mp::kTagTextFar);
    const std::uint32_t alpha = text->colour & 0xFFu;
    CHECK(alpha > 0x70u && alpha < 0x90u);
    CHECK((between.drawn[0].colour & 0xFFu) > 0x70u && (between.drawn[0].colour & 0xFFu) < 0x90u);
}

void the_text_shrinks_with_distance() {
    FakePainter near_one, mid;
    mp::draw_name_tags(near_one, kCamera, {tag_at(mp::kTagNear)});
    mp::draw_name_tags(mid, kCamera, {tag_at((mp::kTagNear + mp::kTagFadeFrom) / 2)});
    CHECK(near_one.text_drawn()->h == mp::kTagTextNear);
    CHECK(mid.text_drawn()->h < mp::kTagTextNear && mid.text_drawn()->h > mp::kTagTextFar);
}

void off_the_picture_there_is_no_tag() {
    FakePainter p;
    p.on_picture = false;
    const mp::TagStats stats = mp::draw_name_tags(p, kCamera, {tag_at(5.0f)});
    CHECK(stats.tags == 1 && stats.on_picture == 0 && p.drawn.empty());
}

void a_tag_stays_on_the_picture() {
    FakePainter right, top;
    right.at_x = 510;
    mp::draw_name_tags(right, kCamera, {tag_at(5.0f)});
    const Drawn* text = right.text_drawn();
    CHECK(text->x + 10 * 0.75f * 7 <= 512 && text->x >= 0);
    top.at_y = 1;
    mp::draw_name_tags(top, kCamera, {tag_at(5.0f)});
    CHECK(top.text_drawn()->y >= 0);
}

void a_lagging_player_has_a_mark() {
    mp::NameTag tag = tag_at(5.0f);
    FakePainter fine, late;
    mp::draw_name_tags(fine, kCamera, {tag});
    tag.late = true;
    mp::draw_name_tags(late, kCamera, {tag});
    CHECK(late.drawn.size() == fine.drawn.size() + 1);
    const Drawn* text = late.text_drawn();
    CHECK(late.drawn.back().x > text->x && late.drawn.back().colour != text->colour);
}

void each_slot_has_its_colour() {
    for (int a = 0; a < 4; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            CHECK(mp::tag_colour(a) != mp::tag_colour(b));
        }
    }

    CHECK(mp::tag_colour(5) == mp::tag_colour(1) && mp::tag_colour(-1) == mp::tag_colour(3));
}

/* The figures shown are the tags: each player's name at its figure, in the party's order, in every
 * game; none for one hidden by a scene or gone. */
void the_shown_figures_have_their_names() {
    Party party;
    for (int i = 0; i < 30; ++i) {
        tick(party.players(), 16);
    }

    const std::vector<mp::NameTag> tags = party.a.coop.name_tags();
    CHECK(tags.size() == 2);
    CHECK(tags[0].name == "P1" && tags[0].slot == 0);
    CHECK(tags[1].name == "P3" && tags[1].slot == 2);
    const auto figure = party.a.game.figures.begin();
    CHECK(!party.a.game.figures.empty());
    bool at_a_figure = false;

    for (const auto& f : party.a.game.figures) {
        at_a_figure = at_a_figure || std::fabs(f.second.position[0] - tags[0].position[0]) < 1e-3f;
    }

    CHECK(at_a_figure && figure != party.a.game.figures.end());
    // The host sees P2 with P2's own slot: a colour is the same in every game.
    const std::vector<mp::NameTag> host_tags = party.host.coop.name_tags();
    CHECK(host_tags.size() == 2 && host_tags[0].name == "P2" && host_tags[0].slot == 1);
    // A scene here: every figure hidden, and no tag.
    party.a.game.now.phase = mp::kPhaseScene;
    tick(party.players(), 16);
    CHECK(party.a.coop.name_tags().empty());
}

/* What the launcher writes (modset.rs, write_game_file). */
const char* const kFile = R"({
  "code": "K7M2-9QXR",
  "created": "2026-10-31T12:00:00Z",
  "format": 1,
  "link": "openrac://modset/K7M2-9QXR",
  "mods": [
    {"id": "difficulty-mod", "version": "1.4.0"},
    {"id": "item-randomizer", "version": "0.3.1"}
  ]
}
)";

mp::ModList list_of(std::vector<std::pair<std::string, std::string>> mods) {
    mp::ModList list;

    for (const auto& m : mods) {
        mp::ModInfo info;
        info.id = m.first;
        info.version = m.second;
        list.mods.push_back(info);
    }

    return list;
}

void the_launchers_file_is_read() {
    mp::ModSetFile file;
    CHECK(mp::parse_modset(kFile, file));
    CHECK(file.code == "K7M2-9QXR" && file.link == "openrac://modset/K7M2-9QXR");
    CHECK(file.mods.size() == 2 && file.mods[1].first == "item-randomizer" && file.mods[1].second == "0.3.1");
    // The server could not be reached: no short code, the long link.
    CHECK(mp::parse_modset(R"({"format":1,"code":null,"link":"openrac://modset/ORMS1.abc","mods":[]})", file));
    CHECK(file.code.empty() && file.link == "openrac://modset/ORMS1.abc" && file.mods.empty());
    // Keys of a later file are passed over.
    CHECK(mp::parse_modset(R"({"format":1,"future":{"a":[1,"}",null]},"mods":[{"id":"a","version":"1","x":2}]})", file));
    CHECK(file.mods.size() == 1);

    for (const char* bad : {"", "[]", "{", "{}", R"({"format":2,"mods":[]})", R"({"format":1})",
                            R"({"mods":[]})", R"({"format":1,"mods":[{"id":"a"}]})",
                            R"({"format":1,"mods":[]} x)", R"({"format":1,"mods":[}")",
                            R"({"format":1,"code":7,"mods":[]})"}) {
        CHECK(!mp::parse_modset(bad, file));
    }

    CHECK(!mp::parse_modset(std::string(70000, ' ') + kFile, file));
}

void the_code_shows_only_for_the_mods_it_names() {
    mp::ModSetFile file;
    CHECK(mp::parse_modset(kFile, file));
    mp::ModList same = list_of({{"item-randomizer", "0.3.1"}, {"difficulty-mod", "1.4.0"}});
    CHECK(mp::modset_matches(file, same));
    // A mod that is a player's own is no matter.
    mp::ModInfo input;
    input.id = "keyboard-mouse";
    input.version = "2.0.1";
    input.party = mp::kModInput;
    same.mods.push_back(input);
    CHECK(mp::modset_matches(file, same));
    // Another version, a mod more, a mod less: not this code.
    CHECK(!mp::modset_matches(file, list_of({{"item-randomizer", "0.3.2"}, {"difficulty-mod", "1.4.0"}})));
    CHECK(!mp::modset_matches(file, list_of({{"difficulty-mod", "1.4.0"}})));
    CHECK(!mp::modset_matches(file, list_of({{"item-randomizer", "0.3.1"}, {"difficulty-mod", "1.4.0"}, {"x", "1"}})));
}

void the_file_is_found_in_the_folder() {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("mp_modcode_" + std::to_string(std::rand()));
    std::filesystem::create_directories(folder);
    const mp::ModList own = list_of({{"difficulty-mod", "1.4.0"}, {"item-randomizer", "0.3.1"}});
    CHECK(!mp::read_mod_code(folder.string(), own).shared);  // none there
    CHECK(!mp::read_mod_code("", own).shared);
    {
        std::ofstream(folder / "modset.json") << kFile;
    }
    mp::ModCode found = mp::read_mod_code(folder.string(), own);
    CHECK(found.shared && found.code == "K7M2-9QXR");
    CHECK(mp::read_mod_code(folder.string(), mp::ModList{}).code.empty());
    {
        std::ofstream(folder / "modset.json") << R"({"format":1,"code":null,"mods":[]})";
    }
    found = mp::read_mod_code(folder.string(), mp::ModList{});
    CHECK(found.shared && found.code.empty());
    {
        std::ofstream(folder / "modset.json") << R"({"format":1,"code":"<b>hi</b>","mods":[]})";
    }
    found = mp::read_mod_code(folder.string(), mp::ModList{});
    CHECK(found.code.empty());
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

void only_a_codes_shape_is_a_code() {
    CHECK(mp::valid_mod_code("K7M2-9QXR"));
    CHECK(mp::mod_code_link("K7M2-9QXR") == "openrac://modset/K7M2-9QXR");

    for (const char* bad : {"", "K7M29QXR", "K7M2-9QX", "K7M2-9QXRR", "K0M2-9QXR", "k7m2-9qxr", "K7M2_9QXR",
                            "K7M2/9QXR", "../../x.", "https://x"}) {
        CHECK(!mp::valid_mod_code(bad) && mp::mod_code_link(bad).empty());
    }
}

void the_hosts_code_reaches_a_refused_member() {
    Player host(1), member(2);
    const std::vector<mp::Member> both = {{1, "P1"}, {2, "P2"}};
    mp::ModList mine = sample_list();
    setting_of(mine, "bolts").value = "3";
    gate(host, sample_list());
    gate(member, mine);
    host.session.handle(info(both));
    member.session.handle(info(both));
    tick_mods({&host, &member}, 16);
    tick_mods({&host, &member}, 16);
    CHECK(member.mods.heard_host() && member.mods.host_list().modcode.empty());
    // The code comes while the party waits: the member hears it, and nothing else changes.
    const std::uint32_t digest = mp::mods_digest(host.mods.own());
    host.mods.set_mod_code("K7M2-9QXR");
    tick_mods({&host, &member}, 16);
    CHECK(member.mods.host_list().modcode == "K7M2-9QXR");
    CHECK(mp::mods_digest(host.mods.own()) == digest);
    CHECK(mp::compare_mods(sample_list(), host.mods.own()).empty());
    // And when it goes away (the mods changed), so does the member's.
    host.mods.set_mod_code("");
    tick_mods({&host, &member}, 16);
    CHECK(member.mods.host_list().modcode.empty());
}

void a_code_on_the_wire_is_only_a_code() {
    mp::ModList list = sample_list();
    list.modcode = "K7M2-9QXR";
    Bytes bytes = mp::encode_mod_list(list);
    mp::Reader r(bytes.data(), bytes.size());
    mp::ModList back;
    CHECK(mp::decode_mod_list(r, back) && r.left() == 0 && back.modcode == "K7M2-9QXR");
    list.modcode = "https://example.com/";
    bytes = mp::encode_mod_list(list);
    mp::Reader evil(bytes.data(), bytes.size());
    CHECK(mp::decode_mod_list(evil, back) && back.modcode.empty());
}

void the_words_name_no_region() {
    for (const std::string& line : {std::string(mp::kModCodeLabel), std::string(mp::kModCodeHint),
                                    std::string(mp::kModCodeInLauncher), std::string(mp::kOpenInLauncher),
                                    std::string(mp::kDifferentDisc)}) {
        CHECK(line.find("PAL") == std::string::npos && line.find("NTSC") == std::string::npos);
        CHECK(line.size() <= mp::kPauseLine + 10);
    }

    CHECK(std::string(mp::kDifferentDisc).find("a different disc") != std::string::npos);
}

}  // namespace

void run_tags_tests() {
    a_tag_up_close_says_the_name_in_the_players_colour();
    far_away_only_a_dot_is_left();
    the_text_shrinks_with_distance();
    off_the_picture_there_is_no_tag();
    a_tag_stays_on_the_picture();
    a_lagging_player_has_a_mark();
    each_slot_has_its_colour();
    the_shown_figures_have_their_names();
    the_launchers_file_is_read();
    the_code_shows_only_for_the_mods_it_names();
    the_file_is_found_in_the_folder();
    only_a_codes_shape_is_a_code();
    the_hosts_code_reaches_a_refused_member();
    a_code_on_the_wire_is_only_a_code();
    the_words_name_no_region();
}

}  // namespace mpt
