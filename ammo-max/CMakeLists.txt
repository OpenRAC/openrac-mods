# SPDX-License-Identifier: GPL-3.0-or-later
# Ammo at the most (README.md): the mod's library (C, on the SDK's openrac/openrac_mod.h) and its
# tests: the count the dialog starts at, and the library as the port loads it.
openrac_add_mod(ammo_max MANIFEST openrac-mod.json SOURCES src/ammo_max.c)
target_include_directories(ammo_max PRIVATE src)
set_target_properties(ammo_max PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(ammo_max PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
  add_executable(ammo_max_count_test tests/count_test.c)
  target_include_directories(ammo_max_count_test PRIVATE src)
  set_target_properties(ammo_max_count_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  add_test(NAME ammo_max_count COMMAND ammo_max_count_test)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(ammo_max_load_test tests/load_test.c)
  target_include_directories(ammo_max_load_test PRIVATE src)
  set_target_properties(ammo_max_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(ammo_max_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  add_dependencies(ammo_max_load_test ammo_max)
  add_test(NAME ammo_max_load COMMAND ammo_max_load_test $<TARGET_FILE:ammo_max>)
endif()
