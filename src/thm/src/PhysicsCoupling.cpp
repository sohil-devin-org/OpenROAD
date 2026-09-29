// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/PhysicsCoupling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "psm/pdnsim.h"
#include "sta/Graph.hh"
#include "sta/Liberty.hh"
#include "sta/MinMax.hh"
#include "sta/Network.hh"
#include "sta/PowerClass.hh"
#include "sta/Scene.hh"
#include "sta/Sdc.hh"
#include "sta/SdcClass.hh"
#include "sta/Transition.hh"
#include "thm/LeakageModel.h"
#include "thm/ThermalGrid.h"
#include "utl/Logger.h"

namespace thm {

// Boltzmann constant in eV/K (CODATA 2018).
static constexpr double kBoltzmannEvPerK = 8.617333262e-5;

PowerExtractor::PowerExtractor(sta::dbSta* sta, utl::Logger* logger)
    : sta_(sta), logger_(logger)
{
}

sta::Scene* PowerExtractor::effectiveCorner() const
{
  return corner_ != nullptr ? corner_ : sta_->cmdScene();
}

bool PowerExtractor::readActivityFile(const std::string& file,
                                      const std::string& scope)
{
  if (file.empty()) {
    activity_file_.clear();
    instance_scales_.clear();
    return true;
  }
  if (file == activity_file_) {
    return true;
  }
  std::ifstream in(file);
  if (!in) {
    logger_->warn(utl::THM,
                  190,
                  "Cannot open activity file {}; no per-instance activity "
                  "scales are applied.",
                  file);
    return false;
  }
  // Format: one "instance_name scale" pair per line; blank lines and text
  // after '#' are ignored.  The scale multiplies the switching + internal
  // power of the named instance (leakage is unaffected).
  std::map<std::string, double> scales;
  std::string line;
  int line_no = 0;
  int bad = 0;
  while (std::getline(in, line)) {
    ++line_no;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) {
      line.erase(hash);
    }
    std::istringstream fields(line);
    std::string name;
    if (!(fields >> name)) {
      continue;
    }
    double scale = 0.0;
    std::string extra;
    if (!(fields >> scale) || scale < 0.0 || (fields >> extra)) {
      if (bad == 0) {
        logger_->warn(utl::THM,
                      191,
                      "Activity file {} line {}: expected 'instance_name "
                      "scale' with scale >= 0; line ignored.",
                      file,
                      line_no);
      }
      ++bad;
      continue;
    }
    scales[name] = scale;
  }
  activity_file_ = file;
  instance_scales_ = std::move(scales);
  logger_->info(utl::THM,
                70,
                "Read activity scales for {} instances from {}{}{}.",
                instance_scales_.size(),
                file,
                scope.empty() ? "" : " for phase ",
                scope);
  return true;
}

double PowerExtractor::designTotalPowerW() const
{
  sta::PowerResult total;
  sta::PowerResult sequential;
  sta::PowerResult combinational;
  sta::PowerResult clock;
  sta::PowerResult macro;
  sta::PowerResult pad;
  sta_->power(
      effectiveCorner(), total, sequential, combinational, clock, macro, pad);
  return total.total();
}

