# SPDX-License-Identifier: GPL-3.0-or-later
# Archipelago (README.md): the mod's library, its tests, and the APWorld (apworld/) packed with the
# table of locations and items the mod uses.
#
# The game side (C) uses the item randomizer's table of locations (../item-randomizer/src/logic.c)
# and its disc addresses and hook points. The client (C++) speaks Archipelago's protocol, JSON over
# a WebSocket, plain or over TLS: mbedTLS and nlohmann/json are fetched and built in, so the mod
# needs nothing of the player's machine but its root certificates.
#
# It needs interface 1.9's play status, scenes, mobys and notices, so the top CMakeLists.txt adds
# this folder only with an SDK of 1.9 or newer.
include(FetchContent)

set(randomizer_src ${CMAKE_CURRENT_SOURCE_DIR}/../item-randomizer/src)

# The fetched libraries go into the mod, a shared library: their code must be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# mbedTLS 3.6 (LTS), the release tarball (its generated files included), static and nothing else.
message(STATUS "archipelago: fetching mbedTLS 3.6.7 and nlohmann/json 3.12.0")
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
  URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(mbedtls nlohmann_json)

# The client: the protocol, the WebSocket and the transport, as a static library the mod and the
# tests share.
add_library(archipelago_client STATIC
  src/ap_client.cpp src/session.cpp src/websocket.cpp src/transport.cpp)
target_include_directories(archipelago_client PUBLIC src ${randomizer_src})
target_link_libraries(archipelago_client PUBLIC nlohmann_json::nlohmann_json mbedtls mbedx509 mbedcrypto)
set_target_properties(archipelago_client PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
target_link_libraries(archipelago_client PUBLIC Threads::Threads)
if(WIN32)
  # Winsock for the sockets, crypt32 for the system's root certificates.
  target_link_libraries(archipelago_client PUBLIC ws2_32 crypt32)
  target_compile_definitions(archipelago_client PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# The mod's library and the manifest it is packaged with.
openrac_add_mod(archipelago MANIFEST openrac-mod.json
  SOURCES src/archipelago.c ${randomizer_src}/logic.c)
target_include_directories(archipelago PRIVATE src ${randomizer_src})
set_target_properties(archipelago PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON LINKER_LANGUAGE CXX)
target_link_libraries(archipelago PRIVATE archipelago_client)
if(NOT MSVC)
  target_compile_options(archipelago PRIVATE -Wall -Wextra)
  target_compile_options(archipelago_client PRIVATE -Wall -Wextra)
endif()
# The static libraries' symbols stay inside the mod (another mbedTLS in the process is not ours).
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
  target_link_options(archipelago PRIVATE "LINKER:--exclude-libs,ALL")
endif()

# The APWorld's data (apworld/rac1/data.py), printed by a small tool over logic.c. data.py is
# committed so the logic can be reviewed; `cmake --build build --target archipelago_data` writes it
# again after logic.c changes, and the archipelago_data test fails when it is out of date.
add_executable(archipelago_dump_table tools/dump_table.c ${randomizer_src}/logic.c)
target_include_directories(archipelago_dump_table PRIVATE src ${randomizer_src})
set_target_properties(archipelago_dump_table PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
set(apworld_data ${CMAKE_CURRENT_SOURCE_DIR}/apworld/rac1/data.py)
if(NOT CMAKE_CROSSCOMPILING)
  add_custom_target(archipelago_data
    COMMAND $<TARGET_FILE:archipelago_dump_table> > ${apworld_data}
    DEPENDS archipelago_dump_table
    COMMENT "Writing apworld/rac1/data.py")
endif()

# rac1.apworld: the apworld/rac1 folder as a zip, with its manifest's container fields.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(apworld ${CMAKE_BINARY_DIR}/apworld/rac1.apworld)
  file(GLOB_RECURSE apworld_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/apworld/rac1/*)
  add_custom_command(OUTPUT ${apworld}
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_apworld.py
            ${CMAKE_CURRENT_SOURCE_DIR}/apworld/rac1 ${apworld}
    DEPENDS ${apworld_sources} tools/pack_apworld.py
    COMMENT "Packing rac1.apworld")
  add_custom_target(archipelago_apworld ALL DEPENDS ${apworld})
endif()

if(BUILD_TESTING)
  # The WebSocket's frames and the protocol's packets, without a network.
  add_executable(archipelago_unit_test tests/unit_test.cpp)
  target_link_libraries(archipelago_unit_test PRIVATE archipelago_client)
  set_target_properties(archipelago_unit_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME archipelago_unit COMMAND archipelago_unit_test)

  # The table: every location and item has its own id, and data.py is what the tool prints.
  add_test(NAME archipelago_table COMMAND archipelago_dump_table --check)
  if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME archipelago_data
      COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:archipelago_dump_table> -DDATA=${apworld_data}
              -DOUT=${CMAKE_CURRENT_BINARY_DIR}/data.py -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_data.cmake)
  endif()

  # The library as the port loads it, against a host of the test's own and an Archipelago server
  # on the loopback.
  add_executable(archipelago_load_test tests/load_test.cpp ${randomizer_src}/logic.c)
  target_include_directories(archipelago_load_test PRIVATE src ${randomizer_src})
  target_link_libraries(archipelago_load_test PRIVATE OpenRAC::mod archipelago_client ${CMAKE_DL_LIBS})
  set_target_properties(archipelago_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  if(WIN32)
    # winsock2.h, not the old winsock.h windows.h would bring in first.
    target_link_libraries(archipelago_load_test PRIVATE ws2_32)
    target_compile_definitions(archipelago_load_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()
  add_dependencies(archipelago_load_test archipelago)
  add_test(NAME archipelago_load COMMAND archipelago_load_test $<TARGET_FILE:archipelago>)
endif()
