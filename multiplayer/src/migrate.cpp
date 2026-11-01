/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * Losing a connection, coming back, and the party going on without its host (migrate.hpp).
 */
#include "migrate.hpp"
#include "world_util.hpp"

#include <algorithm>
#include <utility>

namespace mp {

using namespace wire;

namespace {

/* The most keys a set of them holds on the wire: a level has fewer records than this. */
constexpr std::size_t kMaxKeys = 0x8000;
constexpr std::size_t kMaxSpawned = 0x1000;

void put_keys(Writer& w, const std::set<int>& keys) {
    u16(w, static_cast<std::uint16_t>(keys.size()));

    for (const int key : keys) {
        u16(w, static_cast<std::uint16_t>(key));
    }
}

void take_keys(Reader& r, std::set<int>& keys) {
    const std::size_t count = u16(r);

    for (std::size_t i = 0; i < count && r.ok() && i < kMaxKeys; ++i) {
        keys.insert(u16(r));
    }
}

}  // namespace

Bytes encode_ledger(const Ledger& l) {
    Writer w;
    w.u8(kind::kLedger).u8(kLedgerVersion);
    u16(w, l.epoch);
    w.i32(static_cast<std::int32_t>(l.generation)).i32(l.play);
    u16(w, l.travel);
    u16(w, l.scene);
    w.u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(l.world.level))).u8(l.world.revive ? 1 : 0);
    u16(w, l.world.next_net);
    put_keys(w, l.world.dead);
    put_keys(w, l.world.dug);
    put_keys(w, l.world.spared);
    u16(w, static_cast<std::uint16_t>(l.world.spawned.size()));

    for (const LedgerMoby& m : l.world.spawned) {
        u16(w, m.net);
        u16(w, static_cast<std::uint16_t>(static_cast<std::int16_t>(m.oclass)));
        vec(w, m.position);
        f32(w, m.yaw);
    }

    return w.bytes();
}

bool decode_ledger(const Bytes& bytes, Ledger& out) {
    if (bytes.size() < 2 || bytes[0] != kind::kLedger || bytes[1] != kLedgerVersion) {
        return false;
    }

    Reader r(bytes.data() + 2, bytes.size() - 2);
    Ledger l;
    l.epoch = u16(r);
    l.generation = static_cast<std::uint32_t>(r.i32());
    l.play = r.i32();
    l.travel = u16(r);
    l.scene = u16(r);
    l.world.level = static_cast<std::int8_t>(r.u8());
    l.world.revive = r.u8() != 0;
    l.world.next_net = u16(r);
    take_keys(r, l.world.dead);
    take_keys(r, l.world.dug);
    take_keys(r, l.world.spared);
    const std::size_t count = u16(r);

    for (std::size_t i = 0; i < count && r.ok() && i < kMaxSpawned; ++i) {
        LedgerMoby m;
        m.net = u16(r);
        m.oclass = static_cast<std::int16_t>(u16(r));
        vec(r, m.position);
        m.yaw = f32(r);
        l.world.spawned.push_back(m);
    }

    if (!r.ok()) {
        return false;
    }

    out = std::move(l);
    return true;
}

bool ledger_newer(const Ledger& a, const Ledger& b) {
    return a.epoch != b.epoch ? static_cast<std::int16_t>(static_cast<std::uint16_t>(a.epoch - b.epoch)) > 0
                              : a.generation > b.generation;
}

std::vector<std::int32_t> successor_order(std::vector<Candidate> candidates) {
    // What a link is worth: a known round trip, and more of it for one that has only TCP.
    const auto cost = [](const Candidate& c) {
        return (c.link.ping_ms >= 0 ? c.link.ping_ms : 5000) + (c.link.udp ? 0 : 100);
    };

    std::stable_sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
        return cost(a) < cost(b);
    });

    std::vector<std::int32_t> out;

    for (const Candidate& c : candidates) {
        out.push_back(c.id);
    }

    return out;
}

Migrate::Migrate(Session& session, Game& game, Coop& coop, World& world, Progress& progress, Shared& shared,
                 PartyMods& mods, CoSave& cosave, CamScripts& cam, std::function<std::uint64_t()> clock)
    : session_(session),
      game_(game),
      coop_(coop),
      world_(world),
      progress_(progress),
      shared_(shared),
      mods_(mods),
      cosave_(cosave),
      cam_(cam),
      clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = now_ms;
    }
}

