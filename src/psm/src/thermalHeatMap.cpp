// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "thermalHeatMap.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "odb/geom.h"
#include "psm/pdnsim.h"
#include "thermal.h"
#include "utl/Logger.h"
#include "web/heatMap.h"

namespace psm {

ThermalDataSource::ThermalDataSource(PDNSim* psm, utl::Logger* logger)
    : web::RealValueHeatMapDataSource(logger,
                                      "°C",
                                      "Thermal",
                                      "Thermal",
                                      "Thermal"),
      psm_(psm),
      logger_(logger)
{
  // Temperatures below a fixed color range use the lowest color.
  setDrawBelowRangeMin(true);
}

odb::Rect ThermalDataSource::getBounds() const
{
  const ThermalGrid& grid = psm_->getThermalGrid();
  if (grid.empty()) {
    return RealValueHeatMapDataSource::getBounds();
  }
  return grid.getBounds();
}

std::string ThermalDataSource::formatValue(const double value,
                                           const bool legend) const
{
  std::string text = fmt::format("{:.2f}", convertPercentToValue(value));
  if (legend) {
    text += " " + getValueUnits();
  }
  return text;
}

double ThermalDataSource::getTemperatureRange() const
{
  const double range = getMaxValue() - getMinValue();
  return range == 0.0 ? 1.0 : range;
}

double ThermalDataSource::convertValueToPercent(const double value) const
{
  return 100.0 * (value - getMinValue()) / getTemperatureRange();
}

double ThermalDataSource::convertPercentToValue(const double percent) const
{
  return percent * getTemperatureRange() / 100.0 + getMinValue();
}

double ThermalDataSource::getGridXSize() const
{
  const ThermalGrid& grid = psm_->getThermalGrid();
  if (grid.empty() || getBlock() == nullptr) {
    return RealValueHeatMapDataSource::getGridXSize();
  }
  int min_width = grid.getBounds().dx();
  for (int col = 0; col < grid.getCols(); col++) {
    const int width = grid.getTileRect(0, col).dx();
    if (width > 0) {
      min_width = std::min(min_width, width);
    }
  }
  return min_width / getDbuPerMicron();
}

double ThermalDataSource::getGridYSize() const
{
  const ThermalGrid& grid = psm_->getThermalGrid();
  if (grid.empty() || getBlock() == nullptr) {
    return RealValueHeatMapDataSource::getGridYSize();
  }
  int min_height = grid.getBounds().dy();
  for (int row = 0; row < grid.getRows(); row++) {
    const int height = grid.getTileRect(row, 0).dy();
    if (height > 0) {
      min_height = std::min(min_height, height);
    }
  }
  return min_height / getDbuPerMicron();
}

void ThermalDataSource::populateXYGrid()
{
  const ThermalGrid& grid = psm_->getThermalGrid();
  if (grid.empty()) {
    RealValueHeatMapDataSource::populateXYGrid();
    return;
  }

  std::vector<int> x_grid;
  x_grid.reserve(grid.getCols() + 1);
  for (int col = 0; col < grid.getCols(); col++) {
    x_grid.push_back(grid.getTileRect(0, col).xMin());
  }
  x_grid.push_back(grid.getBounds().xMax());

  std::vector<int> y_grid;
  y_grid.reserve(grid.getRows() + 1);
  for (int row = 0; row < grid.getRows(); row++) {
    y_grid.push_back(grid.getTileRect(row, 0).yMin());
  }
  y_grid.push_back(grid.getBounds().yMax());

  setXYMapGrid(x_grid, y_grid);
}

bool ThermalDataSource::populateMap()
{
  const ThermalGrid& grid = psm_->getThermalGrid();
  if (grid.empty() || getBlock() == nullptr) {
    return false;
  }

  for (int row = 0; row < grid.getRows(); row++) {
    for (int col = 0; col < grid.getCols(); col++) {
      const odb::Rect tile = grid.getTileRect(row, col);
      if (tile.area() == 0) {
        continue;
      }
      addToMap(tile, grid.getTemperature(row, col));
    }
  }

  return true;
}

void ThermalDataSource::combineMapData(const bool base_has_value,
                                       double& base,
                                       const double new_data,
                                       const double data_area,
                                       const double intersection_area,
                                       const double rect_area)
{
  const double weighted = new_data * intersection_area / rect_area;
  if (!base_has_value) {
    base = weighted;
  } else {
    base += weighted;
  }
}

void ThermalDataSource::correctMapScale(HeatMapDataSource::Map& map)
{
  // Absolute temperatures keep the "°C" unit; the base class would switch
  // to an SI prefix (e.g. m°C) for ranges below one degree.
  determineMinMax(map);
  for (const auto& map_col : map) {
    for (const auto& map_pt : map_col) {
      map_pt->value = convertValueToPercent(map_pt->value);
    }
  }
}

void ThermalDataSource::determineMinMax(const HeatMapDataSource::Map& map)
{
  const auto range = psm_->getThermalColorRange();
  if (range) {
    setMinValue(range->first);
    setMaxValue(range->second);
  } else {
    double min_value = std::numeric_limits<double>::max();
    double max_value = std::numeric_limits<double>::lowest();
    for (const auto& map_col : map) {
      for (const auto& map_pt : map_col) {
        if (!map_pt->has_value) {
          continue;
        }
        min_value = std::min(min_value, map_pt->value);
        max_value = std::max(max_value, map_pt->value);
      }
    }
    setMinValue(min_value);
    setMaxValue(max_value);
  }

  debugPrint(logger_,
             utl::PSM,
             "thermal_heatmap",
             1,
             "Thermal heat map: {} x {} tiles, {} color range {:.2f} - {:.2f} "
             "{}",
             psm_->getThermalGrid().getRows(),
             psm_->getThermalGrid().getCols(),
             range ? "fixed" : "auto",
             getMinValue(),
             getMaxValue(),
             getValueUnits());
}

}  // namespace psm
