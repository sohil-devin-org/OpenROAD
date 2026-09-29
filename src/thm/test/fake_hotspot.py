#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
#
# Deterministic stand-in for the HotSpot executable used by the thm
# regression tests.  It accepts the command line analyze_thermal generates,
# validates the floorplan and power trace the same way HotSpot does and writes
# a grid steady file (HotSpot layout: "Layer <n>:" sections with rows*cols
# "<index>\t<kelvin>" lines indexed row-major from the top-left corner) whose
# temperature is ambient + K_PER_WATT * power of the tile covering each cell.

import os
import sys

K_PER_WATT = 1e5
DEFAULT_AMBIENT_K = 318.15


def die(msg):
    sys.stderr.write("fake_hotspot: {}\n".format(msg))
    sys.exit(1)


def parse_args(argv):
    args = {}
    i = 0
    while i < len(argv):
        key = argv[i]
        if not key.startswith("-"):
            die("unexpected argument {}".format(key))
        if i + 1 >= len(argv):
            die("missing value for {}".format(key))
        args[key[1:]] = argv[i + 1]
        i += 2
    return args


def read_config(path):
    values = {}
    if not path:
        return values
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) >= 2 and parts[0].startswith("-"):
                values[parts[0][1:]] = parts[1]
    return values


def read_flp(path):
    units = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 5:
                die("invalid floorplan line: {}".format(line))
            name = parts[0]
            width, height, left, bottom = (float(v) for v in parts[1:5])
            if width <= 0 or height <= 0 or left < 0 or bottom < 0:
                die("invalid floorplan unit {}".format(name))
            units.append((name, width, height, left, bottom))
    if not units:
        die("empty floorplan")
    return units


def read_ptrace(path):
    with open(path) as f:
        lines = [l.rstrip("\n") for l in f if l.strip()]
    if len(lines) < 2:
        die("power trace needs a header and at least one sample")
    names = lines[0].split("\t")
    samples = [[float(v) for v in l.split("\t")] for l in lines[1:]]
    for sample in samples:
        if len(sample) != len(names):
            die("power trace sample has {} values for {} names".format(
                len(sample), len(names)))
    powers = [sum(s[i] for s in samples) / len(samples)
              for i in range(len(names))]
    return names, powers


def main():
    args = parse_args(sys.argv[1:])
    for required in ("f", "p", "grid_steady_file", "grid_rows", "grid_cols"):
        if required not in args:
            die("missing -{}".format(required))
    if args.get("model_type") != "grid":
        die("only the grid model is supported")

    config = read_config(args.get("c"))
    ambient = float(args.get("ambient", config.get("ambient",
                                                   DEFAULT_AMBIENT_K)))
    rows = int(args["grid_rows"])
    cols = int(args["grid_cols"])

    units = read_flp(args["f"])
    names, powers = read_ptrace(args["p"])
    if names != [u[0] for u in units]:
        die("no. of units in floorplan and trace file differ")
    power = dict(zip(names, powers))

    chip_w = max(u[3] + u[1] for u in units)
    chip_h = max(u[4] + u[2] for u in units)
    cell_w = chip_w / cols
    cell_h = chip_h / rows

    def power_at(x, y):
        for name, width, height, left, bottom in units:
            if left <= x < left + width and bottom <= y < bottom + height:
                return power[name]
        return 0.0

    grid = []
    for i in range(rows):  # from the top
        y = chip_h - (i + 0.5) * cell_h
        for j in range(cols):
            x = (j + 0.5) * cell_w
            grid.append(ambient + K_PER_WATT * power_at(x, y))

    print("Computing steady-state temperatures...")
    print("Parsing input files...")
    print("Creating thermal circuit...")
    with open(args["grid_steady_file"], "w") as f:
        # Layer 0 is silicon; the extra layers mimic HotSpot's package layers
        # and must be ignored by the reader.
        for layer in range(2):
            f.write("Layer {}:\n".format(layer))
            for index, temp in enumerate(grid):
                f.write("{}\t{:.2f}\n".format(index, temp if layer == 0 else
                                              ambient))
    if "steady_file" in args:
        with open(args["steady_file"], "w") as f:
            for name in names:
                f.write("{}\t{:.2f}\n".format(
                    name, ambient + K_PER_WATT * power[name]))
    print("Simulation complete.")


if __name__ == "__main__":
    main()
