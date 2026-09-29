# set_physics_derating must change the timing reports while enabled and
# restore the nominal results exactly when disabled.
# physics_derating_gcd_fits.json is a synthetic, test-only derate table that
# exercises the derate mechanics; it is not a library characterization.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

# A user derate that must survive the physics derates.
set_timing_derate -late 1.02

set_thermal_config -grid {8 8} -ambient 85 -nominal_vdd 1.1 \
  -derate_fits physics_derating_gcd_fits.json
analyze_thermal -no_ir_drop -label derating

proc wns { } {
  return [sta::worst_slack_cmd "max"]
}
proc tns { } {
  return [sta::total_negative_slack_cmd "max"]
}

puts "nominal"
report_worst_slack -max
report_tns
set wns0 [wns]
set tns0 [tns]

set_physics_derating -enable
puts "derated"
report_worst_slack -max
report_tns
set wns1 [wns]
if { $wns1 < $wns0 } {
  puts "PASS: derated WNS is worse than nominal"
} else {
  puts "FAIL: derated WNS $wns1 is not worse than nominal $wns0"
}
# Re-applying identical derates is a no-op.
set_physics_derating -enable
set wns1b [wns]
if { $wns1b == $wns1 } {
  puts "PASS: re-enabling does not change the derated timing"
} else {
  puts "FAIL: re-enable changed WNS $wns1 -> $wns1b"
}

set_physics_derating -disable
puts "restored"
report_worst_slack -max
report_tns
set wns2 [wns]
set tns2 [tns]
if { $wns2 == $wns0 && $tns2 == $tns0 } {
  puts "PASS: nominal timing restored exactly"
} else {
  puts "FAIL: nominal WNS/TNS $wns0/$tns0 != restored $wns2/$tns2"
}
if { [thm::get_derated_wns] < [thm::get_nominal_wns] } {
  puts "PASS: report_physics derated WNS < nominal WNS"
} else {
  puts "FAIL: report_physics derated WNS not below nominal"
}
