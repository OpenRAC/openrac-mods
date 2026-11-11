// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The client's parts without a network: the WebSocket's frames and handshake key, the ids, and
// the protocol session fed the server's packets. The WebSocket's waiting, on the loopback.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <mbedtls/net_sockets.h>

#include "ap_ids.h"
#include "ap_gold_weapons.h"
#include "session.hpp"
#include "websocket.hpp"

#include <miniz.h>

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

// A server's ping with nothing after it (Archipelago's pings every 20 seconds): receive must come
// back in its time, or what waits to be sent waits for the server's next message.
void test_ping_leaves_receive() {
    mbedtls_net_context listener, peer;
    mbedtls_net_init(&listener);
    mbedtls_net_init(&peer);
    int port = 38290;

    // A free port on the loopback.
    while (port < 38390
           && mbedtls_net_bind(&listener, "127.0.0.1", std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP)
                  != 0) {
        ++port;
    }

    expect(port < 38390, "a loopback port to listen on");

    std::thread server([&] {
        std::string request;
        unsigned char chunk[2048];

        if (mbedtls_net_accept(&listener, &peer, nullptr, 0, nullptr) != 0) {
            return;
        }

        // The request, then the reply with the accept value of its key.
        while (request.find("\r\n\r\n") == std::string::npos) {
            const int got = mbedtls_net_recv(&peer, chunk, sizeof chunk);

            if (got <= 0) {
                return;
            }

            request.append(reinterpret_cast<char*>(chunk), std::size_t(got));
        }

        const std::string name = "Sec-WebSocket-Key: ";
        const std::size_t at = request.find(name) + name.size();
        const std::string key = request.substr(at, request.find("\r\n", at) - at);
        const std::string reply =
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Accept: "
            + ap::ws::accept_for(key) + "\r\n\r\n";
        const unsigned char ping[] = {0x89, 0x00};
        const unsigned char hi[] = {0x81, 0x02, 'h', 'i'};
        mbedtls_net_send(&peer, reinterpret_cast<const unsigned char*>(reply.data()), reply.size());
        mbedtls_net_send(&peer, ping, sizeof ping);

        // Long after the client's wait; a client stuck in its read is only let go by this.
        std::this_thread::sleep_for(std::chrono::seconds(2));
        mbedtls_net_send(&peer, hi, sizeof hi);
    });

    ap::WebSocket socket;
    std::string error, text;
    expect(socket.open("127.0.0.1", port, false, error), "the loopback's handshake");

    const auto start = std::chrono::steady_clock::now();
    int got = socket.receive(text, 50);
    const auto waited = std::chrono::steady_clock::now() - start;
    expect(got == 0, "a ping alone is no message");
    expect(waited < std::chrono::seconds(1), "a ping alone does not hold receive past its time");

    // The message still comes.
    for (int i = 0; i < 100 && got == 0; ++i) {
        got = socket.receive(text, 50);
    }

    expect(got == 1 && text == "hi", "the message after the ping");
    socket.close();
    server.join();
    mbedtls_net_free(&peer);
    mbedtls_net_free(&listener);
}

// A sender's permessage-deflate: raw deflate, a sync flush, its last four bytes off. One
// compressor for every message: the window is kept from one to the next (context takeover).
struct Deflater {
    tdefl_compressor c;
    Deflater() { tdefl_init(&c, nullptr, nullptr, TDEFL_DEFAULT_MAX_PROBES); }

    std::string message(const std::string& text) {
        std::string out(text.size() + 64, '\0');
        std::size_t in_n = text.size(), out_n = out.size();
        tdefl_compress(&c, text.data(), &in_n, &out[0], &out_n, TDEFL_SYNC_FLUSH);
        out.resize(out_n);
        return out.substr(0, out.size() - 4);
    }
};

