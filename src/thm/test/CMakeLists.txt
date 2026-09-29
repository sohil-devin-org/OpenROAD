# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
include("openroad")

or_integration_tests(
  "thm"
  TESTS
    analyze_thermal_errors
    analyze_thermal_fake
    read_temperature_grid
  PASSFAIL_TESTS
    heatmap_dump
    heatmap_fixed_range
)
