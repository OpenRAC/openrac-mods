/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The mods a party has to agree on (party_mods.hpp).
 */
#include "party_mods.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <utility>

#include "coop.hpp"
#include "modcode.hpp"

namespace mp {
namespace {

void u16(Writer& w, std::size_t v) {
    w.u8(static_cast<std::uint8_t>(v >> 8)).u8(static_cast<std::uint8_t>(v));
}

std::size_t u16(Reader& r) {
    const auto high = static_cast<std::size_t>(r.u8()) << 8;
    return high | r.u8();
}

/* FNV-1a, 32 bits. */
void mix(std::uint32_t& hash, const std::string& text) {
    for (const char c : text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }

    hash = (hash ^ 0xFFu) * 16777619u;
}

const ModInfo* find_mod(const ModList& list, const std::string& id) {
    for (const ModInfo& m : list.mods) {
        if (m.id == id) {
            return &m;
        }
    }

    return nullptr;
}

const ModSettingInfo* find_setting(const ModInfo& mod, const std::string& key) {
    for (const ModSettingInfo& s : mod.settings) {
        if (s.key == key) {
            return &s;
        }
    }

    return nullptr;
}

std::string with_more(const std::string& first, std::size_t others) {
    return others == 0 ? first : first + ", " + std::to_string(others) + " more";
}

}  // namespace

Bytes encode_mod_list(const ModList& list) {
    Writer w;
    w.str(list.multiplayer).str(list.openrac).str(list.disc);
    const std::size_t count = std::min<std::size_t>(list.mods.size(), 255);
    w.u8(static_cast<std::uint8_t>(count));

    for (std::size_t i = 0; i < count; ++i) {
        const ModInfo& m = list.mods[i];
        const std::size_t settings = std::min<std::size_t>(m.settings.size(), 255);
        w.str(m.id).str(m.name).str(m.version).u8(static_cast<std::uint8_t>(m.party));
        w.u8(static_cast<std::uint8_t>(settings));

        for (std::size_t k = 0; k < settings; ++k) {
            const ModSettingInfo& s = m.settings[k];
            w.str(s.key).str(s.name).str(s.value).str(s.shown).u8(
                static_cast<std::uint8_t>(s.party)
            );
            w.u8(s.is_default ? 1 : 0);
        }
    }

    w.str(list.family).str(list.modcode);
    return w.bytes();
}

bool decode_mod_list(Reader& r, ModList& out) {
    out = ModList{};
    out.multiplayer = r.str();
    out.openrac = r.str();
    out.disc = r.str();
    const std::uint8_t count = r.u8();

    for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
        ModInfo m;
        m.id = r.str();
        m.name = r.str();
        m.version = r.str();
        m.party = r.u8();
        const std::uint8_t settings = r.u8();

        for (std::uint8_t k = 0; k < settings && r.ok(); ++k) {
            ModSettingInfo s;
            s.key = r.str();
            s.name = r.str();
            s.value = r.str();
            s.shown = r.str();
            s.party = r.u8();
            s.is_default = r.u8() != 0;
            m.settings.push_back(std::move(s));
        }

        out.mods.push_back(std::move(m));
    }

    out.family = r.left() > 0 ? r.str() : out.disc;
    out.modcode = r.left() > 0 ? r.str() : "";

    // Text of a code's shape or nothing: another game's bytes are not shown as they come.
    if (!valid_mod_code(out.modcode)) {
        out.modcode.clear();
    }

    return r.ok();
}

std::uint32_t mods_digest(const ModList& list) {
    std::vector<std::string> parts;

    // Each mod a party agrees on, by its id, so the order they load in does not count.
    for (const ModInfo& m : list.mods) {
        if (!m.counts()) {
            continue;
        }

        std::vector<std::string> settings;

        for (const ModSettingInfo& s : m.settings) {
            if (s.party == kSettingShared) {
                settings.push_back(s.key + "=" + s.value);
            }
        }

        std::sort(settings.begin(), settings.end());
        std::string part = m.id + " " + m.version;

        for (const std::string& s : settings) {
            part += "\n" + s;
        }

        parts.push_back(std::move(part));
    }

    std::sort(parts.begin(), parts.end());
    std::uint32_t hash = 2166136261u;
    mix(hash, list.multiplayer);
    mix(hash, list.openrac);
    mix(hash, list.family);

    for (const std::string& part : parts) {
        mix(hash, part);
    }

    return hash;
}

