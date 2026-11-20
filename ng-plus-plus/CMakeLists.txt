# SPDX-License-Identifier: GPL-3.0-or-later
# NG++ (README.md): the mod's library (C++17, on the SDK's openrac/mod.hpp) and its tests.
#
# Everything but the entry point is a static library of its own, so the tests can link the parts
# they test: the rules and the book alone, the text resolver's substitution, and the built library
# loaded the way the port loads it, against a game of the test's own.

# The parts: rules, book, game view, weapons, text, vendor, experience, fights, bolts, collectors.
add_library(ng_plus_plus_core STATIC
  src/tiers.cpp
  src/save_book.cpp
  src/game_view.cpp
  src/save_tiers.cpp
  src/weapons.cpp
  src/experience.cpp
  src/vendor.cpp
  src/combat.cpp
  src/text.cpp
  src/bolt_rules.cpp
  src/platinum_bolts.cpp
  src/collectors.cpp)
target_include_directories(ng_plus_plus_core PUBLIC src)
target_link_libraries(ng_plus_plus_core PUBLIC OpenRAC::mod)
# Linked into the mod's shared library, so built for one, with its symbols as hidden as the mod's.
set_target_properties(ng_plus_plus_core PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

# The mod's library: the entry point and the parts, packaged with the manifest.
openrac_add_mod(ng_plus_plus MANIFEST openrac-mod.json SOURCES src/ng_plus_plus.cpp)
target_link_libraries(ng_plus_plus PRIVATE ng_plus_plus_core)

# The warnings of OpenRAC's own C++ (port/cmake/Warnings.cmake), for compilers that take them.
if(NOT MSVC)
  foreach(target ng_plus_plus_core ng_plus_plus)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wpedantic -Wconversion)
  endforeach()
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The rules and the book alone.
  add_executable(ng_plus_plus_rules_test tests/tiers_test.cpp)
  target_link_libraries(ng_plus_plus_rules_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_rules_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_rules COMMAND ng_plus_plus_rules_test)

  # The bolt rules alone: the scales, the platinum share and the split.
  add_executable(ng_plus_plus_bolt_rules_test tests/bolt_rules_test.cpp)
  target_link_libraries(ng_plus_plus_bolt_rules_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_bolt_rules_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_bolt_rules COMMAND ng_plus_plus_bolt_rules_test)

  # The text resolver's substitution alone.
  add_executable(ng_plus_plus_text_test tests/text_test.cpp)
  target_link_libraries(ng_plus_plus_text_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_text_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_text COMMAND ng_plus_plus_text_test)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(ng_plus_plus_load_test tests/load_test.cpp)
  target_include_directories(ng_plus_plus_load_test PRIVATE src)
  target_link_libraries(ng_plus_plus_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(ng_plus_plus_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(ng_plus_plus_load_test ng_plus_plus)
  add_test(NAME ng_plus_plus_load COMMAND ng_plus_plus_load_test $<TARGET_FILE:ng_plus_plus>)
endif()
