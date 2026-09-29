// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2025, The OpenROAD Authors

%include "../../Exception.i"
%{
#include "ord/OpenRoad.hh"
#include "psm/pdnsim.h"
#include "sta/Scene.hh"
#include "thermal.h"

namespace ord {
psm::PDNSim*
getPDNSim();
}

namespace odb {
class dbNet;
}

using ord::getPDNSim;
using psm::PDNSim;
using sta::Scene;

#if TCL_MAJOR_VERSION < 9 && !defined(Tcl_Size)
  typedef int Tcl_Size;
#endif

%}

// OpenSTA swig rules
%include "tcl/StaTclTypes.i"

%typemap(in) psm::GeneratedSourceType {
  Tcl_Size length;
  const char *arg = Tcl_GetStringFromObj($input, &length);

  if (strcmp(arg, "BUMPS") == 0) {
    $1 = psm::GeneratedSourceType::kBumps;
  } else if (strcmp(arg, "FULL") == 0) {
    $1 = psm::GeneratedSourceType::kFull;
  } else if (strcmp(arg, "STRAPS") == 0) {
    $1 = psm::GeneratedSourceType::kStraps;
  } else {
    $1 = psm::GeneratedSourceType::kBumps;
  }
}

%inline %{


void 
set_net_voltage_cmd(odb::dbNet* net, Scene* corner, double voltage)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->setNetVoltage(net, corner, voltage);
}

void 
analyze_power_grid_cmd(odb::dbNet* net, Scene* corner, psm::GeneratedSourceType type, const char* error_file, bool reuse_solution, bool enable_em, const char* em_file, const char* voltage_file, const char* voltage_source_file)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->analyzePowerGrid(net, corner, type, voltage_file, reuse_solution, enable_em, em_file, error_file, voltage_source_file);
}

void
add_decap_master(odb::dbMaster *master, float cap)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->addDecapMaster(master, cap);
}

void
insert_decap_cmd(const float target, const char* net_name)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->insertDecapCells(target, net_name);
}

bool
check_connectivity_cmd(odb::dbNet* net, bool floorplanning, const char* error_file, bool dont_require_bterm)
{
  PDNSim* pdnsim = getPDNSim();
  return pdnsim->checkConnectivity(net, floorplanning, error_file, !dont_require_bterm);
}

void
write_spice_file_cmd(odb::dbNet* net, Scene* corner, psm::GeneratedSourceType type, const char* file, const char* voltage_source_file)
{
  PDNSim* pdnsim = getPDNSim();
  return pdnsim->writeSpiceNetwork(net, corner, type, file, voltage_source_file);
}

void set_debug_gui(bool enable)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->setDebugGui(enable);
}

void clear_solvers()
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->clearSolvers();
}

void set_source_settings(int bump_dx, int bump_dy, int bump_size, int bump_interval, int track_pitch, float resistance)
{
  PDNSim::GeneratedSourceSettings settings;
  settings.bump_dx = bump_dx;
  settings.bump_dy = bump_dy;
  settings.bump_size = bump_size;
  settings.bump_interval = bump_interval;
  settings.strap_track_pitch = track_pitch;
  settings.resistance = resistance;

  PDNSim* pdnsim = getPDNSim();
  pdnsim->setGeneratedSourceSettings(settings);
}

void
set_inst_power(odb::dbInst* inst, Scene* corner, float power)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->setInstPower(inst, corner, power);
}

bool
analyze_thermal_cmd(const char* hotspot_exe,
                    const char* hotspot_config,
                    const char* grid_file,
                    const char* work_dir,
                    int grid_rows,
                    int grid_cols,
                    double ambient_c,
                    Scene* corner,
                    int max_instances,
                    const char* report_file)
{
  psm::ThermalSettings settings;
  settings.hotspot_exe = hotspot_exe;
  settings.hotspot_config = hotspot_config;
  settings.grid_file = grid_file;
  settings.work_dir = work_dir;
  settings.grid_rows = grid_rows;
  settings.grid_cols = grid_cols;
  settings.ambient_c = ambient_c;
  settings.corner = corner;
  settings.max_instances = max_instances;
  settings.report_file = report_file;

  PDNSim* pdnsim = getPDNSim();
  return pdnsim->analyzeThermal(settings);
}

void
set_thermal_color_range_cmd(double min_c, double max_c)
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->setThermalColorRange(std::make_pair(min_c, max_c));
}

void
clear_thermal_color_range_cmd()
{
  PDNSim* pdnsim = getPDNSim();
  pdnsim->setThermalColorRange(std::nullopt);
}

%} // inline