std::vector<Difference> solo_mods(const ModList& own) {
    std::vector<Difference> out;

    for (const ModInfo& m : own.mods) {
        if (m.party == kModSolo) {
            Difference d;
            d.kind = Difference::Kind::kSolo;
            d.mod = m.name;
            out.push_back(std::move(d));
        }
    }

    return out;
}

std::vector<Difference> compare_mods(const ModList& own, const ModList& host) {
    using Kind = Difference::Kind;
    std::vector<Difference> out = solo_mods(own);
    const auto add =
        [&out](
            Kind kind, const std::string& mod, const std::string& mine, const std::string& theirs
        ) {
            Difference d;
            d.kind = kind;
            d.mod = mod;
            d.mine = mine;
            d.theirs = theirs;
            out.push_back(std::move(d));
        };

    // The disc first: with a disc of another world nothing else below matters to the player.
    if (own.family != host.family) {
        add(Kind::kDisc, "", own.family, host.family);
    }

    if (own.openrac != host.openrac) {
        add(Kind::kOpenRac, "", own.openrac, host.openrac);
    }

    if (own.multiplayer != host.multiplayer) {
        add(Kind::kMultiplayer, "", own.multiplayer, host.multiplayer);
    }

    // The host's that count: each here, at its version, with its compared settings.
    for (const ModInfo& h : host.mods) {
        const ModInfo* m = find_mod(own, h.id);

        if (!h.counts() && (m == nullptr || !m->counts())) {
            continue;
        }

        if (m == nullptr) {
            add(Kind::kHostOnly, h.name, "", h.version);
        } else if (m->version != h.version) {
            add(Kind::kVersion, h.name, m->version, h.version);
        } else {
            Difference d;
            d.kind = Kind::kSettings;
            d.mod = h.name;

            for (const ModSettingInfo& theirs : h.settings) {
                const ModSettingInfo* mine = find_setting(*m, theirs.key);

                if (theirs.party == kSettingShared
                    && (mine == nullptr || mine->value != theirs.value)) {
                    d.settings.push_back(theirs.name);
                }
            }

            if (!d.settings.empty()) {
                out.push_back(std::move(d));
            }
        }
    }

    // What only this game has.
    for (const ModInfo& m : own.mods) {
        if (m.party == kModGameplay && find_mod(host, m.id) == nullptr) {
            add(Kind::kYouOnly, m.name, m.version, "");
        }
    }

    return out;
}

std::vector<std::string> wrap_text(const std::string& text, std::size_t width) {
    std::vector<std::string> out;
    std::string line;
    std::size_t at = 0;

    while (at < text.size()) {
        std::size_t end = text.find(' ', at);
        end = end == std::string::npos ? text.size() : end;
        std::string word = text.substr(at, end - at);
        at = end + 1;

        // A word wider than a line (a long name) is cut, not lost.
        while (word.size() > width) {
            if (!line.empty()) {
                out.push_back(line);
                line.clear();
            }

            out.push_back(word.substr(0, width));
            word.erase(0, width);
        }

        if (!line.empty() && line.size() + 1 + word.size() > width) {
            out.push_back(line);
            line.clear();
        }

        line += (line.empty() ? "" : " ") + word;
    }

    if (!line.empty()) {
        out.push_back(line);
    }

    return out;
}

std::vector<std::string> wrap_list(const std::vector<std::string>& items, std::size_t width) {
    std::vector<std::string> out;
    std::string line;

    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string item = items[i] + (i + 1 < items.size() ? "," : "");

        if (!line.empty() && line.size() + 1 + item.size() > width) {
            out.push_back(line);
            line.clear();
        }

        line += (line.empty() ? "" : " ") + item;
    }

    if (!line.empty()) {
        out.push_back(line);
    }

    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i].size() > width) {
            const std::vector<std::string> cut = wrap_text(out[i], width);
            out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(i), cut.begin(), cut.end());
            i += cut.size() - 1;
        }
    }

    return out;
}

