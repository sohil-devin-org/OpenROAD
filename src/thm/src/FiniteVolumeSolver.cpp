// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

// Regular-grid finite-volume conduction solver on a ThermalGrid.
//
// Discretization
//   Every grid cell (x, y, z) is a control volume dx * dy * dz(z) with the
//   material of its stack layer.  Integrating div(k grad T) + q = 0 over the
//   cell gives a 7-point conductance stencil:
//     sum_j G_ij (T_j - T_i) + G_bnd_i (T_amb - T_i) + Q_i = 0
//   with lateral conductances G = k_h * (face area) / (centre distance) and
//   vertical conductances G = A / (dz_i / (2 k_i) + dz_j / (2 k_j)), i.e. the
//   harmonic mean of the two half-cell conductivities (Patankar, "Numerical
//   Heat Transfer and Fluid Flow", 1980, sec. 4.2-3).
//
// Boundary conditions
//   The lumped package resistances of the ThermalConfig connect the cells of
//   the top and bottom z layer to ambient, distributed by area over the
//   n_tiles lateral tiles: R_tile = R_total * n_tiles.  The lumped
//   resistance is attached directly to the boundary cell node; the half-cell
//   conduction resistance of the boundary layer is not added in series so
//   that R_top / R_bottom keep the meaning "total node-to-ambient
//   resistance" used by the lumped reference solver and by
//   ThermalConfig::screeningLengthM().  Lateral faces are adiabatic.
//
// Linear solver
//   The matrix is symmetric positive definite (diagonally dominant with
//   positive boundary conductances), so the temperature rise theta = T -
//   T_amb is obtained with a preconditioned conjugate gradient iteration.
//   The preconditioner is block Jacobi over vertical columns (an exact
//   tridiagonal solve per lateral tile), which removes the stiffness of the
//   thin, highly anisotropic stack layers.  All loops run in a fixed serial
//   order so the result is bit-for-bit deterministic.
//
// Temperature-dependent conductivity (optional)
//   For layers of bulk silicon k(T) = k_ref * (300 K / T)^n with n =
//   silicon_conductivity_exponent (1.3 by default); the T^-1.3 law fits the
//   measured conductivity of intrinsic silicon between 300 K and 1000 K
//   (C. J. Glassbrenner, G. A. Slack, "Thermal Conductivity of Silicon and
//   Germanium from 3 K to the Melting Point", Phys. Rev. 134, A1058, 1964).
//   The nonlinearity is handled by Picard iteration: conductances are
//   rebuilt from the latest temperatures and the linear system re-solved
//   until the temperature field stops changing.
//
// Transient
//   One backward-Euler step of dt: (C / dt + A) theta_new = C / dt theta_old
//   + Q, where C is the volumetric heat capacity times the cell volume.
//   Backward Euler is unconditionally stable for this SPD system.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "thm/ThermalSolver.h"
#include "utl/Logger.h"

namespace thm {

namespace {

constexpr double kKelvinOffset = 273.15;
constexpr double kReferenceTempK = 300.0;
// Picard iteration on k(T): stop when the largest temperature change is
// below this (K) or after the iteration cap.
constexpr double kPicardToleranceK = 1e-3;
constexpr int kPicardMaxIterations = 8;

// Stack layers named "silicon*" by ThermalConfig::buildStack() are bulk
// silicon and get the temperature-dependent conductivity.
bool isSiliconLayer(const StackLayer& layer)
{
  return layer.name.rfind("silicon", 0) == 0;
}

// Conductance network of the discretized stack.  For cell i, gx[i] couples
// i to its +x neighbour, gy[i] to its +y neighbour and gz[i] to the cell
// above; entries on the far faces are zero.
struct System
{
  int nx = 0;
  int ny = 0;
  int nz = 0;
  std::vector<double> gx;
  std::vector<double> gy;
  std::vector<double> gz;
  std::vector<double> g_bnd;     // conductance to ambient (W/K)
  std::vector<double> capacity;  // heat capacity of the cell (J/K)
  std::vector<double> self;      // g_bnd + C/dt: terms not shared with a
                                 // neighbour
  std::vector<double> diag;      // self + all neighbour conductances
  // LDL^T factors of the per-column tridiagonal preconditioner.
  std::vector<double> ldl_d;
  std::vector<double> ldl_l;

