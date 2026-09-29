// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "heatMap.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "thm/Thermal.h"
#include "utl/Logger.h"

namespace thm {

static std::vector<std::string> dieChoicesFor(const Thermal* thermal)
{
  std::vector<std::string> choices{"0"};
  if (thermal != nullptr && thermal->config().two_die) {
    choices.emplace_back("1");
  }
  return choices;
}

std::vector<std::string> TemperatureDataSource::dieChoices() const
{
  return dieChoicesFor(thermal_);
}

std::vector<std::string> PhysicsMapDataSource::dieChoices() const
{
  return dieChoicesFor(thermal_);
}

TemperatureDataSource::TemperatureDataSource(Thermal* thermal,
                                             utl::Logger* logger)
    : web::RealValueHeatMapDataSource(logger,
                                      "C",
                                      "Temperature",
                                      "Temperature",
                                      "Temperature"),
      thermal_(thermal)
{
  addMultipleChoiceSetting(
      "Die",
      "Die:",
      [this]() { return dieChoices(); },
      [this]() { return std::to_string(die_); },
      [this](const std::string& die) { die_ = std::stoi(die); });
  addBooleanSetting(
      "FixedScale",
      "Fixed scale (C):",
      [this]() { return fixed_scale_; },
      [this](bool fixed) { fixed_scale_ = fixed; });
}

bool TemperatureDataSource::populateMap()
{
  if (getBlock() == nullptr || thermal_ == nullptr || !thermal_->hasResults()) {
    return false;
  }
  const ThermalGrid& grid = thermal_->grid();
  const int z = grid.activeZ(die_);
  if (z < 0) {
    return false;
  }
  for (int y = 0; y < grid.ny(); ++y) {
    for (int x = 0; x < grid.nx(); ++x) {
      addToMap(grid.tileRect(x, y), grid.at(x, y, z));
    }
  }
  return true;
}

void TemperatureDataSource::determineMinMax(
    const web::HeatMapDataSource::Map& map)
{
  if (fixed_scale_ && scale_max_c_ > scale_min_c_) {
    setMinValue(scale_min_c_);
    setMaxValue(scale_max_c_);
    return;
  }
  web::RealValueHeatMapDataSource::determineMinMax(map);
}

void TemperatureDataSource::combineMapData(bool base_has_value,
                                           double& base,
                                           const double new_data,
                                           const double data_area,
                                           const double intersection_area,
                                           const double rect_area)
{
  if (!base_has_value) {
    base = new_data;
  } else {
    base = std::max(base, new_data);
  }
}

PhysicsMapDataSource::PhysicsMapDataSource(Thermal* thermal,
                                           Kind kind,
                                           utl::Logger* logger,
                                           const std::string& unit_suffix,
                                           const std::string& name,
                                           const std::string& short_name)
    : web::RealValueHeatMapDataSource(logger,
                                      unit_suffix,
                                      name,
                                      short_name,
                                      "Physics"),
      thermal_(thermal),
      kind_(kind)
{
  addMultipleChoiceSetting(
      "Die",
      "Die:",
      [this]() { return dieChoices(); },
      [this]() { return std::to_string(die_); },
      [this](const std::string& die) { die_ = std::stoi(die); });
}

bool PhysicsMapDataSource::populateMap()
{
  if (getBlock() == nullptr || thermal_ == nullptr || !thermal_->hasResults()) {
    return false;
  }
  MapSnapshot map;
  switch (kind_) {
    case Kind::kLeakage:
      map = thermal_->leakageDensityMap(die_);
      break;
    case Kind::kDerate:
      map = thermal_->derateMap(die_);
      break;
    case Kind::kIrDrop: {
      const MapSnapshot* ir = thermal_->irDropMap();
      if (ir == nullptr) {
        return false;
      }
      map = *ir;
      break;
    }
    case Kind::kDelta: {
      const PhysicsHistory* ref = thermal_->reference();
      if (ref == nullptr || ref->empty()) {
        return false;
      }
      const MapSnapshot* ref_map = ref->back().findMap("temperature", die_);
      map = thermal_->temperatureMap(die_);
      if (ref_map == nullptr || ref_map->values.size() != map.values.size()) {
        return false;
      }
      for (size_t i = 0; i < map.values.size(); ++i) {
        map.values[i] -= ref_map->values[i];
      }
      break;
    }
    case Kind::kEmRisk:
      map = thermal_->emRiskMap(die_);
      break;
  }
  if (map.nx == 0 || map.ny == 0) {
    return false;
  }
  const ThermalGrid& grid = thermal_->grid();
  for (int y = 0; y < map.ny; ++y) {
    for (int x = 0; x < map.nx; ++x) {
      addToMap(grid.tileRect(x, y), map.at(x, y));
    }
  }
  return true;
}

void PhysicsMapDataSource::combineMapData(bool base_has_value,
                                          double& base,
                                          const double new_data,
                                          const double data_area,
                                          const double intersection_area,
                                          const double rect_area)
{
  if (!base_has_value) {
    base = new_data;
  } else {
    base = std::max(base, new_data);
  }
}

}  // namespace thm
