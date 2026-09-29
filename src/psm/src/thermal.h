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

struct ThermalHotRegion
{
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
  utl::Logger* logger_;
  sta::dbSta* sta_;
  ThermalGrid grid_;
  std::optional<ThermalHotRegion> hot_region_;
  std::optional<std::pair<double, double>> color_range_;
};

}  // namespace psm
