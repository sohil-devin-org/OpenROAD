// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include <cmath>
#include <vector>

#include "gtest/gtest.h"
#include "odb/geom.h"
#include "thm/PhysicsState.h"
#include "thm/ThermalConfig.h"
#include "thm/ThermalGrid.h"
#include "thm/ThermalSolver.h"
#include "utl/Logger.h"

namespace thm {

namespace {

constexpr int kDbu = 1000;
// 1 mm x 1 mm die.
const odb::Rect kDie(0, 0, 1000 * kDbu, 1000 * kDbu);

class ThermalCoreTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    config_.grid_x = 8;
    config_.grid_y = 8;
    grid_.reset(
        config_.grid_x, config_.grid_y, kDie, kDbu, config_.buildStack());
    solver_ = makeLumpedSolver(&logger_);
  }

  std::vector<PowerMap> uniformPower(double total_w)
  {
    std::vector<PowerMap> maps(1, PowerMap(config_.grid_x, config_.grid_y));
    for (double& v : maps[0].values()) {
      v = total_w / maps[0].size();
    }
    return maps;
  }

  utl::Logger logger_;
  ThermalConfig config_;
  ThermalGrid grid_;
  std::unique_ptr<ThermalSolver> solver_;
};

}  // namespace

TEST_F(ThermalCoreTest, StackHasActiveLayerPerDie)
{
  ThermalConfig single;
  const auto stack = single.buildStack();
  int active = 0;
  for (const StackLayer& l : stack) {
    active += l.is_active;
  }
  EXPECT_EQ(active, 1);

  ThermalConfig two;
  two.two_die = true;
  const auto stack2 = two.buildStack();
  active = 0;
  bool has_bond = false;
  for (const StackLayer& l : stack2) {
    active += l.is_active;
    has_bond |= l.name.rfind("bond", 0) == 0;
  }
  EXPECT_EQ(active, 2);
  EXPECT_TRUE(has_bond);
  ThermalGrid g;
  g.reset(4, 4, kDie, kDbu, stack2);
  EXPECT_EQ(g.numDies(), 2);
  EXPECT_GE(g.activeZ(0), 0);
  EXPECT_GT(g.activeZ(1), g.activeZ(0));
}

TEST_F(ThermalCoreTest, ZeroPowerGivesAmbient)
{
  auto maps = uniformPower(0.0);
  const SolveResult r = solver_->solveSteady(maps, config_, grid_, false);
  EXPECT_TRUE(r.converged);
  for (double t : grid_.values()) {
    EXPECT_DOUBLE_EQ(t, config_.ambient_c);
  }
}

TEST_F(ThermalCoreTest, UniformPowerMatchesLumpedResistance)
{
  const double p = 1.0;
  auto maps = uniformPower(p);
  solver_->solveSteady(maps, config_, grid_, false);
  const double g
      = 1.0 / config_.top_resistance_k_w + 1.0 / config_.bottom_resistance_k_w;
  const double expected = config_.ambient_c + p / g;
  EXPECT_NEAR(grid_.peak(0), expected, 1e-9);
  EXPECT_NEAR(grid_.average(0), expected, 1e-9);
  EXPECT_NEAR(grid_.maxGradientCPerMm(0), 0.0, 1e-12);
}

TEST_F(ThermalCoreTest, LinearInPower)
{
  auto maps = uniformPower(1.0);
  solver_->solveSteady(maps, config_, grid_, false);
  const double rise1 = grid_.peak(0) - config_.ambient_c;
  auto maps2 = uniformPower(2.0);
  solver_->solveSteady(maps2, config_, grid_, false);
  const double rise2 = grid_.peak(0) - config_.ambient_c;
  EXPECT_NEAR(rise2, 2.0 * rise1, 1e-9);
}

TEST_F(ThermalCoreTest, EnergyIsConserved)
{
  auto maps = uniformPower(0.7);
  const SolveResult r = solver_->solveSteady(maps, config_, grid_, false);
  EXPECT_NEAR(r.boundary_heat_w, 0.7, 1e-9);
}

