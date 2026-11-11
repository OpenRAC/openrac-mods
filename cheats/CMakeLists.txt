# SPDX-License-Identifier: GPL-3.0-or-later
# The cheats mod (README.md): one library (C++, on the SDK's openrac/mod.hpp; docs/MULTI_GAME.md's
# layout, Ratchet & Clank only for now): src/mod.cpp hands the load to src/rac1 by openrac_api.game,
# over src/common (which cheats are on, the multiplier's steps, the HUD's line, the launcher's list
# of mods kept in step). Its tests: the common part alone, and the library as the port loads it in
# Ratchet & Clank.
#
# nlohmann/json reads and writes the launcher's list (plugins.json): fetched and built in, header
# only, the same release the archipelago mod fetches.
include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(nlohmann_json)

# The shared part, compiled into the library and, for the tests, into a library of its own.
set(cheats_core src/common/cheats.cpp src/common/settings_file.cpp src/common/own_library.cpp
                src/common/pad_intent.cpp)

# The mod's library and the manifest it is packaged with.
openrac_add_mod(cheats MANIFEST openrac-mod.json
                SOURCES src/mod.cpp src/rac1/game.cpp ${cheats_core})
# Each game's sources find their own folder's headers by their place; these give the shared ones.
target_include_directories(cheats PRIVATE src src/common)
target_link_libraries(cheats PRIVATE nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(cheats PRIVATE -Wall -Wextra)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The shared part alone, with no host.
  add_library(cheats_core STATIC ${cheats_core})
  target_include_directories(cheats_core PUBLIC src/common)
  target_link_libraries(cheats_core PUBLIC nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
  target_compile_features(cheats_core PUBLIC cxx_std_17)
  foreach(test cheats settings_file)
    add_executable(cheats_${test}_test tests/${test}_test.cpp)
    target_link_libraries(cheats_${test}_test PRIVATE cheats_core)
    add_test(NAME cheats_${test} COMMAND cheats_${test}_test)
  endforeach()

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(cheats_rac1_load_test tests/rac1/load_test.cpp)
  target_include_directories(cheats_rac1_load_test PRIVATE src/rac1)
  target_link_libraries(cheats_rac1_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  target_compile_features(cheats_rac1_load_test PRIVATE cxx_std_17)
  add_dependencies(cheats_rac1_load_test cheats)
  add_test(NAME cheats_rac1_load COMMAND cheats_rac1_load_test $<TARGET_FILE:cheats>
           WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