void PowerExtractor::extract(odb::dbBlock* block,
                             const LeakageModel& leakage_model,
                             double nominal_temp_c,
                             InstancePhysicsMap& state)
{
  sta::dbNetwork* network = sta_->getDbNetwork();
  sta::Scene* corner = effectiveCorner();
  sta_->ensureGraph();
  sta_->ensureLevelized();
  int missing = 0;
  int scaled = 0;
  std::set<std::string> unknown_scales;
  for (const auto& [name, scale] : instance_scales_) {
    unknown_scales.insert(name);
  }
  // Raw (unscaled, library-temperature) sum for the consistency check
  // against OpenSTA's design total.
  double raw_sum_w = 0.0;
  for (odb::dbInst* inst : block->getInsts()) {
    if (!inst->isPlaced() && !inst->isFixed()) {
      continue;
    }
    InstancePhysics& phys = state[inst];
    sta::Instance* sta_inst = network->dbToSta(inst);
    // Physical-only cells (fill, tap, pads without a liberty model, macros
    // described by LEF only) have no timing model: no power, no derate.
    if (sta_inst == nullptr || network->libertyCell(sta_inst) == nullptr) {
      phys.dynamic_power_w = 0.0;
      phys.leakage_power_w = 0.0;
      ++missing;
      continue;
    }
    const sta::PowerResult result = sta_->power(sta_inst, corner);
    raw_sum_w += result.total();
    double scale = activity_scale_;
    if (!instance_scales_.empty()) {
      auto it = instance_scales_.find(inst->getName());
      if (it != instance_scales_.end()) {
        scale *= it->second;
        unknown_scales.erase(it->first);
        ++scaled;
      }
    }
    phys.dynamic_power_w = scale * (result.internal() + result.switching());
    const double temp
        = phys.temperature_c > 0 ? phys.temperature_c : nominal_temp_c;
    phys.leakage_power_w = leakage_model.leakageAt(
        inst->getMaster()->getName(), result.leakage(), nominal_temp_c, temp);
  }
  if (missing > 0) {
    logger_->warn(utl::THM,
                  71,
                  "{} instances have no liberty (timing/power) model and "
                  "contribute no power.",
                  missing);
  }
  if (!unknown_scales.empty()) {
    logger_->warn(utl::THM,
                  192,
                  "{} of {} instances listed in activity file {} are not in "
                  "the design (first: {}).",
                  unknown_scales.size(),
                  instance_scales_.size(),
                  activity_file_,
                  *unknown_scales.begin());
  }
  debugPrint(logger_,
             utl::THM,
             "power",
             1,
             "extracted power for {} instances ({} activity-scaled, {} "
             "without liberty model)",
             state.size(),
             scaled,
             missing);
  // The per-instance sum must reproduce report_power's design total; a
  // mismatch means the instance loop and OpenSTA disagree on which
  // instances carry power (e.g. hierarchical instances counted twice).
  const double design_w = designTotalPowerW();
  const double tolerance_w = std::max(1e-3 * design_w, 1e-12);
  if (!warned_total_mismatch_ && std::abs(raw_sum_w - design_w) > tolerance_w) {
    warned_total_mismatch_ = true;
    logger_->warn(utl::THM,
                  193,
                  "Sum of per-instance power ({:.4e} W) differs from the "
                  "OpenSTA design total ({:.4e} W).",
                  raw_sum_w,
                  design_w);
  }
}

double PowerExtractor::totalPowerW(const InstancePhysicsMap& state) const
{
  double total = 0.0;
  for (const auto& [inst, phys] : state) {
    total += phys.dynamic_power_w + phys.leakage_power_w;
  }
  return total;
}

double PowerExtractor::leakagePowerW(const InstancePhysicsMap& state) const
{
  double total = 0.0;
  for (const auto& [inst, phys] : state) {
    total += phys.leakage_power_w;
  }
  return total;
}

DerateApplier::DerateApplier(sta::dbSta* sta, utl::Logger* logger)
    : sta_(sta), logger_(logger)
{
}

sta::Scene* DerateApplier::effectiveCorner() const
{
  return corner_ != nullptr ? corner_ : sta_->cmdScene();
}

static const std::array<sta::PathClkOrData, 2> kClkOrData
    = {sta::PathClkOrData::clk, sta::PathClkOrData::data};

