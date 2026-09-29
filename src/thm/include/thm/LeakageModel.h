// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sta {
class LibertyCell;
class LibertyLibrary;
class dbSta;
}  // namespace sta

namespace utl {
class Logger;
}

namespace thm {

// One characterized corner of a liberty library.
struct LibraryCorner
{
  std::string library_name;
  std::string process;  // "tt", "ss", "ff"
  double temperature_c = 25.0;
  double voltage_v = 1.8;
  std::string file;
};

// Per-cell leakage(T) fit: L(T) = L(T_ref) * exp(beta * (T - T_ref)).
struct LeakageFit
{
  std::string cell_name;
  double t_ref_c = 25.0;
  double leakage_ref_w = 0.0;
  double beta_per_c = 0.0;
  // Fit quality over the characterized points (relative RMS error).
  double rel_rms_error = 0.0;
  int num_points = 0;
};

// Origin of the coefficients a model is currently using.
enum class FitMode
{
  kNone,     // no characterization: coefficients are zero (no scaling)
  kFitted,   // fitted from liberty corners that isolate T (or V)
  kDefault,  // documented literature default (ThermalConfig / -set key)
};

const char* fitModeName(FitMode mode);

// Least-squares fit of ln(L) = ln(L_ref) + beta * (T - T_ref) over
// (T in C, L in W) samples; points with L <= 0 are ignored.  T_ref is the
// sample temperature closest to t_ref_hint_c.  Returns false with fewer than
// two distinct temperatures.
bool fitLeakageExponential(const std::vector<std::pair<double, double>>& t_l,
                           double t_ref_hint_c,
                           LeakageFit& fit);

// Build LibraryCorner records for the liberty libraries currently loaded in
// STA.  With corner_names empty every scene (define_corners / define_scene
// name) is used; otherwise only the named ones.  The process tag is derived
// from the library / operating-condition name (ff/tt/ss tokens) and from the
// liberty nom_process value.
std::vector<LibraryCorner> collectLibraryCorners(
    sta::dbSta* sta,
    const std::vector<std::string>& corner_names,
    utl::Logger* logger);

// Temperature dependence of leakage, fitted from liberty libraries that were
// characterized at several temperatures at the same process and voltage.
// No coefficient may be invented: when the loaded corners do not isolate
// temperature the model falls back to the documented default beta from
// ThermalConfig::leakage_beta_per_c (overridable through
// set_thermal_config -set leakage_beta_per_c) and reports that mode.
class LeakageModel
{
 public:
  explicit LeakageModel(utl::Logger* logger);

  // Fit from a set of corners (all same process & voltage, >= 2 temps).
  // Returns the number of cells fitted.
  int fitFromLibraries(const std::vector<LibraryCorner>& corners,
                       sta::dbSta* sta);
  // Load / store fits as JSON (data/<platform>_leakage_fits.json).
  bool loadFits(const std::string& path);
  void writeFits(const std::string& path) const;

  bool hasFit(const std::string& cell_name) const;
  const LeakageFit* fit(const std::string& cell_name) const;
  int numFits() const { return fits_.size(); }

  // Scale factor L(T)/L(T_ref) for the cell (1.0 when unknown).
  double leakageScale(const std::string& cell_name, double temp_c) const;
  // Absolute leakage in W given the nominal leakage reported by STA at
  // reference temperature nominal_temp_c.
  double leakageAt(const std::string& cell_name,
                   double nominal_leakage_w,
                   double nominal_temp_c,
                   double temp_c) const;

  // Family-level fallback fit (median beta over all fitted cells, or the
  // default beta in FitMode::kDefault).
  double familyBetaPerC() const { return family_beta_per_c_; }
  double familyTRefC() const { return family_t_ref_c_; }
  const std::vector<LibraryCorner>& corners() const { return corners_; }
  FitMode mode() const { return mode_; }
  // Temperature range covered by the fitted corners (0/0 when none).
  double temperatureMinC() const { return t_min_c_; }
  double temperatureMaxC() const { return t_max_c_; }

