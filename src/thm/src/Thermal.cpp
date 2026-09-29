// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "thm/Thermal.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "heatMap.h"
#include "hotspot.h"
#include "odb/db.h"
#include "sta/PowerClass.hh"
#include "sta/Scene.hh"
#include "utl/Logger.h"
#include "web/heatMap.h"

namespace thm {

namespace {

// Largest temperature grid that is accepted (analysis or import); HotSpot
// itself becomes impractical well before this.
constexpr int64_t kMaxGridCells = int64_t{1} << 20;

// Block names may contain path separators; keep the HotSpot files inside the
// work directory.
std::string fileSafeName(const std::string& name)
{
  std::string safe = name;
  for (char& c : safe) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-'
        && c != '.') {
      c = '_';
    }
  }
  if (safe.empty() || safe == "." || safe == "..") {
    safe = "design";
  }
  return safe;
}

bool parseTemperature(const std::string& token, double& value)
{
  char* end = nullptr;
  value = std::strtod(token.c_str(), &end);
  return end != token.c_str() && *end == '\0' && std::isfinite(value);
}

}  // namespace

odb::Rect TemperatureGrid::cellRect(int row, int col) const
{
  const double dx = static_cast<double>(bounds.dx()) / cols;
  const double dy = static_cast<double>(bounds.dy()) / rows;
  const int x0 = bounds.xMin() + std::lround(col * dx);
  const int x1 = (col == cols - 1)
                     ? bounds.xMax()
                     : bounds.xMin() + std::lround((col + 1) * dx);
  const int y0 = bounds.yMin() + std::lround(row * dy);
  const int y1 = (row == rows - 1)
                     ? bounds.yMax()
                     : bounds.yMin() + std::lround((row + 1) * dy);
  return odb::Rect(x0, y0, x1, y1);
}

std::pair<int, int> TemperatureGrid::cellAt(const odb::Point& point) const
{
  const double dx = static_cast<double>(bounds.dx()) / cols;
  const double dy = static_cast<double>(bounds.dy()) / rows;
  int col = static_cast<int>((point.x() - bounds.xMin()) / dx);
  int row = static_cast<int>((point.y() - bounds.yMin()) / dy);
  col = std::clamp(col, 0, cols - 1);
  row = std::clamp(row, 0, rows - 1);
  return {row, col};
}

ThermalAnalyzer::ThermalAnalyzer(odb::dbDatabase* db,
                                 sta::dbSta* sta,
                                 utl::Logger* logger)
    : db_(db), sta_(sta), logger_(logger)
{
  heatmap_source_ = web::registerHeatMapSource(
      "Temperature", "Temperature", "Temperature", [this, logger]() {
        return std::make_shared<TemperatureDataSource>(logger, this);
      });
}

ThermalAnalyzer::~ThermalAnalyzer() = default;

odb::dbBlock* ThermalAnalyzer::getBlock() const
{
  odb::dbChip* chip = db_->getChip();
  if (chip == nullptr) {
    return nullptr;
  }
  return chip->getBlock();
}

odb::Rect ThermalAnalyzer::getBounds() const
{
  odb::dbBlock* block = getBlock();
  if (block == nullptr) {
    return odb::Rect();
  }
  return block->getDieArea();
}

void ThermalAnalyzer::clearResults()
{
  grid_ = TemperatureGrid();
  stats_ = ThermalStats();
  last_corner_ = nullptr;
  results_block_ = nullptr;
  results_owner_.removeOwner();
  if (heatmap_source_) {
    heatmap_source_->invalidateInstances();
  }
}

odb::PtrMap<odb::dbInst, double> ThermalAnalyzer::getInstancePower(
    sta::Scene* corner) const
{
  odb::PtrMap<odb::dbInst, double> powers;
  odb::dbBlock* block = getBlock();
  if (block == nullptr) {
    return powers;
  }
  sta_->ensureGraph();
  sta_->ensureLevelized();
  sta::dbNetwork* network = sta_->getDbNetwork();
  for (odb::dbInst* inst : block->getInsts()) {
    if (!inst->getPlacementStatus().isPlaced()) {
      continue;
    }
    if (inst->getMaster()->isCover()) {
      continue;
    }
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst == nullptr) {
      continue;
    }
    sta::PowerResult power = sta_->power(sta_inst, corner);
    powers[inst] = power.total();
  }
  return powers;
}

