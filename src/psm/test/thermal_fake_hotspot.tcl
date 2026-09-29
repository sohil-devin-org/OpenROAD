# analyze_thermal running a fake HotSpot (thermal_fake_hotspot.sh) that checks
# its arguments and writes a deterministic grid, so the generated HotSpot
# inputs are covered without HotSpot installed.
source "helpers.tcl"
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_def sky130hd_data/insert_decap_gcd.def
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_sdc sky130hd_data/gcd_sky130hd_floorplan.sdc

# Copy the fake so it is executable regardless of how the test data is staged
# (Bazel runfiles are read-only symlinks).
set fake [file join [make_result_dir] thermal_fake_hotspot-tcl.sh]
set in [open thermal_fake_hotspot.sh r]
set out [open $fake w]
puts -nonewline $out [read $in]
close $in
close $out
file attributes $fake -permissions 0755

# The HotSpot path in the run message depends on the results directory.
suppress_message PSM 221

set work_dir [file join [make_result_dir] thermal_fake_hotspot-tcl]
file delete -force $work_dir

analyze_thermal -hotspot $fake -work_dir $work_dir -grid {8 8} -ambient 25 \
  -max_instances 3

puts "--- design.flp"
report_file [file join $work_dir design.flp]
puts "--- hotspot.config"
report_file [file join $work_dir hotspot.config]
puts "--- hotspot.log"
report_file [file join $work_dir hotspot.log]

puts "--- design.ptrace"
set stream [open [file join $work_dir design.ptrace] r]
set names [gets $stream]
set powers [gets $stream]
close $stream
set total 0.0
foreach power $powers {
  set total [expr { $total + $power }]
}
puts "tiles: [llength $names] powers: [llength $powers]"
puts "first tile: [lindex $names 0] last tile: [lindex $names end]"
puts "total tile power: [format %.4e $total] W"

puts "lower left [format %.2f [psm::thermal_temperature_at 5 5]]"
puts "upper right [format %.2f [psm::thermal_temperature_at 85 85]]"

# The HOTSPOT environment variable and a user config are honored; without
# -work_dir the run happens in a temporary directory.
set ::env(HOTSPOT) $fake
analyze_thermal -hotspot_config [file join $work_dir hotspot.config] \
  -grid {4 4} -max_instances 1
puts "peak [format %.2f [psm::thermal_peak]]"
