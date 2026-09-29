// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/ThermalSolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "utl/Logger.h"

namespace thm {

// Lumped reference solver: every lateral tile is an independent thermal
// column with conductance (1/R_top + 1/R_bottom) * (tile_area / die_area)
// to ambient and no lateral coupling.  Exact for uniform power, and an upper
// bound on peak temperature for any power map (no spreading).
class LumpedSolver : public ThermalSolver
{
 public:
  explicit LumpedSolver(utl::Logger* logger) : logger_(logger) {}

  SolveResult solveSteady(const std::vector<PowerMap>& die_power,
                          const ThermalConfig& config,
                          ThermalGrid& grid,
                          bool warm_start) override
  {
    const auto start = std::chrono::steady_clock::now();
    SolveResult result;
    const double g_tile = tileConductance(config, grid);
    grid.fill(config.ambient_c);
    for (int y = 0; y < grid.ny(); ++y) {
      for (int x = 0; x < grid.nx(); ++x) {
        double p = 0.0;
        for (const PowerMap& map : die_power) {
          p += map.at(x, y);
        }
        const double t = config.ambient_c + (g_tile > 0 ? p / g_tile : 0.0);
        for (int z = 0; z < grid.nz(); ++z) {
          grid.at(x, y, z) = t;
        }
        result.boundary_heat_w += p;
      }
    }
    result.converged = true;
    result.iterations = 1;
    result.runtime_s = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - start)
                           .count();
    return result;
  }

  SolveResult stepTransient(const std::vector<PowerMap>& die_power,
                            const ThermalConfig& config,
                            double dt_s,
                            ThermalGrid& grid) override
  {
    SolveResult result;
    const double g_tile = tileConductance(config, grid);
    const double c_tile = tileHeatCapacity(grid);
    for (int y = 0; y < grid.ny(); ++y) {
      for (int x = 0; x < grid.nx(); ++x) {
        double p = 0.0;
        for (const PowerMap& map : die_power) {
          p += map.at(x, y);
        }
        // Backward Euler: C (T1 - T0)/dt = P - G (T1 - Tamb)
        const double t0 = grid.at(x, y, 0);
        const double t1 = (c_tile * t0 + dt_s * (p + g_tile * config.ambient_c))
                          / (c_tile + dt_s * g_tile);
        for (int z = 0; z < grid.nz(); ++z) {
          grid.at(x, y, z) = t1;
        }
        result.boundary_heat_w += g_tile * (t1 - config.ambient_c);
      }
    }
    result.converged = true;
    result.iterations = 1;
    return result;
  }

  double timeConstantS(const ThermalConfig& config,
                       const ThermalGrid& grid) const override
  {
    const double g_tile = tileConductance(config, grid);
    return g_tile > 0 ? tileHeatCapacity(grid) / g_tile : 0.0;
  }

 private:
  static double tileConductance(const ThermalConfig& config,
                                const ThermalGrid& grid)
  {
    const double g_top
        = config.top_resistance_k_w > 0 ? 1.0 / config.top_resistance_k_w : 0;
    const double g_bot = config.bottom_resistance_k_w > 0
                             ? 1.0 / config.bottom_resistance_k_w
                             : 0;
    const double tiles = static_cast<double>(grid.nx()) * grid.ny();
    return tiles > 0 ? (g_top + g_bot) / tiles : 0.0;
  }

  static double tileHeatCapacity(const ThermalGrid& grid)
  {
    double c = 0.0;
    for (int z = 0; z < grid.nz(); ++z) {
      c += grid.heatCapacity(z) * grid.dz(z);
    }
    return c * grid.dx() * grid.dy();
  }

  utl::Logger* logger_;
};

std::unique_ptr<ThermalSolver> makeLumpedSolver(utl::Logger* logger)
{
  return std::make_unique<LumpedSolver>(logger);
}

}  // namespace thm
