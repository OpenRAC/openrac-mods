# SPDX-License-Identifier: GPL-3.0-or-later
# NG++ (README.md): the mod's library (C++17, on the SDK's openrac/mod.hpp) and its tests.
#
# Everything but the entry point is a static library of its own, so the tests can link the parts
# they test: the rules and the book alone, the text resolver's substitution, and the built library
# loaded the way the port loads it, against a game of the test's own.

# The parts: rules, book, game view, weapons, text, vendor, experience, fights, bolts, collectors,
# the weapons' looks.
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
  src/collectors.cpp
  src/rewards.cpp
  src/weapon_visuals.cpp
  src/extra_enemies.cpp
  src/perk_rules.cpp
  src/moby_scan.cpp
  src/elites.cpp
  src/ice_fx.cpp
  src/perks.cpp
  src/armour.cpp
  src/armour_vendor.cpp
  src/homing.cpp
  src/shots.cpp
  src/tuning_rules.cpp
  src/zodiac_credit.cpp
  src/tuning.cpp
  src/ship_upgrades.cpp
  src/space_missions.cpp
  src/ship_looks.cpp
  src/culling.cpp
  src/test_setup.cpp)
target_include_directories(ng_plus_plus_core PUBLIC src)

# A setup for hidden test runs only (src/test_setup.h): never in a package.
option(NGPP_TEST_SETUP "NG++: the hidden test runs' setup (NGPP_TEST)" OFF)
if(NGPP_TEST_SETUP)
  target_compile_definitions(ng_plus_plus_core PUBLIC NGPP_TEST_SETUP)
endif()
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

# The Ice Gun's meshes and pictures (tools/make_ice_meshes.py), beside the manifest in the package:
# the props read them from the mod's folder.
get_target_property(ngpp_package ng_plus_plus OPENRAC_MOD_PACKAGE_DIR)
add_custom_command(TARGET ng_plus_plus POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${ngpp_package}/assets"
  COMMENT "Packaging the Ice Gun's meshes"
  VERBATIM)

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

  # The 0.5.0 rules alone: the perks, the Ice Gun, the armour, the elites and the Omega Prime step.
  add_executable(ng_plus_plus_perks_test tests/perks_test.cpp)
  target_link_libraries(ng_plus_plus_perks_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_perks_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_perks COMMAND ng_plus_plus_perks_test)

  # The space rules alone: the waves, the elites, the ship's tiers and looks.
  add_executable(ng_plus_plus_space_test tests/space_test.cpp)
  target_link_libraries(ng_plus_plus_space_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_space_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_space COMMAND ng_plus_plus_space_test)

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

  # The homing rockets' rules alone: targets, turns, chases.
  add_executable(ng_plus_plus_homing_test tests/homing_test.cpp)
  target_link_libraries(ng_plus_plus_homing_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_homing_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_homing COMMAND ng_plus_plus_homing_test)

  # 0.5.1's off-screen picks: weapon_ignore_culling found in the port's table.
  add_executable(ng_plus_plus_culling_test tests/culling_test.cpp)
  target_link_libraries(ng_plus_plus_culling_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_culling_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_culling COMMAND ng_plus_plus_culling_test)

  # 0.6.4's Zodiac kill credit: the pulse's and the reach's kills earn the Zodiac's experience.
  add_executable(ng_plus_plus_zodiac_credit_test tests/zodiac_credit_test.cpp)
  target_link_libraries(ng_plus_plus_zodiac_credit_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_zodiac_credit_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_zodiac_credit COMMAND ng_plus_plus_zodiac_credit_test)

  # 0.6.6's armour paint: none in a scene, none without Carbonox, the tier's in play.
  add_executable(ng_plus_plus_paint_test tests/paint_test.cpp)
  target_link_libraries(ng_plus_plus_paint_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_paint_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_paint COMMAND ng_plus_plus_paint_test)

  # 0.6.22's Ice Gun look: its meshes and pictures are there and well formed.
  add_executable(ng_plus_plus_ice_assets_test tests/ice_assets_test.cpp)
  target_link_libraries(ng_plus_plus_ice_assets_test PRIVATE ng_plus_plus_core)
  target_compile_definitions(ng_plus_plus_ice_assets_test PRIVATE NGPP_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  set_target_properties(ng_plus_plus_ice_assets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_ice_assets COMMAND ng_plus_plus_ice_assets_test)

  # 0.6.22's line at the top of the picture: every line clears itself when its time is up.
  add_executable(ng_plus_plus_notices_test tests/notices_test.cpp)
  target_link_libraries(ng_plus_plus_notices_test PRIVATE ng_plus_plus_core)
  set_target_properties(ng_plus_plus_notices_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ng_plus_plus_notices COMMAND ng_plus_plus_notices_test)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(ng_plus_plus_load_test tests/load_test.cpp)
  target_include_directories(ng_plus_plus_load_test PRIVATE src)
  target_link_libraries(ng_plus_plus_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(ng_plus_plus_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(ng_plus_plus_load_test ng_plus_plus)
  add_test(NAME ng_plus_plus_load COMMAND ng_plus_plus_load_test $<TARGET_FILE:ng_plus_plus>)
endif()
