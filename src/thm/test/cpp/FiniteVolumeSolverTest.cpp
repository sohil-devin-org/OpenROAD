// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

// Physics sanity tests for the finite-volume thermal solver.  Every test
// states the physical law or limit it checks; tolerances are set by the
// discretization, not tuned to the solver.

#include <cmath>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "odb/geom.h"
#include "thm/ThermalConfig.h"
#include "thm/ThermalGrid.h"
#include "thm/ThermalSolver.h"
#include "utl/Logger.h"

namespace thm {

namespace {

constexpr int kDbu = 1000;
// 10 mm x 10 mm die: large enough that the vertical conduction resistance of
// the stack (~0.04 K/W) is small against the package resistances, so the
// lumped limits used below hold to well under a percent.
constexpr int kDieUm = 10000;
const odb::Rect kDie(0, 0, kDieUm* kDbu, kDieUm* kDbu);
constexpr double kDieAreaM2 = 1e-4;
// Same vertical refinement as Thermal::ensureGrid().
constexpr int kCellsPerLayer = 3;

class FiniteVolumeSolverTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    // Tight linear-solver tolerance so the checks below measure the
    // discretization, not the iterative solver.
    config_.solver_tolerance = 1e-10;
    config_.solver_max_iterations = 20000;
    solver_ = makeFiniteVolumeSolver(&logger_);
    lumped_ = makeLumpedSolver(&logger_);
  }

  ThermalGrid makeGrid(const ThermalConfig& config, int nx, int ny)
  {
    ThermalGrid grid;
    grid.reset(nx, ny, kDie, kDbu, config.buildStack(), kCellsPerLayer);
    grid.fill(config.ambient_c);
    return grid;
  }

  static std::vector<PowerMap> uniformPower(int nx,
                                            int ny,
                                            double total_w,
                                            int dies = 1,
                                            int powered_die = 0)
  {
    std::vector<PowerMap> maps(dies, PowerMap(nx, ny));
    for (double& v : maps[powered_die].values()) {
      v = total_w / maps[powered_die].size();
    }
    return maps;
  }

  static std::vector<PowerMap> pointPower(int nx,
                                          int ny,
                                          int x,
                                          int y,
                                          double power_w)
  {
    std::vector<PowerMap> maps(1, PowerMap(nx, ny));
    maps[0].at(x, y) = power_w;
    return maps;
  }

  // Gaussian blob of total power total_w centred on the die with standard
  // deviation sigma_tiles measured in units of the die width.
  static std::vector<PowerMap> gaussianPower(int nx,
                                             int ny,
                                             double total_w,
                                             double sigma_rel)
  {
    std::vector<PowerMap> maps(1, PowerMap(nx, ny));
    const double sigma = sigma_rel * nx;
    double sum = 0.0;
    for (int y = 0; y < ny; ++y) {
      for (int x = 0; x < nx; ++x) {
        const double ddx = x + 0.5 - nx / 2.0;
        const double ddy = (y + 0.5 - ny / 2.0) * nx / ny;
        const double v
            = std::exp(-(ddx * ddx + ddy * ddy) / (2 * sigma * sigma));
        maps[0].at(x, y) = v;
        sum += v;
      }
    }
    for (double& v : maps[0].values()) {
      v *= total_w / sum;
    }
    return maps;
  }

  // Temperature rise of the active layer of a die above ambient.
  static std::vector<double> rise(const ThermalGrid& grid,
                                  const ThermalConfig& config,
                                  int die = 0)
  {
    std::vector<double> r = grid.activeLayer(die);
    for (double& v : r) {
      v -= config.ambient_c;
    }
    return r;
  }

  // Package configuration with a weak heat sink so that the lateral
  // screening length is a few mm and hot spots are resolved by the grid.
  static ThermalConfig spreadingConfig(const ThermalConfig& base)
  {
    ThermalConfig c = base;
    c.top_resistance_k_w = 2.0;
    c.bottom_resistance_k_w = 1000.0;
    return c;
  }

