// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "hotspot.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "utl/Logger.h"

namespace thm {

namespace {

// Derived from HotSpot's examples/example1/example.config with the
// comments removed.  Parameters that analyze_thermal controls (ambient,
// initial temperature, model type, grid resolution and output files) are
// appended by writeConfig / passed on the command line.
constexpr const char* kDefaultConfig
    = R"(# HotSpot configuration written by OpenROAD analyze_thermal.
# Based on HotSpot examples/example1/example.config.
# chip specs
-t_chip 0.00015
-k_chip 130.0
-p_chip 1630300
# heat sink specs
-c_convec 140.4
-r_convec 0.1
-s_sink 0.06
-t_sink 0.0069
-k_sink 400.0
-p_sink 3.55e6
# heat spreader specs
-s_spreader 0.03
-t_spreader 0.001
-k_spreader 400.0
-p_spreader 3.55e6
# interface material specs
-t_interface 2.0e-05
-k_interface 4.0
-p_interface 4.0e6
# secondary path (grid model only)
-model_secondary 0
-r_convec_sec 50.0
-c_convec_sec 40.0
-n_metal 8
-t_metal 100.0e-6
-t_c4 0.0001
-s_c4 20.0e-6
-n_c4 400
-s_sub 0.021
-t_sub 0.001
-s_solder 0.021
-t_solder 0.00094
-s_pcb 0.1
-t_pcb 0.002
# others
-sampling_intvl 0.01
-base_proc_freq 3e+09
-dtm_used 0
-leakage_used 0
-leakage_mode 0
-package_model_used 0
-block_omit_lateral 0
-grid_map_mode avg
-use_microfluidic_cooling 0
)";

// HotSpot's flp.h MAX_UNITS.
constexpr size_t kMaxHotSpotUnits = 8192;

bool isExecutable(const std::filesystem::path& path)
{
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec)
         && access(path.c_str(), X_OK) == 0;
}

std::string shellQuote(const std::string& arg)
{
  std::string quoted = "'";
  for (const char c : arg) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted += c;
    }
  }
  quoted += "'";
  return quoted;
}

bool parseLayerHeader(const std::string& line, int& layer)
{
  // "Layer <n>:"
  if (line.rfind("Layer", 0) != 0) {
    return false;
  }
  std::istringstream ss(line.substr(5));
  if (!(ss >> layer)) {
    return false;
  }
  return true;
}

}  // namespace

HotSpotAdapter::HotSpotAdapter(utl::Logger* logger, int dbu_per_micron)
    : logger_(logger), dbu_per_micron_(dbu_per_micron)
{
}

std::string HotSpotAdapter::findBinary(const std::string& binary) const
{
  if (binary.empty()) {
    return "";
  }
  std::error_code ec;
  if (binary.find('/') != std::string::npos) {
    const std::filesystem::path path(binary);
    if (isExecutable(path)) {
      return std::filesystem::absolute(path, ec).string();
    }
    return "";
  }
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr) {
    return "";
  }
  // An empty PATH entry (leading, trailing or doubled colon) means the
  // current directory, as in execvp.
  const std::string path_list = std::string(path_env) + ':';
  std::stringstream paths(path_list);
  std::string dir;
  while (std::getline(paths, dir, ':')) {
    if (dir.empty()) {
      dir = ".";
    }
    const std::filesystem::path candidate = std::filesystem::path(dir) / binary;
    if (isExecutable(candidate)) {
      return candidate.string();
    }
  }
  return "";
}

void HotSpotAdapter::writeFloorplan(const std::string& file,
                                    const std::vector<PowerTile>& tiles,
                                    const odb::Rect& bounds) const
{
  if (tiles.size() > kMaxHotSpotUnits) {
    logger_->warn(utl::THM,
                  30,
                  "{} floorplan tiles exceed HotSpot's limit of {} units; "
                  "increase -tile_size.",
                  tiles.size(),
                  kMaxHotSpotUnits);
  }
  std::ofstream stream(file);
  if (!stream) {
    logger_->error(utl::THM, 31, "Cannot open HotSpot floorplan {}.", file);
  }
  // DBU -> meters; the floorplan origin is the die lower left corner.
  const double scale = 1e-6 / dbu_per_micron_;
  stream << "# HotSpot floorplan written by OpenROAD analyze_thermal\n";
  stream << "# Line Format: <unit-name>\t<width>\t<height>\t<left-x>\t"
            "<bottom-y> (meters)\n";
  for (const PowerTile& tile : tiles) {
    stream << fmt::format("{}\t{:.9g}\t{:.9g}\t{:.9g}\t{:.9g}\n",
                          tile.name,
                          tile.rect.dx() * scale,
                          tile.rect.dy() * scale,
                          (tile.rect.xMin() - bounds.xMin()) * scale,
                          (tile.rect.yMin() - bounds.yMin()) * scale);
  }
}