  // Documented default used when no fit exists (ThermalConfig
  // leakage_beta_per_c).  Setting it while no fit exists activates
  // FitMode::kDefault.
  void setDefaultBetaPerC(double beta_per_c);
  double defaultBetaPerC() const { return default_beta_per_c_; }

  std::string report() const;

 private:
  void useDefault();

  utl::Logger* logger_;
  std::map<std::string, LeakageFit> fits_;
  std::vector<LibraryCorner> corners_;
  double family_beta_per_c_ = 0.0;
  double family_t_ref_c_ = 25.0;
  double default_beta_per_c_;
  FitMode mode_ = FitMode::kNone;
  double t_min_c_ = 0.0;
  double t_max_c_ = 0.0;
};

// Delay derate multiplier as a function of local temperature and supply
// voltage: derate = f_T(T) * f_V(V), with f_T(T_nom) = f_V(V_nom) = 1.
// Both curves are fitted from the same multi-corner libraries (cell delay
// tables at each corner) and stored as piecewise-linear tables plus
// per-cell linear sensitivities.  Without corners that isolate T (or V) the
// documented defaults ThermalConfig::delay_tempco_per_c /
// delay_vcoef_per_v are used as linear coefficients around the nominal
// point (FitMode::kDefault).
class DerateModel
{
 public:
  explicit DerateModel(utl::Logger* logger);

  int fitFromLibraries(const std::vector<LibraryCorner>& corners,
                       sta::dbSta* sta);
  bool loadFits(const std::string& path);
  void writeFits(const std::string& path) const;

  double nominalTempC() const { return t_nom_c_; }
  double nominalVddV() const { return v_nom_v_; }
  void setNominal(double t_nom_c, double v_nom_v);

  // Per-cell derate when a cell-level fit exists, family derate otherwise.
  double derate(const std::string& cell_name,
                double temp_c,
                double vdd_v) const;
  double temperatureFactor(const std::string& cell_name, double temp_c) const;
  double voltageFactor(const std::string& cell_name, double vdd_v) const;

  bool hasTemperatureFit() const { return !temp_points_.empty(); }
  bool hasVoltageFit() const { return !volt_points_.empty(); }
  int numCellFits() const { return cell_temp_sens_per_c_.size(); }
  FitMode temperatureMode() const { return temp_mode_; }
  FitMode voltageMode() const { return volt_mode_; }
  double temperatureMinC() const { return t_min_c_; }
  double temperatureMaxC() const { return t_max_c_; }

  // Documented defaults (ThermalConfig delay_tempco_per_c /
  // delay_vcoef_per_v); setting one while the corresponding fit is missing
  // activates FitMode::kDefault for that axis.
  void setDefaultTempcoPerC(double tempco_per_c);
  void setDefaultVcoefPerV(double vcoef_per_v);
  double defaultTempcoPerC() const { return default_tempco_per_c_; }
  double defaultVcoefPerV() const { return default_vcoef_per_v_; }

  std::string report() const;

 private:
  utl::Logger* logger_;
  double t_nom_c_ = 25.0;
  double v_nom_v_ = 1.8;
  // Family-level piecewise-linear factor tables: (T, factor), (V, factor).
  std::vector<std::pair<double, double>> temp_points_;
  std::vector<std::pair<double, double>> volt_points_;
  // Optional per-cell sensitivities: d(delay)/dT relative per C and
  // d(delay)/dV relative per V.
  std::map<std::string, double> cell_temp_sens_per_c_;
  std::map<std::string, double> cell_volt_sens_per_v_;
  double default_tempco_per_c_;
  double default_vcoef_per_v_;
  FitMode temp_mode_ = FitMode::kNone;
  FitMode volt_mode_ = FitMode::kNone;
  double t_min_c_ = 0.0;
  double t_max_c_ = 0.0;
};

}  // namespace thm
