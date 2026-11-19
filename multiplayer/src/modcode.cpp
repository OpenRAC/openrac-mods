// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The launcher's modset.json, read and checked (modcode.hpp).

#include "modcode.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "protocol.hpp"

namespace mp {
namespace {

// More than the launcher's largest set and its long link.
constexpr std::size_t kFileMax = 64 * 1024;
// The most mods the file may list.
constexpr std::size_t kModsMax = 64;
// How deep `skip` follows arrays and objects inside each other.
constexpr int kDepthMax = 8;

// A reader of the part of JSON this file needs: strings, and any other value passed over. It
// borrows the text, which outlives it.
class Scan {
public:
    explicit Scan(const std::string& text) : text_(text) {}

    // Skips white space.
    void space() {
        while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_])) != 0) {
            ++at_;
        }
    }

    // Skips white space, then consumes `c` and returns true when it is next.
    bool take(char c) {
        space();

        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }

        return false;
    }

    // Skips white space and returns whether `c` is next, without consuming it.
    bool peek(char c) {
        space();
        return at_ < text_.size() && text_[at_] == c;
    }

    // Skips white space, then consumes the word `w` and returns true when it is next.
    bool word(const char* w) {
        space();
        const std::size_t n = std::strlen(w);

        if (text_.compare(at_, n, w) == 0) {
            at_ += n;
            return true;
        }

        return false;
    }

    // Reads a quoted string into `out`. False for a raw control character, an escape other than \",
    // \\ or /, or text that ends first.
    bool string(std::string& out) {
        out.clear();

        if (!take('"')) {
            return false;
        }

        // Characters are taken up to the closing quote.
        while (at_ < text_.size()) {
            const char c = text_[at_++];

            // The closing quote ends the string.
            if (c == '"') {
                return true;
            }

            // A raw control character is not allowed inside a JSON string.
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }

            // A plain character is copied; only a backslash starts an escape.
            if (c != '\\') {
                out += c;
                continue;
            }

            // A backslash at the end of the text has nothing to escape.
            if (at_ >= text_.size()) {
                return false;
            }

            const char e = text_[at_++];

            // What the file holds has none of the others; a \u is not one of its characters.
            if (e != '"' && e != '\\' && e != '/') {
                return false;
            }

            out += e;
        }

        return false;
    }

    // Reads the characters of a number into `out`; false when there are none.
    bool number(double& out) {
        space();
        const std::size_t from = at_;

        // A number is a run of the characters JSON numbers use.
        while (at_ < text_.size() && std::strchr("+-.0123456789eE", text_[at_]) != nullptr) {
            ++at_;
        }

        if (from == at_) {
            return false;
        }

        out = std::atof(text_.substr(from, at_ - from).c_str());
        return true;
    }

    // Any value, passed over.
    bool skip(int depth = 0) {
        std::string ignored;
        double value = 0;

        // Nesting deeper than kDepthMax is refused, so a hostile file cannot exhaust the stack.
        if (depth > kDepthMax) {
            return false;
        }

        // A string value.
        if (peek('"')) {
            return string(ignored);
        }

        // An array: each element is passed over in turn.
        if (take('[')) {
            // An empty array.
            if (take(']')) {
                return true;
            }

            // Elements are separated by commas.
            do {
                if (!skip(depth + 1)) {
                    return false;
                }
            } while (take(','));

            return take(']');
        }

        // An object: each key and value is passed over in turn.
        if (take('{')) {
            // An empty object.
            if (take('}')) {
                return true;
            }

            // Members are separated by commas.
            do {
                if (!string(ignored) || !take(':') || !skip(depth + 1)) {
                    return false;
                }
            } while (take(','));

            return take('}');
        }

        return word("null") || word("true") || word("false") || number(value);
    }

    // Returns whether only white space is left.
    bool end() {
        space();
        return at_ == text_.size();
    }

private:
    const std::string& text_;  // The text being read.
    std::size_t at_ = 0;       // The position of the next character.
};

// Reads one mod of the list into `out` as its id and version; other keys are passed over. False
// unless both are there and not empty.
bool parse_mod(Scan& s, std::pair<std::string, std::string>& out) {
    if (!s.take('{')) {
        return false;
    }

    // Each key of the mod's object.
    do {
        std::string key;

        if (!s.string(key) || !s.take(':')) {
            return false;
        }

        // The id is kept.
        if (key == "id" && !s.string(out.first)) {
            return false;
        }

        // The version is kept.
        if (key == "version" && !s.string(out.second)) {
            return false;
        }

        // Any other key is passed over.
        if (key != "id" && key != "version" && !s.skip()) {
            return false;
        }
    } while (s.take(','));

    return s.take('}') && !out.first.empty() && !out.second.empty();
}

// Reads the mods array into `out.mods`. False for a bad entry or more than kModsMax of them.
bool parse_mods(Scan& s, ModSetFile& out) {
    if (!s.take('[')) {
        return false;
    }

    // An empty list is a whole list.
    if (s.take(']')) {
        return true;
    }

    // One object per mod, separated by commas.
    do {
        std::pair<std::string, std::string> mod;

        // A file with too many mods, or a bad one, is refused.
        if (out.mods.size() >= kModsMax || !parse_mod(s, mod)) {
            return false;
        }

        out.mods.push_back(std::move(mod));
    } while (s.take(','));

    return s.take(']');
}

}  // namespace

