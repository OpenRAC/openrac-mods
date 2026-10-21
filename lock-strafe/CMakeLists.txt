# SPDX-License-Identifier: GPL-3.0-or-later
# The lock-strafe (README.md): the mod's library and the strafe decision's test.
openrac_add_mod(lock_strafe MANIFEST openrac-mod.json SOURCES src/lock_strafe.c)
target_include_directories(lock_strafe PRIVATE src)
if(NOT MSVC)
  target_compile_options(lock_strafe PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

if(BUILD_TESTING)
  add_executable(hero_strafe_test tests/hero_strafe_test.c)
  set_target_properties(hero_strafe_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  # The decision compares angles the way the game computes them: no fused multiply-adds.
  if(MSVC)
    target_compile_options(hero_strafe_test PRIVATE /fp:precise)
  else()
    target_compile_options(hero_strafe_test PRIVATE -ffp-contract=off)
  endif()
  if(UNIX)
    target_link_libraries(hero_strafe_test PRIVATE m)
  endif()
  add_test(NAME hero_strafe COMMAND hero_strafe_test)

  # The library as the port loads it, against a host table of the test's own.
  add_executable(lock_strafe_load_test tests/load_test.c)
  set_target_properties(lock_strafe_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(lock_strafe_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  add_dependencies(lock_strafe_load_test lock_strafe)
  add_test(NAME lock_strafe_load COMMAND lock_strafe_load_test $<TARGET_FILE:lock_strafe>)
endif()