  utl::Logger logger_;
  ThermalConfig config_;
  std::unique_ptr<ThermalSolver> solver_;
  std::unique_ptr<ThermalSolver> lumped_;
};

}  // namespace

// 1. Without heat sources the steady conduction equation with resistive
//    boundaries to ambient has the unique solution T = T_ambient.
TEST_F(FiniteVolumeSolverTest, ZeroPowerGivesAmbient)
{
  const int n = 16;
  ThermalGrid grid = makeGrid(config_, n, n);
  grid.fill(config_.ambient_c + 30.0);  // stale warm-start data
  auto maps = uniformPower(n, n, 0.0);
  const SolveResult r = solver_->solveSteady(maps, config_, grid, false);
  EXPECT_TRUE(r.converged);
  EXPECT_DOUBLE_EQ(r.boundary_heat_w, 0.0);
  for (double t : grid.values()) {
    EXPECT_DOUBLE_EQ(t, config_.ambient_c);
  }
}

// 2. Energy conservation: in steady state all the injected power must leave
//    through the package resistances (the lateral faces are adiabatic).
TEST_F(FiniteVolumeSolverTest, EnergyIsConserved)
{
  const int n = 32;
  ThermalGrid grid = makeGrid(config_, n, n);
  auto maps = uniformPower(n, n, 0.7);
  maps[0].at(5, 9) += 0.3;  // add a hot spot
  const double total = maps[0].total();
  const SolveResult r = solver_->solveSteady(maps, config_, grid, false);
  EXPECT_TRUE(r.converged);
  EXPECT_NEAR(r.boundary_heat_w, total, 1e-6 * total);
}

// 3. The conduction equation with constant conductivity is linear in the
//    source: doubling every power doubles the rise above ambient everywhere.
TEST_F(FiniteVolumeSolverTest, LinearInPower)
{
  const int n = 16;
  ThermalConfig config = spreadingConfig(config_);
  ThermalGrid grid = makeGrid(config, n, n);
  auto maps = pointPower(n, n, 4, 11, 1.0);
  solver_->solveSteady(maps, config, grid, false);
  const std::vector<double> t1 = grid.values();
  auto maps2 = pointPower(n, n, 4, 11, 2.0);
  solver_->solveSteady(maps2, config, grid, false);
  const std::vector<double> t2 = grid.values();
  const double peak_rise = grid.peak(0) - config.ambient_c;
  ASSERT_GT(peak_rise, 0.0);
  for (size_t i = 0; i < t1.size(); ++i) {
    const double rise1 = t1[i] - config.ambient_c;
    const double rise2 = t2[i] - config.ambient_c;
    EXPECT_NEAR(rise2, 2.0 * rise1, 1e-6 * peak_rise);
  }
}

// 4. Uniform power on adiabatic lateral faces is a one-dimensional problem:
//    no lateral gradient, and the rise equals P * R_package (the lumped
//    reference) up to the small vertical conduction resistance of the stack.
TEST_F(FiniteVolumeSolverTest, UniformPowerMatchesLumpedSolver)
{
  const int n = 16;
  const double p = 5.0;
  ThermalGrid grid = makeGrid(config_, n, n);
  auto maps = uniformPower(n, n, p);
  const SolveResult r = solver_->solveSteady(maps, config_, grid, false);
  EXPECT_TRUE(r.converged);
  ThermalGrid reference = makeGrid(config_, n, n);
  lumped_->solveSteady(maps, config_, reference, false);
  const double rise_fv = grid.peak(0) - config_.ambient_c;
  const double rise_lumped = reference.peak(0) - config_.ambient_c;
  EXPECT_NEAR(rise_fv, rise_lumped, 0.01 * rise_lumped);
  EXPECT_NEAR(grid.average(0), grid.peak(0), 1e-8 * rise_fv);
  // Gradient in C/mm scaled by the die width is the total lateral variation.
  EXPECT_LT(grid.maxGradientCPerMm(0) * kDieUm * 1e-3, 1e-6 * rise_fv);
}

