# SPDX-License-Identifier: GPL-3.0-or-later
# The shoulder camera (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
#
# Laid out as docs/MULTI_GAME.md has it: the entry point (src/mod.cpp) picks the game's part;
# Going Commando's is src/rac2/shoulder_camera.cpp over a header with no game memory
# (src/rac2/view.hpp) that holds the aim and the eye's maths, and src/rac2/discs.hpp the discs'
# addresses. The tests are C++ programs registered with CTest: the maths alone, and the built
# library loaded the way the port loads it.

# The mod's library and the manifest it is packaged with.
openrac_add_mod(shoulder_camera MANIFEST openrac-mod.json SOURCES src/mod.cpp src/rac2/shoulder_camera.cpp)
# The sources include rac2/... from src/.
target_include_directories(shoulder_camera PRIVATE src)
# The warnings of OpenRAC's own C++ (port/cmake/Warnings.cmake), for compilers that take them.
if(NOT MSVC)
  target_compile_options(shoulder_camera PRIVATE -Wall -Wextra -Wshadow -Wpedantic -Wconversion)
endif()

# The tests build only when testing is on.
if(BUILD_TESTING)
  # The aim and the eye's maths alone: src/rac2/view.hpp and src/rac2/discs.hpp, no host.
  add_executable(shoulder_camera_view_test tests/rac2/view_test.cpp)
  target_include_directories(shoulder_camera_view_test PRIVATE src)
  set_target_properties(shoulder_camera_view_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  # The maths uses the maths library, which is separate on Unix.
  if(UNIX)
    target_link_libraries(shoulder_camera_view_test PRIVATE m)
  endif()
  add_test(NAME shoulder_camera_view COMMAND shoulder_camera_view_test)

  # The library as the port loads it, against a host table and game memory of the test's own.
  add_executable(shoulder_camera_load_test tests/rac2/load_test.cpp)
  target_include_directories(shoulder_camera_load_test PRIVATE src)
  target_link_libraries(shoulder_camera_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(shoulder_camera_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(shoulder_camera_load_test shoulder_camera)
  add_test(NAME shoulder_camera_load COMMAND shoulder_camera_load_test $<TARGET_FILE:shoulder_camera>)
endif()
