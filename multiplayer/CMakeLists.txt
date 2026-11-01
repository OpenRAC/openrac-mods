# SPDX-License-Identifier: GPL-3.0-or-later
# Multiplayer (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
#
# The library is the sources below built by the SDK's openrac_add_mod with the manifest: the mod's entry
# point and pages, the session (the party and the lobby), the connection (the socket, on its own
# thread), the embedded host of Direct connect (the server's party routing, in the hosting game),
# Direct connect's page logic (the address rows), the protocol (the server's frames and
# datagrams), the co-op (the game started together, the level followed, the figures, the scenes) and the
# poses (their bytes and the interpolation buffer), the shared world (the host's mobys shown by
# the members), the shared progress (items, gold bolts, planets), the mods a party agrees on and its verbose log. It needs interface 1.7's pages of the game's own menus, 1.8's play calls and 1.9's
# world calls, so the top CMakeLists.txt adds this folder only with an SDK of 1.9 or newer.

# The co-op's and the shared world's sources, split by area (coop.hpp, world.hpp); the tests build them too.
set(MULTIPLAYER_COOP_SOURCES src/coop.cpp src/coop_travel.cpp src/coop_figures.cpp src/coop_shots.cpp src/nametags.cpp
                             src/coop_join.cpp src/coop_leave.cpp src/coop_wait.cpp src/cosave.cpp)
set(MULTIPLAYER_WORLD_SOURCES src/world.cpp src/world_pickups.cpp src/world_host.cpp src/world_member.cpp
                              src/world_handle.cpp src/world_gate.cpp)

# The play test's files: main and one file of tests per area, sharing tests/test_support.hpp.
set(MULTIPLAYER_PLAY_TESTS tests/play_test.cpp tests/play_poses_test.cpp tests/play_coop_test.cpp
                           tests/play_world_test.cpp tests/play_mods_test.cpp tests/play_pause_test.cpp
                           tests/play_bolts_test.cpp tests/play_scenes_test.cpp tests/play_party_test.cpp
                           tests/play_sim_test.cpp tests/play_save_test.cpp tests/play_cross_test.cpp
                           tests/play_saves_test.cpp tests/play_tags_test.cpp tests/play_join_test.cpp
                           tests/play_shared_test.cpp tests/play_lobby_test.cpp)

# The mod's library and the manifest it is packaged with.
openrac_add_mod(multiplayer MANIFEST openrac-mod.json
                SOURCES src/multiplayer.cpp src/game_adapter.cpp src/pages_home.cpp src/pages_browse.cpp src/pages_saves.cpp
                        src/pages_lobby.cpp src/pages_refused.cpp src/pages_rules.cpp src/pages_remove.cpp src/session.cpp src/connection.cpp src/host.cpp
                        src/direct.cpp src/protocol.cpp ${MULTIPLAYER_COOP_SOURCES} src/coop_scenes.cpp src/play.cpp ${MULTIPLAYER_WORLD_SOURCES} src/party_mods.cpp src/modcode.cpp src/save_check.cpp src/progress.cpp src/shared.cpp src/despawn.cpp src/trace.cpp src/world_trace.cpp)
# The sources include each other's headers from src/.
target_include_directories(multiplayer PRIVATE src)
# The end-to-end tests' driver (src/bot.hpp, tests/e2e): a development build's, never a release's.
option(MULTIPLAYER_BOT "Build the test bot into the multiplayer mod" OFF)
if(MULTIPLAYER_BOT)
  target_sources(multiplayer PRIVATE src/bot.cpp)
  target_compile_definitions(multiplayer PRIVATE MULTIPLAYER_BOT)
