# Animated GIF export of a transient thermal analysis (gcd, Nangate45).
source "helpers.tcl"

read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

# Two activity phases: a burst followed by an idle cool-down.  The gcd die is
# tiny, so its thermal time constant is well below a microsecond; the phase
# durations are chosen to yield a few hundred transient checkpoints.
set_thermal_config -grid {8 8} -no_ir_drop -phase {burst 3.0 2e-6}
set_thermal_config -phase {idle 0.2 5e-6}
analyze_thermal -transient

proc check_gif { file } {
  if { ![file exists $file] } {
    puts "FAIL: $file does not exist"
    return
  }
  set size [file size $file]
  if { $size <= 0 } {
    puts "FAIL: $file is empty"
    return
  }
  set fp [open $file rb]
  set header [read $fp 6]
  close $fp
  if { $header ne "GIF89a" } {
    puts "FAIL: $file header is not GIF89a"
    return
  }
  puts "OK: [file tail $file] is a non-empty GIF89a file"
}

set cooldown_gif [make_result_file write_physics_animation_gcd_cooldown.gif]
write_physics_animation -type cooldown -file $cooldown_gif
check_gif $cooldown_gif

set transient_gif [make_result_file write_physics_animation_gcd_transient.gif]
write_physics_animation -type transient -file $transient_gif -scale {25 60} -fps 10
check_gif $transient_gif

# Steady-state checkpoints only.
set electro_gif [make_result_file write_physics_animation_gcd_electrothermal.gif]
write_physics_animation -type electrothermal -file $electro_gif
check_gif $electro_gif
