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
 *                              reports its own; for the pause menu's Multiplayer page.
 * Any other first byte is the game's own (coop.hpp: the game, the poses): handed as it is to the
 * callback set with on_data, and sent with send_data, with a monotonic clock (now_ms) to stamp it.
 */
#ifndef MULTIPLAYER_SESSION_H
#define MULTIPLAYER_SESSION_H

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "connection.hpp"
#include "protocol.hpp"

namespace mp {

/* The DATA kinds of the lobby. */
enum class LobbyKind : std::uint8_t {
    kReady = 1,
    kState = 2,
    kStart = 3,
    kLink = 64,
    kLinks = 65
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

    /* The player's actions; each does nothing when it makes no sense now (no hello yet, already in
     * a party, not the host). */
    void create(const std::string& description = "");
    void find();
    /* Asks for the parties with room (LIST; parties() once answered). Out of a party only. */
    void list();
    void join(const std::string& code);
    void leave();
    void toggle_ready();
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
     * must be stamped and idempotent. */
    void send_state(std::int32_t target, const Bytes& bytes);

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

    std::int32_t host() const { return host_; }

    bool is_host() const { return in_party() && host_ == id_; }

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
    /* The host: tells everyone who is ready. */
    void send_state();
    /* The host: tells everyone each member's link. */
    void send_links();
    void handle_data(const Message& message);
    /* Whether game bytes may go: in a party, and not one of the lobby's kinds. */
    bool game_bytes(const Bytes& bytes) const;

    Transport& transport_;
    const std::string version_;
    const std::string name_;
    std::int32_t id_ = 0;
    std::string code_;
    std::int32_t host_ = 0;
    std::vector<Member> members_;
    std::string description_;
    std::vector<Listing> parties_;
    std::uint32_t lists_ = 0;
    std::set<std::int32_t> ready_;
    std::map<std::int32_t, Link> links_;
    bool started_ = false;
    bool waiting_ = false;
    std::string notice_;
    std::function<void(std::int32_t, const Bytes&)> on_data_;
    std::function<std::uint64_t()> clock_;
    int clock_samples_ = 0;
    std::int64_t rtt_ms_ = 0;
    double offset_ms_ = 0.0;
};

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

/* The words for a refusal of the server. */
const char* error_text(ErrorReason reason);

}  // namespace mp

#endif /* MULTIPLAYER_SESSION_H */
