// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <map>
#include <string>
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

// Temperature dependence of leakage, fitted from liberty libraries that were
// characterized at several temperatures at the same process and voltage.
// No coefficient may be invented: when no fit exists for a cell the model
// returns the nominal (STA) leakage unchanged and reports it.
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

  // Family-level fallback fit (median beta over all fitted cells).
  double familyBetaPerC() const { return family_beta_per_c_; }
  const std::vector<LibraryCorner>& corners() const { return corners_; }

  std::string report() const;

 private:
  utl::Logger* logger_;
  std::map<std::string, LeakageFit> fits_;
  std::vector<LibraryCorner> corners_;
  double family_beta_per_c_ = 0.0;
};

// Delay derate multiplier as a function of local temperature and supply
// voltage: derate = f_T(T) * f_V(V), with f_T(T_nom) = f_V(V_nom) = 1.
// Both curves are fitted from the same multi-corner libraries (cell delay
// tables at each corner) and stored as piecewise-linear tables.
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
};

}  // namespace thm
