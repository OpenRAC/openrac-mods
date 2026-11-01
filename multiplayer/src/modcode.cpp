/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The launcher's modset.json, read and checked (modcode.hpp).
 */
#include "modcode.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

#include "protocol.hpp"

namespace mp {
namespace {

/* More than the launcher's largest set and its long link. */
constexpr std::size_t kFileMax = 64 * 1024;
constexpr std::size_t kModsMax = 64;
constexpr int kDepthMax = 8;

/* Just enough of JSON for this file: strings, and any other value passed over. */
class Scan {
public:
    explicit Scan(const std::string& text) : text_(text) {}

    void space() {
        while (at_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[at_])) != 0) {
            ++at_;
        }
    }

    bool take(char c) {
        space();

        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }

        return false;
    }

    bool peek(char c) {
        space();
        return at_ < text_.size() && text_[at_] == c;
    }

    bool word(const char* w) {
        space();
        const std::size_t n = std::strlen(w);

        if (text_.compare(at_, n, w) == 0) {
            at_ += n;
            return true;
        }

        return false;
    }

    bool string(std::string& out) {
        out.clear();

        if (!take('"')) {
            return false;
        }

        while (at_ < text_.size()) {
            const char c = text_[at_++];

            if (c == '"') {
                return true;
            }

            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }

            if (c != '\\') {
                out += c;
                continue;
            }

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

    bool number(double& out) {
        space();
        const std::size_t from = at_;

        while (at_ < text_.size() && std::strchr("+-.0123456789eE", text_[at_]) != nullptr) {
            ++at_;
        }

        if (from == at_) {
            return false;
        }

        out = std::atof(text_.substr(from, at_ - from).c_str());
        return true;
    }

    /* Any value, passed over. */
    bool skip(int depth = 0) {
        std::string ignored;
        double value = 0;

        if (depth > kDepthMax) {
            return false;
        }

        if (peek('"')) {
            return string(ignored);
        }

        if (take('[')) {
            if (take(']')) {
                return true;
            }

            do {
                if (!skip(depth + 1)) {
                    return false;
                }
            } while (take(','));

            return take(']');
        }

        if (take('{')) {
            if (take('}')) {
                return true;
            }

            do {
                if (!string(ignored) || !take(':') || !skip(depth + 1)) {
                    return false;
                }
            } while (take(','));

            return take('}');
        }

        return word("null") || word("true") || word("false") || number(value);
    }

    bool end() {
        space();
        return at_ == text_.size();
    }

private:
    const std::string& text_;
    std::size_t at_ = 0;
};

bool parse_mod(Scan& s, std::pair<std::string, std::string>& out) {
    if (!s.take('{')) {
        return false;
    }

    do {
        std::string key;

        if (!s.string(key) || !s.take(':')) {
            return false;
        }

        if (key == "id" && !s.string(out.first)) {
            return false;
        }

        if (key == "version" && !s.string(out.second)) {
            return false;
        }

        if (key != "id" && key != "version" && !s.skip()) {
            return false;
        }
    } while (s.take(','));

    return s.take('}') && !out.first.empty() && !out.second.empty();
}

bool parse_mods(Scan& s, ModSetFile& out) {
    if (!s.take('[')) {
        return false;
    }

    if (s.take(']')) {
        return true;
    }

    do {
        std::pair<std::string, std::string> mod;

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

    if (text.size() > kFileMax) {
        return false;
    }

    Scan s(text);
    bool format = false;
    bool mods = false;

    if (!s.take('{')) {
        return false;
    }

    do {
        std::string key;

        if (!s.string(key) || !s.take(':')) {
            return false;
        }

        if (key == "format") {
            double value = 0;

            if (!s.number(value) || value != 1.0) {
                return false;
            }

            format = true;
        } else if (key == "code") {
            if (!s.word("null") && !s.string(out.code)) {
                return false;
            }
        } else if (key == "link") {
            if (!s.string(out.link)) {
                return false;
            }
        } else if (key == "mods") {
            if (!parse_mods(s, out)) {
                return false;
            }

            mods = true;
        } else if (!s.skip()) {
            return false;
        }
    } while (s.take(','));

    return s.take('}') && s.end() && format && mods;
}

bool modset_matches(const ModSetFile& file, const ModList& own) {
    std::vector<std::pair<std::string, std::string>> running;

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
    constexpr std::size_t kLength = 9;
    constexpr std::size_t kDash = 4;

    if (code.size() != kLength) {
        return false;
    }

    for (std::size_t i = 0; i < kLength; ++i) {
        const bool fits = i == kDash ? code[i] == '-'
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

    if (!parse_modset(text, set) || !modset_matches(set, own)) {
        return out;
    }

    out.shared = true;
    out.code = valid_mod_code(set.code) ? set.code : "";
    return out;
}

}  // namespace mp
