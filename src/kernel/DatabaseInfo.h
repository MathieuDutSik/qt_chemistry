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

 private:
  std::vector<MasterSpecies> entries_;
  std::map<std::string, size_t> by_element_;
  std::map<std::string, std::string> species_to_element_;
};

}
