#include "kernel/MonteCarlo.h"
#include "kernel/PhreeqcSession.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#ifndef QTCHEM_TEST_DATABASE_DIR
#  define QTCHEM_TEST_DATABASE_DIR ""
#endif

using qtchem::EquilibriumProblem;
using qtchem::MonteCarloBudget;
using qtchem::PhreeqcSession;
using qtchem::PhSpec;
using qtchem::ScalarAggregator;
using qtchem::SolutionComponent;
using qtchem::UncertaintySpec;
using qtchem::perturbProblem;
using qtchem::runMonteCarlo;

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

static void test_aggregator_known_sequence() {
  ScalarAggregator a(3.0);
  for (double x : {1.0, 2.0, 3.0, 4.0, 5.0}) a.add(x);
  auto agg = a.finalize();
  EXPECT(agg.n_samples == 5);
  EXPECT(nearly(agg.baseline, 3.0));
  EXPECT(nearly(agg.min_value, 1.0));
  EXPECT(nearly(agg.max_value, 5.0));
  EXPECT(nearly(agg.mean_l1, (2.0+1.0+0.0+1.0+2.0) / 5.0));
  EXPECT(nearly(agg.rms_l2, std::sqrt((4.0+1.0+0.0+1.0+4.0) / 5.0)));
}

static void test_aggregator_empty_returns_baseline() {
  ScalarAggregator a(7.0);
  auto agg = a.finalize();
  EXPECT(agg.n_samples == 0);
  EXPECT(agg.baseline == 7.0);
  EXPECT(agg.min_value == 7.0);
  EXPECT(agg.max_value == 7.0);
  EXPECT(agg.mean_l1 == 0.0);
  EXPECT(agg.rms_l2 == 0.0);
}

static void test_aggregator_constant_samples_have_zero_error() {
  ScalarAggregator a(5.0);
  for (int i = 0; i < 10; ++i) a.add(5.0);
  auto agg = a.finalize();
  EXPECT(nearly(agg.mean_l1, 0.0));
  EXPECT(nearly(agg.rms_l2, 0.0));
  EXPECT(agg.min_value == 5.0);
  EXPECT(agg.max_value == 5.0);
}

static EquilibriumProblem makeNaClProblem() {
  EquilibriumProblem p;
  p.title = "MC test";
  p.temperature_c = 25.0;
  p.pressure_atm = 1.0;
  p.ph.kind = PhSpec::Fixed;
  p.ph.value = 7.0;
  p.components.push_back({"Na", 1e-3, "mol/kgw"});
  p.components.push_back({"Cl", 1e-3, "mol/kgw"});
  return p;
}

static void test_perturb_zero_spec_is_identity() {
  auto base = makeNaClProblem();
  UncertaintySpec s;
  std::mt19937 rng(42);
  auto out = perturbProblem(base, s, rng);
  EXPECT(out.temperature_c == base.temperature_c);
  EXPECT(out.pressure_atm == base.pressure_atm);
  EXPECT(out.ph.value == base.ph.value);
  EXPECT(out.components[0].total == base.components[0].total);
  EXPECT(out.components[1].total == base.components[1].total);
}

static void test_perturb_temperature_stays_in_interval() {
  auto base = makeNaClProblem();
  UncertaintySpec s;
  s.temperature_delta_c = 5.0;
  std::mt19937 rng(42);
  for (int i = 0; i < 200; ++i) {
    auto out = perturbProblem(base, s, rng);
    EXPECT(out.temperature_c >= 20.0 - 1e-9);
    EXPECT(out.temperature_c <= 30.0 + 1e-9);
    EXPECT(out.pressure_atm == base.pressure_atm);
  }
}

static void test_perturb_pH_only_when_fixed() {
  auto base = makeNaClProblem();
  base.ph.kind = PhSpec::ChargeBalance;
  UncertaintySpec s;
  s.ph_delta = 0.5;
  std::mt19937 rng(42);
  for (int i = 0; i < 50; ++i) {
    auto out = perturbProblem(base, s, rng);
    EXPECT(out.ph.value == base.ph.value);
  }
}

static void test_perturb_seed_reproducible() {
  auto base = makeNaClProblem();
  UncertaintySpec s;
  s.temperature_delta_c = 5.0;
  std::mt19937 rng_a(123), rng_b(123);
  for (int i = 0; i < 10; ++i) {
    auto a = perturbProblem(base, s, rng_a);
    auto b = perturbProblem(base, s, rng_b);
    EXPECT(a.temperature_c == b.temperature_c);
  }
}

// ---- End-to-end with a real PHREEQC session ----

static int run_end_to_end_tests() {
  const std::string db_dir = QTCHEM_TEST_DATABASE_DIR;
  if (db_dir.empty()) {
    std::fprintf(stderr, "SKIP end-to-end: QTCHEM_TEST_DATABASE_DIR unset\n");
    return 0;
  }
  PhreeqcSession s;
  std::string err;
  if (!s.loadDatabase(db_dir + "/phreeqc.dat", &err)) {
    std::fprintf(stderr, "FAIL: loadDatabase: %s\n", err.c_str());
    return 1;
  }

  auto base = makeNaClProblem();

  // Zero-uncertainty + small budget → every run reproduces baseline.
  {
    UncertaintySpec spec;
    spec.seed = 7;
    MonteCarloBudget budget;
    budget.max_seconds = 2.0;
    budget.max_runs = 5;
    auto r = runMonteCarlo(base, spec, budget, s, nullptr);
    EXPECT(r.error.empty());
    EXPECT(r.runs_completed == 5);
    EXPECT(!r.elements.empty());
    bool any_nonzero_error = false;
    for (const auto& e : r.elements) {
      if (e.molality.mean_l1 > 0.0 || e.molality.rms_l2 > 0.0)
        any_nonzero_error = true;
      EXPECT(e.molality.min_value == e.molality.baseline);
      EXPECT(e.molality.max_value == e.molality.baseline);
    }
    EXPECT(!any_nonzero_error);
  }

  // Temperature-only ±10 °C with seed → at least one element has non-zero
  // L1 deviation (equilibrium shifts with T even for a simple NaCl solution).
  {
    UncertaintySpec spec;
    spec.seed = 7;
    spec.temperature_delta_c = 10.0;
    MonteCarloBudget budget;
    budget.max_seconds = 5.0;
    budget.max_runs = 8;
    auto r = runMonteCarlo(base, spec, budget, s, nullptr);
    EXPECT(r.error.empty());
    EXPECT(r.runs_completed > 0);
    bool any_nonzero = false;
    for (const auto& sp : r.species) {
      if (sp.molality.mean_l1 > 0.0) { any_nonzero = true; break; }
    }
    EXPECT(any_nonzero);
  }
  return 0;
}

int main() {
  test_aggregator_known_sequence();
  test_aggregator_empty_returns_baseline();
  test_aggregator_constant_samples_have_zero_error();
  test_perturb_zero_spec_is_identity();
  test_perturb_temperature_stays_in_interval();
  test_perturb_pH_only_when_fixed();
  test_perturb_seed_reproducible();
  run_end_to_end_tests();
  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::printf("all MonteCarlo tests passed\n");
  return 0;
}
