// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <string>

namespace odb {
class dbInst;
class dbNet;
}  // namespace odb

namespace gpl {

// Result of one physics checkpoint (power -> thermal -> IR drop -> timing)
// evaluated on the current placement.
struct PhysicsCheckpointResult
{
  bool valid = false;
  double peak_temp_c = 0.0;
  double avg_temp_c = 0.0;
  double total_power_w = 0.0;
  double leakage_power_w = 0.0;
  double worst_ir_drop_v = 0.0;
  double wns_nominal_s = 0.0;
  double wns_derated_s = 0.0;
  double tns_derated_s = 0.0;
  bool converged = true;
  bool runaway = false;
};

// Interface through which the Nesterov placer talks to the physics engine
// (implemented by thm::PlacementPhysics). Keeping it abstract avoids a
// gpl -> thm dependency.
class PhysicsModel
{
 public:
  virtual ~PhysicsModel() = default;

  // Called once before the first checkpoint / after the last one so the
  // engine can enable derates for the duration of the placement.
  virtual void beginPlacement() = 0;
  virtual void endPlacement() = 0;

  // Run the coupled analysis on the placement currently in the database.
  virtual PhysicsCheckpointResult runCheckpoint(const std::string& label,
                                                int iteration,
                                                double hpwl_um,
                                                double overflow,
                                                double physics_weight)
      = 0;

  // Screening length lambda of the thermal spreading term in DBU.
  virtual double screeningLengthDbu() const = 0;

  // Total (switching + internal + temperature dependent leakage) power of an
  // instance after the last checkpoint, in watts.
  virtual double instancePowerW(odb::dbInst* inst) const = 0;

  // Multiplier (>= 1) applied to the timing weight of a net whose driver or
  // loads are slowed down by temperature / IR drop.
  virtual float netTimingWeightMultiplier(odb::dbNet* net) const = 0;
};

}  // namespace gpl
