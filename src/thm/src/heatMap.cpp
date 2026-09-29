// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "heatMap.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "odb/geom.h"
#include "thm/Thermal.h"
#include "utl/Logger.h"

namespace thm {

namespace {

constexpr double kMinTemperatureC = -273.0;
constexpr double kMaxTemperatureC = 1000.0;
constexpr double kFixedRangeStepC = 1.0;

}  // namespace

TemperatureDataSource::TemperatureDataSource(utl::Logger* logger,
                                             ThermalAnalyzer* analyzer)
    : web::RealValueHeatMapDataSource(logger,
                                      "°C",
                                      "Temperature",
                                      "Temperature",
                                      "Temperature"),
      analyzer_(analyzer)
{
  addBooleanSetting(
      "FixedRange",
      "Fixed range:",
      [this]() { return getFixedRange(); },
      [this](bool value) { setFixedRange(value); });
  addDoubleSetting(
      "FixedMin",
      "Fixed minimum (°C):",
      kMinTemperatureC,
      kMaxTemperatureC,
      kFixedRangeStepC,
      [this]() { return getFixedMin(); },
      [this](double value) { setFixedMin(value); });
  addDoubleSetting(
      "FixedMax",
      "Fixed maximum (°C):",
      kMinTemperatureC,
      kMaxTemperatureC,
      kFixedRangeStepC,
      [this]() { return getFixedMax(); },
      [this](double value) { setFixedMax(value); });
}

std::string TemperatureDataSource::getValueUnits() const
{
  return "°C";
}

std::string TemperatureDataSource::formatValue(double value, bool legend) const
{
  // HotSpot reports temperatures with two decimals; show them when the
  // whole map spans less than a degree so a nearly flat map stays readable.
  const bool fine = getMaxValue() - getMinValue() < 1.0;
  char text[32];
  std::snprintf(
      text, sizeof(text), fine ? "%.2f" : "%.1f", convertPercentToValue(value));
  std::string result(text);
  if (legend) {
    result += " " + getValueUnits();
  }
  return result;
}

double TemperatureDataSource::getDisplayRangeIncrement() const
{
  return 1.0;
}

bool TemperatureDataSource::hasGrid() const
{
  return analyzer_ != nullptr && getBlock() != nullptr
         && analyzer_->hasResults() && !analyzer_->getTemperatureGrid().empty();
}

double TemperatureDataSource::getGridXSize() const
{
  if (!hasGrid()) {
    return web::RealValueHeatMapDataSource::getGridXSize();
  }
  const TemperatureGrid& grid = analyzer_->getTemperatureGrid();
  return grid.bounds.dx() / (grid.cols * getDbuPerMicron());
}

double TemperatureDataSource::getGridYSize() const
{
  if (!hasGrid()) {
    return web::RealValueHeatMapDataSource::getGridYSize();
  }
  const TemperatureGrid& grid = analyzer_->getTemperatureGrid();
  return grid.bounds.dy() / (grid.rows * getDbuPerMicron());
}

odb::Rect TemperatureDataSource::getBounds() const
{
  if (hasGrid()) {
    return analyzer_->getTemperatureGrid().bounds;
  }
  return web::RealValueHeatMapDataSource::getBounds();
}

void TemperatureDataSource::setFixedRange(bool enabled)
{
  if (fixed_range_ == enabled) {
    return;
  }
  fixed_range_ = enabled;
  update();
}

void TemperatureDataSource::setFixedMin(double value)
{
  if (fixed_min_ == value) {
    return;
  }
  fixed_min_ = value;
  if (fixed_range_) {
    update();
  }
}

void TemperatureDataSource::setFixedMax(double value)
{
  if (fixed_max_ == value) {
    return;
  }
  fixed_max_ = value;
  if (fixed_range_) {
    update();
  }
}

void TemperatureDataSource::populateXYGrid()
{
  if (!hasGrid()) {
    web::RealValueHeatMapDataSource::populateXYGrid();
    return;
  }

  const TemperatureGrid& grid = analyzer_->getTemperatureGrid();
  std::vector<int> x_grid;
  std::vector<int> y_grid;
  x_grid.reserve(grid.cols + 1);
  y_grid.reserve(grid.rows + 1);
  for (int col = 0; col < grid.cols; col++) {
    x_grid.push_back(grid.cellRect(0, col).xMin());
  }
  x_grid.push_back(grid.bounds.xMax());
  for (int row = 0; row < grid.rows; row++) {
    y_grid.push_back(grid.cellRect(row, 0).yMin());
  }
  y_grid.push_back(grid.bounds.yMax());

  setXYMapGrid(x_grid, y_grid);
}

bool TemperatureDataSource::populateMap()
{
  if (!hasGrid()) {
    return false;
  }

  const TemperatureGrid& grid = analyzer_->getTemperatureGrid();
  for (int row = 0; row < grid.rows; row++) {
    for (int col = 0; col < grid.cols; col++) {
      addToMap(grid.cellRect(row, col), grid.at(row, col));
    }
  }
  return true;
}

void TemperatureDataSource::combineMapData(bool base_has_value,
                                           double& base,
                                           double new_data,
                                           double data_area,
                                           double intersection_area,
                                           double rect_area)
{
  // Temperature is an intensive quantity: area weighted average.
  base += new_data * intersection_area / rect_area;
}

void TemperatureDataSource::correctMapScale(HeatMapDataSource::Map& map)
{
  // Temperatures stay in absolute °C: no SI prefix scaling of the range.
  determineMinMax(map);

  for (const auto& map_col : map) {
    for (const auto& map_pt : map_col) {
      double percent = convertValueToPercent(map_pt->value);
      if (fixed_range_) {
        percent = std::clamp(percent, 0.0, 100.0);
      }
      map_pt->value = percent;
    }
  }
}

void TemperatureDataSource::determineMinMax(const HeatMapDataSource::Map& map)
{
  if (fixed_range_) {
    double min = std::min(fixed_min_, fixed_max_);
    double max = std::max(fixed_min_, fixed_max_);
    if (min == max) {
      // Equal endpoints: show a 1 degC band centred on the value so the legend
      // and the colours describe the same interval.
      min -= 0.5;
      max += 0.5;
    }
    setMinValue(min);
    setMaxValue(max);
    return;
  }

  double min = std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::lowest();
  for (const auto& map_col : map) {
    for (const auto& map_pt : map_col) {
      if (!map_pt->has_value) {
        continue;
      }
      min = std::min(min, map_pt->value);
      max = std::max(max, map_pt->value);
    }
  }
  if (min > max) {
    min = 0.0;
    max = 0.0;
  }
  setMinValue(min);
  setMaxValue(max);
}

}  // namespace thm
