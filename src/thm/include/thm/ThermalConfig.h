// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

// Physics configuration for the thermal / physics-driven placement module.
//
// Every numeric default below is a documented, cited value.  This header is
// the single place where material, package, electromigration and model
// constants live; nothing else in the module may introduce its own constants.
//
// References
//  [HS]  W. Huang et al., "HotSpot: A Compact Thermal Modeling Methodology for
//        Early-Stage VLSI Design," IEEE TVLSI 14(5), 2006, and the HotSpot 6.0
//        default configuration file (hotspot.config).
//  [INC] F. P. Incropera, D. P. DeWitt, "Fundamentals of Heat and Mass
//        Transfer," 6th ed., Wiley, 2007, Table A.1/A.2 (silicon, copper).
//  [3DI] A. Sridhar et al., "3D-ICE: Fast compact transient thermal modeling
//        for 3D ICs with inter-tier liquid cooling," ICCAD 2010.
//  [BLK] J. R. Black, "Electromigration - A Brief Survey and Some Recent
//        Results," IEEE Trans. Electron Devices 16(4), 1969.
//  [JEP] JEDEC JEP122H, "Failure Mechanisms and Models for Semiconductor
//        Devices," 2016 (Cu interconnect: Ea ~ 0.9 eV, n ~ 2 typical).
//  [CRC] CRC Handbook of Chemistry and Physics, 97th ed., electrical
//        resistivity of copper vs temperature (alpha ~ 3.9e-3 / K).
//  [HB]  P. Batude et al. / hybrid bonding effective conductivity survey:
//        Cu/SiO2 hybrid bond interface ~ 1-4 W/mK effective (see also [3DI]).
//  [ROY] K. Roy, S. Mukhopadhyay, H. Mahmoodi-Meimand, "Leakage Current
//        Mechanisms and Leakage Reduction Techniques in Deep-Submicrometer
//        CMOS Circuits," Proc. IEEE 91(2), 2003 (subthreshold leakage grows
//        exponentially with T; roughly doubles every ~10 C, cf. ITRS).
//  [SN]  T. Sakurai, A. R. Newton, "Alpha-Power Law MOSFET Model and its
//        Applications to CMOS Inverter Delay and Other Formulas," IEEE JSSC
//        25(2), 1990: t_d ~ C V / (I_D0 (V - V_th)^alpha), alpha ~ 1.3.
//  [SZE] S. M. Sze, K. K. Ng, "Physics of Semiconductor Devices," 3rd ed.,
//        Wiley, 2007, Sec. 1.5 (lattice-scattering mobility mu ~ T^-1.5).

namespace thm {

// One homogeneous layer in the vertical stack (z direction).
struct StackLayer
{
  std::string name;
  double thickness_m = 0.0;
  double conductivity_w_mk = 0.0;
  double volumetric_heat_capacity_j_m3k = 0.0;
  // True for a layer in which instance power is injected (the active
  // transistor layer of a die).
  bool is_active = false;
  // Which die (0 = bottom die, 1 = top die) this layer belongs to.
  int die = 0;
};

// Description of one die used to build the default stack.
struct DieDescription
{
  // Bulk silicon thickness. HotSpot default t_chip = 0.15 mm [HS].
  double silicon_thickness_m = 150e-6;
  // Back-end-of-line (metal + dielectric) effective thickness. HotSpot uses
  // no explicit BEOL; 3D-ICE examples use ~10 um of BEOL [3DI].
  double beol_thickness_m = 10e-6;
  // BEOL effective conductivity, dominated by SiO2 (1.4 W/mK [INC]) with
  // copper fill: 3D-ICE example stacks use ~2 W/mK [3DI].
  double beol_conductivity_w_mk = 2.0;
};

// Bonding layer between two dies for the two-die stack.
struct BondLayer
{
  // Hybrid bond / micro-bump layer thickness and effective conductivity [HB].
  double thickness_m = 5e-6;
  double conductivity_w_mk = 2.0;
  // Volumetric heat capacity of SiO2 ~ 1.65e6 J/m^3K [INC].
  double volumetric_heat_capacity_j_m3k = 1.65e6;
};

// One sustained activity phase used for transient workload replay.  Chip
// thermal time constants are milliseconds to seconds, so a phase is a
// constant activity level held for duration_s, never a raw trace replay.
struct ActivityPhase
{
  std::string name;
  // Multiplier on the default switching activity (1.0 = the default
  // OpenSTA propagated activity).  Optional activity file overrides it.
  double activity_scale = 1.0;
  std::string activity_file;  // VCD or SAIF, empty for scaled default
  double duration_s = 0.0;
};

struct ElectromigrationConfig
{
  // Black's equation MTTF = A * J^-n * exp(Ea / (k T)) [BLK].
  double activation_energy_ev = 0.9;  // Cu interconnect [JEP]
  double current_exponent = 2.0;      // [JEP]
  // Reference condition for the relative lifetime factor.
  double reference_temp_c = 105.0;  // JEDEC qualification temperature [JEP]
  // Copper temperature coefficient of resistance [CRC].
  double metal_tcr_per_k = 3.9e-3;
  double metal_reference_temp_c = 25.0;
};

struct ThermalConfig
{
  // Ambient temperature.  25 C matches the sky130 tt_025C corner so that a
  // zero-power design reports the nominal library temperature.
  double ambient_c = 25.0;