void HotSpotAdapter::writePowerTrace(const std::string& file,
                                     const std::vector<PowerTile>& tiles) const
{
  std::ofstream stream(file);
  if (!stream) {
    logger_->error(utl::THM, 32, "Cannot open HotSpot power trace {}.", file);
  }
  std::string names;
  std::string powers;
  for (const PowerTile& tile : tiles) {
    if (!names.empty()) {
      names += '\t';
      powers += '\t';
    }
    names += tile.name;
    powers += fmt::format("{:.6g}", tile.power_w);
  }
  stream << names << '\n' << powers << '\n';
}

void HotSpotAdapter::writeConfig(const std::string& file,
                                 double ambient_c,
                                 int grid_rows,
                                 int grid_cols) const
{
  std::ofstream stream(file);
  if (!stream) {
    logger_->error(utl::THM, 33, "Cannot open HotSpot config {}.", file);
  }
  const double ambient_k = ambient_c + kKelvinOffset;
  stream << kDefaultConfig;
  stream << "# analyze_thermal overrides\n";
  stream << fmt::format("-ambient {:.3f}\n", ambient_k);
  stream << fmt::format("-init_temp {:.3f}\n", ambient_k);
  stream << "-model_type grid\n";
  stream << fmt::format("-grid_rows {}\n", grid_rows);
  stream << fmt::format("-grid_cols {}\n", grid_cols);
}

bool HotSpotAdapter::run(const std::string& binary,
                         const std::string& config,
                         const std::string& floorplan,
                         const std::string& ptrace,
                         int grid_rows,
                         int grid_cols,
                         const std::string& steady_file,
                         const std::string& grid_steady_file,
                         const std::string& log_file) const
{
  std::error_code ec;
  std::filesystem::remove(grid_steady_file, ec);

  const std::string command = fmt::format(
      "{} -c {} -f {} -p {} -steady_file {} -model_type grid "
      "-grid_rows {} -grid_cols {} -grid_steady_file {} > {} 2>&1",
      shellQuote(binary),
      shellQuote(config),
      shellQuote(floorplan),
      shellQuote(ptrace),
      shellQuote(steady_file),
      grid_rows,
      grid_cols,
      shellQuote(grid_steady_file),
      shellQuote(log_file));
  debugPrint(logger_, utl::THM, "hotspot", 1, "Running: {}", command);

  const int status = std::system(command.c_str());
  int exit_code = -1;
  if (status != -1 && WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
  }
  if (exit_code != 0) {
    logger_->warn(utl::THM,
                  34,
                  "HotSpot exited with status {}; see {} for details.",
                  exit_code,
                  log_file);
    return false;
  }
  if (!std::filesystem::is_regular_file(grid_steady_file, ec)) {
    logger_->warn(utl::THM,
                  35,
                  "HotSpot did not write the grid temperatures {}; see {} for "
                  "details.",
                  grid_steady_file,
                  log_file);
    return false;
  }
  return true;
}

bool HotSpotAdapter::readGridSteady(const std::string& file,
                                    int grid_rows,
                                    int grid_cols,
                                    const odb::Rect& bounds,
                                    TemperatureGrid& grid) const
{
  std::ifstream stream(file);
  if (!stream) {
    logger_->warn(
        utl::THM, 36, "Cannot open HotSpot grid temperatures {}.", file);
    return false;
  }

  const size_t cells = static_cast<size_t>(grid_rows) * grid_cols;
  std::vector<double> kelvin(cells, 0.0);
  std::vector<bool> seen(cells, false);
  size_t count = 0;
  bool in_silicon = false;
  std::string line;
  int line_no = 0;
  while (std::getline(stream, line)) {
    line_no++;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    int layer = -1;
    if (parseLayerHeader(line, layer)) {
      if (in_silicon) {
        break;  // done with layer 0
      }
      in_silicon = (layer == 0);
      continue;
    }
    if (!in_silicon) {
      continue;
    }
    std::istringstream ss(line);
    size_t index = 0;
    double temp = 0.0;
    if (!(ss >> index >> temp) || index >= cells) {
      logger_->warn(utl::THM,
                    37,
                    "Unexpected entry \"{}\" at line {} of {}.",
                    line,
                    line_no,
                    file);
      return false;
    }
    if (seen[index]) {
      logger_->warn(utl::THM,
                    39,
                    "Duplicate grid index {} at line {} of {}.",
                    index,
                    line_no,
                    file);
      return false;
    }
    seen[index] = true;
    count++;
    kelvin[index] = temp;
  }
  if (count != cells) {
    logger_->warn(utl::THM,
                  38,
                  "{} has {} silicon layer entries; expected {} for a {}x{} "
                  "grid.",
                  file,
                  count,
                  cells,
                  grid_rows,
                  grid_cols);
    return false;
  }

  // HotSpot indexes cells row-major from the top-left corner of the chip;
  // TemperatureGrid row 0 is the bottom row.
  grid.bounds = bounds;
  grid.rows = grid_rows;
  grid.cols = grid_cols;
  grid.temps_c.assign(cells, 0.0);
  for (int row = 0; row < grid_rows; row++) {
    const int hotspot_row = grid_rows - 1 - row;
    for (int col = 0; col < grid_cols; col++) {
      grid.at(row, col) = kelvin[hotspot_row * grid_cols + col] - kKelvinOffset;
    }
  }
  return true;
}

}  // namespace thm
