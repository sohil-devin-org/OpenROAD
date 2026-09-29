# Argument validation for global_placement -physics_driven.
source helpers.tcl
read_lef Nangate45/Nangate45.lef
read_def Nangate45_data/gcd.def
read_liberty Nangate45/Nangate45_typ.lib
read_sdc Nangate45_data/gcd.sdc
set_thermal_config -no_ir_drop

catch { global_placement -physics_weight 0.5 } msg
puts $msg
catch { global_placement -physics_driven -physics_weight -1 } msg
puts $msg
catch { global_placement -physics_driven -physics_checkpoint_interval 0 } msg
puts $msg
catch { global_placement -physics_driven -physics_grid abc } msg
puts $msg
catch { global_placement -physics_driven -physics_start_overflow 2 } msg
puts $msg
catch { global_placement -physics_driven -place_ios } msg
puts $msg
