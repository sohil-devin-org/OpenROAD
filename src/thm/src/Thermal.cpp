// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/Thermal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>

#include "boost/json.hpp"
#include "db_sta/dbSta.hh"
#include "heatMap.h"
#include "psm/pdnsim.h"
#include "thm/Animation.h"
#include "utl/Logger.h"
#include "web/heatMap.h"

namespace thm {

namespace json = boost::json;

Thermal::Thermal(odb::dbDatabase* db,
                 sta::dbSta* sta,
                 psm::PDNSim* psm,
                 utl::Logger* logger)
    : db_(db),
      sta_(sta),
      psm_(psm),
      logger_(logger),
      solver_(makeFiniteVolumeSolver(logger)),
      leakage_model_(std::make_unique<LeakageModel>(logger)),
      derate_model_(std::make_unique<DerateModel>(logger)),
      power_extractor_(std::make_unique<PowerExtractor>(sta, logger)),
      derate_applier_(std::make_unique<DerateApplier>(sta, logger)),
      ir_coupling_(std::make_unique<IrDropCoupling>(psm, sta, logger))
{
  heatmap_sources_.push_back(web::registerHeatMapSource(
      "Temperature", "Temperature", "Temperature", [this, logger]() {
        return std::make_shared<TemperatureDataSource>(this, logger);
      }));
  using Kind = PhysicsMapDataSource::Kind;
  const struct
  {
    Kind kind;
    const char* units;
    const char* name;
    const char* short_name;
  } maps[] = {
      {Kind::kLeakage, "W/m²", "Leakage Density", "Leakage"},
      {Kind::kDerate, "", "Physics Derate", "Derate"},
      {Kind::kIrDrop, "V", "Physics IR Drop", "PhysicsIR"},
      {Kind::kDelta, "C", "Temperature Delta", "TempDelta"},
      {Kind::kEmRisk, "", "EM Lifetime Factor", "EMRisk"},
  };
  for (const auto& m : maps) {
    heatmap_sources_.push_back(web::registerHeatMapSource(
        m.name, m.short_name, "Physics", [this, logger, m]() {
          return std::make_shared<PhysicsMapDataSource>(
              this, m.kind, logger, m.units, m.name, m.short_name);
        }));
  }
}

Thermal::~Thermal()
{
  if (derating_enabled_) {
    derate_applier_->clear();
  }
}

// ---------------------------------------------------------------------------
// configuration

void Thermal::setConfig(const ThermalConfig& config)
{
  config_ = config;
  has_results_ = false;
}

static bool parseBool(const std::string& value)
{
  std::string v = value;
  std::transform(v.begin(), v.end(), v.begin(), ::tolower);
  if (v == "1" || v == "true" || v == "on" || v == "yes") {
    return true;
  }
  if (v == "0" || v == "false" || v == "off" || v == "no") {
    return false;
  }
  throw std::invalid_argument("expected boolean, got '" + value + "'");
}

void Thermal::setConfigValue(const std::string& key, const std::string& value)
{
  using Setter = std::function<void(const std::string&)>;
  auto dbl = [](double& field) {
    return [&field](const std::string& v) { field = std::stod(v); };
  };
  auto integer = [](int& field) {
    return [&field](const std::string& v) { field = std::stoi(v); };
  };
  auto boolean = [](bool& field) {
    return [&field](const std::string& v) { field = parseBool(v); };
  };
  auto str = [](std::string& field) {
    return [&field](const std::string& v) { field = v; };
  };
  ThermalConfig& c = config_;
  if (c.dies.empty()) {
    c.dies.emplace_back();
  }
  const std::map<std::string, Setter> setters = {
      {"ambient", dbl(c.ambient_c)},
      {"top_resistance", dbl(c.top_resistance_k_w)},
      {"bottom_resistance", dbl(c.bottom_resistance_k_w)},
      {"heat_sink_on_bottom", boolean(c.heat_sink_on_bottom)},
      {"silicon_conductivity", dbl(c.silicon_conductivity_w_mk)},
      {"silicon_heat_capacity", dbl(c.silicon_volumetric_heat_capacity_j_m3k)},
      {"temperature_dependent_conductivity",
       boolean(c.temperature_dependent_conductivity)},
      {"silicon_thickness", dbl(c.dies.front().silicon_thickness_m)},
      {"beol_thickness", dbl(c.dies.front().beol_thickness_m)},
      {"beol_conductivity", dbl(c.dies.front().beol_conductivity_w_mk)},
      {"bond_thickness", dbl(c.bond.thickness_m)},
      {"bond_conductivity", dbl(c.bond.conductivity_w_mk)},
      {"thinned_die_thickness", dbl(c.thinned_die_thickness_m)},
      {"two_die", boolean(c.two_die)},
      {"second_die_power_source", str(c.second_die_power_source)},
      {"second_die_power", dbl(c.second_die_power_w)},
      {"second_die_power_file", str(c.second_die_power_file)},
      {"grid_x", integer(c.grid_x)},
      {"grid_y", integer(c.grid_y)},
      {"solver_tolerance", dbl(c.solver_tolerance)},
      {"solver_max_iterations", integer(c.solver_max_iterations)},
      {"loop_peak_tolerance", dbl(c.loop_peak_tolerance_c)},
      {"loop_leakage_tolerance", dbl(c.loop_leakage_tolerance_rel)},
      {"loop_max_iterations", integer(c.loop_max_iterations)},
      {"runaway_temperature", dbl(c.runaway_temperature_c)},
      {"transient_time_step", dbl(c.transient_time_step_s)},
      {"em_activation_energy", dbl(c.em.activation_energy_ev)},
      {"em_current_exponent", dbl(c.em.current_exponent)},
      {"em_reference_temperature", dbl(c.em.reference_temp_c)},
      {"metal_tcr", dbl(c.em.metal_tcr_per_k)},
      {"activity_scale", dbl(c.default_activity_scale)},
      {"activity_file", str(c.activity_file)},
      {"nominal_vdd", dbl(c.nominal_vdd_v)},
      {"include_ir_drop", boolean(c.include_ir_drop)},
  };
  auto it = setters.find(key);
  if (it == setters.end()) {
    logger_->error(utl::THM, 1, "Unknown thermal config key '{}'.", key);
  }
  try {
    it->second(value);
  } catch (const std::exception& e) {
    logger_->error(utl::THM,
                   2,
                   "Invalid value '{}' for thermal config key '{}': {}",
                   value,
                   key,
                   e.what());
  }
  if (c.grid_x < 2 || c.grid_y < 2) {
    logger_->error(utl::THM, 3, "Thermal grid must be at least 2x2.");
  }
  if (c.two_die) {
    if (c.dies.size() < 2) {
      c.dies.resize(2, c.dies.front());
    }
  }
  has_results_ = false;
}

void Thermal::readConfigFile(const std::string& path)
{
  std::ifstream in(path);
  if (!in) {
    logger_->error(utl::THM, 4, "Cannot open thermal config file {}.", path);
  }
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    const auto hash = line.find('#');
    if (hash != std::string::npos) {
      line = line.substr(0, hash);
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    auto trim = [](std::string s) {
      const auto b = s.find_first_not_of(" \t\r");
      const auto e = s.find_last_not_of(" \t\r");
      return b == std::string::npos ? "" : s.substr(b, e - b + 1);
    };
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (!key.empty()) {
      setConfigValue(key, value);
    }
  }
  logger_->info(
      utl::THM, 5, "Read thermal config from {} ({} lines).", path, line_no);
}

void Thermal::addActivityPhase(const ActivityPhase& phase)
{
  if (phase.duration_s <= 0) {
    logger_->error(utl::THM,
                   6,
                   "Activity phase '{}' must have a positive duration.",
                   phase.name);
  }
  config_.phases.push_back(phase);
}

void Thermal::clearActivityPhases()
{
  config_.phases.clear();
}

std::string Thermal::configReport() const
{
  std::ostringstream out;
  const ThermalConfig& c = config_;
  out << "Thermal configuration\n"
      << "  ambient:              " << c.ambient_c << " C\n"
      << "  top resistance:       " << c.top_resistance_k_w << " K/W\n"
      << "  bottom resistance:    " << c.bottom_resistance_k_w << " K/W\n"
      << "  grid:                 " << c.grid_x << " x " << c.grid_y << "\n"
      << "  dies:                 " << (c.two_die ? 2 : 1) << "\n"
      << "  loop max iterations:  " << c.loop_max_iterations << "\n"
      << "  loop peak tolerance:  " << c.loop_peak_tolerance_c << " C\n"
      << "  runaway temperature:  " << c.runaway_temperature_c << " C\n"
      << "  activity scale:       " << c.default_activity_scale << "\n"
      << "  nominal VDD:          " << c.nominal_vdd_v << " V\n"
      << "  include IR drop:      " << (c.include_ir_drop ? "yes" : "no")
      << "\n"
      << "  phases:               " << c.phases.size() << "\n";
  for (const ActivityPhase& p : c.phases) {
    out << "    " << p.name << ": scale " << p.activity_scale << ", "
        << p.duration_s << " s\n";
  }
  out << "  " << leakage_model_->report() << "\n"
      << "  " << derate_model_->report() << "\n";
  return out.str();
}

int Thermal::characterizeLibraries(const std::vector<LibraryCorner>& corners)
{
  const int leakage = leakage_model_->fitFromLibraries(corners, sta_);
  derate_model_->fitFromLibraries(corners, sta_);
  return leakage;
}

bool Thermal::loadLibraryFits(const std::string& leakage_json,
                              const std::string& derate_json)
{
  bool ok = true;
  if (!leakage_json.empty()) {
    ok &= leakage_model_->loadFits(leakage_json);
  }
  if (!derate_json.empty()) {
    ok &= derate_model_->loadFits(derate_json);
  }
  return ok;
}

// ---------------------------------------------------------------------------
// analysis

odb::dbBlock* Thermal::getBlock() const
{
  odb::dbChip* chip = db_->getChip();
  return chip != nullptr ? chip->getBlock() : nullptr;
}

void Thermal::ensureGrid()
{
  odb::dbBlock* block = getBlock();
  const odb::Rect die = block->getDieArea();
  const auto stack = config_.buildStack();
  const bool same = grid_.nx() == config_.grid_x && grid_.ny() == config_.grid_y
                    && grid_.dieRect() == die
                    && grid_.stack().size() == stack.size();
  if (!same) {
    grid_.reset(config_.grid_x,
                config_.grid_y,
                die,
                block->getDbUnitsPerMicron(),
                stack,
                /*cells_per_layer=*/3);
    grid_.fill(config_.ambient_c);
  }
  const int dies = config_.two_die ? 2 : 1;
  power_maps_.resize(dies);
  for (PowerMap& map : power_maps_) {
    map.reset(config_.grid_x, config_.grid_y);
  }
}

void Thermal::buildPowerMaps()
{
  const odb::Rect die = grid_.dieRect();
  for (PowerMap& map : power_maps_) {
    map.clear();
  }
  for (const auto& [inst, phys] : inst_state_) {
    const odb::Rect bbox = inst->getBBox()->getBox();
    const int d = std::clamp<int>(phys.die, 0, power_maps_.size() - 1);
    power_maps_[d].addRect(
        bbox, die, phys.dynamic_power_w + phys.leakage_power_w);
  }
  if (config_.two_die && power_maps_.size() > 1) {
    // Second die: uniform power (no netlist available for it).
    PowerMap& second = power_maps_[1];
    if (config_.second_die_power_source == "uniform"
        && config_.second_die_power_w > 0) {
      const double per_tile = config_.second_die_power_w / second.size();
      for (double& v : second.values()) {
        v += per_tile;
      }
    }
  }
}

void Thermal::updateInstanceTemperatures()
{
  for (auto& [inst, phys] : inst_state_) {
    const odb::Rect bbox = inst->getBBox()->getBox();
    phys.temperature_c
        = temperatureAt(bbox.xCenter(), bbox.yCenter(), phys.die);
  }
}

void Thermal::electrothermalLoop(const AnalyzeOptions& options,
                                 PhysicsMetrics& metrics)
{
  const int dies = config_.two_die ? 2 : 1;
  double prev_peak = std::numeric_limits<double>::quiet_NaN();
  double prev_leakage = 0.0;
  metrics.converged = false;
  metrics.runaway = false;
  bool warm = options.warm_start && has_results_;
  for (int iter = 0; iter < config_.loop_max_iterations; ++iter) {
    metrics.electrothermal_iterations = iter + 1;
    // 1. power at the current temperatures
    power_extractor_->setActivityScale(config_.default_activity_scale);
    power_extractor_->extract(
        getBlock(), *leakage_model_, nominal_temp_c_, inst_state_);
    buildPowerMaps();
    // 2. thermal solve
    const SolveResult result
        = solver_->solveSteady(power_maps_, config_, grid_, warm);
    warm = true;
    if (!result.converged) {
      logger_->warn(utl::THM,
                    10,
                    "Thermal solver did not converge (residual {:.3e} after "
                    "{} iterations).",
                    result.residual,
                    result.iterations);
    }
    updateInstanceTemperatures();
    // 3. leakage update happens on the next extract; check convergence.
    const double peak = grid_.peak(0);
    const double leakage = power_extractor_->leakagePowerW(inst_state_);
    metrics.total_power_w = power_extractor_->totalPowerW(inst_state_);
    metrics.leakage_power_w = leakage;
    metrics.peak_temp_c.assign(dies, 0.0);
    metrics.avg_temp_c.assign(dies, 0.0);
    for (int d = 0; d < dies; ++d) {
      metrics.peak_temp_c[d] = grid_.peak(d);
      metrics.avg_temp_c[d] = grid_.average(d);
    }
    metrics.max_gradient_c_per_mm = grid_.maxGradientCPerMm(0);
    for (PhysicsObserver* obs : observers_) {
      obs->onLoopIteration(metrics);
    }
    if (peak > config_.runaway_temperature_c) {
      metrics.runaway = true;
      logger_->warn(utl::THM,
                    11,
                    "Thermal runaway: peak temperature {:.1f} C exceeds the "
                    "{:.1f} C limit after {} electrothermal iterations.",
                    peak,
                    config_.runaway_temperature_c,
                    iter + 1);
      break;
    }
    if (!std::isnan(prev_peak)) {
      const double leak_rel
          = prev_leakage > 0 ? std::abs(leakage - prev_leakage) / prev_leakage
                             : 0.0;
      if (std::abs(peak - prev_peak) < config_.loop_peak_tolerance_c
          && leak_rel < config_.loop_leakage_tolerance_rel) {
        metrics.converged = true;
        break;
      }
    }
    // Without a leakage(T) model there is nothing to iterate on.
    if (leakage_model_->numFits() == 0
        && leakage_model_->familyBetaPerC() == 0.0) {
      metrics.converged = true;
      break;
    }
    prev_peak = peak;
    prev_leakage = leakage;
  }
  if (!metrics.converged && !metrics.runaway) {
    logger_->warn(utl::THM,
                  12,
                  "Electrothermal loop did not converge in {} iterations "
                  "(peak {:.2f} C).",
                  config_.loop_max_iterations,
                  grid_.peak(0));
  }
}

void Thermal::runTransient(const AnalyzeOptions& options,
                           PhysicsMetrics& metrics)
{
  std::vector<ActivityPhase> phases;
  for (const ActivityPhase& p : config_.phases) {
    if (options.phases.empty()
        || std::find(options.phases.begin(), options.phases.end(), p.name)
               != options.phases.end()) {
      phases.push_back(p);
    }
  }
  if (phases.empty()) {
    logger_->error(utl::THM,
                   13,
                   "Transient analysis requires at least one activity phase "
                   "(set_thermal_config -phase).");
  }
  const double tau = solver_->timeConstantS(config_, grid_);
  const double dt = config_.transient_time_step_s > 0
                        ? std::min(config_.transient_time_step_s, tau / 10.0)
                        : tau / 10.0;
  logger_->info(utl::THM,
                14,
                "Transient analysis: {} phases, time constant {:.3e} s, step "
                "{:.3e} s.",
                phases.size(),
                tau,
                dt);
  grid_.fill(config_.ambient_c);
  updateInstanceTemperatures();
  double time = 0.0;
  const int dies = config_.two_die ? 2 : 1;
  for (const ActivityPhase& phase : phases) {
    power_extractor_->setActivityScale(phase.activity_scale);
    const int steps = std::max(1, static_cast<int>(phase.duration_s / dt));
    for (int s = 0; s < steps; ++s) {
      power_extractor_->extract(
          getBlock(), *leakage_model_, nominal_temp_c_, inst_state_);
      buildPowerMaps();
      solver_->stepTransient(power_maps_, config_, dt, grid_);
      updateInstanceTemperatures();
      time += dt;
      if (options.record) {
        PhysicsMetrics step_metrics = metrics;
        step_metrics.label = phase.name;
        step_metrics.iteration = history_.size();
        step_metrics.peak_temp_c.assign(dies, 0.0);
        step_metrics.avg_temp_c.assign(dies, 0.0);
        for (int d = 0; d < dies; ++d) {
          step_metrics.peak_temp_c[d] = grid_.peak(d);
          step_metrics.avg_temp_c[d] = grid_.average(d);
        }
        step_metrics.total_power_w = power_extractor_->totalPowerW(inst_state_);
        step_metrics.leakage_power_w
            = power_extractor_->leakagePowerW(inst_state_);
        PhysicsSnapshot snap = makeSnapshot(step_metrics);
        snap.time_s = time;
        snap.tag = "transient";
        history_.add(std::move(snap));
        for (PhysicsObserver* obs : observers_) {
          obs->onSnapshot(history_.back(), history_);
        }
      }
    }
  }
  metrics.peak_temp_c.assign(dies, 0.0);
  metrics.avg_temp_c.assign(dies, 0.0);
  for (int d = 0; d < dies; ++d) {
    metrics.peak_temp_c[d] = grid_.peak(d);
    metrics.avg_temp_c[d] = grid_.average(d);
  }
  metrics.max_gradient_c_per_mm = grid_.maxGradientCPerMm(0);
  metrics.total_power_w = power_extractor_->totalPowerW(inst_state_);
  metrics.leakage_power_w = power_extractor_->leakagePowerW(inst_state_);
  metrics.converged = true;
  metrics.electrothermal_iterations = 1;
}

void Thermal::runIrDrop(PhysicsMetrics& metrics)
{
  has_ir_map_ = ir_coupling_->run(getBlock(),
                                  config_,
                                  grid_,
                                  inst_state_,
                                  metrics.worst_ir_drop_v,
                                  &ir_map_);
}

void Thermal::runTiming(PhysicsMetrics& metrics)
{
  // Derates from the local temperature / voltage of every instance.
  for (auto& [inst, phys] : inst_state_) {
    phys.derate = derate_model_->derate(
        inst->getMaster()->getName(), phys.temperature_c, phys.vdd_v);
  }
  // Nominal timing: without physics derates.
  const bool was_applied = derate_applier_->isApplied();
  if (was_applied) {
    derate_applier_->clear();
  }
  derate_applier_->timingSummary(metrics.wns_nominal_s, metrics.tns_nominal_s);
  derate_applier_->instanceSlacks(inst_state_, /*derated=*/false);
  // Derated timing.
  derate_applier_->apply(inst_state_);
  derate_applier_->timingSummary(metrics.wns_derated_s, metrics.tns_derated_s);
  derate_applier_->instanceSlacks(inst_state_, /*derated=*/true);
  if (!derating_enabled_) {
    derate_applier_->clear();
  }
}

PhysicsMetrics Thermal::analyze(const AnalyzeOptions& options)
{
  const auto start = std::chrono::steady_clock::now();
  odb::dbBlock* block = getBlock();
  if (block == nullptr) {
    logger_->error(utl::THM, 20, "No design is loaded.");
  }
  if (block->getDieArea().area() == 0) {
    logger_->error(utl::THM, 21, "The die area is not defined.");
  }
  if (options.corner != nullptr) {
    power_extractor_->setCorner(options.corner);
  }
  for (PhysicsObserver* obs : observers_) {
    obs->onAnalysisBegin(options.label);
  }
  ensureGrid();
  // Drop instances that no longer exist / were unplaced.
  for (auto it = inst_state_.begin(); it != inst_state_.end();) {
    if (!it->first->isPlaced() && !it->first->isFixed()) {
      it = inst_state_.erase(it);
    } else {
      ++it;
    }
  }
  nominal_temp_c_ = derate_model_->nominalTempC();

  PhysicsMetrics metrics;
  metrics.label = options.label;
  metrics.iteration = options.iteration;
  metrics.hpwl_um = options.hpwl_um;
  metrics.overflow = options.overflow;
  metrics.physics_weight = options.physics_weight;

  if (options.transient) {
    runTransient(options, metrics);
  } else {
    electrothermalLoop(options, metrics);
  }
  has_results_ = true;

  if (options.include_ir_drop && config_.include_ir_drop) {
    runIrDrop(metrics);
  } else {
    for (auto& [inst, phys] : inst_state_) {
      phys.vdd_v = config_.nominal_vdd_v;
    }
  }
  if (options.include_timing) {
    runTiming(metrics);
  }
  metrics.runtime_s
      = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
  last_metrics_ = metrics;
  if (options.record) {
    recordSnapshot(metrics);
  }
  logMetrics(metrics);
  for (web::HeatMapSourceHandle& source : heatmap_sources_) {
    source->invalidateInstances();
  }
  return metrics;
}

void Thermal::reset()
{
  if (derate_applier_->isApplied()) {
    derate_applier_->clear();
  }
  inst_state_.clear();
  has_results_ = false;
  has_ir_map_ = false;
  history_.clear();
  for (PhysicsObserver* obs : observers_) {
    obs->onHistoryCleared();
  }
}

// ---------------------------------------------------------------------------
// results

const InstancePhysics* Thermal::instancePhysics(odb::dbInst* inst) const
{
  auto it = inst_state_.find(inst);
  return it == inst_state_.end() ? nullptr : &it->second;
}

double Thermal::temperatureAt(int x_dbu, int y_dbu, int die) const
{
  if (!has_results_ && grid_.size() == 0) {
    return config_.ambient_c;
  }
  const int z = grid_.activeZ(die);
  if (z < 0) {
    return config_.ambient_c;
  }
  int tx;
  int ty;
  grid_.tileAt(x_dbu, y_dbu, tx, ty);
  return grid_.at(tx, ty, z);
}

MapSnapshot Thermal::temperatureMap(int die) const
{
  MapSnapshot map;
  map.name = "temperature";
  map.units = "C";
  map.die = die;
  map.nx = grid_.nx();
  map.ny = grid_.ny();
  map.values = grid_.activeLayer(die);
  return map;
}

MapSnapshot Thermal::leakageDensityMap(int die) const
{
  MapSnapshot map;
  map.name = "leakage";
  map.units = "W/m^2";
  map.die = die;
  map.nx = grid_.nx();
  map.ny = grid_.ny();
  map.values.assign(static_cast<size_t>(grid_.nx()) * grid_.ny(), 0.0);
  const double tile_area = grid_.dx() * grid_.dy();
  if (tile_area <= 0) {
    return map;
  }
  PowerMap leak(grid_.nx(), grid_.ny());
  for (const auto& [inst, phys] : inst_state_) {
    if (phys.die == die) {
      leak.addRect(
          inst->getBBox()->getBox(), grid_.dieRect(), phys.leakage_power_w);
    }
  }
  for (size_t i = 0; i < map.values.size(); ++i) {
    map.values[i] = leak.values()[i] / tile_area;
  }
  return map;
}

MapSnapshot Thermal::derateMap(int die) const
{
  MapSnapshot map;
  map.name = "derate";
  map.units = "";
  map.die = die;
  map.nx = grid_.nx();
  map.ny = grid_.ny();
  map.values.assign(static_cast<size_t>(grid_.nx()) * grid_.ny(), 1.0);
  std::vector<int> count(map.values.size(), 0);
  std::vector<double> sum(map.values.size(), 0.0);
  for (const auto& [inst, phys] : inst_state_) {
    if (phys.die != die) {
      continue;
    }
    const odb::Rect bbox = inst->getBBox()->getBox();
    int tx;
    int ty;
    grid_.tileAt(bbox.xCenter(), bbox.yCenter(), tx, ty);
    const int idx = ty * grid_.nx() + tx;
    sum[idx] += phys.derate;
    ++count[idx];
  }
  for (size_t i = 0; i < map.values.size(); ++i) {
    if (count[i] > 0) {
      map.values[i] = sum[i] / count[i];
    }
  }
  return map;
}

const MapSnapshot* Thermal::irDropMap() const
{
  return has_ir_map_ ? &ir_map_ : nullptr;
}

MapSnapshot Thermal::emRiskMap(int die) const
{
  // Relative lifetime of the metal at every tile assuming uniform current
  // density: only the temperature term of Black's equation varies.
  MapSnapshot map;
  map.name = "em";
  map.units = "";
  map.die = die;
  map.nx = grid_.nx();
  map.ny = grid_.ny();
  map.values = grid_.activeLayer(die);
  ElectromigrationModel em(config_.em);
  for (double& v : map.values) {
    v = em.relativeLifetime(1.0, v, 1.0);
  }
  return map;
}

void Thermal::loadReference(const std::string& path)
{
  auto ref = std::make_unique<PhysicsHistory>();
  try {
    ref->readJson(path);
  } catch (const std::exception& e) {
    logger_->error(
        utl::THM, 22, "Cannot load physics history {}: {}", path, e.what());
  }
  reference_ = std::move(ref);
  logger_->info(utl::THM,
                23,
                "Loaded reference physics history '{}' with {} snapshots.",
                reference_->tag(),
                reference_->size());
  for (PhysicsObserver* obs : observers_) {
    obs->onReferenceLoaded(*reference_);
  }
}

void Thermal::clearReference()
{
  reference_.reset();
}

// ---------------------------------------------------------------------------
// derates

void Thermal::setDeratingEnabled(bool enable)
{
  derating_enabled_ = enable;
  if (enable) {
    if (!has_results_) {
      logger_->warn(utl::THM,
                    24,
                    "Physics derating enabled before any analysis; derates "
                    "will be applied at the next analyze_thermal.");
      return;
    }
    derate_applier_->apply(inst_state_);
  } else {
    derate_applier_->clear();
  }
}

// ---------------------------------------------------------------------------
// reporting

void Thermal::logMetrics(const PhysicsMetrics& m) const
{
  logger_->info(
      utl::THM,
      30,
      "{}: peak {:.2f} C, avg {:.2f} C, max gradient {:.2f} C/mm, "
      "power {:.4f} W (leakage {:.4f} W), {} loop iterations{}",
      m.label,
      m.peakTemp(),
      m.avg_temp_c.empty() ? 0.0 : m.avg_temp_c[0],
      m.max_gradient_c_per_mm,
      m.total_power_w,
      m.leakage_power_w,
      m.electrothermal_iterations,
      m.runaway ? " (RUNAWAY)" : (m.converged ? "" : " (not converged)"));
  if (m.peak_temp_c.size() > 1) {
    logger_->info(utl::THM,
                  31,
                  "  die 1: peak {:.2f} C, avg {:.2f} C",
                  m.peak_temp_c[1],
                  m.avg_temp_c[1]);
  }
  if (m.worst_ir_drop_v > 0) {
    logger_->info(utl::THM, 32, "  worst IR drop {:.4f} V", m.worst_ir_drop_v);
  }
  logger_->info(utl::THM,
                33,
                "  WNS nominal {:.4e} s, derated {:.4e} s; TNS nominal {:.4e} "
                "s, derated {:.4e} s",
                m.wns_nominal_s,
                m.wns_derated_s,
                m.tns_nominal_s,
                m.tns_derated_s);
}

void Thermal::reportPhysics(const std::string& json_file)
{
  if (!has_results_) {
    logger_->error(utl::THM, 25, "Run analyze_thermal before report_physics.");
  }
  const PhysicsMetrics& m = last_metrics_;
  std::ostringstream out;
  out << "Physics report (" << m.label << ")\n";
  for (size_t d = 0; d < m.peak_temp_c.size(); ++d) {
    out << "  die " << d << ": peak " << m.peak_temp_c[d] << " C, average "
        << m.avg_temp_c[d] << " C\n";
  }
  out << "  max lateral gradient:      " << m.max_gradient_c_per_mm << " C/mm\n"
      << "  total power:               " << m.total_power_w << " W\n"
      << "  leakage power:             " << m.leakage_power_w << " W\n"
      << "  worst IR drop:             " << m.worst_ir_drop_v << " V\n"
      << "  WNS nominal / derated:     " << m.wns_nominal_s << " / "
      << m.wns_derated_s << " s\n"
      << "  TNS nominal / derated:     " << m.tns_nominal_s << " / "
      << m.tns_derated_s << " s\n"
      << "  electrothermal iterations: " << m.electrothermal_iterations
      << (m.converged ? " (converged)" : " (NOT converged)")
      << (m.runaway ? " RUNAWAY" : "") << "\n"
      << "  EM lifetime factor:        " << m.em_lifetime_factor << "\n"
      << "  thermal clock skew delta:  " << m.clock_skew_delta_s << " s\n"
      << "  derating:                  "
      << (derating_enabled_ ? "enabled" : "disabled") << "\n"
      << "  snapshots in history:      " << history_.size() << "\n";
  // Hottest instances.
  std::vector<std::pair<double, odb::dbInst*>> hottest;
  for (const auto& [inst, phys] : inst_state_) {
    hottest.emplace_back(phys.temperature_c, inst);
  }
  std::sort(hottest.begin(), hottest.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second->getId() < b.second->getId();
  });
  const size_t n = std::min<size_t>(5, hottest.size());
  if (n > 0) {
    out << "  hottest instances:\n";
    for (size_t i = 0; i < n; ++i) {
      const InstancePhysics& phys = inst_state_.at(hottest[i].second);
      out << "    " << hottest[i].second->getName() << ": "
          << phys.temperature_c << " C, "
          << (phys.dynamic_power_w + phys.leakage_power_w) << " W, "
          << phys.vdd_v << " V, derate " << phys.derate << "\n";
    }
  }
  logger_->report("{}", out.str());

