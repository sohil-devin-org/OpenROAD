# Coupled thermal + PDNSim IR-drop analysis of gcd with the voltage-source
# location file of the psm tests (analyze_power_grid -vsrc format).
# physics_derating_gcd_fits.json is a synthetic, test-only derate table that
# exercises the derate mechanics; it is not a library characterization.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

set_thermal_config -grid {16 16} -ambient 45 -nominal_vdd 1.1 \
  -vsrc Vsrc_gcd_vdd.loc -power_net VDD \
  -derate_fits physics_derating_gcd_fits.json -report
analyze_thermal -label ir

# The per-instance OpenSTA power must add up to the report_power total.
report_power
set sta_total [thm::get_sta_design_power]
set thm_total [thm::get_total_power]
if { abs($sta_total - $thm_total) <= 1e-3 * $sta_total } {
  puts "PASS: instance power sum matches the OpenSTA design total"
} else {
  puts "FAIL: instance power sum $thm_total != design total $sta_total"
}

set json_file [make_result_file analyze_thermal_ir_gcd.json]
report_physics -json $json_file
if { [file size $json_file] > 0 } {
  puts "PASS: json report written"
}

set map_file [make_result_file analyze_thermal_ir_gcd_ir.map]
write_thermal_map -file $map_file -map ir_drop
set fh [open $map_file r]
set map_max 0.0
while { [gets $fh line] >= 0 } {
  if { [string index $line 0] eq "#" } {
    continue
  }
  foreach v [split $line ", "] {
    if { [string is double -strict $v] && $v > $map_max } {
      set map_max $v
    }
  }
}
close $fh
if { $map_max > 0.0 } {
  puts "PASS: ir_drop map has a positive worst drop"
} else {
  puts "FAIL: ir_drop map is all zero"
}

set worst [thm::get_worst_ir_drop]
if { $worst > 0.0 } {
  puts "PASS: worst IR drop is positive"
} else {
  puts "FAIL: worst IR drop $worst"
}
set nominal [thm::get_nominal_wns]
set derated [thm::get_derated_wns]
if { $derated <= $nominal } {
  puts "PASS: derated WNS <= nominal WNS"
} else {
  puts "FAIL: derated WNS $derated > nominal WNS $nominal"
}
set em [thm::get_em_lifetime_factor]
if { $em > 0.0 && $em <= 1.0e6 } {
  puts "PASS: EM lifetime factor is finite and positive"
} else {
  puts "FAIL: EM lifetime factor $em"
}