bool parse_modset(const std::string& text, ModSetFile& out) {
    out = ModSetFile{};

    // A file larger than kFileMax is refused.
    if (text.size() > kFileMax) {
        return false;
    }

    Scan s(text);
    bool format = false;
    bool mods = false;

    if (!s.take('{')) {
        return false;
    }

    // Each key of the file's object.
    do {
        std::string key;

        if (!s.string(key) || !s.take(':')) {
            return false;
        }

        // Only format 1 is read.
        if (key == "format") {
            double value = 0;

            if (!s.number(value) || value != 1.0) {
                return false;
            }

            format = true;
        } else if (key == "code") {
            // The code is a string, or null when the server could not be reached.
            if (!s.word("null") && !s.string(out.code)) {
                return false;
            }
        } else if (key == "link") {
            // The long link of the set.
            if (!s.string(out.link)) {
                return false;
            }
        } else if (key == "mods") {
            // The list of mods the set names.
            if (!parse_mods(s, out)) {
                return false;
            }

            mods = true;
        } else if (!s.skip()) {
            // Any other key is passed over.
            return false;
        }
    } while (s.take(','));

    return s.take('}') && s.end() && format && mods;
}

bool modset_matches(const ModSetFile& file, const ModList& own) {
    std::vector<std::pair<std::string, std::string>> running;

    // Only the mods a party compares are the set's.
    for (const ModInfo& m : own.mods) {
        if (m.counts()) {
            running.emplace_back(m.id, m.version);
        }
    }

    std::vector<std::pair<std::string, std::string>> named = file.mods;
    std::sort(running.begin(), running.end());
    std::sort(named.begin(), named.end());
    return running == named;
}

bool valid_mod_code(const std::string& code) {
    // A code reads like K7M2-9QXR: nine characters, with the dash after the fourth.
    constexpr std::size_t kLength = 9;
    constexpr std::size_t kDash = 4;

    if (code.size() != kLength) {
        return false;
    }

    // Each position is the dash or a letter of the code's alphabet.
    for (std::size_t i = 0; i < kLength; ++i) {
        const bool fits = i == kDash
                              ? code[i] == '-'
                              : code[i] != '\0' && std::strchr(kCodeAlphabet, code[i]) != nullptr;

        if (!fits) {
            return false;
        }
    }

    return true;
}

std::string mod_code_link(const std::string& code) {
    return valid_mod_code(code) ? "openrac://modset/" + code : "";
}

std::string mod_code_with_party(const std::string& code, const std::string& party) {
    return party.empty() ? code : code + "?party=" + party;
}

std::string take_join_request(const std::string& folder, std::int64_t now_seconds) {
    if (folder.empty()) {
        return "";
    }

    const std::string path = folder + "/modset-join.json";
    std::string text(kFileMax + 1, '\0');
    {
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            return "";
        }

        file.read(&text[0], static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(file.gcount()));
    }
    // The file is for one start: it goes whatever it says.
    std::remove(path.c_str());

    // A file larger than kFileMax is refused.
    if (text.size() > kFileMax) {
        return "";
    }

    Scan s(text);
    std::string party;
    double written = -1;
    bool format = false;

    if (!s.take('{')) {
        return "";
    }

    // Each key of the object.
    do {
        std::string key;

        if (!s.string(key) || !s.take(':')) {
            return "";
        }

        // Only format 1 is read.
        if (key == "format") {
            double value = 0;

            if (!s.number(value) || value != 1.0) {
                return "";
            }

            format = true;
        } else if (key == "party") {
            // The party the launcher asked to join.
            if (!s.string(party)) {
                return "";
            }
        } else if (key == "written") {
            // The time the launcher wrote the file, in seconds.
            if (!s.number(written)) {
                return "";
            }
        } else if (!s.skip()) {
            // Any other key is passed over.
            return "";
        }
    } while (s.take(','));

    const bool letters =
        party.size() == kCodeLength && party.find_first_not_of(kCodeAlphabet) == std::string::npos;
    const double age = static_cast<double>(now_seconds) - written;

    // A bad file, a party code of the wrong shape, or an old request says nothing.
    if (!s.take('}') || !s.end() || !format || !letters || written < 0 || age < 0
        || age > static_cast<double>(kJoinRequestSeconds)) {
        return "";
    }

    return party;
}

ModCode read_mod_code(const std::string& folder, const ModList& own) {
    ModCode out;

    if (folder.empty()) {
        return out;
    }

    std::ifstream file(folder + "/modset.json", std::ios::binary);

    if (!file) {
        return out;
    }

    std::string text(kFileMax + 1, '\0');
    file.read(&text[0], static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    ModSetFile set;

    // Only a whole file of the set this game runs is shared.
    if (!parse_modset(text, set) || !modset_matches(set, own)) {
        return out;
    }

    out.shared = true;
    out.code = valid_mod_code(set.code) ? set.code : "";
    return out;
}

}  // namespace mp