DerateApplier::Factors DerateApplier::readFactors(sta::Sdc* sdc,
                                                  sta::Instance* inst) const
{
  Factors factors;
  factors.fill(1.0f);
  sta::dbNetwork* network = sta_->getDbNetwork();
  std::unique_ptr<sta::InstancePinIterator> pin_iter(
      network->pinIterator(inst));
  if (!pin_iter->hasNext()) {
    return factors;
  }
  // Instance derates are looked up through any pin of the instance; the
  // SDC falls back to cell and global factors when no instance factor is set.
  const sta::Pin* pin = pin_iter->next();
  for (int cd = 0; cd < 2; ++cd) {
    for (int rf = 0; rf < 2; ++rf) {
      for (int el = 0; el < 2; ++el) {
        factors[factorIndex(cd, rf, el)]
            = sdc->timingDerateInstance(pin,
                                        sta::TimingDerateCellType::cell_delay,
                                        kClkOrData[cd],
                                        sta::RiseFall::range()[rf],
                                        sta::EarlyLate::range()[el]);
      }
    }
  }
  return factors;
}

void DerateApplier::writeFactors(sta::Sdc* sdc,
                                 sta::Instance* inst,
                                 const Factors& base,
                                 double scale)
{
  // Slower cells (derate > 1) hurt late (setup) paths; the same factor is
  // applied to early paths and to clock as well as data cells so that clock
  // skew stays physically consistent.
  static const std::array<const sta::RiseFallBoth*, 2> rise_fall
      = {sta::RiseFallBoth::rise(), sta::RiseFallBoth::fall()};
  for (int cd = 0; cd < 2; ++cd) {
    for (int rf = 0; rf < 2; ++rf) {
      for (int el = 0; el < 2; ++el) {
        sta_->setTimingDerate(
            inst,
            sta::TimingDerateCellType::cell_delay,
            kClkOrData[cd],
            rise_fall[rf],
            sta::EarlyLate::range()[el],
            static_cast<float>(base[factorIndex(cd, rf, el)] * scale),
            sdc);
      }
    }
  }
}

void DerateApplier::apply(const InstancePhysicsMap& state)
{
  constexpr double kUnityTolerance = 1e-6;
  sta::dbNetwork* network = sta_->getDbNetwork();
  sta::Sdc* sdc = effectiveCorner()->sdc();
  int changed = 0;
  int restored = 0;
  // Instances that disappeared from the state keep their factors until
  // clear(); instances back at 1.0 are restored right away.
  for (const auto& [inst, phys] : state) {
    const bool unity = std::abs(phys.derate - 1.0) < kUnityTolerance;
    auto applied = applied_derates_.find(inst);
    if (unity) {
      if (applied != applied_derates_.end()) {
        sta::Instance* sta_inst = network->dbToSta(inst);
        if (sta_inst != nullptr) {
          writeFactors(sdc, sta_inst, baseline_.at(inst), 1.0);
        }
        applied_derates_.erase(applied);
        ++restored;
      }
      continue;
    }
    if (applied != applied_derates_.end()
        && std::abs(applied->second - phys.derate) < kUnityTolerance) {
      continue;
    }
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst == nullptr || network->libertyCell(sta_inst) == nullptr) {
      continue;
    }
    auto base = baseline_.find(inst);
    if (base == baseline_.end()) {
      base = baseline_.emplace(inst, readFactors(sdc, sta_inst)).first;
    }
    writeFactors(sdc, sta_inst, base->second, phys.derate);
    applied_derates_[inst] = phys.derate;
    ++changed;
  }
  applied_ = !applied_derates_.empty();
  logger_->info(utl::THM,
                72,
                "Applied physics derates to {} instances.",
                applied_derates_.size());
  debugPrint(logger_,
             utl::THM,
             "derate",
             1,
             "Derate update: {} changed, {} restored to nominal.",
             changed,
             restored);
}

void DerateApplier::clear()
{
  if (!applied_ && applied_derates_.empty()) {
    return;
  }
  sta::dbNetwork* network = sta_->getDbNetwork();
  sta::Sdc* sdc = effectiveCorner()->sdc();
  for (const auto& [inst, derate] : applied_derates_) {
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst != nullptr) {
      writeFactors(sdc, sta_inst, baseline_.at(inst), 1.0);
    }
  }
  debugPrint(logger_,
             utl::THM,
             "derate",
             1,
             "restored pre-physics derates of {} instances",
             applied_derates_.size());
  applied_derates_.clear();
  baseline_.clear();
  applied_ = false;
}

