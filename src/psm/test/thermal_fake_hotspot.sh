#!/bin/sh
# Stand-in for HotSpot used by the thermal tests. It checks the arguments
# analyze_thermal passes and writes a deterministic grid temperature file.
echo "fake hotspot $*"

fail() {
  echo "fake hotspot error: $1"
  exit 1
}

if [ -n "$FAKE_HOTSPOT_FAIL" ]; then
  fail "failure requested by FAKE_HOTSPOT_FAIL"
fi

while [ $# -gt 0 ]; do
  [ $# -ge 2 ] || fail "missing value for $1"
  case "$1" in
    -c) config=$2 ;;
    -f) flp=$2 ;;
    -p) ptrace=$2 ;;
    -model_type) model=$2 ;;
    -grid_rows) rows=$2 ;;
    -grid_cols) cols=$2 ;;
    -ambient) ambient=$2 ;;
    -init_temp) init=$2 ;;
    -steady_file) steady=$2 ;;
    -grid_steady_file) grid_steady=$2 ;;
    *) fail "unexpected option $1" ;;
  esac
  shift 2
done

[ -f "$config" ] || fail "config file '$config' not found"
[ -f "$flp" ] || fail "floorplan file '$flp' not found"
[ -f "$ptrace" ] || fail "power trace file '$ptrace' not found"
[ "$model" = grid ] || fail "-model_type must be grid"
[ -n "$ambient" ] && [ "$init" = "$ambient" ] \
  || fail "-init_temp must equal -ambient"
[ -n "$rows" ] && [ -n "$cols" ] || fail "missing -grid_rows/-grid_cols"
[ -n "$steady" ] && [ -n "$grid_steady" ] || fail "missing output files"

blocks=$(grep -v '^#' "$flp" | grep -c .)
names=$(head -n 1 "$ptrace" | wc -w)
[ "$blocks" -eq "$names" ] \
  || fail "$blocks floorplan blocks but $names power trace columns"

if [ -n "$FAKE_HOTSPOT_NO_OUTPUT" ]; then
  echo "fake hotspot: no output requested by FAKE_HOTSPOT_NO_OUTPUT"
  exit 0
fi

# Layer 0 peaks in the bottom-left cell (HotSpot row rows-1, column 0).
# Layer 1 is a cooler, flat layer. Temperatures are in hundredths of Kelvin.
{
  echo "Layer 0:"
  i=0
  r=0
  while [ "$r" -lt "$rows" ]; do
    c=0
    while [ "$c" -lt "$cols" ]; do
      t=$((31815 + r * 100 + (cols - 1 - c) * 50))
      printf '%d\t%d.%02d\n' "$i" $((t / 100)) $((t % 100))
      i=$((i + 1))
      c=$((c + 1))
    done
    r=$((r + 1))
  done
  echo "Layer 1:"
  i=0
  while [ "$i" -lt $((rows * cols)) ]; do
    printf '%d\t%s\n' "$i" "$ambient"
    i=$((i + 1))
  done
} > "$grid_steady"
echo "fake steady" > "$steady"
exit 0
