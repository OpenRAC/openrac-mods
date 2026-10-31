// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The protocol for one slot (session.hpp).
#include "session.hpp"

#include <cstdlib>

#include <nlohmann/json.hpp>

namespace ap {

namespace {

using nlohmann::json;

// The protocol version the client speaks (Archipelago 0.6): the server only checks it is new
// enough for the world.
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 6;
constexpr int kVersionBuild = 0;

// items_handling: every item, from other worlds, this world's own and the starting inventory.
constexpr int kItemsHandling = 0b111;

// StatusUpdate's CLIENT_GOAL.
constexpr int kClientGoal = 30;

// The lines kept for the game side at most: older ones go first.
constexpr std::size_t kMaxMessages = 32;

// A whole number of a packet, or `fallback` when it is missing or another type.
std::int64_t int_of(const json& j, const char* key, std::int64_t fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<std::int64_t>() : fallback;
}

// A text of a packet, or "" when it is missing or another type.
std::string text_of(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// One packet as the list the server takes.
std::string packet(json p) {
    return json::array({std::move(p)}).dump();
}

}  // namespace

Session::Session(SessionConfig config) : config_(std::move(config)) {}

void Session::on_open() {
    connected_ = false;
    synced_ = false;
    goal_sent_ = false;
    pending_ = found_;
}

void Session::on_close() {
    connected_ = false;
    synced_ = false;
}

void Session::add_message(std::string text) {
    // A full queue drops its oldest line.
    if (messages_.size() >= kMaxMessages) {
        messages_.pop_front();
    }

    messages_.push_back(std::move(text));
}

bool Session::next_message(std::string& out) {
    // Nothing to show.
    if (messages_.empty()) {
        return false;
    }

    out = std::move(messages_.front());
    messages_.pop_front();
    return true;
}

bool Session::location_item(std::int64_t location, std::int64_t& item, bool& own) const {
    const auto it = scouted_.find(location);

    // Not told yet.
    if (it == scouted_.end()) {
        return false;
    }

    item = it->second.first;
    own = it->second.second;
    return true;
}

void Session::check(std::int64_t location, std::vector<std::string>& out) {
    // Known already: sent before (or waiting to be).
    if (!found_.insert(location).second) {
        return;
    }

    pending_.insert(location);
    flush(out);
}

void Session::goal(std::vector<std::string>& out) {
    goal_ = true;
    flush(out);
}

// What waits to be sent, when connected: the checks, then the goal.
void Session::flush(std::vector<std::string>& out) {
    // Not in the room: they wait.
    if (!connected_) {
        return;
    }

    // Every pending check in one packet.
    if (!pending_.empty()) {
        out.push_back(packet(
            {{"cmd", "LocationChecks"},
             {"locations", std::vector<std::int64_t>(pending_.begin(), pending_.end())}}
        ));
        pending_.clear();
    }

    // The goal, once per connection.
    if (goal_ && !goal_sent_) {
        out.push_back(packet({{"cmd", "StatusUpdate"}, {"status", kClientGoal}}));
        goal_sent_ = true;
    }
}

std::string Session::player_name(int slot) const {
    const auto it = players_.find(slot);
    return it != players_.end() ? it->second.alias : "player " + std::to_string(slot);
}

std::string Session::item_name(std::int64_t item, int owner) const {
    const auto player = players_.find(owner);
    const auto game =
        player != players_.end() ? item_names_.find(player->second.game) : item_names_.end();

    // The owner's game names it, when its data package came.
    if (game != item_names_.end()) {
        const auto it = game->second.find(item);

        if (it != game->second.end()) {
            return it->second;
        }
    }

    return "item " + std::to_string(item);
}

std::string Session::location_name(std::int64_t location, int owner) const {
    const auto player = players_.find(owner);
    const auto game = player != players_.end() ? location_names_.find(player->second.game)
                                               : location_names_.end();

    // The owner's game names it, when its data package came.
    if (game != location_names_.end()) {
        const auto it = game->second.find(location);

        if (it != game->second.end()) {
            return it->second;
        }
    }

    return "location " + std::to_string(location);
}

// A PrintJSON's text parts as one line, the ids named.
std::string Session::render(const json& data) const {
    std::string line;

    // Each part, by its type.
    for (const json& part : data) {
        // A part that is not an object has nothing to say.
        if (!part.is_object()) {
            continue;
        }

        const std::string type = text_of(part, "type");
        const std::string text = text_of(part, "text");
        const int owner = int(int_of(part, "player", slot_));
        char* end = nullptr;
        const long long id = std::strtoll(text.c_str(), &end, 10);
        const bool numeric = !text.empty() && end != nullptr && *end == 0;

        // The ids become names; the rest is its text.
        if (type == "player_id" && numeric) {
            line += int(id) == slot_ ? "you" : player_name(int(id));
        } else if (type == "item_id" && numeric) {
            line += item_name(id, owner);
        } else if (type == "location_id" && numeric) {
            line += location_name(id, owner);
        } else {
            line += text;
        }
    }

    // The server's line for an item found for oneself: "you found their X" says "your".
    static const std::string kSelf = "you found their ";

    if (line.rfind(kSelf, 0) == 0) {
        line = "You found your " + line.substr(kSelf.size());
    } else if (!line.empty() && line[0] >= 'a' && line[0] <= 'z') {
        line[0] = char(line[0] - 'a' + 'A');
    }

    return line;
}

void Session::on_message(const std::string& text, std::vector<std::string>& out) {
    const json packets = json::parse(text, nullptr, false);

    // Not a list of packets: nothing the protocol knows.
    if (!packets.is_array()) {
        return;
    }

    // Each packet in its order.
    for (const json& p : packets) {
        // A packet is an object with a command.
        if (p.is_object()) {
            on_packet(p, out);
        }
    }
}

void Session::on_packet(const json& p, std::vector<std::string>& out) {
    const std::string cmd = text_of(p, "cmd");

    if (cmd == "RoomInfo") {
        // The names of every game in the room, then the slot's connection.
        json games = json::array();
        const auto list = p.find("games");

        if (list != p.end() && list->is_array()) {
            games = *list;
        }

        out.push_back(packet({{"cmd", "GetDataPackage"}, {"games", games}}));
        out.push_back(packet(
            {{"cmd", "Connect"},
             {"password", config_.password},
             {"game", config_.game},
             {"name", config_.slot},
             {"uuid", config_.uuid},
             {"version",
              {{"major", kVersionMajor},
               {"minor", kVersionMinor},
               {"build", kVersionBuild},
               {"class", "Version"}}},
             {"items_handling", kItemsHandling},
             {"tags", json::array()},
             {"slot_data", false}}
        ));
    } else if (cmd == "Connected") {
        connected_ = true;
        refused_ = false;
        synced_ = true;
        slot_ = int(int_of(p, "slot", -1));
        team_ = int(int_of(p, "team", 0));
        // The server sends every item again (ReceivedItems index 0) right after: start the list.
        items_.clear();
        ++generation_;
        players_.clear();
        const auto players = p.find("players");

        // Each player's name to show.
        if (players != p.end() && players->is_array()) {
            for (const json& player : *players) {
                if (player.is_object() && int_of(player, "team", team_) == team_) {
                    players_[int(int_of(player, "slot", -1))].alias = text_of(player, "alias");
                }
            }
        }

        const auto slots = p.find("slot_info");

        // Each slot's game, to name its items and locations.
        if (slots != p.end() && slots->is_object()) {
            for (const auto& [key, info] : slots->items()) {
                if (info.is_object()) {
                    players_[std::atoi(key.c_str())].game = text_of(info, "game");
                }
            }
        }

        // What every location of this slot holds, so the game can show it.
        std::vector<std::int64_t> all;

        for (const char* key : {"missing_locations", "checked_locations"}) {
            const auto it = p.find(key);

            if (it != p.end() && it->is_array()) {
                for (const json& id : *it) {
                    if (id.is_number_integer()) {
                        all.push_back(id.get<std::int64_t>());
                    }
                }
            }
        }

        if (!all.empty()) {
            out.push_back(
                packet({{"cmd", "LocationScouts"}, {"locations", all}, {"create_as_hint", 0}})
            );
        }

        add_message("Archipelago: connected as " + config_.slot);
        flush(out);
    } else if (cmd == "ConnectionRefused") {
        refused_ = true;
        connected_ = false;
        refusal_.clear();
        const auto errors = p.find("errors");

        // The server's reasons, as it names them (InvalidSlot, InvalidPassword, ...).
        if (errors != p.end() && errors->is_array()) {
            for (const json& e : *errors) {
                if (e.is_string()) {
                    refusal_ += (refusal_.empty() ? "" : ", ") + e.get<std::string>();
                }
            }
        }

        add_message(
            "Archipelago refused the connection: " + (refusal_.empty() ? "no reason" : refusal_)
        );
    } else if (cmd == "ReceivedItems") {
        const std::int64_t index = int_of(p, "index", -1);
        const auto items = p.find("items");

        // A list out of step with this one: start again from the server's whole list.
        if (index != 0 && index != std::int64_t(items_.size())) {
            out.push_back(packet({{"cmd", "Sync"}}));
            return;
        }

        // The whole list starts again.
        if (index == 0) {
            items_.clear();
        }

        // Each item's id, in order.
        if (items != p.end() && items->is_array()) {
            for (const json& item : *items) {
                if (item.is_object()) {
                    items_.push_back(int_of(item, "item", -1));
                }
            }
        }

        ++generation_;
    } else if (cmd == "LocationInfo") {
        const auto locations = p.find("locations");

        // What each scouted location holds, and for whom.
        if (locations != p.end() && locations->is_array()) {
            for (const json& item : *locations) {
                if (item.is_object()) {
                    scouted_[int_of(item, "location", -1)] =
                        {int_of(item, "item", -1), int_of(item, "player", -1) == slot_};
                }
            }
        }
    } else if (cmd == "DataPackage") {
        const auto data = p.find("data");
        const json* games = nullptr;

        // data.games, when it is there.
        if (data != p.end() && data->is_object() && data->contains("games")) {
            games = &(*data)["games"];
        }

        // Each game's ids to names.
        if (games != nullptr && games->is_object()) {
            for (const auto& [game, info] : games->items()) {
                if (!info.is_object()) {
                    continue;
                }

                for (const auto& [names, into] :
                     {std::pair<const char*, decltype(&item_names_)>{
                          "item_name_to_id", &item_names_
                      },
                      {"location_name_to_id", &location_names_}}) {
                    const auto table = info.find(names);

                    if (table != info.end() && table->is_object()) {
                        for (const auto& [name, id] : table->items()) {
                            if (id.is_number_integer()) {
                                (*into)[game][id.get<std::int64_t>()] = name;
                            }
                        }
                    }
                }
            }
        }
    } else if (cmd == "PrintJSON") {
        const std::string type = text_of(p, "type");
        const auto data = p.find("data");

        // Only the lines that are this slot's: an item it sent or got, a hint for it, a countdown.
        if (data == p.end() || !data->is_array()) {
            return;
        }

        bool mine = type == "Countdown";

        if (type == "ItemSend" || type == "ItemCheat" || type == "Hint") {
            const auto item = p.find("item");
            const int finder =
                item != p.end() && item->is_object() ? int(int_of(*item, "player", -1)) : -1;
            mine = int_of(p, "receiving", -1) == slot_ || finder == slot_;
        }

        if (mine) {
            add_message(render(*data));
        }
    }
}

}  // namespace ap