// 5. The geometry, materials and boundary conditions are mirror symmetric,
//    so mirroring the power map mirrors the temperature field.
TEST_F(FiniteVolumeSolverTest, MirroredPowerGivesMirroredTemperature)
{
  const int n = 24;
  ThermalConfig config = spreadingConfig(config_);
  ThermalGrid grid = makeGrid(config, n, n);
  auto maps = pointPower(n, n, 3, 7, 1.0);
  maps[0].at(10, 15) = 0.4;
  solver_->solveSteady(maps, config, grid, false);
  const std::vector<double> t = rise(grid, config);
  const double peak = grid.peak(0) - config.ambient_c;

  std::vector<PowerMap> mirrored(1, PowerMap(n, n));
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      mirrored[0].at(n - 1 - x, y) = maps[0].at(x, y);
    }
  }
  ThermalGrid grid2 = makeGrid(config, n, n);
  solver_->solveSteady(mirrored, config, grid2, false);
  const std::vector<double> t2 = rise(grid2, config);
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      EXPECT_NEAR(t[y * n + x], t2[y * n + (n - 1 - x)], 1e-6 * peak);
    }
  }
}

// 6. A point source in a plate that loses heat to ambient through an areal
//    resistance obeys the screened Poisson equation; the rise decays as the
//    modified Bessel function K0(r / lambda) with the screening length
//    lambda = sqrt(k t r_area) of ThermalConfig::screeningLengthM().  The
//    peak must sit on the source tile and the rise must fall monotonically
//    with distance.  The decay length is extracted from the asymptotic form
//    K0(x) ~ exp(-x) / sqrt(x) between r = lambda and r = 2 lambda; the 3-D
//    stack (BEOL layer, finite silicon thickness, adiabatic edges) is not
//    exactly the thin-plate model, so agreement within a factor of two is
//    the meaningful check.
TEST_F(FiniteVolumeSolverTest, PointSourceDecaysWithScreeningLength)
{
  const int n = 64;
  ThermalConfig config = spreadingConfig(config_);
  ThermalGrid grid = makeGrid(config, n, n);
  const int xc = n / 2;
  const int yc = n / 2;
  auto maps = pointPower(n, n, xc, yc, 1.0);
  const SolveResult r = solver_->solveSteady(maps, config, grid, false);
  EXPECT_TRUE(r.converged);
  const std::vector<double> t = rise(grid, config);

  // Peak on the source tile.
  const double peak = t[yc * n + xc];
  for (double v : t) {
    EXPECT_LE(v, peak);
  }
  // Monotonic decay along the row and the column through the source.
  for (int x = xc; x + 1 < n; ++x) {
    EXPECT_LE(t[yc * n + x + 1], t[yc * n + x] + 1e-9 * peak);
  }
  for (int x = xc; x > 0; --x) {
    EXPECT_LE(t[yc * n + x - 1], t[yc * n + x] + 1e-9 * peak);
  }
  for (int y = yc; y + 1 < n; ++y) {
    EXPECT_LE(t[(y + 1) * n + xc], t[y * n + xc] + 1e-9 * peak);
  }
  for (int y = yc; y > 0; --y) {
    EXPECT_LE(t[(y - 1) * n + xc], t[y * n + xc] + 1e-9 * peak);
  }

  const double lambda = config.screeningLengthM(kDieAreaM2);
  const double dx = grid.dx();
  const int c1 = static_cast<int>(std::lround(lambda / dx));
  const int c2 = 2 * c1;
  ASSERT_GE(c1, 4);           // lambda is resolved by the grid
  ASSERT_LT(xc + c2, n - 4);  // and fits on the die
  const double r1 = c1 * dx;
  const double r2 = c2 * dx;
  const double t1 = t[yc * n + xc + c1];
  const double t2 = t[yc * n + xc + c2];
  const double lambda_fit
      = (r2 - r1) / std::log((t1 * std::sqrt(r1)) / (t2 * std::sqrt(r2)));
  EXPECT_GT(lambda_fit, 0.5 * lambda);
  EXPECT_LT(lambda_fit, 2.0 * lambda);
}

