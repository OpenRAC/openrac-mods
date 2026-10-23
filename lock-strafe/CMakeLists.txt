# SPDX-License-Identifier: GPL-3.0-or-later
# The lock-strafe (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
#
# The library is one C++ source (src/lock_strafe.cpp) over a header of plain C (src/strafe.h) that
# holds the decision of when and how the hero strafes. The tests are C programs registered with
# CTest: the decision alone, and the built library loaded the way the port loads it.

# The mod's library and the manifest it is packaged with.
openrac_add_mod(lock_strafe MANIFEST openrac-mod.json SOURCES src/lock_strafe.cpp)
# The source includes strafe.h and discs.h from src/.
target_include_directories(lock_strafe PRIVATE src)
# Warnings on, and no fused multiply-adds so the angles come out as the game computes them.
if(NOT MSVC)
  target_compile_options(lock_strafe PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The strafe decision alone: it includes src/strafe.h and needs no host.
  add_executable(hero_strafe_test tests/hero_strafe_test.c)
  set_target_properties(hero_strafe_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  # The decision compares angles the way the game computes them: no fused multiply-adds.
  if(MSVC)
    target_compile_options(hero_strafe_test PRIVATE /fp:precise)
  else()
    target_compile_options(hero_strafe_test PRIVATE -ffp-contract=off)
  endif()
  # The decision uses the maths library, which is separate on Unix.
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
