# analyze_thermal / report_thermal error handling.
source "helpers.tcl"
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_def sky130hd_data/gcd_sky130hd_placed.def
read_sdc sky130hd_data/gcd_sky130hd_placed.sdc

# Missing binary (path and PATH lookup).
catch { analyze_thermal -hotspot_binary /no/such/dir/hotspot } msg
puts $msg
catch { analyze_thermal -hotspot_binary no_such_hotspot_binary } msg
puts $msg

# Report before any analysis.
report_thermal
puts "has results: [thm::has_thermal_results]"
catch { write_temperature_map [make_result_file analyze_thermal_errors.csv] } msg
puts $msg

# HotSpot exiting with a non-zero status.
set fail_hotspot [make_result_file fail_hotspot.sh]
set stream [open $fail_hotspot w]
puts $stream "#!/bin/sh"
puts $stream "echo 'fatal: simulated HotSpot failure' >&2"
puts $stream "exit 3"
close $stream
file attributes $fail_hotspot -permissions 0755

# THM-0012 / THM-0034 include machine dependent paths.
suppress_message THM 12
suppress_message THM 34
set work_dir [make_result_test_dir analyze_thermal_errors]
catch { analyze_thermal -hotspot_binary $fail_hotspot -work_dir $work_dir \
  -grid_rows 4 -grid_cols 4 } msg
puts $msg
puts "has results: [thm::has_thermal_results]"
puts "--- hotspot log"
report_file [file join $work_dir gcd.hotspot.log]

# Missing HotSpot config file.
catch { analyze_thermal -hotspot_binary $fail_hotspot \
  -hotspot_config /no/such/hotspot.config } msg
puts $msg

# A failed rerun (missing binary, invalid argument) must not keep the results
# of the previous successful analysis.
set fake_hotspot [file join [pwd] fake_hotspot.py]
analyze_thermal -hotspot_binary $fake_hotspot -work_dir $work_dir \
  -grid_rows 4 -grid_cols 4 -report_instances 2
puts "has results: [thm::has_thermal_results]"
catch { analyze_thermal -hotspot_binary /no/such/dir/hotspot } msg
puts $msg
puts "has results after failed rerun: [thm::has_thermal_results]"
analyze_thermal -hotspot_binary $fake_hotspot -work_dir $work_dir \
  -grid_rows 4 -grid_cols 4 -report_instances 2 -report_file /dev/null
catch { analyze_thermal -hotspot_binary $fake_hotspot -tile_size -5 } msg
puts $msg
puts "has results after invalid argument: [thm::has_thermal_results]"

# The report copies the instance names: deleting an instance afterwards is
# harmless.
set report_file [make_result_file analyze_thermal_errors.rpt]
analyze_thermal -hotspot_binary $fake_hotspot -work_dir $work_dir \
  -grid_rows 4 -grid_cols 4 -report_instances 1 -report_file $report_file
set stream [open $report_file r]
set report [read $stream]
close $stream
regexp {shown\):\n  (\S+)} $report -> hottest_inst
odb::dbInst_destroy [[ord::get_db_block] findInst $hottest_inst]
puts "deleted $hottest_inst: [expr {[[ord::get_db_block] findInst $hottest_inst] == "NULL"}]"
report_thermal

# Tile sizes: below a database unit, and too many tiles for HotSpot.
catch { analyze_thermal -hotspot_binary $fake_hotspot -tile_size 0.0001 } msg
puts $msg
catch { analyze_thermal -hotspot_binary $fake_hotspot -tile_size 1 } msg
puts $msg

# Broken grid output from HotSpot (relative work dir keeps the paths in the
# messages machine independent).
set grid_dir [file join results analyze_thermal_errors_grid]
file mkdir $grid_dir
foreach broken { truncated garbage duplicate } {
  set ::env(FAKE_HOTSPOT_GRID) $broken
  catch { analyze_thermal -hotspot_binary $fake_hotspot -work_dir $grid_dir \
    -grid_rows 4 -grid_cols 4 } msg
  puts $msg
  puts "has results: [thm::has_thermal_results]"
}
unset ::env(FAKE_HOTSPOT_GRID)

# Grid dimensions: finer than the die (300 um = 300000 DBU per side) or too
# many cells are rejected before anything is allocated.
catch { analyze_thermal -hotspot_binary $fake_hotspot -work_dir $work_dir \
  -grid_rows 400000 -grid_cols 4 } msg
puts $msg
catch { analyze_thermal -hotspot_binary $fake_hotspot -work_dir $work_dir \
  -grid_rows 2048 -grid_cols 2048 } msg
puts $msg

# Imported grids must contain finite numbers (relative path: see above).
foreach { name content } {
  bad_grid_nan "45.0,46.0\nnan,47.0\n"
  bad_grid_text "45.0,46.0\n47.0,hot\n"
} {
  set csv [file join $grid_dir $name.csv]
  set stream [open $csv w]
  puts -nonewline $stream [subst -nocommands -novariables $content]
  close $stream
  catch { thm::read_temperature_grid_cmd $csv } msg
  puts $msg
  puts "has results: [thm::has_thermal_results]"
}

# -ambient only applies to the built-in configuration.
set config [file join $grid_dir custom.config]
set stream [open $config w]
puts $stream "-ambient 318.15"
close $stream
analyze_thermal -hotspot_binary $fake_hotspot -work_dir $work_dir \
  -hotspot_config $config -ambient 60 -grid_rows 4 -grid_cols 4 \
  -report_instances 1
puts "has results: [thm::has_thermal_results]"

# Results belong to the analyzed block: replacing the block (which may reuse
# the freed block slot) drops them.
set chip [[ord::get_db] getChip]
odb::dbBlock_destroy [$chip getBlock]
odb::dbBlock_create $chip replacement
puts "has results after replacing the block: [thm::has_thermal_results]"
report_thermal
