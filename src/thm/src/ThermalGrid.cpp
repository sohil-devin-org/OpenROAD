// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/ThermalGrid.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace thm {

PowerMap::PowerMap(int nx, int ny)
{
  reset(nx, ny);
}

void PowerMap::reset(int nx, int ny)
{
  nx_ = nx;
  ny_ = ny;
  power_w_.assign(static_cast<size_t>(nx) * ny, 0.0);
}

void PowerMap::clear()
{
  std::fill(power_w_.begin(), power_w_.end(), 0.0);
}

double PowerMap::total() const
{
  return std::accumulate(power_w_.begin(), power_w_.end(), 0.0);
}

double PowerMap::max() const
{
  if (power_w_.empty()) {
    return 0.0;
  }
  return *std::max_element(power_w_.begin(), power_w_.end());
}

void PowerMap::addRect(const odb::Rect& rect,
                       const odb::Rect& die,
                       double power_w)
{
  if (nx_ == 0 || ny_ == 0 || power_w == 0.0 || die.area() == 0) {
    return;
  }
  const double tile_w = static_cast<double>(die.dx()) / nx_;
  const double tile_h = static_cast<double>(die.dy()) / ny_;
  odb::Rect clipped = rect.intersect(die);
  if (clipped.area() == 0) {
    // Point-like or off-die: drop into the nearest tile.
    const int tx = std::clamp(
        static_cast<int>((rect.xCenter() - die.xMin()) / tile_w), 0, nx_ - 1);
    const int ty = std::clamp(
        static_cast<int>((rect.yCenter() - die.yMin()) / tile_h), 0, ny_ - 1);
    at(tx, ty) += power_w;
    return;
  }
  const double rect_area = static_cast<double>(clipped.area());
  const int x0 = std::clamp(
      static_cast<int>((clipped.xMin() - die.xMin()) / tile_w), 0, nx_ - 1);
  const int x1 = std::clamp(
      static_cast<int>((clipped.xMax() - die.xMin()) / tile_w), 0, nx_ - 1);
  const int y0 = std::clamp(
      static_cast<int>((clipped.yMin() - die.yMin()) / tile_h), 0, ny_ - 1);
  const int y1 = std::clamp(
      static_cast<int>((clipped.yMax() - die.yMin()) / tile_h), 0, ny_ - 1);
  for (int ty = y0; ty <= y1; ++ty) {
    const double ty_min = die.yMin() + ty * tile_h;
    const double ty_max = ty_min + tile_h;
    const double oy = std::min<double>(clipped.yMax(), ty_max)
                      - std::max<double>(clipped.yMin(), ty_min);
    if (oy <= 0) {
      continue;
    }
    for (int tx = x0; tx <= x1; ++tx) {
      const double tx_min = die.xMin() + tx * tile_w;
      const double tx_max = tx_min + tile_w;
      const double ox = std::min<double>(clipped.xMax(), tx_max)
                        - std::max<double>(clipped.xMin(), tx_min);
      if (ox <= 0) {
        continue;
      }
      at(tx, ty) += power_w * (ox * oy) / rect_area;
    }
  }
}

void ThermalGrid::reset(int nx,
                        int ny,
                        const odb::Rect& die,
                        double dbu_per_micron,
                        const std::vector<StackLayer>& stack,
                        int cells_per_layer)
{
  nx_ = nx;
  ny_ = ny;
  die_ = die;
  dbu_per_micron_ = dbu_per_micron;
  stack_ = stack;
  const double dbu_to_m = 1e-6 / dbu_per_micron;
  dx_m_ = die.dx() * dbu_to_m / nx;
  dy_m_ = die.dy() * dbu_to_m / ny;

  dz_m_.clear();
  k_w_mk_.clear();
  c_j_m3k_.clear();
  layer_of_z_.clear();
  die_of_z_.clear();
  active_z_.clear();
  cells_per_layer = std::max(1, cells_per_layer);
  for (size_t layer = 0; layer < stack.size(); ++layer) {
    const StackLayer& l = stack[layer];
    // The thin active layer never needs vertical refinement.
    const int cells = l.is_active ? 1 : cells_per_layer;
    for (int c = 0; c < cells; ++c) {
      dz_m_.push_back(l.thickness_m / cells);
      k_w_mk_.push_back(l.conductivity_w_mk);
      c_j_m3k_.push_back(l.volumetric_heat_capacity_j_m3k);
      layer_of_z_.push_back(layer);
      die_of_z_.push_back(l.die);
      active_z_.push_back(l.is_active);
    }
  }
  nz_ = dz_m_.size();
  temp_c_.assign(static_cast<size_t>(nx_) * ny_ * nz_, 0.0);
}