// 7. For uniform power the rise is P * R_package; doubling both package
//    resistances doubles it (the vertical stack resistance is negligible on
//    this die).
TEST_F(FiniteVolumeSolverTest, RiseScalesWithBoundaryResistance)
{
  const int n = 16;
  ThermalGrid grid = makeGrid(config_, n, n);
  auto maps = uniformPower(n, n, 2.0);
  solver_->solveSteady(maps, config_, grid, false);
  const double rise1 = grid.peak(0) - config_.ambient_c;

  ThermalConfig doubled = config_;
  doubled.top_resistance_k_w *= 2.0;
  doubled.bottom_resistance_k_w *= 2.0;
  ThermalGrid grid2 = makeGrid(doubled, n, n);
  solver_->solveSteady(maps, doubled, grid2, false);
  const double rise2 = grid2.peak(0) - doubled.ambient_c;
  EXPECT_NEAR(rise2, 2.0 * rise1, 1e-3 * rise2);
}

// 8. Grid convergence: a smooth (Gaussian) source resolved by both grids
//    gives the same peak within the O(h^2) discretization error.
TEST_F(FiniteVolumeSolverTest, PeakConvergesUnderGridRefinement)
{
  ThermalConfig config = spreadingConfig(config_);
  ThermalGrid coarse = makeGrid(config, 16, 16);
  solver_->solveSteady(
      gaussianPower(16, 16, 1.0, 0.125), config, coarse, false);
  ThermalGrid fine = makeGrid(config, 32, 32);
  solver_->solveSteady(gaussianPower(32, 32, 1.0, 0.125), config, fine, false);
  const double rise_coarse = coarse.peak(0) - config.ambient_c;
  const double rise_fine = fine.peak(0) - config.ambient_c;
  EXPECT_NEAR(rise_coarse, rise_fine, 0.03 * rise_fine);
}

// 9. Step response: the stack is dominated by the silicon heat capacity
//    discharging through the package resistance, a first-order system with
//    tau = C_total * R_total that reaches 1 - 1/e of the steady rise at
//    t = tau and the steady solution for t >> tau.  The heat sink is on the
//    silicon side so the heat has to charge the silicon on its way out.
TEST_F(FiniteVolumeSolverTest, TransientStepResponse)
{
  const int n = 8;
  ThermalConfig config = config_;
  config.heat_sink_on_bottom = true;
  config.top_resistance_k_w = 2.0;
  config.bottom_resistance_k_w = 1000.0;
  ThermalGrid grid = makeGrid(config, n, n);
  auto maps = uniformPower(n, n, 1.0);

  const double tau = solver_->timeConstantS(config, grid);
  ASSERT_GT(tau, 0.0);
  ThermalGrid steady = makeGrid(config, n, n);
  solver_->solveSteady(maps, config, steady, false);
  const double rise_ss = steady.peak(0) - config.ambient_c;

  const int steps_per_tau = 50;
  const double dt = tau / steps_per_tau;
  double rise_1tau = 0.0;
  for (int i = 1; i <= 10 * steps_per_tau; ++i) {
    const SolveResult r = solver_->stepTransient(maps, config, dt, grid);
    EXPECT_TRUE(r.converged);
    if (i == steps_per_tau) {
      rise_1tau = grid.peak(0) - config.ambient_c;
    }
  }
  EXPECT_NEAR(rise_1tau / rise_ss, 1.0 - std::exp(-1.0), 0.05);
  EXPECT_NEAR(grid.peak(0), steady.peak(0), 1e-3 * rise_ss);
  // In steady state the boundary heat equals the input power.
  const SolveResult last = solver_->stepTransient(maps, config, dt, grid);
  EXPECT_NEAR(last.boundary_heat_w, 1.0, 1e-3);
}