  int size() const { return nx * ny * nz; }
  int index(int x, int y, int z) const { return (z * ny + y) * nx + x; }
};

}  // namespace

class FiniteVolumeSolver : public ThermalSolver
{
 public:
  explicit FiniteVolumeSolver(utl::Logger* logger) : logger_(logger) {}

  SolveResult solveSteady(const std::vector<PowerMap>& die_power,
                          const ThermalConfig& config,
                          ThermalGrid& grid,
                          bool warm_start) override;

  SolveResult stepTransient(const std::vector<PowerMap>& die_power,
                            const ThermalConfig& config,
                            double dt_s,
                            ThermalGrid& grid) override;

  double timeConstantS(const ThermalConfig& config,
                       const ThermalGrid& grid) const override;

 private:
  bool checkInputs(const std::vector<PowerMap>& die_power,
                   const ThermalConfig& config,
                   const ThermalGrid& grid) const;
  void buildSource(const std::vector<PowerMap>& die_power,
                   const ThermalGrid& grid,
                   std::vector<double>& source) const;
  void buildSystem(const ThermalConfig& config,
                   const ThermalGrid& grid,
                   const std::vector<double>& rise,
                   double dt_s,
                   System& sys) const;
  static void factorPreconditioner(System& sys);
  static void applyMatrix(const System& sys,
                          const std::vector<double>& x,
                          std::vector<double>& y);
  static void applyPreconditioner(const System& sys,
                                  const std::vector<double>& r,
                                  std::vector<double>& z);
  void conjugateGradient(const System& sys,
                         const std::vector<double>& rhs,
                         std::vector<double>& x,
                         double tolerance,
                         int max_iterations,
                         SolveResult& result);
  static double boundaryHeat(const System& sys,
                             const std::vector<double>& rise);
  static void storeRise(const ThermalConfig& config,
                        const std::vector<double>& rise,
                        ThermalGrid& grid);
  static void loadRise(const ThermalConfig& config,
                       const ThermalGrid& grid,
                       std::vector<double>& rise);

