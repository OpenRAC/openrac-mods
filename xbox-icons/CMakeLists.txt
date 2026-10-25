# SPDX-License-Identifier: GPL-3.0-or-later
# Xbox button icons (README.md): the mod's library (C, on the SDK's openrac/openrac_mod.h) and its
# tests: painting on fonts made in the test, and the library as the port loads it. tools/preview.c
# paints a decompiled level's fonts into a picture, to see them without the game.
openrac_add_mod(xbox_icons MANIFEST openrac-mod.json SOURCES src/xbox_icons.c src/paint.c)
target_include_directories(xbox_icons PRIVATE src)
set_target_properties(xbox_icons PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(xbox_icons PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
  add_executable(xbox_icons_paint_test tests/paint_test.c src/paint.c)
  target_include_directories(xbox_icons_paint_test PRIVATE src)
  set_target_properties(xbox_icons_paint_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  add_test(NAME xbox_icons_paint COMMAND xbox_icons_paint_test)

  # The library as the port loads it, against a host table and game memory of the test's own;
  # OPENRAC_RAC1_LEVEL=<decompiled level> tries the game's own fonts too.
  add_executable(xbox_icons_load_test tests/load_test.c src/paint.c)
  target_include_directories(xbox_icons_load_test PRIVATE src)
  set_target_properties(xbox_icons_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_link_libraries(xbox_icons_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  add_dependencies(xbox_icons_load_test xbox_icons)
  add_test(NAME xbox_icons_load COMMAND xbox_icons_load_test $<TARGET_FILE:xbox_icons>)

  add_executable(xbox_icons_preview tools/preview.c src/paint.c)
  target_include_directories(xbox_icons_preview PRIVATE src)
  set_target_properties(xbox_icons_preview PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
endif()