  if (!json_file.empty()) {
    json::object root;
    root["design"] = getBlock()->getName();
    root["label"] = m.label;
    root["peak_temp_c"] = m.peakTemp();
    json::array peaks;
    for (double p : m.peak_temp_c) {
      peaks.push_back(p);
    }
    root["peak_temp_c_per_die"] = peaks;
    root["avg_temp_c"] = m.avg_temp_c.empty() ? 0.0 : m.avg_temp_c[0];
    root["max_gradient_c_per_mm"] = m.max_gradient_c_per_mm;
    root["total_power_w"] = m.total_power_w;
    root["leakage_power_w"] = m.leakage_power_w;
    root["worst_ir_drop_v"] = m.worst_ir_drop_v;
    root["wns_nominal_s"] = m.wns_nominal_s;
    root["wns_derated_s"] = m.wns_derated_s;
    root["tns_nominal_s"] = m.tns_nominal_s;
    root["tns_derated_s"] = m.tns_derated_s;
    root["electrothermal_iterations"] = m.electrothermal_iterations;
    root["converged"] = m.converged;
    root["runaway"] = m.runaway;
    root["em_lifetime_factor"] = m.em_lifetime_factor;
    root["clock_skew_delta_s"] = m.clock_skew_delta_s;
    root["hpwl_um"] = m.hpwl_um;
    root["runtime_s"] = m.runtime_s;
    root["derating_enabled"] = derating_enabled_;
    std::ofstream file(json_file);
    if (!file) {
      logger_->error(utl::THM, 26, "Cannot write {}.", json_file);
    }
    file << json::serialize(root);
  }
}

