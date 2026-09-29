// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>

#include "web/heatMap.h"

namespace thm {

class ThermalAnalyzer;

// GUI heat map of the HotSpot temperature grid in absolute degrees Celsius.
// Unlike the other real valued maps the colour range can be pinned to a fixed
// [min, max] so different designs are directly comparable.
class TemperatureDataSource : public web::RealValueHeatMapDataSource
{
 public:
  TemperatureDataSource(utl::Logger* logger, ThermalAnalyzer* analyzer);

  std::string getValueUnits() const override;
  std::string formatValue(double value, bool legend) const override;
  double getDisplayRangeIncrement() const override;

  // Fixed colour range (°C); disabled by default.
  void setFixedRange(bool enabled) { fixed_range_ = enabled; }
  bool getFixedRange() const { return fixed_range_; }
  void setFixedMin(double value) { fixed_min_ = value; }
  double getFixedMin() const { return fixed_min_; }
  void setFixedMax(double value) { fixed_max_ = value; }
  double getFixedMax() const { return fixed_max_; }

 protected:
  bool populateMap() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;
  void determineMinMax(const HeatMapDataSource::Map& map) override;
  bool destroyMapOnNotVisible() const override { return false; }

 private:
  ThermalAnalyzer* analyzer_;
  bool fixed_range_ = false;
  double fixed_min_ = 25.0;
  double fixed_max_ = 125.0;
};

}  // namespace thm
