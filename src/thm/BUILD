# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//bazel:tcl_encode_or.bzl", "tcl_encode")
load("//bazel:tcl_wrap_cc.bzl", "tcl_wrap_cc")
load("//test:regression.bzl", "messages_txt")

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "thm",
    srcs = [
        "src/MakeThermal.cpp",
        "src/Thermal.cpp",
        "src/heatMap.cpp",
        "src/heatMap.h",
        "src/hotspot.cpp",
        "src/hotspot.h",
        ":swig",
        ":tcl",
    ],
    hdrs = [
        "include/thm/MakeThermal.h",
        "include/thm/Thermal.h",
    ],
    includes = [
        "include",
        "src",
    ],
    deps = [
        "//:ord",
        "//src/dbSta",
        "//src/dbSta:dbNetwork",
        "//src/odb/src/db",
        "//src/sta:opensta_lib",
        "//src/utl",
        "//src/web:core",
        "@boost.multi_array",
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
        "//src/sta:sta_swig_files",
    ],
    module = "thm",
    namespace_prefix = "thm",
    root_swig_src = "src/thermal.i",
    swig_includes = [
        "../sta",
        "src/thm/src",
    ],
    deps = [
        "//src/odb:swig",
    ],
)

filegroup(
    name = "doc_files",
    srcs = [
        "README.md",
        "src/thermal.tcl",
    ],
    visibility = ["//src/thm/test:__pkg__"],
)

messages_txt(
    visibility = [
        "//docs:__pkg__",
        "//src/thm/test:__pkg__",
    ],
)