void Thermal::writeThermalMap(const std::string& file,
                              const std::string& map_name,
                              int die)
{
  if (!has_results_) {
    logger_->error(
        utl::THM, 27, "Run analyze_thermal before write_thermal_map.");
  }
  MapSnapshot map;
  if (map_name == "temperature") {
    map = temperatureMap(die);
  } else if (map_name == "leakage") {
    map = leakageDensityMap(die);
  } else if (map_name == "derate") {
    map = derateMap(die);
  } else if (map_name == "em") {
    map = emRiskMap(die);
  } else if (map_name == "ir_drop") {
    if (!has_ir_map_) {
      logger_->error(utl::THM, 28, "No IR-drop map is available.");
    }
    map = ir_map_;
  } else if (map_name == "delta") {
    if (reference_ == nullptr || reference_->empty()) {
      logger_->error(
          utl::THM, 29, "No reference history loaded for the delta map.");
    }
    map = temperatureMap(die);
    const MapSnapshot* ref = reference_->back().findMap("temperature", die);
    if (ref == nullptr || ref->values.size() != map.values.size()) {
      logger_->error(utl::THM, 34, "Reference map size does not match.");
    }
    for (size_t i = 0; i < map.values.size(); ++i) {
      map.values[i] -= ref->values[i];
    }
    map.name = "delta";
  } else {
    logger_->error(utl::THM, 35, "Unknown map '{}'.", map_name);
  }
  std::ofstream out(file);
  if (!out) {
    logger_->error(utl::THM, 36, "Cannot write {}.", file);
  }
  const odb::Rect die_rect = grid_.dieRect();
  const double dbu = grid_.dbuPerMicron();
  out << "# " << map.name << " [" << map.units << "] die " << die << " grid "
      << map.nx << "x" << map.ny << "\n";
  out << "# x_um y_um value\n";
  for (int y = 0; y < map.ny; ++y) {
    for (int x = 0; x < map.nx; ++x) {
      const odb::Rect tile = grid_.tileRect(x, y);
      out << tile.xCenter() / dbu << " " << tile.yCenter() / dbu << " "
          << map.at(x, y) << "\n";
    }
    out << "\n";
  }
  logger_->info(utl::THM,
                37,
                "Wrote {} map to {}.",
                map.name,
                std::filesystem::path(file).filename().string());
}

