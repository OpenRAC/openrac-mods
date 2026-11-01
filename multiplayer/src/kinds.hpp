/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The wire registry: every kind of DATA (the first payload byte the server forwards as it is), who
 * owns it, and the router that hands a received DATA to its owner.
 *
 * One table is the truth. A kind is a number in a range one owner holds; no two kinds share a value
 * (static_asserts below, and tests/kinds_test.cpp over the whole table). A feature that adds a
 * message takes its numbers from its own range only; one that runs out asks the lead to widen the
 * range here. The enums of each file (session.hpp's LobbyKind, coop.hpp's GameKind, world.hpp's
 * WorldKind, party_mods.hpp's ModsKind, progress.hpp's kProgressKind) take their values from the
 * `kind::` names, so a number is written once.
 *
 * Reserved ranges (free until their package lands):
 *    36-39 shared progress, 44-47 late join, 48-51 shots,
 *    52-55 camera (54 and 55 free), 56-59 tether, 60-63 the party's rules, 68-70 lobby (read by the Session, as
 *    64-67), 71-72 the world's second range; free: 0, 73-127 (and 128-255, which the wire does not
 *    use). Late join uses 44 to 46 (47 is free).
 *
 * Routing: Router::dispatch looks the owner of the first byte up in the table and calls what that
 * owner registered; a kind nobody registered goes to the fallback (the co-op), as a DATA the
 * registry does not know always did.
 */
#ifndef MULTIPLAYER_KINDS_H
#define MULTIPLAYER_KINDS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace mp {

/* Who a kind belongs to. */
enum class Owner : std::uint8_t {
    kLobby,
    kCoop,
    kWorld,
    kProgress,
    kMods,
    kSave,
    kShare,
    kMigrate,
    kJoin,
    kShots,
    kCamera,
    kTether,
    kPartyRules,
    kCount
};

/* The kinds written once; the enums of the files that own them use these. */
namespace kind {
// Lobby (session.hpp)
constexpr std::uint8_t kReady = 1;
constexpr std::uint8_t kState = 2;
constexpr std::uint8_t kStart = 3;
constexpr std::uint8_t kLink = 64;
constexpr std::uint8_t kLinks = 65;
constexpr std::uint8_t kBatch = 66;
constexpr std::uint8_t kKick = 67;
// Co-op (coop.hpp, play.hpp)
constexpr std::uint8_t kPlay = 4;
constexpr std::uint8_t kChunk = 5;
constexpr std::uint8_t kLevel = 6;
constexpr std::uint8_t kPose = 7;
constexpr std::uint8_t kWant = 8;
constexpr std::uint8_t kSkip = 18;
constexpr std::uint8_t kShot = 19;
constexpr std::uint8_t kShotX = 48;
constexpr std::uint8_t kScene = 20;
constexpr std::uint8_t kSceneState = 21;
constexpr std::uint8_t kPoses = 25;
// The party's saves (cosave.hpp)
constexpr std::uint8_t kCoSave = 32;
constexpr std::uint8_t kCoSaveAck = 33;
constexpr std::uint8_t kSaveNow = 34;
constexpr std::uint8_t kCoParty = 35;
// A change of host (migrate.hpp)
constexpr std::uint8_t kSuccessor = 40;
constexpr std::uint8_t kLedger = 41;
constexpr std::uint8_t kLedgerAsk = 42;
constexpr std::uint8_t kEpoch = 43;
// Late join and waits (coop.hpp)
constexpr std::uint8_t kPlace = 44;
constexpr std::uint8_t kPlaceAsk = 45;
constexpr std::uint8_t kWaitVote = 46;
// Camera scripts (camscripts.hpp)
constexpr std::uint8_t kCamScript = 52;
constexpr std::uint8_t kCamState = 53;
// World (world.hpp)
constexpr std::uint8_t kWorld = 9;
constexpr std::uint8_t kKilled = 10;
constexpr std::uint8_t kSpawn = 11;
constexpr std::uint8_t kDespawn = 12;
constexpr std::uint8_t kHit = 13;
constexpr std::uint8_t kDamage = 14;
constexpr std::uint8_t kBolts = 15;
constexpr std::uint8_t kDead = 16;
constexpr std::uint8_t kBroke = 17;
constexpr std::uint8_t kClaim = 22;
constexpr std::uint8_t kTaken = 24;
constexpr std::uint8_t kHello = 26;
constexpr std::uint8_t kReset = 27;
constexpr std::uint8_t kSynced = 28;
constexpr std::uint8_t kRevive = 71;
constexpr std::uint8_t kRestore = 72;
// Progress (progress.hpp)
constexpr std::uint8_t kProgress = 23;
// Mods (party_mods.hpp)
constexpr std::uint8_t kMods = 29;
constexpr std::uint8_t kFits = 30;
constexpr std::uint8_t kRules = 31;
// Shared progress (shared.hpp)
constexpr std::uint8_t kShared = 36;
constexpr std::uint8_t kMission = 37;
constexpr std::uint8_t kSharedAsk = 38;
// Grabs (world.hpp; read by the World)
constexpr std::uint8_t kHold = 56;
constexpr std::uint8_t kRelease = 57;
}  // namespace kind

