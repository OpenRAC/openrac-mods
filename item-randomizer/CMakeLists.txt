# SPDX-License-Identifier: GPL-3.0-or-later
# The item randomizer (README.md): the mod's library, and the tests of its seed code, solver,
# table, settings and hooks.
#
# The seed code, solver, config and paths are the mod's core, compiled into the library and, for the
# tests, into a static library of their own so they run without a host.

# The sources shared by the mod's library and the tests.
set(item_randomizer_core src/logic.c src/seed.c src/solver.c src/config.c src/paths.c)

# The mod's library and the manifest it is packaged with.
openrac_add_mod(item_randomizer MANIFEST openrac-mod.json SOURCES src/item_randomizer.c ${item_randomizer_core})
target_include_directories(item_randomizer PRIVATE src)
set_target_properties(item_randomizer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(item_randomizer PRIVATE -Wall -Wextra)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The core as a library of its own for the tests that need no host.
  add_library(item_randomizer_core STATIC ${item_randomizer_core})
  target_include_directories(item_randomizer_core PUBLIC src)
  set_target_properties(item_randomizer_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

  # One test program each for the seed code, the solver and the settings, over the core.
  foreach(test seed solver settings table)
    add_executable(item_randomizer_${test}_test tests/${test}_test.c)
    set_target_properties(item_randomizer_${test}_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(item_randomizer_${test}_test PRIVATE item_randomizer_core)
  endforeach()
  # Register them: the settings test also reads the manifest to check its keys and defaults, the
  # table test both discs' facts.
  add_test(NAME item_randomizer_seed COMMAND item_randomizer_seed_test)
  add_test(NAME item_randomizer_solver COMMAND item_randomizer_solver_test)
  add_test(NAME item_randomizer_settings COMMAND item_randomizer_settings_test ${CMAKE_CURRENT_SOURCE_DIR}/openrac-mod.json)
  add_test(NAME item_randomizer_table COMMAND item_randomizer_table_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/discs.tsv)

  # Many seeds of every setting, each checked beatable: tests/solver_test.c with a count and a
  # first seed.
  add_test(NAME item_randomizer_solver_sweep COMMAND item_randomizer_solver_test 5000 1000)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(item_randomizer_load_test tests/load_test.c)
  set_target_properties(item_randomizer_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_include_directories(item_randomizer_load_test PRIVATE src)
  target_link_libraries(item_randomizer_load_test PRIVATE OpenRAC::mod item_randomizer_core ${CMAKE_DL_LIBS})
  add_dependencies(item_randomizer_load_test item_randomizer)
  add_test(NAME item_randomizer_load COMMAND item_randomizer_load_test $<TARGET_FILE:item_randomizer>)
endif()
