// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>

#include "odb/db.h"
#include "web/heatMap.h"

namespace utl {
class Logger;
}

namespace psm {
class PDNSim;

// Heat map of the last analyze_thermal result in absolute degrees Celsius.
class ThermalDataSource : public web::RealValueHeatMapDataSource
{
 public:
  ThermalDataSource(PDNSim* psm, utl::Logger* logger);

  bool canAdjustGrid() const override { return false; }

 protected:
  bool populateMap() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;
  void determineMinMax(const web::HeatMapDataSource::Map& map) override;

 private:
  PDNSim* psm_;
};

}  // namespace psm
