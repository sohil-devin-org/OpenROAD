// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "odb/db.h"
#include "thm/PhysicsState.h"
#include "thm/ThermalConfig.h"

namespace sta {
class dbSta;
class Instance;
class Scene;
class Sdc;
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
class PowerMap;
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
  // Reads an "instance_name scale" text file (see README, "Activity file");
  // the listed instances have their switching + internal power multiplied by
  // scale on top of the global activity scale.  Re-reading the same path is
  // a no-op; an empty path clears the per-instance scales.  Returns false if
  // the file cannot be read (a THM warning is issued, nothing is applied).
  bool readActivityFile(const std::string& file, const std::string& scope);
  int numInstanceScales() const { return instance_scales_.size(); }
  // Design-level total (internal + switching + leakage) reported by OpenSTA
  // for the selected corner, i.e. what report_power prints as "Total".
  double designTotalPowerW() const;

  // Fills dynamic_power_w and leakage_power_w (at temperature_c, using the
  // leakage model) for every placed instance in the block.
  void extract(odb::dbBlock* block,
               const LeakageModel& leakage_model,
               double nominal_temp_c,
               InstancePhysicsMap& state);

  double totalPowerW(const InstancePhysicsMap& state) const;
  double leakagePowerW(const InstancePhysicsMap& state) const;

 private:
  sta::Scene* effectiveCorner() const;

  sta::dbSta* sta_;
  utl::Logger* logger_;
  sta::Scene* corner_ = nullptr;
  double activity_scale_ = 1.0;
  std::string activity_file_;
  std::map<std::string, double> instance_scales_;
  bool warned_total_mismatch_ = false;
};

// Applies per-instance delay derates through OpenSTA's existing derating
// mechanism (Sta::setTimingDerate for instances) so that every timing report
// and the resizer see physics-aware timing while enabled.
class DerateApplier
{
 public:
  DerateApplier(sta::dbSta* sta, utl::Logger* logger);

  void setCorner(sta::Scene* corner) { corner_ = corner; }

  // Pushes InstancePhysics::derate of every instance into the SDC of the
  // corner as an instance cell-delay derate on both clock and data paths
  // (early and late).  Incremental: instances whose derate is 1.0 +- 1e-6
  // are not derated, and instances whose derate did not change since the
  // last call are not touched.  The factors in effect before the first
  // application (user set_timing_derate, cell/global fallbacks) are
  // remembered and multiplied, so the physics derate composes with them.
  void apply(const InstancePhysicsMap& state);
  // Restores the remembered pre-physics factors of every derated instance
  // (nominal timing is recovered exactly); user derates are preserved.
  void clear();
  bool isApplied() const { return applied_; }
  int numDerated() const { return applied_derates_.size(); }

  // Worst slack / total negative slack of the design at the current STA state.
  void timingSummary(double& wns_s, double& tns_s);
  // Worst slack per instance, filled into slack_nominal_s or slack_derated_s.
  void instanceSlacks(InstancePhysicsMap& state, bool derated);

 private:
  // Effective cell-delay derate of an instance for
  // [clk_or_data][rise/fall][early/late] as seen by the timer.
  using Factors = std::array<float, 8>;
  static int factorIndex(int clk_data, int rf, int el)
  {
    return (clk_data * 2 + rf) * 2 + el;
  }
  sta::Scene* effectiveCorner() const;
  Factors readFactors(sta::Sdc* sdc, sta::Instance* inst) const;
  void writeFactors(sta::Sdc* sdc,
                    sta::Instance* inst,
                    const Factors& base,
                    double scale);

  sta::dbSta* sta_;
  utl::Logger* logger_;
  sta::Scene* corner_ = nullptr;
  bool applied_ = false;
  // Physics derate currently in the SDC per instance, and the factors that
  // were in effect before it was first applied.
  std::map<odb::dbInst*, double, DbInstIdLess> applied_derates_;
  std::map<odb::dbInst*, Factors, DbInstIdLess> baseline_;
};

