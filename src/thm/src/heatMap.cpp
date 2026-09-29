// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "heatMap.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "thm/Thermal.h"
#include "thm/ThermalGrid.h"
#include "utl/Logger.h"

namespace thm {

namespace {

std::vector<std::string> dieChoicesFor(const Thermal* thermal)
{
  std::vector<std::string> choices{"0"};
  if (thermal != nullptr && thermal->config().two_die) {
    choices.emplace_back("1");
  }
  return choices;
}

// Fixed absolute scales offered by the Temperature map (min C, max C).  The
// default 25-125 C spans ambient to the usual junction temperature limit so
// before/after images of the same design share one colour mapping.
struct FixedScale
{
  const char* label;
  double min_c;
  double max_c;
};

constexpr FixedScale kFixedScales[] = {
    {"25-125 C", 25.0, 125.0},
    {"25-85 C", 25.0, 85.0},
    {"0-150 C", 0.0, 150.0},
    {"-40-125 C", -40.0, 125.0},
};

}  // namespace

odb::Rect physicsTileRect(const odb::Rect& die_rect,
                          const int nx,
                          const int ny,
                          const int x,
                          const int y)
{
  const double dx = static_cast<double>(die_rect.dx()) / nx;
  const double dy = static_cast<double>(die_rect.dy()) / ny;
  const int x0 = die_rect.xMin() + static_cast<int>(x * dx);
  const int y0 = die_rect.yMin() + static_cast<int>(y * dy);
  const int x1 = x + 1 == nx ? die_rect.xMax()
                             : die_rect.xMin() + static_cast<int>((x + 1) * dx);
  const int y1 = y + 1 == ny ? die_rect.yMax()
                             : die_rect.yMin() + static_cast<int>((y + 1) * dy);
  return odb::Rect(x0, y0, x1, y1);
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
  addMultipleChoiceSetting(
      "ScaleRange",
      "Fixed range:",
      [this]() { return scaleChoices(); },
      [this]() { return scaleChoice(); },
      [this](const std::string& choice) { setScaleChoice(choice); });
}

std::vector<std::string> TemperatureDataSource::scaleChoices() const
{
  std::vector<std::string> choices;
  for (const FixedScale& scale : kFixedScales) {
    choices.emplace_back(scale.label);
  }
  if (std::none_of(std::begin(kFixedScales),
                   std::end(kFixedScales),
                   [this](const FixedScale& scale) {
                     return scale.min_c == scale_min_c_
                            && scale.max_c == scale_max_c_;
                   })) {
    choices.push_back(scaleChoice());
  }
  return choices;
}

std::string TemperatureDataSource::scaleChoice() const
{
  for (const FixedScale& scale : kFixedScales) {
    if (scale.min_c == scale_min_c_ && scale.max_c == scale_max_c_) {
      return scale.label;
    }
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%g-%g C", scale_min_c_, scale_max_c_);
  return buffer;
}

void TemperatureDataSource::setScaleChoice(const std::string& choice)
{
  for (const FixedScale& scale : kFixedScales) {
    if (choice == scale.label) {
      setScale(scale.min_c, scale.max_c);
      return;
    }
  }
}

bool TemperatureDataSource::populateMap()
{
  if (getBlock() == nullptr || thermal_ == nullptr || !thermal_->hasResults()) {
    return false;
  }
  const MapSnapshot map = thermal_->displayedTemperatureMap(die_);
  if (map.nx <= 0 || map.ny <= 0
      || map.values.size() != static_cast<size_t>(map.nx * map.ny)) {
    return false;
  }
  // A historical snapshot may have been recorded on a different grid than the
  // live one, so tile it over the die area instead of the live grid.
  const ThermalGrid& grid = thermal_->grid();
  const bool live_grid = map.nx == grid.nx() && map.ny == grid.ny();
  const odb::Rect die_rect
      = live_grid ? grid.dieRect() : getBlock()->getDieArea();
  const bool clamp = fixed_scale_ && scale_max_c_ > scale_min_c_;
  for (int y = 0; y < map.ny; ++y) {
    for (int x = 0; x < map.nx; ++x) {
      double value = map.at(x, y);
      if (clamp) {
        // Keep out-of-range temperatures visible at the ends of the fixed
        // scale rather than dropping them from the display range.
        value = std::clamp(value, scale_min_c_, scale_max_c_);
      }
      const odb::Rect tile
          = live_grid ? grid.tileRect(x, y)
                      : physicsTileRect(die_rect, map.nx, map.ny, x, y);
      addToMap(tile, value);
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

bool PhysicsMapDataSource::buildMap(MapSnapshot& map) const
{
  switch (kind_) {
    case Kind::kLeakage:
      map = thermal_->leakageDensityMap(die_);
      return true;
    case Kind::kDerate:
      map = thermal_->derateMap(die_);
      return true;
    case Kind::kIrDrop: {
      const MapSnapshot* ir = thermal_->irDropMap();
      if (ir == nullptr) {
        return false;
      }
      map = *ir;
      return true;
    }
    case Kind::kDelta: {
      // Difference of the displayed temperature map to the last snapshot of
      // the loaded reference history (same die).
      const PhysicsHistory* ref = thermal_->reference();
      if (ref == nullptr || ref->empty()) {
        return false;
      }
      const MapSnapshot* ref_map = ref->back().findMap("temperature", die_);
      map = thermal_->displayedTemperatureMap(die_);
      if (ref_map == nullptr || ref_map->values.size() != map.values.size()) {
        return false;
      }
      for (size_t i = 0; i < map.values.size(); ++i) {
        map.values[i] -= ref_map->values[i];
      }
      return true;
    }
    case Kind::kEmRisk:
      map = thermal_->emRiskMap(die_);
      return true;
  }
  return false;
}

bool PhysicsMapDataSource::populateMap()
{
  if (getBlock() == nullptr || thermal_ == nullptr || !thermal_->hasResults()) {
    return false;
  }
  MapSnapshot map;
  if (!buildMap(map)) {
    return false;
  }
  if (map.nx <= 0 || map.ny <= 0
      || map.values.size() != static_cast<size_t>(map.nx * map.ny)) {
    return false;
  }
  const ThermalGrid& grid = thermal_->grid();
  const bool live_grid = map.nx == grid.nx() && map.ny == grid.ny();
  const odb::Rect die_rect = getBlock()->getDieArea();
  for (int y = 0; y < map.ny; ++y) {
    for (int x = 0; x < map.nx; ++x) {
      const odb::Rect tile
          = live_grid ? grid.tileRect(x, y)
                      : physicsTileRect(die_rect, map.nx, map.ny, x, y);
      addToMap(tile, map.at(x, y));
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
