# Example: run HotSpot on the placed sky130hd gcd design and show the result
# in the GUI (openroad -gui gcd_hotspot.tcl). Needs `hotspot` on PATH or set
# the HOTSPOT environment variable to the executable.
read_lef sky130hd/sky130hd.tlef
read_lef sky130hd/sky130hd_std_cell.lef
read_liberty sky130hd/sky130_fd_sc_hd__tt_025C_1v80.lib
read_def sky130hd_data/gcd_sky130hd_placed.def
read_sdc sky130hd_data/gcd_sky130hd_placed.sdc

set hotspot hotspot
if { [info exists ::env(HOTSPOT)] } {
  set hotspot $::env(HOTSPOT)
}
analyze_thermal -hotspot_binary $hotspot -grid_rows 32 -grid_cols 32 \
  -report_instances 5
write_temperature_map gcd_temperature.csv

if { [gui::enabled] } {
  gui::set_display_controls "Heat Maps/Temperature" visible true
}
