# Argument validation of the thermal commands.
source "helpers.tcl"
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib

catch { set_thermal_config -grid {1 1} } msg
puts $msg
catch { set_thermal_config -grid 7 } msg
puts $msg
catch { set_thermal_config -set {bogus_key 1} } msg
puts $msg
catch { set_thermal_config -ambient hot } msg
puts $msg
catch { set_thermal_config -phase {burst 2.0} } msg
puts $msg
catch { set_thermal_config -phase {burst 2.0 0} } msg
puts $msg
catch { report_physics } msg
puts $msg
catch { write_thermal_map -map temperature } msg
puts $msg
catch { write_thermal_map -file x.map -map bogus } msg
puts $msg
catch { set_physics_derating } msg
puts $msg
catch { set_physics_derating -enable -disable } msg
puts $msg
catch { analyze_thermal -transient } msg
puts $msg
catch { write_physics_animation -file x.gif -type bogus } msg
puts $msg
catch { write_physics_animation -file x.gif -format avi } msg
puts $msg
catch { write_physics_animation -file x.gif } msg
puts $msg
catch { load_physics_reference /nonexistent/history.json } msg
puts $msg
set_thermal_config -grid {4 4} -phase {burst 2.0 0.01} -report
