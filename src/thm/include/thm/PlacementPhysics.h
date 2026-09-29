// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <string>

#include "gpl/PhysicsModel.h"

namespace odb {
class dbInst;
class dbNet;
}  // namespace odb

namespace thm {

class Thermal;

// Adapter that exposes the thermal/physics engine to the global placer
// (global_placement -physics_driven).
class PlacementPhysics : public gpl::PhysicsModel
{
 public:
  explicit PlacementPhysics(Thermal* thermal);

  void beginPlacement() override;
  void endPlacement() override;
  gpl::PhysicsCheckpointResult runCheckpoint(const std::string& label,
                                             int iteration,
                                             double hpwl_um,
                                             double overflow,
                                             double physics_weight) override;
  double screeningLengthDbu() const override;
  double instancePowerW(odb::dbInst* inst) const override;
  float netTimingWeightMultiplier(odb::dbNet* net) const override;

 private:
  Thermal* thermal_;
  bool derating_was_enabled_ = false;
  bool in_placement_ = false;
  double max_derate_ = 1.0;
};

}  // namespace thm
