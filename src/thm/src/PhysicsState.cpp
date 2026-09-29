// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/PhysicsState.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "boost/json.hpp"

namespace thm {

namespace json = boost::json;

double PhysicsMetrics::peakTemp() const
{
  if (peak_temp_c.empty()) {
    return 0.0;
  }
  return *std::max_element(peak_temp_c.begin(), peak_temp_c.end());
}

const MapSnapshot* PhysicsSnapshot::findMap(const std::string& name,
                                            int die) const
{
  for (const MapSnapshot& map : maps) {
    if (map.name == name && map.die == die) {
      return &map;
    }
  }
  return nullptr;
}

void PhysicsHistory::add(PhysicsSnapshot snapshot)
{
  snapshots_.push_back(std::move(snapshot));
}

static json::array toArray(const std::vector<double>& values)
{
  json::array arr;
  arr.reserve(values.size());
  for (double v : values) {
    arr.push_back(v);
  }
  return arr;
}

static std::vector<double> fromArray(const json::value& value)
{
  std::vector<double> out;
  if (!value.is_array()) {
    return out;
  }
  for (const json::value& v : value.as_array()) {
    out.push_back(v.to_number<double>());
  }
  return out;
}

static json::object metricsToJson(const PhysicsMetrics& m)
{
  json::object o;
  o["iteration"] = m.iteration;
  o["label"] = m.label;
  o["peak_temp_c"] = toArray(m.peak_temp_c);
  o["avg_temp_c"] = toArray(m.avg_temp_c);
  o["max_gradient_c_per_mm"] = m.max_gradient_c_per_mm;
  o["worst_ir_drop_v"] = m.worst_ir_drop_v;
  o["wns_nominal_s"] = m.wns_nominal_s;
  o["tns_nominal_s"] = m.tns_nominal_s;
  o["wns_derated_s"] = m.wns_derated_s;
  o["tns_derated_s"] = m.tns_derated_s;
  o["total_power_w"] = m.total_power_w;
  o["leakage_power_w"] = m.leakage_power_w;
  o["hpwl_um"] = m.hpwl_um;
  o["overflow"] = m.overflow;
  o["electrothermal_iterations"] = m.electrothermal_iterations;
  o["converged"] = m.converged;
  o["runaway"] = m.runaway;
  o["em_lifetime_factor"] = m.em_lifetime_factor;
  o["clock_skew_delta_s"] = m.clock_skew_delta_s;
  o["physics_weight"] = m.physics_weight;
  o["runtime_s"] = m.runtime_s;
  return o;
}

static double numberOr(const json::object& o, const char* key, double def = 0)
{
  auto it = o.find(key);
  if (it == o.end() || !it->value().is_number()) {
    return def;
  }
  return it->value().to_number<double>();
}

static bool boolOr(const json::object& o, const char* key, bool def)
{
  auto it = o.find(key);
  if (it == o.end() || !it->value().is_bool()) {
    return def;
  }
  return it->value().as_bool();
}

static std::string stringOr(const json::object& o, const char* key)
{
  auto it = o.find(key);
  if (it == o.end() || !it->value().is_string()) {
    return "";
  }
  return std::string(it->value().as_string());
}

static PhysicsMetrics metricsFromJson(const json::object& o)
{
  PhysicsMetrics m;
  m.iteration = static_cast<int>(numberOr(o, "iteration"));
  m.label = stringOr(o, "label");
  if (auto it = o.find("peak_temp_c"); it != o.end()) {
    m.peak_temp_c = fromArray(it->value());
  }
  if (auto it = o.find("avg_temp_c"); it != o.end()) {
    m.avg_temp_c = fromArray(it->value());
  }
  m.max_gradient_c_per_mm = numberOr(o, "max_gradient_c_per_mm");
  m.worst_ir_drop_v = numberOr(o, "worst_ir_drop_v");
  m.wns_nominal_s = numberOr(o, "wns_nominal_s");
  m.tns_nominal_s = numberOr(o, "tns_nominal_s");
  m.wns_derated_s = numberOr(o, "wns_derated_s");
  m.tns_derated_s = numberOr(o, "tns_derated_s");
  m.total_power_w = numberOr(o, "total_power_w");
  m.leakage_power_w = numberOr(o, "leakage_power_w");
  m.hpwl_um = numberOr(o, "hpwl_um");
  m.overflow = numberOr(o, "overflow");
  m.electrothermal_iterations
      = static_cast<int>(numberOr(o, "electrothermal_iterations"));
  m.converged = boolOr(o, "converged", true);
  m.runaway = boolOr(o, "runaway", false);
  m.em_lifetime_factor = numberOr(o, "em_lifetime_factor", 1.0);
  m.clock_skew_delta_s = numberOr(o, "clock_skew_delta_s");
  m.physics_weight = numberOr(o, "physics_weight");
  m.runtime_s = numberOr(o, "runtime_s");
  return m;
}

std::string PhysicsHistory::toJson() const
{
  json::object root;
  root["design"] = design_name_;
  root["tag"] = tag_;
  json::array snaps;
  for (const PhysicsSnapshot& s : snapshots_) {
    json::object so;
    so["tag"] = s.tag;
    so["time_s"] = s.time_s;
    so["die_bbox"]
        = json::array{s.die_xmin, s.die_ymin, s.die_xmax, s.die_ymax};
    so["dbu_per_micron"] = s.dbu_per_micron;
    so["metrics"] = metricsToJson(s.metrics);
    json::array maps;
    for (const MapSnapshot& m : s.maps) {
      json::object mo;
      mo["name"] = m.name;
      mo["units"] = m.units;
      mo["die"] = m.die;
      mo["nx"] = m.nx;
      mo["ny"] = m.ny;
      mo["values"] = toArray(m.values);
      maps.push_back(mo);
    }
    so["maps"] = maps;
    json::array positions;
    for (const InstancePosition& p : s.positions) {
      positions.push_back(json::array{p.name, p.x_dbu, p.y_dbu});
    }
    so["positions"] = positions;
    snaps.push_back(so);
  }
  root["snapshots"] = snaps;
  return json::serialize(root);
}

void PhysicsHistory::fromJson(const std::string& text)
{
  json::value root = json::parse(text);
  const json::object& ro = root.as_object();
  design_name_ = stringOr(ro, "design");
  tag_ = stringOr(ro, "tag");
  snapshots_.clear();
  auto snaps_it = ro.find("snapshots");
  if (snaps_it == ro.end()) {
    return;
  }
  for (const json::value& sv : snaps_it->value().as_array()) {
    const json::object& so = sv.as_object();
    PhysicsSnapshot s;
    s.tag = stringOr(so, "tag");
    s.time_s = numberOr(so, "time_s");
    if (auto it = so.find("die_bbox"); it != so.end()) {
      const auto bbox = fromArray(it->value());
      if (bbox.size() == 4) {
        s.die_xmin = static_cast<int>(bbox[0]);
        s.die_ymin = static_cast<int>(bbox[1]);
        s.die_xmax = static_cast<int>(bbox[2]);
        s.die_ymax = static_cast<int>(bbox[3]);
      }
    }
    s.dbu_per_micron = numberOr(so, "dbu_per_micron", 1.0);
    if (auto it = so.find("metrics"); it != so.end()) {
      s.metrics = metricsFromJson(it->value().as_object());
    }
    if (auto it = so.find("maps"); it != so.end()) {
      for (const json::value& mv : it->value().as_array()) {
        const json::object& mo = mv.as_object();
        MapSnapshot m;
        m.name = stringOr(mo, "name");
        m.units = stringOr(mo, "units");
        m.die = static_cast<int>(numberOr(mo, "die"));
        m.nx = static_cast<int>(numberOr(mo, "nx"));
        m.ny = static_cast<int>(numberOr(mo, "ny"));
        if (auto vit = mo.find("values"); vit != mo.end()) {
          m.values = fromArray(vit->value());
        }
        s.maps.push_back(std::move(m));
      }
    }
    if (auto it = so.find("positions"); it != so.end()) {
      for (const json::value& pv : it->value().as_array()) {
        const json::array& pa = pv.as_array();
        if (pa.size() == 3) {
          InstancePosition p;
          p.name = std::string(pa[0].as_string());
          p.x_dbu = static_cast<int>(pa[1].to_number<double>());
          p.y_dbu = static_cast<int>(pa[2].to_number<double>());
          s.positions.push_back(std::move(p));
        }
      }
    }
    snapshots_.push_back(std::move(s));
  }
}

void PhysicsHistory::writeJson(const std::string& path) const
{
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("cannot open " + path + " for writing");
  }
  out << toJson();
}

void PhysicsHistory::readJson(const std::string& path)
{
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open " + path + " for reading");
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  fromJson(buffer.str());
}

}  // namespace thm
