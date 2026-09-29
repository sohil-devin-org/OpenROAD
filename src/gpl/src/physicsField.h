// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <vector>

#include "odb/geom.h"
#include "point.h"

namespace utl {
class Logger;
}

namespace gpl {

// Screened-Poisson "thermal spreading" potential on a regular grid over the
// core area:
//
//   lambda^2 * laplacian(phi) - phi = -q
//
// where q is the normalized power density deposited by the cells and lambda
// the thermal screening length (sqrt(k_eff * t_si * R_theta_area)). The
// gradient of phi is the force that pushes power away from hot regions; it
// decays with the screening length so distant cells do not interact.
class PhysicsField
{
 public:
  PhysicsField(const odb::Rect& core,
               int nx,
               int ny,
               double screening_length_dbu,
               utl::Logger* log);

  int nx() const { return nx_; }
  int ny() const { return ny_; }

  void clearPower();
  // Deposit `watts` over [lx, ux] x [ly, uy] weighted by overlap area.
  void addPower(int lx, int ly, int ux, int uy, double watts);
  // Solve for phi with Gauss-Seidel/SOR sweeps (warm started from the
  // previous solution). Returns the number of sweeps used.
  int solve(int max_sweeps, double tolerance);

  // d(phi)/dx and d(phi)/dy (per DBU) at a point, bilinearly interpolated
  // between tile centers.
  FloatPoint gradientAt(int x, int y) const;

  double peakPotential() const;
  double totalPowerW() const { return total_power_w_; }
  double tileWatts(int ix, int iy) const { return power_[index(ix, iy)]; }
  double potential(int ix, int iy) const { return phi_[index(ix, iy)]; }

 private:
  int index(int ix, int iy) const { return iy * nx_ + ix; }
  double normalizedSource(int ix, int iy) const;

  odb::Rect core_;
  int nx_;
  int ny_;
  double tile_w_;  // DBU
  double tile_h_;  // DBU
  double lambda_;  // DBU
  utl::Logger* log_;
  std::vector<double> power_;  // watts per tile
  std::vector<double> phi_;
  double total_power_w_ = 0.0;
};

}  // namespace gpl