  // Package / heat-sink boundary: lumped thermal resistance from the top of
  // the stack (heat spreader side) to ambient.  HotSpot default is
  // r_convec = 0.1 K/W for a heat-sinked package [HS].
  double top_resistance_k_w = 0.1;
  // Secondary path (package substrate / PCB) from the bottom of the stack.
  // HotSpot secondary-path default r_convec_sec = 50 K/W [HS].
  double bottom_resistance_k_w = 50.0;
  // When true heat leaves through the bottom face (as in a flip-chip package
  // where the heat sink is on the substrate side of the die stack).
  bool heat_sink_on_bottom = false;

  // Bulk silicon properties.  HotSpot defaults: k = 100 W/mK (temperature
  // adjusted), volumetric heat capacity 1.75e6 J/m^3K [HS]; Incropera lists
  // 148 W/mK at 300 K [INC].
  double silicon_conductivity_w_mk = 100.0;
  double silicon_volumetric_heat_capacity_j_m3k = 1.75e6;
  // Optional temperature dependence k(T) = k300 * (300/T)^1.3 (T in K),
  // off by default.  Exponent from [INC] silicon data fit; HotSpot uses a
  // similar correction.
  bool temperature_dependent_conductivity = false;
  double silicon_conductivity_exponent = 1.3;

  // Die stack.  dies[0] is the bottom die (the design being placed for a
  // single die; the *lower* die of a two-die stack).
  std::vector<DieDescription> dies{DieDescription{}};
  BondLayer bond;
  // Thinned top die thickness used when two_die is enabled (50 um is typical
  // for face-to-back hybrid bonded stacks [HB]).
  double thinned_die_thickness_m = 50e-6;
  bool two_die = false;
  // Source of the second die's power map: "mirror" (same design, mirrored),
  // "uniform" (uniform power of second_die_power_w) or a CSV map file.
  std::string second_die_power_source = "uniform";
  double second_die_power_w = 0.0;
  std::string second_die_power_file;

  // Lateral grid resolution.  64x64 keeps a steady solve well under a second
  // on ibex-sized designs while resolving hot spots of a few hundred um.
  int grid_x = 64;
  int grid_y = 64;

  // Linear solver tolerances (relative residual) and iteration cap.
  double solver_tolerance = 1e-6;
  int solver_max_iterations = 5000;

  // Electrothermal fixed-point loop.
  double loop_peak_tolerance_c = 0.05;
  double loop_leakage_tolerance_rel = 1e-3;
  int loop_max_iterations = 10;
  // Temperatures above this are reported as thermal runaway / non-physical.
  double runaway_temperature_c = 150.0;

  // Transient stepping.
  double transient_time_step_s = 1e-3;

  ElectromigrationConfig em;

  // Activity.
  double default_activity_scale = 1.0;
  std::string activity_file;
  std::vector<ActivityPhase> phases;

  // Nominal supply voltage used when no IR-drop analysis is available.
  double nominal_vdd_v = 1.8;
  bool include_ir_drop = true;
  // IR-drop analysis (PDNSim): power net to analyze (empty: first routed
  // POWER net of the block) and voltage-source location file in the
  // analyze_power_grid -vsrc format (empty: PDNSim's own sources).
  std::string ir_power_net;
  std::string ir_vsrc_file;

  // Library-fit fallbacks.  Used by LeakageModel / DerateModel only when the
  // loaded liberty corners do not isolate temperature (or voltage), e.g.
  // Nangate45 fast/typ/slow or sky130 ff/tt/ss where process changes too.
  // Overridable with set_thermal_config -set <key> <value>.
  //
  // Subthreshold leakage doubles roughly every 10 C [ROY]:
  //   L(T) = L(T_ref) exp(beta (T - T_ref)),  beta = ln(2) / 10 C.
  double leakage_beta_per_c = 0.0693147180559945;
  // Delay temperature coefficient (fraction per C).  In the alpha-power law
  // [SN] the drive current follows the carrier mobility, mu ~ T^-1.5 [SZE],
  // so d ln(t_d)/dT = 1.5 / T_nom with T_nom = 298.15 K (25 C):
  //   1.5 / 298.15 K = 5.03e-3 / C
  // (the V_th(T) reduction that partly offsets this at high V is neglected,
  // which is the pessimistic direction for temperature-driven placement).
  double delay_tempco_per_c = 5.03e-3;
  // Delay voltage coefficient (fraction per V) from the alpha-power law [SN]
  // t_d ~ V / (V - V_th)^alpha linearized at the nominal supply:
  //   d ln(t_d)/dV = 1/V - alpha / (V - V_th)
  // with alpha = 1.3 [SN], V = 1.8 V and V_th/V = 0.22 (V_th = 0.4 V):
  //   1/1.8 - 1.3/1.4 = -0.37 / V.
  double delay_vcoef_per_v = -0.37;

  // Build the full vertical stack for the configured dies.
  std::vector<StackLayer> buildStack() const;

  // Screening length lambda of the lateral heat equation
  //   d2T/dx2 + d2T/dy2 - T/lambda^2 = -q/k_eff
  // for the bottom die: lambda = sqrt(k * t * R_area) where R_area is the
  // vertical areal resistance (m^2 K/W) from the active layer to ambient.
  double screeningLengthM(double die_area_m2) const;
};

}  // namespace thm
