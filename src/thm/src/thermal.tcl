# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2025, The OpenROAD Authors

sta::define_cmd_args "set_thermal_config" { \
    [-ambient temp_c] \
    [-top_resistance k_per_w] \
    [-bottom_resistance k_per_w] \
    [-grid {nx ny}] \
    [-two_die] [-single_die] \
    [-bond_conductivity w_mk] \
    [-thinned_die_thickness m] \
    [-second_die_power watts] \
    [-activity_scale scale] \
    [-activity_file file] \
    [-phase {name scale duration_s}] \
    [-clear_phases] \
    [-loop_max_iterations n] \
    [-loop_peak_tolerance temp_c] \
    [-runaway_temperature temp_c] \
    [-nominal_vdd volts] \
    [-include_ir_drop] [-no_ir_drop] \
    [-leakage_fits file] \
    [-derate_fits file] \
    [-config_file file] \
    [-set key value] \
    [-report]}

proc set_thermal_config { args } {
  sta::parse_key_args "set_thermal_config" args \
    keys {-ambient -top_resistance -bottom_resistance -grid \
          -bond_conductivity -thinned_die_thickness -second_die_power \
          -activity_scale -activity_file -phase -loop_max_iterations \
          -loop_peak_tolerance -runaway_temperature -nominal_vdd \
          -leakage_fits -derate_fits -config_file -set} \
    flags {-two_die -single_die -clear_phases -include_ir_drop -no_ir_drop \
           -report}
  sta::check_argc_eq0 "set_thermal_config" $args

  if { [info exists keys(-config_file)] } {
    thm::read_config_file_cmd $keys(-config_file)
  }
  foreach {opt key} {
    -ambient ambient
    -top_resistance top_resistance
    -bottom_resistance bottom_resistance
    -bond_conductivity bond_conductivity
    -thinned_die_thickness thinned_die_thickness
    -second_die_power second_die_power
    -activity_scale activity_scale
    -activity_file activity_file
    -loop_max_iterations loop_max_iterations
    -loop_peak_tolerance loop_peak_tolerance
    -runaway_temperature runaway_temperature
    -nominal_vdd nominal_vdd
  } {
    if { [info exists keys($opt)] } {
      thm::set_config_value_cmd $key $keys($opt)
    }
  }
  if { [info exists keys(-grid)] } {
    set grid $keys(-grid)
    if { [llength $grid] != 2 } {
      utl::error THM 60 "-grid requires {nx ny}."
    }
    thm::set_config_value_cmd grid_x [lindex $grid 0]
    thm::set_config_value_cmd grid_y [lindex $grid 1]
  }
  if { [info exists flags(-two_die)] } {
    thm::set_config_value_cmd two_die 1
  }
  if { [info exists flags(-single_die)] } {
    thm::set_config_value_cmd two_die 0
  }
  if { [info exists flags(-include_ir_drop)] } {
    thm::set_config_value_cmd include_ir_drop 1
  }
  if { [info exists flags(-no_ir_drop)] } {
    thm::set_config_value_cmd include_ir_drop 0
  }
  if { [info exists flags(-clear_phases)] } {
    thm::clear_activity_phases_cmd
  }
  if { [info exists keys(-phase)] } {
    set phase $keys(-phase)
    if { [llength $phase] < 3 || [llength $phase] > 4 } {
      utl::error THM 61 "-phase requires {name scale duration_s ?activity_file?}."
    }
    set file ""
    if { [llength $phase] == 4 } {
      set file [lindex $phase 3]
    }
    thm::add_activity_phase_cmd [lindex $phase 0] [lindex $phase 1] $file \
      [lindex $phase 2]
  }
  if { [info exists keys(-set)] } {
    set kv $keys(-set)
    if { [llength $kv] != 2 } {
      utl::error THM 62 "-set requires {key value}."
    }
    thm::set_config_value_cmd [lindex $kv 0] [lindex $kv 1]
  }
  if { [info exists keys(-leakage_fits)] || [info exists keys(-derate_fits)] } {
    set leakage ""
    set derate ""
    if { [info exists keys(-leakage_fits)] } {
      set leakage $keys(-leakage_fits)
    }
    if { [info exists keys(-derate_fits)] } {
      set derate $keys(-derate_fits)
    }
    thm::load_library_fits_cmd $leakage $derate
  }
  if { [info exists flags(-report)] } {
    thm::report_config_cmd
  }
}

sta::define_cmd_args "analyze_thermal" { \
    [-transient] \
    [-phases phase_names] \
    [-die die] \
    [-label label] \
    [-corner corner] \
    [-no_ir_drop] \
    [-no_timing]}

proc analyze_thermal { args } {
  sta::parse_key_args "analyze_thermal" args \
    keys {-phases -die -label -corner} \
    flags {-transient -no_ir_drop -no_timing}
  sta::check_argc_eq0 "analyze_thermal" $args

  set transient [info exists flags(-transient)]
  set phases ""
  if { [info exists keys(-phases)] } {
    set phases $keys(-phases)
    set transient 1
  }
  set die -1
  if { [info exists keys(-die)] } {
    set die $keys(-die)
    thm::check_die "-die" $die
  }
  set label "analyze_thermal"
  if { [info exists keys(-label)] } {
    set label $keys(-label)
  }
  set corner ""
  if { [info exists keys(-corner)] } {
    set corner $keys(-corner)
  }
  thm::analyze_thermal_cmd $transient $phases $die $label \
    [expr { ![info exists flags(-no_ir_drop)] }] \
    [expr { ![info exists flags(-no_timing)] }] $corner
}

sta::define_cmd_args "report_physics" { [-json file] }

