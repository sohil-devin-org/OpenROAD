// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/LeakageModel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "boost/json.hpp"
#include "db_sta/dbSta.hh"
#include "sta/Iterator.hh"
#include "sta/LeakagePower.hh"
#include "sta/Liberty.hh"
#include "sta/MinMax.hh"
#include "sta/Network.hh"
#include "sta/Scene.hh"
#include "sta/TimingArc.hh"
#include "sta/TimingModel.hh"
#include "sta/TimingRole.hh"
#include "thm/ThermalConfig.h"
#include "utl/Logger.h"

namespace thm {

namespace json = boost::json;

namespace {

// Corners closer than this are the same temperature / voltage.
constexpr double kTempEpsC = 1e-3;
constexpr double kVoltEpsV = 1e-4;

using Samples = std::vector<std::pair<double, double>>;

struct ResolvedCorner
{
  LibraryCorner corner;
  sta::LibertyLibrary* lib = nullptr;
};

// Corners that share `process` and one of (voltage, temperature) and differ
// in the other one; they isolate the dependence on the varying quantity.
struct CornerGroup
{
  std::string process;
  double fixed_value = 0.0;  // the shared voltage (or temperature)
  std::vector<const ResolvedCorner*> corners;
  std::vector<double> distinct;  // sorted distinct varying values
};

std::string toLower(std::string s)
{
  for (char& c : s) {
    c = std::tolower(static_cast<unsigned char>(c));
  }
  return s;
}

std::string fmt(double v, int precision = 6)
{
  std::ostringstream out;
  out.precision(precision);
  out << v;
  return out.str();
}

std::string joinValues(const std::vector<double>& values)
{
  std::string out;
  for (double v : values) {
    if (!out.empty()) {
      out += ", ";
    }
    out += fmt(v);
  }
  return out;
}

std::string joinStrings(const std::set<std::string>& values)
{
  std::string out;
  for (const std::string& v : values) {
    if (!out.empty()) {
      out += ", ";
    }
    out += v;
  }
  return out;
}

// Process tag from the tokens of a liberty / operating-condition / file
// name: "ff", "tt", "ss" (or "sf"/"fs") when recognizable.
std::string processTagFromName(const std::string& name)
{
  static const std::map<std::string, std::string> kTokens = {
      {"ff", "ff"},
      {"fastfast", "ff"},
      {"fast", "ff"},
      {"tt", "tt"},
      {"typtyp", "tt"},
      {"typ", "tt"},
      {"typical", "tt"},
      {"nom", "tt"},
      {"nominal", "tt"},
      {"ss", "ss"},
      {"slowslow", "ss"},
      {"slow", "ss"},
      {"sf", "sf"},
      {"fs", "fs"},
  };
  const std::string lower = toLower(name);
  std::string token;
  auto flush = [&]() -> std::string {
    auto it = kTokens.find(token);
    token.clear();
    return it == kTokens.end() ? "" : it->second;
  };
  for (char c : lower) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      token += c;
    } else {
      const std::string tag = flush();
      if (!tag.empty()) {
        return tag;
      }
    }
  }
  return flush();
}

std::string processTag(const sta::LibertyLibrary* lib)
{
  std::string tag;
  if (const sta::OperatingConditions* op = lib->defaultOperatingConditions()) {
    tag = processTagFromName(op->name());
  }
  if (tag.empty()) {
    tag = processTagFromName(lib->name());
  }
  if (tag.empty()) {
    std::string base = lib->filename();
    const size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) {
      base = base.substr(slash + 1);
    }
    tag = processTagFromName(base);
  }
  const double process = lib->nominalProcess();
  if (tag.empty()) {
    return "p" + fmt(process, 4);
  }
  if (std::abs(process - 1.0) > 1e-6) {
    tag += "@p" + fmt(process, 4);
  }
  return tag;
}

sta::LibertyLibrary* findLibrary(sta::dbSta* sta, const LibraryCorner& corner)
{
  sta::Network* network = sta->network();
  std::unique_ptr<sta::LibertyLibraryIterator> iter(
      network->libertyLibraryIterator());
  sta::LibertyLibrary* by_file = nullptr;
  while (iter->hasNext()) {
    sta::LibertyLibrary* lib = iter->next();
    if (lib->name() == corner.library_name) {
      return lib;
    }
    if (!corner.file.empty() && lib->filename() == corner.file) {
      by_file = lib;
    }
  }
  return by_file;
}

