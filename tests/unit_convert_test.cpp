#include "kernel/UnitConvert.h"

#include <cmath>
#include <cstdio>
#include <string>

using qtchem::UnitConvertContext;
using qtchem::UnitConvertInputs;
using qtchem::convertFromMolality;
using qtchem::supportedUnits;

static int g_failures = 0;

static bool nearly(double a, double b, double rtol = 1e-12) {
  const double scale = std::max(1.0, std::abs(b));
  return std::abs(a - b) <= rtol * scale;
}

#define EXPECT(cond) do {                                                  \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static void test_identity_kgw() {
  UnitConvertInputs in; in.molality_mol_per_kgw = 0.012;
  UnitConvertContext ctx; ctx.kg_water = 1.0;
  auto v = convertFromMolality(in, ctx, "mol/kgw");
  EXPECT(v.has_value());
  EXPECT(v && nearly(*v, 0.012));
}

static void test_kgw_prefixes() {
  UnitConvertInputs in; in.molality_mol_per_kgw = 1e-3;
  UnitConvertContext ctx; ctx.kg_water = 1.0;
  EXPECT(nearly(*convertFromMolality(in, ctx, "mmol/kgw"), 1.0));
}

static void test_kgw_independent_of_water_amount() {
  // molality is defined per kg of water, so changing kg_water must not
  // change the reported /kgw value.
  UnitConvertInputs in; in.molality_mol_per_kgw = 0.01;
  UnitConvertContext c1; c1.kg_water = 1.0;
  UnitConvertContext c2; c2.kg_water = 5.0;
  EXPECT(*convertFromMolality(in, c1, "mol/kgw") ==
         *convertFromMolality(in, c2, "mol/kgw"));
}

static void test_mass_units_need_molar_mass() {
  UnitConvertInputs in; in.molality_mol_per_kgw = 0.01;
  UnitConvertContext ctx; ctx.kg_water = 1.0;
  EXPECT(!convertFromMolality(in, ctx, "g/kgw").has_value());
  in.molar_mass_g_per_mol = 23.0;  // Na
  EXPECT(nearly(*convertFromMolality(in, ctx, "g/kgw"),  0.23));
  EXPECT(nearly(*convertFromMolality(in, ctx, "mg/kgw"), 230.0));
}

static void test_per_liter_units_need_L() {
  UnitConvertInputs in; in.molality_mol_per_kgw = 0.5;
  in.molar_mass_g_per_mol = 58.44;
  UnitConvertContext ctx; ctx.kg_water = 1.0;
  EXPECT(!convertFromMolality(in, ctx, "mol/L").has_value());
  EXPECT(!convertFromMolality(in, ctx, "g/L").has_value());
  ctx.L_solution = 1.02;
  EXPECT(nearly(*convertFromMolality(in, ctx, "mol/L"),  0.5 / 1.02));
  EXPECT(nearly(*convertFromMolality(in, ctx, "mmol/L"), 0.5 / 1.02 * 1e3));
  EXPECT(nearly(*convertFromMolality(in, ctx, "g/L"),    0.5 * 58.44 / 1.02));
}

static void test_unknown_unit() {
  UnitConvertInputs in; in.molality_mol_per_kgw = 1.0;
  UnitConvertContext ctx; ctx.kg_water = 1.0; ctx.L_solution = 1.0;
  EXPECT(!convertFromMolality(in, ctx, "g/kqw").has_value());  // typo
  EXPECT(!convertFromMolality(in, ctx, "").has_value());
  EXPECT(!convertFromMolality(in, ctx, "mg/m3").has_value());
  EXPECT(!convertFromMolality(in, ctx, "MOL/KGW").has_value());  // case-sensitive
  // Units intentionally removed from the supported set.
  EXPECT(!convertFromMolality(in, ctx, "umol/kgw").has_value());
  EXPECT(!convertFromMolality(in, ctx, "mol").has_value());
  EXPECT(!convertFromMolality(in, ctx, "g").has_value());
  EXPECT(!convertFromMolality(in, ctx, "g/kgs").has_value());
}

static void test_zero_denominator_is_unconvertible() {
  UnitConvertInputs in; in.molality_mol_per_kgw = 1.0;
  in.molar_mass_g_per_mol = 18.0;
  UnitConvertContext ctx; ctx.kg_water = 1.0; ctx.L_solution = 0.0;
  EXPECT(!convertFromMolality(in, ctx, "mol/L").has_value());
}

static void test_all_listed_units_convert_when_fully_populated() {
  UnitConvertInputs in;
  in.molality_mol_per_kgw = 1.0;
  in.molar_mass_g_per_mol = 18.0;
  UnitConvertContext ctx;
  ctx.kg_water = 1.0; ctx.L_solution = 1.0;
  for (const auto& s : supportedUnits()) {
    auto v = convertFromMolality(in, ctx, s);
    if (!v) {
      std::fprintf(stderr, "FAIL: listed unit '%s' did not convert\n",
                   s.c_str());
      ++g_failures;
    }
  }
}

int main() {
  test_identity_kgw();
  test_kgw_prefixes();
  test_kgw_independent_of_water_amount();
  test_mass_units_need_molar_mass();
  test_per_liter_units_need_L();
  test_unknown_unit();
  test_zero_denominator_is_unconvertible();
  test_all_listed_units_convert_when_fully_populated();
  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all UnitConvert tests passed\n");
  return 0;
}