void ThermalGrid::fill(double temp_c)
{
  std::fill(temp_c_.begin(), temp_c_.end(), temp_c);
}

int ThermalGrid::activeZ(int die) const
{
  for (int z = 0; z < nz_; ++z) {
    if (active_z_[z] && die_of_z_[z] == die) {
      return z;
    }
  }
  return -1;
}

int ThermalGrid::numDies() const
{
  int dies = 0;
  for (int z = 0; z < nz_; ++z) {
    dies = std::max(dies, die_of_z_[z] + 1);
  }
  return dies;
}

odb::Rect ThermalGrid::tileRect(int x, int y) const
{
  const double tile_w = static_cast<double>(die_.dx()) / nx_;
  const double tile_h = static_cast<double>(die_.dy()) / ny_;
  return odb::Rect(die_.xMin() + static_cast<int>(x * tile_w),
                   die_.yMin() + static_cast<int>(y * tile_h),
                   die_.xMin() + static_cast<int>((x + 1) * tile_w),
                   die_.yMin() + static_cast<int>((y + 1) * tile_h));
}

void ThermalGrid::tileAt(int x_dbu, int y_dbu, int& tx, int& ty) const
{
  const double tile_w = static_cast<double>(die_.dx()) / nx_;
  const double tile_h = static_cast<double>(die_.dy()) / ny_;
  tx = std::clamp(static_cast<int>((x_dbu - die_.xMin()) / tile_w), 0, nx_ - 1);
  ty = std::clamp(static_cast<int>((y_dbu - die_.yMin()) / tile_h), 0, ny_ - 1);
}

std::vector<double> ThermalGrid::activeLayer(int die) const
{
  std::vector<double> slice(static_cast<size_t>(nx_) * ny_, 0.0);
  const int z = activeZ(die);
  if (z < 0) {
    return slice;
  }
  for (int y = 0; y < ny_; ++y) {
    for (int x = 0; x < nx_; ++x) {
      slice[y * nx_ + x] = at(x, y, z);
    }
  }
  return slice;
}

double ThermalGrid::peak(int die) const
{
  const auto slice = activeLayer(die);
  if (slice.empty()) {
    return 0.0;
  }
  return *std::max_element(slice.begin(), slice.end());
}

double ThermalGrid::average(int die) const
{
  const auto slice = activeLayer(die);
  if (slice.empty()) {
    return 0.0;
  }
  return std::accumulate(slice.begin(), slice.end(), 0.0) / slice.size();
}

double ThermalGrid::maxGradientCPerMm(int die) const
{
  const int z = activeZ(die);
  if (z < 0) {
    return 0.0;
  }
  double max_grad = 0.0;
  for (int y = 0; y < ny_; ++y) {
    for (int x = 0; x < nx_; ++x) {
      if (x + 1 < nx_) {
        max_grad = std::max(
            max_grad, std::abs(at(x + 1, y, z) - at(x, y, z)) / (dx_m_ * 1e3));
      }
      if (y + 1 < ny_) {
        max_grad = std::max(
            max_grad, std::abs(at(x, y + 1, z) - at(x, y, z)) / (dy_m_ * 1e3));
      }
    }
  }
  return max_grad;
}

}  // namespace thm
