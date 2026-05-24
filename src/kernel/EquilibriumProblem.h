#pragma once

#include <optional>
#include <string>
#include <vector>

namespace qtchem {

struct SolutionComponent {
  std::string element;
  double total = 0.0;
  std::string units = "mol/kgw";
};

enum class PhSpec {
  Fixed,
  ChargeBalance,
  PhaseEquilibrium,
};

struct PhSpecification {
  PhSpec kind = PhSpec::Fixed;
  double value = 7.0;
  std::string phase;
  double target_si = 0.0;
  std::string charge_element;
};

struct PhaseConstraint {
  std::string phase;
  double target_si = 0.0;
  double initial_moles = 10.0;
};

struct EquilibriumProblem {
  std::vector<SolutionComponent> components;
  double temperature_c = 25.0;
  double pressure_atm = 1.0;
  PhSpecification ph;
  std::optional<double> pe = 4.0;
  double water_kg = 1.0;
  std::vector<PhaseConstraint> phases;
  std::string title;

  std::string toPhreeqcInput() const;
};

}