std::pair<int, int> ThermalAnalyzer::tileCounts(const odb::Rect& bounds,
                                                int tile_size)
{
  if (tile_size <= 0 || bounds.dx() <= 0 || bounds.dy() <= 0) {
    return {0, 0};
  }
  // Round the tile count so the tiles cover the die exactly (no sliver
  // tiles at the far edges); tile edges are then die_size * i / count.
  const int rows = std::max(
      1, static_cast<int>(std::lround(bounds.dy() / double(tile_size))));
  const int cols = std::max(
      1, static_cast<int>(std::lround(bounds.dx() / double(tile_size))));
  return {rows, cols};
}

std::vector<PowerTile> ThermalAnalyzer::buildTiles(
    const odb::Rect& bounds,
    int tile_size,
    const odb::PtrMap<odb::dbInst, double>& inst_power)
{
  std::vector<PowerTile> tiles;
  const auto [rows, cols] = tileCounts(bounds, tile_size);
  if (rows == 0 || cols == 0) {
    return tiles;
  }
  const auto x_edge = [&](int col) {
    return bounds.xMin()
           + static_cast<int>(static_cast<int64_t>(bounds.dx()) * col / cols);
  };
  const auto y_edge = [&](int row) {
    return bounds.yMin()
           + static_cast<int>(static_cast<int64_t>(bounds.dy()) * row / rows);
  };
  tiles.reserve(static_cast<size_t>(rows) * cols);
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      PowerTile tile;
      tile.name = "t" + std::to_string(row) + "_" + std::to_string(col);
      tile.rect = odb::Rect(
          x_edge(col), y_edge(row), x_edge(col + 1), y_edge(row + 1));
      tiles.push_back(tile);
    }
  }

  // Distribute each instance's power over the tiles it overlaps, weighted
  // by overlap area; the part of an instance outside the die is not modeled.
  const auto col_at = [&](int x) {
    const int64_t offset = x - bounds.xMin();
    return static_cast<int>(
        std::clamp<int64_t>(offset * cols / bounds.dx(), 0, cols - 1));
  };
  const auto row_at = [&](int y) {
    const int64_t offset = y - bounds.yMin();
    return static_cast<int>(
        std::clamp<int64_t>(offset * rows / bounds.dy(), 0, rows - 1));
  };
  for (const auto& [inst, power] : inst_power) {
    if (power <= 0.0) {
      continue;
    }
    odb::Rect box = inst->getBBox()->getBox();
    odb::Rect clipped = box.intersect(bounds);
    if (clipped.area() == 0) {
      continue;
    }
    const int col0 = std::max(0, col_at(clipped.xMin()) - 1);
    const int col1 = std::min(cols - 1, col_at(clipped.xMax() - 1) + 1);
    const int row0 = std::max(0, row_at(clipped.yMin()) - 1);
    const int row1 = std::min(rows - 1, row_at(clipped.yMax() - 1) + 1);
    const double area = static_cast<double>(box.area());
    for (int row = row0; row <= row1; row++) {
      for (int col = col0; col <= col1; col++) {
        PowerTile& tile = tiles[row * cols + col];
        if (!tile.rect.overlaps(clipped)) {
          continue;
        }
        const double overlap
            = static_cast<double>(tile.rect.intersect(clipped).area());
        tile.power_w += power * overlap / area;
      }
    }
  }
  return tiles;
}

