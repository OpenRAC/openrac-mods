// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The client's parts without a network: the WebSocket's frames and handshake key, the ids, and
// the protocol session fed the server's packets.
#include <cstdio>
#include <string>
#include <vector>

#include "ap_ids.h"
#include "session.hpp"
#include "websocket.hpp"

#include <nlohmann/json.hpp>

namespace {

int g_failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

using nlohmann::json;

// The command of each message sent, in order.
std::vector<std::string> commands(const std::vector<std::string>& out) {
    std::vector<std::string> cmds;

    for (const std::string& m : out) {
        for (const json& p : json::parse(m)) {
            cmds.push_back(p.at("cmd").get<std::string>());
        }
    }

    return cmds;
}

void test_websocket() {
    // RFC 6455's own example of the accept value.
    const std::string accept = ap::ws::accept_for("dGhlIHNhbXBsZSBub25jZQ==");
    expect(accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "the handshake's accept value");

    if (accept != "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") {
        std::printf("  got %s\n", accept.c_str());
    }
    const unsigned char hi[] = {'h', 'i'};
    expect(ap::ws::base64(hi, 2) == "aGk=", "base64 of two bytes");

    // A frame of each length class reads back, masked, and a half frame waits.
    for (std::size_t n : {std::size_t(5), std::size_t(300), std::size_t(70000)}) {
        const std::uint8_t mask[4] = {1, 2, 3, 4};
        const std::string text(n, 'x');
        std::string buffer = ap::ws::encode(ap::ws::kText, text, mask);
        std::string half = buffer.substr(0, buffer.size() / 2);
        ap::ws::Opcode op;
        bool fin = false, bad = false;
        std::string payload;
        expect(!ap::ws::decode(half, op, fin, payload, bad) && !bad, "half a frame waits");
        expect(ap::ws::decode(buffer, op, fin, payload, bad), "a whole frame reads");
        expect(op == ap::ws::kText && fin && payload == text && buffer.empty(), "the frame's text");
    }
}

void test_ids() {
    const rando_reward swing = {REWARD_ITEM, ITEM_SWINGSHOT};
    const rando_reward kerwan = {REWARD_PLANET, LEVEL_KERWAN};
    expect(
        ap_reward_of(ap_item_id(swing)).kind == REWARD_ITEM
            && ap_reward_of(ap_item_id(swing)).value == ITEM_SWINGSHOT,
        "an item's id reads back"
    );
    expect(
        ap_reward_of(ap_item_id(kerwan)).kind == REWARD_PLANET
            && ap_reward_of(ap_item_id(kerwan)).value == LEVEL_KERWAN,
        "a planet's id reads back"
    );
    expect(ap_reward_of(12345).kind == REWARD_NONE, "another game's id is none");
}

void test_session() {
    ap::Session s({AP_GAME, "Ratchet", "pw", "uuid"});
    std::vector<std::string> out;
    s.on_open();

    // A check before the connection waits for it.
    s.check(AP_BASE + 0x10001, out);
    expect(out.empty(), "a check waits for the connection");

    // RoomInfo: the data package and the connection asked for.
    s.on_message(R"([{"cmd":"RoomInfo","games":["Ratchet & Clank","Other"]}])", out);
    const auto cmds = commands(out);
    expect(
        cmds.size() == 2 && cmds[0] == "GetDataPackage" && cmds[1] == "Connect",
        "RoomInfo asks for the data package and connects"
    );
    const json connect = json::parse(out[1])[0];
    expect(
        connect.at("name") == "Ratchet" && connect.at("password") == "pw"
            && connect.at("game") == AP_GAME && connect.at("items_handling") == 7,
        "the Connect packet"
    );
    out.clear();

    // The names, then the connection: the scouts and the waiting check are sent.
    s.on_message(
        R"([{"cmd":"DataPackage","data":{"games":{"Other":{"item_name_to_id":{"Sword":7},
        "location_name_to_id":{"Chest":9}},"Ratchet & Clank":{"item_name_to_id":{"Swingshot":1380122636},
        "location_name_to_id":{}}}}}])",
        out
    );
    s.on_message(
        R"([{"cmd":"Connected","team":0,"slot":1,
        "players":[{"team":0,"slot":1,"alias":"Ratchet","name":"Ratchet"},{"team":0,"slot":2,"alias":"Link","name":"Link"}],
        "slot_info":{"1":{"name":"Ratchet","game":"Ratchet & Clank","type":1},"2":{"name":"Link","game":"Other","type":1}},
        "missing_locations":[1380122625,1380122626],"checked_locations":[]},
        {"cmd":"ReceivedItems","index":0,"items":[{"item":1380122636,"location":9,"player":2,"flags":1}]}])",
        out
    );
    const auto sent = commands(out);
    expect(s.connected() && s.synced(), "connected");
    expect(
        sent.size() == 2 && sent[0] == "LocationScouts" && sent[1] == "LocationChecks",
        "scouts and the waiting check go out"
    );
    expect(s.items().size() == 1 && s.items()[0] == 1380122636, "the item received");
    out.clear();

    // An item in step adds; one out of step asks for a sync.
    s.on_message(
        R"([{"cmd":"ReceivedItems","index":1,"items":[{"item":1380122637,"location":1,"player":1,"flags":0}]}])",
        out
    );
    expect(s.items().size() == 2 && out.empty(), "the next item adds");
    s.on_message(R"([{"cmd":"ReceivedItems","index":5,"items":[]}])", out);
    expect(
        commands(out) == std::vector<std::string>{"Sync"} && s.items().size() == 2,
        "out of step: Sync"
    );
    out.clear();

    // A scout's answer; then a line of this slot's, its ids named.
    s.on_message(
        R"([{"cmd":"LocationInfo","locations":[{"item":7,"location":1380122625,"player":2,"flags":1}]}])",
        out
    );
    std::int64_t item = 0;
    bool own = true;
    expect(s.location_item(1380122625, item, own) && item == 7 && !own, "the scouted location");
    s.on_message(
        R"([{"cmd":"PrintJSON","type":"ItemSend","receiving":2,"item":{"item":7,"location":1380122625,"player":1,"flags":1},
        "data":[{"type":"player_id","text":"1"},{"text":" sent "},{"type":"item_id","text":"7","player":2},
        {"text":" to "},{"type":"player_id","text":"2"}]}])",
        out
    );
    std::string line;
    s.next_message(line);  // "connected as"
    expect(s.next_message(line) && line == "You sent Sword to Link", "the line rendered");

    // Another slot's line is not shown.
    s.on_message(
        R"([{"cmd":"PrintJSON","type":"ItemSend","receiving":3,"item":{"item":7,"location":1,"player":4,"flags":1},"data":[{"text":"x"}]}])",
        out
    );
    expect(!s.next_message(line), "another slot's line is not shown");

    // The goal; a refusal stops it.
    s.goal(out);
    expect(commands(out) == std::vector<std::string>{"StatusUpdate"}, "the goal");
    s.on_message(R"([{"cmd":"ConnectionRefused","errors":["InvalidSlot"]}])", out);
    expect(s.refused() && s.refusal() == "InvalidSlot", "the refusal");
}

}  // namespace

int main() {
    test_websocket();
    test_ids();
    test_session();
    std::printf(g_failures == 0 ? "unit: ok\n" : "unit: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
