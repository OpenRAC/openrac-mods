# SPDX-License-Identifier: GPL-3.0-or-later
# Ratchet & Clank's mods for OpenRAC's native port, each a folder (README.md).
#
# This is the top of the build: it finds the OpenRAC mod SDK and then adds each mod's folder, whose
# CMakeLists.txt builds the mod's library (through the SDK's openrac_add_mod) and its tests. The
# commands to build, test and package are:
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja
#   cmake --build build && ctest --test-dir build
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/* -o dist
cmake_minimum_required(VERSION 3.20)
project(rac1_mods LANGUAGES C CXX)

# The SDK: the release sdk.lock pins, which tools/fetch_sdk.py puts in .sdk/. OPENRAC_SDK=<dir> (an
# unpacked SDK, or sdk/ in a checkout of OpenRAC) or OpenRACMod_DIR=<dir>/cmake build against another.
# An explicit OpenRACMod_DIR wins; otherwise the SDK is found from OPENRAC_SDK or from sdk.lock.
if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  else()
    # The pinned SDK sits in .sdk/ under the version sdk.lock names.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    # Stop early, saying how to get the SDK, when it has not been fetched.
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
# The SDK provides openrac_add_mod and the OpenRAC::mod target the tests link against.
find_package(OpenRACMod 1.3 REQUIRED)

# CTest gives every mod's tests the BUILD_TESTING switch and the ctest command.
include(CTest)

# One folder per mod; each adds its own library target and tests.
add_subdirectory(lock-strafe)
add_subdirectory(item-randomizer)
add_subdirectory(difficulty-mod)
add_subdirectory(keyboard-mouse)
add_subdirectory(live-splits)
add_subdirectory(dualsense)
add_subdirectory(ammo-max)
add_subdirectory(xbox-icons)
# Multiplayer needs interface 1.7's pages of the game's own menus, 1.8's play calls and 1.9's world
# calls: built only with an SDK that has them (until sdk.lock pins 1.9.0, build it with
# -DOPENRAC_SDK=<OpenRAC>/sdk; an older SDK leaves it out).
if(OpenRACMod_VERSION VERSION_GREATER_EQUAL 1.9)
  add_subdirectory(multiplayer)
  add_subdirectory(archipelago)
else()
  message(STATUS "multiplayer, archipelago: skipped, they need the mod SDK 1.9 (this one is ${OpenRACMod_VERSION})")
endif()
