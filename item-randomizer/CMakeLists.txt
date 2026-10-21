# SPDX-License-Identifier: GPL-3.0-or-later
# The item randomizer (README.md): the mod's library, and the tests of its seed code, solver,
# settings and hooks.
set(item_randomizer_core src/logic.c src/seed.c src/solver.c src/config.c src/paths.c)

openrac_add_mod(item_randomizer MANIFEST openrac-mod.json SOURCES src/item_randomizer.c ${item_randomizer_core})
target_include_directories(item_randomizer PRIVATE src)
set_target_properties(item_randomizer PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(item_randomizer PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
  add_library(item_randomizer_core STATIC ${item_randomizer_core})
  target_include_directories(item_randomizer_core PUBLIC src)
  set_target_properties(item_randomizer_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

  foreach(test seed solver settings)
    add_executable(item_randomizer_${test}_test tests/${test}_test.c)
    set_target_properties(item_randomizer_${test}_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(item_randomizer_${test}_test PRIVATE item_randomizer_core)
  endforeach()
  add_test(NAME item_randomizer_seed COMMAND item_randomizer_seed_test)
  add_test(NAME item_randomizer_solver COMMAND item_randomizer_solver_test)
  add_test(NAME item_randomizer_settings COMMAND item_randomizer_settings_test ${CMAKE_CURRENT_SOURCE_DIR}/openrac-mod.json)

  # Many seeds of every setting, each checked beatable: tests/solver_test.c with a count and a
  # first seed (tools/sweep.sh runs it across machines).
  add_test(NAME item_randomizer_solver_sweep COMMAND item_randomizer_solver_test 5000 1000)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(item_randomizer_load_test tests/load_test.c)
  set_target_properties(item_randomizer_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_include_directories(item_randomizer_load_test PRIVATE src)
  target_link_libraries(item_randomizer_load_test PRIVATE OpenRAC::mod item_randomizer_core ${CMAKE_DL_LIBS})
  add_dependencies(item_randomizer_load_test item_randomizer)
  add_test(NAME item_randomizer_load COMMAND item_randomizer_load_test $<TARGET_FILE:item_randomizer>)
endif()
