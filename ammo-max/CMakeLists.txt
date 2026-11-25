# SPDX-License-Identifier: GPL-3.0-or-later
# Preserve the original ammo-max dialog watcher and add Vendor Buy All for RAC1/RAC2.
# Vendor Buy All's imported source is MIT-licensed (see LICENSE-VENDOR-BUY-ALL).
openrac_add_mod(ammo_max MANIFEST openrac-mod.json SOURCES
  src/mod_entry.cpp
  src/ammo_max.cpp
  src/buy_all_rac1.cpp
  src/buy_all_rac2.cpp
)
target_include_directories(ammo_max PRIVATE src)
set_target_properties(ammo_max PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_compile_options(ammo_max PRIVATE -Wall -Wextra)
endif()

# Preserve Android's contextual Square control in every packaged platform build.
# Include the MIT notice with copies of the contributed implementation.
get_target_property(AMMO_MAX_PACKAGE_DIR ammo_max OPENRAC_MOD_PACKAGE_DIR)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/touch.json"
               "${AMMO_MAX_PACKAGE_DIR}/touch.json" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-VENDOR-BUY-ALL"
               "${AMMO_MAX_PACKAGE_DIR}/LICENSE-VENDOR-BUY-ALL" COPYONLY)

if(BUILD_TESTING)
  add_executable(ammo_max_count_test tests/count_test.cpp)
  target_include_directories(ammo_max_count_test PRIVATE src)
  set_target_properties(ammo_max_count_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  add_test(NAME ammo_max_count COMMAND ammo_max_count_test)

  add_executable(ammo_max_load_test tests/load_test.cpp)
  target_include_directories(ammo_max_load_test PRIVATE src)
  set_target_properties(ammo_max_load_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_link_libraries(ammo_max_load_test PRIVATE OpenRAC::mod ${CMAKE_DL_LIBS})
  add_dependencies(ammo_max_load_test ammo_max)
  add_test(NAME ammo_max_load COMMAND ammo_max_load_test $<TARGET_FILE:ammo_max>)

  add_executable(ammo_max_purchase_plan_test tests/purchase_plan_test.cpp)
  target_compile_features(ammo_max_purchase_plan_test PRIVATE cxx_std_17)
  target_include_directories(ammo_max_purchase_plan_test PRIVATE src)
  add_test(NAME ammo_max_purchase_plan COMMAND ammo_max_purchase_plan_test)

  add_executable(ammo_max_rac1_vendor_test tests/rac1_vendor_test.cpp)
  target_compile_features(ammo_max_rac1_vendor_test PRIVATE cxx_std_17)
  target_include_directories(ammo_max_rac1_vendor_test PRIVATE src)
  add_test(NAME ammo_max_rac1_vendor COMMAND ammo_max_rac1_vendor_test)

  add_executable(ammo_max_rac1_runtime_test tests/rac1_runtime_test.cpp src/buy_all_rac1.cpp)
  target_compile_features(ammo_max_rac1_runtime_test PRIVATE cxx_std_17)
  target_include_directories(ammo_max_rac1_runtime_test PRIVATE src)
  target_link_libraries(ammo_max_rac1_runtime_test PRIVATE OpenRAC::mod)
  add_test(NAME ammo_max_rac1_runtime_pal COMMAND ammo_max_rac1_runtime_test pal)
  add_test(NAME ammo_max_rac1_runtime_ntsc COMMAND ammo_max_rac1_runtime_test ntsc)
  add_test(NAME ammo_max_rac1_runtime_pal_ps COMMAND ammo_max_rac1_runtime_test pal playstation)
endif()
