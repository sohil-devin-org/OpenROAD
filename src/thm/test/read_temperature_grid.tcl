# Load a temperature grid directly and check the statistics / report.
source "helpers.tcl"
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_def sky130hd_data/gcd_sky130hd_placed.def
read_sdc sky130hd_data/gcd_sky130hd_placed.sdc

thm::read_temperature_grid_cmd read_temperature_grid.csv
puts "has results: [thm::has_thermal_results]"
puts "peak: [format %.2f [thm::get_peak_temperature]]"
puts "average: [format %.2f [thm::get_average_temperature]]"

set report [make_result_file read_temperature_grid.rpt]
report_thermal -file $report
diff_files read_temperature_grid.rptok $report

set csv [make_result_file read_temperature_grid_out.csv]
write_temperature_map $csv
diff_files read_temperature_grid.csvok $csv

thm::clear_thermal_results
puts "has results: [thm::has_thermal_results]"
