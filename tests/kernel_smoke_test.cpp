#include "kernel/EquilibriumProblem.h"
#include "kernel/PhreeqcSession.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef QTCHEM_TEST_DATABASE_DIR
#  define QTCHEM_TEST_DATABASE_DIR ""
#endif

static int fail(const char* msg) {
  std::fprintf(stderr, "FAIL: %s\n", msg);
  return 1;
}

int main() {
  const std::string db_dir = QTCHEM_TEST_DATABASE_DIR;
  if (db_dir.empty())
    return fail("QTCHEM_TEST_DATABASE_DIR not set");
  const std::string db_path = db_dir + "/phreeqc.dat";

  qtchem::PhreeqcSession s;
  std::string err;
  if (!s.loadDatabase(db_path, &err)) {
    std::fprintf(stderr, "loadDatabase err: %s\n", err.c_str());
    return fail("loadDatabase phreeqc.dat");
  }

  qtchem::EquilibriumProblem p;
  p.title = "smoke";
  p.temperature_c = 25.0;
  p.pressure_atm = 1.0;
  p.ph.kind = qtchem::PhSpec::Fixed;
  p.ph.value = 7.0;
  p.components = {
      {"Na", 1e-3, "mol/kgw"},
      {"Cl", 1e-3, "mol/kgw"},
  };

  const auto r = s.solveEquilibrium(p);
  if (!r.ok) {
    std::fprintf(stderr, "phreeqc err: %s\n", r.error_string.c_str());
    std::fprintf(stderr, "input was:\n%s\n", r.raw_input.c_str());
    return fail("solveEquilibrium");
  }
  if (r.selected_output.size() < 2)
    return fail("selected_output too small");
  std::printf("OK — %zu selected-output rows, %zu cols\n",
              r.selected_output.size(), r.selected_output.front().size());
  return 0;
}