std::vector<std::string> refusal_lines(
    const std::vector<Difference>& differences, std::size_t width
) {
    using Kind = Difference::Kind;
    std::vector<std::string> out;
    const auto add = [&out, width](const std::string& text) {
        const std::vector<std::string> lines = wrap_text(text, width);
        out.insert(out.end(), lines.begin(), lines.end());
    };

    for (const Difference& d : differences) {
        switch (d.kind) {
            case Kind::kDisc:
                add(kDifferentDisc);
                break;
            case Kind::kOpenRac:
                add("OpenRAC: the host has " + d.theirs + ", you have " + d.mine);
                break;
            case Kind::kMultiplayer:
                add("Multiplayer: the host has " + d.theirs + ", you have " + d.mine);
                break;
            case Kind::kHostOnly:
                add("Host has " + d.mod + ", you don't");
                break;
            case Kind::kYouOnly:
                add("You have " + d.mod + ", the host doesn't");
                break;
            case Kind::kVersion:
                add(d.mod + ": the host has " + d.theirs + ", you have " + d.mine);
                break;
            case Kind::kSettings: {
                add("Your " + d.mod + " settings differ:");
                const std::vector<std::string> names = wrap_list(d.settings, width);
                out.insert(out.end(), names.begin(), names.end());
                break;
            }
            case Kind::kSolo:
                add(d.mod + " can't be played in a party");
                break;
        }
    }

    return out;
}

std::string refusal_short(const std::vector<Difference>& differences) {
    using Kind = Difference::Kind;

    if (differences.empty()) {
        return "";
    }

    const Difference& d = differences.front();
    std::string first;

    switch (d.kind) {
        case Kind::kDisc:
            first = "a different disc";
            break;
        case Kind::kOpenRac:
            first = "another OpenRAC";
            break;
        case Kind::kMultiplayer:
            first = "another Multiplayer";
            break;
        case Kind::kHostOnly:
            first = "no " + d.mod;
            break;
        case Kind::kYouOnly:
            first = "extra " + d.mod;
            break;
        case Kind::kVersion:
            first = d.mod + " version";
            break;
        case Kind::kSettings:
            first = d.mod + " settings";
            break;
        case Kind::kSolo:
            first = d.mod + " is solo only";
            break;
    }

    return with_more(first, differences.size() - 1);
}

std::vector<std::string> rules_lines(
    const ModList& host, const std::vector<PartyRules>& rules, const ModList& own, std::size_t width
) {
    std::vector<std::string> out;
    const auto add = [&out](const std::vector<std::string>& lines) {
        out.insert(out.end(), lines.begin(), lines.end());
    };

    for (const ModInfo& m : host.mods) {
        if (!m.counts()) {
            continue;
        }

        add(wrap_text(m.name + " " + m.version, width));
        const auto rule = std::find_if(rules.begin(), rules.end(), [&m](const PartyRules& r) {
            return r.id == m.id;
        });
        std::vector<std::string> items;

        if (rule != rules.end() && !rule->text.empty()) {
            std::size_t at = 0;

            while (at <= rule->text.size()) {
                std::size_t end = rule->text.find('\n', at);
                end = end == std::string::npos ? rule->text.size() : end;

                if (end > at) {
                    items.push_back(rule->text.substr(at, end - at));
                }

                at = end + 1;
            }
        } else {
            for (const ModSettingInfo& s : m.settings) {
                if (s.party == kSettingShared && !s.is_default) {
                    items.push_back(s.name + ": " + s.shown);
                }
            }
        }

        add(wrap_list(items, width));
    }

    if (out.empty()) {
        add(wrap_text("No gameplay mods: the game as it is", width));
    }

    // Two discs of one world: the levels are the same, the enemies play by the host's.
    if (host.disc != own.disc) {
        add(wrap_text("Enemies follow the host's game", width));
    }

    // What stays the player's: its input and look mods, the settings its gameplay mods call its own.
    std::vector<std::string> mine;

    for (const ModInfo& m : own.mods) {
        if (!m.counts()) {
            mine.push_back(m.name);
            continue;
        }

        for (const ModSettingInfo& s : m.settings) {
            if (s.party == kSettingOwn) {
                mine.push_back(s.name);
            }
        }
    }

    add(wrap_text("Stays yours: your controls and looks", width));

    if (!mine.empty()) {
        add(wrap_list(mine, width));
    }

    return out;
}

std::string digest_tag(std::uint32_t digest) {
    char tag[8];
    std::snprintf(tag, sizeof tag, "%04x", static_cast<unsigned>(digest & 0xFFFFu));
    return tag;
}

std::string tagged_description(const std::string& description, std::uint32_t digest) {
    const std::string tag = "|" + digest_tag(digest);
    return description.substr(0, kMaxDescriptionLength - tag.size()) + tag;
}

std::string plain_description(const std::string& description) {
    const std::size_t bar = description.rfind('|');
    return bar == std::string::npos ? description : description.substr(0, bar);
}