void DerateApplier::timingSummary(double& wns_s, double& tns_s)
{
  sta_->ensureGraph();
  sta_->updateTiming(false);
  sta::Slack worst = 0.0;
  sta::Vertex* vertex = nullptr;
  sta_->worstSlack(sta::MinMax::max(), worst, vertex);
  wns_s = vertex != nullptr ? static_cast<double>(worst) : 0.0;
  tns_s = sta_->totalNegativeSlack(sta::MinMax::max());
}

void DerateApplier::instanceSlacks(InstancePhysicsMap& state, bool derated)
{
  sta::dbNetwork* network = sta_->getDbNetwork();
  sta::Graph* graph = sta_->ensureGraph();
  sta_->updateTiming(false);
  for (auto& [inst, phys] : state) {
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst == nullptr) {
      continue;
    }
    double worst = std::numeric_limits<double>::infinity();
    std::unique_ptr<sta::InstancePinIterator> pin_iter(
        network->pinIterator(sta_inst));
    while (pin_iter->hasNext()) {
      sta::Pin* pin = pin_iter->next();
      sta::Vertex* vertex = graph->pinLoadVertex(pin);
      if (vertex == nullptr) {
        vertex = graph->pinDrvrVertex(pin);
      }
      if (vertex != nullptr) {
        const sta::Slack slack = sta_->slack(vertex, sta::MinMax::max());
        worst = std::min<double>(worst, slack);
      }
    }
    if (!std::isfinite(worst)) {
      worst = 0.0;
    }
    if (derated) {
      phys.slack_derated_s = worst;
    } else {
      phys.slack_nominal_s = worst;
    }
  }
}

IrDropCoupling::IrDropCoupling(psm::PDNSim* psm,
                               sta::dbSta* sta,
                               utl::Logger* logger)
    : psm_(psm), sta_(sta), logger_(logger)
{
}

sta::Scene* IrDropCoupling::effectiveCorner() const
{
  return corner_ != nullptr ? corner_ : sta_->cmdScene();
}

odb::dbNet* IrDropCoupling::findPowerNet(odb::dbBlock* block,
                                         const std::string& name)
{
  if (!name.empty()) {
    return block->findNet(name.c_str());
  }
  odb::dbNet* candidate = nullptr;
  for (odb::dbNet* net : block->getNets()) {
    if (net->getSigType() == odb::dbSigType::POWER) {
      if (!net->getSWires().empty()) {
        return net;
      }
      if (candidate == nullptr) {
        candidate = net;
      }
    }
  }
  return candidate;
}

odb::dbTechLayer* IrDropCoupling::lowestLayer(odb::dbNet* net)
{
  odb::dbTechLayer* lowest = nullptr;
  for (odb::dbSWire* swire : net->getSWires()) {
    for (odb::dbSBox* box : swire->getWires()) {
      if (box->isVia()) {
        continue;
      }
      odb::dbTechLayer* layer = box->getTechLayer();
      if (layer == nullptr
          || layer->getType() != odb::dbTechLayerType::ROUTING) {
        continue;
      }
      if (lowest == nullptr
          || layer->getRoutingLevel() < lowest->getRoutingLevel()) {
        lowest = layer;
      }
    }
  }
  return lowest;
}