std::vector<ResolvedCorner> resolveCorners(
    const std::vector<LibraryCorner>& corners,
    sta::dbSta* sta,
    utl::Logger* logger)
{
  std::vector<ResolvedCorner> resolved;
  if (sta == nullptr) {
    return resolved;
  }
  std::set<std::string> seen;
  for (const LibraryCorner& corner : corners) {
    if (!seen.insert(corner.library_name).second) {
      continue;
    }
    sta::LibertyLibrary* lib = findLibrary(sta, corner);
    if (lib == nullptr) {
      logger->warn(utl::THM,
                   130,
                   "Liberty library {} of corner ({}, {} C, {} V) is not "
                   "loaded; it is skipped.",
                   corner.library_name,
                   corner.process,
                   corner.temperature_c,
                   corner.voltage_v);
      continue;
    }
    resolved.push_back({corner, lib});
  }
  return resolved;
}

// Group corners by (process, fixed quantity) and keep the groups whose
// varying quantity takes at least two distinct values.
std::vector<CornerGroup> groupCorners(
    const std::vector<ResolvedCorner>& corners,
    bool vary_temperature)
{
  std::map<std::pair<std::string, std::string>, CornerGroup> groups;
  const double eps = vary_temperature ? kVoltEpsV : kTempEpsC;
  for (const ResolvedCorner& rc : corners) {
    const double fixed
        = vary_temperature ? rc.corner.voltage_v : rc.corner.temperature_c;
    const double varying
        = vary_temperature ? rc.corner.temperature_c : rc.corner.voltage_v;
    // Quantize the fixed value so that float noise does not split groups.
    const std::string fixed_key = fmt(std::round(fixed / eps) * eps, 10);
    CornerGroup& g = groups[{rc.corner.process, fixed_key}];
    g.process = rc.corner.process;
    g.fixed_value = fixed;
    g.corners.push_back(&rc);
    const double var_eps = vary_temperature ? kTempEpsC : kVoltEpsV;
    const bool known
        = std::any_of(g.distinct.begin(), g.distinct.end(), [&](double v) {
            return std::abs(v - varying) < var_eps;
          });
    if (!known) {
      g.distinct.push_back(varying);
    }
  }
  std::vector<CornerGroup> result;
  for (auto& [key, g] : groups) {
    if (g.distinct.size() >= 2) {
      std::sort(g.distinct.begin(), g.distinct.end());
      result.push_back(g);
    }
  }
  // Most distinct values first, then most corners; deterministic tie-break
  // on the key order of the map (process, fixed value).
  std::stable_sort(result.begin(),
                   result.end(),
                   [](const CornerGroup& a, const CornerGroup& b) {
                     if (a.distinct.size() != b.distinct.size()) {
                       return a.distinct.size() > b.distinct.size();
                     }
                     return a.corners.size() > b.corners.size();
                   });
  return result;
}

std::string describeCorners(const std::vector<ResolvedCorner>& corners)
{
  std::set<std::string> processes;
  std::vector<double> temps;
  std::vector<double> volts;
  for (const ResolvedCorner& rc : corners) {
    processes.insert(rc.corner.process);
    if (std::none_of(temps.begin(), temps.end(), [&](double t) {
          return std::abs(t - rc.corner.temperature_c) < kTempEpsC;
        })) {
      temps.push_back(rc.corner.temperature_c);
    }
    if (std::none_of(volts.begin(), volts.end(), [&](double v) {
          return std::abs(v - rc.corner.voltage_v) < kVoltEpsV;
        })) {
      volts.push_back(rc.corner.voltage_v);
    }
  }
  std::sort(temps.begin(), temps.end());
  std::sort(volts.begin(), volts.end());
  std::ostringstream out;
  out << corners.size() << " corners, " << processes.size() << " process tag"
      << (processes.size() == 1 ? "" : "s") << " (" << joinStrings(processes)
      << "), temperatures {" << joinValues(temps) << "} C, voltages {"
      << joinValues(volts) << "} V";
  return out.str();
}

// Cell leakage in W: the cell_leakage_power attribute, or the mean of the
// state-dependent leakage_power groups when only those exist.
bool cellLeakageW(const sta::LibertyCell* cell, double& leakage_w)
{
  float leakage;
  bool exists;
  cell->leakagePower(leakage, exists);
  if (exists && leakage > 0.0F) {
    leakage_w = leakage;
    return true;
  }
  double sum = 0.0;
  int count = 0;
  for (const sta::LeakagePower& lp : cell->leakagePowers()) {
    if (lp.power() > 0.0F) {
      sum += lp.power();
      ++count;
    }
  }
  if (count == 0) {
    return false;
  }
  leakage_w = sum / count;
  return true;
}

