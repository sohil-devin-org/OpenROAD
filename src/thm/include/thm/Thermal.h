// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "odb/db.h"
#include "thm/LeakageModel.h"
#include "thm/PhysicsCoupling.h"
#include "thm/PhysicsState.h"
#include "thm/ThermalConfig.h"
#include "thm/ThermalGrid.h"
#include "thm/ThermalSolver.h"

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

namespace web {
class HeatMapSourceRegistration;
using HeatMapSourceHandle = std::shared_ptr<HeatMapSourceRegistration>;
}  // namespace web

namespace thm {

// Observer notified as physics snapshots are produced (GUI physics panel,
// live placement view, animation recorders).
class PhysicsObserver
{
 public:
  virtual ~PhysicsObserver() = default;
  virtual void onAnalysisBegin(const std::string& label) {}
  // Called after every electrothermal loop iteration with the running
  // metrics (not yet recorded in the history).
  virtual void onLoopIteration(const PhysicsMetrics& metrics) {}
  // Called after a snapshot has been appended to the history.
  virtual void onSnapshot(const PhysicsSnapshot& snapshot,
                          const PhysicsHistory& history)
  {
  }
  virtual void onHistoryCleared() {}
  virtual void onReferenceLoaded(const PhysicsHistory& reference) {}
};

struct AnalyzeOptions
{
  // Transient replay of the configured (or the named) activity phases
  // instead of one steady-state solve.
  bool transient = false;
  std::vector<std::string> phases;
  // Die to report (-1 = all).
  int die = -1;
  // Snapshot label / iteration / placer metrics, filled by the placer.
  std::string label = "analyze_thermal";
  int iteration = 0;
  double hpwl_um = 0.0;
  double overflow = 0.0;
  double physics_weight = 0.0;
  // Whether to record a snapshot into the history.
  bool record = true;
  // Skip the IR-drop and STA timing parts (thermal + leakage only); the
  // placer uses this for cheap intermediate checkpoints.
  bool include_ir_drop = true;
  bool include_timing = true;
  // Warm start the solver from the previous temperature field.
  bool warm_start = true;
  sta::Scene* corner = nullptr;
};

enum class AnimationType
{
  kCooldown,
  kTransient,
  kElectrothermal,
  kCompare,
  kStack
};

struct AnimationOptions
{
  AnimationType type = AnimationType::kCooldown;
  std::string format = "gif";  // "gif" or "mp4"
  std::string file;
  // Fixed absolute color scale in C (min/max); when both are zero the scale
  // is derived from the histories but still shared between all frames.
  double scale_min_c = 0.0;
  double scale_max_c = 0.0;
  int fps = 4;
  int width_px = 800;
  int die = -1;
  std::string title;
};

// Physics analysis engine: thermal grid and solvers, temperature-dependent
// leakage, electrothermal loop, IR-drop and timing derate coupling, history
// and snapshots, heat-map data sources.  Used standalone through the Tcl
// commands and by the global placer's -physics_driven mode.
class Thermal
{
 public:
  Thermal(odb::dbDatabase* db,
          sta::dbSta* sta,
          psm::PDNSim* psm,
          utl::Logger* logger);
  ~Thermal();

  // ---- configuration -----------------------------------------------------
  ThermalConfig& config() { return config_; }
  const ThermalConfig& config() const { return config_; }
  void setConfig(const ThermalConfig& config);
  // Generic key/value setter used by set_thermal_config and config files.
  // Throws (logger error) on unknown key or bad value.
  void setConfigValue(const std::string& key, const std::string& value);
  void readConfigFile(const std::string& path);
  void addActivityPhase(const ActivityPhase& phase);
  void clearActivityPhases();
  std::string configReport() const;

  LeakageModel& leakageModel() { return *leakage_model_; }
  DerateModel& derateModel() { return *derate_model_; }
  // Fit both models from the multi-corner libraries currently loaded (or
  // from the given corner list).  Returns number of cells fitted.
  int characterizeLibraries(const std::vector<LibraryCorner>& corners);
  bool loadLibraryFits(const std::string& leakage_json,
                       const std::string& derate_json);

  // ---- analysis ----------------------------------------------------------
  // Full coupled analysis at the current placement (Section 4.2 data flow).
  // Records a snapshot into the history when options.record is set and
  // returns the final metrics.
  PhysicsMetrics analyze(const AnalyzeOptions& options);
  bool hasResults() const { return has_results_; }
  void reset();

