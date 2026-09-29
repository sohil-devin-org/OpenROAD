# Temperature heat map: load a synthetic temperature grid and check that the
# dumped heat map cells are exactly the grid cells in absolute degrees Celsius
# (the peak cell equals the CSV peak).
source "helpers.tcl"

proc fail { msg } {
  puts "fail: $msg"
  exit 0
}

# Parse a CSV of temperatures (bottom row first) into a flat list of values.
proc read_csv_values { path } {
  set values {}
  set f [open $path r]
  while { [gets $f line] >= 0 } {
    if { $line eq "" || [string index $line 0] eq "#" } {
      continue
    }
    foreach v [split $line ,] {
      lappend values [expr { double($v) }]
    }
  }
  close $f
  return $values
}

# Parse a gui::dump_heatmap CSV into a list of {x0 y0 x1 y1 value} rows.
proc read_dump { path } {
  set rows {}
  set f [open $path r]
  set header [gets $f]
  while { [gets $f line] >= 0 } {
    if { $line eq "" } {
      continue
    }
    lappend rows [split $line ,]
  }
  close $f
  return [list $header $rows]
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

  # No thermal results yet: the map is empty.
  if { [gui::get_heatmap_bool Temperature has_data] } {
    fail "Temperature map has data before any thermal results"
  }

  thm::read_temperature_grid_cmd heatmap_dump.csv

  set dump_file [make_result_file heatmap_dump.csv]
  gui::dump_heatmap Temperature $dump_file

  if { ![gui::get_heatmap_bool Temperature has_data] } {
    fail "Temperature map has no data after loading a grid"
  }

  set f [open $dump_file r]
  puts [string trim [read $f]]
  close $f

  lassign [read_dump $dump_file] header rows
  if { [string first "value (°C)" $header] < 0 } {
    fail "dump header is not in degrees Celsius: $header"
  }

  set csv_values [read_csv_values heatmap_dump.csv]
  if { [llength $rows] != [llength $csv_values] } {
    fail "expected [llength $csv_values] cells, dumped [llength $rows]"
  }

  # Every dumped value must be one of the CSV temperatures (no scaling).
  set csv_peak [tcl::mathfunc::max {*}$csv_values]
  set dump_peak -1e9
  set peak_row {}
  foreach row $rows {
    lassign $row x0 y0 x1 y1 value
    set found 0
    foreach v $csv_values {
      if { abs($v - $value) < 1e-3 } {
        set found 1
        break
      }
    }
    if { !$found } {
      fail "dumped value $value is not in the CSV"
    }
    if { $value > $dump_peak } {
      set dump_peak $value
      set peak_row $row
    }
  }
  puts "CSV peak: $csv_peak, dumped peak: $dump_peak"
  if { abs($csv_peak - $dump_peak) > 1e-3 } {
    fail "peak mismatch"
  }

  # The peak (row 2, col 2 of a 4x4 grid) is the third cell from the left and
  # the third from the bottom of the die area.
  set block [ord::get_db_block]
  set die [$block getDieArea]
  set dbu [expr { double([[ord::get_db_tech] getDbUnitsPerMicron]) }]
  set cx [expr { ([$die xMin] + 2.5 * [$die dx] / 4.0) / $dbu }]
  set cy [expr { ([$die yMin] + 2.5 * [$die dy] / 4.0) / $dbu }]
  lassign $peak_row x0 y0 x1 y1 value
  if { !($x0 < $cx && $cx < $x1 && $y0 < $cy && $cy < $y1) } {
    fail "peak cell $peak_row does not cover the expected grid cell ($cx, $cy)"
  }

  if { [diff_files heatmap_dump.csvok $dump_file] } {
    fail "dump differs from golden heatmap_dump.csvok"
  }

  puts "pass"
}