// Runs PDNSim on the supply net with the temperature-aware instance powers
// (PDNSim::setInstPower) and reads the IR drop of the lowest routed layer of
// the grid back into InstancePhysics::vdd_v = nominal - drop and the tile
// map ir_map.  The net is ThermalConfig::ir_power_net or the first routed
// POWER net; voltage sources come from ThermalConfig::ir_vsrc_file (the
// analyze_power_grid -vsrc format) or, when empty, from PDNSim's own source
// settings (bterms / set_pdnsim_source_settings / generated sources).
// The grid resistance is temperature independent in this version: PDNSim
// has no public hook to scale its resistors, so em.metal_tcr_per_k is not
// applied here (see README, "IR drop").
class IrDropCoupling
{
 public:
  IrDropCoupling(psm::PDNSim* psm, sta::dbSta* sta, utl::Logger* logger);

  void setCorner(sta::Scene* corner) { corner_ = corner; }
  bool available() const { return psm_ != nullptr; }
  // Forget a previous failure so the next run() analyzes the grid again.
  void reset();
  // Returns false (vdd_v left at nominal, one THM warning) when no power
  // grid / supply net / sources are available or the analysis fails.
  bool run(odb::dbBlock* block,
           const ThermalConfig& config,
           const ThermalGrid& grid,
           InstancePhysicsMap& state,
           double& worst_drop_v,
           MapSnapshot* ir_map);

  // Power net used by the last run (configured name or discovered).
  static odb::dbNet* findPowerNet(odb::dbBlock* block, const std::string& name);
  // Lowest routing layer carrying special wires of the net.
  static odb::dbTechLayer* lowestLayer(odb::dbNet* net);

 private:
  sta::Scene* effectiveCorner() const;

  psm::PDNSim* psm_;
  sta::dbSta* sta_;
  utl::Logger* logger_;
  sta::Scene* corner_ = nullptr;
  bool warned_no_grid_ = false;
  bool warned_failed_ = false;
  bool warned_vdd_ = false;
  // A failed analysis is not retried until reset(); PDNSim logs an error
  // for every attempt on an unconnected grid.
  bool failed_ = false;
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
  // Relative lifetime of every lateral tile of the active layer of die
  // versus the reference temperature.  The current density proxy is the
  // tile current I = P_tile / VDD_tile with VDD_tile = nominal - IR drop
  // (J is proportional to the current the grid segments feeding the tile
  // carry); J_ref is the mean tile current of the powered tiles.  When
  // ir_map is null (no IR-drop analysis) the current term is 1.0 and only
  // the Arrhenius temperature term varies.  Tiles without power get the
  // temperature-only value so that the map stays finite.
  MapSnapshot tileLifetimeMap(const ThermalGrid& grid,
                              const PowerMap& power,
                              const MapSnapshot* ir_map,
                              double nominal_vdd_v,
                              int die) const;
  // Minimum over the tiles of a map produced by tileLifetimeMap (1.0 when
  // the map is empty).
  static double worstFactor(const MapSnapshot& map);

 private:
  ElectromigrationConfig config_;
};

// Thermal clock skew: the additional skew introduced by temperature and IR
// differences between clock tree branches (post-CTS).
class ClockSkewAnalyzer
{
 public:
  ClockSkewAnalyzer(sta::dbSta* sta, utl::Logger* logger);
  // Returns skew_derated - skew_nominal of the worst (largest magnitude)
  // register-to-register clock skew across clocks; the derates are applied
  // / cleared through `derates` and the applier state is restored.
  double skewDeltaS(DerateApplier& derates, const InstancePhysicsMap& state);
  // Worst clock skew (s) of the current STA state (with whatever derates
  // are applied right now); 0 without clocks or registers.
  double worstSkewS();

 private:
  sta::dbSta* sta_;
  utl::Logger* logger_;
};

}  // namespace thm
