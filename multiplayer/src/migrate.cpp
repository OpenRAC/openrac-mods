// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// Losing a connection, coming back, and the party going on without its host (migrate.hpp).

#include "migrate.hpp"

#include <algorithm>
#include <utility>

#include "world_util.hpp"

namespace mp {

using namespace wire;

namespace {

// The most keys a set of them holds on the wire: a level has fewer records than this.
constexpr std::size_t kMaxKeys = 0x8000;
// The most spawned mobys a ledger lists.
constexpr std::size_t kMaxSpawned = 0x1000;

// Writes `keys` as a 16-bit count and then each key as 16 bits.
void put_keys(Writer& w, const std::set<int>& keys) {
    u16(w, static_cast<std::uint16_t>(keys.size()));

    for (const int key : keys) {
        u16(w, static_cast<std::uint16_t>(key));
    }
}

// Reads a set written by put_keys. At most kMaxKeys keys are kept; a cut message ends it early.
void take_keys(Reader& r, std::set<int>& keys) {
    const std::size_t count = u16(r);

    // A cut message or a count past the limit stops the reading.
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
    // The level goes as one signed byte, -1 for none.
    w.u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(l.world.level)))
        .u8(l.world.revive ? 1 : 0);
    u16(w, l.world.next_net);
    put_keys(w, l.world.dead);
    put_keys(w, l.world.dug);
    put_keys(w, l.world.spared);
    u16(w, static_cast<std::uint16_t>(l.world.spawned.size()));

    // Each spawned moby goes out as its net id, class, position and yaw.
    for (const LedgerMoby& m : l.world.spawned) {
        u16(w, m.net);
        u16(w, static_cast<std::uint16_t>(static_cast<std::int16_t>(m.oclass)));
        vec(w, m.position);
        f32(w, m.yaw);
    }

    return w.bytes();
}

bool decode_ledger(const Bytes& bytes, Ledger& out) {
    // A header of another kind or version is not a ledger.
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

    // No more mobys than a ledger is made with: a longer list is no ledger of the party's.
    if (count > kMaxSpawned) {
        return false;
    }

    // Each spawned moby is its net id, class, position and yaw.
    for (std::size_t i = 0; i < count && r.ok(); ++i) {
        LedgerMoby m;
        m.net = u16(r);
        m.oclass = static_cast<std::int16_t>(u16(r));
        vec(r, m.position);
        m.yaw = f32(r);
        l.world.spawned.push_back(m);
    }

    // A cut message is no ledger.
    if (!r.ok()) {
        return false;
    }

    out = std::move(l);
    return true;
}

bool ledger_newer(const Ledger& a, const Ledger& b) {
    // The epochs are 16-bit counters that wrap, so the signed difference says which is later.
    return a.epoch != b.epoch
               ? static_cast<std::int16_t>(static_cast<std::uint16_t>(a.epoch - b.epoch)) > 0
               : a.generation > b.generation;
}

std::vector<std::int32_t> successor_order(std::vector<Candidate> candidates) {
    // What a link is worth: a known round trip, and more of it for one that has only TCP.
    const auto cost = [](const Candidate& c) {
        // Milliseconds: 5000 for a round trip not measured yet, 100 more for TCP only.
        return (c.link.ping_ms >= 0 ? c.link.ping_ms : 5000) + (c.link.udp ? 0 : 100);
    };

    std::stable_sort(
        candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
            return cost(a) < cost(b);
        }
    );

    std::vector<std::int32_t> out;

    for (const Candidate& c : candidates) {
        out.push_back(c.id);
    }

    return out;
}

Migrate::Migrate(
    Session& session,
    Game& game,
    Coop& coop,
    World& world,
    Progress& progress,
    Shared& shared,
    PartyMods& mods,
    CoSave& cosave,
    CamScripts& cam,
    std::function<std::uint64_t()> clock
)
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
    // A test passes its own clock; the game uses the wall clock.
    if (!clock_) {
        clock_ = now_ms;
    }
}

void Migrate::handle(std::int32_t sender, const Bytes& bytes) {
    // A message from outside a party is not for it.
    if (bytes.empty() || !session_.in_party()) {
        return;
    }

    const bool from_host = session_.role().from_host(sender);

    // The host names who follows it, in order.
    if (bytes[0] == kind::kSuccessor && from_host) {
        Reader r(bytes.data() + 1, bytes.size() - 1);
        const std::uint8_t count = r.u8();
        std::vector<std::int32_t> next;

        // The ids come as a count and then each id.
        for (std::uint8_t i = 0; i < count && r.ok(); ++i) {
            next.push_back(r.i32());
        }

        // A cut list changes nothing.
        if (r.ok()) {
            next_ = std::move(next);
        }
    } else if (bytes[0] == kind::kLedger) {
        // A ledger comes from the host, or from a member this game asked.
        Ledger heard;

        // A ledger that does not decode is dropped.
        if (!decode_ledger(bytes, heard)) {
            return;
        }

        // The host's word, or an answer to this game's asking from a member it asked, once each.
        // Only ever a newer one stands, so nothing of an old host can put the party back, and none
        // more than a few hosts ahead of this one's, so no member can hold the party's epoch past
        // every later host's.
        const bool answer = !from_host && gather_ms_ != 0 && asked_.count(sender) != 0
                            && answered_.count(sender) == 0;

        // A ledger from anyone else is refused.
        if (!from_host && !answer) {
            return;
        }

        // Each member answers once.
        if (answer) {
            answered_.insert(sender);
        }

        const auto ahead =
            static_cast<std::int16_t>(static_cast<std::uint16_t>(heard.epoch - ledger_.epoch));

        if (ahead <= kLedgerEpochReach && ledger_newer(heard, ledger_)) {
            ledger_ = std::move(heard);
        }
    } else if (bytes[0] == kind::kLedgerAsk && from_host) {
        // A new host asks for the ledgers: the answer goes to it.
        session_.send_data(session_.host(), encode_ledger(ledger_));
    }
}