  utl::Logger* logger_;
  // Scratch vectors reused between solves.
  std::vector<double> r_;
  std::vector<double> z_;
  std::vector<double> p_;
  std::vector<double> ap_;
};

bool FiniteVolumeSolver::checkInputs(const std::vector<PowerMap>& die_power,
                                     const ThermalConfig& config,
                                     const ThermalGrid& grid) const
{
  if (grid.size() <= 0) {
    logger_->error(utl::THM, 100, "Thermal grid has no cells.");
    return false;
  }
  for (size_t d = 0; d < die_power.size(); ++d) {
    const PowerMap& map = die_power[d];
    if (map.size() == 0) {
      continue;
    }
    if (map.nx() != grid.nx() || map.ny() != grid.ny()) {
      logger_->error(utl::THM,
                     101,
                     "Power map of die {} is {}x{} but the thermal grid is "
                     "{}x{}.",
                     d,
                     map.nx(),
                     map.ny(),
                     grid.nx(),
                     grid.ny());
      return false;
    }
  }
  if (config.top_resistance_k_w <= 0 && config.bottom_resistance_k_w <= 0) {
    logger_->error(utl::THM,
                   102,
                   "At least one of the top and bottom package resistances "
                   "must be positive; the stack has no path to ambient.");
    return false;
  }
  return true;
}

void FiniteVolumeSolver::buildSource(const std::vector<PowerMap>& die_power,
                                     const ThermalGrid& grid,
                                     std::vector<double>& source) const
{
  source.assign(grid.size(), 0.0);
  for (size_t d = 0; d < die_power.size(); ++d) {
    const PowerMap& map = die_power[d];
    const int z = grid.activeZ(static_cast<int>(d));
    if (z < 0 || map.size() == 0) {
      continue;
    }
    for (int y = 0; y < grid.ny(); ++y) {
      for (int x = 0; x < grid.nx(); ++x) {
        source[grid.index(x, y, z)] += map.at(x, y);
      }
    }
  }
}

void FiniteVolumeSolver::buildSystem(const ThermalConfig& config,
                                     const ThermalGrid& grid,
                                     const std::vector<double>& rise,
                                     double dt_s,
                                     System& sys) const
{
  sys.nx = grid.nx();
  sys.ny = grid.ny();
  sys.nz = grid.nz();
  const int n = sys.size();
  sys.gx.assign(n, 0.0);
  sys.gy.assign(n, 0.0);
  sys.gz.assign(n, 0.0);
  sys.g_bnd.assign(n, 0.0);
  sys.capacity.assign(n, 0.0);
  sys.self.assign(n, 0.0);
  sys.diag.assign(n, 0.0);

  const double dx = grid.dx();
  const double dy = grid.dy();
  const double tile_area = dx * dy;

  // Per-cell conductivity, optionally temperature dependent for silicon.
  std::vector<double> k(n, 0.0);
  for (int z = 0; z < sys.nz; ++z) {
    const StackLayer& layer = grid.stack()[grid.layerOf(z)];
    const bool variable
        = config.temperature_dependent_conductivity && isSiliconLayer(layer);
    for (int y = 0; y < sys.ny; ++y) {
      for (int x = 0; x < sys.nx; ++x) {
        const int i = sys.index(x, y, z);
        double k_cell = grid.conductivity(z);
        if (variable) {
          const double t_k
              = std::max(1.0, config.ambient_c + rise[i] + kKelvinOffset);
          k_cell *= std::pow(kReferenceTempK / t_k,
                             config.silicon_conductivity_exponent);
        }
        k[i] = k_cell;
        sys.capacity[i] = grid.heatCapacity(z) * tile_area * grid.dz(z);
      }
    }
  }

  // Package resistances: the top face of the stack sees top_resistance_k_w
  // unless the heat sink is on the bottom, in which case the two swap.
  const double r_top_face = config.heat_sink_on_bottom
                                ? config.bottom_resistance_k_w
                                : config.top_resistance_k_w;
  const double r_bottom_face = config.heat_sink_on_bottom
                                   ? config.top_resistance_k_w
                                   : config.bottom_resistance_k_w;
  const double n_tiles = static_cast<double>(sys.nx) * sys.ny;
  const double g_top_tile = r_top_face > 0 ? 1.0 / (r_top_face * n_tiles) : 0;
  const double g_bottom_tile
      = r_bottom_face > 0 ? 1.0 / (r_bottom_face * n_tiles) : 0;

  for (int z = 0; z < sys.nz; ++z) {
    const double dz = grid.dz(z);
    for (int y = 0; y < sys.ny; ++y) {
      for (int x = 0; x < sys.nx; ++x) {
        const int i = sys.index(x, y, z);
        if (x + 1 < sys.nx) {
          const int j = i + 1;
          const double k_h = 2.0 * k[i] * k[j] / (k[i] + k[j]);
          sys.gx[i] = k_h * dy * dz / dx;
        }
        if (y + 1 < sys.ny) {
          const int j = i + sys.nx;
          const double k_h = 2.0 * k[i] * k[j] / (k[i] + k[j]);
          sys.gy[i] = k_h * dx * dz / dy;
        }
        if (z + 1 < sys.nz) {
          const int j = i + sys.nx * sys.ny;
          const double r = dz / (2.0 * k[i]) + grid.dz(z + 1) / (2.0 * k[j]);
          sys.gz[i] = tile_area / r;
        }
        if (z == 0) {
          sys.g_bnd[i] += g_bottom_tile;
        }
        if (z + 1 == sys.nz) {
          sys.g_bnd[i] += g_top_tile;
        }
      }
    }
  }

  for (int z = 0; z < sys.nz; ++z) {
    for (int y = 0; y < sys.ny; ++y) {
      for (int x = 0; x < sys.nx; ++x) {
        const int i = sys.index(x, y, z);
        sys.self[i] = sys.g_bnd[i];
        if (dt_s > 0) {
          sys.self[i] += sys.capacity[i] / dt_s;
        }
        double d = sys.self[i] + sys.gx[i] + sys.gy[i] + sys.gz[i];
        if (x > 0) {
          d += sys.gx[i - 1];
        }
        if (y > 0) {
          d += sys.gy[i - sys.nx];
        }
        if (z > 0) {
          d += sys.gz[i - sys.nx * sys.ny];
        }
        sys.diag[i] = d;
      }
    }
  }
  factorPreconditioner(sys);
}

// LDL^T factorization of the tridiagonal (diag, -gz) system of every vertical
// column; the blocks are SPD so no pivoting is needed.
void FiniteVolumeSolver::factorPreconditioner(System& sys)
{
  const int n = sys.size();
  const int plane = sys.nx * sys.ny;
  sys.ldl_d.assign(n, 0.0);
  sys.ldl_l.assign(n, 0.0);
  for (int y = 0; y < sys.ny; ++y) {
    for (int x = 0; x < sys.nx; ++x) {
      int i = sys.index(x, y, 0);
      sys.ldl_d[i] = sys.diag[i];
      for (int z = 1; z < sys.nz; ++z) {
        const int below = i;
        i += plane;
        const double off = -sys.gz[below];
        sys.ldl_l[i] = off / sys.ldl_d[below];
        sys.ldl_d[i] = sys.diag[i] - off * sys.ldl_l[i];
      }
    }
  }
}

void FiniteVolumeSolver::applyMatrix(const System& sys,
                                     const std::vector<double>& x,
                                     std::vector<double>& y)
{
  const int n = sys.size();
  const int plane = sys.nx * sys.ny;
  y.resize(n);
  // y = A x assembled edge by edge: every conductance g between i and j
  // contributes g (x_i - x_j) to row i and g (x_j - x_i) to row j.
  for (int i = 0; i < n; ++i) {
    y[i] = sys.self[i] * x[i];
  }
  for (int z = 0; z < sys.nz; ++z) {
    for (int yy = 0; yy < sys.ny; ++yy) {
      for (int xx = 0; xx < sys.nx; ++xx) {
        const int i = sys.index(xx, yy, z);
        if (xx + 1 < sys.nx) {
          const double f = sys.gx[i] * (x[i + 1] - x[i]);
          y[i] -= f;
          y[i + 1] += f;
        }
        if (yy + 1 < sys.ny) {
          const double f = sys.gy[i] * (x[i + sys.nx] - x[i]);
          y[i] -= f;
          y[i + sys.nx] += f;
        }
        if (z + 1 < sys.nz) {
          const double f = sys.gz[i] * (x[i + plane] - x[i]);
          y[i] -= f;
          y[i + plane] += f;
        }
      }
    }
  }
}

void FiniteVolumeSolver::applyPreconditioner(const System& sys,
                                             const std::vector<double>& r,
                                             std::vector<double>& z)
{
  const int n = sys.size();
  const int plane = sys.nx * sys.ny;
  z.resize(n);
  for (int y = 0; y < sys.ny; ++y) {
    for (int x = 0; x < sys.nx; ++x) {
      // Forward substitution L w = r, then D, then backward L^T z = w.
      int i = sys.index(x, y, 0);
      z[i] = r[i];
      for (int k = 1; k < sys.nz; ++k) {
        const int below = i;
        i += plane;
        z[i] = r[i] - sys.ldl_l[i] * z[below];
      }
      z[i] /= sys.ldl_d[i];
      for (int k = sys.nz - 2; k >= 0; --k) {
        const int above = i;
        i -= plane;
        z[i] = z[i] / sys.ldl_d[i] - sys.ldl_l[above] * z[above];
      }
    }
  }
}

void FiniteVolumeSolver::conjugateGradient(const System& sys,
                                           const std::vector<double>& rhs,
                                           std::vector<double>& x,
                                           double tolerance,
                                           int max_iterations,
                                           SolveResult& result)
{
  const int n = sys.size();
  x.resize(n, 0.0);
  double rhs_norm2 = 0.0;
  double rhs_abs_sum = 0.0;
  for (int i = 0; i < n; ++i) {
    rhs_norm2 += rhs[i] * rhs[i];
    rhs_abs_sum += std::abs(rhs[i]);
  }
  if (rhs_norm2 == 0.0) {
    std::fill(x.begin(), x.end(), 0.0);
    result.converged = true;
    result.residual = 0.0;
    return;
  }
  const double rhs_norm = std::sqrt(rhs_norm2);

  applyMatrix(sys, x, r_);
  for (int i = 0; i < n; ++i) {
    r_[i] = rhs[i] - r_[i];
  }
  applyPreconditioner(sys, r_, z_);
  p_ = z_;
  double rz = 0.0;
  for (int i = 0; i < n; ++i) {
    rz += r_[i] * z_[i];
  }

  auto converged = [&](double& residual) {
    double r_norm2 = 0.0;
    double r_sum = 0.0;
    for (int i = 0; i < n; ++i) {
      r_norm2 += r_[i] * r_[i];
      r_sum += r_[i];
    }
    residual = std::sqrt(r_norm2) / rhs_norm;
    // The sum of the residual is the net heat imbalance of the whole stack
    // (all internal fluxes cancel), so it is checked as well.
    return residual <= tolerance && std::abs(r_sum) <= tolerance * rhs_abs_sum;
  };

  result.converged = converged(result.residual);
  int iter = 0;
  while (!result.converged && iter < max_iterations) {
    ++iter;
    applyMatrix(sys, p_, ap_);
    double pap = 0.0;
    for (int i = 0; i < n; ++i) {
      pap += p_[i] * ap_[i];
    }
    if (pap <= 0.0) {
      break;
    }
    const double alpha = rz / pap;
    for (int i = 0; i < n; ++i) {
      x[i] += alpha * p_[i];
      r_[i] -= alpha * ap_[i];
    }
    if (converged(result.residual)) {
      result.converged = true;
      break;
    }
    applyPreconditioner(sys, r_, z_);
    double rz_new = 0.0;
    for (int i = 0; i < n; ++i) {
      rz_new += r_[i] * z_[i];
    }
    const double beta = rz_new / rz;
    rz = rz_new;
    for (int i = 0; i < n; ++i) {
      p_[i] = z_[i] + beta * p_[i];
    }
  }
  result.iterations += iter;
}

double FiniteVolumeSolver::boundaryHeat(const System& sys,
                                        const std::vector<double>& rise)
{
  double heat = 0.0;
  const int n = sys.size();
  for (int i = 0; i < n; ++i) {
    heat += sys.g_bnd[i] * rise[i];
  }
  return heat;
}

void FiniteVolumeSolver::storeRise(const ThermalConfig& config,
                                   const std::vector<double>& rise,
                                   ThermalGrid& grid)
{
  std::vector<double>& temps = grid.values();
  for (size_t i = 0; i < temps.size(); ++i) {
    temps[i] = config.ambient_c + rise[i];
  }
}

void FiniteVolumeSolver::loadRise(const ThermalConfig& config,
                                  const ThermalGrid& grid,
                                  std::vector<double>& rise)
{
  const std::vector<double>& temps = grid.values();
  rise.resize(temps.size());
  for (size_t i = 0; i < temps.size(); ++i) {
    rise[i] = temps[i] - config.ambient_c;
  }
}

SolveResult FiniteVolumeSolver::solveSteady(
    const std::vector<PowerMap>& die_power,
    const ThermalConfig& config,
    ThermalGrid& grid,
    bool warm_start)
{
  const auto start = std::chrono::steady_clock::now();
  SolveResult result;
  if (!checkInputs(die_power, config, grid)) {
    return result;
  }
  std::vector<double> source;
  buildSource(die_power, grid, source);

  std::vector<double> rise;
  if (warm_start) {
    loadRise(config, grid, rise);
  } else {
    rise.assign(grid.size(), 0.0);
  }

  System sys;
  const int picard_steps
      = config.temperature_dependent_conductivity ? kPicardMaxIterations : 1;
  std::vector<double> previous;
  for (int step = 0; step < picard_steps; ++step) {
    buildSystem(config, grid, rise, /*dt_s=*/0.0, sys);
    previous = rise;
    conjugateGradient(sys,
                      source,
                      rise,
                      config.solver_tolerance,
                      config.solver_max_iterations,
                      result);
    double max_change = 0.0;
    for (size_t i = 0; i < rise.size(); ++i) {
      max_change = std::max(max_change, std::abs(rise[i] - previous[i]));
    }
    if (picard_steps > 1) {
      debugPrint(logger_,
                 utl::THM,
                 "solver",
                 1,
                 "Picard step {}: max temperature change {:.3e} K, {} CG "
                 "iterations so far.",
                 step + 1,
                 max_change,
                 result.iterations);
    }
    if (max_change <= kPicardToleranceK) {
      break;
    }
  }

  storeRise(config, rise, grid);
  result.boundary_heat_w = boundaryHeat(sys, rise);
  result.runtime_s
      = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
  return result;
}

SolveResult FiniteVolumeSolver::stepTransient(
    const std::vector<PowerMap>& die_power,
    const ThermalConfig& config,
    double dt_s,
    ThermalGrid& grid)
{
  const auto start = std::chrono::steady_clock::now();
  SolveResult result;
  if (!checkInputs(die_power, config, grid)) {
    return result;
  }
  if (dt_s <= 0) {
    logger_->error(
        utl::THM, 103, "Transient time step must be positive ({} s).", dt_s);
    return result;
  }
  std::vector<double> source;
  buildSource(die_power, grid, source);
  std::vector<double> rise;
  loadRise(config, grid, rise);

  // Conductances are linearized at the temperatures of the start of the
  // step (semi-implicit treatment of k(T)).
  System sys;
  buildSystem(config, grid, rise, dt_s, sys);
  std::vector<double> rhs(source.size());
  for (size_t i = 0; i < rhs.size(); ++i) {
    rhs[i] = source[i] + sys.capacity[i] / dt_s * rise[i];
  }
  conjugateGradient(sys,
                    rhs,
                    rise,
                    config.solver_tolerance,
                    config.solver_max_iterations,
                    result);
  storeRise(config, rise, grid);
  result.boundary_heat_w = boundaryHeat(sys, rise);
  result.runtime_s
      = std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
  return result;
}

// Lumped estimate tau = C_total * R_total: the whole stack heat capacity
// discharging through the parallel package resistances.  This is the slowest
// mode of the stack; internal conduction modes are much faster.
double FiniteVolumeSolver::timeConstantS(const ThermalConfig& config,
                                         const ThermalGrid& grid) const
{
  const double g_top
      = config.top_resistance_k_w > 0 ? 1.0 / config.top_resistance_k_w : 0;
  const double g_bottom = config.bottom_resistance_k_w > 0
                              ? 1.0 / config.bottom_resistance_k_w
                              : 0;
  const double g = g_top + g_bottom;
  if (g <= 0) {
    return 0.0;
  }
  double capacity = 0.0;
  for (int z = 0; z < grid.nz(); ++z) {
    capacity += grid.heatCapacity(z) * grid.dz(z);
  }
  capacity *= grid.dx() * grid.dy() * grid.nx() * grid.ny();
  return capacity / g;
}

std::unique_ptr<ThermalSolver> makeFiniteVolumeSolver(utl::Logger* logger)
{
  return std::make_unique<FiniteVolumeSolver>(logger);
}

}  // namespace thm
