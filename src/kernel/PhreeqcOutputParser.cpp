#include "kernel/PhreeqcOutputParser.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace qtchem {

namespace {

bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::string trim(std::string s) {
  auto issp = [](unsigned char c){ return std::isspace(c); };
  while (!s.empty() && issp(s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && issp(s[i])) ++i;
  return s.substr(i);
}

std::vector<std::string> tokenize(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string t;
  while (is >> t) out.push_back(t);
  return out;
}

double to_double_or(const std::string& s, double fallback = 0.0) {
  char* end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  return (end == s.c_str()) ? fallback : v;
}

bool is_section(const std::string& line, const std::string& title) {
  return line.find(title) != std::string::npos &&
         line.find("---") != std::string::npos;
}

}

ParsedOutput parsePhreeqcOutput(const std::string& raw) {
  ParsedOutput out;
  std::istringstream is(raw);
  std::vector<std::string> lines;
  for (std::string l; std::getline(is, l);) lines.push_back(l);

  enum Section { NONE, COMPOSITION, DESCRIPTION, SPECIES, SI, ASSEMBLAGE };
  Section sec = NONE;
  std::string current_element;

  std::vector<ElementTotalRow> cur_totals;
  std::map<std::string, std::string> cur_desc;
  std::vector<SpeciesRow> cur_species;
  std::vector<SaturationRow> cur_si;
  std::vector<PhaseAssemblageRow> cur_assemblage;

  auto flush_composition = [&]() {
    if (!cur_totals.empty()) { out.totals = cur_totals; cur_totals.clear(); }
  };
  auto flush_description = [&]() {
    if (!cur_desc.empty()) { out.description = cur_desc; cur_desc.clear(); }
  };
  auto flush_species = [&]() {
    if (!cur_species.empty()) { out.species = cur_species; cur_species.clear(); }
  };
  auto flush_si = [&]() {
    if (!cur_si.empty()) { out.saturation = cur_si; cur_si.clear(); }
  };
  auto flush_assemblage = [&]() {
    if (!cur_assemblage.empty()) {
      out.assemblage = cur_assemblage;
      cur_assemblage.clear();
    }
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& ln = lines[i];

    if (is_section(ln, "Solution composition")) {
      flush_composition(); flush_description(); flush_species();
      flush_si(); flush_assemblage();
      sec = COMPOSITION; current_element.clear(); continue;
    }
    if (is_section(ln, "Description of solution")) {
      flush_composition(); sec = DESCRIPTION; continue;
    }
    if (is_section(ln, "Distribution of species")) {
      flush_description(); sec = SPECIES; current_element.clear(); continue;
    }
    if (is_section(ln, "Saturation indices")) {
      flush_species(); sec = SI; continue;
    }
    if (is_section(ln, "Phase assemblage")) {
      flush_species(); sec = ASSEMBLAGE; continue;
    }
    if (starts_with(trim(ln), "Beginning of batch-reaction")) {
      out.has_reaction_step = true;
      sec = NONE; continue;
    }
    if (starts_with(trim(ln), "Beginning of initial solution")) {
      sec = NONE; continue;
    }

    switch (sec) {
      case COMPOSITION: {
        auto t = tokenize(ln);
        if (t.size() >= 3 && std::isalpha(static_cast<unsigned char>(t[0][0]))
            && t[0] != "Elements") {
          ElementTotalRow r;
          r.element = t[0];
          r.molality = to_double_or(t[1]);
          r.moles = to_double_or(t[2]);
          cur_totals.push_back(r);
        }
        break;
      }
      case DESCRIPTION: {
        auto eq = ln.find('=');
        if (eq != std::string::npos) {
          std::string key = trim(ln.substr(0, eq));
          std::string val = trim(ln.substr(eq + 1));
          if (!key.empty()) cur_desc[key] = val;
        }
        break;
      }
      case SPECIES: {
        const std::string t = trim(ln);
        if (t.empty() || starts_with(t, "Species") || starts_with(t, "Log")
            || starts_with(t, "Molality")) break;
        auto toks = tokenize(ln);
        if (toks.empty()) break;
        if (toks.size() == 2) {
          if (toks[0] != "Total" && std::isalpha(
                  static_cast<unsigned char>(toks[0][0]))) {
            current_element = toks[0];
          }
          break;
        }
        if (toks.size() >= 6) {
          SpeciesRow r;
          r.element = current_element;
          r.name = toks[0];
          r.molality = to_double_or(toks[1]);
          r.activity = to_double_or(toks[2]);
          r.log_molality = to_double_or(toks[3]);
          r.log_activity = to_double_or(toks[4]);
          r.log_gamma = to_double_or(toks[5]);
          if (toks.size() >= 7 && toks[6] != "(0)") {
            r.mole_v = to_double_or(toks[6]);
            r.has_mole_v = true;
          }
          cur_species.push_back(r);
        }
        break;
      }
      case SI: {
        const std::string t = trim(ln);
        if (t.empty() || starts_with(t, "Phase") || starts_with(t, "**")
            || starts_with(t, "For")) break;
        auto toks = tokenize(ln);
        if (toks.size() >= 4) {
          SaturationRow r;
          r.phase = toks[0];
          r.si = to_double_or(toks[1]);
          r.log_iap = to_double_or(toks[2]);
          r.log_k = to_double_or(toks[3]);
          if (toks.size() >= 5) r.formula = toks[4];
          cur_si.push_back(r);
        }
        break;
      }
      case ASSEMBLAGE: {
        const std::string t = trim(ln);
        if (t.empty() || starts_with(t, "Phase")
            || starts_with(t, "Moles")) break;
        auto toks = tokenize(ln);
        if (toks.size() >= 7) {
          PhaseAssemblageRow r;
          r.phase = toks[0];
          r.si = to_double_or(toks[1]);
          r.log_iap = to_double_or(toks[2]);
          r.log_k = to_double_or(toks[3]);
          r.initial_moles = to_double_or(toks[4]);
          r.final_moles = to_double_or(toks[5]);
          r.delta_moles = to_double_or(toks[6]);
          cur_assemblage.push_back(r);
        }
        break;
      }
      case NONE: break;
    }
  }
  flush_composition(); flush_description(); flush_species();
  flush_si(); flush_assemblage();
  return out;
}

}
