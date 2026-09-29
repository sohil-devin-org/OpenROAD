// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "odb/geom.h"
#include "thm/Thermal.h"

namespace utl {
class Logger;
}  // namespace utl

namespace thm {

// Thin adapter around the external HotSpot executable
// (https://github.com/uvahotspot/HotSpot).  Writes the floorplan (.flp) and
// power trace (.ptrace) files HotSpot expects, runs it in grid mode and reads
// the steady-state grid temperatures back.
class HotSpotAdapter
{
 public:
  HotSpotAdapter(utl::Logger* logger, int dbu_per_micron);

  // Locates the HotSpot binary (absolute path or through PATH).  Returns an
  // empty string when it cannot be found.
  std::string findBinary(const std::string& binary) const;

  // HotSpot floorplan: "<unit-name> <width> <height> <left-x> <bottom-y>"
  // in meters, one line per tile.
  void writeFloorplan(const std::string& file,
                      const std::vector<PowerTile>& tiles,
                      const odb::Rect& bounds) const;

  // HotSpot power trace: header line with the unit names followed by one
  // line of power values (watts).  Steady state only needs a single sample.
  void writePowerTrace(const std::string& file,
                       const std::vector<PowerTile>& tiles) const;

  // Writes a HotSpot configuration derived from the stock example config
  // with the ambient temperature and grid size overridden.
  void writeConfig(const std::string& file,
                   double ambient_c,
                   int grid_rows,
                   int grid_cols) const;

  // Runs "hotspot -c config -f flp -p ptrace -model_type grid
  //   -grid_rows rows -grid_cols cols -steady_file ... -grid_steady_file ..."
  // Returns true when HotSpot exits successfully and produced the grid file.
  bool run(const std::string& binary,
           const std::string& config,
           const std::string& floorplan,
           const std::string& ptrace,
           int grid_rows,
           int grid_cols,
           const std::string& steady_file,
           const std::string& grid_steady_file,
           const std::string& log_file) const;

  // Parses a HotSpot grid steady file (one "<index> <kelvin>" line per grid
  // cell for each layer; the silicon layer is used) into a grid in Celsius.
  // HotSpot numbers grid cells from the top-left, this converts to the
  // bottom-left origin used by TemperatureGrid.
  bool readGridSteady(const std::string& file,
                      int grid_rows,
                      int grid_cols,
                      const odb::Rect& bounds,
                      TemperatureGrid& grid) const;

  static constexpr double kKelvinOffset = 273.15;
  // HotSpot's flp.h MAX_UNITS: the most floorplan units it accepts.
  static constexpr int64_t kMaxUnits = 8192;

 private:
  utl::Logger* logger_;
  int dbu_per_micron_;
};

}  // namespace thm
