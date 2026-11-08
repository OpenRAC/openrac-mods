# SPDX-License-Identifier: GPL-3.0-or-later
# The difficulty mod (README.md): one library for Ratchet & Clank and Going Commando (C++, on the
# SDK's openrac/mod.hpp; docs/MULTI_GAME.md): src/mod.cpp hands the load to src/rac1 or src/rac2 by
# openrac_api.game, both over src/common (the rules, Chaos, the party's rules, the moby pool's
# guard). Its tests: the rules (ranges, Chaos's rolls and events), the manifest's presets, the moby
# pool's guard, the classes' table against the SDK's behaviour catalogue, and the library as the
# port loads it in each game.
#
# The rules and the moby pool are the mod's core, compiled into the library and, for the tests, into
# a static library of their own so they run without a host.

# The sources shared by the mod's library and the tests of the rules and the pool.
set(difficulty_core src/common/rules.cpp src/common/pool.cpp)

# The mod's library and the manifest it is packaged with.
openrac_add_mod(difficulty MANIFEST openrac-mod.json
                SOURCES src/mod.cpp src/common/session.cpp src/rac1/game.cpp src/rac2/game.cpp
                        ${difficulty_core})
# Each game's sources find their own folder's headers by their place; these give the shared ones.
target_include_directories(difficulty PRIVATE src src/common)
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(difficulty PRIVATE -Wall -Wextra)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The core as a library of its own for the tests that need no host.
  add_library(difficulty_core STATIC ${difficulty_core})
  target_include_directories(difficulty_core PUBLIC src/common)
  target_compile_features(difficulty_core PUBLIC cxx_std_17)

  # One test program each for the rules and the pool, over the core.
  foreach(test rules pool)
    add_executable(difficulty_${test}_test tests/${test}_test.cpp)
    target_link_libraries(difficulty_${test}_test PRIVATE difficulty_core)
    add_test(NAME difficulty_${test} COMMAND difficulty_${test}_test)
  endforeach()

  # The classes' table: every class the catalogue names as an enemy, a turret, a mine, a boss, a
  # ship or a thief is in classes.tsv, and src/rac1/classes.h says what classes.tsv says. The presets:
  # every slider and the Chaos switch, inside the sliders' ranges (rules.cpp's).
  # These two checks are Python scripts, so they run only when Python is found.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME difficulty_classes
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/rac1/classes_test.py
                     ${OpenRACMod_DIR}/../catalogue/rac1/classes.json)
    add_test(NAME difficulty_presets
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/presets_test.py)
  endif()

  # The library as the port loads it, against a host table and game memory of the test's own: one
  # program for each game, each with its own game's headers.
  foreach(game rac1 rac2)
    add_executable(difficulty_${game}_load_test tests/${game}/load_test.cpp)
    target_include_directories(difficulty_${game}_load_test PRIVATE src/${game})
    target_link_libraries(difficulty_${game}_load_test PRIVATE OpenRAC::mod difficulty_core ${CMAKE_DL_LIBS})
    target_compile_features(difficulty_${game}_load_test PRIVATE cxx_std_17)
    add_dependencies(difficulty_${game}_load_test difficulty)
    add_test(NAME difficulty_${game}_load COMMAND difficulty_${game}_load_test $<TARGET_FILE:difficulty>)
  endforeach()
endif()
