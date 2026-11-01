// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// The party on a seeded network, over many seeds: every mix of 0, 1, 5 and 15 % loss each way, 30,
// 100 and 250 ms, with reordering and repeats, for 2 to 4 players, and what the world did: whether
// it came right on every member, how long after the play stopped, the longest a moving moby went
// unheard, what went out by kind. A loss that bites one run in thirty passes a run of each seed by
// luck, so this is the test of "the world survives a bad link"; `--csv` gives a line a run, for
// sweeps split over machines (`--shard i/n`).
//
//   multiplayer_sweep_test [--seeds N] [--base B] [--plans all|walk|sparse|fire] [--settle ms]
//                          [--shard i/n] [--csv]
#include "test_support.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>

int g_failures = 0;

namespace {

using namespace mpt;

/* Seconds played a run. */
constexpr double kPlaySeconds = 14.0;

struct Named {
    const char* name;
    SimPlan plan;
};

const Named kPlans[] = {
    {"walk", SimPlan{}},                                       // every enemy walks: the most WORLD can have to say
    {"sparse", [] { SimPlan p; p.walk_every = 12; return p; }()},  // a level as it stands most of the time
    {"fire", [] { SimPlan p; p.fire = true; return p; }()},   // and the shots take their share
};

struct Cell {
    long runs = 0, bad = 0;
    std::vector<long> settled;
    std::vector<double> gaps;
    int most_wrong = 0;
    long state_drops = 0, data_drops = 0, kicked = 0;
    std::map<int, SimNet::Traffic> host_state;
    long member_state_bytes = 0;
    double seconds = 0;
};

long percentile(std::vector<long> v, double at) {
    if (v.empty()) {
        return 0;
    }

    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(at * static_cast<double>(v.size())))];
}

const char* kind_name(int kind) {
    return kind == mp::kind::kWorld ? "WORLD" : kind == mp::kind::kPoses ? "POSES" : kind == mp::kind::kPose ? "POSE"
           : kind == mp::kind::kShotX ? "SHOTX" : "other";
}

}  // namespace