bool IrDropCoupling::run(odb::dbBlock* block,
                         const ThermalConfig& config,
                         const ThermalGrid& grid,
                         InstancePhysicsMap& state,
                         double& worst_drop_v,
                         MapSnapshot* ir_map)
{
  worst_drop_v = 0.0;
  for (auto& [inst, phys] : state) {
    phys.vdd_v = config.nominal_vdd_v;
  }
  if (psm_ == nullptr) {
    return false;
  }
  odb::dbNet* net = findPowerNet(block, config.ir_power_net);
  odb::dbTechLayer* layer = net != nullptr ? lowestLayer(net) : nullptr;
  if (layer == nullptr) {
    if (!warned_no_grid_) {
      warned_no_grid_ = true;
      if (!config.ir_power_net.empty() && net == nullptr) {
        logger_->warn(utl::THM,
                      194,
                      "Power net '{}' not found; IR drop is not included and "
                      "the supply voltage stays at the nominal {:.3f} V.",
                      config.ir_power_net,
                      config.nominal_vdd_v);
      } else {
        logger_->warn(utl::THM,
                      195,
                      "No routed power grid found{}; IR drop is not included "
                      "and the supply voltage stays at the nominal {:.3f} V.",
                      net != nullptr ? " on net " + net->getName() : "",
                      config.nominal_vdd_v);
      }
    }
    return false;
  }
  sta::Scene* corner = effectiveCorner();
  // The nominal voltage should match the liberty operating conditions of the
  // corner; otherwise vdd_v = nominal - drop feeds a shifted voltage into the
  // derate model.
  sta::OperatingConditions* op_cond
      = sta_->operatingConditions(sta::MinMax::max(), corner->sdc());
  if (!warned_vdd_ && op_cond != nullptr && op_cond->voltage() > 0
      && std::abs(op_cond->voltage() - config.nominal_vdd_v)
             > 0.05 * op_cond->voltage()) {
    warned_vdd_ = true;
    logger_->warn(utl::THM,
                  196,
                  "Nominal VDD {:.3f} V differs from the {:.3f} V liberty "
                  "operating conditions; use set_thermal_config -nominal_vdd.",
                  config.nominal_vdd_v,
                  op_cond->voltage());
  }
  // Feed the temperature-aware powers to PDNSim so that hot (leaky) regions
  // draw more current.  PDNSim adds user powers on top of the OpenSTA power
  // it computes itself for every instance with a liberty cell, so only the
  // difference between the thermal power and the OpenSTA power is passed
  // (and only where it is non-zero); the grid then sees exactly the thermal
  // power.  The net voltage is left to PDNSim (voltage-source file, SDC or
  // liberty PVT).
  sta::dbNetwork* network = sta_->getDbNetwork();
  for (const auto& [inst, phys] : state) {
    double sta_power_w = 0.0;
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst != nullptr && network->libertyCell(sta_inst) != nullptr) {
      sta_power_w = sta_->power(sta_inst, corner).total();
    }
    const double thermal_power_w = phys.dynamic_power_w + phys.leakage_power_w;
    const double delta_w = thermal_power_w - sta_power_w;
    if (std::abs(delta_w) <= 1e-9 * std::max(thermal_power_w, sta_power_w)) {
      continue;
    }
    psm_->setInstPower(inst, corner, static_cast<float>(delta_w));
  }
  try {
    psm_->analyzePowerGrid(net,
                           corner,
                           psm::GeneratedSourceType::kFull,
                           "",
                           false,
                           false,
                           "",
                           "",
                           config.ir_vsrc_file);
  } catch (const std::exception& e) {
    if (!warned_failed_) {
      warned_failed_ = true;
      logger_->warn(utl::THM,
                    197,
                    "IR-drop analysis of net {} failed ({}); the supply "
                    "voltage stays at the nominal {:.3f} V.",
                    net->getName(),
                    e.what(),
                    config.nominal_vdd_v);
    }
    return false;
  }

  // Voltage drop map on the thermal grid from the lowest routed layer of
  // the grid (the layer the standard cells tap): worst drop of the grid
  // points inside every tile.
  MapSnapshot map;
  map.name = "ir_drop";
  map.units = "V";
  map.nx = grid.nx();
  map.ny = grid.ny();
  map.values.assign(static_cast<size_t>(grid.nx()) * grid.ny(), 0.0);
  std::vector<bool> has(static_cast<size_t>(grid.nx()) * grid.ny(), false);
  psm::PDNSim::IRDropByPoint drops;
  psm_->getIRDropForLayer(net, corner, layer, drops);
  if (drops.empty()) {
    if (!warned_failed_) {
      warned_failed_ = true;
      logger_->warn(utl::THM,
                    198,
                    "IR-drop analysis of net {} produced no voltages on {}; "
                    "the supply voltage stays at the nominal {:.3f} V.",
                    net->getName(),
                    layer->getName(),
                    config.nominal_vdd_v);
    }
    return false;
  }
  for (const auto& [point, drop] : drops) {
    int tx;
    int ty;
    grid.tileAt(point.x(), point.y(), tx, ty);
    const int idx = ty * grid.nx() + tx;
    map.values[idx] = std::max(map.values[idx], drop);
    has[idx] = true;
    worst_drop_v = std::max(worst_drop_v, drop);
  }
  // Fill tiles without grid points from the nearest populated tile in the
  // same row (then column) so that every instance gets a voltage.
  auto fill = [&](int outer_n, int inner_n, auto index_of) {
    for (int o = 0; o < outer_n; ++o) {
      double last = -1.0;
      for (int i = 0; i < inner_n; ++i) {
        const int idx = index_of(o, i);
        if (has[idx]) {
          last = map.values[idx];
        } else if (last >= 0) {
          map.values[idx] = std::max(map.values[idx], last);
        }
      }
      last = -1.0;
      for (int i = inner_n - 1; i >= 0; --i) {
        const int idx = index_of(o, i);
        if (has[idx]) {
          last = map.values[idx];
        } else if (last >= 0) {
          map.values[idx] = std::max(map.values[idx], last);
        }
      }
    }
  };
  fill(grid.ny(), grid.nx(), [&](int y, int x) { return y * grid.nx() + x; });
  fill(grid.nx(), grid.ny(), [&](int x, int y) { return y * grid.nx() + x; });
  for (auto& [inst, phys] : state) {
    const odb::Rect bbox = inst->getBBox()->getBox();
    int tx;
    int ty;
    grid.tileAt(bbox.xCenter(), bbox.yCenter(), tx, ty);
    phys.vdd_v = config.nominal_vdd_v - map.values[ty * grid.nx() + tx];
  }
  logger_->info(utl::THM,
                199,
                "IR drop on {} ({}): worst {:.3e} V over {} grid points.",
                net->getName(),
                layer->getName(),
                worst_drop_v,
                drops.size());
  if (ir_map != nullptr) {
    *ir_map = std::move(map);
  }
  return true;
}

