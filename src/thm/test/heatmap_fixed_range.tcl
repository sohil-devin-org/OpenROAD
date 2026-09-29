# Temperature heat map: FixedRange/FixedMin/FixedMax pin the colour range to
# exactly [FixedMin, FixedMax]; cells outside clamp to the end values/colours.
source "helpers.tcl"

proc fail { msg } {
  puts "fail: $msg"
  exit 0
}

proc check_double { name value expected } {
  if { abs($value - $expected) > 1e-3 } {
    fail "$name is $value, expected $expected"
  }
}

# Dump the Temperature map and return its {min max count_min count_max}.
proc dump_range { name } {
  set dump_file [make_result_file $name.csv]
  gui::dump_heatmap Temperature $dump_file
  set f [open $dump_file r]
  gets $f header
  set min 1e9
  set max -1e9
  set values {}
  while { [gets $f line] >= 0 } {
    if { $line eq "" } {
      continue
    }
    set value [lindex [split $line ,] 4]
    lappend values $value
    if { $value < $min } {
      set min $value
    }
    if { $value > $max } {
      set max $value
    }
  }
  close $f
  set count_min 0
  set count_max 0
  foreach value $values {
    if { abs($value - $min) < 1e-3 } {
      incr count_min
    }
    if { abs($value - $max) < 1e-3 } {
      incr count_max
    }
  }
  puts "$name: min $min ($count_min cells), max $max ($count_max cells)"
  return [list $min $max $count_min $count_max]
}

if { ![gui::supported] } {
  # Built without the GUI; nothing to exercise.
  puts "pass"
} else {
  read_lef sky130hd/sky130hd.tlef
  read_lef sky130hd/sky130hd_std_cell.lef
  read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
  read_def sky130hd_data/gcd_sky130hd_placed.def
  read_sdc sky130hd_data/gcd_sky130hd_placed.sdc

  # Grid values span 30..80 degC.
  thm::read_temperature_grid_cmd heatmap_fixed_range.csv

  puts "FixedRange default: [gui::get_heatmap_bool Temperature FixedRange]"
  puts "FixedMin default: [gui::get_heatmap_double Temperature FixedMin]"
  puts "FixedMax default: [gui::get_heatmap_double Temperature FixedMax]"
  if { [gui::get_heatmap_bool Temperature FixedRange] } {
    fail "FixedRange must be off by default"
  }

  # Default: the range follows the real grid min/max.
  lassign [dump_range auto_range] min max count_min count_max
  check_double "auto min" $min 30.0
  check_double "auto max" $max 80.0

  gui::set_heatmap Temperature FixedRange 1
  gui::set_heatmap Temperature FixedMin 40
  gui::set_heatmap Temperature FixedMax 60

  set fixed_range [gui::get_heatmap_bool Temperature FixedRange]
  set fixed_min [gui::get_heatmap_double Temperature FixedMin]
  set fixed_max [gui::get_heatmap_double Temperature FixedMax]
  puts "FixedRange: $fixed_range FixedMin: $fixed_min FixedMax: $fixed_max"
  if { !$fixed_range } {
    fail "FixedRange did not stick"
  }
  check_double "FixedMin" $fixed_min 40.0
  check_double "FixedMax" $fixed_max 60.0

  # The colour scale spans the full display range, which now maps exactly to
  # [FixedMin, FixedMax].
  check_double "DisplayMin" [gui::get_heatmap_double Temperature DisplayMin] 0.0
  check_double "DisplayMax" \
    [gui::get_heatmap_double Temperature DisplayMax] 100.0

  # 30, 35, 38 and 40 clamp to the minimum; 60, 62, 65, 70, 75 and 80 to the
  # maximum.
  lassign [dump_range fixed_range] min max count_min count_max
  check_double "fixed min" $min 40.0
  check_double "fixed max" $max 60.0
  if { $count_min != 4 || $count_max != 6 } {
    fail "expected 4 cells at the minimum and 6 at the maximum"
  }

  # Widening the fixed range keeps the map in sync without an explicit rebuild.
  gui::set_heatmap Temperature FixedMin 20
  gui::set_heatmap Temperature FixedMax 90
  lassign [dump_range wide_range] min max count_min count_max
  check_double "wide min" $min 30.0
  check_double "wide max" $max 80.0

  # Turning the fixed range off restores the automatic range.
  gui::set_heatmap Temperature FixedRange 0
  lassign [dump_range auto_range_again] min max count_min count_max
  check_double "auto min" $min 30.0
  check_double "auto max" $max 80.0

  puts "pass"
}
