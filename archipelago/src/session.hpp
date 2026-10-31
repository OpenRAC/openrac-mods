// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The Archipelago protocol for one slot, apart from any socket: it takes the server's messages
// (JSON arrays of packets) and gives the messages to send back, and keeps what the game side asks
// for (the items received, the lines to show, what each location holds). The client
// (ap_client.cpp) runs it under a lock on its thread; the tests feed it the server's packets.
//
// The packets are the network protocol's (Archipelago's docs/network protocol.md): RoomInfo,
// Connected, ConnectionRefused, ReceivedItems, LocationInfo, DataPackage, PrintJSON; and from
// here Connect, GetDataPackage, LocationScouts, LocationChecks, StatusUpdate, Sync.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace ap {

struct SessionConfig {
    std::string game;
    std::string slot;
    std::string password;
    std::string uuid;
};

class Session {
public:
    explicit Session(SessionConfig config);

    // A new connection is open: what the last one knew of the room is forgotten (the items and
    // what was learnt of the locations stay until the server says otherwise).
    void on_open();

    // The connection is gone.
    void on_close();

    // One message from the server; the replies go on out.
    void on_message(const std::string& text, std::vector<std::string>& out);

    // A location found, and the goal reached: sent now (onto out) when connected, else kept for
    // the connection.
    void check(std::int64_t location, std::vector<std::string>& out);
    void goal(std::vector<std::string>& out);

    bool connected() const { return connected_; }

    bool refused() const { return refused_; }

    bool synced() const { return synced_; }

    const std::string& refusal() const { return refusal_; }

    const std::vector<std::int64_t>& items() const { return items_; }

    std::uint32_t generation() const { return generation_; }

    // The next line to show, oldest first; false when there is none.
    bool next_message(std::string& out);
    void add_message(std::string text);

    // What a location of this slot holds: true, its item and whether it is this slot's.
    bool location_item(std::int64_t location, std::int64_t& item, bool& own) const;

    int slot() const { return slot_; }

private:
    struct Player {
        std::string alias;
        std::string game;
    };

    void on_packet(const nlohmann::json& packet, std::vector<std::string>& out);
    void flush(std::vector<std::string>& out);
    std::string player_name(int slot) const;
    std::string item_name(std::int64_t item, int owner) const;
    std::string location_name(std::int64_t location, int owner) const;
    std::string render(const nlohmann::json& data) const;

    SessionConfig config_;
    bool connected_ = false;
    bool refused_ = false;
    bool synced_ = false;
    bool goal_ = false;
    bool goal_sent_ = false;
    std::string refusal_;
    int slot_ = -1;
    int team_ = 0;

    std::vector<std::int64_t> items_;
    std::uint32_t generation_ = 0;
    std::set<std::int64_t> pending_;  // checks not yet sent on this connection
    std::set<std::int64_t> found_;    // every check made, sent again on each connection
    std::map<int, Player> players_;
    std::map<std::string, std::map<std::int64_t, std::string>> item_names_;      // by game
    std::map<std::string, std::map<std::int64_t, std::string>> location_names_;  // by game
    std::map<std::int64_t, std::pair<std::int64_t, bool>> scouted_;
    std::deque<std::string> messages_;
};

}  // namespace ap