void Thermal::writeHistory(const std::string& path) const
{
  try {
    history_.writeJson(path);
  } catch (const std::exception& e) {
    logger_->error(utl::THM, 38, "Cannot write physics history: {}", e.what());
  }
  logger_->info(utl::THM,
                39,
                "Wrote {} snapshots to {}.",
                history_.size(),
                std::filesystem::path(path).filename().string());
}

void Thermal::writeAnimation(const AnimationOptions& options)
{
  if (history_.empty()) {
    logger_->error(utl::THM, 40, "No physics history to animate.");
  }
  thm::writeAnimation(history_, reference_.get(), options, logger_);
}

// ---------------------------------------------------------------------------
// snapshots

PhysicsSnapshot Thermal::makeSnapshot(const PhysicsMetrics& metrics) const
{
  PhysicsSnapshot snap;
  snap.metrics = metrics;
  const odb::Rect die = grid_.dieRect();
  snap.die_xmin = die.xMin();
  snap.die_ymin = die.yMin();
  snap.die_xmax = die.xMax();
  snap.die_ymax = die.yMax();
  snap.dbu_per_micron = grid_.dbuPerMicron();
  const int dies = config_.two_die ? 2 : 1;
  for (int d = 0; d < dies; ++d) {
    snap.maps.push_back(temperatureMap(d));
    // Power density in W per tile.
    MapSnapshot power;
    power.name = "power";
    power.units = "W";
    power.die = d;
    power.nx = grid_.nx();
    power.ny = grid_.ny();
    power.values = power_maps_[d].values();
    snap.maps.push_back(std::move(power));
  }
  if (has_ir_map_) {
    snap.maps.push_back(ir_map_);
  }
  snap.positions.reserve(inst_state_.size());
  for (const auto& [inst, phys] : inst_state_) {
    const odb::Rect bbox = inst->getBBox()->getBox();
    snap.positions.push_back({inst->getName(), bbox.xCenter(), bbox.yCenter()});
  }
  return snap;
}