/* One kind in use. */
struct KindInfo {
    std::uint8_t value;
    const char* name;
    Owner owner;
};

/* One range an owner holds (inclusive); the kinds in use lie inside their owner's. */
struct KindRange {
    std::uint8_t first;
    std::uint8_t last;
    Owner owner;
};

inline constexpr KindInfo kKinds[] = {
    {kind::kReady, "READY", Owner::kLobby},
    {kind::kState, "STATE", Owner::kLobby},
    {kind::kStart, "START", Owner::kLobby},
    {kind::kPlay, "PLAY", Owner::kCoop},
    {kind::kChunk, "CHUNK", Owner::kCoop},
    {kind::kLevel, "LEVEL", Owner::kCoop},
    {kind::kPose, "POSE", Owner::kCoop},
    {kind::kWant, "WANT", Owner::kCoop},
    {kind::kWorld, "WORLD", Owner::kWorld},
    {kind::kKilled, "KILLED", Owner::kWorld},
    {kind::kSpawn, "SPAWN", Owner::kWorld},
    {kind::kDespawn, "DESPAWN", Owner::kWorld},
    {kind::kHit, "HIT", Owner::kWorld},
    {kind::kDamage, "DAMAGE", Owner::kWorld},
    {kind::kBolts, "BOLTS", Owner::kWorld},
    {kind::kDead, "DEAD", Owner::kWorld},
    {kind::kBroke, "BROKE", Owner::kWorld},
    {kind::kSkip, "SKIP", Owner::kCoop},
    {kind::kShot, "SHOT", Owner::kCoop},
    {kind::kShotX, "SHOTX", Owner::kShots},
    {kind::kScene, "SCENE", Owner::kCoop},
    {kind::kSceneState, "SCENE_STATE", Owner::kCoop},
    {kind::kClaim, "CLAIM", Owner::kWorld},
    {kind::kProgress, "PROGRESS", Owner::kProgress},
    {kind::kTaken, "TAKEN", Owner::kWorld},
    {kind::kPoses, "POSES", Owner::kCoop},
    {kind::kPlace, "PLACE", Owner::kJoin},
    {kind::kPlaceAsk, "PLACE_ASK", Owner::kJoin},
    {kind::kWaitVote, "WAIT_VOTE", Owner::kJoin},
    {kind::kCamScript, "CAMSCRIPT", Owner::kCamera},
    {kind::kCamState, "CAMSTATE", Owner::kCamera},
    {kind::kHello, "HELLO", Owner::kWorld},
    {kind::kReset, "RESET", Owner::kWorld},
    {kind::kSynced, "SYNCED", Owner::kWorld},
    {kind::kRevive, "REVIVE", Owner::kWorld},
    {kind::kRestore, "RESTORE", Owner::kWorld},
    {kind::kMods, "MODS", Owner::kMods},
    {kind::kFits, "FITS", Owner::kMods},
    {kind::kRules, "RULES", Owner::kMods},
    {kind::kCoSave, "COSAVE", Owner::kSave},
    {kind::kCoSaveAck, "COSAVE_ACK", Owner::kSave},
    {kind::kSaveNow, "SAVE_NOW", Owner::kSave},
    {kind::kCoParty, "COPARTY", Owner::kSave},
    {kind::kSuccessor, "SUCCESSOR", Owner::kMigrate},
    {kind::kLedger, "LEDGER", Owner::kMigrate},
    {kind::kLedgerAsk, "LEDGER_ASK", Owner::kMigrate},
    {kind::kEpoch, "EPOCH", Owner::kMigrate},
    {kind::kShared, "SHARED", Owner::kShare},
    {kind::kMission, "MISSION", Owner::kShare},
    {kind::kSharedAsk, "SHARED_ASK", Owner::kShare},
    {kind::kHold, "HOLD", Owner::kTether},
    {kind::kRelease, "RELEASE", Owner::kTether},
    {kind::kLink, "LINK", Owner::kLobby},
    {kind::kLinks, "LINKS", Owner::kLobby},
    {kind::kBatch, "BATCH", Owner::kLobby},
    {kind::kKick, "KICK", Owner::kLobby},
};

