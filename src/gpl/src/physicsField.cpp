// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "physicsField.h"

#include <algorithm>
#include <cmath>

#include "utl/Logger.h"

namespace gpl {

PhysicsField::PhysicsField(const odb::Rect& core,
                           const int nx,
                           const int ny,
                           const double screening_length_dbu,
                           utl::Logger* log)
    : core_(core),
      nx_(std::max(nx, 2)),
      ny_(std::max(ny, 2)),
      tile_w_(static_cast<double>(core.dx()) / nx_),
      tile_h_(static_cast<double>(core.dy()) / ny_),
      lambda_(std::max(screening_length_dbu, 0.5 * (tile_w_ + tile_h_))),
      log_(log),
      power_(static_cast<size_t>(nx_) * ny_, 0.0),
      phi_(static_cast<size_t>(nx_) * ny_, 0.0)
{
}

void PhysicsField::clearPower()
{
  std::fill(power_.begin(), power_.end(), 0.0);
  total_power_w_ = 0.0;
}

void PhysicsField::addPower(const int lx,
                            const int ly,
                            const int ux,
                            const int uy,
                            const double watts)
{
  if (watts <= 0.0 || ux <= lx || uy <= ly) {
    return;
  }
  const double area = static_cast<double>(ux - lx) * (uy - ly);
  const auto clampX = [&](double x) {
    return std::clamp(x, 0.0, static_cast<double>(nx_) - 1e-9);
  };
  const auto clampY = [&](double y) {
    return std::clamp(y, 0.0, static_cast<double>(ny_) - 1e-9);
  };
  const double fx0 = clampX((lx - core_.xMin()) / tile_w_);
  const double fx1 = clampX((ux - core_.xMin()) / tile_w_);
  const double fy0 = clampY((ly - core_.yMin()) / tile_h_);
  const double fy1 = clampY((uy - core_.yMin()) / tile_h_);
  const int ix0 = static_cast<int>(fx0);
  const int ix1 = static_cast<int>(fx1);
  const int iy0 = static_cast<int>(fy0);
  const int iy1 = static_cast<int>(fy1);
  for (int iy = iy0; iy <= iy1; ++iy) {
    const double oy = std::min(fy1, iy + 1.0) - std::max(fy0, 1.0 * iy);
    if (oy <= 0) {
      continue;
    }
    for (int ix = ix0; ix <= ix1; ++ix) {
      const double ox = std::min(fx1, ix + 1.0) - std::max(fx0, 1.0 * ix);
      if (ox <= 0) {
        continue;
      }
      const double overlap = ox * tile_w_ * oy * tile_h_;
      power_[index(ix, iy)] += watts * std::min(1.0, overlap / area);
    }
  }
  total_power_w_ += watts;
}

double PhysicsField::normalizedSource(const int ix, const int iy) const
{
  // Normalize by the mean tile power so phi is O(1) regardless of the design
  // power level; the placer rescales the force against the wirelength
  // gradient anyway.
  const double mean = total_power_w_ / (static_cast<double>(nx_) * ny_);
  if (mean <= 0.0) {
    return 0.0;
  }
  return power_[index(ix, iy)] / mean;
}

int PhysicsField::solve(const int max_sweeps, const double tolerance)
{
  if (total_power_w_ <= 0.0) {
    std::fill(phi_.begin(), phi_.end(), 0.0);
    return 0;
  }
  // lambda^2 (phi_xx + phi_yy) - phi = -q  discretized with adiabatic
  // (mirror) edges; the diagonal is 2 lambda^2 (1/hx^2 + 1/hy^2) + 1.
  const double ax = lambda_ * lambda_ / (tile_w_ * tile_w_);
  const double ay = lambda_ * lambda_ / (tile_h_ * tile_h_);
  const double diag = 2.0 * ax + 2.0 * ay + 1.0;
  const double omega = 1.5;  // SOR relaxation
  int sweep = 0;
  for (; sweep < max_sweeps; ++sweep) {
    double max_delta = 0.0;
    double max_phi = 0.0;
    for (int iy = 0; iy < ny_; ++iy) {
      for (int ix = 0; ix < nx_; ++ix) {
        const double w = phi_[index(ix > 0 ? ix - 1 : ix + 1, iy)];
        const double e = phi_[index(ix + 1 < nx_ ? ix + 1 : ix - 1, iy)];
        const double s = phi_[index(ix, iy > 0 ? iy - 1 : iy + 1)];
        const double n = phi_[index(ix, iy + 1 < ny_ ? iy + 1 : iy - 1)];
        const double gs
            = (ax * (w + e) + ay * (s + n) + normalizedSource(ix, iy)) / diag;
        double& cell = phi_[index(ix, iy)];
        const double updated = cell + omega * (gs - cell);
        max_delta = std::max(max_delta, std::fabs(updated - cell));
        max_phi = std::max(max_phi, std::fabs(updated));
        cell = updated;
      }
    }
    if (max_delta <= tolerance * std::max(max_phi, 1e-30)) {
      ++sweep;
      break;
    }
  }
  return sweep;
}

FloatPoint PhysicsField::gradientAt(const int x, const int y) const
{
  if (total_power_w_ <= 0.0) {
    return FloatPoint(0, 0);
  }
  // Tile-center coordinates of the query point.
  const double fx = std::clamp(
      (x - core_.xMin()) / tile_w_ - 0.5, 0.0, static_cast<double>(nx_ - 1));
  const double fy = std::clamp(
      (y - core_.yMin()) / tile_h_ - 0.5, 0.0, static_cast<double>(ny_ - 1));
  const int ix = std::min(static_cast<int>(fx), nx_ - 2);
  const int iy = std::min(static_cast<int>(fy), ny_ - 2);
  const double tx = fx - ix;
  const double ty = fy - iy;
  const double p00 = phi_[index(ix, iy)];
  const double p10 = phi_[index(ix + 1, iy)];
  const double p01 = phi_[index(ix, iy + 1)];
  const double p11 = phi_[index(ix + 1, iy + 1)];
  const double dphi_dx
      = ((p10 - p00) * (1.0 - ty) + (p11 - p01) * ty) / tile_w_;
  const double dphi_dy
      = ((p01 - p00) * (1.0 - tx) + (p11 - p10) * tx) / tile_h_;
  return FloatPoint(static_cast<float>(dphi_dx), static_cast<float>(dphi_dy));
}

double PhysicsField::peakPotential() const
{
  double peak = 0.0;
  for (const double v : phi_) {
    peak = std::max(peak, v);
  }
  return peak;
}

}  // namespace gpl
