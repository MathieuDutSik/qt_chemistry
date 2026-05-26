#pragma once

#include <optional>
#include <string>
#include <vector>

namespace qtchem {

class DatabaseInfo;

// Override the database's per-species activity-coefficient model by
// re-emitting every aqueous reaction with a Truesdell-Jones `-gamma a b`
// that mathematically reduces to:
//   DebyeHuckel : log γ = -A·z²·√I    (a=0, b=0, "limiting law")
//   Ideal      : log γ ≈ 0           (a=1e10, b=0, gives γ ≈ 1)
// Use ::UseDatabase to leave the loaded model intact.
enum class ActivityOverride {
  UseDatabase,
  DebyeHuckel,
  Ideal,
};

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
  ActivityOverride activity_override = ActivityOverride::UseDatabase;

  // `db` is required when `activity_override != UseDatabase`, because
  // overriding requires enumerating every aqueous formation reaction in
  // the database to re-emit it with the chosen `-gamma`.
  std::string toPhreeqcInput(const DatabaseInfo* db = nullptr) const;
};

}