void test_inflate() {
    Deflater d;
    ap::ws::Inflater inflater;
    const std::string first = R"([{"cmd":"PrintJSON","data":[{"text":"Ratchet found a Swingshot"}]}])";
    const std::string second = R"([{"cmd":"PrintJSON","data":[{"text":"Ratchet found a Hoverboard"}]}])";
    std::string out;
    expect(inflater.inflate(d.message(first), out, 1 << 20) && out == first, "a compressed message");
    out.clear();
    // The second refers back into the first's bytes.
    expect(inflater.inflate(d.message(second), out, 1 << 20) && out == second, "with the window kept");
    out.clear();
    expect(!inflater.inflate(d.message(std::string(4096, 'x')), out, 100), "past the most bytes");
    out.clear();
    expect(!inflater.inflate("\xff\xff\xff", out, 1 << 20), "a broken stream");
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
    for (int i = 0; i < AP_GOLD_WEAPON_COUNT; ++i) {
        int base = k_ap_gold_bases[i];
        expect(ap_gold_weapon_base(AP_BASE + AP_GOLD_WEAPON_OFFSET + base) == base, "gold item id maps to its base weapon");
        expect(ap_gold_case_id(base) == INT64_C(0x52550000) + base, "gold case ids match the APWorld");
    }
    expect(ap_gold_weapon_base(AP_BASE + AP_GOLD_WEAPON_OFFSET + ITEM_RYNO) == -1, "no fabricated Gold RYNO");
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
    expect(connect.at("slot_data") == true, "request YAML slot settings");
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
    std::string preview;
    expect(s.location_hint(1380122625, preview) && preview == "Sword for Link", "purchase preview names the scouted reward and recipient");
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

// A connection with these checked locations, and the server's whole item list.
void connect(
    ap::Session& s, const char* checked, std::vector<std::string>& out, const char* seed = "seed-b"
) {
    s.on_open();
    s.on_message(std::string(R"([{"cmd":"RoomInfo","games":[],"seed_name":")") + seed + "\"}]", out);
    s.on_message(
        std::string(R"([{"cmd":"Connected","team":0,"slot":1,"players":[],"slot_info":{},"missing_locations":[],)")
            + R"("checked_locations":)" + checked + "}]",
        out
    );
}

void test_save() {
    std::vector<std::string> out;

    // A skill point in a room without them is no check and no sign of another seed; in a room
    // with them it is sent. (1,2 are the room's; the skill id is either unknown or missing.)
    {
        const std::int64_t skill = ap_skill_location_id(1, 0);
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        connect(s, "[1,2]", out);
        out.clear();
        s.save_loaded({1, skill}, -1, out);
        expect(!s.foreign_save(), "a skill point's flag in a room without them: not another seed");
        for (const std::string& sent : out) {
            expect(sent.find(std::to_string(skill)) == std::string::npos, "and it is not sent");
        }

        ap::Session on({AP_GAME, "Ratchet", "", "uuid"});
        on.on_open();
        out.clear();
        on.on_message(R"([{"cmd":"RoomInfo","seed_name":"seed-a"}])", out);
        on.on_message(
            "[{\"cmd\":\"Connected\",\"slot\":1,\"team\":0,\"checked_locations\":[1],\"missing_locations\":["
                + std::to_string(skill) + "]}]",
            out
        );
        out.clear();
        on.save_loaded({1, skill}, -1, out);
        expect(commands(out) == std::vector<std::string>{"LocationChecks"}, "a skill point sent");
    }

    // The save's checks are the server's and more: play while offline, sent.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        connect(s, "[1,2]", out);
        out.clear();
        s.save_loaded({1, 2, 3}, -1, out);
        expect(!s.foreign_save() && commands(out) == std::vector<std::string>{"LocationChecks"}, "offline play is sent");
    }

    // Another seed's save: checks the server never saw, and some it saw missing. Not sent, nor
    // what is played on it.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        s.save_loaded({5, 6}, -1, out);
        connect(s, "[1,2]", out);
        out.clear();
        expect(s.foreign_save(), "another seed's save is seen");
        s.check(7, out);
        expect(out.empty(), "nothing of it is sent");

        // A new game: its checks go again.
        s.save_loaded({}, -1, out);
        s.check(8, out);
        expect(!s.foreign_save() && commands(out) == std::vector<std::string>{"LocationChecks"}, "a new game is sent");
    }

    // A reconnection keeps the items until the server sends them again.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        connect(s, "[]", out);
        s.on_message(R"([{"cmd":"ReceivedItems","index":0,"items":[{"item":5,"location":1,"player":1,"flags":0}]}])", out);
        connect(s, "[]", out);
        expect(s.items().size() == 1, "the items stay across a reconnection");
    }

    // The book: a slot that holds another room's game is that room's, whatever its checks; one of
    // this room's is sent, whatever they are; a save written binds its slot.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        s.book().slots[2] = {"seed-a", "Ratchet"};
        s.book().slots[3] = {"seed-b", "Ratchet"};
        connect(s, "[]", out);
        out.clear();
        s.save_loaded({5, 6}, 2, out);
        expect(s.foreign_save() && out.empty(), "a slot of another room's");
        s.save_loaded({1, 2}, 3, out);
        expect(!s.foreign_save() && commands(out) == std::vector<std::string>{"LocationChecks"}, "a slot of this room's");
        s.save_written(2);
        expect(s.book().slots[2].first == "seed-b" && s.book().changed, "a save written binds its slot");
        s.book().changed = false;
        out.clear();
        s.save_loaded({7}, 2, out);
        expect(!s.foreign_save(), "the slot saved into is this room's now");
    }

    // A slot not in the book: the checks decide, then the slot is booked for this room.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        connect(s, "[1,2]", out);
        s.save_loaded({1, 2, 3}, 4, out);
        expect(s.book().slots.count(4) == 1 && s.book().slots[4].first == "seed-b", "booked after its checks");
    }

    // A save written before the room is known is booked on the connection.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        s.save_written(1);
        expect(s.book().slots.empty(), "no room yet");
        connect(s, "[]", out);
        expect(s.book().slots.count(1) == 1, "booked on the connection");
    }

    // A known data package is not asked for again.
    {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        out.clear();
        s.on_message(R"([{"cmd":"DataPackage","data":{"games":{"G":{"checksum":"abc","item_name_to_id":{},"location_name_to_id":{}}}}}])", out);
        s.on_message(R"([{"cmd":"RoomInfo","games":["G"],"datapackage_checksums":{"G":"abc"}}])", out);
        expect(commands(out) == std::vector<std::string>{"Connect"}, "a cached data package");
    }
}