// 10. Two-die stack with the heat sink on the bottom (silicon side of die 0)
//     and power on die 1 (top): the heat has to cross the thinned die, the
//     bond layer and the BEOL of die 0, so die 0 heats up as well and the
//     drop between the two active layers is q_down * R_series where R_series
//     is the sum of the vertical link resistances between the two active
//     cells (each link: dz_i/(2 k_i) + dz_j/(2 k_j), divided by area).
//     Halving the bond conductivity increases the drop by exactly the added
//     bond resistance.
TEST_F(FiniteVolumeSolverTest, TwoDieStackVerticalDrop)
{
  const int n = 8;
  ThermalConfig config = config_;
  config.two_die = true;
  config.heat_sink_on_bottom = true;
  config.top_resistance_k_w = 1.0;
  config.bottom_resistance_k_w = 1000.0;
  const double p = 1.0;

  auto solve_drop = [&](const ThermalConfig& c, double& q_down) {
    ThermalGrid grid = makeGrid(c, n, n);
    EXPECT_EQ(grid.numDies(), 2);
    auto maps = uniformPower(n, n, p, /*dies=*/2, /*powered_die=*/1);
    const SolveResult r = solver_->solveSteady(maps, c, grid, false);
    EXPECT_TRUE(r.converged);
    EXPECT_NEAR(r.boundary_heat_w, p, 1e-6 * p);
    const double rise1 = grid.average(1) - c.ambient_c;
    const double rise0 = grid.average(0) - c.ambient_c;
    EXPECT_GT(rise0, 0.0);
    EXPECT_GT(rise1, rise0);
    // The top face (bottom_resistance_k_w when the sink is on the bottom)
    // is attached to the active cells of die 1.
    const double q_up = rise1 / c.bottom_resistance_k_w;
    q_down = p - q_up;
    double r_series = 0.0;
    for (int z = grid.activeZ(0); z < grid.activeZ(1); ++z) {
      r_series += grid.dz(z) / (2.0 * grid.conductivity(z))
                  + grid.dz(z + 1) / (2.0 * grid.conductivity(z + 1));
    }
    r_series /= kDieAreaM2;
    const double drop = rise1 - rise0;
    EXPECT_NEAR(drop, q_down * r_series, 1e-3 * drop);
    // The rise of die 0 is the heat leaving through the sink times the
    // sink resistance plus the conduction through die 0's own stack.
    EXPECT_GT(rise0, q_down * c.top_resistance_k_w);
    return drop;
  };

  double q_down1 = 0.0;
  const double drop1 = solve_drop(config, q_down1);
  ThermalConfig half_bond = config;
  half_bond.bond.conductivity_w_mk *= 0.5;
  double q_down2 = 0.0;
  const double drop2 = solve_drop(half_bond, q_down2);
  const double added_bond_r
      = config.bond.thickness_m
            / (half_bond.bond.conductivity_w_mk * kDieAreaM2)
        - config.bond.thickness_m
              / (config.bond.conductivity_w_mk * kDieAreaM2);
  EXPECT_NEAR(drop2 - drop1, q_down2 * added_bond_r, 1e-2 * (drop2 - drop1));

  // Default package (sink on top of die 1): die 0 is still heated by the
  // fraction of the heat that leaks to the bottom resistance.
  ThermalConfig top_sink = config_;
  top_sink.two_die = true;
  ThermalGrid grid = makeGrid(top_sink, n, n);
  auto maps = uniformPower(n, n, p, 2, 1);
  solver_->solveSteady(maps, top_sink, grid, false);
  const double rise0 = grid.average(0) - top_sink.ambient_c;
  const double rise1 = grid.average(1) - top_sink.ambient_c;
  EXPECT_GT(rise0, 0.0);
  EXPECT_GT(rise1, rise0);
}

