/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * This game's part in its party: the one thing that says who runs the world, so that a change of
 * host (migrate.hpp) changes one value. The Session keeps it from the server's word; the Coop and the
 * World ask it, never a flag of their own.
 */
#ifndef MULTIPLAYER_ROLE_H
#define MULTIPLAYER_ROLE_H

#include <cstdint>

namespace mp {

class Role {
public:
    /* Out of a party it is neither. */
    bool host() const { return party_ && host_ == self_; }

    bool member() const { return party_ && host_ != self_; }

    /* Whether a message from `sender` is the host's word to this game. */
    bool from_host(std::int32_t sender) const { return member() && sender == host_; }

    /* The party's host and this game's id. */
    std::int32_t host_id() const { return host_; }

    /* Which host of the party this is: 0 the first, one more at each change. */
    std::uint16_t epoch() const { return epoch_; }

    void set(bool party, std::int32_t self, std::int32_t host, std::uint16_t epoch = 0) {
        party_ = party;
        self_ = self;
        host_ = host;
        epoch_ = epoch;
    }

private:
    bool party_ = false;
    std::int32_t self_ = 0;
    std::int32_t host_ = 0;
    std::uint16_t epoch_ = 0;
};

}  // namespace mp

#endif /* MULTIPLAYER_ROLE_H */
