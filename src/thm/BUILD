# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025, The OpenROAD Authors

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//bazel:python_wrap_cc.bzl", "python_wrap_cc")
load("//bazel:tcl_encode_or.bzl", "tcl_encode")
load("//bazel:tcl_wrap_cc.bzl", "tcl_wrap_cc")
load("//test:regression.bzl", "messages_txt")

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

# Physics engine without Tcl / GUI dependencies; unit tests depend on this.
cc_library(
    name = "thm_core",
    srcs = [
        "src/FiniteVolumeSolver.cpp",
        "src/LeakageModel.cpp",
        "src/PhysicsState.cpp",
        "src/ThermalConfig.cpp",
        "src/ThermalGrid.cpp",
        "src/ThermalSolver.cpp",
    ],
    hdrs = [
        "include/thm/LeakageModel.h",
        "include/thm/PhysicsState.h",
        "include/thm/ThermalConfig.h",
        "include/thm/ThermalGrid.h",
        "include/thm/ThermalSolver.h",
    ],
    includes = [
        "include",
    ],
    deps = [
        "//src/dbSta",
        "//src/odb/src/db",
        "//src/sta:opensta_lib",
        "//src/utl",
        "@boost.json",
    ],
)

cc_library(
    name = "thm",
    srcs = [
        "src/PhysicsCoupling.cpp",
        "src/PlacementPhysics.cpp",
        "src/Thermal.cpp",
        "src/animation.cpp",
        "src/heatMap.cpp",
        "src/heatMap.h",
    ],
    hdrs = [
        "include/thm/Animation.h",
        "include/thm/PhysicsCoupling.h",
        "include/thm/PlacementPhysics.h",
        "include/thm/Thermal.h",
    ],
    includes = [
        "include",
        "src",
    ],
    deps = [
        ":thm_core",
        "//src/dbSta",
        "//src/dbSta:dbNetwork",
        "//src/gpl",
        "//src/odb/src/db",
        "//src/psm",
        "//src/sta:opensta_lib",
        "//src/utl",
        "//src/web:core",
        "@boost.json",
    ],
)

cc_library(
    name = "ui",
    srcs = [
        "src/MakeThermal.cpp",
        ":swig",
        ":tcl",
    ],
    hdrs = [
        "include/thm/MakeThermal.h",
    ],
    includes = [
        "include",
        "src",
    ],
    deps = [
        ":thm",
        "//:ord",
        "//src/dbSta",
        "//src/odb/src/db",
        "//src/sta:opensta_lib",
        "//src/utl",
        "@boost.stacktrace",
        "@tcl_lang//:tcl",
    ],
)

tcl_encode(
    name = "tcl",
    srcs = [
        "src/thermal.tcl",
    ],
    char_array_name = "thm_tcl_inits",
    namespace = "thm",
)

tcl_wrap_cc(
    name = "swig",
    srcs = [
        "src/thermal.i",
        "//:error_swig",
    ],
    module = "thm",
    namespace_prefix = "thm",
    root_swig_src = "src/thermal.i",
    swig_includes = [
        "src/thm/src",
    ],
    deps = [
        "//src/odb:swig",
    ],
)

python_wrap_cc(
    name = "swig-py",
    srcs = [
        "include/thm/PhysicsState.h",
        "include/thm/Thermal.h",
        "include/thm/ThermalConfig.h",
        "src/thermal-py.i",
        "//:error_swig-py",
    ],
    module = "thm_py",
    root_swig_src = "src/thermal-py.i",
    swig_includes = [
        "include",
        "src",
    ],
)

exports_files(["README.md"])

messages_txt(
    visibility = ["//docs:__pkg__"],
)