/* Kinds 4 to 31 are shared out one by one (the owners interleave there); the rest are ranges.
 * The ranges listed are the reserved ones and the lobby's 64 to 70. */
inline constexpr KindRange kReservedRanges[] = {
    {36, 39, Owner::kShare},
    {40, 43, Owner::kMigrate},
    {44, 47, Owner::kJoin},
    {48, 51, Owner::kShots},
    {52, 55, Owner::kCamera},
    {56, 59, Owner::kTether},
    {60, 63, Owner::kPartyRules},
    {64, 70, Owner::kLobby},
    {71, 72, Owner::kWorld},
};

/* A kind's owner; kCount for a number nobody holds (0 and 71 on). */
constexpr Owner owner_of(std::uint8_t value) {
    for (const KindInfo& k : kKinds) {
        if (k.value == value) {
            return k.owner;
        }
    }

    for (const KindRange& r : kReservedRanges) {
        if (value >= r.first && value <= r.last) {
            return r.owner;
        }
    }

    return Owner::kCount;
}

/* The kinds in use, one owner each: a duplicate would make owner_of ambiguous. */
constexpr bool kinds_unique() {
    constexpr std::size_t n = sizeof(kKinds) / sizeof(kKinds[0]);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (kKinds[i].value == kKinds[j].value) {
                return false;
            }
        }
    }

    return true;
}

static_assert(kinds_unique(), "two wire kinds share a value (kinds.hpp)");

/* Where a received DATA goes: the handler each owner registered, else the fallback. */
class Router {
public:
    using Handler = std::function<void(std::int32_t sender, const std::vector<std::uint8_t>& bytes)>;

    Router() : handlers_(static_cast<std::size_t>(Owner::kCount)) {}

    /* What a kind nobody registered goes to. */
    void fallback(Handler handler) { fallback_ = std::move(handler); }

    /* The owner's handler; it gets every kind the table gives the owner. */
    void route(Owner owner, Handler handler) { handlers_[static_cast<std::size_t>(owner)] = std::move(handler); }

    /* Which owner a DATA goes to, or kCount for the fallback. */
    Owner target(const std::vector<std::uint8_t>& bytes) const {
        if (bytes.empty()) {
            return Owner::kCount;
        }

        const Owner owner = owner_of(bytes[0]);
        return owner != Owner::kCount && handlers_[static_cast<std::size_t>(owner)] ? owner : Owner::kCount;
    }

    void dispatch(std::int32_t sender, const std::vector<std::uint8_t>& bytes) const {
        const Owner owner = target(bytes);

        if (owner != Owner::kCount) {
            handlers_[static_cast<std::size_t>(owner)](sender, bytes);
        } else if (fallback_) {
            fallback_(sender, bytes);
        }
    }

private:
    std::vector<Handler> handlers_;
    Handler fallback_;
};

}  // namespace mp

#endif