// Representative cell delay (s) at the corner of `lib`: the mean over the
// combinational / clock-to-Q arcs of the delay driving a fanout-of-4 load
// (4x the arc's input pin capacitance) with the self-consistent FO4 input
// slew, i.e. the FO4 delay metric of R. Ho, K. Mai, M. Horowitz, "The
// Future of Wires," Proc. IEEE 89(4), 2001.  Uses the liberty timing
// tables through the public GateTimingModel interface.
bool representativeDelayS(const sta::LibertyCell* cell,
                          const sta::LibertyLibrary* lib,
                          double& delay_s)
{
  const sta::Pvt* pvt = lib->defaultOperatingConditions();
  double sum = 0.0;
  int count = 0;
  for (const sta::TimingArcSet* arc_set : cell->timingArcSets()) {
    const sta::TimingRole* role = arc_set->role();
    if (role != sta::TimingRole::combinational()
        && role != sta::TimingRole::regClkToQ()) {
      continue;
    }
    const sta::LibertyPort* from = arc_set->from();
    const sta::LibertyPort* to = arc_set->to();
    if (from == nullptr || to == nullptr) {
      continue;
    }
    float cin = from->capacitance();
    if (cin <= 0.0F) {
      cin = lib->defaultInputPinCap();
    }
    if (cin <= 0.0F) {
      continue;
    }
    const float load = 4.0F * cin;
    for (const sta::TimingArc* arc : arc_set->arcs()) {
      const auto* model
          = dynamic_cast<const sta::GateTimingModel*>(arc->model());
      if (model == nullptr) {
        continue;
      }
      float delay0;
      float slew0;
      model->gateDelay(pvt, 0.0F, load, delay0, slew0);
      float delay;
      float slew;
      model->gateDelay(pvt, slew0, load, delay, slew);
      if (delay > 0.0F) {
        sum += delay;
        ++count;
      }
    }
  }
  if (count == 0) {
    return false;
  }
  delay_s = sum / count;
  return true;
}

// Samples of `value(cell, lib)` for every cell present in at least two
// distinct varying values of the group: cell -> [(varying, value)].
template <typename ValueFn>
std::map<std::string, Samples> collectSamples(const CornerGroup& group,
                                              bool vary_temperature,
                                              ValueFn value)
{
  std::map<std::string, Samples> samples;
  for (const ResolvedCorner* rc : group.corners) {
    const double varying
        = vary_temperature ? rc->corner.temperature_c : rc->corner.voltage_v;
    sta::LibertyCellIterator cells(rc->lib);
    while (cells.hasNext()) {
      const sta::LibertyCell* cell = cells.next();
      double v;
      if (value(cell, rc->lib, v)) {
        samples[cell->name()].emplace_back(varying, v);
      }
    }
  }
  const double eps = vary_temperature ? kTempEpsC : kVoltEpsV;
  for (auto it = samples.begin(); it != samples.end();) {
    Samples& s = it->second;
    std::sort(s.begin(), s.end());
    int distinct = 1;
    for (size_t i = 1; i < s.size(); ++i) {
      if (s[i].first - s[i - 1].first >= eps) {
        ++distinct;
      }
    }
    if (distinct < 2) {
      it = samples.erase(it);
    } else {
      ++it;
    }
  }
  return samples;
}

