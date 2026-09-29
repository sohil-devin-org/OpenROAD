// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/LeakageModel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "boost/json.hpp"
#include "db_sta/dbSta.hh"
#include "utl/Logger.h"

namespace thm {

namespace json = boost::json;

LeakageModel::LeakageModel(utl::Logger* logger) : logger_(logger)
{
}

int LeakageModel::fitFromLibraries(const std::vector<LibraryCorner>& corners,
                                   sta::dbSta* sta)
{
  // Library characterization (per-cell leakage(T) fit from multi-corner
  // liberty files) is provided by the library-fitting component.  Until it
  // is available no fits exist and leakage stays at its STA value.
  corners_ = corners;
  logger_->warn(utl::THM,
                80,
                "Library leakage characterization is not available; "
                "temperature-dependent leakage scaling is disabled.");
  return 0;
}

bool LeakageModel::loadFits(const std::string& path)
{
  std::ifstream in(path);
  if (!in) {
    logger_->warn(utl::THM, 81, "Cannot open leakage fit file {}.", path);
    return false;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  json::value root = json::parse(buffer.str());
  const json::object& ro = root.as_object();
  fits_.clear();
  if (auto it = ro.find("family_beta_per_c"); it != ro.end()) {
    family_beta_per_c_ = it->value().to_number<double>();
  }
  if (auto it = ro.find("cells"); it != ro.end()) {
    for (const json::value& cv : it->value().as_array()) {
      const json::object& co = cv.as_object();
      LeakageFit fit;
      fit.cell_name = std::string(co.at("name").as_string());
      fit.t_ref_c = co.at("t_ref_c").to_number<double>();
      fit.leakage_ref_w = co.at("leakage_ref_w").to_number<double>();
      fit.beta_per_c = co.at("beta_per_c").to_number<double>();
      if (auto e = co.find("rel_rms_error"); e != co.end()) {
        fit.rel_rms_error = e->value().to_number<double>();
      }
      if (auto n = co.find("num_points"); n != co.end()) {
        fit.num_points = static_cast<int>(n->value().to_number<double>());
      }
      fits_[fit.cell_name] = fit;
    }
  }
  logger_->info(
      utl::THM, 82, "Loaded {} leakage fits from {}.", fits_.size(), path);
  return true;
}

void LeakageModel::writeFits(const std::string& path) const
{
  json::object root;
  root["family_beta_per_c"] = family_beta_per_c_;
  json::array cells;
  for (const auto& [name, fit] : fits_) {
    json::object co;
    co["name"] = name;
    co["t_ref_c"] = fit.t_ref_c;
    co["leakage_ref_w"] = fit.leakage_ref_w;
    co["beta_per_c"] = fit.beta_per_c;
    co["rel_rms_error"] = fit.rel_rms_error;
    co["num_points"] = fit.num_points;
    cells.push_back(co);
  }
  root["cells"] = cells;
  std::ofstream out(path);
  out << json::serialize(root);
}

bool LeakageModel::hasFit(const std::string& cell_name) const
{
  return fits_.count(cell_name) > 0;
}

const LeakageFit* LeakageModel::fit(const std::string& cell_name) const
{
  auto it = fits_.find(cell_name);
  return it == fits_.end() ? nullptr : &it->second;
}

double LeakageModel::leakageScale(const std::string& cell_name,
                                  double temp_c) const
{
  const LeakageFit* f = fit(cell_name);
  if (f != nullptr) {
    return std::exp(f->beta_per_c * (temp_c - f->t_ref_c));
  }
  if (family_beta_per_c_ != 0.0) {
    const double t_ref
        = corners_.empty() ? 25.0 : corners_.front().temperature_c;
    return std::exp(family_beta_per_c_ * (temp_c - t_ref));
  }
  return 1.0;
}

double LeakageModel::leakageAt(const std::string& cell_name,
                               double nominal_leakage_w,
                               double nominal_temp_c,
                               double temp_c) const
{
  const LeakageFit* f = fit(cell_name);
  const double beta = f != nullptr ? f->beta_per_c : family_beta_per_c_;
  return nominal_leakage_w * std::exp(beta * (temp_c - nominal_temp_c));
}

std::string LeakageModel::report() const
{
  std::ostringstream out;
  out << "Leakage model: " << fits_.size() << " cell fits, family beta "
      << family_beta_per_c_ << " /C, " << corners_.size() << " corners";
  return out.str();
}

DerateModel::DerateModel(utl::Logger* logger) : logger_(logger)
{
}

int DerateModel::fitFromLibraries(const std::vector<LibraryCorner>& corners,
                                  sta::dbSta* sta)
{
  logger_->warn(utl::THM,
                83,
                "Library delay characterization is not available; "
                "temperature/voltage derates are 1.0.");
  return 0;
}

static std::vector<std::pair<double, double>> readPoints(const json::object& o,
                                                         const char* key)
{
  std::vector<std::pair<double, double>> points;
  auto it = o.find(key);
  if (it == o.end()) {
    return points;
  }
  for (const json::value& pv : it->value().as_array()) {
    const json::array& pa = pv.as_array();
    points.emplace_back(pa[0].to_number<double>(), pa[1].to_number<double>());
  }
  std::sort(points.begin(), points.end());
  return points;
}

bool DerateModel::loadFits(const std::string& path)
{
  std::ifstream in(path);
  if (!in) {
    logger_->warn(utl::THM, 84, "Cannot open derate fit file {}.", path);
    return false;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  json::value root = json::parse(buffer.str());
  const json::object& ro = root.as_object();
  if (auto it = ro.find("t_nom_c"); it != ro.end()) {
    t_nom_c_ = it->value().to_number<double>();
  }
  if (auto it = ro.find("v_nom_v"); it != ro.end()) {
    v_nom_v_ = it->value().to_number<double>();
  }
  temp_points_ = readPoints(ro, "temperature_factor");
  volt_points_ = readPoints(ro, "voltage_factor");
  cell_temp_sens_per_c_.clear();
  cell_volt_sens_per_v_.clear();
  if (auto it = ro.find("cells"); it != ro.end()) {
    for (const json::value& cv : it->value().as_array()) {
      const json::object& co = cv.as_object();
      const std::string name(co.at("name").as_string());
      if (auto t = co.find("temp_sens_per_c"); t != co.end()) {
        cell_temp_sens_per_c_[name] = t->value().to_number<double>();
      }
      if (auto v = co.find("volt_sens_per_v"); v != co.end()) {
        cell_volt_sens_per_v_[name] = v->value().to_number<double>();
      }
    }
  }
  logger_->info(utl::THM,
                85,
                "Loaded derate fits from {} ({} temperature points, {} "
                "voltage points, {} cells).",
                path,
                temp_points_.size(),
                volt_points_.size(),
                cell_temp_sens_per_c_.size());
  return true;
}

void DerateModel::writeFits(const std::string& path) const
{
  json::object root;
  root["t_nom_c"] = t_nom_c_;
  root["v_nom_v"] = v_nom_v_;
  json::array tp;
  for (const auto& [t, f] : temp_points_) {
    tp.push_back(json::array{t, f});
  }
  root["temperature_factor"] = tp;
  json::array vp;
  for (const auto& [v, f] : volt_points_) {
    vp.push_back(json::array{v, f});
  }
  root["voltage_factor"] = vp;
  json::array cells;
  for (const auto& [name, sens] : cell_temp_sens_per_c_) {
    json::object co;
    co["name"] = name;
    co["temp_sens_per_c"] = sens;
    if (auto it = cell_volt_sens_per_v_.find(name);
        it != cell_volt_sens_per_v_.end()) {
      co["volt_sens_per_v"] = it->second;
    }
    cells.push_back(co);
  }
  root["cells"] = cells;
  std::ofstream out(path);
  out << json::serialize(root);
}

void DerateModel::setNominal(double t_nom_c, double v_nom_v)
{
  t_nom_c_ = t_nom_c;
  v_nom_v_ = v_nom_v;
}

static double interpolate(const std::vector<std::pair<double, double>>& pts,
                          double x)
{
  if (pts.empty()) {
    return 1.0;
  }
  if (x <= pts.front().first) {
    return pts.front().second;
  }
  if (x >= pts.back().first) {
    return pts.back().second;
  }
  for (size_t i = 1; i < pts.size(); ++i) {
    if (x <= pts[i].first) {
      const double x0 = pts[i - 1].first;
      const double x1 = pts[i].first;
      const double y0 = pts[i - 1].second;
      const double y1 = pts[i].second;
      return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
    }
  }
  return pts.back().second;
}

double DerateModel::temperatureFactor(const std::string& cell_name,
                                      double temp_c) const
{
  auto it = cell_temp_sens_per_c_.find(cell_name);
  if (it != cell_temp_sens_per_c_.end()) {
    return std::max(0.0, 1.0 + it->second * (temp_c - t_nom_c_));
  }
  return interpolate(temp_points_, temp_c);
}

double DerateModel::voltageFactor(const std::string& cell_name,
                                  double vdd_v) const
{
  auto it = cell_volt_sens_per_v_.find(cell_name);
  if (it != cell_volt_sens_per_v_.end()) {
    return std::max(0.0, 1.0 + it->second * (vdd_v - v_nom_v_));
  }
  return interpolate(volt_points_, vdd_v);
}

double DerateModel::derate(const std::string& cell_name,
                           double temp_c,
                           double vdd_v) const
{
  return temperatureFactor(cell_name, temp_c) * voltageFactor(cell_name, vdd_v);
}

std::string DerateModel::report() const
{
  std::ostringstream out;
  out << "Derate model: nominal " << t_nom_c_ << " C / " << v_nom_v_ << " V, "
      << temp_points_.size() << " temperature points, " << volt_points_.size()
      << " voltage points, " << cell_temp_sens_per_c_.size() << " cell fits";
  return out.str();
}

}  // namespace thm
