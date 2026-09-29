// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <map>
#include <string>
#include <vector>

#include "odb/db.h"

namespace thm {

// Per-instance coupled physics state at the last analysis.
struct InstancePhysics
{
  double dynamic_power_w = 0.0;    // internal + switching (from OpenSTA)
  double leakage_power_w = 0.0;    // leakage at the local temperature
  double library_leakage_w = 0.0;  // OpenSTA leakage at library temperature
  double temperature_c = 0.0;      // local temperature of the active layer
  double vdd_v = 0.0;              // local supply voltage (nominal or IR)
  double derate = 1.0;             // delay multiplier f_T(T) * f_V(V)
  double slack_nominal_s = 0.0;    // worst pin slack without derates
  double slack_derated_s = 0.0;    // worst pin slack with derates
  int die = 0;
};

// Metrics recorded at every physics checkpoint / loop iteration.
struct PhysicsMetrics
{
  int iteration = 0;                // placer iteration or loop index
  std::string label;                // e.g. "checkpoint 2", "loop 3"
  std::vector<double> peak_temp_c;  // per die
  std::vector<double> avg_temp_c;   // per die
  double max_gradient_c_per_mm = 0.0;
  double worst_ir_drop_v = 0.0;
  double wns_nominal_s = 0.0;
  double tns_nominal_s = 0.0;
  double wns_derated_s = 0.0;
  double tns_derated_s = 0.0;
  double total_power_w = 0.0;
  double leakage_power_w = 0.0;
  double hpwl_um = 0.0;
  double overflow = 0.0;
  int electrothermal_iterations = 0;
  bool converged = true;
  bool runaway = false;
  double em_lifetime_factor = 1.0;  // worst segment, relative to reference
  double clock_skew_delta_s = 0.0;
  double physics_weight = 0.0;
  double runtime_s = 0.0;

  double peakTemp() const;
};

// Compact cell position used by the timeline / animation.
struct InstancePosition
{
  std::string name;
  int x_dbu = 0;
  int y_dbu = 0;
};

// A 2D map (nx*ny) with its bounding box and units, serializable.
struct MapSnapshot
{
  std::string name;   // "temperature", "ir_drop", "leakage", ...
  std::string units;  // "C", "V", "W"
  int die = 0;
  int nx = 0;
  int ny = 0;
  std::vector<double> values;
  double at(int x, int y) const { return values[y * nx + x]; }
};

// Everything the GUI, charts, comparison mode and animation export need
// about one point in time.
struct PhysicsSnapshot
{
  PhysicsMetrics metrics;
  std::vector<MapSnapshot> maps;
  std::vector<InstancePosition> positions;
  // Bounding box of the die in DBU and the DBU scale.
  int die_xmin = 0;
  int die_ymin = 0;
  int die_xmax = 0;
  int die_ymax = 0;
  double dbu_per_micron = 1.0;
  // Free-form tag e.g. "baseline", "physics_driven", "stress:hot_block".
  std::string tag;
  // For transient replay: simulated time in seconds.
  double time_s = 0.0;

  const MapSnapshot* findMap(const std::string& name, int die = 0) const;
};

// Ordered history of snapshots recorded during a run.  Serializable to JSON so
// a baseline run can be reloaded for comparison mode and animation export.
class PhysicsHistory
{
 public:
  void clear() { snapshots_.clear(); }
  void add(PhysicsSnapshot snapshot);
  bool empty() const { return snapshots_.empty(); }
  int size() const { return snapshots_.size(); }
  const PhysicsSnapshot& at(int i) const { return snapshots_.at(i); }
  const PhysicsSnapshot& back() const { return snapshots_.back(); }
  const std::vector<PhysicsSnapshot>& snapshots() const { return snapshots_; }

  const std::string& designName() const { return design_name_; }
  void setDesignName(const std::string& name) { design_name_ = name; }
  const std::string& tag() const { return tag_; }
  void setTag(const std::string& tag) { tag_ = tag; }

  // JSON serialization; throws std::runtime_error on failure.
  void writeJson(const std::string& path) const;
  void readJson(const std::string& path);
  std::string toJson() const;
  void fromJson(const std::string& json);

 private:
  std::string design_name_;
  std::string tag_;
  std::vector<PhysicsSnapshot> snapshots_;
};

}  // namespace thm
