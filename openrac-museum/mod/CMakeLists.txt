# SPDX-License-Identifier: GPL-3.0-or-later
# The OpenRAC Museum's mod (../README.md): two C files on the SDK's openrac/openrac_mod.h. Its data
# (src/museum_data.h) is written by ../tools/mod_data.py from the museum's exhibits, its effects'
# sprites (fx/) by ../tools/fx.py.
openrac_add_mod(openrac_museum MANIFEST openrac-mod.json SOURCES src/museum_mod.c src/museum_fx.c)
# The effects' sprites (../tools/fx.py), read by the mod as props.
get_target_property(museum_package openrac_museum OPENRAC_MOD_PACKAGE_DIR)
file(COPY fx DESTINATION "${museum_package}")
target_include_directories(openrac_museum PRIVATE src)
set_target_properties(openrac_museum PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(openrac_museum PRIVATE -Wall -Wextra)
  target_link_libraries(openrac_museum PRIVATE m)
endif()
