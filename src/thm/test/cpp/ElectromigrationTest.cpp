// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include <cmath>

#include "gtest/gtest.h"
#include "odb/geom.h"
#include "thm/PhysicsCoupling.h"
#include "thm/PhysicsState.h"
#include "thm/ThermalConfig.h"
#include "thm/ThermalGrid.h"

namespace thm {

namespace {

// Boltzmann constant in eV/K (CODATA 2018).
constexpr double kBoltzmannEvPerK = 8.617333262e-5;

double arrhenius(double ea_ev, double temp_c, double ref_c)
{
  return std::exp(ea_ev / kBoltzmannEvPerK
                  * (1.0 / (temp_c + 273.15) - 1.0 / (ref_c + 273.15)));
}

TEST(ElectromigrationTest, ReferenceConditionsGiveUnity)
{
  const ElectromigrationConfig cfg;
  const ElectromigrationModel em(cfg);
  EXPECT_DOUBLE_EQ(em.relativeLifetime(1.0, cfg.reference_temp_c, 1.0), 1.0);
  EXPECT_DOUBLE_EQ(em.relativeLifetime(5e9, cfg.reference_temp_c, 5e9), 1.0);
}

TEST(ElectromigrationTest, ArrheniusFactorBetween105And125C)
{
  const ElectromigrationConfig cfg;
  ASSERT_DOUBLE_EQ(cfg.activation_energy_ev, 0.9);
  ASSERT_DOUBLE_EQ(cfg.reference_temp_c, 105.0);
  const ElectromigrationModel em(cfg);
  // exp(0.9 eV / k * (1/398.15 K - 1/378.15 K)) = 0.2497: 20 C hotter costs
  // 4x lifetime at Ea = 0.9 eV.
  const double factor = em.relativeLifetime(1.0, 125.0, 1.0);
  EXPECT_NEAR(factor, 0.2497, 5e-4);
  EXPECT_NEAR(factor, arrhenius(0.9, 125.0, 105.0), 1e-12);
  // Cooler than the reference lives longer.
  EXPECT_NEAR(
      em.relativeLifetime(1.0, 85.0, 1.0), arrhenius(0.9, 85.0, 105.0), 1e-12);
  EXPECT_GT(em.relativeLifetime(1.0, 85.0, 1.0), 1.0);
}

TEST(ElectromigrationTest, CurrentDensityPowerLaw)
{
  const ElectromigrationConfig cfg;
  ASSERT_DOUBLE_EQ(cfg.current_exponent, 2.0);
  const ElectromigrationModel em(cfg);
  // MTTF ~ J^-n: doubling J at the reference temperature quarters the
  // lifetime; halving it quadruples it.
  EXPECT_NEAR(em.relativeLifetime(2.0, cfg.reference_temp_c, 1.0), 0.25, 1e-12);
  EXPECT_NEAR(em.relativeLifetime(0.5, cfg.reference_temp_c, 1.0), 4.0, 1e-12);
  // Both terms multiply.
  EXPECT_NEAR(em.relativeLifetime(2.0, 125.0, 1.0),
              0.25 * arrhenius(0.9, 125.0, 105.0),
              1e-12);
  // A different exponent from the config is honored.
  ElectromigrationConfig n1 = cfg;
  n1.current_exponent = 1.0;
  EXPECT_NEAR(ElectromigrationModel(n1).relativeLifetime(
                  2.0, cfg.reference_temp_c, 1.0),
              0.5,
              1e-12);
  // Non-positive currents have no EM wear.
  EXPECT_TRUE(std::isinf(em.relativeLifetime(0.0, 125.0, 1.0)));
}

TEST(ElectromigrationTest, TileMapUsesPowerOverVddAsCurrentProxy)
{
  const ElectromigrationConfig cfg;
  const ElectromigrationModel em(cfg);
  ThermalConfig thermal;
  thermal.grid_x = 2;
  thermal.grid_y = 2;
  constexpr int kDbu = 1000;
  const odb::Rect die(0, 0, 1000 * kDbu, 1000 * kDbu);
  ThermalGrid grid;
  grid.reset(2, 2, die, kDbu, thermal.buildStack());
  grid.fill(cfg.reference_temp_c);

  PowerMap power(2, 2);
  power.at(0, 0) = 1.0;
  power.at(1, 0) = 2.0;
  power.at(0, 1) = 3.0;
  power.at(1, 1) = 0.0;

  // Without an IR map only the temperature term applies: all tiles at the
  // reference temperature have relative lifetime 1.
  const MapSnapshot uniform = em.tileLifetimeMap(grid, power, nullptr, 1.1, 0);
  ASSERT_EQ(uniform.values.size(), 4u);
  for (double v : uniform.values) {
    EXPECT_DOUBLE_EQ(v, 1.0);
  }
  EXPECT_DOUBLE_EQ(ElectromigrationModel::worstFactor(uniform), 1.0);

  // With an IR map, J ~ P / (VDD - drop); J_ref is the mean of the powered
  // tiles (1 + 2 + 3) / 3 = 2 A at zero drop.
  MapSnapshot ir;
  ir.name = "ir_drop";
  ir.nx = 2;
  ir.ny = 2;
  ir.values = {0.0, 0.0, 0.0, 0.0};
  const MapSnapshot map = em.tileLifetimeMap(grid, power, &ir, 1.0, 0);
  EXPECT_NEAR(map.at(0, 0), 4.0, 1e-12);        // (2/1)^2
  EXPECT_NEAR(map.at(1, 0), 1.0, 1e-12);        // (2/2)^2
  EXPECT_NEAR(map.at(0, 1), 4.0 / 9.0, 1e-12);  // (2/3)^2
  EXPECT_DOUBLE_EQ(map.at(1, 1), 1.0);          // no current: thermal only
  EXPECT_NEAR(ElectromigrationModel::worstFactor(map), 4.0 / 9.0, 1e-12);

  // A local drop raises the current of that tile and lowers its lifetime.
  ir.values[1 * ir.nx + 0] = 0.5;
  const MapSnapshot dropped = em.tileLifetimeMap(grid, power, &ir, 1.0, 0);
  EXPECT_LT(dropped.at(0, 1), map.at(0, 1));

  // Hotter tile: Arrhenius term multiplies the current term.
  grid.at(0, 1, grid.activeZ(0)) = 125.0;
  const MapSnapshot hot = em.tileLifetimeMap(grid, power, &ir, 1.0, 0);
  EXPECT_NEAR(
      hot.at(0, 1), dropped.at(0, 1) * arrhenius(0.9, 125.0, 105.0), 1e-12);
}

}  // namespace

}  // namespace thm