endif()
# The connection runs on a thread of its own, so it needs the platform's thread library.
find_package(Threads REQUIRED)
target_link_libraries(multiplayer PRIVATE Threads::Threads)
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(multiplayer PRIVATE -Wall -Wextra)
endif()
# On Windows the sockets come from Winsock, whose headers define min and max as macros unless told
# not to (they broke std::min in host.cpp).
if(WIN32)
  target_link_libraries(multiplayer PRIVATE ws2_32)
  target_compile_definitions(multiplayer PRIVATE NOMINMAX)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The protocol's bytes and the session's lobby, against a recorder; then the connection against
  # a server the test makes on the loopback, and Direct connect: a host and clients on the loopback.
  add_executable(multiplayer_test tests/multiplayer_test.cpp src/session.cpp src/connection.cpp src/host.cpp
                 src/direct.cpp src/protocol.cpp ${MULTIPLAYER_COOP_SOURCES} src/coop_scenes.cpp src/play.cpp ${MULTIPLAYER_WORLD_SOURCES} src/party_mods.cpp src/modcode.cpp src/save_check.cpp src/progress.cpp src/shared.cpp src/despawn.cpp src/trace.cpp src/world_trace.cpp)
  target_include_directories(multiplayer_test PRIVATE src "${OPENRAC_MOD_SDK_DIR}/include")
  set_target_properties(multiplayer_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(multiplayer_test PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(multiplayer_test PRIVATE ws2_32)
    target_compile_definitions(multiplayer_test PRIVATE NOMINMAX)
  endif()
  add_test(NAME multiplayer COMMAND multiplayer_test)
  # Playing together, without the game: the poses' bytes, the interpolation buffer, and the co-op
  # of a host and members through a router that does what the server does with DATA.
  add_executable(multiplayer_play_test ${MULTIPLAYER_PLAY_TESTS} src/session.cpp src/protocol.cpp
                 ${MULTIPLAYER_COOP_SOURCES} src/coop_scenes.cpp src/play.cpp ${MULTIPLAYER_WORLD_SOURCES} src/party_mods.cpp src/modcode.cpp src/save_check.cpp src/progress.cpp src/shared.cpp src/despawn.cpp src/trace.cpp src/world_trace.cpp)
  target_include_directories(multiplayer_play_test PRIVATE src "${OPENRAC_MOD_SDK_DIR}/include")
  # Poses recorded from two games (a walk with stops, a stall of the sender's), replayed.
  target_compile_definitions(multiplayer_play_test PRIVATE MP_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  set_target_properties(multiplayer_play_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME multiplayer_play COMMAND multiplayer_play_test)
  # The party on a seeded network over many seeds (the head of tests/sim_sweep_test.cpp).
  add_executable(multiplayer_sweep_test tests/sim_sweep_test.cpp src/session.cpp src/protocol.cpp
                 ${MULTIPLAYER_COOP_SOURCES} src/coop_scenes.cpp src/play.cpp ${MULTIPLAYER_WORLD_SOURCES} src/party_mods.cpp src/save_check.cpp src/progress.cpp src/despawn.cpp src/trace.cpp src/world_trace.cpp)
  target_include_directories(multiplayer_sweep_test PRIVATE src tests "${OPENRAC_MOD_SDK_DIR}/include")
  set_target_properties(multiplayer_sweep_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  # The interpolation buffer over a real network: late, uneven, reordered and lost poses, and the
  # arrivals of a session through the public server, shown as smoothly as they should be.
  add_executable(multiplayer_smooth_test tests/smooth_test.cpp src/play.cpp src/protocol.cpp)
  target_include_directories(multiplayer_smooth_test PRIVATE src)
  target_compile_definitions(multiplayer_smooth_test PRIVATE MP_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  set_target_properties(multiplayer_smooth_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME multiplayer_smooth COMMAND multiplayer_smooth_test)
  # The wire registry (src/kinds.hpp): no two kinds share a value, each lies in its owner's range,
  # and the router sends every kind where the hand-written chain it replaced did.
  add_executable(multiplayer_kinds_test tests/kinds_test.cpp)
  target_include_directories(multiplayer_kinds_test PRIVATE src)
  set_target_properties(multiplayer_kinds_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME multiplayer_kinds COMMAND multiplayer_kinds_test)
endif()