ElectromigrationModel::ElectromigrationModel(
    const ElectromigrationConfig& config)
    : config_(config)
{
}

double ElectromigrationModel::relativeLifetime(double j_a_m2,
                                               double temp_c,
                                               double j_ref_a_m2) const
{
  if (j_a_m2 <= 0 || j_ref_a_m2 <= 0) {
    return std::numeric_limits<double>::infinity();
  }
  const double t_k = temp_c + 273.15;
  const double t_ref_k = config_.reference_temp_c + 273.15;
  const double current_term
      = std::pow(j_ref_a_m2 / j_a_m2, config_.current_exponent);
  const double thermal_term
      = std::exp(config_.activation_energy_ev / kBoltzmannEvPerK
                 * (1.0 / t_k - 1.0 / t_ref_k));
  return current_term * thermal_term;
}

double ElectromigrationModel::worstLifetimeFactor(
    const ThermalGrid& grid,
    const std::vector<std::pair<odb::Rect, double>>& segment_current_densities)
    const
{
  if (segment_current_densities.empty()) {
    return 1.0;
  }
  double j_ref = 0.0;
  for (const auto& [rect, j] : segment_current_densities) {
    j_ref = std::max(j_ref, j);
  }
  const int z = grid.activeZ(0);
  double worst = std::numeric_limits<double>::infinity();
  for (const auto& [rect, j] : segment_current_densities) {
    int tx;
    int ty;
    grid.tileAt(rect.xCenter(), rect.yCenter(), tx, ty);
    const double temp = z >= 0 ? grid.at(tx, ty, z) : config_.reference_temp_c;
    worst = std::min(worst, relativeLifetime(j, temp, j_ref));
  }
  return std::isfinite(worst) ? worst : 1.0;
}

