// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

// Finite-volume conduction solver.  Placeholder: until the full 3D solver
// lands, the production factory returns the lumped reference solver so the
// rest of the flow (power extraction, leakage loop, derates, GUI) can run.

#include <memory>

#include "thm/ThermalSolver.h"
#include "utl/Logger.h"

namespace thm {

std::unique_ptr<ThermalSolver> makeFiniteVolumeSolver(utl::Logger* logger)
{
  return makeLumpedSolver(logger);
}

}  // namespace thm
