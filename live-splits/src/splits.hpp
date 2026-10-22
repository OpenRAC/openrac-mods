/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * What the live-splits mod does with the game's levels: the commands it sends LiveSplit
 * (splits.cpp). Nothing of the game and nothing of the network is here: the state machine that
 * turns "the player is on planet X" into "split" is a plain class, so it can be tested.
 */
#ifndef LIVE_SPLITS_SPLITS_H
#define LIVE_SPLITS_SPLITS_H

#include <cstdint>
#include <string>
#include <vector>

namespace livesplit {

/* When the timer is split. */
enum class SplitWhen {
    kReached, /* when a planet's gameplay is loaded: the planet is reached */
    kLeft,    /* when the game leaves a planet for the galaxy map */
};

/* Who starts the timer. */
enum class StartWho {
    kMod,    /* the mod resets the timer and starts it at the first planet */
    kRunner, /* the runner starts and resets the timer; the mod only splits */
};

/* What the mod tells LiveSplit. */
enum class Action {
    kReset, /* "reset": the timer back to zero, the attempt begun anew */
    kStart, /* "starttimer": the timer runs */
    kSplit, /* "split": the segment is over, the next one begins */
};

/* The planets, by the level the game plays them in (the port's own names, front_end.c). */
inline constexpr int kPlanetCount = 19;
/* Veldin (the last one), where the last boss is: leaving it ends the run. */
inline constexpr int kLastPlanet = 18;

/* A time as LiveSplit's parser reads it: minutes, seconds and hundredths ("12:34.56"). */
std::string format_time(double seconds);

/* The splits: what the game's levels ask of LiveSplit.
 *
 * The game tells the mod two things: that a planet's gameplay was loaded (the planet is reached)
 * and that the game left a planet (the level is none now). The mod asks for one split per planet
 * the player finishes, and starts the run at the first planet.
 */
class Splits {
public:
    Splits(SplitWhen when, StartWho who) : when_(when), who_(who) {}

    /* The planet's gameplay was loaded: the planet is reached. The commands to send. */
    std::vector<Action> reached(int planet);
    /* The game left the planet for the galaxy map. The commands to send. */
    std::vector<Action> left(int planet);

    /* Whether the run has begun (the first planet was reached). */
    bool started() const { return started_; }

    /* Whether the last event began a run: the first planet of it. */
    bool began() const { return began_; }

private:
    SplitWhen when_;
    StartWho who_;
    bool started_ = false;      /* the run has begun */
    bool began_ = false;        /* the last event began a run */
    int reached_ = -1;          /* the last planet reached */
    int split_at_ = -1;         /* the last planet a split was taken at */
    bool finished_ = false;     /* the run's last split has been sent */
    std::uint32_t visited_ = 0; /* the planets the run has been to, one bit each */
};

}  // namespace livesplit

#endif /* LIVE_SPLITS_SPLITS_H */