MapSnapshot ElectromigrationModel::tileLifetimeMap(const ThermalGrid& grid,
                                                   const PowerMap& power,
                                                   const MapSnapshot* ir_map,
                                                   double nominal_vdd_v,
                                                   int die) const
{
  MapSnapshot map;
  map.name = "em";
  map.units = "";
  map.die = die;
  map.nx = grid.nx();
  map.ny = grid.ny();
  const std::vector<double> temps = grid.activeLayer(die);
  map.values.assign(temps.size(), 1.0);
  const bool use_current = ir_map != nullptr && ir_map->nx == grid.nx()
                           && ir_map->ny == grid.ny() && power.nx() == grid.nx()
                           && power.ny() == grid.ny() && nominal_vdd_v > 0;
  // Current-density proxy: J ∝ tile current I = P_tile / VDD_tile, the
  // current the grid segments feeding the tile carry (the segment cross
  // section is the same everywhere on one layer).  J_ref = mean current of
  // the powered tiles, so the current term compares every tile against the
  // design-average loading.
  std::vector<double> currents;
  double j_ref = 0.0;
  if (use_current) {
    currents.assign(temps.size(), 0.0);
    int powered = 0;
    for (size_t i = 0; i < temps.size(); ++i) {
      const double vdd = nominal_vdd_v - ir_map->values[i];
      const double p = power.values()[i];
      if (p > 0 && vdd > 0) {
        currents[i] = p / vdd;
        j_ref += currents[i];
        ++powered;
      }
    }
    j_ref = powered > 0 ? j_ref / powered : 0.0;
  }
  for (size_t i = 0; i < temps.size(); ++i) {
    // Tiles without current would have infinite lifetime; report the
    // temperature-only value so that the map stays finite.
    const bool has_current = use_current && j_ref > 0 && currents[i] > 0;
    map.values[i] = has_current ? relativeLifetime(currents[i], temps[i], j_ref)
                                : relativeLifetime(1.0, temps[i], 1.0);
  }
  return map;
}

double ElectromigrationModel::worstFactor(const MapSnapshot& map)
{
  double worst = std::numeric_limits<double>::infinity();
  for (double v : map.values) {
    worst = std::min(worst, v);
  }
  return std::isfinite(worst) ? worst : 1.0;
}

ClockSkewAnalyzer::ClockSkewAnalyzer(sta::dbSta* sta, utl::Logger* logger)
    : sta_(sta), logger_(logger)
{
}

double ClockSkewAnalyzer::worstSkewS()
{
  const sta::Sdc* sdc = sta_->cmdSdc();
  if (sdc == nullptr || sdc->clocks().empty()) {
    return 0.0;
  }
  sta_->ensureGraph();
  sta_->updateTiming(false);
  // Worst source/target register clock-arrival difference over all clocks,
  // late (setup) analysis, including the register internal clock latency.
  return sta_->findWorstClkSkew(sta::SetupHold::max(),
                                /*include_internal_latency=*/true);
}

double ClockSkewAnalyzer::skewDeltaS(DerateApplier& derates,
                                     const InstancePhysicsMap& state)
{
  const bool was_applied = derates.isApplied();
  if (was_applied) {
    derates.clear();
  }
  const double nominal = worstSkewS();
  derates.apply(state);
  const double derated = worstSkewS();
  if (!was_applied) {
    derates.clear();
  }
  return derated - nominal;
}

}  // namespace thm
