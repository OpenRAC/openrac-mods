# SPDX-License-Identifier: GPL-3.0-or-later
# Linked statically into each mod: no runtime dependency on another mod or shared library.
add_library(controller_icons STATIC paint.c font_bank.c)
set_target_properties(controller_icons PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
                      POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
target_include_directories(controller_icons PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
