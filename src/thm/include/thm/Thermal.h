// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <map>
#include <memory>
#include <string>
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
}  // namespace utl

namespace web {
class HeatMapSourceRegistration;
}  // namespace web

namespace thm {

// User facing knobs for analyze_thermal.  Distances are in microns,
// temperatures in degrees Celsius.
struct ThermalOptions
{
  // HotSpot executable; resolved through PATH when not an absolute path.
  std::string hotspot_binary = "hotspot";
  // HotSpot configuration file.  Empty means the built-in default config.
  std::string hotspot_config;
  // Directory for the generated HotSpot inputs/outputs.  Empty means a
  // temporary directory that is removed unless keep_files is set.
  std::string work_dir;
  bool keep_files = false;
  // Size of the square floorplan tiles that instance power is binned into
  // before being handed to HotSpot.  0 means automatic (die / 32).
  double tile_size_um = 0.0;
  // HotSpot grid model resolution.
  int grid_rows = 64;
  int grid_cols = 64;
  // Ambient (heat sink) temperature.
  double ambient_c = 45.0;
  // Number of instances to list for the hottest region in the report.
  int report_instances = 10;
  // Optional file to write the text report to.
  std::string report_file;
};

// Steady state temperature grid over `bounds`.  Row 0 is the bottom (minimum
// y) row and column 0 is the left most (minimum x) column.
struct TemperatureGrid
{
  odb::Rect bounds;
  int rows = 0;
  int cols = 0;
  std::vector<double> temps_c;  // row-major, rows * cols entries

  bool empty() const { return temps_c.empty(); }
  double at(int row, int col) const { return temps_c[row * cols + col]; }
  double& at(int row, int col) { return temps_c[row * cols + col]; }
  odb::Rect cellRect(int row, int col) const;
  // Grid cell containing the point, clamped to the grid.
  std::pair<int, int> cellAt(const odb::Point& point) const;
};

struct ThermalStats
{
  double peak_c = 0.0;
  double average_c = 0.0;
  double min_c = 0.0;
  int peak_row = -1;
  int peak_col = -1;
  // Cell of the peak temperature.
  odb::Rect hottest_region;
  // Instances overlapping hottest_region sorted by decreasing power. Names
  // are copied so the report stays valid if the design changes afterwards.
  struct Instance
  {
    std::string name;
    std::string master;
    double power_w = 0.0;
  };
  std::vector<Instance> hottest_insts;
  // Total power fed to HotSpot in watts (instance power clipped to the die).
  double total_power_w = 0.0;
};

// Power binned into a rectangular tile of the die; one HotSpot floorplan
// unit per tile.
struct PowerTile
{
  std::string name;
  odb::Rect rect;
  double power_w = 0.0;
};

class ThermalAnalyzer
{
 public:
  ThermalAnalyzer(odb::dbDatabase* db, sta::dbSta* sta, utl::Logger* logger);
  ~ThermalAnalyzer();

  // Runs the full pipeline: per instance power from OpenSTA -> tiles ->
  // HotSpot floorplan/power trace -> HotSpot grid model -> temperature grid.
  // Returns true on success; results are available through the getters.
  void analyze(sta::Scene* corner, const ThermalOptions& options);

  bool hasResults() const { return !grid_.empty(); }
  const TemperatureGrid& getTemperatureGrid() const { return grid_; }
  const ThermalStats& getStats() const { return stats_; }
  sta::Scene* getLastCorner() const { return last_corner_; }
  void clearResults();

  // Per placed instance total power in watts for the corner.
  odb::PtrMap<odb::dbInst, double> getInstancePower(sta::Scene* corner) const;

  // Number of tile {rows, cols} buildTiles creates for bounds and tile_size.
  static std::pair<int, int> tileCounts(const odb::Rect& bounds, int tile_size);

  // Bins instance power into square tiles of tile_size (dbu) covering
  // bounds; the tile count is rounded so the tiles cover bounds exactly.
  static std::vector<PowerTile> buildTiles(
      const odb::Rect& bounds,
      int tile_size,
      const odb::PtrMap<odb::dbInst, double>& inst_power);

  // Computes peak/average/min, the total power inside the die and the
  // hottest region and its instances.
  static ThermalStats computeStats(
      const TemperatureGrid& grid,
      const odb::PtrMap<odb::dbInst, double>& inst_power,
      int max_instances);
  void checkGridSize(int rows, int cols, const odb::Rect& bounds) const;

  // Logs the report; also writes it to `file` when non-empty.
  void report(const std::string& file = "") const;

  // Writes the grid as CSV (x_min,y_min,x_max,y_max,temperature) in microns.
  void writeTemperatureCsv(const std::string& file) const;

  // Loads a temperature grid directly (bypassing HotSpot); used by tests and
  // to view externally produced results.  The file has one row per line,
  // bottom row first, comma separated temperatures in Celsius.
  void readTemperatureGrid(const std::string& file);

  // Analysis area: the die area.
  odb::Rect getBounds() const;
  odb::dbBlock* getBlock() const;

 private:
  odb::dbDatabase* db_;
  sta::dbSta* sta_;
  utl::Logger* logger_;

  TemperatureGrid grid_;
  ThermalStats stats_;
  sta::Scene* last_corner_ = nullptr;

  std::shared_ptr<web::HeatMapSourceRegistration> heatmap_source_;
};

}  // namespace thm
