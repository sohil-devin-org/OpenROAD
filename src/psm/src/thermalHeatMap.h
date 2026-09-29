// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>

#include "odb/db.h"
#include "odb/geom.h"
#include "web/heatMap.h"

namespace utl {
class Logger;
}

namespace psm {
class PDNSim;

// Heat map of the last analyze_thermal result in absolute degrees Celsius.
//
// The display grid is the thermal tile grid, so every displayed cell holds
// exactly one HotSpot tile temperature. Values are combined with an
// area-weighted average, which reduces to the tile temperature when the
// grids coincide.
//
// The color scale spans the min..max temperature of the grid, or the fixed
// range from set_thermal_color_range. Temperatures outside a fixed range are
// drawn with the end colors while the cell values keep the true temperature.
class ThermalDataSource : public web::RealValueHeatMapDataSource
{
 public:
  ThermalDataSource(PDNSim* psm, utl::Logger* logger);

  bool canAdjustGrid() const override { return false; }
  odb::Rect getBounds() const override;
  std::string formatValue(double value, bool legend) const override;
  double convertValueToPercent(double value) const override;
  double convertPercentToValue(double percent) const override;
  double getGridXSize() const override;
  double getGridYSize() const override;

 protected:
  bool populateMap() override;
  void populateXYGrid() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;
  void correctMapScale(web::HeatMapDataSource::Map& map) override;
  void determineMinMax(const web::HeatMapDataSource::Map& map) override;

 private:
  double getTemperatureRange() const;

  PDNSim* psm_;
  utl::Logger* logger_;
};

}  // namespace psm
