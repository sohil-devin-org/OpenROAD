# Two-die stack: gcd on the bottom die with a uniformly powered second die
# on top, solved with the finite-volume solver.  report_physics lists the
# per-die peaks.  Die 1 carries 0.05 W spread uniformly and sits directly
# under the 0.1 K/W heat sink, so its rise is a few mK; die 0 draws only
# ~0.14 mW but concentrated in a few tiles whose heat has to cross the
# low-conductivity BEOL and bond layers, so its hot spots rise more.
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