double median(std::vector<double> values)
{
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  return n % 2 == 1 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

double closest(const std::vector<double>& values, double target)
{
  double best = values.front();
  for (double v : values) {
    if (std::abs(v - target) < std::abs(best - target)) {
      best = v;
    }
  }
  return best;
}

// Nominal (T, V) of the library STA times with: the first liberty of the
// command scene, else the first loaded library.
bool staNominal(sta::dbSta* sta, double& temp_c, double& vdd_v)
{
  if (sta == nullptr) {
    return false;
  }
  const sta::LibertyLibrary* lib = nullptr;
  if (const sta::Scene* scene = sta->cmdScene()) {
    const sta::LibertySeq& libs = scene->libertyLibraries(sta::MinMax::max());
    if (!libs.empty()) {
      lib = libs.front();
    }
  }
  if (lib == nullptr) {
    std::unique_ptr<sta::LibertyLibraryIterator> iter(
        sta->network()->libertyLibraryIterator());
    if (iter->hasNext()) {
      lib = iter->next();
    }
  }
  if (lib == nullptr) {
    return false;
  }
  temp_c = lib->nominalTemperature();
  vdd_v = lib->nominalVoltage();
  return true;
}

// Relative sensitivity (fraction per unit of x) of the samples around the
// reference sample x_ref: least squares of (y/y_ref - 1) = s * (x - x_ref)
// through the origin, so that the factor is exactly 1 at x_ref.  Returns
// the per-x ratios y/y_ref as well.
bool relativeSensitivity(const Samples& samples,
                         double x_ref,
                         double eps,
                         double& sensitivity,
                         Samples& ratios)
{
  double y_ref = 0.0;
  int n_ref = 0;
  for (const auto& [x, y] : samples) {
    if (std::abs(x - x_ref) < eps) {
      y_ref += y;
      ++n_ref;
    }
  }
  if (n_ref == 0 || y_ref <= 0.0) {
    return false;
  }
  y_ref /= n_ref;
  double sxy = 0.0;
  double sxx = 0.0;
  ratios.clear();
  for (const auto& [x, y] : samples) {
    const double dx = x - x_ref;
    const double r = y / y_ref;
    ratios.emplace_back(x, r);
    sxy += dx * (r - 1.0);
    sxx += dx * dx;
  }
  if (sxx <= 0.0) {
    return false;
  }
  sensitivity = sxy / sxx;
  return true;
}

std::vector<std::pair<double, double>> readPoints(const json::object& o,
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

json::array writePoints(const std::vector<std::pair<double, double>>& points)
{
  json::array out;
  for (const auto& [x, y] : points) {
    out.push_back(json::array{x, y});
  }
  return out;
}

FitMode readMode(const json::object& o, const char* key)
{
  auto it = o.find(key);
  if (it == o.end()) {
    return FitMode::kNone;
  }
  const std::string_view name = it->value().as_string();
  if (name == fitModeName(FitMode::kFitted)) {
    return FitMode::kFitted;
  }
  if (name == fitModeName(FitMode::kDefault)) {
    return FitMode::kDefault;
  }
  return FitMode::kNone;
}

double readDouble(const json::object& o, const char* key, double fallback)
{
  auto it = o.find(key);
  return it == o.end() ? fallback : it->value().to_number<double>();
}

double interpolate(const std::vector<std::pair<double, double>>& pts, double x)
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

}  // namespace

const char* fitModeName(FitMode mode)
{
  switch (mode) {
    case FitMode::kFitted:
      return "fitted";
    case FitMode::kDefault:
      return "default";
    case FitMode::kNone:
      break;
  }
  return "none";
}

bool fitLeakageExponential(const std::vector<std::pair<double, double>>& t_l,
                           double t_ref_hint_c,
                           LeakageFit& fit)
{
  std::vector<double> temps;
  std::vector<double> ln_l;
  for (const auto& [t, l] : t_l) {
    if (l > 0.0) {
      temps.push_back(t);
      ln_l.push_back(std::log(l));
    }
  }
  if (temps.size() < 2) {
    return false;
  }
  const double t_ref = closest(temps, t_ref_hint_c);
  // Least squares of y = a + b x with x = T - T_ref, y = ln L.
  const size_t n = temps.size();
  double sx = 0.0;
  double sy = 0.0;
  double sxx = 0.0;
  double sxy = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double x = temps[i] - t_ref;
    sx += x;
    sy += ln_l[i];
    sxx += x * x;
    sxy += x * ln_l[i];
  }
  const double det = n * sxx - sx * sx;
  if (det <= 0.0) {
    return false;
  }
  const double b = (n * sxy - sx * sy) / det;
  const double a = (sy - b * sx) / n;
  double err2 = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const double x = temps[i] - t_ref;
    // Relative error of the fitted leakage: exp(fit - ln L) - 1.
    const double rel = std::exp(a + b * x - ln_l[i]) - 1.0;
    err2 += rel * rel;
  }
  fit.t_ref_c = t_ref;
  fit.leakage_ref_w = std::exp(a);
  fit.beta_per_c = b;
  fit.rel_rms_error = std::sqrt(err2 / n);
  fit.num_points = n;
  return true;
}

std::vector<LibraryCorner> collectLibraryCorners(
    sta::dbSta* sta,
    const std::vector<std::string>& corner_names,
    utl::Logger* logger)
{
  std::vector<LibraryCorner> corners;
  std::set<std::string> seen;
  auto add = [&](const sta::LibertyLibrary* lib) {
    if (!seen.insert(lib->name()).second) {
      return;
    }
    LibraryCorner corner;
    corner.library_name = lib->name();
    corner.process = processTag(lib);
    corner.temperature_c = lib->nominalTemperature();
    corner.voltage_v = lib->nominalVoltage();
    corner.file = lib->filename();
    corners.push_back(corner);
  };
  for (const sta::Scene* scene : sta->scenes()) {
    if (!corner_names.empty()
        && std::find(corner_names.begin(), corner_names.end(), scene->name())
               == corner_names.end()) {
      continue;
    }
    for (const sta::MinMax* min_max : sta::MinMax::range()) {
      for (const sta::LibertyLibrary* lib : scene->libertyLibraries(min_max)) {
        add(lib);
      }
    }
  }
  for (const std::string& name : corner_names) {
    if (sta->findScene(name) == nullptr) {
      logger->warn(utl::THM, 137, "Corner {} is not defined.", name);
    }
  }
  if (corners.empty() && corner_names.empty()) {
    // No scene owns the libraries (e.g. read_liberty without corners).
    std::unique_ptr<sta::LibertyLibraryIterator> iter(
        sta->network()->libertyLibraryIterator());
    while (iter->hasNext()) {
      add(iter->next());
    }
  }
  return corners;
}

////////////////////////////////////////////////////////////////

