#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qtchem {

// Frame-wide state of the modeled solution. Anything per-row (the species'
// molality, its molar mass) belongs in UnitConvertInputs. Fields left empty
// here make any target unit that needs them unconvertible — the converter
// returns std::nullopt instead of guessing.
struct UnitConvertContext {
  double kg_water = 1.0;
  std::optional<double> kg_solution;
  std::optional<double> L_solution;
};

struct UnitConvertInputs {
  double molality_mol_per_kgw = 0.0;
  std::optional<double> molar_mass_g_per_mol;
};

// Returns the value of `in.molality_mol_per_kgw` expressed in `target_unit`,
// or std::nullopt if `target_unit` is unknown or a needed context field is
// missing (mass unit without molar mass, /L unit without L_solution, /kgs
// unit without kg_solution, zero denominator).
std::optional<double> convertFromMolality(const UnitConvertInputs& in,
                                          const UnitConvertContext& ctx,
                                          std::string_view target_unit);

// Every unit string the converter accepts, in display order.
const std::vector<std::string>& supportedUnits();

}