void Thermal::recordSnapshot(const PhysicsMetrics& metrics)
{
  if (history_.designName().empty()) {
    history_.setDesignName(getBlock()->getName());
  }
  history_.add(makeSnapshot(metrics));
  for (PhysicsObserver* obs : observers_) {
    obs->onSnapshot(history_.back(), history_);
  }
}

// ---------------------------------------------------------------------------
// placer interface

double Thermal::screeningLengthDbu() const
{
  odb::dbBlock* block = getBlock();
  if (block == nullptr) {
    return 0.0;
  }
  const odb::Rect die = block->getDieArea();
  const double dbu = block->getDbUnitsPerMicron();
  const double m_per_dbu = 1e-6 / dbu;
  const double area_m2 = die.dx() * m_per_dbu * die.dy() * m_per_dbu;
  return config_.screeningLengthM(area_m2) / m_per_dbu;
}

double Thermal::instancePowerW(odb::dbInst* inst) const
{
  const InstancePhysics* phys = instancePhysics(inst);
  return phys != nullptr ? phys->dynamic_power_w + phys->leakage_power_w : 0.0;
}

void Thermal::addObserver(PhysicsObserver* observer)
{
  observers_.insert(observer);
}

void Thermal::removeObserver(PhysicsObserver* observer)
{
  observers_.erase(observer);
}

}  // namespace thm
