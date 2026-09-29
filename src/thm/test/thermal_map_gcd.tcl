# Heat-map export, derating and history serialization.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

set_thermal_config -grid {8 8} -no_ir_drop
analyze_thermal -label before

set map_file [make_result_file thermal_map_gcd.map]
write_thermal_map -file $map_file -map temperature
diff_files thermal_map_gcd.mapok $map_file

set leak_file [make_result_file thermal_map_gcd_leak.map]
write_thermal_map -file $leak_file -map leakage -die 0

set history_file [make_result_file thermal_map_gcd.json]
write_physics_history $history_file
load_physics_reference $history_file
set delta_file [make_result_file thermal_map_gcd_delta.map]
write_thermal_map -file $delta_file -map delta
load_physics_reference -clear

set_physics_derating -enable
puts "derating [thm::physics_derating_enabled]"
report_checks -path_delay max -format end
set_physics_derating -disable
puts "derating [thm::physics_derating_enabled]"