std::string description_tag(const std::string& description) {
    const std::size_t bar = description.rfind('|');
    return bar == std::string::npos ? "" : description.substr(bar + 1);
}

PartyMods::PartyMods(
    Session& session, Game& game, ModList own, std::function<std::uint64_t()> clock
)
    : session_(session),
      game_(game),
      own_(std::move(own)),
      clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = now_ms;
    }
}

const ModList& PartyMods::host_list() const {
    return session_.is_host() || !heard_host_ ? own_ : host_;
}

Fit PartyMods::fit(std::int32_t member) const {
    if (member == session_.host()) {
        return Fit::kMatch;
    }

    const auto it = peers_.find(member);
    return it != peers_.end() ? it->second.fit : Fit::kUnknown;
}

std::string PartyMods::why(std::int32_t member) const {
    const auto it = peers_.find(member);
    return it != peers_.end() ? it->second.why : std::string();
}

bool PartyMods::may_play(std::int32_t member) const {
    return fit(member) == Fit::kMatch;
}

bool PartyMods::host_silent() const {
    return !session_.is_host() && session_.in_party() && !heard_host_
           && clock_() - joined_ms_ >= kModsQuietMs;
}

void PartyMods::clear() {
    // The party went: this game's mods go by their own settings again.
    for (const std::string& id : delivered_) {
        game_.deliver_rules(id, {});
    }

    delivered_.clear();
    rules_.clear();
    peers_.clear();
    differences_.clear();
    heard_host_ = false;
    host_ = ModList{};
}

void PartyMods::set_mod_code(const std::string& code) {
    if (code == own_.modcode) {
        return;
    }

    own_.modcode = code;

    if (session_.in_party() && session_.is_host()) {
        send_mods(kBroadcast);
    }
}

void PartyMods::send_mods(std::int32_t target) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(ModsKind::kMods)).raw(encode_mod_list(own_));
    session_.send_data(target, w.bytes());
}

void PartyMods::send_fits() {
    Writer w;
    w.u8(static_cast<std::uint8_t>(ModsKind::kFits)).u8(static_cast<std::uint8_t>(peers_.size()));

    for (const auto& [id, peer] : peers_) {
        w.i32(id).u8(static_cast<std::uint8_t>(peer.fit)).str(peer.why);
    }

    session_.send_data(kBroadcast, w.bytes());
}

void PartyMods::send_rules(std::int32_t target) {
    Writer w;
    w.u8(static_cast<std::uint8_t>(ModsKind::kRules)).u8(static_cast<std::uint8_t>(rules_.size()));

    for (const PartyRules& r : rules_) {
        w.str(r.id);
        u16(w, r.data.size());
        w.raw(r.data).str(r.text);
    }

    session_.send_data(target, w.bytes());
}

void PartyMods::on_mods(std::int32_t sender, Reader& r) {
    ModList theirs;

    if (!decode_mod_list(r, theirs)) {
        return;
    }

    if (!session_.is_host()) {
        if (sender != session_.host()) {
            return;
        }

        const bool first = !heard_host_;
        heard_host_ = true;
        host_ = std::move(theirs);
        differences_ = compare_mods(own_, host_);

        if (first) {
            log(differences_.empty() ? "the host's mods match"
                                     : "the host's mods differ: " + refusal_short(differences_));
        }

        return;
    }

    Peer& peer = peers_[sender];
    const bool first = !peer.answered;
    const std::vector<Difference> differences = compare_mods(theirs, own_);
    const Fit fit = differences.empty() ? Fit::kMatch : Fit::kDiffers;
    const std::string why = refusal_short(differences);
    const bool changed = fit != peer.fit || why != peer.why;
    peer.fit = fit;
    peer.why = why;
    peer.answered = true;

    if (first) {
        send_mods(sender);
        log("player " + std::to_string(sender)
            + (fit == Fit::kMatch ? ": mods match" : ": mods differ: " + why));
    }

    if (changed) {
        send_fits();
    }

    // Ahead of any PLAY on the stream (Coop's gate opens with this fit).
    if (fit == Fit::kMatch && (first || changed)) {
        // Newer rules than the last poll saw are everyone's; else only this member lacks them.
        if (!refresh_rules()) {
            send_rules(sender);
        }
    }
}

void PartyMods::on_fits(Reader& r) {
    const std::uint8_t count = r.u8();

    for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
        const std::int32_t id = r.i32();
        const auto fit = static_cast<Fit>(r.u8());
        std::string why = r.str();

        if (r.ok()) {
            Peer& peer = peers_[id];
            peer.fit = fit;
            peer.why = std::move(why);
        }
    }
}

