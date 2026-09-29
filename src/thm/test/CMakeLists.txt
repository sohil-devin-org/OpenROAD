# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2025, The OpenROAD Authors

include("openroad")

or_integration_tests(
  "thm"
  TESTS
    analyze_thermal_gcd
    analyze_thermal_ir_gcd
    physics_derating_gcd
    thermal_config_errors
    thermal_map_gcd
)

add_executable(thermal_core_test cpp/ThermalCoreTest.cpp)

target_link_libraries(thermal_core_test
  GTest::gtest
  GTest::gtest_main
  thm_core
  utl_lib
)

gtest_discover_tests(thermal_core_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_dependencies(build_and_test thermal_core_test)

add_executable(electromigration_test cpp/ElectromigrationTest.cpp)

target_link_libraries(electromigration_test
  GTest::gtest
  GTest::gtest_main
  thm_lib
  thm_core
  utl_lib
)

gtest_discover_tests(electromigration_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_dependencies(build_and_test electromigration_test)
