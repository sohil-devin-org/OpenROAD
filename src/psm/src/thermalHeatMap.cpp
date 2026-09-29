// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "thermalHeatMap.h"

#include "psm/pdnsim.h"
#include "thermal.h"
#include "web/heatMap.h"

namespace psm {

ThermalDataSource::ThermalDataSource(PDNSim* psm, utl::Logger* logger)
    : web::RealValueHeatMapDataSource(logger,
                                      "°C",
                                      "Thermal",
                                      "Thermal",
                                      "Thermal"),
      psm_(psm)
{
}

bool ThermalDataSource::populateMap()
{
  // TODO(gui): implement from psm_->getThermalGrid().
  return false;
}

void ThermalDataSource::combineMapData(bool base_has_value,
                                       double& base,
                                       const double new_data,
                                       const double data_area,
                                       const double intersection_area,
                                       const double rect_area)
{
  base = new_data;
}

void ThermalDataSource::determineMinMax(const HeatMapDataSource::Map& map)
{
  // TODO(gui): honor psm_->getThermalColorRange().
  RealValueHeatMapDataSource::determineMinMax(map);
}

}  // namespace psm
