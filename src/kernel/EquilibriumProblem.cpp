#include "kernel/EquilibriumProblem.h"

#include "kernel/DatabaseInfo.h"

#include <locale>
#include <sstream>

namespace qtchem {

namespace {

void emitActivityOverride(std::ostream& os, const DatabaseInfo& db,
                          ActivityOverride mode) {
  const char* gamma = (mode == ActivityOverride::DebyeHuckel)
                          ? "0 0"
                          : "1e10 0";
  os << "SOLUTION_SPECIES\n";
  for (const auto& r : db.aqueousReactions()) {
    if (r.name == "e-" || r.name == "H2O" || r.equation.empty()) continue;
    os << "    " << r.equation << '\n';
    if (!r.analytical_coeffs.empty()) {
      os << "        -analytical_expression";
      for (double c : r.analytical_coeffs) os << ' ' << c;
      os << '\n';
    } else if (r.has_log_k) {
      os << "        log_k " << r.log_k << '\n';
      if (r.has_delta_h) {
        os << "        delta_h " << r.delta_h;
        if (!r.delta_h_unit.empty()) os << ' ' << r.delta_h_unit;
        os << '\n';
      }
    }
    os << "        -gamma " << gamma << '\n';
  }
}

}  // namespace

std::string EquilibriumProblem::toPhreeqcInput(const DatabaseInfo* db) const {
  std::ostringstream os;
  os.imbue(std::locale::classic());
  if (!title.empty())
    os << "TITLE " << title << '\n';

  if (activity_override != ActivityOverride::UseDatabase && db) {
    emitActivityOverride(os, *db, activity_override);
  }

  os << "SOLUTION 1\n";
  os << "    temp      " << temperature_c << '\n';
  os << "    pressure  " << pressure_atm << '\n';
  os << "    units     mol/kgw\n";
  os << "    water     " << water_kg << '\n';

  switch (ph.kind) {
    case PhSpec::Fixed:
      os << "    pH        " << ph.value << '\n';
      break;
    case PhSpec::ChargeBalance:
      os << "    pH        " << ph.value << " charge\n";
      break;
    case PhSpec::PhaseEquilibrium:
      os << "    pH        " << ph.value << " " << ph.phase
         << " " << ph.target_si << '\n';
      break;
  }
  if (pe)
    os << "    pe        " << *pe << '\n';

  for (const auto& c : components) {
    os << "    " << c.element << "  " << c.total;
    if (!c.units.empty() && c.units != "mol/kgw")
      os << " " << c.units;
    if (ph.kind == PhSpec::ChargeBalance && c.element == ph.charge_element)
      os << " charge";
    os << '\n';
  }

  if (!phases.empty()) {
    os << "EQUILIBRIUM_PHASES 1\n";
    for (const auto& p : phases) {
      os << "    " << p.phase << "    " << p.target_si
         << "    " << p.initial_moles << '\n';
    }
  }

  os << "SELECTED_OUTPUT 1\n";
  os << "    -reset                 false\n";
  os << "    -simulation            true\n";
  os << "    -state                 true\n";
  os << "    -solution              true\n";
  os << "    -pH                    true\n";
  os << "    -pe                    true\n";
  os << "    -temperature           true\n";
  os << "    -ionic_strength        true\n";
  os << "    -water                 true\n";
  os << "    -charge_balance        true\n";
  os << "    -percent_error         true\n";
  os << "    -totals                ";
  for (const auto& c : components) os << c.element << " ";
  os << '\n';
  os << "    -saturation_indices    ";
  for (const auto& p : phases) os << p.phase << " ";
  os << '\n';
  os << "END\n";
  return os.str();
}

}
