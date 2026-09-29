# analyze_thermal reading a checked-in HotSpot grid file (no HotSpot needed).
# HotSpot writes rows top (max y) first: the hottest cell of layer 0 is in the
# last HotSpot row, first column, so it must land in the lower-left corner of
# the die. Layer 1 (not the silicon layer) peaks in the upper-right corner and
# must be ignored.
source "helpers.tcl"
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_def sky130hd_data/insert_decap_gcd.def
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_sdc sky130hd_data/gcd_sky130hd_floorplan.sdc

set report_file [make_result_file thermal_grid_file.rpt]
analyze_thermal -grid_file thermal_grid_file.grid -grid {4 4} \
  -max_instances 5 -report_file $report_file

puts "peak [format %.2f [psm::thermal_peak]]"
puts "average [format %.2f [psm::thermal_average]]"
puts "min [format %.2f [psm::thermal_min]]"
puts "lower left [format %.2f [psm::thermal_temperature_at 5 5]]"
puts "lower right [format %.2f [psm::thermal_temperature_at 80 5]]"
puts "upper left [format %.2f [psm::thermal_temperature_at 5 80]]"
puts "upper right [format %.2f [psm::thermal_temperature_at 80 80]]"

set stream [open $report_file r]
set lines [split [string trim [read $stream]] "\n"]
close $stream
puts "report file header: [lindex $lines 0]"
puts "report file instances: [expr { [llength $lines] - 1 }]"
puts "report file first: [lindex $lines 1]"