LeakageModel::LeakageModel(utl::Logger* logger)
    : logger_(logger), default_beta_per_c_(ThermalConfig{}.leakage_beta_per_c)
{
}

void LeakageModel::useDefault()
{
  fits_.clear();
  mode_ = FitMode::kDefault;
  family_beta_per_c_ = default_beta_per_c_;
}

void LeakageModel::setDefaultBetaPerC(double beta_per_c)
{
  default_beta_per_c_ = beta_per_c;
  if (mode_ != FitMode::kFitted) {
    useDefault();
  }
}

int LeakageModel::fitFromLibraries(const std::vector<LibraryCorner>& corners,
                                   sta::dbSta* sta)
{
  corners_ = corners;
  fits_.clear();
  t_min_c_ = 0.0;
  t_max_c_ = 0.0;
  const std::vector<ResolvedCorner> resolved
      = resolveCorners(corners, sta, logger_);
  const std::vector<CornerGroup> groups
      = groupCorners(resolved, /*vary_temperature=*/true);
  if (groups.empty()) {
    // Fall back to the documented default beta [ROY] (ThermalConfig
    // leakage_beta_per_c, or the set_thermal_config -set override).
    useDefault();
    logger_->warn(utl::THM,
                  131,
                  "Loaded liberty corners do not isolate temperature ({}); "
                  "per-cell leakage(T) fits are not possible.  Using the "
                  "default leakage beta {:.4g} /C (leakage doubling every "
                  "~10 C, Roy et al. Proc. IEEE 2003).",
                  describeCorners(resolved),
                  family_beta_per_c_);
    return 0;
  }
  const CornerGroup& group = groups.front();
  if (groups.size() > 1) {
    logger_->info(utl::THM,
                  132,
                  "Fitting leakage(T) at process {} / {} V ({} temperatures); "
                  "{} other temperature sweep(s) are ignored.",
                  group.process,
                  group.fixed_value,
                  group.distinct.size(),
                  groups.size() - 1);
  }
  t_min_c_ = group.distinct.front();
  t_max_c_ = group.distinct.back();
  double t_nom = 25.0;
  double v_nom = 0.0;
  staNominal(sta, t_nom, v_nom);
  const double t_ref = closest(group.distinct, t_nom);

  const std::map<std::string, Samples> samples = collectSamples(
      group,
      /*vary_temperature=*/true,
      [](const sta::LibertyCell* cell, const sta::LibertyLibrary*, double& v) {
        return cellLeakageW(cell, v);
      });
  std::vector<double> betas;
  for (const auto& [name, t_l] : samples) {
    LeakageFit fit;
    if (fitLeakageExponential(t_l, t_ref, fit)) {
      fit.cell_name = name;
      fits_[name] = fit;
      betas.push_back(fit.beta_per_c);
    }
  }
  if (fits_.empty()) {
    useDefault();
    logger_->warn(utl::THM,
                  133,
                  "No liberty cell has positive leakage at two or more "
                  "temperatures; using the default leakage beta {:.4g} /C.",
                  family_beta_per_c_);
    return 0;
  }
  mode_ = FitMode::kFitted;
  family_beta_per_c_ = median(betas);
  family_t_ref_c_ = t_ref;
  logger_->info(utl::THM,
                134,
                "Fitted leakage(T) for {} cells over {}..{} C at process {} / "
                "{} V: family beta {:.4g} /C (T_ref {} C).",
                fits_.size(),
                t_min_c_,
                t_max_c_,
                group.process,
                group.fixed_value,
                family_beta_per_c_,
                t_ref);
  return fits_.size();
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
  corners_.clear();
  family_beta_per_c_ = readDouble(ro, "family_beta_per_c", 0.0);
  family_t_ref_c_ = readDouble(ro, "family_t_ref_c", family_t_ref_c_);
  default_beta_per_c_
      = readDouble(ro, "default_beta_per_c", default_beta_per_c_);
  t_min_c_ = readDouble(ro, "t_min_c", 0.0);
  t_max_c_ = readDouble(ro, "t_max_c", 0.0);
  if (auto it = ro.find("corners"); it != ro.end()) {
    for (const json::value& cv : it->value().as_array()) {
      const json::object& co = cv.as_object();
      LibraryCorner corner;
      corner.library_name = std::string(co.at("library").as_string());
      corner.process = std::string(co.at("process").as_string());
      corner.temperature_c = co.at("temperature_c").to_number<double>();
      corner.voltage_v = co.at("voltage_v").to_number<double>();
      if (auto f = co.find("file"); f != co.end()) {
        corner.file = std::string(f->value().as_string());
      }
      corners_.push_back(corner);
    }
  }
  if (auto it = ro.find("cells"); it != ro.end()) {
    for (const json::value& cv : it->value().as_array()) {
      const json::object& co = cv.as_object();
      LeakageFit fit;
      fit.cell_name = std::string(co.at("name").as_string());
      fit.t_ref_c = co.at("t_ref_c").to_number<double>();
      fit.leakage_ref_w = co.at("leakage_ref_w").to_number<double>();
      fit.beta_per_c = co.at("beta_per_c").to_number<double>();
      fit.rel_rms_error = readDouble(co, "rel_rms_error", 0.0);
      fit.num_points = static_cast<int>(readDouble(co, "num_points", 0.0));
      fits_[fit.cell_name] = fit;
    }
  }
  if (ro.contains("mode")) {
    mode_ = readMode(ro, "mode");
  } else {
    // Files written before the mode field existed.
    mode_ = (fits_.empty() && family_beta_per_c_ == 0.0) ? FitMode::kNone
                                                         : FitMode::kFitted;
  }
  logger_->info(
      utl::THM, 82, "Loaded {} leakage fits from {}.", fits_.size(), path);
  return true;
}

