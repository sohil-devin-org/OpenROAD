// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "odb/geom.h"
#include "thm/ThermalConfig.h"

namespace thm {

// Lateral power density map on the thermal grid for a single die.
// Values are dissipated power in watts per tile (not per area).
class PowerMap
{
 public:
  PowerMap() = default;
  PowerMap(int nx, int ny);

  void reset(int nx, int ny);
  void clear();

  int nx() const { return nx_; }
  int ny() const { return ny_; }
  int size() const { return nx_ * ny_; }
  int index(int x, int y) const { return y * nx_ + x; }

  double& at(int x, int y) { return power_w_[index(x, y)]; }
  double at(int x, int y) const { return power_w_[index(x, y)]; }
  const std::vector<double>& values() const { return power_w_; }
  std::vector<double>& values() { return power_w_; }

  double total() const;
  double max() const;

  // Adds a rectangle of power (in W) to the tiles it overlaps, weighted by
  // overlap area. The rectangle and die bounds are in DBU.
  void addRect(const odb::Rect& rect, const odb::Rect& die, double power_w);

 private:
  int nx_ = 0;
  int ny_ = 0;
  std::vector<double> power_w_;
};

// 3D finite-volume temperature grid covering the vertical stack.
// z index 0 is the bottom of the stack; layer k spans z cells given by the
// stack description.  Temperatures are stored in degrees Celsius.
class ThermalGrid
{
 public:
  ThermalGrid() = default;

  // Build a grid of nx*ny lateral tiles over the die rectangle (DBU) with the
  // given stack.  Each StackLayer becomes one or more z cells
  // (cells_per_layer).
  void reset(int nx,
             int ny,
             const odb::Rect& die,
             double dbu_per_micron,
             const std::vector<StackLayer>& stack,
             int cells_per_layer = 1);

  int nx() const { return nx_; }
  int ny() const { return ny_; }
  int nz() const { return nz_; }
  int size() const { return nx_ * ny_ * nz_; }
  int index(int x, int y, int z) const { return (z * ny_ + y) * nx_ + x; }

  double& at(int x, int y, int z) { return temp_c_[index(x, y, z)]; }
  double at(int x, int y, int z) const { return temp_c_[index(x, y, z)]; }
  std::vector<double>& values() { return temp_c_; }
  const std::vector<double>& values() const { return temp_c_; }
  void fill(double temp_c);

  // Lateral tile size in meters.
  double dx() const { return dx_m_; }
  double dy() const { return dy_m_; }
  // Thickness of z cell k in meters and its material.
  double dz(int z) const { return dz_m_[z]; }
  double conductivity(int z) const { return k_w_mk_[z]; }
  double heatCapacity(int z) const { return c_j_m3k_[z]; }
  int layerOf(int z) const { return layer_of_z_[z]; }
  int dieOf(int z) const { return die_of_z_[z]; }
  bool isActive(int z) const { return active_z_[z]; }
  const std::vector<StackLayer>& stack() const { return stack_; }

  // z index of the active layer of the given die (-1 if none).
  int activeZ(int die) const;
  int numDies() const;

  const odb::Rect& dieRect() const { return die_; }
  double dbuPerMicron() const { return dbu_per_micron_; }
  // Rectangle (DBU) covered by lateral tile (x, y).
  odb::Rect tileRect(int x, int y) const;
  // Lateral tile containing DBU point (x, y), clamped to the grid.
  void tileAt(int x_dbu, int y_dbu, int& tx, int& ty) const;

  // 2D slice (nx*ny) of the active layer of a die.
  std::vector<double> activeLayer(int die) const;

  double peak(int die) const;
  double average(int die) const;
  // Maximum lateral temperature gradient of the die's active layer in C/mm.
  double maxGradientCPerMm(int die) const;

 private:
  int nx_ = 0;
  int ny_ = 0;
  int nz_ = 0;
  odb::Rect die_;
  double dbu_per_micron_ = 1.0;
  double dx_m_ = 0.0;
  double dy_m_ = 0.0;
  std::vector<StackLayer> stack_;
  std::vector<double> dz_m_;
  std::vector<double> k_w_mk_;
  std::vector<double> c_j_m3k_;
  std::vector<int> layer_of_z_;
  std::vector<int> die_of_z_;
  std::vector<bool> active_z_;
  std::vector<double> temp_c_;
};

}  // namespace thm
