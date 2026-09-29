# analyze_thermal / report_thermal error handling.
source "helpers.tcl"
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_def sky130hd_data/gcd_sky130hd_placed.def
read_sdc sky130hd_data/gcd_sky130hd_placed.sdc

# Missing binary (path and PATH lookup).
catch { analyze_thermal -hotspot_binary /no/such/dir/hotspot } msg
puts $msg
catch { analyze_thermal -hotspot_binary no_such_hotspot_binary } msg
puts $msg

# Report before any analysis.
report_thermal
puts "has results: [thm::has_thermal_results]"
catch { write_temperature_map [make_result_file analyze_thermal_errors.csv] } msg
puts $msg

# HotSpot exiting with a non-zero status.
set fail_hotspot [make_result_file fail_hotspot.sh]
set stream [open $fail_hotspot w]
puts $stream "#!/bin/sh"
puts $stream "echo 'fatal: simulated HotSpot failure' >&2"
puts $stream "exit 3"
close $stream
file attributes $fail_hotspot -permissions 0755

# THM-0012 / THM-0034 include machine dependent paths.
suppress_message THM 12
suppress_message THM 34
set work_dir [make_result_test_dir analyze_thermal_errors]
catch { analyze_thermal -hotspot_binary $fail_hotspot -work_dir $work_dir \
  -grid_rows 4 -grid_cols 4 } msg
puts $msg
puts "has results: [thm::has_thermal_results]"
puts "--- hotspot log"
report_file [file join $work_dir gcd.hotspot.log]

# Missing HotSpot config file.
catch { analyze_thermal -hotspot_binary $fail_hotspot \
  -hotspot_config /no/such/hotspot.config } msg
puts $msg
