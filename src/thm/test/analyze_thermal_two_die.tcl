# Two-die stack: gcd on the bottom die with a uniformly powered second die
# on top, solved with the finite-volume solver.  report_physics lists the
# per-die peaks (die 1 carries 0.05 W against ~0.14 mW for gcd, so it must be
# the hotter one and die 0 must be heated through the bond layer).
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

set_thermal_config -two_die -second_die_power 0.05 -grid {16 16} -no_ir_drop -report
analyze_thermal -label two_die
report_physics

set peak [thm::get_peak_temperature]
if { $peak <= 25.0 } {
  puts "FAIL: peak temperature $peak is not above ambient"
} else {
  puts "PASS: peak temperature above ambient"
}
puts "converged [thm::thermal_converged] runaway [thm::thermal_runaway]"
