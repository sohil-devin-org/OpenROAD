// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

%{
#include "ord/OpenRoad.hh"
#include "thm/PhysicsCoupling.h"
#include "thm/Thermal.h"
#include "sta/Scene.hh"
#include "db_sta/dbSta.hh"
#include "utl/Logger.h"
#include <sstream>

static thm::Thermal* getThermal()
{
  return ord::OpenRoad::openRoad()->getThermal();
}
%}

%include "../../Exception.i"

%inline %{

namespace thm {

void set_config_value_cmd(const char* key, const char* value)
{
  getThermal()->setConfigValue(key, value);
}

void read_config_file_cmd(const char* file)
{
  getThermal()->readConfigFile(file);
}

void add_activity_phase_cmd(const char* name, double scale, const char* file,
                            double duration)
{
  ActivityPhase phase;
  phase.name = name;
  phase.activity_scale = scale;
  phase.activity_file = file;
  phase.duration_s = duration;
  getThermal()->addActivityPhase(phase);
}

void clear_activity_phases_cmd()
{
  getThermal()->clearActivityPhases();
}

void report_config_cmd()
{
  getThermal()->logger()->report("{}", getThermal()->configReport());
}

void load_library_fits_cmd(const char* leakage_json, const char* derate_json)
{
  getThermal()->loadLibraryFits(leakage_json, derate_json);
}

void analyze_thermal_cmd(bool transient, const char* phases, int die,
                         const char* label, bool include_ir_drop,
                         bool include_timing, const char* corner_name)
{
  AnalyzeOptions options;
  options.transient = transient;
  std::string phase_list(phases);
  size_t start = 0;
  while (start < phase_list.size()) {
    size_t end = phase_list.find(' ', start);
    if (end == std::string::npos) {
      end = phase_list.size();
    }
    if (end > start) {
      options.phases.push_back(phase_list.substr(start, end - start));
    }
    start = end + 1;
  }
  options.die = die;
  options.label = label;
  options.include_ir_drop = include_ir_drop;
  options.include_timing = include_timing;
  if (corner_name != nullptr && corner_name[0] != '\0') {
    sta::dbSta* sta = ord::OpenRoad::openRoad()->getSta();
    options.corner = sta->findScene(corner_name);
    if (options.corner == nullptr) {
      getThermal()->logger()->error(utl::THM, 50, "Unknown corner '{}'.",
                                    corner_name);
    }
  }
  getThermal()->analyze(options);
}

void report_physics_cmd(const char* json_file)
{
  getThermal()->reportPhysics(json_file);
}

void set_physics_derating_cmd(bool enable)
{
  getThermal()->setDeratingEnabled(enable);
}

bool physics_derating_enabled()
{
  return getThermal()->deratingEnabled();
}

void write_thermal_map_cmd(const char* file, const char* map, int die)
{
  getThermal()->writeThermalMap(file, map, die);
}

void write_physics_history_cmd(const char* file)
{
  getThermal()->writeHistory(file);
}

void load_physics_reference_cmd(const char* file)
{
  getThermal()->loadReference(file);
}

void clear_physics_reference_cmd()
{
  getThermal()->clearReference();
}

void write_physics_animation_cmd(const char* type, const char* format,
                                 const char* file, double scale_min,
                                 double scale_max, int fps, int die)
{
  AnimationOptions options;
  const std::string t(type);
  if (t == "cooldown") {
    options.type = AnimationType::kCooldown;
  } else if (t == "transient") {
    options.type = AnimationType::kTransient;
  } else if (t == "electrothermal") {
    options.type = AnimationType::kElectrothermal;
  } else if (t == "compare") {
    options.type = AnimationType::kCompare;
  } else if (t == "stack") {
    options.type = AnimationType::kStack;
  } else {
    getThermal()->logger()->error(utl::THM, 51, "Unknown animation type '{}'.",
                                  type);
  }
  options.format = format;
  options.file = file;
  options.scale_min_c = scale_min;
  options.scale_max_c = scale_max;
  options.fps = fps;
  options.die = die;
  getThermal()->writeAnimation(options);
}

void reset_thermal_cmd()
{
  getThermal()->reset();
}

double get_peak_temperature()
{
  return getThermal()->lastMetrics().peakTemp();
}

double get_total_power()
{
  return getThermal()->lastMetrics().total_power_w;
}

double get_worst_ir_drop()
{
  return getThermal()->lastMetrics().worst_ir_drop_v;
}

double get_derated_wns()
{
  return getThermal()->lastMetrics().wns_derated_s;
}

double get_nominal_wns()
{
  return getThermal()->lastMetrics().wns_nominal_s;
}

int get_history_size()
{
  return getThermal()->history().size();
}

bool thermal_converged()
{
  return getThermal()->lastMetrics().converged;
}

bool thermal_runaway()
{
  return getThermal()->lastMetrics().runaway;
}

double get_screening_length_um()
{
  odb::dbBlock* block = getThermal()->db()->getChip()->getBlock();
  return getThermal()->screeningLengthDbu() / block->getDbUnitsPerMicron();
}

double get_em_lifetime_factor()
{
  return getThermal()->lastMetrics().em_lifetime_factor;
}

double get_derated_tns()
{
  return getThermal()->lastMetrics().tns_derated_s;
}

double get_nominal_tns()
{
  return getThermal()->lastMetrics().tns_nominal_s;
}

// OpenSTA design total power (what report_power prints as Total) for the
// command corner; used to check the per-instance extraction against it.
double get_sta_design_power()
{
  thm::Thermal* thermal = getThermal();
  thm::PowerExtractor extractor(thermal->sta(), thermal->logger());
  return extractor.designTotalPowerW();
}

////////////////////////////////////////////////////////////////
// Library characterization.

int characterize_libraries_cmd(const char* corner_names,
                               const char* leakage_json,
                               const char* derate_json)
{
  Thermal* thermal = getThermal();
  utl::Logger* logger = thermal->logger();
  std::vector<std::string> names;
  std::istringstream name_stream(corner_names);
  for (std::string name; name_stream >> name;) {
    names.push_back(name);
  }
  const std::vector<LibraryCorner> corners
      = collectLibraryCorners(thermal->sta(), names, logger);
  if (corners.empty()) {
    logger->error(utl::THM, 142,
                  "No liberty libraries are loaded for the requested corners.");
  }
  logger->report("Library corners");
  logger->report("  {:<32} {:<8} {:>10} {:>8}", "library", "process",
                 "temp (C)", "VDD (V)");
  for (const LibraryCorner& corner : corners) {
    logger->report("  {:<32} {:<8} {:>10g} {:>8g}", corner.library_name,
                   corner.process, corner.temperature_c, corner.voltage_v);
  }
  const int fitted = thermal->characterizeLibraries(corners);
  const LeakageModel& leakage = thermal->leakageModel();
  const DerateModel& derate = thermal->derateModel();
  logger->report("Library characterization");
  logger->report("  {:<28} {}", "leakage mode:", fitModeName(leakage.mode()));
  logger->report("  {:<28} {}", "leakage cells fitted:", leakage.numFits());
  logger->report("  {:<28} {:.6g} /C", "leakage family beta:",
                 leakage.familyBetaPerC());
  logger->report("  {:<28} {:g}..{:g} C", "leakage temperature range:",
                 leakage.temperatureMinC(), leakage.temperatureMaxC());
  logger->report("  {:<28} {}", "delay temperature mode:",
                 fitModeName(derate.temperatureMode()));
  logger->report("  {:<28} {}", "delay voltage mode:",
                 fitModeName(derate.voltageMode()));
  logger->report("  {:<28} {}", "delay cells fitted:", derate.numCellFits());
  logger->report("  {:<28} {:g}..{:g} C", "delay temperature range:",
                 derate.temperatureMinC(), derate.temperatureMaxC());
  logger->report("  {:<28} {:g} C / {:g} V", "derate nominal:",
                 derate.nominalTempC(), derate.nominalVddV());
  if (leakage_json != nullptr && leakage_json[0] != '\0') {
    leakage.writeFits(leakage_json);
  }
  if (derate_json != nullptr && derate_json[0] != '\0') {
    derate.writeFits(derate_json);
  }
  return fitted;
}

} // namespace thm

%} // inline