  // ---- results -----------------------------------------------------------
  const ThermalGrid& grid() const { return grid_; }
  const std::vector<PowerMap>& powerMaps() const { return power_maps_; }
  const InstancePhysicsMap& instancePhysics() const { return inst_state_; }
  const InstancePhysics* instancePhysics(odb::dbInst* inst) const;
  const PhysicsMetrics& lastMetrics() const { return last_metrics_; }
  // Temperature (C) of the active layer at a DBU location.
  double temperatureAt(int x_dbu, int y_dbu, int die = 0) const;
  // Leakage power density (W/m^2) map and derate map on the lateral grid.
  MapSnapshot leakageDensityMap(int die = 0) const;
  MapSnapshot derateMap(int die = 0) const;
  MapSnapshot temperatureMap(int die = 0) const;
  const MapSnapshot* irDropMap() const;
  MapSnapshot emRiskMap(int die = 0) const;

  PhysicsHistory& history() { return history_; }
  const PhysicsHistory& history() const { return history_; }
  const PhysicsHistory* reference() const { return reference_.get(); }
  void loadReference(const std::string& path);
  void clearReference();

  // ---- timing derates ----------------------------------------------------
  void setDeratingEnabled(bool enable);
  bool deratingEnabled() const { return derating_enabled_; }

  // ---- reporting / export ------------------------------------------------
  void reportPhysics(const std::string& json_file);
  // map_name: temperature | leakage | derate | delta | ir_drop | em
  void writeThermalMap(const std::string& file,
                       const std::string& map_name,
                       int die);
  void writeHistory(const std::string& path) const;
  void writeAnimation(const AnimationOptions& options);

  // ---- placer interface --------------------------------------------------
  // Screening length (in DBU) of the lateral heat equation for the current
  // config and die: used by the placer's screened-Poisson spreading term.
  double screeningLengthDbu() const;
  // Per-instance power (W) used for the power-weighted density field.
  double instancePowerW(odb::dbInst* inst) const;
  // Total power of the last extraction.
  double totalPowerW() const { return last_metrics_.total_power_w; }

  // ---- observers ---------------------------------------------------------
  void addObserver(PhysicsObserver* observer);
  void removeObserver(PhysicsObserver* observer);

  odb::dbDatabase* db() const { return db_; }
  sta::dbSta* sta() const { return sta_; }
  utl::Logger* logger() const { return logger_; }

  // ---- GUI history playback ----------------------------------------------
  // Index of the history snapshot the Temperature heat map draws instead of
  // the live grid; -1 (default) draws the live grid.  Refreshes the physics
  // heat maps.
  void setDisplayedSnapshot(int index);
  int displayedSnapshot() const { return displayed_snapshot_; }
  // The snapshot selected with setDisplayedSnapshot, or nullptr when the
  // live grid is displayed (or the index is out of range).
  const PhysicsSnapshot* displayedSnapshotData() const;
  // Temperature map (C) the heat map draws: the displayed history snapshot
  // when one is selected, otherwise the live grid.
  MapSnapshot displayedTemperatureMap(int die = 0) const;
  // Re-populate every registered physics heat map instance.
  void refreshHeatMaps();

 private:
  odb::dbBlock* getBlock() const;
  void ensureGrid();
  void buildPowerMaps();
  void updateInstanceTemperatures();
  // One electrothermal fixed-point loop; returns iterations and sets the
  // convergence flags in metrics.
  void electrothermalLoop(const AnalyzeOptions& options,
                          PhysicsMetrics& metrics);
  void runTransient(const AnalyzeOptions& options, PhysicsMetrics& metrics);
  void runIrDrop(PhysicsMetrics& metrics);
  void runTiming(PhysicsMetrics& metrics);
  PhysicsSnapshot makeSnapshot(const PhysicsMetrics& metrics) const;
  void recordSnapshot(const PhysicsMetrics& metrics);
  void logMetrics(const PhysicsMetrics& metrics) const;

  odb::dbDatabase* db_;
  sta::dbSta* sta_;
  psm::PDNSim* psm_;
  utl::Logger* logger_;

  ThermalConfig config_;
  std::unique_ptr<ThermalSolver> solver_;
  std::unique_ptr<LeakageModel> leakage_model_;
  std::unique_ptr<DerateModel> derate_model_;
  std::unique_ptr<PowerExtractor> power_extractor_;
  std::unique_ptr<DerateApplier> derate_applier_;
  std::unique_ptr<IrDropCoupling> ir_coupling_;

  ThermalGrid grid_;
  std::vector<PowerMap> power_maps_;
  InstancePhysicsMap inst_state_;
  PhysicsMetrics last_metrics_;
  MapSnapshot ir_map_;
  bool has_ir_map_ = false;
  bool has_results_ = false;
  bool derating_enabled_ = false;
  double nominal_temp_c_ = 25.0;

  PhysicsHistory history_;
  std::unique_ptr<PhysicsHistory> reference_;
  std::set<PhysicsObserver*> observers_;

  std::vector<web::HeatMapSourceHandle> heatmap_sources_;
  int displayed_snapshot_ = -1;
};

}  // namespace thm