void LeakageModel::writeFits(const std::string& path) const
{
  json::object root;
  root["mode"] = fitModeName(mode_);
  root["family_beta_per_c"] = family_beta_per_c_;
  root["family_t_ref_c"] = family_t_ref_c_;
  root["default_beta_per_c"] = default_beta_per_c_;
  root["t_min_c"] = t_min_c_;
  root["t_max_c"] = t_max_c_;
  json::array corners;
  for (const LibraryCorner& corner : corners_) {
    json::object co;
    co["library"] = corner.library_name;
    co["process"] = corner.process;
    co["temperature_c"] = corner.temperature_c;
    co["voltage_v"] = corner.voltage_v;
    co["file"] = corner.file;
    corners.push_back(co);
  }
  root["corners"] = corners;
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
    return std::exp(family_beta_per_c_ * (temp_c - family_t_ref_c_));
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
  out << "Leakage model: ";
  switch (mode_) {
    case FitMode::kNone:
      out << fits_.size() << " cell fits, family beta " << family_beta_per_c_
          << " /C, " << corners_.size() << " corners";
      break;
    case FitMode::kFitted:
      out << "fitted from " << corners_.size() << " corners (" << t_min_c_
          << ".." << t_max_c_ << " C), " << fits_.size()
          << " cell fits, family beta " << family_beta_per_c_ << " /C at T_ref "
          << family_t_ref_c_ << " C";
      break;
    case FitMode::kDefault:
      out << "default beta " << family_beta_per_c_
          << " /C (no fit; leakage doubling per ~10 C, Roy et al. 2003), "
          << corners_.size() << " corners";
      break;
  }
  return out.str();
}

////////////////////////////////////////////////////////////////

DerateModel::DerateModel(utl::Logger* logger)
    : logger_(logger),
      default_tempco_per_c_(ThermalConfig{}.delay_tempco_per_c),
      default_vcoef_per_v_(ThermalConfig{}.delay_vcoef_per_v)
{
}

void DerateModel::setDefaultTempcoPerC(double tempco_per_c)
{
  default_tempco_per_c_ = tempco_per_c;
  if (temp_mode_ != FitMode::kFitted) {
    temp_mode_ = FitMode::kDefault;
  }
}

void DerateModel::setDefaultVcoefPerV(double vcoef_per_v)
{
  default_vcoef_per_v_ = vcoef_per_v;
  if (volt_mode_ != FitMode::kFitted) {
    volt_mode_ = FitMode::kDefault;
  }
}

