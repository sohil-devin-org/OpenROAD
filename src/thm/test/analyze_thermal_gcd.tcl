# Steady-state thermal analysis of gcd with the default configuration.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

set_thermal_config -grid {16 16} -ambient 45 -no_ir_drop -report
analyze_thermal -label steady
report_physics

# Power is extracted from OpenSTA, so the peak must be above ambient.
set peak [thm::get_peak_temperature]
if { $peak <= 45.0 } {
  puts "FAIL: peak temperature $peak is not above ambient"
} else {
  puts "PASS: peak temperature above ambient"
}
puts "history size [thm::get_history_size]"
puts "converged [thm::thermal_converged] runaway [thm::thermal_runaway]"