void PartyMods::on_rules(Reader& r) {
    std::vector<PartyRules> rules;
    const std::uint8_t count = r.u8();

    for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
        PartyRules rule;
        rule.id = r.str();
        const std::size_t size = u16(r);

        if (size > r.left()) {
            return;
        }

        rule.data.resize(size);

        for (std::uint8_t& b : rule.data) {
            b = r.u8();
        }

        rule.text = r.str();
        rules.push_back(std::move(rule));
    }

    if (!r.ok()) {
        return;
    }

    // What changed goes to this game's same mod, at once: no level loads without it.
    for (const PartyRules& rule : rules) {
        const auto old = std::find_if(rules_.begin(), rules_.end(), [&rule](const PartyRules& x) {
            return x.id == rule.id;
        });

        if (old == rules_.end() || old->data != rule.data) {
            if (game_.deliver_rules(rule.id, rule.data)
                && std::find(delivered_.begin(), delivered_.end(), rule.id) == delivered_.end()) {
                delivered_.push_back(rule.id);
            }
        }
    }

    rules_ = std::move(rules);
}

void PartyMods::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    Reader r(bytes.data() + 1, bytes.size() - 1);
    const auto kind = static_cast<ModsKind>(bytes[0]);
    const bool from_host = sender == session_.host() && !session_.is_host();

    if (kind == ModsKind::kMods) {
        on_mods(sender, r);
    } else if (kind == ModsKind::kFits && from_host) {
        on_fits(r);
    } else if (kind == ModsKind::kRules && from_host) {
        on_rules(r);
    }
}

bool PartyMods::refresh_rules() {
    std::vector<PartyRules> rules = game_.party_rules();
    const bool changed = rules.size() != rules_.size()
                         || !std::equal(
                             rules.begin(),
                             rules.end(),
                             rules_.begin(),
                             [](const PartyRules& a, const PartyRules& b) {
                                 return a.id == b.id && a.data == b.data && a.text == b.text;
                             }
                         );

    if (!changed) {
        return false;
    }

    rules_ = std::move(rules);

    for (const auto& [id, peer] : peers_) {
        if (peer.fit == Fit::kMatch) {
            send_rules(id);
        }
    }

    return true;
}

void PartyMods::tick_host(std::uint64_t now) {
    bool gone = false;

    // Those who left are forgotten; those who came are waited for.
    for (auto it = peers_.begin(); it != peers_.end();) {
        const bool here =
            std::any_of(session_.members().begin(), session_.members().end(), [&](const Member& m) {
                return m.id == it->first;
            });
        gone = gone || !here;
        it = here ? std::next(it) : peers_.erase(it);
    }

    for (const Member& m : session_.members()) {
        if (m.id != session_.id() && peers_.count(m.id) == 0) {
            peers_[m.id].since = now;
        }
    }

    // One that says nothing is an older game: it stays out, and the host's lobby says so.
    for (auto& [id, peer] : peers_) {
        if (!peer.answered && peer.fit == Fit::kUnknown && now - peer.since >= kModsQuietMs) {
            peer.fit = Fit::kDiffers;
            peer.why = "an older Multiplayer";
            gone = true;
        }
    }

    if (gone) {
        send_fits();
    }

    if (now - polled_ms_ < kRulesPollMs) {
        return;
    }

    polled_ms_ = now;
    refresh_rules();
}

void PartyMods::tick(std::uint64_t now) {
    if (!session_.in_party()) {
        if (in_party_) {
            in_party_ = false;
            clear();
        }

        return;
    }

    // Another party than the last tick's, with no tick out of one between: nothing of it stands.
    if (in_party_ && (session_.host() != party_host_ || session_.code() != party_code_)) {
        clear();
        in_party_ = false;
    }

    if (!in_party_) {
        in_party_ = true;
        joined_ms_ = now;
        sent_ms_ = 0;
        party_host_ = session_.host();
        party_code_ = session_.code();

        if (session_.is_host()) {
            rules_ = game_.party_rules();
        }
    }

    if (session_.is_host()) {
        tick_host(now);
    } else if (!heard_host_ && (sent_ms_ == 0 || now - sent_ms_ >= kModsMs)) {
        sent_ms_ = now;
        send_mods(session_.host());
    }
}

}  // namespace mp
