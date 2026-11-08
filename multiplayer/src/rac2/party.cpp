/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * A party without pages (party.hpp).
 */
#include "rac2/party.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace mp {

PartyAction party_action(const std::string& value) {
    if (value == "host") {
        return PartyAction::kHost;
    }

    if (value == "join") {
        return PartyAction::kJoin;
    }

    if (value == "host_direct") {
        return PartyAction::kHostDirect;
    }

    if (value == "join_direct") {
        return PartyAction::kJoinDirect;
    }

    return PartyAction::kOff;
}

std::string party_code(const std::string& typed) {
    std::string code;

    for (const char c : typed) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            code += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    return code;
}

PageParty::PageParty(Session& session, Coop& coop, PartyMods& mods, PartyNet net, std::string game)
    : session_(session),
      coop_(coop),
      mods_(mods),
      net_(std::move(net)),
      game_(std::move(game)) {
    // The mod holds no control here: nobody waits for anyone's pad (Coop::barrier); and no world is
    // shared, so a member is in the host's world as soon as it stands in the host's level.
    coop_.barrier(false);
}

void PageParty::choose(PartyAction action, std::string code, std::string address) {
    action_ = action;
    code_ = party_code(code);
    address_ = std::move(address);
}

std::string PageParty::description() const {
    return tagged_description(kRac2Description, mods_digest(mods_.own()), game_);
}

void PageParty::notice(std::uint64_t now, const std::string& text) {
    notice_ = text;
    notice_until_ = now + kNoticeMs;
    log(text);
}

void PageParty::begin(std::uint64_t now) {
    if (begun_) {
        return;
    }

    begun_ = true;

    switch (action_) {
        case PartyAction::kOff:
            return;
        case PartyAction::kHost:
            net_.server();
            notice(now, "Multiplayer: hosting a party");
            return;
        case PartyAction::kJoin:
            if (code_.size() != kCodeLength) {
                action_ = PartyAction::kOff;
                notice(
                    now,
                    "Multiplayer: the code \"" + code_
                        + "\" is not a party's (6 letters and digits)"
                );
                return;
            }

            net_.server();
            notice(now, "Multiplayer: joining party " + code_);
            return;
        case PartyAction::kHostDirect: {
            std::string error;

            if (!net_.host_direct(description(), error)) {
                action_ = PartyAction::kOff;
                notice(
                    now,
                    "Multiplayer: cannot host here" + (error.empty() ? std::string() : ": " + error)
                );
                return;
            }

            notice(now, "Multiplayer: hosting directly, port " + std::to_string(kDefaultPort));
            return;
        }
        case PartyAction::kJoinDirect:
            if (address_.empty() || !net_.join_direct(address_)) {
                action_ = PartyAction::kOff;
                notice(now, "Multiplayer: no address to join (the address setting)");
                return;
            }

            notice(now, "Multiplayer: joining " + address_);
            return;
    }
}

void PageParty::ask(std::uint64_t now) {
    // Only the server's create and join are asked; a direct party comes with the connection.
    if (action_ != PartyAction::kHost && action_ != PartyAction::kJoin) {
        return;
    }

    if (had_party_ || !session_.greeted() || session_.in_party() || session_.waiting()) {
        return;
    }

    if (asks_ != 0 && now - asked_ms_ < kAskAgainMs) {
        return;
    }

    if (asks_ >= kAsks) {
        notice(
            now,
            action_ == PartyAction::kHost ? "Multiplayer: the server made no party"
                                          : "Multiplayer: could not join party " + code_
        );
        action_ = PartyAction::kOff;
        return;
    }

    ++asks_;
    asked_ms_ = now;

    if (action_ == PartyAction::kHost) {
        log("asking the server for a party: " + description());
        session_.create(description());
    } else {
        log("asking the server to join party " + code_);
        session_.join(code_);
    }
}

void PageParty::tick(std::uint64_t now, const GameStatus& status) {
    if (!begun_) {
        return;
    }

    // No world is shared: a member is in the host's world as soon as it stands in the host's level
    // (the co-op forgets it with each party).
    coop_.world_ready(true);

    // What the session and the co-op had to say (a refusal, the host gone, a game starting).
    bool told = false;

    for (const std::string& said : {session_.take_notice(), coop_.take_notice()}) {
        if (!said.empty()) {
            told = true;
            notice(now, "Multiplayer: " + said);
        }
    }

    ask(now);

    const bool in = session_.in_party();

    if (in != in_party_) {
        in_party_ = in;
        members_.clear();
        told_differs_ = false;
        title_since_ = 0;
        title_out_ = false;

        if (in) {
            had_party_ = true;
            const bool direct = session_.direct();
            notice(
                now,
                session_.is_host()
                    ? (direct
                           ? std::string("Multiplayer: hosting directly: others join your address")
                           : "Multiplayer: party " + session_.code()
                                 + ": others join with this code")
                    : "Multiplayer: in "
                          + (direct ? std::string("the direct party") : "party " + session_.code())
            );
        } else if (!told) {
            notice(now, "Multiplayer: out of the party");
        }
    }

    if (in) {
        tick_party(now, status);
    }
}

void PageParty::tick_party(std::uint64_t now, const GameStatus& status) {
    // A party of another game's (or one that does not say whose it is): left at once.
    if (!session_.is_host() && other_game(session_.description(), game_)) {
        log("the party is listed as \"" + session_.description() + "\": not this game's");
        session_.leave();
        in_party_ = false;
        members_.clear();
        notice(now, std::string("Multiplayer: ") + kOtherGameNotice);
        return;
    }

    // Who came and who went.
    std::set<std::int32_t> now_members;

    for (const Member& m : session_.members()) {
        now_members.insert(m.id);

        if (!members_.empty() && members_.count(m.id) == 0 && m.id != session_.id()) {
            notice(
                now,
                "Multiplayer: " + m.name + " joined"
                    + (session_.is_host() && !session_.direct() ? " (party " + session_.code() + ")"
                                                                : "")
            );
        }
    }

    for (const std::int32_t id : members_) {
        if (now_members.count(id) == 0) {
            notice(now, "Multiplayer: a player left");
        }
    }

    members_ = std::move(now_members);

    // A member whose mods differ from the host's is not brought into its game: said once.
    if (!session_.is_host() && mods_.heard_host() && !mods_.differences().empty()
        && !told_differs_) {
        told_differs_ = true;
        notice(now, "Multiplayer: " + refusal_short(mods_.differences()));
    }

    if (!session_.is_host()) {
        return;
    }

    // The host's game is the party's once it is in a level, and again after a while in the title.
    if (coop_.playing() && status.phase == kPhaseFrontEnd) {
        if (title_since_ == 0) {
            title_since_ = now;
        }

        title_out_ = title_out_ || now - title_since_ >= kTitleMs;
    } else {
        title_since_ = 0;
    }

    if (in_level(status.phase) && (!coop_.playing() || title_out_)) {
        title_out_ = false;
        coop_.adopt();
    }
}

std::string PageParty::banner(std::uint64_t now) const {
    const std::string coop = coop_.banner(now);

    if (!coop.empty()) {
        return coop;
    }

    if (!notice_.empty() && now < notice_until_) {
        return notice_;
    }

    // A host alone in its party: its code, until someone comes.
    if (session_.in_party() && session_.is_host() && !session_.direct()
        && session_.members().size() <= 1) {
        return "Multiplayer: party " + session_.code() + ": others join with this code";
    }

    return "";
}

}  // namespace mp