ThermalStats ThermalAnalyzer::computeStats(
    const TemperatureGrid& grid,
    const odb::PtrMap<odb::dbInst, double>& inst_power,
    int max_instances)
{
  ThermalStats stats;
  if (grid.empty()) {
    return stats;
  }
  stats.peak_c = -std::numeric_limits<double>::infinity();
  stats.min_c = std::numeric_limits<double>::infinity();
  double sum = 0.0;
  for (int row = 0; row < grid.rows; row++) {
    for (int col = 0; col < grid.cols; col++) {
      const double t = grid.at(row, col);
      sum += t;
      stats.min_c = std::min(stats.min_c, t);
      if (t > stats.peak_c) {
        stats.peak_c = t;
        stats.peak_row = row;
        stats.peak_col = col;
      }
    }
  }
  stats.average_c = sum / grid.temps_c.size();
  stats.hottest_region = grid.cellRect(stats.peak_row, stats.peak_col);

  // Same clipping as buildTiles, so the total matches the HotSpot trace.
  std::vector<ThermalStats::Instance> insts;
  for (const auto& [inst, power] : inst_power) {
    const odb::Rect bbox = inst->getBBox()->getBox();
    const double area = static_cast<double>(bbox.area());
    if (area > 0 && bbox.overlaps(grid.bounds)) {
      const double inside
          = static_cast<double>(bbox.intersect(grid.bounds).area());
      stats.total_power_w += power * inside / area;
    }
    if (bbox.intersects(stats.hottest_region)) {
      insts.push_back({inst->getName(), inst->getMaster()->getName(), power});
    }
  }
  std::sort(insts.begin(), insts.end(), [](const auto& a, const auto& b) {
    if (a.power_w != b.power_w) {
      return a.power_w > b.power_w;
    }
    return a.name < b.name;
  });
  if (max_instances >= 0 && static_cast<int>(insts.size()) > max_instances) {
    insts.resize(max_instances);
  }
  stats.hottest_insts = std::move(insts);
  return stats;
}

void ThermalAnalyzer::analyze(sta::Scene* corner, const ThermalOptions& options)
{
  // A failed analysis must not leave the previous results behind.
  clearResults();
  odb::dbBlock* block = getBlock();
  if (block == nullptr) {
    logger_->error(utl::THM, 1, "No design loaded.");
  }
  const odb::Rect bounds = getBounds();
  if (bounds.dx() <= 0 || bounds.dy() <= 0) {
    logger_->error(
        utl::THM, 2, "Die area is not set; initialize the floorplan first.");
  }
  if (options.grid_rows <= 0 || options.grid_cols <= 0) {
    logger_->error(utl::THM, 3, "Grid rows and columns must be positive.");
  }
  checkGridSize(options.grid_rows, options.grid_cols, bounds);

  const int dbu = block->getDbUnitsPerMicron();

  HotSpotAdapter hotspot(logger_, dbu);
  const std::string binary = hotspot.findBinary(options.hotspot_binary);
  if (binary.empty()) {
    logger_->error(utl::THM,
                   4,
                   "HotSpot executable \"{}\" not found; use -hotspot_binary "
                   "or add it to PATH.",
                   options.hotspot_binary);
  }

  odb::PtrMap<odb::dbInst, double> inst_power = getInstancePower(corner);
  if (inst_power.empty()) {
    logger_->error(utl::THM, 5, "No placed instances found.");
  }

  int tile_size = static_cast<int>(std::lround(options.tile_size_um * dbu));
  if (options.tile_size_um > 0 && tile_size < 1) {
    logger_->error(utl::THM,
                   18,
                   "-tile_size {} um is smaller than a database unit ({} um).",
                   options.tile_size_um,
                   1.0 / dbu);
  }
  if (tile_size <= 0) {
    tile_size = std::max(1, std::max(bounds.dx(), bounds.dy()) / 32);
  }
  const auto [tile_rows, tile_cols] = tileCounts(bounds, tile_size);
  const int64_t tile_count = static_cast<int64_t>(tile_rows) * tile_cols;
  if (tile_count > HotSpotAdapter::kMaxUnits) {
    logger_->error(
        utl::THM,
        19,
        "{} x {} tiles of {:.2f} um exceed HotSpot's limit of {} "
        "floorplan units; increase -tile_size to at least {:.2f} um.",
        tile_cols,
        tile_rows,
        tile_size / double(dbu),
        HotSpotAdapter::kMaxUnits,
        std::sqrt(static_cast<double>(bounds.dx()) * bounds.dy()
                  / HotSpotAdapter::kMaxUnits)
            / dbu);
  }
  std::vector<PowerTile> tiles = buildTiles(bounds, tile_size, inst_power);
  double total_power = 0.0;
  for (const auto& tile : tiles) {
    total_power += tile.power_w;
  }
  logger_->info(utl::THM,
                10,
                "Thermal analysis: {} instances, {} tiles of {:.2f} um, total "
                "power {:.4g} W, HotSpot grid {}x{}.",
                inst_power.size(),
                tiles.size(),
                static_cast<double>(tile_size) / dbu,
                total_power,
                options.grid_rows,
                options.grid_cols);
  if (total_power <= 0.0) {
    logger_->warn(utl::THM,
                  11,
                  "Total instance power is zero; the temperature map will be "
                  "uniform at ambient. Check liberty/activity setup.");
  }

  grid_ = runHotSpot(hotspot, binary, tiles, bounds, options);
  results_block_ = block;
  results_owner_.addOwner(block);
  last_corner_ = corner;
  stats_ = computeStats(grid_, inst_power, options.report_instances);
  if (heatmap_source_) {
    heatmap_source_->invalidateInstances();
  }
  report(options.report_file);
}

