# SPDX-License-Identifier: GPL-3.0-or-later
# Xbox button icons (README.md): the mod's library (C, on the SDK's openrac/openrac_mod.h), one for
# Ratchet & Clank and Going Commando (src/mod.c picks the game; src/rac1 and src/rac2 hold each
# game's addresses, src/common the painting), and its tests: painting on fonts made in the test, and
# the library as the port loads it, in each game. tools/preview.c paints a decompiled level's fonts
# into a picture, to see them without the game.
set(XBOX_ICONS_GAMES src/rac1/rac1.c src/rac2/rac2.c)
openrac_add_mod(xbox_icons MANIFEST openrac-mod.json
                SOURCES src/mod.c src/common/paint.c ${XBOX_ICONS_GAMES})
target_include_directories(xbox_icons PRIVATE src/common)
set_target_properties(xbox_icons PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(xbox_icons PRIVATE -Wall -Wextra)
endif()

if(BUILD_TESTING)
  add_executable(xbox_icons_paint_test tests/common/paint_test.c src/common/paint.c)
  target_include_directories(xbox_icons_paint_test PRIVATE src/common)
  set_target_properties(xbox_icons_paint_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  add_test(NAME xbox_icons_paint COMMAND xbox_icons_paint_test)

  # The library as the port loads it, against a host table and game memory of the test's own, in
  # each game; OPENRAC_RAC1_LEVEL / OPENRAC_RAC2_LEVEL=<decompiled level> try the game's own fonts too.
  foreach(game rac1 rac2)
    add_executable(xbox_icons_${game}_load_test tests/${game}/load_test.c src/common/paint.c
                                                ${XBOX_ICONS_GAMES})
    target_include_directories(xbox_icons_${game}_load_test PRIVATE src/common tests/common)
    set_target_properties(xbox_icons_${game}_load_test PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(xbox_icons_${game}_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
    add_dependencies(xbox_icons_${game}_load_test xbox_icons)
    add_test(NAME xbox_icons_${game}_load COMMAND xbox_icons_${game}_load_test $<TARGET_FILE:xbox_icons>)
  endforeach()

  add_executable(xbox_icons_preview tools/preview.c src/common/paint.c)
  target_include_directories(xbox_icons_preview PRIVATE src/common)
  set_target_properties(xbox_icons_preview PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
endif()
