// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "thermal.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "odb/PtrSetMap.h"
#include "odb/db.h"
#include "odb/geom.h"
#include "sta/Liberty.hh"
#include "sta/NetworkClass.hh"
#include "sta/PowerClass.hh"
#include "sta/Scene.hh"
#include "utl/Logger.h"

namespace psm {

namespace {

constexpr double kKelvinOffset = 273.15;
// HotSpot reads each power trace line into a 64 KiB buffer, which bounds the
// number of floorplan blocks.
constexpr int kMaxTilesPerSide = 32;
// Package defaults are HotSpot's own, but the spreader and sink must be at
// least as large as the die (and the sink at least as large as the spreader).
constexpr double kMinSpreaderSide = 30e-3;
constexpr double kMinSinkSide = 60e-3;
constexpr int kLogTailLines = 20;
constexpr int kMaxGridSide = 1024;

bool isValidGridSide(const int value)
{
  return value > 0 && value <= kMaxGridSide && (value & (value - 1)) == 0;
}

double dbuToMeters(odb::dbBlock* block, const int64_t dbu)
{
  return block->dbuToMicrons(dbu) * 1e-6;
}

// Temporary work directory removed on scope exit (including on errors).
class TempDir
{
 public:
  explicit TempDir(utl::Logger* logger)
  {
    std::error_code ec;
    const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
    std::string templ = ((ec ? std::filesystem::path("/tmp") : base)
                         / "openroad_thermal_XXXXXX")
                            .string();
    if (mkdtemp(templ.data()) == nullptr) {
      logger->error(utl::PSM,
                    211,
                    "Unable to create temporary directory: {}.",
                    std::strerror(errno));
    }
    path_ = templ;
  }
  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

std::string fileTail(const std::string& file, const int lines)
{
  std::ifstream in(file);
  std::deque<std::string> tail;
  std::string line;
  while (std::getline(in, line)) {
    tail.push_back(line);
    if (tail.size() > static_cast<size_t>(lines)) {
      tail.pop_front();
    }
  }
  std::string text;
  for (const std::string& l : tail) {
    text += "\n  " + l;
  }
  return text;
}

bool isExecutable(const std::filesystem::path& path)
{
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec)
         && access(path.c_str(), X_OK) == 0;
}

}  // namespace

ThermalGrid::ThermalGrid(const odb::Rect& bounds,
                         int rows,
                         int cols,
                         std::vector<double> temperatures_c)
    : bounds_(bounds),
      rows_(rows),
      cols_(cols),
      temperatures_c_(std::move(temperatures_c))
{
}

double ThermalGrid::getTemperature(int row, int col) const
{
  return temperatures_c_[(row * cols_) + col];
}

odb::Rect ThermalGrid::getTileRect(int row, int col) const
{
  const int64_t width = bounds_.dx();
  const int64_t height = bounds_.dy();
  const int x0 = bounds_.xMin() + static_cast<int>(width * col / cols_);
  const int x1 = bounds_.xMin() + static_cast<int>(width * (col + 1) / cols_);
  const int y0 = bounds_.yMin() + static_cast<int>(height * row / rows_);
  const int y1 = bounds_.yMin() + static_cast<int>(height * (row + 1) / rows_);
  return {x0, y0, x1, y1};
}

std::optional<std::pair<int, int>> ThermalGrid::findTile(
    const odb::Point& pt) const
{
  if (rows_ == 0 || cols_ == 0 || !bounds_.intersects(pt)) {
    return std::nullopt;
  }
  // Initial guess, then correct for the integer rounding of tile edges.
  int col = static_cast<int>(static_cast<int64_t>(pt.x() - bounds_.xMin())
                             * cols_ / std::max(1, bounds_.dx()));
  int row = static_cast<int>(static_cast<int64_t>(pt.y() - bounds_.yMin())
                             * rows_ / std::max(1, bounds_.dy()));
  col = std::clamp(col, 0, cols_ - 1);
  row = std::clamp(row, 0, rows_ - 1);
  while (col > 0 && pt.x() < getTileRect(row, col).xMin()) {
    col--;
  }
  while (col < cols_ - 1 && pt.x() >= getTileRect(row, col).xMax()) {
    col++;
  }
  while (row > 0 && pt.y() < getTileRect(row, col).yMin()) {
    row--;
  }
  while (row < rows_ - 1 && pt.y() >= getTileRect(row, col).yMax()) {
    row++;
  }
  return std::make_pair(row, col);
}

std::optional<double> ThermalGrid::getTemperatureAt(const odb::Point& pt) const
{
  const auto tile = findTile(pt);
  if (!tile) {
    return std::nullopt;
  }
  return getTemperature(tile->first, tile->second);
}

double ThermalGrid::getMin() const
{
  return *std::min_element(temperatures_c_.begin(), temperatures_c_.end());
}

double ThermalGrid::getMax() const
{
  return *std::max_element(temperatures_c_.begin(), temperatures_c_.end());
}

double ThermalGrid::getAverage() const
{
  return std::accumulate(temperatures_c_.begin(), temperatures_c_.end(), 0.0)
         / temperatures_c_.size();
}

ThermalAnalyzer::ThermalAnalyzer(utl::Logger* logger, sta::dbSta* sta)
    : logger_(logger), sta_(sta)
{
}

bool ThermalAnalyzer::analyze(
    odb::dbBlock* block,
    const ThermalSettings& settings,
    const odb::PtrMap<odb::dbInst, std::map<sta::Scene*, float>>& user_powers)
{
  clear();
  validate(block, settings);

  sta::Scene* corner
      = settings.corner != nullptr ? settings.corner : sta_->cmdScene();

  // Fail fast on a missing simulator before doing any work.
  std::string hotspot;
  if (settings.grid_file.empty()) {
    hotspot = findHotSpot(settings);
  }

  const InstancePowers powers
      = collectInstancePower(block, corner, user_powers);
  double total_power = 0.0;
  for (const auto& [inst, power] : powers) {
    total_power += power;
  }

  std::string grid_file = settings.grid_file;
  std::unique_ptr<TempDir> temp_dir;
  if (grid_file.empty()) {
    std::string work_dir = settings.work_dir;
    if (work_dir.empty()) {
      temp_dir = std::make_unique<TempDir>(logger_);
      work_dir = temp_dir->path();
    } else {
      std::error_code ec;
      std::filesystem::create_directories(work_dir, ec);
      if (ec) {
        logger_->error(utl::PSM,
                       212,
                       "Unable to create work directory {}: {}.",
                       work_dir,
                       ec.message());
      }
    }
    ThermalSettings run_settings = settings;
    run_settings.hotspot_exe = hotspot;
    grid_file = runHotSpotFlow(block, run_settings, powers, work_dir);
  } else {
    logger_->info(
        utl::PSM, 205, "Reading HotSpot grid temperatures from {}.", grid_file);
  }

  ThermalGrid grid = readGridFile(
      grid_file, block->getDieArea(), settings.grid_rows, settings.grid_cols);

  grid_ = std::move(grid);
  total_power_ = total_power;
  hot_region_ = findHotRegion(powers);

  report(block, settings, corner);
  if (!settings.report_file.empty()) {
    writeReportFile(settings.report_file);
  }
  return true;
}

void ThermalAnalyzer::validate(odb::dbBlock* block,
                               const ThermalSettings& settings) const
{
  if (!isValidGridSide(settings.grid_rows)
      || !isValidGridSide(settings.grid_cols)) {
    logger_->error(utl::PSM,
                   206,
                   "Thermal grid rows and columns must be powers of 2 no "
                   "larger than {} ({} x {}).",
                   kMaxGridSide,
                   settings.grid_rows,
                   settings.grid_cols);
  }
  const odb::Rect die = block->getDieArea();
  if (die.dx() <= 0 || die.dy() <= 0) {
    logger_->error(
        utl::PSM, 207, "Design {} has no die area.", block->getName());
  }
  const auto& insts = block->getInsts();
  const bool placed
      = std::any_of(insts.begin(), insts.end(), [](odb::dbInst* inst) {
          return inst->isPlaced();
        });
  if (!placed) {
    logger_->error(utl::PSM,
                   208,
                   "Design {} has no placed instances. Run analyze_thermal "
                   "after placement.",
                   block->getName());
  }
  if (!settings.hotspot_config.empty()
      && !std::filesystem::is_regular_file(settings.hotspot_config)) {
    logger_->error(utl::PSM,
                   209,
                   "HotSpot configuration file {} does not exist.",
                   settings.hotspot_config);
  }
}

ThermalAnalyzer::InstancePowers ThermalAnalyzer::collectInstancePower(
    odb::dbBlock* block,
    sta::Scene* corner,
    const odb::PtrMap<odb::dbInst, std::map<sta::Scene*, float>>& user_powers)
    const
{
  sta::dbNetwork* network = sta_->getDbNetwork();
  InstancePowers powers;
  int unplaced = 0;
  int no_power = 0;
  for (odb::dbInst* inst : block->getInsts()) {
    if (!inst->isPlaced()) {
      unplaced++;
      continue;
    }
    // set_pdnsim_inst_power overrides OpenSTA, as in analyze_power_grid.
    std::optional<float> power;
    const auto user = user_powers.find(inst);
    if (user != user_powers.end()) {
      auto find_power = user->second.find(corner);
      if (find_power == user->second.end()) {
        find_power = user->second.find(nullptr);
      }
      if (find_power != user->second.end()) {
        power = find_power->second;
      }
    }
    if (!power) {
      sta::Instance* sta_inst = network->dbToSta(inst);
      if (network->libertyCell(sta_inst) != nullptr) {
        power = sta_->power(sta_inst, corner).total();
      }
    }
    if (!power) {
      if (!inst->getMaster()->isFiller()) {
        no_power++;
        debugPrint(logger_,
                   utl::PSM,
                   "thermal",
                   1,
                   "Instance {} ({}) has no Liberty power.",
                   inst->getName(),
                   inst->getMaster()->getName());
      }
      continue;
    }
    powers.emplace_back(inst, *power);
  }
  if (unplaced > 0) {
    logger_->warn(utl::PSM,
                  210,
                  "{} unplaced instances are ignored by thermal analysis.",
                  unplaced);
  }
  if (no_power > 0) {
    logger_->warn(utl::PSM,
                  213,
                  "{} placed instances have no Liberty power and no "
                  "set_pdnsim_inst_power value; they dissipate no power.",
                  no_power);
  }
  return powers;
}

std::string ThermalAnalyzer::findHotSpot(const ThermalSettings& settings) const
{
  std::string exe = settings.hotspot_exe;
  if (exe.empty()) {
    const char* env = std::getenv("HOTSPOT");
    exe = (env != nullptr && *env != '\0') ? env : "hotspot";
  }
  if (exe.find('/') != std::string::npos) {
    if (isExecutable(exe)) {
      return std::filesystem::absolute(exe).string();
    }
  } else if (const char* path = std::getenv("PATH")) {
    std::istringstream dirs(path);
    std::string dir;
    while (std::getline(dirs, dir, ':')) {
      const std::filesystem::path candidate
          = std::filesystem::path(dir.empty() ? "." : dir) / exe;
      if (isExecutable(candidate)) {
        return std::filesystem::absolute(candidate).string();
      }
    }
  }
  logger_->error(utl::PSM,
                 214,
                 "HotSpot executable {} not found. Use -hotspot, set the "
                 "HOTSPOT environment variable, or add hotspot to PATH.",
                 exe);
  return "";
}

void ThermalAnalyzer::writeFloorplan(odb::dbBlock* block,
                                     const InstancePowers& powers,
                                     const int tile_rows,
                                     const int tile_cols,
                                     const std::string& flp_file,
                                     const std::string& ptrace_file) const
{
  const odb::Rect die = block->getDieArea();
  const ThermalGrid tiles(
      die, tile_rows, tile_cols, std::vector<double>(tile_rows * tile_cols));
  std::vector<double> tile_power(tile_rows * tile_cols, 0.0);

  // Spread each instance's power over the tiles it overlaps by area.
  for (const auto& [inst, power] : powers) {
    const odb::Rect box = inst->getBBox()->getBox();
    odb::Rect clipped;
    box.intersection(die, clipped);
    const int64_t area = box.intersects(die) ? clipped.area() : 0;
    if (area <= 0) {
      odb::Point center = box.center();
      center = {std::clamp(center.x(), die.xMin(), die.xMax()),
                std::clamp(center.y(), die.yMin(), die.yMax())};
      const auto tile = tiles.findTile(center);
      tile_power[(tile->first * tile_cols) + tile->second] += power;
      continue;
    }
    const auto ll = tiles.findTile(clipped.ll());
    const auto ur = tiles.findTile(clipped.ur());
    for (int row = ll->first; row <= ur->first; row++) {
      for (int col = ll->second; col <= ur->second; col++) {
        odb::Rect overlap;
        tiles.getTileRect(row, col).intersection(clipped, overlap);
        tile_power[(row * tile_cols) + col]
            += power * static_cast<double>(overlap.area()) / area;
      }
    }
  }

  std::ofstream flp(flp_file);
  std::ofstream ptrace(ptrace_file);
  if (!flp || !ptrace) {
    logger_->error(utl::PSM,
                   215,
                   "Unable to write HotSpot inputs {} and {}.",
                   flp_file,
                   ptrace_file);
  }
  flp << "# OpenROAD thermal floorplan of " << block->getName() << ": "
      << tile_rows << " x " << tile_cols << " tiles\n";
  flp << "# <name>\t<width>\t<height>\t<left-x>\t<bottom-y> (meters)\n";
  std::string names;
  std::string values;
  double binned_power = 0.0;
  for (int row = 0; row < tile_rows; row++) {
    for (int col = 0; col < tile_cols; col++) {
      const std::string name = fmt::format("t{}_{}", row, col);
      const odb::Rect tile = tiles.getTileRect(row, col);
      flp << fmt::format("{}\t{:.9e}\t{:.9e}\t{:.9e}\t{:.9e}\n",
                         name,
                         dbuToMeters(block, tile.dx()),
                         dbuToMeters(block, tile.dy()),
                         dbuToMeters(block, tile.xMin() - die.xMin()),
                         dbuToMeters(block, tile.yMin() - die.yMin()));
      const double power = tile_power[(row * tile_cols) + col];
      binned_power += power;
      names += (names.empty() ? "" : "\t") + name;
      values += (values.empty() ? "" : "\t") + fmt::format("{:.9e}", power);
    }
  }
  ptrace << names << "\n" << values << "\n";

  double total_power = 0.0;
  for (const auto& [inst, power] : powers) {
    total_power += power;
  }
  logger_->info(utl::PSM,
                216,
                "Binned {:.4e} W of instance power into {:.4e} W over {} x {} "
                "HotSpot tiles.",
                total_power,
                binned_power,
                tile_rows,
                tile_cols);
}

void ThermalAnalyzer::writeConfig(odb::dbBlock* block,
                                  const std::string& config_file) const
{
  const odb::Rect die = block->getDieArea();
  const double chip_side
      = std::max(dbuToMeters(block, die.dx()), dbuToMeters(block, die.dy()));
  const double spreader = std::max(kMinSpreaderSide, chip_side);
  const double sink = std::max(kMinSinkSide, 2.0 * spreader);

  std::ofstream config(config_file);
  if (!config) {
    logger_->error(
        utl::PSM, 217, "Unable to write HotSpot config {}.", config_file);
  }
  // HotSpot built-in defaults, written out so a run can be reproduced.
  config << "# OpenROAD default HotSpot configuration\n"
         << "-t_chip 0.00015\n"
         << "-k_chip 100.0\n"
         << "-p_chip 1.75e6\n"
         << "-c_convec 140.4\n"
         << "-r_convec 0.1\n"
         << fmt::format("-s_sink {:.6e}\n", sink) << "-t_sink 0.0069\n"
         << "-k_sink 400.0\n"
         << "-p_sink 3.55e6\n"
         << fmt::format("-s_spreader {:.6e}\n", spreader)
         << "-t_spreader 0.001\n"
         << "-k_spreader 400.0\n"
         << "-p_spreader 3.55e6\n"
         << "-t_interface 2.0e-05\n"
         << "-k_interface 4.0\n"
         << "-p_interface 4.0e6\n"
         << "-model_secondary 0\n"
         << "-package_model_used 0\n";
}

void ThermalAnalyzer::runHotSpot(const std::vector<std::string>& argv,
                                 const std::string& work_dir,
                                 const std::string& log_file) const
{
  std::vector<char*> args;
  args.reserve(argv.size() + 1);
  for (const std::string& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  const pid_t pid = fork();
  if (pid < 0) {
    logger_->error(
        utl::PSM, 218, "Unable to start HotSpot: {}.", std::strerror(errno));
  }
  if (pid == 0) {
    // Child: only async-signal-safe calls until exec.
    const int fd = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
    if (chdir(work_dir.c_str()) != 0) {
      _exit(126);
    }
    execv(args[0], args.data());
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      logger_->error(utl::PSM,
                     219,
                     "Unable to wait for HotSpot: {}.",
                     std::strerror(errno));
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    const std::string reason
        = WIFEXITED(status) ? fmt::format("exit code {}", WEXITSTATUS(status))
                            : fmt::format("signal {}", WTERMSIG(status));
    logger_->error(utl::PSM,
                   220,
                   "HotSpot failed ({}). Last lines of {}:{}",
                   reason,
                   log_file,
                   fileTail(log_file, kLogTailLines));
  }
}

std::string ThermalAnalyzer::runHotSpotFlow(odb::dbBlock* block,
                                            const ThermalSettings& settings,
                                            const InstancePowers& powers,
                                            const std::string& work_dir) const
{
  const std::filesystem::path dir(work_dir);
  const std::string flp = "design.flp";
  const std::string ptrace = "design.ptrace";
  const std::string steady = "design.steady";
  const std::string grid_steady = "design.grid.steady";
  const std::string log = (dir / "hotspot.log").string();

  // Tiles are equal to or coarser than the grid; both are powers of 2 so
  // every tile covers a whole number of grid cells.
  const int tile_rows = std::min(settings.grid_rows, kMaxTilesPerSide);
  const int tile_cols = std::min(settings.grid_cols, kMaxTilesPerSide);
  writeFloorplan(block,
                 powers,
                 tile_rows,
                 tile_cols,
                 (dir / flp).string(),
                 (dir / ptrace).string());

  std::string config;
  if (settings.hotspot_config.empty()) {
    config = "hotspot.config";
    writeConfig(block, (dir / config).string());
  } else {
    config = std::filesystem::absolute(settings.hotspot_config).string();
  }

  const std::string ambient_k
      = fmt::format("{:.2f}", settings.ambient_c + kKelvinOffset);
  const std::vector<std::string> argv{settings.hotspot_exe,
                                      "-c",
                                      config,
                                      "-f",
                                      flp,
                                      "-p",
                                      ptrace,
                                      "-model_type",
                                      "grid",
                                      "-grid_rows",
                                      std::to_string(settings.grid_rows),
                                      "-grid_cols",
                                      std::to_string(settings.grid_cols),
                                      "-ambient",
                                      ambient_k,
                                      "-init_temp",
                                      ambient_k,
                                      "-steady_file",
                                      steady,
                                      "-grid_steady_file",
                                      grid_steady};

  logger_->info(utl::PSM,
                221,
                "Running HotSpot {} in {}.",
                settings.hotspot_exe,
                settings.work_dir.empty() ? "a temporary directory" : work_dir);
  for (const std::string& output : {steady, grid_steady}) {
    std::error_code ec;
    std::filesystem::remove(dir / output, ec);
  }
  runHotSpot(argv, work_dir, log);
  if (!std::filesystem::is_regular_file(dir / grid_steady)) {
    logger_->error(utl::PSM,
                   229,
                   "HotSpot did not write {}. Last lines of {}:{}",
                   (dir / grid_steady).string(),
                   log,
                   fileTail(log, kLogTailLines));
  }
  return (dir / grid_steady).string();
}

ThermalGrid ThermalAnalyzer::readGridFile(const std::string& grid_file,
                                          const odb::Rect& bounds,
                                          const int rows,
                                          const int cols) const
{
  std::ifstream in(grid_file);
  if (!in) {
    logger_->error(
        utl::PSM, 222, "Unable to open HotSpot grid file {}.", grid_file);
  }
  const int cells = rows * cols;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  std::vector<std::vector<double>> layers;
  std::vector<int> counts;
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    line_no++;
    std::istringstream fields(line);
    std::string first;
    if (!(fields >> first)) {
      continue;
    }
    if (first == "Layer") {
      layers.emplace_back(cells, nan);
      counts.push_back(0);
      continue;
    }
    if (layers.empty()) {
      // Older HotSpot versions dump a single layer without a header.
      layers.emplace_back(cells, nan);
      counts.push_back(0);
    }
    std::istringstream values(line);
    int64_t index = -1;
    double temperature_k = 0.0;
    if (!(values >> index >> temperature_k) || !std::isfinite(temperature_k)
        || index < 0 || index >= cells || !std::isnan(layers.back()[index])) {
      logger_->error(utl::PSM,
                     223,
                     "Invalid line {} in HotSpot grid file {} for a {} x {} "
                     "grid: {}",
                     line_no,
                     grid_file,
                     rows,
                     cols,
                     line);
    }
    layers.back()[index] = temperature_k;
    counts.back()++;
  }

  if (layers.empty()) {
    logger_->error(
        utl::PSM, 224, "HotSpot grid file {} has no temperatures.", grid_file);
  }
  for (size_t layer = 0; layer < layers.size(); layer++) {
    if (counts[layer] != cells) {
      logger_->error(utl::PSM,
                     225,
                     "Layer {} of HotSpot grid file {} has {} temperatures; "
                     "expected {} for a {} x {} grid.",
                     layer,
                     grid_file,
                     counts[layer],
                     cells,
                     rows,
                     cols);
    }
  }

  // HotSpot's default stack is silicon, interface, spreader, sink: layer 0
  // is the active (power dissipating) silicon layer.
  constexpr size_t silicon = 0;

  // HotSpot row 0 is the top (max y) of the die; ThermalGrid row 0 is the
  // bottom.
  std::vector<double> temperatures_c(cells);
  for (int hs_row = 0; hs_row < rows; hs_row++) {
    const int row = rows - 1 - hs_row;
    for (int col = 0; col < cols; col++) {
      temperatures_c[(row * cols) + col]
          = layers[silicon][(hs_row * cols) + col] - kKelvinOffset;
    }
  }
  return {bounds, rows, cols, std::move(temperatures_c)};
}

ThermalHotRegion ThermalAnalyzer::findHotRegion(
    const InstancePowers& powers) const
{
  const int rows = grid_.getRows();
  const int cols = grid_.getCols();
  const std::vector<double>& temps = grid_.getTemperatures();
  const auto peak_it = std::max_element(temps.begin(), temps.end());
  const int peak_index = static_cast<int>(peak_it - temps.begin());
  const double peak = *peak_it;
  const double threshold
      = peak - ThermalHotRegion::kHotRegionFraction * (peak - grid_.getMin());

  ThermalHotRegion region;
  region.peak_c = peak;
  std::vector<bool> in_region(temps.size(), false);
  std::queue<std::pair<int, int>> queue;
  queue.emplace(peak_index / cols, peak_index % cols);
  in_region[peak_index] = true;
  double sum = 0.0;
  bool first = true;
  while (!queue.empty()) {
    const auto [row, col] = queue.front();
    queue.pop();
    region.tiles.emplace_back(row, col);
    sum += grid_.getTemperature(row, col);
    const odb::Rect rect = grid_.getTileRect(row, col);
    if (first) {
      region.bounds = rect;
      first = false;
    } else {
      region.bounds.merge(rect);
    }
    const std::pair<int, int> neighbors[]
        = {{row - 1, col}, {row + 1, col}, {row, col - 1}, {row, col + 1}};
    for (const auto& [r, c] : neighbors) {
      if (r < 0 || r >= rows || c < 0 || c >= cols) {
        continue;
      }
      const int index = (r * cols) + c;
      if (!in_region[index] && temps[index] >= threshold) {
        in_region[index] = true;
        queue.emplace(r, c);
      }
    }
  }
  std::sort(region.tiles.begin(), region.tiles.end());
  region.average_c = sum / region.tiles.size();

  for (const auto& [inst, power] : powers) {
    const auto tile = grid_.findTile(inst->getBBox()->getBox().center());
    if (tile && in_region[(tile->first * cols) + tile->second]) {
      region.instances.emplace_back(inst, power);
    }
  }
  std::stable_sort(region.instances.begin(),
                   region.instances.end(),
                   [](const auto& a, const auto& b) {
                     if (a.second != b.second) {
                       return a.second > b.second;
                     }
                     return a.first->getName() < b.first->getName();
                   });
  return region;
}

void ThermalAnalyzer::report(odb::dbBlock* block,
                             const ThermalSettings& settings,
                             sta::Scene* corner) const
{
  const ThermalHotRegion& region = *hot_region_;
  const odb::Rect& bbox = region.bounds;
  logger_->report("########## Thermal report ##########");
  logger_->report("Design             : {}", block->getName());
  logger_->report("Corner             : {}",
                  corner != nullptr ? corner->name() : "default");
  logger_->report(
      "Grid               : {} x {}", grid_.getRows(), grid_.getCols());
  logger_->report("Ambient            : {:.2f} C", settings.ambient_c);
  logger_->report("Total power        : {:.4e} W", total_power_);
  logger_->report("Peak temperature   : {:.2f} C", grid_.getMax());
  logger_->report("Average temperature: {:.2f} C", grid_.getAverage());
  logger_->report("Min temperature    : {:.2f} C", grid_.getMin());
  logger_->report("Hottest region     : ({:.3f}, {:.3f}) - ({:.3f}, {:.3f}) um",
                  block->dbuToMicrons(bbox.xMin()),
                  block->dbuToMicrons(bbox.yMin()),
                  block->dbuToMicrons(bbox.xMax()),
                  block->dbuToMicrons(bbox.yMax()));
  logger_->report("Region tiles       : {}", region.tiles.size());
  logger_->report("Region average     : {:.2f} C", region.average_c);
  logger_->report("Region instances   : {}", region.instances.size());
  const size_t count = std::min(region.instances.size(),
                                static_cast<size_t>(settings.max_instances));
  if (count > 0) {
    logger_->report("{:<40} {:<30} {:>12}", "Instance", "Master", "Power (W)");
    for (size_t i = 0; i < count; i++) {
      const auto& [inst, power] = region.instances[i];
      logger_->report("{:<40} {:<30} {:>12.4e}",
                      inst->getName(),
                      inst->getMaster()->getName(),
                      power);
    }
  }
  logger_->report("####################################");
}

void ThermalAnalyzer::writeReportFile(const std::string& report_file) const
{
  std::ofstream out(report_file);
  if (!out) {
    logger_->error(
        utl::PSM, 226, "Unable to write thermal report {}.", report_file);
  }
  out << "Instance\tMaster\tPower (W)\n";
  for (const auto& [inst, power] : hot_region_->instances) {
    out << fmt::format("{}\t{}\t{:.4e}\n",
                       inst->getName(),
                       inst->getMaster()->getName(),
                       power);
  }
}

void ThermalAnalyzer::clear()
{
  grid_ = ThermalGrid();
  total_power_ = 0.0;
  hot_region_.reset();
}

}  // namespace psm