// 11. Temperature-dependent conductivity: silicon conducts worse when hot
//     (k ~ T^-1.3, Glassbrenner & Slack 1964), so with the heat flowing
//     through the silicon the rise is higher than with constant k, while
//     energy is still conserved.  Below ~300 K the correction reverses.
TEST_F(FiniteVolumeSolverTest, TemperatureDependentConductivityRaisesPeak)
{
  const int n = 16;
  ThermalConfig config = spreadingConfig(config_);
  config.heat_sink_on_bottom = true;
  config.ambient_c = 60.0;
  // 0.5 W keeps the hot spot below ~100 C so the power law stays in the
  // regime the comparison below assumes.
  const double p = 0.5;
  ThermalGrid linear = makeGrid(config, n, n);
  auto maps = pointPower(n, n, n / 2, n / 2, p);
  solver_->solveSteady(maps, config, linear, false);
  const double rise_linear = linear.peak(0) - config.ambient_c;

  ThermalConfig nonlinear = config;
  nonlinear.temperature_dependent_conductivity = true;
  ThermalGrid grid = makeGrid(nonlinear, n, n);
  const SolveResult r = solver_->solveSteady(maps, nonlinear, grid, false);
  EXPECT_TRUE(r.converged);
  EXPECT_NEAR(r.boundary_heat_w, p, 1e-6 * p);
  const double rise_nonlinear = grid.peak(0) - nonlinear.ambient_c;
  EXPECT_GT(rise_nonlinear, rise_linear);
  // The spreading resistance in the silicon scales with 1/k.  Between the
  // ambient (60 C) and the hot spot (< 100 C) k(T)/k(300 K) lies between
  // (300 / 333)^1.3 ~ 0.87 and (300 / 373)^1.3 ~ 0.75, so the rise grows by
  // more than a few percent but less than 1/0.75.
  EXPECT_LT(rise_nonlinear, 1.3 * rise_linear);
  EXPECT_LT(grid.peak(0), 100.0);
}

// 12. Warm start from the converged solution is a no-op: the same
//     temperatures with (almost) no additional iterations.
TEST_F(FiniteVolumeSolverTest, WarmStartReusesSolution)
{
  const int n = 32;
  ThermalConfig config = spreadingConfig(config_);
  ThermalGrid grid = makeGrid(config, n, n);
  auto maps = pointPower(n, n, 5, 20, 1.0);
  const SolveResult cold = solver_->solveSteady(maps, config, grid, false);
  EXPECT_TRUE(cold.converged);
  EXPECT_GT(cold.iterations, 1);
  const std::vector<double> t = grid.values();
  const SolveResult warm = solver_->solveSteady(maps, config, grid, true);
  EXPECT_TRUE(warm.converged);
  EXPECT_LE(warm.iterations, 2);
  const double peak = grid.peak(0) - config.ambient_c;
  for (size_t i = 0; i < t.size(); ++i) {
    EXPECT_NEAR(grid.values()[i], t[i], 1e-8 * peak);
  }
}

// 13. Production-size grid: 64x64 tiles on a two-die stack (~10 z cells)
//     solves in well under a second with the default tolerance, and two
//     cold solves produce bit-identical temperatures (fixed loop order, no
//     unordered reductions).
TEST_F(FiniteVolumeSolverTest, LargeGridIsFastAndDeterministic)
{
  const int n = 64;
  ThermalConfig config = spreadingConfig(config_);
  config.two_die = true;
  config.solver_tolerance = 1e-8;
  ThermalGrid grid = makeGrid(config, n, n);
  EXPECT_GE(grid.nz(), 8);
  auto maps = gaussianPower(n, n, 2.0, 0.1);
  maps.push_back(uniformPower(n, n, 1.0)[0]);
  const SolveResult r = solver_->solveSteady(maps, config, grid, false);
  EXPECT_TRUE(r.converged);
  EXPECT_NEAR(r.boundary_heat_w, 3.0, 1e-6 * 3.0);
  EXPECT_LT(r.runtime_s, 1.0);
  const std::vector<double> first = grid.values();

  ThermalGrid again = makeGrid(config, n, n);
  solver_->solveSteady(maps, config, again, false);
  EXPECT_EQ(again.values(), first);
}

}  // namespace thm
