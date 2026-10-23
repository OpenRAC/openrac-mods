# SPDX-License-Identifier: GPL-3.0-or-later
# Ammo at the most (README.md): the mod's library (C, on the SDK's openrac/openrac_mod.h) and its
# tests: the count the dialog starts at, and the library as the port loads it.
#
# The library is one source file, built by the SDK's openrac_add_mod with the manifest. The tests
# are two executables registered with CTest: the count function alone, and the built library
# loaded the way the port loads it.

# The mod's library: one C file, with the manifest packaged alongside.
openrac_add_mod(ammo_max MANIFEST openrac-mod.json SOURCES src/ammo_max.c)
# The sources include ammo_max.h from src/.
target_include_directories(ammo_max PRIVATE src)
set_target_properties(ammo_max PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
# C11 is set above; warnings are turned on for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(ammo_max PRIVATE -Wall -Wextra)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The count function alone: it needs only the header.
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
