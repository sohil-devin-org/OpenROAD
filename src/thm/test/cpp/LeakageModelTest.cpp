// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "thm/LeakageModel.h"
#include "thm/ThermalConfig.h"
#include "utl/Logger.h"

namespace thm {

namespace {

std::string tempFile(const char* name)
{
  const char* dir = std::getenv("TEST_TMPDIR");
  return std::string(dir != nullptr ? dir : "/tmp") + "/" + name;
}

class LeakageModelTest : public ::testing::Test
{
 protected:
  utl::Logger logger_;
};

TEST_F(LeakageModelTest, ExactBetaRecoveryFromThreeTemperatures)
{
  // L(T) = 2 nW * exp(0.05 (T - 25)) sampled at 0, 25 and 125 C.
  const double beta = 0.05;
  const double l_ref = 2e-9;
  std::vector<std::pair<double, double>> samples;
  for (double t : {125.0, 0.0, 25.0}) {
    samples.emplace_back(t, l_ref * std::exp(beta * (t - 25.0)));
  }
  LeakageFit fit;
  ASSERT_TRUE(fitLeakageExponential(samples, 25.0, fit));
  EXPECT_NEAR(fit.beta_per_c, beta, 1e-12);
  EXPECT_DOUBLE_EQ(fit.t_ref_c, 25.0);
  EXPECT_NEAR(fit.leakage_ref_w, l_ref, 1e-12 * l_ref);
  EXPECT_NEAR(fit.rel_rms_error, 0.0, 1e-9);
  EXPECT_EQ(fit.num_points, 3);

  // A single temperature cannot be fitted; non-positive samples are dropped.
  LeakageFit bad;
  EXPECT_FALSE(fitLeakageExponential({{25.0, 1e-9}, {25.0, 2e-9}}, 25.0, bad));
  EXPECT_FALSE(fitLeakageExponential({{25.0, 1e-9}, {85.0, 0.0}}, 25.0, bad));
}

TEST_F(LeakageModelTest, LeastSquaresOnLogLeakage)
{
  // Noisy samples: the slope of ln L is the least-squares slope.
  const std::vector<std::pair<double, double>> samples
      = {{0.0, 1.0e-9}, {50.0, 10.0e-9}, {100.0, 90.0e-9}};
  LeakageFit fit;
  ASSERT_TRUE(fitLeakageExponential(samples, 0.0, fit));
  // Slope of ln(L) vs T over x = 0, 50, 100: (ln 90 - ln 1) / 100.
  const double expected = (std::log(90.0) - std::log(1.0)) / 100.0;
  EXPECT_NEAR(fit.beta_per_c, expected, 1e-12);
  EXPECT_GT(fit.rel_rms_error, 0.0);
}

TEST_F(LeakageModelTest, JsonRoundTrip)
{
  const std::string path = tempFile("leakage_fits.json");
  {
    LeakageModel model(&logger_);
    // No fit: the model stays inert.
    EXPECT_EQ(model.mode(), FitMode::kNone);
    EXPECT_DOUBLE_EQ(model.leakageScale("X", 100.0), 1.0);
    // Default mode from the config key.
    model.setDefaultBetaPerC(0.07);
    EXPECT_EQ(model.mode(), FitMode::kDefault);
    EXPECT_DOUBLE_EQ(model.familyBetaPerC(), 0.07);
    EXPECT_NEAR(
        model.leakageAt("X", 1e-9, 25.0, 35.0), 1e-9 * std::exp(0.7), 1e-20);
    model.writeFits(path);
  }
  {
    LeakageModel model(&logger_);
    ASSERT_TRUE(model.loadFits(path));
    EXPECT_EQ(model.mode(), FitMode::kDefault);
    EXPECT_DOUBLE_EQ(model.familyBetaPerC(), 0.07);
    EXPECT_DOUBLE_EQ(model.defaultBetaPerC(), 0.07);
    EXPECT_EQ(model.numFits(), 0);
    EXPECT_NE(model.report().find("default"), std::string::npos);
  }
  // Deterministic serialization.
  const std::string path2 = tempFile("leakage_fits2.json");
  {
    LeakageModel model(&logger_);
    ASSERT_TRUE(model.loadFits(path));
    model.writeFits(path2);
  }
  std::ifstream a(path);
  std::ifstream b(path2);
  std::string sa((std::istreambuf_iterator<char>(a)),
                 std::istreambuf_iterator<char>());
  std::string sb((std::istreambuf_iterator<char>(b)),
                 std::istreambuf_iterator<char>());
  EXPECT_EQ(sa, sb);
  std::remove(path.c_str());
  std::remove(path2.c_str());
}

TEST_F(LeakageModelTest, DerateFitRoundTripAndNominal)
{
  const std::string path = tempFile("derate_fits.json");
  // Write a fitted-mode file by hand (as characterize_thermal_libraries
  // would) and check the piecewise table / per-cell sensitivities survive.
  {
    std::ofstream out(path);
    out << R"({"t_nom_c":25,"v_nom_v":1.1,)"
        << R"("temperature_mode":"fitted","voltage_mode":"fitted",)"
        << R"("temperature_factor":[[0,0.95],[25,1.0],[125,1.3]],)"
        << R"("voltage_factor":[[0.95,1.25],[1.1,1.0],[1.25,0.85]],)"
        << R"("cells":[{"name":"INV_X1","temp_sens_per_c":0.002,)"
        << R"("volt_sens_per_v":-0.9}]})";
  }
  DerateModel model(&logger_);
  ASSERT_TRUE(model.loadFits(path));
  EXPECT_EQ(model.temperatureMode(), FitMode::kFitted);
  EXPECT_EQ(model.voltageMode(), FitMode::kFitted);
  EXPECT_TRUE(model.hasTemperatureFit());
  EXPECT_TRUE(model.hasVoltageFit());
  EXPECT_DOUBLE_EQ(model.nominalTempC(), 25.0);
  EXPECT_DOUBLE_EQ(model.nominalVddV(), 1.1);
  // 1.0 at nominal, per-cell and family.
  EXPECT_DOUBLE_EQ(model.derate("INV_X1", 25.0, 1.1), 1.0);
  EXPECT_DOUBLE_EQ(model.derate("NAND2_X1", 25.0, 1.1), 1.0);
  // Monotone: increasing in T, decreasing in V (increasing in 1/V).
  double prev = 0.0;
  for (double t = 0.0; t <= 125.0; t += 12.5) {
    const double d_cell = model.derate("INV_X1", t, 1.1);
    const double d_family = model.derate("NAND2_X1", t, 1.1);
    if (t > 0.0) {
      EXPECT_GT(d_cell, prev);
    }
    prev = d_cell;
    EXPECT_GE(d_family, model.derate("NAND2_X1", t - 12.5, 1.1));
  }
  prev = 0.0;
  for (double v = 0.95; v <= 1.2501; v += 0.05) {
    const double d_cell = model.derate("INV_X1", 25.0, v);
    const double d_family = model.derate("NAND2_X1", 25.0, v);
    if (v > 0.95) {
      EXPECT_LT(d_cell, prev);
      EXPECT_LT(d_family, model.derate("NAND2_X1", 25.0, v - 0.05));
    }
    prev = d_cell;
  }
  // Interpolation of the family table.
  EXPECT_NEAR(model.temperatureFactor("NAND2_X1", 75.0), 1.15, 1e-12);
  EXPECT_NEAR(model.voltageFactor("NAND2_X1", 1.025), 1.125, 1e-12);

  // Round trip.
  const std::string path2 = tempFile("derate_fits2.json");
  model.writeFits(path2);
  DerateModel copy(&logger_);
  ASSERT_TRUE(copy.loadFits(path2));
  for (double t : {0.0, 40.0, 125.0}) {
    for (double v : {0.95, 1.1, 1.2}) {
      EXPECT_DOUBLE_EQ(copy.derate("INV_X1", t, v),
                       model.derate("INV_X1", t, v));
      EXPECT_DOUBLE_EQ(copy.derate("BUF_X2", t, v),
                       model.derate("BUF_X2", t, v));
    }
  }
  std::remove(path.c_str());
  std::remove(path2.c_str());
}

TEST_F(LeakageModelTest, DerateDefaultsAreNominalNormalizedAndMonotone)
{
  const ThermalConfig defaults;
  DerateModel model(&logger_);
  // Without a fit and without defaults the factor is 1.
  EXPECT_DOUBLE_EQ(model.derate("X", 100.0, 0.9), 1.0);
  model.setNominal(25.0, 1.8);
  model.setDefaultTempcoPerC(defaults.delay_tempco_per_c);
  model.setDefaultVcoefPerV(defaults.delay_vcoef_per_v);
  EXPECT_EQ(model.temperatureMode(), FitMode::kDefault);
  EXPECT_EQ(model.voltageMode(), FitMode::kDefault);
  EXPECT_FALSE(model.hasTemperatureFit());
  EXPECT_DOUBLE_EQ(model.derate("X", 25.0, 1.8), 1.0);
  EXPECT_GT(defaults.delay_tempco_per_c, 0.0);
  EXPECT_LT(defaults.delay_vcoef_per_v, 0.0);
  double prev = model.derate("X", -40.0, 1.8);
  for (double t = -20.0; t <= 125.0; t += 20.0) {
    const double d = model.derate("X", t, 1.8);
    EXPECT_GT(d, prev);
    prev = d;
  }
  prev = model.derate("X", 25.0, 1.5);
  for (double v = 1.6; v <= 2.0; v += 0.1) {
    const double d = model.derate("X", 25.0, v);
    EXPECT_LT(d, prev);
    prev = d;
  }
  EXPECT_NE(model.report().find("default"), std::string::npos);
  // The leakage default is ln(2)/10 per C: leakage doubles every 10 C.
  LeakageModel leakage(&logger_);
  EXPECT_NEAR(leakage.defaultBetaPerC(), std::log(2.0) / 10.0, 1e-15);
  leakage.setDefaultBetaPerC(leakage.defaultBetaPerC());
  EXPECT_NEAR(leakage.leakageAt("X", 1.0, 25.0, 35.0), 2.0, 1e-12);
}

}  // namespace

}  // namespace thm
