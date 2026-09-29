// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/PhysicsCoupling.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "db_sta/dbNetwork.hh"
#include "db_sta/dbSta.hh"
#include "psm/pdnsim.h"
#include "sta/Graph.hh"
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

bool PowerExtractor::readActivityFile(const std::string& file,
                                      const std::string& scope)
{
  // Activity files are read through the existing read_vcd / read_saif
  // commands (see thermal.tcl); this only records that activity is present.
  logger_->info(utl::THM,
                70,
                "Using switching activity from {} (scope '{}') for power "
                "extraction.",
                file,
                scope);
  return true;
}

void PowerExtractor::extract(odb::dbBlock* block,
                             const LeakageModel& leakage_model,
                             double nominal_temp_c,
                             InstancePhysicsMap& state)
{
  sta::dbNetwork* network = sta_->getDbNetwork();
  sta::Scene* corner = corner_ != nullptr ? corner_ : sta_->cmdScene();
  sta_->ensureGraph();
  sta_->ensureLevelized();
  int missing = 0;
  for (odb::dbInst* inst : block->getInsts()) {
    if (!inst->isPlaced() && !inst->isFixed()) {
      continue;
    }
    InstancePhysics& phys = state[inst];
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst == nullptr) {
      ++missing;
      continue;
    }
    sta::PowerResult result = sta_->power(sta_inst, corner);
    phys.dynamic_power_w
        = activity_scale_ * (result.internal() + result.switching());
    const double temp
        = phys.temperature_c > 0 ? phys.temperature_c : nominal_temp_c;
    phys.leakage_power_w = leakage_model.leakageAt(
        inst->getMaster()->getName(), result.leakage(), nominal_temp_c, temp);
  }
  if (missing > 0) {
    logger_->warn(utl::THM,
                  71,
                  "{} instances have no timing model and contribute no power.",
                  missing);
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

void DerateApplier::apply(const InstancePhysicsMap& state)
{
  sta::dbNetwork* network = sta_->getDbNetwork();
  sta::Scene* corner = sta_->cmdScene();
  sta::Sdc* sdc = corner->sdc();
  int count = 0;
  for (const auto& [inst, phys] : state) {
    sta::Instance* sta_inst = network->dbToSta(inst);
    if (sta_inst == nullptr || std::abs(phys.derate - 1.0) < 1e-9) {
      continue;
    }
    // Slower cells (derate > 1) hurt late (setup) paths; the same factor is
    // applied to early paths so that clock skew stays physically consistent.
    for (const sta::EarlyLate* el : sta::EarlyLate::range()) {
      sta_->setTimingDerate(sta_inst,
                            sta::TimingDerateCellType::cell_delay,
                            sta::PathClkOrData::data,
                            sta::RiseFallBoth::riseFall(),
                            el,
                            static_cast<float>(phys.derate),
                            sdc);
      sta_->setTimingDerate(sta_inst,
                            sta::TimingDerateCellType::cell_delay,
                            sta::PathClkOrData::clk,
                            sta::RiseFallBoth::riseFall(),
                            el,
                            static_cast<float>(phys.derate),
                            sdc);
    }
    ++count;
  }
  applied_ = count > 0;
  logger_->info(
      utl::THM, 72, "Applied physics derates to {} instances.", count);
}

void DerateApplier::clear()
{
  if (applied_) {
    sta::Scene* corner = sta_->cmdScene();
    sta_->unsetTimingDerate(corner->sdc());
    applied_ = false;
  }
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

static odb::dbNet* findSupplyNet(odb::dbBlock* block)
{
  odb::dbNet* candidate = nullptr;
  for (odb::dbNet* net : block->getNets()) {
    if (net->getSigType() == odb::dbSigType::POWER) {
      if (net->getSWires().size() > 0) {
        return net;
      }
      if (candidate == nullptr) {
        candidate = net;
      }
    }
  }
  return candidate;
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
  odb::dbNet* net = findSupplyNet(block);
  if (net == nullptr || net->getSWires().empty()) {
    logger_->info(
        utl::THM, 73, "No routed power grid found; IR drop is not included.");
    return false;
  }
  sta::Scene* corner = sta_->cmdScene();
  // Feed the temperature-aware powers to PDNSim so that hot (leaky) regions
  // draw more current.
  for (const auto& [inst, phys] : state) {
    psm_->setInstPower(
        inst,
        corner,
        static_cast<float>(phys.dynamic_power_w + phys.leakage_power_w));
  }
  psm_->setNetVoltage(net, corner, config.nominal_vdd_v);
  try {
    psm_->analyzePowerGrid(net,
                           corner,
                           psm::GeneratedSourceType::kFull,
                           "",
                           false,
                           false,
                           "",
                           "",
                           "");
  } catch (const std::exception& e) {
    logger_->warn(utl::THM, 74, "IR-drop analysis failed: {}", e.what());
    return false;
  }

  odb::dbTech* tech = block->getTech();
  // Voltage drop map on the thermal grid: worst drop of every point in a
  // tile, over all layers (the cells see the lowest metal, but that is the
  // most pessimistic and stable choice across PDN styles).
  MapSnapshot map;
  map.name = "ir_drop";
  map.units = "V";
  map.nx = grid.nx();
  map.ny = grid.ny();
  map.values.assign(static_cast<size_t>(grid.nx()) * grid.ny(), 0.0);
  std::vector<bool> has(static_cast<size_t>(grid.nx()) * grid.ny(), false);
  for (odb::dbTechLayer* layer : tech->getLayers()) {
    psm::PDNSim::IRDropByPoint drops;
    psm_->getIRDropForLayer(net, corner, layer, drops);
    for (const auto& [point, drop] : drops) {
      int tx;
      int ty;
      grid.tileAt(point.x(), point.y(), tx, ty);
      const int idx = ty * grid.nx() + tx;
      map.values[idx] = std::max(map.values[idx], drop);
      has[idx] = true;
      worst_drop_v = std::max(worst_drop_v, drop);
    }
  }
  // Fill tiles without grid points from the nearest populated tile in the
  // same row so that every instance gets a voltage.
  for (int y = 0; y < grid.ny(); ++y) {
    double last = -1.0;
    for (int x = 0; x < grid.nx(); ++x) {
      const int idx = y * grid.nx() + x;
      if (has[idx]) {
        last = map.values[idx];
      } else if (last >= 0) {
        map.values[idx] = last;
      }
    }
    last = -1.0;
    for (int x = grid.nx() - 1; x >= 0; --x) {
      const int idx = y * grid.nx() + x;
      if (has[idx]) {
        last = map.values[idx];
      } else if (last >= 0) {
        map.values[idx] = std::max(map.values[idx], last);
      }
    }
  }
  for (auto& [inst, phys] : state) {
    const odb::Rect bbox = inst->getBBox()->getBox();
    int tx;
    int ty;
    grid.tileAt(bbox.xCenter(), bbox.yCenter(), tx, ty);
    phys.vdd_v = config.nominal_vdd_v - map.values[ty * grid.nx() + tx];
  }
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
