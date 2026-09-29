// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include <memory>
#include <vector>

#include "thm/ThermalConfig.h"
#include "thm/ThermalGrid.h"

namespace utl {
class Logger;
}

namespace thm {

struct SolveResult
{
  int iterations = 0;
  double residual = 0.0;
  bool converged = false;
  double runtime_s = 0.0;
  // Heat leaving through the boundaries (W), for energy-conservation checks.
  double boundary_heat_w = 0.0;
};

// Finite-volume conduction solver on a ThermalGrid.
//
// The grid geometry (tiles, stack layers, materials) is owned by the grid;
// the solver owns only its own scratch state (preconditioner, previous
// solution for warm starts).  One PowerMap per die is injected in that die's
// active layer.  Boundary conditions come from the config: lumped resistance
// from the top and bottom faces to ambient, adiabatic lateral faces.
class ThermalSolver
{
 public:
  virtual ~ThermalSolver() = default;

  // Steady state: div(k grad T) + q = 0.  If warm_start is true the current
  // grid values are the initial guess.
  virtual SolveResult solveSteady(const std::vector<PowerMap>& die_power,
                                  const ThermalConfig& config,
                                  ThermalGrid& grid,
                                  bool warm_start)
      = 0;

  // One implicit (backward Euler) transient step of dt_s seconds starting
  // from the temperatures in the grid.
  virtual SolveResult stepTransient(const std::vector<PowerMap>& die_power,
                                    const ThermalConfig& config,
                                    double dt_s,
                                    ThermalGrid& grid)
      = 0;

  // Dominant thermal time constant of the stack in seconds (for reporting
  // and for choosing transient step sizes).
  virtual double timeConstantS(const ThermalConfig& config,
                               const ThermalGrid& grid) const
      = 0;
};

// Factory for the production finite-volume solver (implemented in
// src/FiniteVolumeSolver.cpp).
std::unique_ptr<ThermalSolver> makeFiniteVolumeSolver(utl::Logger* logger);

// Closed-form placeholder used before the real solver is available and as a
// sanity reference in tests: T = ambient + P_tile * R_area / tile_area, no
// lateral spreading.
std::unique_ptr<ThermalSolver> makeLumpedSolver(utl::Logger* logger);

}  // namespace thm
