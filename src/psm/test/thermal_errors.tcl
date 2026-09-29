# analyze_thermal error handling.
source "helpers.tcl"

proc check_error { script } {
  if { [catch { uplevel 1 $script } err] } {
    puts "caught: $err"
  } else {
    puts "no error: $script"
  }
}

check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {4 4} }

read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_def sky130hd_data/insert_decap_gcd.def
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_sdc sky130hd_data/gcd_sky130hd_floorplan.sdc

# No result yet.
check_error { psm::thermal_peak }

# Bad -grid.
check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {4} }
check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {3 4} }
check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {0 4} }

# Grid file that does not match -grid, or does not exist.
check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {8 8} }
check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {2 2} }
check_error { analyze_thermal -grid_file does_not_exist.grid -grid {4 4} }

# Missing HotSpot.
unset -nocomplain ::env(HOTSPOT)
check_error { analyze_thermal -hotspot ./does_not_exist/hotspot }
set ::env(HOTSPOT) does_not_exist_hotspot
check_error { analyze_thermal }
unset ::env(HOTSPOT)

# Missing user config.
check_error {
  analyze_thermal -hotspot_config does_not_exist.config \
    -grid_file thermal_grid_file.grid -grid {4 4}
}

# HotSpot exits with an error: the log tail is reported.
set fake [file join [make_result_dir] thermal_errors-tcl.sh]
set in [open thermal_fake_hotspot.sh r]
set out [open $fake w]
puts -nonewline $out [read $in]
close $in
close $out
file attributes $fake -permissions 0755
# The messages contain paths that depend on the results directory.
suppress_message PSM 220
suppress_message PSM 221
set work_dir [file join [make_result_dir] thermal_errors-tcl]
set ::env(FAKE_HOTSPOT_FAIL) 1
check_error { analyze_thermal -hotspot $fake -work_dir $work_dir -grid {4 4} }
unset ::env(FAKE_HOTSPOT_FAIL)
report_file [file join $work_dir hotspot.log]

# Result accessor outside the die.
analyze_thermal -grid_file thermal_grid_file.grid -grid {4 4} -max_instances 1
check_error { psm::thermal_temperature_at 1000 1000 }

# No placement.
foreach inst [[ord::get_db_block] getInsts] {
  $inst setPlacementStatus NONE
}
check_error { analyze_thermal -grid_file thermal_grid_file.grid -grid {4 4} }
