#!/usr/bin/env python3
"""Merge the per-run CSV rows written by bench_sky130hd.tcl into one CSV and a
Markdown table.  Standard library only.

    summarize_benchmarks.py --out-dir DIR [--csv FILE] [--md FILE]
                            [--config stock|stress]

Design/mode combinations without a CSV row are reported as "not run" with the
reason from the matching .status file (skipped, flag unavailable, failed).
"""

import argparse
import csv
import os
import sys

DESIGNS = ["gcd", "aes", "ibex"]
MODES = ["baseline", "physics"]
COLUMNS = [
    "design",
    "mode",
    "config",
    "status",
    "hpwl_um",
    "peak_temp_c",
    "avg_temp_c",
    "max_gradient_c_per_mm",
    "leakage_power_w",
    "worst_ir_drop_v",
    "wns_nominal_s",
    "wns_derated_s",
    "tns_nominal_s",
    "tns_derated_s",
    "runtime_s",
]
HEADERS = {
    "design": "Design",
    "mode": "Mode",
    "config": "Config",
    "status": "Status",
    "hpwl_um": "HPWL (um)",
    "peak_temp_c": "Peak T (C)",
    "avg_temp_c": "Avg T (C)",
    "max_gradient_c_per_mm": "Max grad (C/mm)",
    "leakage_power_w": "Leakage (W)",
    "worst_ir_drop_v": "Worst IR drop (V)",
    "wns_nominal_s": "WNS nom (s)",
    "wns_derated_s": "WNS derated (s)",
    "tns_nominal_s": "TNS nom (s)",
    "tns_derated_s": "TNS derated (s)",
    "runtime_s": "Runtime (s)",
}


def read_status(out_dir, tag):
    path = os.path.join(out_dir, tag + ".status")
    if not os.path.exists(path):
        return "not run"
    with open(path, encoding="utf-8") as handle:
        text = handle.read().strip()
    return text or "not run"


def collect(out_dir, config):
    rows = []
    for design in DESIGNS:
        for mode in MODES:
            tag = f"{design}_{mode}"
            csv_path = os.path.join(out_dir, tag + ".csv")
            row = None
            if os.path.exists(csv_path):
                with open(csv_path, newline="", encoding="utf-8") as handle:
                    for record in csv.DictReader(handle):
                        row = {col: record.get(col, "") for col in COLUMNS}
            if row is None or row.get("status") != "ok":
                row = {col: "" for col in COLUMNS}
                row.update(design=design, mode=mode, config=config)
                row["status"] = read_status(out_dir, tag)
            rows.append(row)
    return rows


def fmt(value, column):
    if value in ("", None):
        return ""
    try:
        number = float(value)
    except ValueError:
        return str(value)
    if column in ("hpwl_um",):
        return f"{number:.1f}"
    if column in ("peak_temp_c", "avg_temp_c", "max_gradient_c_per_mm"):
        return f"{number:.2f}"
    if column in ("runtime_s",):
        return f"{number:.1f}"
    return f"{number:.4g}"


def markdown(rows, config):
    lines = []
    lines.append(f"## sky130hd physics-driven placement benchmarks ({config})")
    lines.append("")
    if config == "stress":
        lines.append(
            "**Stress configuration (not stock):** ambient 105 C, "
            "50 K/W package (no heat sink), 4x switching activity."
        )
    else:
        lines.append("Stock thermal configuration (`set_thermal_config` defaults).")
    lines.append("")
    lines.append("| " + " | ".join(HEADERS[c] for c in COLUMNS) + " |")
    lines.append("|" + "|".join(" --- " for _ in COLUMNS) + "|")
    for row in rows:
        cells = []
        for column in COLUMNS:
            value = row.get(column, "")
            if row["status"] != "ok" and column not in ("design", "mode", "config", "status"):
                value = "not run" if column == "hpwl_um" else ""
            cells.append(fmt(value, column))
        lines.append("| " + " | ".join(cells) + " |")
    lines.append("")

    # Physics-driven minus baseline deltas where both runs exist.
    delta_cols = ["hpwl_um", "peak_temp_c", "avg_temp_c", "leakage_power_w", "wns_derated_s"]
    deltas = []
    by_key = {(r["design"], r["mode"]): r for r in rows}
    for design in DESIGNS:
        base = by_key.get((design, "baseline"))
        phys = by_key.get((design, "physics"))
        if not base or not phys or base["status"] != "ok" or phys["status"] != "ok":
            continue
        cells = [design]
        for column in delta_cols:
            try:
                diff = float(phys[column]) - float(base[column])
                cells.append(fmt(diff, column))
            except ValueError:
                cells.append("")
        deltas.append(cells)
    if deltas:
        lines.append("### physics - baseline")
        lines.append("")
        lines.append("| Design | " + " | ".join("d " + HEADERS[c] for c in delta_cols) + " |")
        lines.append("|" + "|".join(" --- " for _ in range(len(delta_cols) + 1)) + "|")
        for cells in deltas:
            lines.append("| " + " | ".join(cells) + " |")
        lines.append("")
    return "\n".join(lines)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", required=True, help="benchmark output directory")
    parser.add_argument("--config", default="stock", choices=["stock", "stress"])
    parser.add_argument("--csv", help="merged CSV output (default: <out-dir>/benchmarks.csv)")
    parser.add_argument("--md", help="Markdown output (default: stdout)")
    args = parser.parse_args(argv)

    rows = collect(args.out_dir, args.config)
    csv_path = args.csv or os.path.join(args.out_dir, "benchmarks.csv")
    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS)
        writer.writeheader()
        writer.writerows(rows)

    text = markdown(rows, args.config)
    if args.md:
        with open(args.md, "w", encoding="utf-8") as handle:
            handle.write(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