TemperatureGrid ThermalAnalyzer::runHotSpot(HotSpotAdapter& hotspot,
                                            const std::string& binary,
                                            const std::vector<PowerTile>& tiles,
                                            const odb::Rect& bounds,
                                            const ThermalOptions& options) const
{
  std::filesystem::path work_dir;
  bool remove_work_dir = false;
  if (!options.work_dir.empty()) {
    work_dir = options.work_dir;
    std::filesystem::create_directories(work_dir);
  } else {
    // A fresh directory per run, so -keep_files output is never removed by a
    // later run in the same session.
    std::string dir_template
        = (std::filesystem::temp_directory_path() / "openroad_thermal_XXXXXX")
              .string();
    if (::mkdtemp(dir_template.data()) == nullptr) {
      logger_->error(utl::THM,
                     21,
                     "Cannot create a work directory under {}; use -work_dir.",
                     std::filesystem::temp_directory_path().string());
    }
    work_dir = dir_template;
    remove_work_dir = !options.keep_files;
  }
  const std::string base
      = (work_dir / fileSafeName(getBlock()->getName())).string();
  const std::string flp = base + ".flp";
  const std::string ptrace = base + ".ptrace";
  const std::string steady = base + ".steady";
  const std::string grid_steady = base + ".grid.steady";
  const std::string log = base + ".hotspot.log";
  std::string config = options.hotspot_config;
  if (config.empty()) {
    config = base + ".config";
    hotspot.writeConfig(
        config, options.ambient_c, options.grid_rows, options.grid_cols);
  }

  hotspot.writeFloorplan(flp, tiles, bounds);
  hotspot.writePowerTrace(ptrace, tiles);

  bool ok = hotspot.run(binary,
                        config,
                        flp,
                        ptrace,
                        options.grid_rows,
                        options.grid_cols,
                        steady,
                        grid_steady,
                        log);
  TemperatureGrid grid;
  if (ok) {
    ok = hotspot.readGridSteady(
        grid_steady, options.grid_rows, options.grid_cols, bounds, grid);
  }
  // Keep the files of a failed run so the HotSpot log can be inspected.
  if (remove_work_dir && ok) {
    std::error_code ec;
    std::filesystem::remove_all(work_dir, ec);
  } else {
    logger_->info(utl::THM, 12, "HotSpot files kept in {}.", work_dir.string());
  }
  if (!ok) {
    logger_->error(utl::THM, 6, "HotSpot thermal analysis failed.");
  }
  return grid;
}

void ThermalAnalyzer::checkGridSize(int rows,
                                    int cols,
                                    const odb::Rect& bounds) const
{
  if (rows > bounds.dy() || cols > bounds.dx()) {
    logger_->error(utl::THM,
                   22,
                   "Grid {}x{} is finer than the die ({} x {} database units); "
                   "reduce -grid_rows/-grid_cols.",
                   cols,
                   rows,
                   bounds.dx(),
                   bounds.dy());
  }
  if (static_cast<int64_t>(rows) * cols > kMaxGridCells) {
    logger_->error(utl::THM,
                   23,
                   "Grid {}x{} exceeds the limit of {} cells.",
                   cols,
                   rows,
                   kMaxGridCells);
  }
}