void Migrate::handle(std::int32_t sender, const Bytes& bytes) {
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    const bool from_host = session_.role().from_host(sender);

    if (bytes[0] == kind::kSuccessor && from_host) {
        Reader r(bytes.data() + 1, bytes.size() - 1);
        const std::uint8_t count = r.u8();
        std::vector<std::int32_t> next;

        for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
            next.push_back(r.i32());
        }

        if (r.ok()) {
            next_ = std::move(next);
        }
    } else if (bytes[0] == kind::kLedger) {
        Ledger heard;

        if (!decode_ledger(bytes, heard)) {
            return;
        }

        // The host's word, or an answer to this game's asking. Only ever a newer one stands, so
        // nothing of an old host can put the party back.
        answered_.insert(sender);

        if ((from_host || gather_ms_ != 0) && ledger_newer(heard, ledger_)) {
            ledger_ = std::move(heard);
        }
    } else if (bytes[0] == kind::kLedgerAsk && from_host) {
        session_.send_data(session_.host(), encode_ledger(ledger_));
    }
}

std::string Migrate::name_of(std::int32_t member) const {
    for (const Member& m : session_.members()) {
        if (m.id == member) {
            return m.name;
        }
    }

    return {};
}

void Migrate::clear() {
    if (host_gone_ms_ != 0 || stand_ms_ != 0) {
        coop_.hold_for_host(false);
    }

    coop_.between_hosts(false);
    host_gone_ms_ = stand_ms_ = gather_ms_ = 0;
    ledger_ = Ledger{};
    ledger_said_.clear();
    ledger_members_ = 0;
    named_.clear();
    next_.clear();
    answered_.clear();
    took_over_ = false;
    new_host_until_ = 0;
}

void Migrate::leaving() {
    if (session_.is_host() && !named_.empty()) {
        send_ledger(clock_());
    }
}

void Migrate::host_changed(std::int32_t host, std::uint64_t now) {
    coop_.between_hosts(false);
    host_gone_ms_ = 0;
    new_host_ = host;
    new_host_until_ = now + kNewHostLineMs;
    named_.clear();
    next_.clear();
    cosave_.host_changed();
    cam_.reset();
    progress_.again();
    shared_.again();

    if (host == session_.id()) {
        log("the party's host is gone: this game is next");

        if (!coop_.can_take_over()) {
            log("this game cannot carry it on: the next one is asked");
            coop_.hold_for_host(true);
            coop_.between_hosts(true);
            stand_ms_ = now;
            session_.decline_host();
            return;
        }

        // Every member may hold a newer ledger: the one the old host sent last went to each by
        // its own road.
        coop_.hold_for_host(true);
        coop_.between_hosts(true);
        gather_ms_ = now;
        answered_.clear();
        session_.send_data(kBroadcast, Bytes{kind::kLedgerAsk});
        return;
    }

    log(name_of(host) + " hosts the party now");
    coop_.host_changed();
    world_.rejoined();
    coop_.hold_for_host(true);
    stand_ms_ = now;
}

void Migrate::finish_gather(std::uint64_t now) {
    gather_ms_ = 0;

    // With a game on and no ledger of it, nobody here can say what the party's world is.
    if (coop_.playing() && ledger_.play != coop_.play()) {
        log("no ledger of this game: the next one is asked");
        stand_ms_ = now;
        session_.decline_host();
        return;
    }

    Bytes save = cosave_.slot() >= 0 ? game_.card_read(cosave_.slot()) : Bytes{};

    if (!save.empty() && !game_.save_usable(save)) {
        save.clear();
    }

    world_.take_over(ledger_.world);
    coop_.take_over(ledger_.travel, ledger_.scene, std::move(save), now);
    coop_.hold_for_host(false);
    coop_.between_hosts(false);
    took_over_ = true;
    ledger_.epoch = session_.role().epoch();
    ledger_said_.clear();
    ledger_members_ = 0;
    named_ms_ = 0;
    log("hosting the party: generation " + std::to_string(ledger_.generation) + " of epoch "
        + std::to_string(ledger_.epoch));
}

