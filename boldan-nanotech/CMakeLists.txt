# SPDX-License-Identifier: GPL-3.0-or-later
# Boldan Nanotech at any hour (README.md): the mod's library (C, on the SDK's openrac/openrac_mod.h)
# for Going Commando, and its test: the library's code against a host table and Boldan's mobys laid
# out by the test, on each disc.
openrac_add_mod(boldan_nanotech MANIFEST openrac-mod.json SOURCES src/mod.c)
set_target_properties(boldan_nanotech PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(boldan_nanotech PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
  add_executable(boldan_nanotech_test tests/boldan_test.c)
  set_target_properties(boldan_nanotech_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(boldan_nanotech_test PRIVATE OpenRAC::mod)
  add_test(NAME boldan_nanotech COMMAND boldan_nanotech_test)
endif()
