# Physics-driven placement benchmark on the in-repo sky130hd designs.
#
# Driven by run_sky130hd_benchmarks.sh through environment variables:
#   BENCH_DESIGN   gcd | aes | ibex   (test/<design>_sky130hd.tcl)
#   BENCH_MODE     baseline | physics (physics adds -physics_driven)
#   BENCH_OUT      output directory
#   BENCH_STRESS   0 (stock, default) | 1 (stress thermal configuration)
#   BENCH_THREADS  thread count (default: cpu_count)
#
# The flow mirrors test/flow.tcl up to and including global placement:
# read libraries/verilog/sdc -> floorplan -> tapcell -> pdngen ->
# global_placement -skip_io -> place_pins -> global_placement
# -routability_driven, then analyze_thermal / report_physics and one CSV row.

set flow_dir [file dirname [file normalize [info script]]]
set repo_test_dir [file normalize $flow_dir/../../../../test]

proc bench_env { name default } {
  if { [info exists ::env($name)] && $::env($name) ne "" } {
    return $::env($name)
  }
  return $default
}

set bench_design [bench_env BENCH_DESIGN gcd]
set bench_mode [bench_env BENCH_MODE baseline]
set bench_out [bench_env BENCH_OUT $flow_dir/results/benchmarks]
set bench_stress [bench_env BENCH_STRESS 0]
set bench_threads [bench_env BENCH_THREADS ""]
file mkdir $bench_out
set bench_tag ${bench_design}_${bench_mode}
set status_file $bench_out/$bench_tag.status

proc write_status { text } {
  global status_file
  set fh [open $status_file w]
  puts $fh $text
  close $fh
}
write_status "failed"

if { [lsearch -exact {baseline physics} $bench_mode] < 0 } {
  error "BENCH_MODE must be baseline or physics, got '$bench_mode'"
}
set design_script $repo_test_dir/${bench_design}_sky130hd.tcl
if { ![file exists $design_script] } {
  error "No flow input $design_script for design '$bench_design'"
}

# The per-design scripts end with `include -echo "flow.tcl"`, which runs the
# whole flow through routing.  Source them with `include` disabled to pick up
# only the stock platform/design settings.
cd $repo_test_dir
rename include bench_saved_include
proc include { args } {}
source $design_script
rename include ""
rename bench_saved_include include

################################################################
# Flow (see test/flow.tcl).
read_libraries
read_verilog $synth_verilog
link_design $top_module
read_sdc $sdc_file

if { $bench_threads ne "" } {
  set_thread_count $bench_threads
} else {
  set_thread_count [cpu_count]
}
sta::set_thread_count 1

initialize_floorplan -site $site -die_area $die_area -core_area $core_area
source $tracks_file
remove_buffers
eval tapcell $tapcell_args ;# tclint-disable command-args
source $pdn_cfg
pdngen

foreach layer_adjustment $global_routing_layer_adjustments {
  lassign $layer_adjustment layer adjustment
  set_global_routing_layer_adjustment $layer $adjustment
}
set_routing_layers -signal $global_routing_layers \
  -clock $global_routing_clock_layers
set_macro_extension 2

################################################################
# Thermal configuration.
if { $bench_stress } {
  # STRESS configuration (not stock): high activity, high ambient and a
  # package without a heat sink.  See README "Benchmarks".
  #   -ambient 105       AEC-Q100 Grade 2 maximum ambient temperature.
  #   -top_resistance 50 heat leaves only through the package/board path,
  #                      HotSpot's secondary-path default (50 K/W) [HS].
  #   -activity_scale 4  workload assumption, 4x the default switching
  #                      activity; not a physical constant.
  set_thermal_config -ambient 105 -top_resistance 50 -activity_scale 4.0 \
    -report
  set bench_config stress
} else {
  set_thermal_config -report
  set bench_config stock
}

################################################################
# Global placement (timed).
set place_args [list -density $global_place_density \
  -pad_left $global_place_pad -pad_right $global_place_pad]
if { $bench_mode eq "physics" } {
  lappend place_args -physics_driven
}

set t0 [clock milliseconds]
if { [catch {
  global_placement {*}$place_args -skip_io
  place_pins -hor_layers $io_placer_hor_layer -ver_layers $io_placer_ver_layer
  global_placement {*}$place_args -routability_driven
} err] } {
  if { $bench_mode eq "physics" && [string match "*physics_driven*" $err] } {
    write_status "not run (flag unavailable)"
    puts "bench_sky130hd: 'global_placement -physics_driven' is not available\
      in this build: $err"
    exit 0
  }
  error $err
}
set runtime_s [expr { ([clock milliseconds] - $t0) / 1000.0 }]

################################################################
# Physics analysis and CSV row.
analyze_thermal -label $bench_tag
set json_file $bench_out/$bench_tag.json
report_physics -json $json_file
write_physics_history $bench_out/$bench_tag.history.json

set fh [open $json_file r]
set report [read $fh]
close $fh
proc json_number { report key } {
  if { [regexp "\"$key\"\\s*:\\s*(\[-+0-9.eE\]+)" $report -> value] } {
    return $value
  }
  return "nan"
}

# Half-perimeter wirelength of the signal nets from the placed pin bboxes.
proc design_hpwl_um { } {
  set block [ord::get_db_block]
  set dbu [$block getDbUnitsPerMicron]
  set hpwl 0
  foreach net [$block getNets] {
    if { [$net isSpecial] } {
      continue
    }
    set bbox [$net getTermBBox]
    set hpwl [expr { $hpwl + [$bbox dx] + [$bbox dy] }]
  }
  return [expr { double($hpwl) / $dbu }]
}

set columns {hpwl_um peak_temp_c avg_temp_c max_gradient_c_per_mm
             leakage_power_w worst_ir_drop_v wns_nominal_s wns_derated_s
             tns_nominal_s tns_derated_s}
set row [list $bench_design $bench_mode $bench_config ok]
foreach column $columns {
  if { $column eq "hpwl_um" } {
    lappend row [format %.1f [design_hpwl_um]]
  } else {
    lappend row [json_number $report $column]
  }
}
lappend row [format %.2f $runtime_s]

set csv_file $bench_out/$bench_tag.csv
set fh [open $csv_file w]
puts $fh "design,mode,config,status,[join $columns ,],runtime_s"
puts $fh [join $row ,]
close $fh
write_status "ok"
puts "bench_sky130hd: wrote $csv_file"