std::string Migrate::name_of(std::int32_t member) const {
    // The member is found by id.
    for (const Member& m : session_.members()) {
        if (m.id == member) {
            return m.name;
        }
    }

    return {};
}

void Migrate::clear() {
    // The hold is lifted only when this object put it on.
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
    asked_.clear();
    took_over_ = false;
    new_host_until_ = 0;
}

void Migrate::leaving() {
    // A host with someone to follow it says the ledger as it leaves.
    if (session_.is_host() && !named_.empty()) {
        send_ledger(clock_());
    }
}

void Migrate::host_changed(std::int32_t host, std::uint64_t now) {
    coop_.between_hosts(false);
    coop_.forget(old_host_);
    took_over_ = false;
    host_gone_ms_ = 0;
    new_host_ = host;
    new_host_until_ = now + kNewHostLineMs;
    named_.clear();
    next_.clear();
    cosave_.host_changed();
    cam_.reset();
    progress_.again();
    shared_.again();

    // This game is the host the session named.
    if (host == session_.id()) {
        log("the party's host is gone: this game is next");

        // A game that cannot host (a joiner, one in the title, no ledger) declines, and the next is
        // asked.
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
        asked_.clear();

        // Every other member is asked for its ledger.
        for (const Member& m : session_.members()) {
            if (m.id != session_.id()) {
                asked_.insert(m.id);
            }
        }

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

    // A save this game cannot use is left out.
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
    // Each kind of event the session reports.
    switch (event.kind) {
        case PartyEvent::Kind::kDropped:
            log("connection lost: keeping this game's place in the party");
            break;
        case PartyEvent::Kind::kResumed:
            log("back in the party");
            progress_.again();
            shared_.again();

            // The host's own game brings its world back; a member comes in again.
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

            // The host's connection starts the wait; a member's matters to the host alone.
            if (session_.role().from_host(event.id)) {
                host_gone_ms_ = now;
                coop_.hold_for_host(true);
            } else if (session_.role().host()) {
                // A member's connection dropped: the host marks its place in the world.
                world_.member_away(event.id);
                named_ms_ = 0;
            }

            break;
        case PartyEvent::Kind::kBack:
            log(name_of(event.id) + " is back");

            // The host coming back ends the wait; a member coming back is the host's to bring up to
            // date.
            if (session_.role().from_host(event.id)) {
                host_gone_ms_ = 0;
                coop_.hold_for_host(false);
                world_.rejoined();
            } else if (session_.role().host()) {
                // A member is back: the host brings it up to date.
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

    // Nothing changed since the last one, so nothing is sent.
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
    // The ledger is only for a game that is on.
    if (!coop_.playing()) {
        return;
    }

    // The ledger goes while the world is hosted and no one is arriving, at most every kLedgerMs.
    if (world_.hosting() && !coop_.arriving() && now - ledger_ms_ >= kLedgerMs) {
        send_ledger(now);
    }

    // Successors are named only when a change of host is possible, at most every kSuccessorMs.
    if (!session_.can_change_host() || now - named_ms_ < kSuccessorMs) {
        return;
    }

    named_ms_ = now;
    std::vector<Candidate> candidates;

    // Candidates are the other members who are here, not being kicked, may play and hold the world.
    for (const Member& m : session_.members()) {
        if (m.id != session_.id() && !session_.away(m.id) && !session_.kicking(m.id)
            && mods_.may_play(m.id) && world_.has_world(m.id)) {
            candidates.push_back({m.id, session_.link(m.id)});
        }
    }

    const std::vector<std::int32_t> order = successor_order(std::move(candidates));

    // A list that did not change is not sent again.
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
    // Outside a party nothing is kept.
    if (!session_.in_party()) {
        clear();
        was_host_ = false;
        session_.take_events();
        return;
    }

    // The events come in the order they happened.
    for (const PartyEvent& event : session_.take_events()) {
        take(event, now);
    }

    was_host_ = session_.is_host();
    old_host_ = session_.host();

    // Everyone it asked answered, or it has given them long enough.
    // A new host is waiting for the ledgers.
    if (gather_ms_ != 0) {
        std::size_t others = 0;

        for (const Member& m : session_.members()) {
            others += m.id != session_.id() && !session_.away(m.id) ? 1 : 0;
        }

        if (answered_.size() >= others || now - gather_ms_ >= kGatherMs) {
            finish_gather(now);
        }
    } else if (session_.is_host() && !session_.dropped()) {
        // A host that is still in the party names its successors and sends the ledger.
        tick_host(now);
    }

    tick_stand(now);
}

std::string Migrate::banner(std::uint64_t now) const {
    // A dropped connection shows the reconnecting line.
    if (session_.dropped()) {
        return kDroppedLine;
    }

    // With the host away, the line counts the grace down in whole seconds.
    if (host_gone_ms_ != 0) {
        const std::uint64_t waited = now - host_gone_ms_;
        const std::uint64_t left = waited < kHostGraceMs ? (kHostGraceMs - waited + 999) / 1000 : 0;
        const bool mine = !next_.empty() && next_.front() == session_.id();
        return "The host lost its connection: "
               + (mine ? std::string("you take over in ") : std::string("waiting for it ")) + "("
               + std::to_string(left) + ")";
    }

    // The line about a new host stays up for a while.
    if (now < new_host_until_) {
        return new_host_ == session_.id() ? std::string(kYouHostLine)
                                          : name_of(new_host_) + " hosts now";
    }

    return {};
}

}  // namespace mp