int DerateModel::fitFromLibraries(const std::vector<LibraryCorner>& corners,
                                  sta::dbSta* sta)
{
  temp_points_.clear();
  volt_points_.clear();
  cell_temp_sens_per_c_.clear();
  cell_volt_sens_per_v_.clear();
  t_min_c_ = 0.0;
  t_max_c_ = 0.0;
  const std::vector<ResolvedCorner> resolved
      = resolveCorners(corners, sta, logger_);
  const std::vector<CornerGroup> temp_groups
      = groupCorners(resolved, /*vary_temperature=*/true);
  const std::vector<CornerGroup> volt_groups
      = groupCorners(resolved, /*vary_temperature=*/false);

  // Nominal point: the STA library corner, snapped onto the fitted sweeps.
  double t_nom = t_nom_c_;
  double v_nom = v_nom_v_;
  staNominal(sta, t_nom, v_nom);
  if (!temp_groups.empty()) {
    t_nom = closest(temp_groups.front().distinct, t_nom);
    v_nom = temp_groups.front().fixed_value;
  }
  const CornerGroup* volt_group = nullptr;
  for (const CornerGroup& g : volt_groups) {
    if (std::abs(g.fixed_value - t_nom) < kTempEpsC) {
      volt_group = &g;
      break;
    }
  }
  if (volt_group == nullptr && !volt_groups.empty()) {
    volt_group = &volt_groups.front();
  }
  if (volt_group != nullptr) {
    v_nom = closest(volt_group->distinct, v_nom);
    if (temp_groups.empty()) {
      t_nom = volt_group->fixed_value;
    }
  }
  t_nom_c_ = t_nom;
  v_nom_v_ = v_nom;

  auto delay = [](const sta::LibertyCell* cell,
                  const sta::LibertyLibrary* lib,
                  double& v) { return representativeDelayS(cell, lib, v); };

  // Temperature sweep.
  if (temp_groups.empty()) {
    temp_mode_ = FitMode::kDefault;
    logger_->warn(utl::THM,
                  135,
                  "Loaded liberty corners do not isolate temperature ({}); "
                  "delay(T) is not fitted.  Using the default delay "
                  "temperature coefficient {:.4g} /C (alpha-power law with "
                  "mobility ~ T^-1.5, Sakurai-Newton JSSC 1990).",
                  describeCorners(resolved),
                  default_tempco_per_c_);
  } else {
    const CornerGroup& group = temp_groups.front();
    t_min_c_ = group.distinct.front();
    t_max_c_ = group.distinct.back();
    const std::map<std::string, Samples> samples
        = collectSamples(group, /*vary_temperature=*/true, delay);
    std::map<double, std::vector<double>> ratios_by_t;
    for (const auto& [name, t_d] : samples) {
      double sens;
      Samples ratios;
      if (relativeSensitivity(t_d, t_nom, kTempEpsC, sens, ratios)) {
        cell_temp_sens_per_c_[name] = sens;
        for (const auto& [t, r] : ratios) {
          ratios_by_t[closest(group.distinct, t)].push_back(r);
        }
      }
    }
    for (const auto& [t, ratios] : ratios_by_t) {
      temp_points_.emplace_back(t, median(ratios));
    }
    if (temp_points_.empty()) {
      temp_mode_ = FitMode::kDefault;
      logger_->warn(utl::THM,
                    136,
                    "No liberty cell has delay tables at two or more "
                    "temperatures; using the default delay temperature "
                    "coefficient {:.4g} /C.",
                    default_tempco_per_c_);
    } else {
      temp_mode_ = FitMode::kFitted;
      logger_->info(utl::THM,
                    138,
                    "Fitted delay(T) for {} cells over {}..{} C at process {} "
                    "/ {} V: family factor {:.4g} at {} C, {:.4g} at {} C.",
                    cell_temp_sens_per_c_.size(),
                    t_min_c_,
                    t_max_c_,
                    group.process,
                    group.fixed_value,
                    temp_points_.front().second,
                    temp_points_.front().first,
                    temp_points_.back().second,
                    temp_points_.back().first);
    }
  }

  // Voltage sweep.
  if (volt_group == nullptr) {
    volt_mode_ = FitMode::kDefault;
    logger_->warn(utl::THM,
                  139,
                  "Loaded liberty corners do not isolate supply voltage; "
                  "delay(V) is not fitted.  Using the default delay voltage "
                  "coefficient {:.4g} /V (alpha-power law, Sakurai-Newton "
                  "JSSC 1990).",
                  default_vcoef_per_v_);
  } else {
    const std::map<std::string, Samples> samples
        = collectSamples(*volt_group, /*vary_temperature=*/false, delay);
    std::map<double, std::vector<double>> ratios_by_v;
    for (const auto& [name, v_d] : samples) {
      double sens;
      Samples ratios;
      if (relativeSensitivity(v_d, v_nom, kVoltEpsV, sens, ratios)) {
        cell_volt_sens_per_v_[name] = sens;
        for (const auto& [v, r] : ratios) {
          ratios_by_v[closest(volt_group->distinct, v)].push_back(r);
        }
      }
    }
    for (const auto& [v, ratios] : ratios_by_v) {
      volt_points_.emplace_back(v, median(ratios));
    }
    if (volt_points_.empty()) {
      volt_mode_ = FitMode::kDefault;
      logger_->warn(utl::THM,
                    140,
                    "No liberty cell has delay tables at two or more supply "
                    "voltages; using the default delay voltage coefficient "
                    "{:.4g} /V.",
                    default_vcoef_per_v_);
    } else {
      volt_mode_ = FitMode::kFitted;
      logger_->info(utl::THM,
                    141,
                    "Fitted delay(V) for {} cells over {}..{} V at process {} "
                    "/ {} C: family factor {:.4g} at {} V, {:.4g} at {} V.",
                    cell_volt_sens_per_v_.size(),
                    volt_group->distinct.front(),
                    volt_group->distinct.back(),
                    volt_group->process,
                    volt_group->fixed_value,
                    volt_points_.front().second,
                    volt_points_.front().first,
                    volt_points_.back().second,
                    volt_points_.back().first);
    }
  }
  return cell_temp_sens_per_c_.size();
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
  t_nom_c_ = readDouble(ro, "t_nom_c", t_nom_c_);
  v_nom_v_ = readDouble(ro, "v_nom_v", v_nom_v_);
  default_tempco_per_c_
      = readDouble(ro, "default_tempco_per_c", default_tempco_per_c_);
  default_vcoef_per_v_
      = readDouble(ro, "default_vcoef_per_v", default_vcoef_per_v_);
  t_min_c_ = readDouble(ro, "t_min_c", 0.0);
  t_max_c_ = readDouble(ro, "t_max_c", 0.0);
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
  if (ro.contains("temperature_mode")) {
    temp_mode_ = readMode(ro, "temperature_mode");
  } else {
    temp_mode_ = temp_points_.empty() ? FitMode::kNone : FitMode::kFitted;
  }
  if (ro.contains("voltage_mode")) {
    volt_mode_ = readMode(ro, "voltage_mode");
  } else {
    volt_mode_ = volt_points_.empty() ? FitMode::kNone : FitMode::kFitted;
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
  root["temperature_mode"] = fitModeName(temp_mode_);
  root["voltage_mode"] = fitModeName(volt_mode_);
  root["default_tempco_per_c"] = default_tempco_per_c_;
  root["default_vcoef_per_v"] = default_vcoef_per_v_;
  root["t_min_c"] = t_min_c_;
  root["t_max_c"] = t_max_c_;
  root["temperature_factor"] = writePoints(temp_points_);
  root["voltage_factor"] = writePoints(volt_points_);
  json::array cells;
  std::set<std::string> names;
  for (const auto& [name, sens] : cell_temp_sens_per_c_) {
    names.insert(name);
  }
  for (const auto& [name, sens] : cell_volt_sens_per_v_) {
    names.insert(name);
  }
  for (const std::string& name : names) {
    json::object co;
    co["name"] = name;
    if (auto it = cell_temp_sens_per_c_.find(name);
        it != cell_temp_sens_per_c_.end()) {
      co["temp_sens_per_c"] = it->second;
    }
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

double DerateModel::temperatureFactor(const std::string& cell_name,
                                      double temp_c) const
{
  auto it = cell_temp_sens_per_c_.find(cell_name);
  if (it != cell_temp_sens_per_c_.end()) {
    return std::max(0.0, 1.0 + it->second * (temp_c - t_nom_c_));
  }
  if (!temp_points_.empty()) {
    return interpolate(temp_points_, temp_c);
  }
  if (temp_mode_ == FitMode::kDefault) {
    return std::max(0.0, 1.0 + default_tempco_per_c_ * (temp_c - t_nom_c_));
  }
  return 1.0;
}

double DerateModel::voltageFactor(const std::string& cell_name,
                                  double vdd_v) const
{
  auto it = cell_volt_sens_per_v_.find(cell_name);
  if (it != cell_volt_sens_per_v_.end()) {
    return std::max(0.0, 1.0 + it->second * (vdd_v - v_nom_v_));
  }
  if (!volt_points_.empty()) {
    return interpolate(volt_points_, vdd_v);
  }
  if (volt_mode_ == FitMode::kDefault) {
    return std::max(0.0, 1.0 + default_vcoef_per_v_ * (vdd_v - v_nom_v_));
  }
  return 1.0;
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
  out << "Derate model: nominal " << t_nom_c_ << " C / " << v_nom_v_ << " V, ";
  if (temp_mode_ == FitMode::kNone && volt_mode_ == FitMode::kNone) {
    out << temp_points_.size() << " temperature points, " << volt_points_.size()
        << " voltage points, " << cell_temp_sens_per_c_.size() << " cell fits";
    return out.str();
  }
  out << "temperature ";
  switch (temp_mode_) {
    case FitMode::kFitted:
      out << "fitted (" << temp_points_.size() << " points, " << t_min_c_
          << ".." << t_max_c_ << " C, " << cell_temp_sens_per_c_.size()
          << " cell fits)";
      break;
    case FitMode::kDefault:
      out << "default " << default_tempco_per_c_
          << " /C (Sakurai-Newton alpha-power law)";
      break;
    case FitMode::kNone:
      out << "none (factor 1)";
      break;
  }
  out << ", voltage ";
  switch (volt_mode_) {
    case FitMode::kFitted:
      out << "fitted (" << volt_points_.size() << " points, "
          << cell_volt_sens_per_v_.size() << " cell fits)";
      break;
    case FitMode::kDefault:
      out << "default " << default_vcoef_per_v_
          << " /V (Sakurai-Newton alpha-power law)";
      break;
    case FitMode::kNone:
      out << "none (factor 1)";
      break;
  }
  return out.str();
}

}  // namespace thm
