// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "heatMap.h"

#include <string>

#include "thm/Thermal.h"
#include "utl/Logger.h"

namespace thm {

TemperatureDataSource::TemperatureDataSource(utl::Logger* logger,
                                             ThermalAnalyzer* analyzer)
    : web::RealValueHeatMapDataSource(logger,
                                      "°C",
                                      "Temperature",
                                      "Temperature",
                                      "Temperature"),
      analyzer_(analyzer)
{
  // TODO(gui-heatmap): expose FixedRange/FixedMin/FixedMax settings.
}

std::string TemperatureDataSource::getValueUnits() const
{
  return "°C";
}

std::string TemperatureDataSource::formatValue(double value, bool legend) const
{
  // TODO(gui-heatmap): absolute Celsius formatting, no SI scaling.
  return web::RealValueHeatMapDataSource::formatValue(value, legend);
}

double TemperatureDataSource::getDisplayRangeIncrement() const
{
  return 1.0;
}

bool TemperatureDataSource::populateMap()
{
  // TODO(gui-heatmap): copy analyzer_->getTemperatureGrid() into the map.
  return false;
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

void TemperatureDataSource::determineMinMax(const HeatMapDataSource::Map& map)
{
  if (fixed_range_) {
    setMinValue(fixed_min_);
    setMaxValue(fixed_max_);
    return;
  }
  web::RealValueHeatMapDataSource::determineMinMax(map);
}

}  // namespace thm
