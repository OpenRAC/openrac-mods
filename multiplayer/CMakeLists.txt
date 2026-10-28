# SPDX-License-Identifier: GPL-3.0-or-later
# Multiplayer (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
#
# The library is five sources built by the SDK's openrac_add_mod with the manifest: the mod's entry
# point and pages, the session (the party and the lobby), the connection (the socket, on its own
# thread), the embedded host of Direct connect (the server's party routing, in the hosting game)
# and the protocol (the server's frames and datagrams). It needs interface 1.7's pages of the
# game's own menus, so the top CMakeLists.txt adds this folder only with an SDK of 1.7 or newer.

# The mod's library and the manifest it is packaged with.
openrac_add_mod(multiplayer MANIFEST openrac-mod.json
                SOURCES src/multiplayer.cpp src/session.cpp src/connection.cpp src/host.cpp
                        src/protocol.cpp)
# The sources include each other's headers from src/.
target_include_directories(multiplayer PRIVATE src)
# The connection runs on a thread of its own, so it needs the platform's thread library.
find_package(Threads REQUIRED)
target_link_libraries(multiplayer PRIVATE Threads::Threads)
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(multiplayer PRIVATE -Wall -Wextra)
endif()
# On Windows the sockets come from Winsock.
if(WIN32)
  target_link_libraries(multiplayer PRIVATE ws2_32)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The protocol's bytes and the session's lobby, against a recorder; then the connection against
  # a server the test makes on the loopback, and Direct connect: a host and clients on the loopback.
  add_executable(multiplayer_test tests/multiplayer_test.cpp src/session.cpp src/connection.cpp src/host.cpp
                 src/protocol.cpp)
  target_include_directories(multiplayer_test PRIVATE src)
  set_target_properties(multiplayer_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(multiplayer_test PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(multiplayer_test PRIVATE ws2_32)
  endif()
  add_test(NAME multiplayer COMMAND multiplayer_test)
endif()
