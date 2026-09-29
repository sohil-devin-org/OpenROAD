# Non-hermetic physics-driven placement demo on the Nangate45 gcd design.
#
# The floorplan (rows, pins, power grid) is already in Nangate45_data/gcd.def,
# so the flow is:
#   global_placement (baseline) -> analyze_thermal -> write_physics_history
#   global_placement -physics_driven -> analyze_thermal -> report_physics -json
#   -> write_physics_animation -type compare
#
# Run from any directory:
#   openroad -exit src/thm/test/flow/physics_placement_demo.tcl
# Outputs go to $THM_DEMO_OUT (default: src/thm/test/flow/results).
#
# This script is not part of the regression suite: placement and IR-drop
# runtimes and the animation stub make the log non-deterministic.

set flow_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $flow_dir]
set out_dir $flow_dir/results
if { [info exists ::env(THM_DEMO_OUT)] } {
  set out_dir $::env(THM_DEMO_OUT)
}
file mkdir $out_dir

read_lef $test_dir/Nangate45/Nangate45.lef
read_def $test_dir/Nangate45_data/gcd.def
read_liberty $test_dir/Nangate45/Nangate45_typ.lib
read_sdc $test_dir/Nangate45_data/gcd.sdc

# Stock thermal configuration; only the grid is reduced to keep the demo fast.
set_thermal_config -grid {32 32} -report

################################################################
# Baseline placement and analysis.
global_placement
analyze_thermal -label baseline
report_physics
write_physics_history $out_dir/gcd_baseline.json
write_thermal_map -file $out_dir/gcd_baseline_temperature.map -map temperature
puts "baseline: peak [format %.2f [thm::get_peak_temperature]] C,\
  history size [thm::get_history_size]"

################################################################
# Physics-driven placement.  The -physics_driven flag is added by the gpl
# integration; report clearly if this binary does not have it yet.
set physics_driven_ok 1
if { [catch { global_placement -physics_driven } err] } {
  set physics_driven_ok 0
  puts "WARNING: 'global_placement -physics_driven' is not available in this\
    build ($err); skipping the physics-driven placement pass and analyzing\
    the baseline placement a second time."
}
analyze_thermal -label physics_driven
report_physics -json $out_dir/gcd_physics_driven.json
write_thermal_map -file $out_dir/gcd_physics_driven_temperature.map \
  -map temperature
puts "physics_driven: peak [format %.2f [thm::get_peak_temperature]] C,\
  history size [thm::get_history_size]"

################################################################
# Baseline vs. physics-driven comparison.
load_physics_reference $out_dir/gcd_baseline.json
write_thermal_map -file $out_dir/gcd_delta_temperature.map -map delta
if { [catch {
  write_physics_animation -type compare -file $out_dir/gcd_compare.gif
} err] } {
  puts "WARNING: write_physics_animation failed: $err"
}
load_physics_reference -clear

if { $physics_driven_ok } {
  puts "physics_placement_demo: done (physics-driven placement ran)"
} else {
  puts "physics_placement_demo: done (physics-driven placement NOT available)"
}
