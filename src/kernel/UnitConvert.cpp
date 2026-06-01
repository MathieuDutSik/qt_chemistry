#include "kernel/UnitConvert.h"

#include <unordered_map>

namespace qtchem {

namespace {

enum class Amount { Mol, G };
enum class Prefix { None, Milli };
enum class Denom  { Kgw, L };

struct Spec {
  Amount amount;
  Prefix prefix;
  Denom denom;
};

const std::unordered_map<std::string, Spec>& specs() {
  static const std::unordered_map<std::string, Spec> m = {
      {"mol/kgw",  {Amount::Mol, Prefix::None,  Denom::Kgw}},
      {"mmol/kgw", {Amount::Mol, Prefix::Milli, Denom::Kgw}},
      {"g/kgw",    {Amount::G,   Prefix::None,  Denom::Kgw}},
      {"mg/kgw",   {Amount::G,   Prefix::Milli, Denom::Kgw}},
      {"mol/L",    {Amount::Mol, Prefix::None,  Denom::L}},
      {"mmol/L",   {Amount::Mol, Prefix::Milli, Denom::L}},
      {"g/L",      {Amount::G,   Prefix::None,  Denom::L}},
      {"mg/L",     {Amount::G,   Prefix::Milli, Denom::L}},
  };
  return m;
}

double prefixFactor(Prefix p) {
  switch (p) {
    case Prefix::None:  return 1.0;
    case Prefix::Milli: return 1e3;
  }
  return 1.0;
}

}  // namespace

std::optional<double> convertFromMolality(const UnitConvertInputs& in,
                                          const UnitConvertContext& ctx,
                                          std::string_view target_unit) {
  const auto& table = specs();
  const auto it = table.find(std::string(target_unit));
  if (it == table.end()) return std::nullopt;
  const Spec s = it->second;

  // Numerator: total amount of solute, first in mol, then optionally in g.
  double numerator = in.molality_mol_per_kgw * ctx.kg_water;
  if (s.amount == Amount::G) {
    if (!in.molar_mass_g_per_mol) return std::nullopt;
    numerator *= *in.molar_mass_g_per_mol;
  }

  double denom = 1.0;
  switch (s.denom) {
    case Denom::Kgw:
      denom = ctx.kg_water;
      break;
    case Denom::L:
      if (!ctx.L_solution) return std::nullopt;
      denom = *ctx.L_solution;
      break;
  }
  if (denom == 0.0) return std::nullopt;

  return (numerator / denom) * prefixFactor(s.prefix);
}

const std::vector<std::string>& supportedUnits() {
  static const std::vector<std::string> u = {
      "mol/kgw", "mmol/kgw", "g/kgw", "mg/kgw",
      "mol/L",   "mmol/L",   "g/L",   "mg/L",
  };
  return u;
}

}  // namespace qtchem
