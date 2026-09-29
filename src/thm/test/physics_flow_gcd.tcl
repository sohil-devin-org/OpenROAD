# End-to-end physics flow on the hermetic Nangate45 gcd design:
# baseline analysis -> history/report/maps -> derated timing -> delta map.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

proc check { name cond } {
  if { $cond } {
    puts "PASS: $name"
  } else {
    puts "FAIL: $name"
  }
}

set_thermal_config -grid {16 16} -no_ir_drop -report

# 1. Baseline analysis.
analyze_thermal -label baseline
set ambient 25.0
set peak [thm::get_peak_temperature]
set total_power [thm::get_total_power]
check "peak temperature >= ambient" [expr { $peak >= $ambient }]
check "total power > 0" [expr { $total_power > 0.0 }]
check "history size == 1" [expr { [thm::get_history_size] == 1 }]
check "converged" [thm::thermal_converged]
check "no runaway" [expr { ![thm::thermal_runaway] }]

# 2. History and JSON report.
set history_file [make_result_file physics_flow_gcd_baseline.json]
write_physics_history $history_file
check "history file written" [expr { [file size $history_file] > 0 }]

set report_file [make_result_file physics_flow_gcd_report.json]
report_physics -json $report_file
set fh [open $report_file r]
set report [read $fh]
close $fh
foreach key {peak_temp_c avg_temp_c total_power_w leakage_power_w
             wns_nominal_s wns_derated_s hpwl_um} {
  check "report has $key" [regexp "\"$key\"" $report]
}
if { [regexp {"leakage_power_w"\s*:\s*([-0-9.eE+]+)} $report -> leakage] } {
  check "leakage <= total power" [expr { $leakage <= $total_power }]
} else {
  puts "FAIL: leakage_power_w missing from report"
}

# 3. Maps.
foreach map {temperature leakage derate} {
  set map_file [make_result_file physics_flow_gcd_$map.map]
  write_thermal_map -file $map_file -map $map
  check "$map map written" [expr { [file size $map_file] > 0 }]
}
diff_files physics_flow_gcd_temperature.mapok \
  [make_result_file physics_flow_gcd_temperature.map]

# 4. Derated timing.
set_physics_derating -enable
puts "derating [thm::physics_derating_enabled]"
report_worst_slack -max
report_worst_slack -min
set_physics_derating -disable

# 5. Compare against the saved baseline.
load_physics_reference $history_file
set delta_file [make_result_file physics_flow_gcd_delta.map]
write_thermal_map -file $delta_file -map delta
diff_files physics_flow_gcd_delta.mapok $delta_file
load_physics_reference -clear
