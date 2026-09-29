#!/usr/bin/env bash
# Baseline vs physics-driven global placement benchmarks on the in-repo
# sky130hd designs (test/gcd_sky130hd.tcl, aes, ibex).
#
# Usage: run_sky130hd_benchmarks.sh [options]
#   --openroad PATH   openroad binary (default: $OPENROAD_EXE, bazel-bin/openroad,
#                     build/src/openroad or `openroad` on PATH)
#   --designs LIST    comma separated subset of gcd,aes,ibex (default: gcd;
#                     designs not listed are reported as "not run")
#   --modes LIST      comma separated subset of baseline,physics (default: both)
#   --out DIR         output directory (default: <this dir>/results/benchmarks)
#   --threads N       placer thread count (default: all cores)
#   --stress          apply the STRESS thermal configuration (NOT stock):
#                     ambient 105 C, 50 K/W package, 4x activity
#   -h, --help
#
# Outputs: <out>/<design>_<mode>.{log,json,history.json,csv,status},
#          <out>/benchmarks.csv and <out>/benchmarks.md.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../../.." && pwd)"

all_designs=(gcd aes ibex)
designs="gcd"
modes="baseline,physics"
out_dir="$script_dir/results/benchmarks"
threads=""
stress=0
openroad="${OPENROAD_EXE:-}"

usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | grep '^#' | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --openroad) openroad="$2"; shift 2 ;;
    --designs) designs="$2"; shift 2 ;;
    --modes) modes="$2"; shift 2 ;;
    --out) out_dir="$2"; shift 2 ;;
    --threads) threads="$2"; shift 2 ;;
    --stress) stress=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$openroad" ]]; then
  for candidate in "$repo_root/bazel-bin/openroad" "$repo_root/build/src/openroad"; do
    if [[ -x "$candidate" ]]; then openroad="$candidate"; break; fi
  done
fi
if [[ -z "$openroad" ]]; then
  openroad="$(command -v openroad || true)"
fi
if [[ -z "$openroad" || ! -x "$openroad" ]]; then
  echo "error: openroad binary not found; use --openroad PATH" >&2
  exit 1
fi

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
config="stock"
if [[ $stress -eq 1 ]]; then
  config="stress"
  echo "NOTE: --stress applies a NON-STOCK thermal configuration"
fi
echo "openroad: $openroad"
echo "output:   $out_dir"

IFS=',' read -r -a design_list <<< "$designs"
IFS=',' read -r -a mode_list <<< "$modes"

for design in "${all_designs[@]}"; do
  for mode in baseline physics; do
    tag="${design}_${mode}"
    rm -f "$out_dir/$tag.csv" "$out_dir/$tag.status"
    run=0
    for d in "${design_list[@]}"; do [[ "$d" == "$design" ]] && run=1; done
    for m in "${mode_list[@]}"; do [[ "$m" == "$mode" ]] || continue; run=$((run + 1)); done
    if [[ $run -ne 2 ]]; then
      echo "not run (skipped)" > "$out_dir/$tag.status"
      continue
    fi
    echo "=== $design / $mode / $config ==="
    if ! BENCH_DESIGN="$design" BENCH_MODE="$mode" BENCH_OUT="$out_dir" \
        BENCH_STRESS="$stress" BENCH_THREADS="$threads" \
        "$openroad" -no_splash -no_init -exit "$script_dir/bench_sky130hd.tcl" \
        > "$out_dir/$tag.log" 2>&1; then
      echo "failed (see $out_dir/$tag.log)" > "$out_dir/$tag.status"
      echo "    FAILED, see $out_dir/$tag.log"
    else
      echo "    $(cat "$out_dir/$tag.status")"
    fi
  done
done

python3 "$script_dir/summarize_benchmarks.py" --out-dir "$out_dir" \
  --config "$config" --csv "$out_dir/benchmarks.csv" --md "$out_dir/benchmarks.md"
echo
cat "$out_dir/benchmarks.md"
