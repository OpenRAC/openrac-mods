# SPDX-License-Identifier: GPL-3.0-or-later
# The live splits (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
openrac_add_mod(live_splits MANIFEST openrac-mod.json SOURCES src/live_splits.cpp src/splits.cpp src/client.cpp)
target_include_directories(live_splits PRIVATE src)
# The client's thread.
find_package(Threads REQUIRED)
target_link_libraries(live_splits PRIVATE Threads::Threads)
if(NOT MSVC)
  target_compile_options(live_splits PRIVATE -Wall -Wextra)
endif()
if(WIN32)
  target_link_libraries(live_splits PRIVATE ws2_32)
endif()

if(BUILD_TESTING)
  # The splits alone: the planets the game plays, the commands the mod sends LiveSplit.
  add_executable(live_splits_splits_test tests/splits_test.cpp src/splits.cpp)
  target_include_directories(live_splits_splits_test PRIVATE src)
  set_target_properties(live_splits_splits_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME live_splits_splits COMMAND live_splits_splits_test)

  # The library as the port loads it, against a host table of the test's own.
  add_executable(live_splits_load_test tests/load_test.cpp)
  target_include_directories(live_splits_load_test PRIVATE src)
  target_link_libraries(live_splits_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(live_splits_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(live_splits_load_test live_splits)
  add_test(NAME live_splits_load COMMAND live_splits_load_test $<TARGET_FILE:live_splits>)
endif()
