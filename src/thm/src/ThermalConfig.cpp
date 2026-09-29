// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/ThermalConfig.h"

#include <cmath>

namespace thm {

// Stack order is bottom -> top.  For a single die the bottom face is the
// package/board side (R_bottom) and the top face is the heat-sink side
// (R_top) unless heat_sink_on_bottom is set; the solver maps the two
// resistances accordingly.  The active (transistor) layer of every die is
// modelled as a thin layer on the BEOL side of the silicon where the
// instance power is injected.
std::vector<StackLayer> ThermalConfig::buildStack() const
{
  std::vector<StackLayer> stack;
  const int num_dies = two_die ? 2 : 1;
  for (int die = 0; die < num_dies; ++die) {
    const DieDescription& desc
        = die < static_cast<int>(dies.size()) ? dies[die] : dies.front();
    if (die > 0) {
      StackLayer bond_layer;
      bond_layer.name = "bond" + std::to_string(die);
      bond_layer.thickness_m = bond.thickness_m;
      bond_layer.conductivity_w_mk = bond.conductivity_w_mk;
      bond_layer.volumetric_heat_capacity_j_m3k
          = bond.volumetric_heat_capacity_j_m3k;
      bond_layer.die = die;
      stack.push_back(bond_layer);
    }
    StackLayer si;
    si.name = "silicon" + std::to_string(die);
    si.thickness_m
        = (die > 0) ? thinned_die_thickness_m : desc.silicon_thickness_m;
    si.conductivity_w_mk = silicon_conductivity_w_mk;
    si.volumetric_heat_capacity_j_m3k = silicon_volumetric_heat_capacity_j_m3k;
    si.die = die;
    stack.push_back(si);

    StackLayer active;
    active.name = "active" + std::to_string(die);
    active.thickness_m = desc.beol_thickness_m;
    active.conductivity_w_mk = desc.beol_conductivity_w_mk;
    // BEOL is mostly SiO2 (1.6e6 J/m3K) with copper; use the oxide value.
    active.volumetric_heat_capacity_j_m3k = 1.6e6;
    active.is_active = true;
    active.die = die;
    stack.push_back(active);
  }
  return stack;
}

// Lateral heat spreading in a thin plate of thickness t and conductivity k
// that loses heat to ambient through an area-specific resistance r
// (K m^2/W) obeys  k t lap(T) - (T - T_amb)/r = -q, i.e. a screened Poisson
// equation with screening length lambda = sqrt(k t r).  The die-level lumped
// resistances are converted to area-specific values using the die area.
double ThermalConfig::screeningLengthM(double die_area_m2) const
{
  const double g_top = top_resistance_k_w > 0 ? 1.0 / top_resistance_k_w : 0;
  const double g_bot
      = bottom_resistance_k_w > 0 ? 1.0 / bottom_resistance_k_w : 0;
  const double g = g_top + g_bot;
  if (g <= 0 || die_area_m2 <= 0) {
    return 0.0;
  }
  const double r_area = die_area_m2 / g;
  const double thickness
      = dies.empty() ? 150e-6 : dies.front().silicon_thickness_m;
  return std::sqrt(silicon_conductivity_w_mk * thickness * r_area);
}

}  // namespace thm
