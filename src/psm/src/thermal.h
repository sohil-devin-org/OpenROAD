// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "odb/PtrSetMap.h"
#include "odb/db.h"
#include "odb/geom.h"

namespace sta {
class dbSta;
class Scene;
}  // namespace sta

namespace utl {
class Logger;
}

namespace psm {

// Settings for a single analyze_thermal run.
struct ThermalSettings
{
  // HotSpot executable. Empty: use $HOTSPOT, then "hotspot" from PATH.
  std::string hotspot_exe;
  // Optional HotSpot config file (-c). Empty: use built-in defaults.
  std::string hotspot_config;
  // Directory for HotSpot inputs/outputs. Empty: a temporary directory that
  // is removed after the run.
  std::string work_dir;
  // Read an existing HotSpot -grid_steady_file instead of running HotSpot.
  std::string grid_file;
  // HotSpot grid size (both must be powers of 2).
  int grid_rows = 64;
  int grid_cols = 64;
  double ambient_c = 45.0;
  sta::Scene* corner = nullptr;
  // Number of instances in the hottest region to list in the report.
  int max_instances = 10;
  // Optional file receiving every instance in the hottest region.
  std::string report_file;
};

// Steady-state silicon-layer temperature grid in absolute degrees Celsius.
// Row 0 is at bounds.yMin(), column 0 at bounds.xMin().
class ThermalGrid
{
 public:
  ThermalGrid() = default;
  ThermalGrid(const odb::Rect& bounds,
              int rows,
              int cols,
              std::vector<double> temperatures_c);

  bool empty() const { return temperatures_c_.empty(); }
  const odb::Rect& getBounds() const { return bounds_; }
  int getRows() const { return rows_; }
  int getCols() const { return cols_; }
  double getTemperature(int row, int col) const;
  odb::Rect getTileRect(int row, int col) const;
  // (row, col) of the tile containing pt; nullopt if pt is outside bounds.
  std::optional<std::pair<int, int>> findTile(const odb::Point& pt) const;
  // Temperature of the tile containing pt; nullopt if pt is outside bounds.
  std::optional<double> getTemperatureAt(const odb::Point& pt) const;
  const std::vector<double>& getTemperatures() const { return temperatures_c_; }

  double getMin() const;
  double getMax() const;
  double getAverage() const;

 private:
  odb::Rect bounds_;
  int rows_ = 0;
  int cols_ = 0;
  std::vector<double> temperatures_c_;
};

// Connected (4-neighbor) set of tiles containing the peak tile whose
// temperature is at least peak - kHotRegionFraction * (peak - min).
struct ThermalHotRegion
{
  static constexpr double kHotRegionFraction = 0.1;

  odb::Rect bounds;
  std::vector<std::pair<int, int>> tiles;  // (row, col)
  double peak_c = 0.0;
  double average_c = 0.0;
  // Instances whose center is in the region, sorted by decreasing power (W).
  std::vector<std::pair<odb::dbInst*, float>> instances;
};

class ThermalAnalyzer
{
 public:
  ThermalAnalyzer(utl::Logger* logger, sta::dbSta* sta);

  // Runs the full flow: collect power, write HotSpot inputs, run HotSpot,
  // read the grid, report. Returns false on failure.
  bool analyze(odb::dbBlock* block,
               const ThermalSettings& settings,
               const odb::PtrMap<odb::dbInst, std::map<sta::Scene*, float>>&
                   user_powers);

  const ThermalGrid& getGrid() const { return grid_; }
  // Total instance power (W) of the last successful run.
  double getTotalPower() const { return total_power_; }
  const std::optional<ThermalHotRegion>& getHotRegion() const
  {
    return hot_region_;
  }
  void clear();

  // Fixed color range for the heat map (degrees C). nullopt: auto range.
  void setColorRange(std::optional<std::pair<double, double>> range)
  {
    color_range_ = range;
  }
  std::optional<std::pair<double, double>> getColorRange() const
  {
    return color_range_;
  }

 private:
  using InstancePowers = std::vector<std::pair<odb::dbInst*, float>>;

  void validate(odb::dbBlock* block, const ThermalSettings& settings) const;
  InstancePowers collectInstancePower(
      odb::dbBlock* block,
      sta::Scene* corner,
      const odb::PtrMap<odb::dbInst, std::map<sta::Scene*, float>>& user_powers)
      const;
  std::string findHotSpot(const ThermalSettings& settings) const;
  void writeFloorplan(odb::dbBlock* block,
                      const InstancePowers& powers,
                      int tile_rows,
                      int tile_cols,
                      const std::string& flp_file,
                      const std::string& ptrace_file) const;
  void writeConfig(odb::dbBlock* block, const std::string& config_file) const;
  void runHotSpot(const std::vector<std::string>& argv,
                  const std::string& work_dir,
                  const std::string& log_file) const;
  std::string runHotSpotFlow(odb::dbBlock* block,
                             const ThermalSettings& settings,
                             const InstancePowers& powers,
                             const std::string& work_dir) const;
  ThermalGrid readGridFile(const std::string& grid_file,
                           const odb::Rect& bounds,
                           int rows,
                           int cols) const;
  ThermalHotRegion findHotRegion(const InstancePowers& powers) const;
  void report(odb::dbBlock* block,
              const ThermalSettings& settings,
              sta::Scene* corner) const;
  void writeReportFile(const std::string& report_file) const;

  utl::Logger* logger_;
  sta::dbSta* sta_;
  ThermalGrid grid_;
  double total_power_ = 0.0;
  std::optional<ThermalHotRegion> hot_region_;
  std::optional<std::pair<double, double>> color_range_;
};

}  // namespace psm