TEST_F(ThermalCoreTest, PowerMapRectSplitsByOverlap)
{
  PowerMap map(2, 2);
  // Rect covering the left half of the die exactly.
  map.addRect(odb::Rect(0, 0, kDie.xMax() / 2, kDie.yMax()), kDie, 1.0);
  EXPECT_NEAR(map.at(0, 0), 0.5, 1e-9);
  EXPECT_NEAR(map.at(0, 1), 0.5, 1e-9);
  EXPECT_NEAR(map.at(1, 0), 0.0, 1e-9);
  EXPECT_NEAR(map.total(), 1.0, 1e-9);
  // Zero-area rect lands in the nearest tile.
  map.addRect(
      odb::Rect(900 * kDbu, 900 * kDbu, 900 * kDbu, 900 * kDbu), kDie, 0.25);
  EXPECT_NEAR(map.at(1, 1), 0.25, 1e-9);
}

TEST_F(ThermalCoreTest, TransientApproachesSteadyState)
{
  auto maps = uniformPower(1.0);
  const double tau = solver_->timeConstantS(config_, grid_);
  EXPECT_GT(tau, 0.0);
  grid_.fill(config_.ambient_c);
  const double dt = tau / 20.0;
  double t_1tau = 0.0;
  const int steps = static_cast<int>(10.0 * tau / dt);
  for (int i = 0; i < steps; ++i) {
    solver_->stepTransient(maps, config_, dt, grid_);
    if (i + 1 == steps / 10) {
      t_1tau = grid_.peak(0);
    }
  }
  ThermalGrid steady = grid_;
  solver_->solveSteady(maps, config_, steady, false);
  const double rise_ss = steady.peak(0) - config_.ambient_c;
  EXPECT_NEAR(grid_.peak(0), steady.peak(0), 1e-3 * rise_ss);
  // After one time constant a first-order system reaches ~63% of the step.
  EXPECT_NEAR(
      (t_1tau - config_.ambient_c) / rise_ss, 1.0 - std::exp(-1.0), 0.05);
}

TEST_F(ThermalCoreTest, ScreeningLengthScalesWithSqrtResistance)
{
  ThermalConfig a;
  ThermalConfig b;
  b.top_resistance_k_w = a.top_resistance_k_w * 4.0;
  b.bottom_resistance_k_w = a.bottom_resistance_k_w * 4.0;
  const double area = 1e-6;
  EXPECT_NEAR(b.screeningLengthM(area), 2.0 * a.screeningLengthM(area), 1e-12);
  EXPECT_GT(a.screeningLengthM(area), 0.0);
}

TEST_F(ThermalCoreTest, HistoryJsonRoundTrip)
{
  PhysicsHistory history;
  history.setDesignName("gcd");
  history.setTag("baseline");
  PhysicsSnapshot snap;
  snap.metrics.iteration = 3;
  snap.metrics.label = "checkpoint";
  snap.metrics.peak_temp_c = {61.5};
  snap.metrics.avg_temp_c = {50.25};
  snap.metrics.wns_derated_s = -1.5e-10;
  snap.metrics.runaway = true;
  MapSnapshot map;
  map.name = "temperature";
  map.units = "C";
  map.nx = 2;
  map.ny = 1;
  map.values = {50.0, 61.5};
  snap.maps.push_back(map);
  snap.positions.push_back({"u1", 10, 20});
  snap.die_xmax = 1000;
  snap.die_ymax = 2000;
  history.add(snap);

  PhysicsHistory copy;
  copy.fromJson(history.toJson());
  ASSERT_EQ(copy.size(), 1);
  EXPECT_EQ(copy.designName(), "gcd");
  EXPECT_EQ(copy.tag(), "baseline");
  const PhysicsSnapshot& s = copy.at(0);
  EXPECT_EQ(s.metrics.iteration, 3);
  EXPECT_EQ(s.metrics.label, "checkpoint");
  EXPECT_DOUBLE_EQ(s.metrics.peakTemp(), 61.5);
  EXPECT_DOUBLE_EQ(s.metrics.wns_derated_s, -1.5e-10);
  EXPECT_TRUE(s.metrics.runaway);
  const MapSnapshot* m = s.findMap("temperature");
  ASSERT_NE(m, nullptr);
  EXPECT_DOUBLE_EQ(m->at(1, 0), 61.5);
  ASSERT_EQ(s.positions.size(), 1u);
  EXPECT_EQ(s.positions[0].name, "u1");
  EXPECT_EQ(s.die_ymax, 2000);
}

}  // namespace thm
