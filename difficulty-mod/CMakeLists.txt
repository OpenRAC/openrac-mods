# SPDX-License-Identifier: GPL-3.0-or-later
# The difficulty mod (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests:
# the rules (ranges, Chaos's rolls and events), the manifest's presets, the moby pool's guard, the
# classes' table against the SDK's behaviour catalogue, and the library as the port loads it.
#
# The rules and the moby pool are the mod's core, compiled into the library and, for the tests, into
# a static library of their own so they run without a host.

# The sources shared by the mod's library and the tests of the rules and the pool.
set(difficulty_core src/rules.cpp src/pool.cpp)

# The mod's library and the manifest it is packaged with.
openrac_add_mod(difficulty MANIFEST openrac-mod.json SOURCES src/difficulty.cpp ${difficulty_core})
target_include_directories(difficulty PRIVATE src)
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(difficulty PRIVATE -Wall -Wextra)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The core as a library of its own for the tests that need no host.
  add_library(difficulty_core STATIC ${difficulty_core})
  target_include_directories(difficulty_core PUBLIC src)
  target_compile_features(difficulty_core PUBLIC cxx_std_17)

  # One test program each for the rules and the pool, over the core.
  foreach(test rules pool)
    add_executable(difficulty_${test}_test tests/${test}_test.cpp)
    target_link_libraries(difficulty_${test}_test PRIVATE difficulty_core)
    add_test(NAME difficulty_${test} COMMAND difficulty_${test}_test)
  endforeach()

  # The classes' table: every class the catalogue names as an enemy, a turret, a mine, a boss, a
  # ship or a thief is in classes.tsv, and src/classes.h says what classes.tsv says. The presets:
  # every slider and the Chaos switch, inside the sliders' ranges (rules.cpp's).
  # These two checks are Python scripts, so they run only when Python is found.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME difficulty_classes
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/classes_test.py
                     ${OpenRACMod_DIR}/../catalogue/rac1/classes.json)
    add_test(NAME difficulty_presets
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/presets_test.py)
  endif()

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(difficulty_load_test tests/load_test.cpp)
  target_include_directories(difficulty_load_test PRIVATE src)
  target_link_libraries(difficulty_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  target_compile_features(difficulty_load_test PRIVATE cxx_std_17)
  add_dependencies(difficulty_load_test difficulty)
  add_test(NAME difficulty_load COMMAND difficulty_load_test $<TARGET_FILE:difficulty>)
endif()
