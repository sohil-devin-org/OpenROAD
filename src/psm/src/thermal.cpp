// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "thermal.h"

#include <algorithm>
#include <map>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include "db_sta/dbSta.hh"
#include "odb/db.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace psm {

ThermalGrid::ThermalGrid(const odb::Rect& bounds,
                         int rows,
                         int cols,
                         std::vector<double> temperatures_c)
    : bounds_(bounds),
      rows_(rows),
      cols_(cols),
      temperatures_c_(std::move(temperatures_c))
{
}

double ThermalGrid::getTemperature(int row, int col) const
{
  return temperatures_c_[(row * cols_) + col];
}

odb::Rect ThermalGrid::getTileRect(int row, int col) const
{
  const int64_t width = bounds_.dx();
  const int64_t height = bounds_.dy();
  const int x0 = bounds_.xMin() + static_cast<int>(width * col / cols_);
  const int x1 = bounds_.xMin() + static_cast<int>(width * (col + 1) / cols_);
  const int y0 = bounds_.yMin() + static_cast<int>(height * row / rows_);
  const int y1 = bounds_.yMin() + static_cast<int>(height * (row + 1) / rows_);
  return {x0, y0, x1, y1};
}

double ThermalGrid::getMin() const
{
  return *std::min_element(temperatures_c_.begin(), temperatures_c_.end());
}

double ThermalGrid::getMax() const
{
  return *std::max_element(temperatures_c_.begin(), temperatures_c_.end());
}

double ThermalGrid::getAverage() const
{
  return std::accumulate(temperatures_c_.begin(), temperatures_c_.end(), 0.0)
         / temperatures_c_.size();
}

ThermalAnalyzer::ThermalAnalyzer(utl::Logger* logger, sta::dbSta* sta)
    : logger_(logger), sta_(sta)
{
}

bool ThermalAnalyzer::analyze(
    odb::dbBlock* block,
    const ThermalSettings& settings,
    const odb::PtrMap<odb::dbInst, std::map<sta::Scene*, float>>& user_powers)
{
  // TODO(adapter): implement.
  return false;
}

void ThermalAnalyzer::clear()
{
  grid_ = ThermalGrid();
  hot_region_.reset();
}

}  // namespace psm
