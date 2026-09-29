// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "odb/db.h"
#include "odb/geom.h"
#include "thm/PhysicsState.h"
#include "web/heatMap.h"

namespace thm {

class Thermal;

// Rectangle (DBU) of lateral tile (x, y) of an nx*ny map covering die_rect.
odb::Rect physicsTileRect(const odb::Rect& die_rect,
                          int nx,
                          int ny,
                          int x,
                          int y);

// Absolute temperature heat map (degrees C) of the active layer of a die.
// The scale is absolute by default (fixed min/max in C) so that maps from
// different iterations / runs are directly comparable.  Draws the live grid,
// or the history snapshot selected with Thermal::setDisplayedSnapshot.
class TemperatureDataSource : public web::RealValueHeatMapDataSource
{
 public:
  TemperatureDataSource(Thermal* thermal, utl::Logger* logger);

  void setDie(int die) { die_ = die; }
  int getDie() const { return die_; }
  void setFixedScale(bool fixed) { fixed_scale_ = fixed; }
  void setScale(double min_c, double max_c)
  {
    scale_min_c_ = min_c;
    scale_max_c_ = max_c;
  }
  double getScaleMin() const { return scale_min_c_; }
  double getScaleMax() const { return scale_max_c_; }
  bool isFixedScale() const { return fixed_scale_; }

 protected:
  bool populateMap() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;
  void determineMinMax(const web::HeatMapDataSource::Map& map) override;

 private:
  std::vector<std::string> dieChoices() const;
  std::vector<std::string> scaleChoices() const;
  std::string scaleChoice() const;
  void setScaleChoice(const std::string& choice);

  Thermal* thermal_;
  int die_ = 0;
  bool fixed_scale_ = true;
  double scale_min_c_ = 25.0;
  double scale_max_c_ = 125.0;
};

// Generic map over the thermal grid for the other physics quantities.
class PhysicsMapDataSource : public web::RealValueHeatMapDataSource
{
 public:
  enum class Kind
  {
    kLeakage,  // leakage power density W/m^2
    kDerate,   // delay derate multiplier
    kIrDrop,   // supply drop V
    kDelta,    // temperature difference to the reference run, C
    kEmRisk    // relative electromigration lifetime factor
  };

  PhysicsMapDataSource(Thermal* thermal,
                       Kind kind,
                       utl::Logger* logger,
                       const std::string& unit_suffix,
                       const std::string& name,
                       const std::string& short_name);

 protected:
  bool populateMap() override;
  void combineMapData(bool base_has_value,
                      double& base,
                      double new_data,
                      double data_area,
                      double intersection_area,
                      double rect_area) override;

 private:
  std::vector<std::string> dieChoices() const;
  bool buildMap(MapSnapshot& map) const;

  Thermal* thermal_;
  Kind kind_;
  int die_ = 0;
};

}  // namespace thm
