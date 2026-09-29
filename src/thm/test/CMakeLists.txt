# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2025, The OpenROAD Authors

include("openroad")

or_integration_tests(
  "thm"
  TESTS
    analyze_thermal_gcd
    thermal_config_errors
    thermal_map_gcd
    characterize_libraries_nangate45
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

add_executable(leakage_model_test cpp/LeakageModelTest.cpp)

target_link_libraries(leakage_model_test
  GTest::gtest
  GTest::gtest_main
  thm_core
  utl_lib
)

gtest_discover_tests(leakage_model_test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_dependencies(build_and_test leakage_model_test)
