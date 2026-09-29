# Thermal heat map from a HotSpot grid file with auto and fixed color range
source "helpers.tcl"

read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc

catch { gui::dump_heatmap Thermal [make_result_file thermal_heatmap.empty.csv] }

set_debug_level PSM thermal_heatmap 1

analyze_thermal -grid_file thermal_heatmap.grid.steady -grid {8 8}

set auto_csv [make_result_file thermal_heatmap.auto.csv]
gui::dump_heatmap Thermal $auto_csv
diff_files thermal_heatmap.csvok $auto_csv

# A fixed color range only changes the colors, not the cell temperatures.
set_thermal_color_range -min 50 -max 70
set fixed_csv [make_result_file thermal_heatmap.fixed.csv]
gui::dump_heatmap Thermal $fixed_csv
diff_files thermal_heatmap.csvok $fixed_csv

gui::set_heatmap Thermal LogScale 1
set log_csv [make_result_file thermal_heatmap.log.csv]
gui::dump_heatmap Thermal $log_csv
diff_files thermal_heatmap.csvok $log_csv
gui::set_heatmap Thermal LogScale 0

set_thermal_color_range -auto
set auto2_csv [make_result_file thermal_heatmap.auto2.csv]
gui::dump_heatmap Thermal $auto2_csv
diff_files thermal_heatmap.csvok $auto2_csv

# A failed rerun clears the previous result from the heat map.
catch { analyze_thermal -grid_file missing.grid.steady -grid {8 8} } err
puts $err
catch { gui::dump_heatmap Thermal [make_result_file thermal_heatmap.failed.csv] } err
puts $err