void ThermalAnalyzer::report(const std::string& file) const
{
  if (!hasResults()) {
    logger_->warn(
        utl::THM, 13, "No thermal results; run analyze_thermal first.");
    return;
  }
  odb::dbBlock* block = getBlock();
  const double dbu = block->getDbUnitsPerMicron();
  const odb::Rect& region = stats_.hottest_region;

  std::ostringstream out;
  out << "Thermal analysis report\n";
  out << "-----------------------\n";
  out << fmt::format(
      "Grid:                {} x {} cells over die {:.2f} x {:.2f} um\n",
      grid_.cols,
      grid_.rows,
      grid_.bounds.dx() / dbu,
      grid_.bounds.dy() / dbu);
  out << fmt::format("Total power:         {:.4g} W\n", stats_.total_power_w);
  out << fmt::format("Peak temperature:    {:.2f} C\n", stats_.peak_c);
  out << fmt::format("Average temperature: {:.2f} C\n", stats_.average_c);
  out << fmt::format("Min temperature:     {:.2f} C\n", stats_.min_c);
  out << fmt::format(
      "Hottest region:      ({:.2f}, {:.2f}) - ({:.2f}, {:.2f}) um "
      "[row {}, col {}]\n",
      region.xMin() / dbu,
      region.yMin() / dbu,
      region.xMax() / dbu,
      region.yMax() / dbu,
      stats_.peak_row,
      stats_.peak_col);
  out << fmt::format("Instances in hottest region ({} shown):\n",
                     stats_.hottest_insts.size());
  for (const ThermalStats::Instance& inst : stats_.hottest_insts) {
    out << fmt::format(
        "  {:<40} {:<30} {:.4g} W\n", inst.name, inst.master, inst.power_w);
  }

  logger_->report("{}", out.str());
  if (!file.empty()) {
    std::ofstream stream(file);
    if (!stream) {
      logger_->error(utl::THM, 7, "Cannot open report file {}.", file);
    }
    stream << out.str();
  }
}

void ThermalAnalyzer::writeTemperatureCsv(const std::string& file) const
{
  if (!hasResults()) {
    logger_->error(
        utl::THM, 8, "No thermal results; run analyze_thermal first.");
  }
  std::ofstream stream(file);
  if (!stream) {
    logger_->error(utl::THM, 9, "Cannot open {}.", file);
  }
  const double dbu = getBlock()->getDbUnitsPerMicron();
  stream << "x_min,y_min,x_max,y_max,temperature_c\n";
  for (int row = 0; row < grid_.rows; row++) {
    for (int col = 0; col < grid_.cols; col++) {
      const odb::Rect rect = grid_.cellRect(row, col);
      stream << fmt::format("{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}\n",
                            rect.xMin() / dbu,
                            rect.yMin() / dbu,
                            rect.xMax() / dbu,
                            rect.yMax() / dbu,
                            grid_.at(row, col));
    }
  }
}

void ThermalAnalyzer::readTemperatureGrid(const std::string& file)
{
  odb::dbBlock* block = getBlock();
  if (block == nullptr) {
    logger_->error(utl::THM, 17, "No design loaded.");
  }
  std::ifstream stream(file);
  if (!stream) {
    logger_->error(utl::THM, 14, "Cannot open temperature grid {}.", file);
  }
  TemperatureGrid grid;
  grid.bounds = getBounds();
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::vector<double> row;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token.empty()) {
        continue;
      }
      double value = 0.0;
      if (!parseTemperature(token, value)) {
        logger_->error(
            utl::THM, 24, "Invalid temperature \"{}\" in {}.", token, file);
      }
      row.push_back(value);
    }
    if (row.empty()) {
      continue;
    }
    if (grid.cols == 0) {
      grid.cols = row.size();
    } else if (static_cast<int>(row.size()) != grid.cols) {
      logger_->error(utl::THM, 15, "Inconsistent row length in {}.", file);
    }
    // Bound the grid while reading, before anything more is stored.
    checkGridSize(grid.rows + 1, grid.cols, grid.bounds);
    grid.temps_c.insert(grid.temps_c.end(), row.begin(), row.end());
    grid.rows++;
  }
  if (grid.empty()) {
    logger_->error(utl::THM, 16, "No temperatures found in {}.", file);
  }
  clearResults();
  grid_ = std::move(grid);
  results_block_ = block;
  results_owner_.addOwner(block);
  last_corner_ = sta_->cmdScene();
  stats_ = computeStats(grid_, getInstancePower(last_corner_), 10);
  if (heatmap_source_) {
    heatmap_source_->invalidateInstances();
  }
}

}  // namespace thm
