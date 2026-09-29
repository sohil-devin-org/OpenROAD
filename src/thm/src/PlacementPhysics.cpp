// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/PlacementPhysics.h"

#include <algorithm>

#include "odb/db.h"
#include "thm/PhysicsState.h"
#include "thm/Thermal.h"

namespace thm {

PlacementPhysics::PlacementPhysics(Thermal* thermal) : thermal_(thermal)
{
}

void PlacementPhysics::beginPlacement()
{
  // Keep the temperature / IR-drop derates applied while the placer runs so
  // the timing-driven net reweighting sees physics-aware slacks.
  derating_was_enabled_ = thermal_->deratingEnabled();
  thermal_->setDeratingEnabled(true);
  in_placement_ = true;
}

void PlacementPhysics::endPlacement()
{
  if (in_placement_ && !derating_was_enabled_) {
    thermal_->setDeratingEnabled(false);
  }
  in_placement_ = false;
}

gpl::PhysicsCheckpointResult PlacementPhysics::runCheckpoint(
    const std::string& label,
    const int iteration,
    const double hpwl_um,
    const double overflow,
    const double physics_weight)
{
  AnalyzeOptions options;
  options.label = label;
  options.iteration = iteration;
  options.hpwl_um = hpwl_um;
  options.overflow = overflow;
  options.physics_weight = physics_weight;
  options.record = true;
  options.warm_start = true;
  const PhysicsMetrics metrics = thermal_->analyze(options);

  max_derate_ = 1.0;
  for (const auto& [inst, phys] : thermal_->instancePhysics()) {
    max_derate_ = std::max(max_derate_, phys.derate);
  }

  gpl::PhysicsCheckpointResult result;
  result.valid = true;
  for (const double peak : metrics.peak_temp_c) {
    result.peak_temp_c = std::max(result.peak_temp_c, peak);
  }
  if (!metrics.avg_temp_c.empty()) {
    result.avg_temp_c = metrics.avg_temp_c.front();
  }
  result.total_power_w = metrics.total_power_w;
  result.leakage_power_w = metrics.leakage_power_w;
  result.worst_ir_drop_v = metrics.worst_ir_drop_v;
  result.wns_nominal_s = metrics.wns_nominal_s;
  result.wns_derated_s = metrics.wns_derated_s;
  result.tns_derated_s = metrics.tns_derated_s;
  result.converged = metrics.converged;
  result.runaway = metrics.runaway;
  return result;
}

double PlacementPhysics::screeningLengthDbu() const
{
  return thermal_->screeningLengthDbu();
}

double PlacementPhysics::instancePowerW(odb::dbInst* inst) const
{
  return thermal_->instancePowerW(inst);
}

float PlacementPhysics::netTimingWeightMultiplier(odb::dbNet* net) const
{
  // Nets touching the most derated (hottest / most IR-starved) instances get
  // up to 2x their timing weight; nets at nominal conditions are unchanged.
  if (max_derate_ <= 1.0) {
    return 1.0f;
  }
  double worst = 1.0;
  for (odb::dbITerm* iterm : net->getITerms()) {
    const InstancePhysics* phys = thermal_->instancePhysics(iterm->getInst());
    if (phys != nullptr) {
      worst = std::max(worst, phys->derate);
    }
  }
  const double frac = (worst - 1.0) / (max_derate_ - 1.0);
  return static_cast<float>(1.0 + std::clamp(frac, 0.0, 1.0));
}

}  // namespace thm
