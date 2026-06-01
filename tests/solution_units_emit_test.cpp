#include "kernel/EquilibriumProblem.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

#define EXPECT(cond) do {                                                  \
    if (!(cond)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

static bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

int main() {
  // Rows: mixed units, one per-L row to trigger -density.
  qtchem::EquilibriumProblem p;
  p.title = "units-emit-test";
  p.components.push_back({"Na", 10.77, "g/kgw"});
  p.components.push_back({"Cl", 0.55,  "mol/kgw"});  // default — should be elided
  p.components.push_back({"Ca", 412.0, "mg/kgw"});
  p.components.push_back({"K",  10.0,  "mmol/L"});

  const std::string in = p.toPhreeqcInput();

  EXPECT(contains(in, "Na  10.77 g/kgw"));
  EXPECT(contains(in, "Cl  0.55\n"));
  EXPECT(!contains(in, "Cl  0.55 mol/kgw"));
  EXPECT(contains(in, "Ca  412 mg/kgw"));
  EXPECT(contains(in, "K  10 mmol/L"));
  EXPECT(contains(in, "density   1.0 calculate"));

  // No /L row → no density line.
  qtchem::EquilibriumProblem q;
  q.title = "kgw-only";
  q.components.push_back({"Na", 1.0, "mol/kgw"});
  q.components.push_back({"Cl", 1.0, "mol/kgw"});
  const std::string in_q = q.toPhreeqcInput();
  EXPECT(!contains(in_q, "density"));

  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    std::fprintf(stderr, "----- in -----\n%s\n", in.c_str());
    std::fprintf(stderr, "----- in_q -----\n%s\n", in_q.c_str());
    return 1;
  }
  std::printf("solution_units_emit_test passed\n");
  return 0;
}
