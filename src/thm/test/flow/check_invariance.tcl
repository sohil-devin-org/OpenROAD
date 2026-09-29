# Default-behavior invariance check: `global_placement` without
# -physics_driven must place the Nangate45 gcd design bit-identically to
# origin/master.
#
#   openroad -exit src/thm/test/flow/check_invariance.tcl
#
# The golden check_invariance_gcd.defok was written with
# THM_INVARIANCE_REGEN=1 by an openroad built from the physics-driven-placement
# base branch (commit 5a311be7dc), whose src/gpl is identical to origin/master.
# Regenerate it the same way after an intentional change to default placement.

set flow_dir [file dirname [file normalize [info script]]]
set test_dir [file dirname $flow_dir]
source $test_dir/helpers.tcl

read_lef $test_dir/Nangate45/Nangate45.lef
read_def $test_dir/Nangate45_data/gcd.def
read_liberty $test_dir/Nangate45/Nangate45_typ.lib
read_sdc $test_dir/Nangate45_data/gcd.sdc

set_thread_count 1
global_placement

set golden $flow_dir/check_invariance_gcd.defok
if { [info exists ::env(THM_INVARIANCE_REGEN)] && $::env(THM_INVARIANCE_REGEN) } {
  write_def $golden
  puts "check_invariance: wrote golden $golden"
  exit 0
}

set def_file [make_result_file check_invariance_gcd.def]
write_def $def_file
puts "check_invariance: comparing $def_file against $golden"
if { [catch { diff_files $golden $def_file } err] } {
  puts "FAIL: default global_placement differs from golden: $err"
  exit 1
}
puts "PASS: default global_placement is identical to golden"
