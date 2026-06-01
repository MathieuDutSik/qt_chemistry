#include "kernel/WaterDensity.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using qtchem::DensitySource;
using qtchem::pureWaterDensity;
using qtchem::resolveSolutionDensity;

static int g_failures = 0;

static bool nearly(double a, double b, double atol = 5e-5) {
  return std::abs(a - b) <= atol;
}

#define EXPECT(cond) do {                                                  \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static void test_pure_water_reference_points() {
  // Reference values from IAPWS / Kell tabulations, kg/L at 1 atm.
  EXPECT(nearly(pureWaterDensity(0.0),   0.99984));
  EXPECT(nearly(pureWaterDensity(4.0),   0.99997));  // density maximum
  EXPECT(nearly(pureWaterDensity(25.0),  0.99705));
  EXPECT(nearly(pureWaterDensity(50.0),  0.98804));
  EXPECT(nearly(pureWaterDensity(100.0), 0.95835, 1e-3));
}

static void test_pure_water_rejects_out_of_range() {
  EXPECT(pureWaterDensity(-100.0) == 0.0);
  EXPECT(pureWaterDensity(500.0)  == 0.0);
}

static void test_resolver_prefers_database_value() {
  std::map<std::string, std::string> desc = {
      {"Density (g/cm\xC2\xB3)", "1.02340"},
  };
  auto d = resolveSolutionDensity(desc, 25.0);
  EXPECT(d.source == DensitySource::Database);
  EXPECT(nearly(d.kg_per_L, 1.02340));
}

static void test_resolver_falls_back_to_pure_water() {
  std::map<std::string, std::string> desc;  // no density key
  auto d = resolveSolutionDensity(desc, 25.0);
  EXPECT(d.source == DensitySource::PureWaterFallback);
  EXPECT(nearly(d.kg_per_L, 0.99705));
}

static void test_resolver_rejects_nonsense_database_value() {
  std::map<std::string, std::string> desc = {
      {"Density (g/cm\xC2\xB3)", "nan"},
  };
  auto d = resolveSolutionDensity(desc, 25.0);
  EXPECT(d.source == DensitySource::PureWaterFallback);
  EXPECT(nearly(d.kg_per_L, 0.99705));

  desc["Density (g/cm\xC2\xB3)"] = "-1.0";
  auto d2 = resolveSolutionDensity(desc, 25.0);
  EXPECT(d2.source == DensitySource::PureWaterFallback);
}

int main() {
  test_pure_water_reference_points();
  test_pure_water_rejects_out_of_range();
  test_resolver_prefers_database_value();
  test_resolver_falls_back_to_pure_water();
  test_resolver_rejects_nonsense_database_value();
  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("water_density_test passed\n");
  return 0;
}
