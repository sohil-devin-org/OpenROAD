# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "analyze_thermal" {
  [-corner corner]
  [-hotspot_binary path]
  [-hotspot_config file]
  [-work_dir dir]
  [-keep_files]
  [-tile_size size_um]
  [-grid_rows rows]
  [-grid_cols cols]
  [-ambient temperature_c]
  [-report_instances count]
  [-report_file file]
}

proc analyze_thermal { args } {
  sta::parse_key_args "analyze_thermal" args \
    keys {-corner -hotspot_binary -hotspot_config -work_dir -tile_size \
      -grid_rows -grid_cols -ambient -report_instances -report_file} \
    flags {-keep_files}
  sta::check_argc_eq0 "analyze_thermal" $args

  set hotspot_binary ""
  if { [info exists keys(-hotspot_binary)] } {
    set hotspot_binary $keys(-hotspot_binary)
    if { [string index $hotspot_binary 0] eq "~" } {
      set hotspot_binary [file normalize $hotspot_binary]
    }
  }
  set hotspot_config ""
  if { [info exists keys(-hotspot_config)] } {
    set hotspot_config $keys(-hotspot_config)
    if { ![file exists $hotspot_config] } {
      utl::error THM 20 "HotSpot config file $hotspot_config does not exist."
    }
  }
  set work_dir ""
  if { [info exists keys(-work_dir)] } {
    set work_dir $keys(-work_dir)
  }
  set tile_size 0.0
  if { [info exists keys(-tile_size)] } {
    set tile_size $keys(-tile_size)
    sta::check_positive_float "-tile_size" $tile_size
  }
  set grid_rows 64
  if { [info exists keys(-grid_rows)] } {
    set grid_rows $keys(-grid_rows)
    sta::check_positive_integer "-grid_rows" $grid_rows
  }
  set grid_cols 64
  if { [info exists keys(-grid_cols)] } {
    set grid_cols $keys(-grid_cols)
    sta::check_positive_integer "-grid_cols" $grid_cols
  }
  set ambient 45.0
  if { [info exists keys(-ambient)] } {
    set ambient $keys(-ambient)
    sta::check_float "-ambient" $ambient
  }
  set report_instances 10
  if { [info exists keys(-report_instances)] } {
    set report_instances $keys(-report_instances)
    sta::check_cardinal "-report_instances" $report_instances
  }
  set report_file ""
  if { [info exists keys(-report_file)] } {
    set report_file $keys(-report_file)
  }

  thm::analyze_thermal_cmd \
    [sta::parse_scene_or_default keys] \
    $hotspot_binary \
    $hotspot_config \
    $work_dir \
    [info exists flags(-keep_files)] \
    $tile_size \
    $grid_rows \
    $grid_cols \
    $ambient \
    $report_instances \
    $report_file
}

sta::define_cmd_args "report_thermal" {
  [-file file]
}

proc report_thermal { args } {
  sta::parse_key_args "report_thermal" args keys {-file} flags {}
  sta::check_argc_eq0 "report_thermal" $args
  set file ""
  if { [info exists keys(-file)] } {
    set file $keys(-file)
  }
  thm::report_thermal_cmd $file
}

sta::define_cmd_args "write_temperature_map" {
  file
}

proc write_temperature_map { args } {
  sta::parse_key_args "write_temperature_map" args keys {} flags {}
  sta::check_argc_eq1 "write_temperature_map" $args
  thm::write_temperature_map_cmd [lindex $args 0]
}
