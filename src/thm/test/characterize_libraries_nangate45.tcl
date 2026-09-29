# Library characterization from the Nangate45 fast/typ/slow corners.  The
# corners differ in process and voltage as well as temperature, so the fit
# must detect that and fall back to the documented defaults.
source "helpers.tcl"
define_corners fast typ slow
read_lef Nangate45/Nangate45.lef
read_liberty -corner fast Nangate45/Nangate45_fast.lib
read_liberty -corner typ Nangate45/Nangate45_typ.lib
read_liberty -corner slow Nangate45/Nangate45_slow.lib
read_def Nangate45_data/gcd.def
read_sdc Nangate45_data/gcd.sdc

set leakage_json [make_result_file characterize_libraries_nangate45_leakage.json]
set derate_json [make_result_file characterize_libraries_nangate45_derate.json]
characterize_thermal_libraries -leakage_json $leakage_json \
  -derate_json $derate_json

# Only the typical corner: a single temperature, nothing to fit either.
characterize_thermal_libraries -corners typ

set_thermal_config -grid {8 8} -nominal_vdd 1.1 -leakage_fits $leakage_json \
  -derate_fits $derate_json -report
analyze_thermal -no_ir_drop
report_physics

# The default coefficients are overridable.
set_thermal_config -set {leakage_beta_per_c 0.05}
set_thermal_config -set {delay_tempco_per_c 0.002}
set_thermal_config -set {delay_vcoef_per_v -0.5} -report
