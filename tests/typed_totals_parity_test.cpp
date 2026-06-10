// Parity check between the text-parsed element totals and the typed
// totals read directly from SELECTED_OUTPUT via PhreeqcSession::readTypedTotals.
// The text-parsed values carry ~4 significant figures (PHREEQC's print
// format), so the assertion is on relative agreement at that precision.

#include "kernel/EquilibriumProblem.h"
#include "kernel/PhreeqcOutputParser.h"
#include "kernel/PhreeqcSession.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef QTCHEM_TEST_DATABASE_DIR
#  define QTCHEM_TEST_DATABASE_DIR ""
#endif

static int g_failures = 0;

static int fail(const char* msg) {
  std::fprintf(stderr, "FAIL: %s\n", msg);
  ++g_failures;
  return 1;
}

int main() {
  using qtchem::EquilibriumProblem;
  using qtchem::ElementTotalRow;
  using qtchem::PhSpec;
  using qtchem::PhreeqcSession;

  const std::string db_dir = QTCHEM_TEST_DATABASE_DIR;
  if (db_dir.empty()) return fail("QTCHEM_TEST_DATABASE_DIR not set");

  PhreeqcSession s;
  std::string err;
  if (!s.loadDatabase(db_dir + "/phreeqc.dat", &err))
    return fail(("loadDatabase: " + err).c_str());

  EquilibriumProblem p;
  p.title = "typed-totals parity";
  p.ph.kind = PhSpec::Fixed;
  p.ph.value = 7.0;
  p.components = {{"Na", 1.234e-3, "mol/kgw"},
                  {"Cl", 5.678e-3, "mol/kgw"}};

  const auto r = s.solveEquilibrium(p);
  if (!r.ok) return fail(("solveEquilibrium: " + r.error_string).c_str());

  const auto po = qtchem::parsePhreeqcOutput(r.raw_output);
  if (po.frames.empty()) return fail("parsed output had no frames");

  std::vector<std::string> elements;
  for (const auto& c : p.components) elements.push_back(c.element);
  const auto typed = s.readTypedTotals(elements);

  if (typed.size() != elements.size())
    return fail("readTypedTotals returned wrong row count");

  // Both backends should report each element. Tolerance: PHREEQC's text
  // dump prints ~4 sig figs, so 5e-4 relative is comfortable.
  constexpr double kRelTol = 5e-4;
  const auto& parsed_totals = po.frames.back().totals;
  for (const auto& tt : typed) {
    bool matched = false;
    for (const auto& pt : parsed_totals) {
      if (pt.element != tt.element) continue;
      matched = true;
      const double denom = std::max(std::abs(tt.molality), 1e-300);
      const double rel = std::abs(pt.molality - tt.molality) / denom;
      if (rel > kRelTol) {
        std::fprintf(stderr,
                     "FAIL [%s] parsed=%.6e typed=%.6e rel=%.3e\n",
                     tt.element.c_str(), pt.molality, tt.molality, rel);
        ++g_failures;
      } else {
        std::printf("OK   [%s] parsed=%.4e typed=%.10e rel=%.2e\n",
                    tt.element.c_str(), pt.molality, tt.molality, rel);
      }
      break;
    }
    if (!matched) {
      std::fprintf(stderr, "FAIL [%s] absent from parsed totals\n",
                   tt.element.c_str());
      ++g_failures;
    }
  }

  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  std::puts("typed totals parity OK");
  return 0;
}