void Migrate::take(const PartyEvent& event, std::uint64_t now) {
    switch (event.kind) {
        case PartyEvent::Kind::kDropped:
            log("connection lost: keeping this game's place in the party");
            break;
        case PartyEvent::Kind::kResumed:
            log("back in the party");
            progress_.again();
            shared_.again();

            if (session_.role().host()) {
                world_.host_back();
            } else {
                coop_.rejoined(now);
                world_.rejoined();

                // It went away the host, and the party moved on.
                if (was_host_) {
                    log("the party has another host now: coming in again");
                    coop_.demoted();
                    world_.reset();
                }
            }

            break;
        case PartyEvent::Kind::kAway:
            log(name_of(event.id) + "'s connection dropped");

            if (session_.role().from_host(event.id)) {
                host_gone_ms_ = now;
                coop_.hold_for_host(true);
            } else if (session_.role().host()) {
                world_.member_away(event.id);
                named_ms_ = 0;
            }

            break;
        case PartyEvent::Kind::kBack:
            log(name_of(event.id) + " is back");

            if (session_.role().from_host(event.id)) {
                host_gone_ms_ = 0;
                coop_.hold_for_host(false);
            } else if (session_.role().host()) {
                coop_.member_back(event.id);
                world_.member_back(event.id);
                progress_.again();
                shared_.again();
                ledger_members_ = 0;
                named_ms_ = 0;
            }

            break;
        case PartyEvent::Kind::kHostChanged:
            host_changed(event.id, now);
            break;
    }
}

void Migrate::send_ledger(std::uint64_t now) {
    ledger_ms_ = now;
    Ledger l;
    l.epoch = session_.role().epoch();
    l.generation = ledger_.generation;
    l.play = coop_.play();
    l.travel = coop_.travels();
    l.scene = coop_.scenes();
    l.world = world_.ledger();
    const std::size_t members = session_.members().size();

    if (encode_ledger(l) == ledger_said_ && members == ledger_members_) {
        return;
    }

    // Each change has a number of its own; the bytes compared are of the same number.
    ++l.generation;
    ledger_said_ = encode_ledger(l);
    ledger_members_ = members;
    ledger_ = l;
    session_.send_data(kBroadcast, ledger_said_);
}

void Migrate::tick_host(std::uint64_t now) {
    if (!coop_.playing()) {
        return;
    }

    if (world_.hosting() && !coop_.arriving() && now - ledger_ms_ >= kLedgerMs) {
        send_ledger(now);
    }

    if (!session_.can_change_host() || now - named_ms_ < kSuccessorMs) {
        return;
    }

    named_ms_ = now;
    std::vector<Candidate> candidates;

    for (const Member& m : session_.members()) {
        if (m.id != session_.id() && !session_.away(m.id) && !session_.kicking(m.id) && mods_.may_play(m.id)
            && world_.has_world(m.id)) {
            candidates.push_back({m.id, session_.link(m.id)});
        }
    }

    const std::vector<std::int32_t> order = successor_order(std::move(candidates));

    if (order == named_) {
        return;
    }

    named_ = order;
    session_.name_successors(order);
    Writer w;
    w.u8(kind::kSuccessor).u8(static_cast<std::uint8_t>(order.size()));

    for (const std::int32_t id : order) {
        w.i32(id);
    }

    session_.send_data(kBroadcast, w.bytes());
}

void Migrate::tick_stand(std::uint64_t now) {
    if (stand_ms_ == 0) {
        return;
    }

    // Stands until the new host's world is here, a game not in its level has nothing to wait for.
    if (world_.synced() || now - stand_ms_ >= kNewHostMs) {
        stand_ms_ = 0;
        coop_.hold_for_host(false);
    }
}

void Migrate::tick(std::uint64_t now) {
    if (!session_.in_party()) {
        clear();
        was_host_ = false;
        return;
    }

    for (const PartyEvent& event : session_.take_events()) {
        take(event, now);
    }

    was_host_ = session_.is_host();

    // Everyone it asked answered, or it has given them long enough.
    if (gather_ms_ != 0) {
        std::size_t others = 0;

        for (const Member& m : session_.members()) {
            others += m.id != session_.id() && !session_.away(m.id) ? 1 : 0;
        }

        if (answered_.size() >= others || now - gather_ms_ >= kGatherMs) {
            finish_gather(now);
        }
    } else if (session_.is_host() && !session_.dropped()) {
        tick_host(now);
    }

    tick_stand(now);
}

std::string Migrate::banner(std::uint64_t now) const {
    if (session_.dropped()) {
        return kDroppedLine;
    }

    if (host_gone_ms_ != 0) {
        const std::uint64_t waited = now - host_gone_ms_;
        const std::uint64_t left = waited < kHostGraceMs ? (kHostGraceMs - waited + 999) / 1000 : 0;
        const bool mine = !next_.empty() && next_.front() == session_.id();
        return "The host lost its connection: "
               + (mine ? std::string("you take over in ") : std::string("waiting for it ")) + "("
               + std::to_string(left) + ")";
    }

    if (now < new_host_until_) {
        return new_host_ == session_.id() ? std::string(kYouHostLine) : name_of(new_host_) + " is the party's host now";
    }

    return {};
}

}  // namespace mp