void test_new_game_plus() {
    std::vector<std::string> out;
    for (const json value : {json(true), json(1), json(false), json(0), json("true"), json(2), json(nullptr)}) {
        ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
        expect(s.new_game_plus() == -1, "mode unknown before connecting");
        s.on_open();
        s.on_message(json::array({{{"cmd", "Connected"}, {"slot_data", {{"start_in_new_game_plus", value}}}}}).dump(), out);
        expect(s.new_game_plus() == -1, "mode withheld before save judgment");
        s.save_loaded({}, -1, out);
        const bool enabled = value == json(true) || value == json(1);
        expect(s.new_game_plus() == int(enabled), "only true or one enables New Game+");
        s.on_close();
        expect(s.new_game_plus() == -1, "no mode applied while disconnected");
        s.on_open();
        s.on_message(R"([{"cmd":"Connected"}])", out);
        expect(s.new_game_plus() == 0, "older seeds default off on reconnect");
    }
    ap::Session s({AP_GAME, "Ratchet", "", "uuid"});
    s.book().slots[2] = {"other-seed", "Ratchet"};
    s.save_loaded({}, 2, out);
    s.on_message(R"([{"cmd":"RoomInfo","seed_name":"this-seed"},{"cmd":"Connected","slot_data":{"start_in_new_game_plus":1}}])", out);
    expect(s.foreign_save() && s.new_game_plus() == -1, "foreign saves never receive mode changes");
}

}  // namespace

int main() {
    test_websocket();
    test_ping_leaves_receive();
    test_inflate();
    test_ids();
    test_session();
    test_save();
    test_new_game_plus();
    std::printf(g_failures == 0 ? "unit: ok\n" : "unit: %d failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
