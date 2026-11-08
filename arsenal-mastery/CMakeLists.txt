# SPDX-License-Identifier: MIT
# Initial native OpenRAC port of Arsenal Mastery.
openrac_add_mod(arsenal_mastery MANIFEST openrac-mod.json SOURCES src/arsenal_mastery.cpp)
set_target_properties(arsenal_mastery PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(arsenal_mastery PRIVATE -Wall -Wextra)
endif()
