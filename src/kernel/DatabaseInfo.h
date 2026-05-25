#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qtchem {

struct MasterSpecies {
  std::string element;
  std::string species;
  std::string alkalinity;
  std::string gfw_formula;
  std::string element_gfw;
  double element_gfw_value = 0.0;
};

struct DbReaction {
  // For a phase: phase name (e.g. "Calcite").
  // For an aqueous species: the species being defined (first non-master
  // species on the RHS; falls back to last RHS token).
  std::string name;
  std::string equation;        // e.g. "CaCO3 = CO3-2 + Ca+2"
  bool   has_log_k = false;
  double log_k = 0.0;
  bool   has_delta_h = false;
  double delta_h = 0.0;
  std::string delta_h_unit;    // "kcal" or "kJ", as written in the database
  std::vector<double> analytical_coeffs;  // for -analytical_expression
};

class DatabaseInfo {
 public:
  bool load(const std::string& path, std::string* err = nullptr);

  const std::vector<MasterSpecies>& entries() const { return entries_; }
  std::vector<std::string> elementsAndStates() const;

  std::optional<MasterSpecies> findByElement(const std::string& name) const;
  std::optional<std::string> elementForSpecies(const std::string& species) const;

  // Atomic weight of the element (g/mol). For redox states whose row does
  // not carry a gfw value (e.g. C(+4)), falls back to the parent element's
  // gfw (C). Returns 0 if unknown.
  double atomicWeight(const std::string& element_or_state) const;

  const std::vector<DbReaction>& phases() const { return phases_; }
  const std::vector<DbReaction>& aqueousReactions() const { return aqueous_; }
  std::optional<DbReaction> findPhase(const std::string& name) const;
  std::optional<DbReaction> findAqueous(const std::string& species) const;

 private:
  std::vector<MasterSpecies> entries_;
  std::map<std::string, size_t> by_element_;
  std::map<std::string, std::string> species_to_element_;
  std::vector<DbReaction> phases_;
  std::vector<DbReaction> aqueous_;
  std::map<std::string, size_t> phase_by_name_;
  std::map<std::string, size_t> aqueous_by_species_;
};

}
