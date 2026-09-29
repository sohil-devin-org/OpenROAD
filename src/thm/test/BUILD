# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2025, The OpenROAD Authors
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//test:regression.bzl", "regression_test")

package(features = ["layering_check"])

TESTS = [
    "analyze_thermal_gcd",
    "analyze_thermal_two_die",
    "thermal_config_errors",
    "thermal_map_gcd",
    "characterize_libraries_nangate45",
]

filegroup(
    name = "test_resources",
    srcs = glob(
        ["**/*"],
        exclude = [
            test + "." + ext
            for test in TESTS
            for ext in [
                "tcl",
                "py",
            ]
        ] + ["cpp/**"],
    ),
)

[regression_test(
    name = test_name,
    data = [":test_resources"],
) for test_name in TESTS]

cc_test(
    name = "thermal_core_test",
    srcs = [
        "cpp/ThermalCoreTest.cpp",
    ],
    deps = [
        "//src/odb/src/db",
        "//src/thm:thm_core",
        "//src/utl",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "finite_volume_solver_test",
    srcs = [
        "cpp/FiniteVolumeSolverTest.cpp",
    ],
    deps = [
        "//src/odb/src/db",
        "//src/thm:thm_core",
        "//src/utl",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "leakage_model_test",
    srcs = [
        "cpp/LeakageModelTest.cpp",
    ],
    deps = [
        "//src/thm:thm_core",
        "//src/utl",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
