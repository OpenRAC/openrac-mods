# SPDX-License-Identifier: GPL-3.0-or-later
# Ratchet & Clank's mods for OpenRAC's native port, each a folder (README.md).
#
#   python3 tools/fetch_sdk.py                     # the SDK sdk.lock pins, into .sdk/
#   cmake -S . -B build -G Ninja
#   cmake --build build && ctest --test-dir build
#   python3 .sdk/openrac-mod-sdk-*/tools/package_mod.py build/package/* -o dist
cmake_minimum_required(VERSION 3.20)
project(rac1_mods LANGUAGES C CXX)

# The SDK: the release sdk.lock pins, which tools/fetch_sdk.py puts in .sdk/. OPENRAC_SDK=<dir> (an
# unpacked SDK, or sdk/ in a checkout of OpenRAC) or OpenRACMod_DIR=<dir>/cmake build against another.
if(NOT OpenRACMod_DIR)
  if(DEFINED OPENRAC_SDK)
    set(OpenRACMod_DIR "${OPENRAC_SDK}/cmake")
  else()
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/sdk.lock" lock)
    string(JSON sdk_version GET "${lock}" version)
    set(OpenRACMod_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/openrac-mod-sdk-${sdk_version}/cmake")
    if(NOT EXISTS "${OpenRACMod_DIR}/OpenRACModConfig.cmake")
      message(FATAL_ERROR "No OpenRAC mod SDK ${sdk_version}: run python3 tools/fetch_sdk.py first "
                          "(or pass -DOPENRAC_SDK=<an unpacked SDK>)")
    endif()
  endif()
endif()
find_package(OpenRACMod 1.3 REQUIRED)

include(CTest)

add_subdirectory(lock-strafe)
add_subdirectory(item-randomizer)
add_subdirectory(difficulty-mod)
add_subdirectory(keyboard-mouse)
add_subdirectory(live-splits)
add_subdirectory(dualsense)
add_subdirectory(ammo-max)
