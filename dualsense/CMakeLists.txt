# SPDX-License-Identifier: GPL-3.0-or-later
# The DualSense (README.md): the mod's library (C++, on the SDK's openrac/mod.hpp) and its tests.
#
# The mod talks to the PlayStation 5 controller through SDL3. It brings its own copy of SDL3, built
# static, so that it works with a port that has SDL3 built in (the 'standalone' preset, what the
# launcher ships) and with one that uses the system's: the mod then needs nothing of the player's
# machine.
#
#   -DOPENRAC_DUALSENSE_SHARED_SDL=ON   use the system's shared SDL3 instead (a quicker build;
#                                       the mod then needs libSDL3 at run time)
# The mod's library: its manifest and its four sources.
include(FetchContent)
openrac_add_mod(dualsense MANIFEST openrac-mod.json
  SOURCES src/dualsense.cpp src/ps2_pad.cpp src/rumble.cpp src/weapons.cpp)
# The sources include one another by bare name.
target_include_directories(dualsense PRIVATE src)
# Warnings on for the mod's own code (not for MSVC, which has other flags).
if(NOT MSVC)
  target_compile_options(dualsense PRIVATE -Wall -Wextra)
endif()

# Which SDL3 the mod links: the system's shared one, or a static copy fetched here.
option(OPENRAC_DUALSENSE_SHARED_SDL "Use the system's shared SDL3 (the mod then needs it at run time)" OFF)
if(OPENRAC_DUALSENSE_SHARED_SDL)
  find_package(SDL3 CONFIG REQUIRED)
  set(dualsense_sdl SDL3::SDL3)
  message(STATUS "DualSense: SDL3 ${SDL3_VERSION} (system, shared)")
else()
  # SDL3 is fetched and built static, the way the port does it. The system one is not looked for:
  # its imported targets have the same names as the fetched ones, and CMake cannot have both.
  message(STATUS "DualSense: fetching SDL3 release-3.2.24 (static)")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  # The mod opens no window, only the controller: on Linux it builds without the X11 or Wayland
  # headers too (the arm64 runner has neither), and SDL still uses them when they are there.
  if(UNIX AND NOT APPLE)
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
  endif()
  # The static library goes into the mod, which is a shared object: it has to be position
  # independent. SDL3 does not ask for that itself.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    set(sdl_options SYSTEM)
  endif()
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    list(APPEND sdl_options EXCLUDE_FROM_ALL)
  endif()
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.24
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL3)
  set(dualsense_sdl SDL3::SDL3-static)
endif()
# The mod links whichever SDL3 was chosen above.
target_link_libraries(dualsense PRIVATE ${dualsense_sdl})

# The tests and probes, built only when testing is on.
if(BUILD_TESTING)
  # The vibration and the buttons alone: the envelope, the strengths, and the report.
  add_executable(dualsense_logic_test tests/logic_test.cpp src/ps2_pad.cpp src/rumble.cpp)
  target_include_directories(dualsense_logic_test PRIVATE src)
  set_target_properties(dualsense_logic_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME dualsense_logic COMMAND dualsense_logic_test)

  # A probe of the controller itself, for a machine that has one attached. Not a test: it says
  # what the controller reports and lets the light bar and the motors be seen.
  add_executable(dualsense_probe tests/hardware_probe.cpp)
  target_link_libraries(dualsense_probe PRIVATE ${dualsense_sdl})
  set_target_properties(dualsense_probe PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

  # A live check of the extra buttons: it watches the touchpad and the microphone button.
  add_executable(dualsense_watch tests/watch_buttons.cpp src/ps2_pad.cpp)
  target_include_directories(dualsense_watch PRIVATE src)
  target_link_libraries(dualsense_watch PRIVATE ${dualsense_sdl})
  set_target_properties(dualsense_watch PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

  # The mod and the port each have their own copy of SDL3, and both open the same controller: the
  # two in one process. It needs a controller attached. It links no SDL of its own: the system's is
  # opened with dlopen and the mod's comes with the library, so the two really are two copies.
  add_executable(dualsense_two_instances tests/two_instances.cpp)
  target_link_libraries(dualsense_two_instances PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(dualsense_two_instances PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

  # The weapons' feels and what the game is doing with them.
  add_executable(dualsense_weapons_test tests/weapons_test.cpp src/weapons.cpp)
  target_include_directories(dualsense_weapons_test PRIVATE src)
  set_target_properties(dualsense_weapons_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME dualsense_weapons COMMAND dualsense_weapons_test)

  # The library as the port loads it, against a host table of the test's own.
  add_executable(dualsense_load_test tests/load_test.cpp)
  target_include_directories(dualsense_load_test PRIVATE src)
  target_link_libraries(dualsense_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  set_target_properties(dualsense_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_dependencies(dualsense_load_test dualsense)
  add_test(NAME dualsense_load COMMAND dualsense_load_test $<TARGET_FILE:dualsense>)
endif()
