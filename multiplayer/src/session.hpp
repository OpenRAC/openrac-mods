/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The player's session on the OpenRAC server and the party it is in (session.cpp): what the lobby
 * shows, kept from the server's messages, and the player's actions turned into frames. No socket
 * and no thread: it is given a Transport and the messages, on the game's thread, so the tests drive
 * it with a recorder.
 *
 * Ready and Start are the mod's own, carried in DATA (the server only forwards it), one kind byte
 * first:
 *   1 READY  flag u8        a member to the host (the server sends a member's DATA to the host);
 *   2 STATE  count u8, then id i32 + flag u8 per member   the host to everyone: who is ready;
 *   3 START                 the host to everyone: the game starts;
 *  64 LINK   ping i32, udp u8   a member to the host: its round trip to the server (-1 not known
 *                              yet) and whether its game state goes by UDP;
 *  65 LINKS  count u8, then id i32 + ping i32 + udp u8 per member   the host to everyone: each
 *                              member's link (its own among them), each time one comes or the host
 *                              reports its own; for the pause menu's Multiplayer page;
 *  66 BATCH  then per message length u16 + its bytes   several of the game's messages for one
 *                              target in one DATA (below);
 *  67 KICK   reason u8         the host to the member it removes, ahead of the server's KICK (which
 *                              says nothing of why): removed, or banned from this save. A server
 *                              that takes no KICK (UDP's feature bits) has only this word: the
 *                              member leaves by itself.
 * Any other first byte is the game's own (coop.hpp: the game, the poses): handed as it is to the
 * callback set with on_data, and sent with send_data, with a monotonic clock (now_ms) to stamp it.
 *
 * Pacing. The server counts DATA and state by the packet: 60 a second each, over a burst of 120. A
 * DATA over it is dropped and a strike, and enough strikes close the connection; state over it goes
 * without a word. So the game's data goes through here, and nothing is sent the server would drop:
 * send_data queues, in order, and flush (once a tick) sends what is queued, messages for one target
 * side by side going as one DATA (BATCH), as far as a bucket a little below the server's allows; the
 * rest waits, late but whole. State is the next update's to replace, so send_state refuses one over
 * its own bucket (and says so: the world keeps it to send again), and the fallback of state over TCP
 * only takes what events leave.
 *
 * A dropped connection is no end (migrate.hpp). On a server that takes RESUME the session keeps its
 * party, its id and its token while the connection comes back, and asks for its slot before anything
 * else; nothing it sends goes meanwhile. The server's word on anyone's connection, and every change
 * in who hosts, are events for the party's own code to act on (take_events).
 */
#ifndef MULTIPLAYER_SESSION_H
#define MULTIPLAYER_SESSION_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "connection.hpp"
#include "protocol.hpp"
#include "role.hpp"

namespace mp {

/* The DATA kinds of the lobby. */
enum class LobbyKind : std::uint8_t {
    kReady = kind::kReady,
    kState = kind::kState,
    kStart = kind::kStart,
    kLink = kind::kLink,
    kLinks = kind::kLinks,
    kBatch = kind::kBatch,
    kKick = kind::kKick
};

/* Why the host removed a member. */
enum class KickReason : std::uint8_t {
    kRemoved = 0,
    kBanned = 1
};

/* What happened to the party that the session cannot deal with by itself. */
struct PartyEvent {
    enum class Kind {
        kAway,      // a member's connection dropped, its slot waits
        kBack,      // it came back
        kDropped,   // this game's connection went
        kResumed    // this game has its slot again
    };
    Kind kind = Kind::kAway;
    std::int32_t id = 0;
};

/* A token bucket, as the server's RateLimit, on a clock of milliseconds. */
class Pacer {
public:
    Pacer(double capacity, double per_second)
        : capacity_(capacity), per_ms_(per_second / 1000.0), tokens_(capacity) {}

    /* The tokens there are now. */
    double level(std::uint64_t now);
    /* One spent, if there is one. */
    bool take(std::uint64_t now);
    /* One spent regardless (what cannot wait: the lobby's few messages). */
    void spend(std::uint64_t now);

private:
    double capacity_;
    double per_ms_;
    double tokens_;
    std::uint64_t last_ = 0;
};

/* What went through the session, for the verbose log and the tests: each DATA kind's messages and
 * bytes, out and in, by way of the reliable stream or as state; and what the buckets held back. */
struct NetCounts {
    struct Kind {
        std::uint64_t messages = 0;
        std::uint64_t bytes = 0;
    };

    std::array<Kind, 256> events_out{};
    std::array<Kind, 256> state_out{};
    std::array<Kind, 256> in{};
    std::uint64_t data_frames = 0;      // DATA frames sent, the server's count
    std::uint64_t state_datagrams = 0;  // state sent, the server's other count
    std::uint64_t state_held = 0;       // state the bucket refused
    std::uint64_t state_tcp = 0;        // state that also went as DATA
    std::uint64_t state_tcp_held = 0;   // state whose TCP copy events left no room for
    std::uint64_t waited = 0;           // flushes that left events queued for the bucket
    std::size_t queue_most = 0;         // the most messages ever queued
};

/* A member's link to the server (or to the embedded host), as the party last heard it: its round
 * trip in milliseconds (-1 not known) and whether its game state goes by UDP. */
struct Link {
    std::int32_t ping_ms = -1;
    bool udp = false;
};

class Session {
public:
    /* `clock` gives milliseconds of a monotonic clock (now_ms when none is given; a test's own). */
    Session(
        Transport& transport,
        std::string version,
        std::string name,
        std::function<std::uint64_t()> clock = {}
    );

    /* A new connection: hello again, and nothing of a party is left. */
    void connected();
    /* The connection is gone: so is the party. */
    void disconnected();
    /* One message from the server. */
    void handle(const Message& message);

    /* What happened since the last call, in order. */
    std::vector<PartyEvent> take_events();

    /* Whether this game lost its connection and waits to have its slot back. */
    bool dropped() const { return dropped_; }

    /* Whether a member's connection dropped and its slot waits. */
    bool away(std::int32_t member) const { return away_.count(member) != 0; }

    /* The player's actions; each does nothing when it makes no sense now (no hello yet, already in
     * a party, not the host). */
    void create(const std::string& description = "");
    void find();
    /* Asks for the parties with room (LIST; parties() once answered). Out of a party only. */
    void list();
    void join(const std::string& code);
    void leave();
    /* The host takes `member` out of the party. False when it cannot: not the host, or no such
     * member, or the host itself, or already asked. */
    bool kick(std::int32_t member, KickReason reason = KickReason::kRemoved);
    /* Whether the host asked `member` out and it has not gone yet: it gets no game meanwhile. */
    bool kicking(std::int32_t member) const { return kicking_.count(member) != 0; }
    void toggle_ready();
    /* Who may say Ready (party_mods.hpp: a member whose mods match the host's); none set: anyone. A
     * host takes no Ready of one that may not, and it does not count as ready; a member's own
     * toggle does nothing. */
    void set_ready_gate(std::function<bool(std::int32_t)> gate) { ready_gate_ = std::move(gate); }
    /* The host starts the game for everyone, once, and only when all_ready(). Returns whether it
     * did. */
    bool start();

    /* DATA that is not the lobby's: `callback(sender, bytes)` gets each, in a party; send_data sends
     * bytes (their first not a LobbyKind) to `target` (kBroadcast: every other member; a member's
     * DATA always goes to the host). For the gameplay to come; unused by the lobby itself. */
    void on_data(std::function<void(std::int32_t sender, const Bytes& bytes)> callback);
    void send_data(std::int32_t target, const Bytes& bytes);
    /* As send_data, for game state the next update replaces anyway (poses): over UDP where the
     * server or the embedded host offered it, else as DATA; it arrives through on_data the same
     * way. It may arrive twice (by UDP and TCP while UDP is unconfirmed) or not at all, so state
     * must be stamped and idempotent. False when the bucket held it back: nothing went. */
    bool send_state(std::int32_t target, const Bytes& bytes);
    /* Sends what send_data queued, as the bucket allows. Once a tick, and at its end. */
    void flush();

    const NetCounts& counts() const { return counts_; }

    /* Tells the party this game's link (report it every few seconds): a member to the host, the
     * host's own kept and every member's sent to everyone. Nothing out of a party. */
    void report_link(std::int64_t ping_ms, bool udp);

    /* A member's link as last heard: the player's own as last reported; a default Link (ping -1)
     * for one not heard of. */
    Link link(std::int32_t member) const;

    /* Asks the server's clock (TIME); done once after hello, and again whenever the mod wants a
     * fresher estimate. */
    void sync_clock();

    /* The clock estimate from TIME's answers: whether there is one, the last round trip, and the
     * server's clock minus this one, smoothed (each answer moves it by kOffsetSmoothing of the
     * difference), in milliseconds. server_ms() is now in the server's clock. */
    bool has_clock() const { return clock_samples_ > 0; }

    std::int64_t rtt_ms() const { return rtt_ms_; }

    double clock_offset_ms() const { return offset_ms_; }

    std::int64_t server_ms() const;

    /* Whether the server has answered hello, and the session's id. */
    bool greeted() const { return id_ != 0; }

    std::int32_t id() const { return id_; }

    const std::string& name() const { return name_; }

    /* The party: whether there is one, its code, its host, its members (the host first), who of them
     * is ready, and whether the host started the game. */
    bool in_party() const { return !code_.empty(); }

    const std::string& code() const { return code_; }

    std::int32_t host() const { return role_.host_id(); }

    /* What this game is in the party (role.hpp). */
    const Role& role() const { return role_; }

    bool is_host() const { return role_.host(); }

    /* Whether the party is an embedded host's (Direct connect), which has no code to show. */
    bool direct() const { return code_ == kDirectCode; }

    const std::vector<Member>& members() const { return members_; }

    /* The party's description (its host's save), as INFO gave it. */
    const std::string& description() const { return description_; }

    /* The parties LIST last named, and how many answers came (a new count: a new list). */
    const std::vector<Listing>& parties() const { return parties_; }

    std::uint32_t lists() const { return lists_; }

    bool ready(std::int32_t member) const { return ready_.count(member) != 0; }

    /* Whether every member but the host is ready (the host's Start is its own word), as the
     * player last heard: true for a host alone, false out of a party. A member who joins is not
     * ready yet, so it takes all-ready away until it says so. Every release of the mod sends
     * READY (it came with the lobby in 0.1.0), so no member is exempt: an old one says Ready too. */
    bool all_ready() const;

    bool started() const { return started_; }

    /* An action sent and not answered yet (create, join). */
    bool waiting() const { return waiting_; }

    /* The last thing worth a line on the page: a refusal, the host leaving, the start. Empty for
     * none; take_notice() gives it once. */
    std::string take_notice();

private:
    void reset_party();
    /* A message for the wire as a lobby's would go (spends from the bucket, never waits). */
    void send_lobby(std::int32_t target, const Bytes& bytes);
    /* The host: tells everyone who is ready. */
    void send_state();
    /* The host: tells everyone each member's link. */
    void send_links();
    void handle_data(const Message& message);
    void note_in(const Bytes& bytes);
    /* Whether game bytes may go: in a party, and not one of the lobby's kinds. */
    bool game_bytes(const Bytes& bytes) const;
    /* The connection went: the party waits for it, if the server keeps the slot. */
    void drop();
    /* What to do with a word of the server about this session's slot. */
    void take_info(const Message& message);

    Transport& transport_;
    const std::string version_;
    const std::string name_;
    std::int32_t id_ = 0;
    /* What waits for the bucket, in order: each message and its target. */
    struct Queued {
        std::int32_t target = 0;
        Bytes bytes;
    };

    std::deque<Queued> queue_;
    Pacer data_bucket_;
    Pacer state_bucket_;
    NetCounts counts_;
    std::string code_;
    Role role_;
    std::vector<Member> members_;
    std::string description_;
    std::vector<Listing> parties_;
    std::uint32_t lists_ = 0;
    std::set<std::int32_t> ready_;
    std::set<std::int32_t> kicking_;  // the host: asked out, not gone yet
    std::set<std::int32_t> away_;     // members whose slot waits for them
    std::map<std::int32_t, Link> links_;
    bool started_ = false;
    bool waiting_ = false;
    bool server_kicks_ = false;  // the server said it takes KICK
    bool server_resumes_ = false;  // the server said it takes RESUME
    std::int64_t resume_token_ = 0;
    bool dropped_ = false;       // the connection went, the party stands
    bool resume_asked_ = false;  // RESUME went on this connection
    std::uint64_t dropped_ms_ = 0;
    std::vector<PartyEvent> events_;
    KickReason kick_reason_ = KickReason::kRemoved;
    std::string notice_;
    std::function<void(std::int32_t, const Bytes&)> on_data_;
    std::function<bool(std::int32_t)> ready_gate_;
    std::function<std::uint64_t()> clock_;
    int clock_samples_ = 0;
    std::int64_t rtt_ms_ = 0;
    double offset_ms_ = 0.0;
};

/* The buckets, a little below the server's 60 a second over a burst of 120 (its DATA_PER_SECOND,
 * DATA_BURST) so a clock's difference or the lobby's own messages never tip it over. What state may
 * take of the event bucket is what lies over kStateReserve. The most bytes in one BATCH. */
constexpr double kDataRate = 50.0;
constexpr double kDataBurst = 100.0;
constexpr double kStateRate = 52.0;
constexpr double kStateBurst = 60.0;
constexpr double kStateReserve = 50.0;
constexpr std::size_t kBatchBytes = 32000;

/* How often a game in a party reports its link (Session::report_link). */
constexpr std::uint64_t kLinkMs = 2000;

/* How much one TIME answer moves the clock offset: an EWMA of 0.05 (INTERPOLATION.md). */
constexpr double kOffsetSmoothing = 0.05;

/* Milliseconds of a monotonic clock (steady, never set back), to stamp game data with. */
std::uint64_t now_ms();

/* Whether `name` is a multiplayer name as the launcher makes them: 3 to 16 letters, digits and
 * underscores. */
bool valid_name(const std::string& name);

/* The player's name: the mod's `name` setting when it is a valid one, else the launcher's
 * (OPENRAC_PLAYER_NAME) when valid, else empty (the mod then makes one up). */
std::string choose_name(const std::string& setting, const std::string& launcher);

/* What a removed player reads. */
constexpr const char* kKickedNotice = "The host removed you from the party";
constexpr const char* kBannedNotice = "The host banned you from this save";
/* What a player reads when its way back is gone, whatever the reason. */
constexpr const char* kLostSlotNotice = "Your place in the party is gone";

/* The words for a refusal of the server. */
const char* error_text(ErrorReason reason);

}  // namespace mp

#endif /* MULTIPLAYER_SESSION_H */
