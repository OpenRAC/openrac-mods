# SPDX-License-Identifier: GPL-3.0-or-later
# Keyboard and mouse (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
#
# The library is one C++ source built by the SDK's openrac_add_mod with the manifest. The tests are
# two executables registered with CTest: the logic alone, and the built library loaded the way the
# port loads it.

# The mod's library and the manifest it is packaged with.
openrac_add_mod(keyboard_mouse MANIFEST openrac-mod.json SOURCES src/keyboard_mouse.cpp)
target_include_directories(keyboard_mouse PRIVATE src)
# Warnings on, for compilers that take these flags.
if(NOT MSVC)
  target_compile_options(keyboard_mouse PRIVATE -Wall -Wextra)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The logic alone: binds, conflicts, the sticks, the Quick Select's direction, the report; and
  # the manifest's keys and defaults against the action table.
  add_executable(keyboard_mouse_logic_test tests/logic_test.cpp)
  target_include_directories(keyboard_mouse_logic_test PRIVATE src)
  target_link_libraries(keyboard_mouse_logic_test PRIVATE OpenRAC::mod)
  set_target_properties(keyboard_mouse_logic_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME keyboard_mouse_logic
           COMMAND keyboard_mouse_logic_test "${CMAKE_CURRENT_SOURCE_DIR}/openrac-mod.json")

  # The library as the port loads it, against a host table of the test's own.
  add_executable(keyboard_mouse_load_test tests/load_test.cpp)
  target_include_directories(keyboard_mouse_load_test PRIVATE src)
  target_link_libraries(keyboard_mouse_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(keyboard_mouse_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(keyboard_mouse_load_test keyboard_mouse)
  add_test(NAME keyboard_mouse_load COMMAND keyboard_mouse_load_test $<TARGET_FILE:keyboard_mouse>)
endif()
