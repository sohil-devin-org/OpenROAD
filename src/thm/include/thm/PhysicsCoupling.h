// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <map>
#include <string>
#include <vector>

#include "odb/db.h"
#include "thm/PhysicsState.h"
#include "thm/ThermalConfig.h"

namespace sta {
class dbSta;
class Scene;
}  // namespace sta

namespace psm {
class PDNSim;
}

namespace utl {
class Logger;
}

namespace thm {

class LeakageModel;
class DerateModel;
class ThermalGrid;

struct DbInstIdLess
{
  bool operator()(const odb::dbInst* lhs, const odb::dbInst* rhs) const
  {
    return lhs->getId() < rhs->getId();
  }
};

using InstancePhysicsMap
    = std::map<odb::dbInst*, InstancePhysics, DbInstIdLess>;

// Pulls per-instance power (internal + switching + leakage) from OpenSTA
// and re-evaluates leakage at each instance's local temperature.
class PowerExtractor
{
 public:
  PowerExtractor(sta::dbSta* sta, utl::Logger* logger);

  void setCorner(sta::Scene* corner) { corner_ = corner; }
  sta::Scene* corner() const { return corner_; }
  // Activity scale multiplies the propagated switching activity (see
  // ThermalConfig::default_activity_scale and ActivityPhase).
  void setActivityScale(double scale) { activity_scale_ = scale; }
  bool readActivityFile(const std::string& file, const std::string& scope);

  // Fills dynamic_power_w and leakage_power_w (at temperature_c, using the
  // leakage model) for every placed instance in the block.
  void extract(odb::dbBlock* block,
               const LeakageModel& leakage_model,
               double nominal_temp_c,
               InstancePhysicsMap& state);

  double totalPowerW(const InstancePhysicsMap& state) const;
  double leakagePowerW(const InstancePhysicsMap& state) const;

 private:
  sta::dbSta* sta_;
  utl::Logger* logger_;
  sta::Scene* corner_ = nullptr;
  double activity_scale_ = 1.0;
};

// Applies per-instance delay derates through OpenSTA's existing derating
// mechanism (Sta::setTimingDerate for instances) so that every timing report
// and the resizer see physics-aware timing while enabled.
class DerateApplier
{
 public:
  DerateApplier(sta::dbSta* sta, utl::Logger* logger);

  void apply(const InstancePhysicsMap& state);
  void clear();
  bool isApplied() const { return applied_; }

  // Worst slack / total negative slack of the design at the current STA state.
  void timingSummary(double& wns_s, double& tns_s);
  // Worst slack per instance, filled into slack_nominal_s or slack_derated_s.
  void instanceSlacks(InstancePhysicsMap& state, bool derated);

 private:
  sta::dbSta* sta_;
  utl::Logger* logger_;
  bool applied_ = false;
};

// Runs PDNSim on the supply nets with the temperature-aware instance powers
// and reads the per-instance supply voltage back.  Optionally corrects metal
// resistance for temperature: R(T) = R0 (1 + alpha (T - T0)).
class IrDropCoupling
{
 public:
  IrDropCoupling(psm::PDNSim* psm, sta::dbSta* sta, utl::Logger* logger);

  bool available() const { return psm_ != nullptr; }
  // Returns false when no power grid / supply net is available.
  bool run(odb::dbBlock* block,
           const ThermalConfig& config,
           const ThermalGrid& grid,
           InstancePhysicsMap& state,
           double& worst_drop_v,
           MapSnapshot* ir_map);

 private:
  psm::PDNSim* psm_;
  sta::dbSta* sta_;
  utl::Logger* logger_;
};

// Black's-equation relative lifetime of power-grid segments.
class ElectromigrationModel
{
 public:
  explicit ElectromigrationModel(const ElectromigrationConfig& config);

  // Relative MTTF of a segment at (current density j, temperature T)
  // versus (j_ref, T_ref): (j_ref/j)^n * exp(Ea/k (1/T - 1/T_ref)).
  double relativeLifetime(double j_a_m2,
                          double temp_c,
                          double j_ref_a_m2) const;
  // Worst relative lifetime over the grid segments reported by PDNSim.
  double worstLifetimeFactor(const ThermalGrid& grid,
                             const std::vector<std::pair<odb::Rect, double>>&
                                 segment_current_densities) const;

 private:
  ElectromigrationConfig config_;
};

// Thermal clock skew: the additional skew introduced by temperature and IR
// differences between clock tree branches (post-CTS).
class ClockSkewAnalyzer
{
 public:
  ClockSkewAnalyzer(sta::dbSta* sta, utl::Logger* logger);
  // Returns worst |skew_derated - skew_nominal| across clocks.
  double skewDeltaS(DerateApplier& derates, const InstancePhysicsMap& state);

 private:
  sta::dbSta* sta_;
  utl::Logger* logger_;
};

}  // namespace thm
