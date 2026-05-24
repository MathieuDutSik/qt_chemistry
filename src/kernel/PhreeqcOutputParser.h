#pragma once

#include <map>
#include <string>
#include <vector>

namespace qtchem {

struct SpeciesRow {
  std::string element;
  std::string name;
  double molality = 0.0;
  double activity = 0.0;
  double log_molality = 0.0;
  double log_activity = 0.0;
  double log_gamma = 0.0;
  double mole_v = 0.0;
  bool has_mole_v = false;
};

struct SaturationRow {
  std::string phase;
  double si = 0.0;
  double log_iap = 0.0;
  double log_k = 0.0;
  std::string formula;
};

struct PhaseAssemblageRow {
  std::string phase;
  double si = 0.0;
  double log_iap = 0.0;
  double log_k = 0.0;
  double initial_moles = 0.0;
  double final_moles = 0.0;
  double delta_moles = 0.0;
};

struct ElementTotalRow {
  std::string element;
  double molality = 0.0;
  double moles = 0.0;
};

struct ParsedOutput {
  std::map<std::string, std::string> description;
  std::vector<ElementTotalRow> totals;
  std::vector<SpeciesRow> species;
  std::vector<SaturationRow> saturation;
  std::vector<PhaseAssemblageRow> assemblage;
  bool has_reaction_step = false;
};

ParsedOutput parsePhreeqcOutput(const std::string& raw);

}
