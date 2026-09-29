# analyze_thermal end to end with a fake HotSpot: checks the generated
# floorplan / power trace and the grid steady parsing.
source "helpers.tcl"
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_def sky130hd_data/gcd_sky130hd_placed.def
read_sdc sky130hd_data/gcd_sky130hd_placed.sdc

# THM-0012 reports the (machine dependent) work directory.
suppress_message THM 12

set work_dir [make_result_test_dir analyze_thermal_fake]
analyze_thermal -hotspot_binary [file join [pwd] fake_hotspot.py] \
  -work_dir $work_dir -keep_files \
  -grid_rows 16 -grid_cols 16 -tile_size 20 -report_instances 5

puts "has results: [thm::has_thermal_results]"
puts "peak: [format %.2f [thm::get_peak_temperature]]"
puts "average: [format %.2f [thm::get_average_temperature]]"

report_thermal

set csv [make_result_file analyze_thermal_fake.csv]
write_temperature_map $csv
diff_files analyze_thermal_fake.csvok $csv

puts "--- floorplan"
report_file [file join $work_dir gcd.flp]
puts "--- power trace"
report_file [file join $work_dir gcd.ptrace]
puts "--- config"
report_file [file join $work_dir gcd.config]
puts "--- log"
report_file [file join $work_dir gcd.hotspot.log]
