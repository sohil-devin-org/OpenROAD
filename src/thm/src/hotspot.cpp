// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "hotspot.h"

#include <string>
#include <vector>

#include "utl/Logger.h"

namespace thm {

HotSpotAdapter::HotSpotAdapter(utl::Logger* logger, int dbu_per_micron)
    : logger_(logger), dbu_per_micron_(dbu_per_micron)
{
}

std::string HotSpotAdapter::findBinary(const std::string& binary) const
{
  // TODO(hotspot-adapter): resolve through PATH.
  return binary;
}

void HotSpotAdapter::writeFloorplan(const std::string& file,
                                    const std::vector<PowerTile>& tiles,
                                    const odb::Rect& bounds) const
{
  // TODO(hotspot-adapter)
  logger_->error(utl::THM, 90, "HotSpot floorplan export not implemented.");
}

void HotSpotAdapter::writePowerTrace(const std::string& file,
                                     const std::vector<PowerTile>& tiles) const
{
  // TODO(hotspot-adapter)
  logger_->error(utl::THM, 91, "HotSpot power trace export not implemented.");
}

void HotSpotAdapter::writeConfig(const std::string& file,
                                 double ambient_c,
                                 int grid_rows,
                                 int grid_cols) const
{
  // TODO(hotspot-adapter)
  logger_->error(utl::THM, 92, "HotSpot config export not implemented.");
}

bool HotSpotAdapter::run(const std::string& binary,
                         const std::string& config,
                         const std::string& floorplan,
                         const std::string& ptrace,
                         int grid_rows,
                         int grid_cols,
                         const std::string& steady_file,
                         const std::string& grid_steady_file,
                         const std::string& log_file) const
{
  // TODO(hotspot-adapter)
  logger_->error(utl::THM, 93, "HotSpot execution not implemented.");
  return false;
}

bool HotSpotAdapter::readGridSteady(const std::string& file,
                                    int grid_rows,
                                    int grid_cols,
                                    const odb::Rect& bounds,
                                    TemperatureGrid& grid) const
{
  // TODO(hotspot-adapter)
  logger_->error(utl::THM, 94, "HotSpot grid parsing not implemented.");
  return false;
}

}  // namespace thm
