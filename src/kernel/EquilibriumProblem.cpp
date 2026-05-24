#include "kernel/EquilibriumProblem.h"

#include <sstream>

namespace qtchem {

std::string EquilibriumProblem::toPhreeqcInput() const {
  std::ostringstream os;
  if (!title.empty())
    os << "TITLE " << title << '\n';

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