proc report_physics { args } {
  sta::parse_key_args "report_physics" args keys {-json} flags {}
  sta::check_argc_eq0 "report_physics" $args
  set json ""
  if { [info exists keys(-json)] } {
    set json $keys(-json)
  }
  thm::report_physics_cmd $json
}

sta::define_cmd_args "set_physics_derating" { -enable | -disable }

proc set_physics_derating { args } {
  sta::parse_key_args "set_physics_derating" args keys {} \
    flags {-enable -disable}
  sta::check_argc_eq0 "set_physics_derating" $args
  if { [info exists flags(-enable)] == [info exists flags(-disable)] } {
    utl::error THM 63 "set_physics_derating requires exactly one of -enable or -disable."
  }
  thm::set_physics_derating_cmd [info exists flags(-enable)]
}

sta::define_cmd_args "write_thermal_map" { \
    -file file \
    [-map temperature|leakage|derate|ir_drop|delta|em] \
    [-die die]}

proc write_thermal_map { args } {
  sta::parse_key_args "write_thermal_map" args keys {-file -map -die} flags {}
  sta::check_argc_eq0 "write_thermal_map" $args
  if { ![info exists keys(-file)] } {
    utl::error THM 64 "write_thermal_map requires -file."
  }
  set map "temperature"
  if { [info exists keys(-map)] } {
    set map $keys(-map)
    if { [lsearch -exact {temperature leakage derate ir_drop delta em} $map] < 0 } {
      utl::error THM 65 "Unknown map '$map'; expected one of temperature, leakage, derate, ir_drop, delta, em."
    }
  }
  set die 0
  if { [info exists keys(-die)] } {
    set die $keys(-die)
    thm::check_die "-die" $die
  }
  thm::write_thermal_map_cmd $keys(-file) $map $die
}

sta::define_cmd_args "write_physics_history" { file }

proc write_physics_history { args } {
  sta::parse_key_args "write_physics_history" args keys {} flags {}
  sta::check_argc_eq1 "write_physics_history" $args
  thm::write_physics_history_cmd [lindex $args 0]
}

sta::define_cmd_args "load_physics_reference" { [-clear] [file] }

proc load_physics_reference { args } {
  sta::parse_key_args "load_physics_reference" args keys {} flags {-clear}
  if { [info exists flags(-clear)] } {
    sta::check_argc_eq0 "load_physics_reference" $args
    thm::clear_physics_reference_cmd
  } else {
    sta::check_argc_eq1 "load_physics_reference" $args
    thm::load_physics_reference_cmd [lindex $args 0]
  }
}

sta::define_cmd_args "write_physics_animation" { \
    -file file \
    [-type cooldown|transient|electrothermal|compare|stack] \
    [-format gif|mp4] \
    [-scale {min_c max_c}] \
    [-fps fps] \
    [-die die]}

proc write_physics_animation { args } {
  sta::parse_key_args "write_physics_animation" args \
    keys {-file -type -format -scale -fps -die} flags {}
  sta::check_argc_eq0 "write_physics_animation" $args
  if { ![info exists keys(-file)] } {
    utl::error THM 66 "write_physics_animation requires -file."
  }
  set type "cooldown"
  if { [info exists keys(-type)] } {
    set type $keys(-type)
    if { [lsearch -exact {cooldown transient electrothermal compare stack} $type] < 0 } {
      utl::error THM 67 "Unknown animation type '$type'."
    }
  }
  set format "gif"
  if { [info exists keys(-format)] } {
    set format $keys(-format)
    if { [lsearch -exact {gif mp4} $format] < 0 } {
      utl::error THM 68 "Unknown animation format '$format'; expected gif or mp4."
    }
  }
  set scale_min 0
  set scale_max 0
  if { [info exists keys(-scale)] } {
    if { [llength $keys(-scale)] != 2 } {
      utl::error THM 69 "-scale requires {min_c max_c}."
    }
    set scale_min [lindex $keys(-scale) 0]
    set scale_max [lindex $keys(-scale) 1]
  }
  set fps 4
  if { [info exists keys(-fps)] } {
    set fps $keys(-fps)
    sta::check_positive_integer "-fps" $fps
  }
  set die -1
  if { [info exists keys(-die)] } {
    set die $keys(-die)
    thm::check_die "-die" $die
  }
  thm::write_physics_animation_cmd $type $format $keys(-file) $scale_min \
    $scale_max $fps $die
}

namespace eval thm {
proc check_die { cmd_arg arg } {
  if { !([string is integer $arg] && $arg >= 0) } {
    utl::error THM 86 "$cmd_arg '$arg' is not a non-negative integer."
  }
}

proc reset_thermal { args } {
  sta::parse_key_args "reset_thermal" args keys {} flags {}
  thm::reset_thermal_cmd
}
}

################################################################
# Library characterization: leakage(T) and delay(T, V) fits from the loaded
# liberty corners.

sta::define_cmd_args "characterize_thermal_libraries" { \
    [-leakage_json file] \
    [-derate_json file] \
    [-corners {name ...}]}

proc characterize_thermal_libraries { args } {
  sta::parse_key_args "characterize_thermal_libraries" args \
    keys {-leakage_json -derate_json -corners} flags {}
  sta::check_argc_eq0 "characterize_thermal_libraries" $args
  set corners {}
  if { [info exists keys(-corners)] } {
    set corners $keys(-corners)
  }
  set leakage_json ""
  if { [info exists keys(-leakage_json)] } {
    set leakage_json $keys(-leakage_json)
  }
  set derate_json ""
  if { [info exists keys(-derate_json)] } {
    set derate_json $keys(-derate_json)
  }
  return [thm::characterize_libraries_cmd [join $corners " "] $leakage_json \
    $derate_json]
}
