# SPDX-License-Identifier: GPL-3.0-or-later
# NG++ (README.md): the mod's library (C, on the SDK's openrac/openrac_mod.h) and its tests:
# the rules alone (src/tiers.c), and the library as the port loads it against a game of the test's
# own.

openrac_add_mod(ng_plus_plus MANIFEST openrac-mod.json SOURCES src/ng_plus_plus.c src/tiers.c)
target_include_directories(ng_plus_plus PRIVATE src)
set_target_properties(ng_plus_plus PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(ng_plus_plus PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
  # The rules alone.
  add_executable(ng_plus_plus_rules_test tests/tiers_test.c src/tiers.c)
  target_include_directories(ng_plus_plus_rules_test PRIVATE src)
  set_target_properties(ng_plus_plus_rules_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_rules COMMAND ng_plus_plus_rules_test)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(ng_plus_plus_load_test tests/load_test.c)
  target_include_directories(ng_plus_plus_load_test PRIVATE src)
  set_target_properties(ng_plus_plus_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(ng_plus_plus_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  add_dependencies(ng_plus_plus_load_test ng_plus_plus)
  add_test(NAME ng_plus_plus_load COMMAND ng_plus_plus_load_test $<TARGET_FILE:ng_plus_plus>)
endif()
