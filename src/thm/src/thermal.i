// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

%include "../../Exception.i"
%{
#include "ord/OpenRoad.hh"
#include "sta/Scene.hh"
#include "thm/Thermal.h"

namespace ord {
thm::ThermalAnalyzer*
getThermalAnalyzer();
}

using ord::getThermalAnalyzer;
using sta::Scene;
using thm::ThermalAnalyzer;
using thm::ThermalOptions;
%}

// OpenSTA swig rules
%include "tcl/StaTclTypes.i"

%inline %{

namespace thm {

void analyze_thermal_cmd(Scene* corner,
                         const char* hotspot_binary,
                         const char* hotspot_config,
                         const char* work_dir,
                         bool keep_files,
                         double tile_size_um,
                         int grid_rows,
                         int grid_cols,
                         double ambient_c,
                         int report_instances,
                         const char* report_file)
{
  ThermalOptions options;
  if (hotspot_binary[0] != '\0') {
    options.hotspot_binary = hotspot_binary;
  }
  options.hotspot_config = hotspot_config;
  options.work_dir = work_dir;
  options.keep_files = keep_files;
  options.tile_size_um = tile_size_um;
  options.grid_rows = grid_rows;
  options.grid_cols = grid_cols;
  options.ambient_c = ambient_c;
  options.report_instances = report_instances;
  options.report_file = report_file;
  getThermalAnalyzer()->analyze(corner, options);
}

void report_thermal_cmd(const char* file)
{
  getThermalAnalyzer()->report(file);
}

void write_temperature_map_cmd(const char* file)
{
  getThermalAnalyzer()->writeTemperatureCsv(file);
}

// Test hook: load a temperature grid without running HotSpot.
void read_temperature_grid_cmd(const char* file)
{
  getThermalAnalyzer()->readTemperatureGrid(file);
}

bool has_thermal_results()
{
  return getThermalAnalyzer()->hasResults();
}

double get_peak_temperature()
{
  return getThermalAnalyzer()->getStats().peak_c;
}

double get_average_temperature()
{
  return getThermalAnalyzer()->getStats().average_c;
}

void clear_thermal_results()
{
  getThermalAnalyzer()->clearResults();
}

}  // namespace thm

%}
