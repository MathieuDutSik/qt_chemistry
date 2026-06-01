#include "kernel/UnitConvert.h"

#include <unordered_map>

namespace qtchem {

namespace {

enum class Amount { Mol, G };
enum class Prefix { None, Milli, Micro };
enum class Denom  { Absolute, Kgw, Kgs, L };

struct Spec {
  Amount amount;
  Prefix prefix;
  Denom denom;
};

const std::unordered_map<std::string, Spec>& specs() {
  static const std::unordered_map<std::string, Spec> m = {
      {"mol",      {Amount::Mol, Prefix::None,  Denom::Absolute}},
      {"mmol",     {Amount::Mol, Prefix::Milli, Denom::Absolute}},
      {"umol",     {Amount::Mol, Prefix::Micro, Denom::Absolute}},
      {"g",        {Amount::G,   Prefix::None,  Denom::Absolute}},
      {"mg",       {Amount::G,   Prefix::Milli, Denom::Absolute}},
      {"ug",       {Amount::G,   Prefix::Micro, Denom::Absolute}},
      {"mol/kgw",  {Amount::Mol, Prefix::None,  Denom::Kgw}},
      {"mmol/kgw", {Amount::Mol, Prefix::Milli, Denom::Kgw}},
      {"umol/kgw", {Amount::Mol, Prefix::Micro, Denom::Kgw}},
      {"g/kgw",    {Amount::G,   Prefix::None,  Denom::Kgw}},
      {"mg/kgw",   {Amount::G,   Prefix::Milli, Denom::Kgw}},
      {"ug/kgw",   {Amount::G,   Prefix::Micro, Denom::Kgw}},
      {"mol/kgs",  {Amount::Mol, Prefix::None,  Denom::Kgs}},
      {"mmol/kgs", {Amount::Mol, Prefix::Milli, Denom::Kgs}},
      {"umol/kgs", {Amount::Mol, Prefix::Micro, Denom::Kgs}},
      {"g/kgs",    {Amount::G,   Prefix::None,  Denom::Kgs}},
      {"mg/kgs",   {Amount::G,   Prefix::Milli, Denom::Kgs}},
      {"ug/kgs",   {Amount::G,   Prefix::Micro, Denom::Kgs}},
      {"mol/L",    {Amount::Mol, Prefix::None,  Denom::L}},
      {"mmol/L",   {Amount::Mol, Prefix::Milli, Denom::L}},
      {"umol/L",   {Amount::Mol, Prefix::Micro, Denom::L}},
      {"g/L",      {Amount::G,   Prefix::None,  Denom::L}},
      {"mg/L",     {Amount::G,   Prefix::Milli, Denom::L}},
      {"ug/L",     {Amount::G,   Prefix::Micro, Denom::L}},
  };
  return m;
}

double prefixFactor(Prefix p) {
  switch (p) {
    case Prefix::None:  return 1.0;
    case Prefix::Milli: return 1e3;
    case Prefix::Micro: return 1e6;
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
    case Denom::Absolute:
      denom = 1.0;
      break;
    case Denom::Kgw:
      denom = ctx.kg_water;
      break;
    case Denom::Kgs:
      if (!ctx.kg_solution) return std::nullopt;
      denom = *ctx.kg_solution;
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
      "mol/kgw", "mmol/kgw", "umol/kgw",
      "g/kgw",   "mg/kgw",   "ug/kgw",
      "mol/kgs", "mmol/kgs", "umol/kgs",
      "g/kgs",   "mg/kgs",   "ug/kgs",
      "mol/L",   "mmol/L",   "umol/L",
      "g/L",     "mg/L",     "ug/L",
      "mol",     "mmol",     "umol",
      "g",       "mg",       "ug",
  };
  return u;
}

}  // namespace qtchem