int main(int argc, char** argv) {
    int seeds = 4;
    unsigned base = 1;
    int shard = 0, shards = 1;
    bool csv = false;
    std::string plans = "all";
    int settle = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const char* value = i + 1 < argc ? argv[i + 1] : "";

        if (arg == "--seeds") {
            seeds = std::atoi(value);
            ++i;
        } else if (arg == "--base") {
            base = static_cast<unsigned>(std::atoi(value));
            ++i;
        } else if (arg == "--plans") {
            plans = value;
            ++i;
        } else if (arg == "--settle") {
            settle = std::atoi(value);
            ++i;
        } else if (arg == "--shard") {
            std::sscanf(value, "%d/%d", &shard, &shards);
            ++i;
        } else if (arg == "--csv") {
            csv = true;
        }
    }

    const auto started = std::chrono::steady_clock::now();
    long runs = 0, index = 0, bad = 0;

    if (!csv) {
        std::printf("%-7s %3s %5s %4s %5s %4s %7s %7s %7s %6s %7s %7s %7s %7s\n", "plan", "pl", "loss", "ms", "runs", "bad", "settle50",
                    "99", "max", "gapmax", "WORLD/s", "dgram/s", "kB/s", "total");
    }

    for (Named named : kPlans) {
        if (plans != "all" && plans != named.name) {
            continue;
        }

        named.plan.settle_ms = settle != 0 ? settle : named.plan.settle_ms;

        for (int members = 1; members <= 3; ++members) {
            for (const double loss : {0.0, 1.0, 5.0, 15.0}) {
                for (const double latency : {30.0, 100.0, 250.0}) {
                    Cell cell;

                    for (int s = 0; s < seeds; ++s) {
                        if (index++ % shards != shard) {
                            continue;
                        }

                        SimLink link;
                        link.loss = loss / 100.0;
                        link.latency = latency / 2.0;
                        link.jitter = latency / 5.0;
                        link.reorder = 0.02;
                        link.duplicate = 0.01;
                        SimNet* net = nullptr;
                        const unsigned seed = base + static_cast<unsigned>(s);
                        const SimResult r = run_sim(link, seed, members, kPlaySeconds, &net, named.plan);
                        const bool wrong = r.settled_ms < 0 || net->state_drops != 0 || net->data_drops != 0 || net->kicked != 0;
                        ++cell.runs;
                        ++runs;
                        cell.bad += wrong ? 1 : 0;
                        bad += wrong ? 1 : 0;
                        cell.settled.push_back(r.settled_ms < 0 ? 999999 : r.settled_ms);
                        cell.gaps.push_back(r.worst_gap_ms);
                        cell.most_wrong = std::max(cell.most_wrong, r.most_wrong);
                        cell.state_drops += net->state_drops;
                        cell.data_drops += net->data_drops;
                        cell.kicked += net->kicked;
                        cell.seconds += kPlaySeconds + named.plan.settle_ms / 1000.0;

                        for (const auto& [kind, t] : net->host_state) {
                            cell.host_state[kind].messages += t.messages;
                            cell.host_state[kind].bytes += t.bytes;
                        }

                        for (const auto& [kind, t] : net->member_state) {
                            cell.member_state_bytes += t.bytes;
                        }

                        if (csv) {
                            std::printf("%s,%d,%.0f,%.0f,%u,%d,%ld,%d,%d,%d,%d,%.0f,%ld,%ld,%ld", named.name, members + 1, loss, latency, seed,
                                        wrong ? 1 : 0, r.settled_ms, r.most_wrong, r.wrong_place, r.wrong_hidden, r.wrong_crates,
                                        r.worst_gap_ms, net->state_drops, net->data_drops, net->kicked);

                            // In the play, then all of the run.
                            for (const auto* traffic : {&net->played_state, &net->host_state}) {
                                for (const int kind : {mp::kind::kWorld, mp::kind::kPoses, mp::kind::kPose, mp::kind::kShotX}) {
                                    const auto it = traffic->find(kind);
                                    std::printf(",%ld,%ld", it != traffic->end() ? it->second.messages : 0,
                                                it != traffic->end() ? it->second.bytes : 0);
                                }
                            }

                            long up = 0;

                            for (const auto& [kind, t] : net->member_state) {
                                up += t.bytes;
                            }

                            std::printf(",%ld\n", up);
                        }
                    }

                    if (!csv && cell.runs != 0) {
                        long messages = 0, bytes = 0;

                        for (const auto& [kind, t] : cell.host_state) {
                            messages += t.messages;
                            bytes += t.bytes;
                        }

                        const SimNet::Traffic world = cell.host_state[mp::kind::kWorld];
                        std::printf("%-7s %3d %5.0f %4.0f %5ld %4ld %7ld %7ld %7ld %6.0f %7.1f %7.1f %7.1f %7.1f\n", named.name, members + 1, loss,
                                    latency, cell.runs, cell.bad, percentile(cell.settled, 0.5), percentile(cell.settled, 0.99),
                                    percentile(cell.settled, 1.0), *std::max_element(cell.gaps.begin(), cell.gaps.end()),
                                    static_cast<double>(world.messages) / cell.seconds, static_cast<double>(messages) / cell.seconds,
                                    static_cast<double>(world.bytes) / cell.seconds / 1000.0,
                                    static_cast<double>(bytes) / cell.seconds / 1000.0);
                    }
                }
            }
        }
    }

    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    if (!csv) {
        std::printf("%ld runs, %ld wrong, %.1f s\n", runs, bad, took);
    }

    if (bad != 0 || g_failures != 0) {
        std::fprintf(stderr, "%ld of %ld runs ended wrong\n", bad, runs);
        return 1;
    }

    return 0;
}
