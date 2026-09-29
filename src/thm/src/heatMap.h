// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>

#include "odb/geom.h"
#include "web/heatMap.h"

namespace thm {

class ThermalAnalyzer;

// GUI heat map of the HotSpot temperature grid in absolute degrees Celsius.
// The map cells are exactly the temperature grid cells (no adjustable grid).
// Unlike the other real valued maps the colour range can be pinned to a fixed
// [min, max] so different designs are directly comparable.
class TemperatureDataSource : public web::RealValueHeatMapDataSource
{
 public:
  TemperatureDataSource(utl::Logger* logger, ThermalAnalyzer* analyzer);

  std::string getValueUnits() const override;
  std::string formatValue(double value, bool legend) const override;
  double getDisplayRangeIncrement() const override;

  bool canAdjustGrid() const override { return false; }
  double getGridXSize() const override;
  double getGridYSize() const override;
  odb::Rect getBounds() const override;

  // Fixed colour range (°C); disabled by default.  Values outside the range
  // are clamped to the end colours.
  void setFixedRange(bool enabled);
  bool getFixedRange() const { return fixed_range_; }
  void setFixedMin(double value);
  double getFixedMin() const { return fixed_min_; }
  void setFixedMax(double value);
  double getFixedMax() const { return fixed_max_; }

 protected:
  bool populateMap() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;
  void correctMapScale(HeatMapDataSource::Map& map) override;
  void determineMinMax(const HeatMapDataSource::Map& map) override;
  void populateXYGrid() override;
  bool destroyMapOnNotVisible() const override { return false; }

 private:
  bool hasGrid() const;

  ThermalAnalyzer* analyzer_;
  bool fixed_range_ = false;
  double fixed_min_ = 25.0;
  double fixed_max_ = 125.0;
};

}  // namespace thm
