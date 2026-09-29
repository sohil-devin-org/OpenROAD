# Physics-driven global placement on gcd: power -> thermal -> IR drop ->
# derated timing checkpoints plus the screened-Poisson spreading force.
source helpers.tcl
set test_name physics01
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

set_wire_rc -signal -layer metal3
set_wire_rc -clock -layer metal5

set_thermal_config -grid {16 16} -ambient 45 -no_ir_drop

global_placement -physics_driven -timing_driven \
  -physics_checkpoint_interval 40 -physics_weight 0.5 -physics_grid 32

report_physics

set history [thm::get_history_size]
if { $history < 2 } {
  puts "FAIL: expected at least two physics checkpoints, got $history"
} else {
  puts "PASS: physics checkpoints recorded"
}
set peak [thm::get_peak_temperature]
if { $peak <= 45.0 } {
  puts "FAIL: peak temperature $peak is not above ambient"
} else {
  puts "PASS: peak temperature above ambient"
}

set def_file [make_result_file $test_name.def]
write_def $def_file
diff_files $def_file $test_name.defok
