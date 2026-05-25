#include "kernel/DatabaseInfo.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <locale>
#include <set>
#include <sstream>

namespace qtchem {

namespace {

double parseDoubleC(const std::string& s) {
  std::istringstream is(s);
  is.imbue(std::locale::classic());
  double v = 0.0;
  is >> v;
  return is.fail() ? 0.0 : v;
}

std::string trim(std::string s) {
  auto issp = [](unsigned char c) { return std::isspace(c); };
  while (!s.empty() && issp(s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && issp(s[i])) ++i;
  return s.substr(i);
}

std::vector<std::string> tokenize(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream is(s);
  std::string t;
  while (is >> t) v.push_back(t);
  return v;
}

std::string normalizeElement(std::string s) {
  auto open = s.find('(');
  if (open != std::string::npos && open + 1 < s.size() && s[open + 1] == '+')
    s.erase(open + 1, 1);
  return s;
}

bool isKeywordLine(const std::string& s) {
  if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) return false;
    if (!std::isupper(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return s.size() >= 3;
}

std::string stripComment(const std::string& l) {
  auto h = l.find('#');
  return h == std::string::npos ? l : l.substr(0, h);
}

bool isIndented(const std::string& raw) {
  return !raw.empty() && std::isspace(static_cast<unsigned char>(raw[0]));
}

bool isNumeric(const std::string& t) {
  if (t.empty()) return false;
  bool saw_dot = false;
  size_t i = 0;
  if (t[0] == '+' || t[0] == '-') i = 1;
  if (i == t.size()) return false;
  for (; i < t.size(); ++i) {
    char c = t[i];
    if (std::isdigit(static_cast<unsigned char>(c))) continue;
    if (c == '.' && !saw_dot) { saw_dot = true; continue; }
    if (c == 'e' || c == 'E') {
      // Allow exponent
      if (i + 1 < t.size() && (t[i+1] == '+' || t[i+1] == '-')) ++i;
      continue;
    }
    return false;
  }
  return true;
}

// Extract the species being defined from an aqueous reaction. Uses the
// master-species set to skip "master" species on the RHS.
std::string definedSpecies(const std::string& equation,
                           const std::set<std::string>& master_species) {
  const auto eq_pos = equation.find('=');
  if (eq_pos == std::string::npos) return {};
  const std::string rhs = trim(equation.substr(eq_pos + 1));
  std::vector<std::string> species;
  for (const auto& t : tokenize(rhs)) {
    if (t == "+" || isNumeric(t)) continue;
    species.push_back(t);
  }
  if (species.empty()) return {};
  for (const auto& s : species) {
    if (!master_species.count(s)) return s;
  }
  return species.back();
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string baseName(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Built-in registry of database provenance / intended use. Lookup is on
// lowercased basename. Unknown databases fall through to an empty string.
std::string lookupDescription(const std::string& path) {
  static const std::map<std::string, std::string> kReg = {
    {"phreeqc.dat",
     "General-purpose USGS PHREEQC database; T,P-dependent for dilute "
     "to moderately saline waters."},
    {"wateq4f.dat",
     "USGS WATEQ4F speciation database (extended Debye-Huckel)."},
    {"minteq.dat",
     "USEPA MINTEQA2 thermodynamic database."},
    {"minteq.v4.dat",
     "USEPA MINTEQA2 v4 thermodynamic database."},
    {"llnl.dat",
     "Lawrence Livermore Lab compilation (thermo.com.V8.R6.230); large "
     "species set, high T/P."},
    {"pitzer.dat",
     "Pitzer ion-interaction model for high-salinity brines and "
     "evaporites (Na-K-Mg-Ca-Cl-SO4-H2O system + extensions)."},
    {"sit.dat",
     "Specific-ion Interaction Theory database; OECD/NEA TDB style, "
     "common in radioactive-waste studies."},
    {"frezchem.dat",
     "FREZCHEM: Pitzer-based, cryogenic brines and sub-zero systems."},
    {"coldchem.dat",
     "ColdChem: cold/cryogenic chemistry."},
    {"concrete_pz.dat",
     "Cement / concrete chemistry with Pitzer model."},
    {"concrete_phr.dat",
     "Cement / concrete chemistry (Davies-style activity model)."},
    {"core10.dat",
     "ThermoChimie v10 (Andra); nuclear waste reference compilation."},
    {"phreeqc_thermoddemv1.10_15dec2020.dat",
     "Thermoddem (BRGM); ~1600 phases, radwaste / cement / mining."},
    {"amm.dat",
     "phreeqc.dat extension adding ammonia/ammonium speciation."},
    {"iso.dat",
     "Isotope speciation database (D, 13C, 18O, etc.)."},
    {"kinec.v2.dat",
     "Kinetic-rate compilation v2."},
    {"kinec_v3.dat",
     "Kinetic-rate compilation v3."},
    {"tipping_hurley.dat",
     "Tipping-Hurley humic/organic complexation (WHAM-style)."},
    {"phreeqc_rates.dat",
     "Example BASIC rate expressions for KINETICS."},
    {"minimum.dat",
     "Minimal database — bare master species, useful for didactic "
     "exercises."},
    {"stimela.dat",
     "Stimela: water-treatment-focused database."},
  };
  const auto it = kReg.find(toLower(baseName(path)));
  return it == kReg.end() ? std::string{} : it->second;
}

void applyProperty(DbReaction& r, const std::string& line) {
  const auto toks = tokenize(line);
  if (toks.empty()) return;
  std::string key = toks[0];
  if (!key.empty() && key[0] == '-') key.erase(0, 1);
  if (key == "log_k" || key == "logk") {
    if (toks.size() >= 2) { r.has_log_k = true; r.log_k = parseDoubleC(toks[1]); }
  } else if (key == "delta_h") {
    if (toks.size() >= 2) {
      r.has_delta_h = true;
      r.delta_h = parseDoubleC(toks[1]);
      if (toks.size() >= 3) r.delta_h_unit = toks[2];
    }
  } else if (key == "analytical_expression" ||
             key == "analytic" || key == "a_e") {
    for (size_t i = 1; i < toks.size(); ++i)
      r.analytical_coeffs.push_back(parseDoubleC(toks[i]));
  }
}

}  // namespace

bool DatabaseInfo::load(const std::string& path, std::string* err) {
  entries_.clear();
  by_element_.clear();
  species_to_element_.clear();
  phases_.clear();
  aqueous_.clear();
  phase_by_name_.clear();
  aqueous_by_species_.clear();
  activity_model_.clear();
  description_ = lookupDescription(path);

  std::ifstream f(path);
  if (!f) { if (err) *err = "cannot open " + path; return false; }

  enum class Section { None, MasterSpecies, SolutionSpecies, Phases };
  Section sec = Section::None;
  DbReaction* current = nullptr;

  // Pass 1: read the whole file into memory so we can do master-species
  // lookups across passes. (File is < 1 MB even for the largest db.)
  std::vector<std::string> lines;
  for (std::string l; std::getline(f, l);) lines.push_back(l);

  // Activity-model detection: which keyword blocks does the database
  // contain? Priority matters because some databases include LLNL params
  // *and* Pitzer (the latter wins).
  bool has_pitzer = false, has_sit = false, has_llnl = false;
  bool has_tj_gamma = false;
  for (const auto& raw : lines) {
    const std::string s = trim(stripComment(raw));
    if (s == "PITZER") has_pitzer = true;
    else if (s == "SIT") has_sit = true;
    else if (s == "LLNL_AQUEOUS_MODEL_PARAMETERS") has_llnl = true;
    else if (s.size() > 6 && s.substr(0, 7) == "-gamma ") has_tj_gamma = true;
  }
  if (has_pitzer)      activity_model_ = "Pitzer ion-interaction";
  else if (has_sit)    activity_model_ = "SIT (Specific-ion Interaction)";
  else if (has_llnl)   activity_model_ = "LLNL B-dot (Helgeson)";
  else if (has_tj_gamma) activity_model_ = "Davies + Truesdell-Jones";
  else                 activity_model_ = "Davies (default)";

  // First pass: collect master species so we can identify defined species
  // in SOLUTION_SPECIES later.
  for (const auto& raw : lines) {
    const std::string s = trim(stripComment(raw));
    if (s.empty()) continue;
    if (isKeywordLine(s)) {
      sec = (s == "SOLUTION_MASTER_SPECIES") ? Section::MasterSpecies
                                              : Section::None;
      continue;
    }
    if (sec != Section::MasterSpecies) continue;
    const auto toks = tokenize(s);
    if (toks.size() < 2) continue;
    MasterSpecies m;
    m.element = toks[0];
    m.species = toks[1];
    if (toks.size() > 2) m.alkalinity = toks[2];
    if (toks.size() > 3) m.gfw_formula = toks[3];
    if (toks.size() > 4) m.element_gfw = toks[4];
    if (!m.element_gfw.empty())
      m.element_gfw_value = parseDoubleC(m.element_gfw);
    by_element_.emplace(normalizeElement(m.element), entries_.size());
    species_to_element_.emplace(m.species, normalizeElement(m.element));
    entries_.push_back(std::move(m));
  }

  std::set<std::string> master_species_set;
  for (const auto& e : entries_) master_species_set.insert(e.species);
  // H+ and H2O and e- are conventionally master too.
  master_species_set.insert("H+");
  master_species_set.insert("H2O");
  master_species_set.insert("e-");

  // Second pass: PHASES and SOLUTION_SPECIES.
  sec = Section::None;
  for (const auto& raw : lines) {
    const std::string s_nc = stripComment(raw);
    const std::string s = trim(s_nc);
    if (s.empty()) continue;
    if (isKeywordLine(s)) {
      if (s == "PHASES") sec = Section::Phases;
      else if (s == "SOLUTION_SPECIES") sec = Section::SolutionSpecies;
      else sec = Section::None;
      current = nullptr;
      continue;
    }

    const bool indented = isIndented(s_nc);
    if (sec == Section::Phases) {
      if (!indented) {
        phases_.push_back(DbReaction{});
        current = &phases_.back();
        current->name = s;
        phase_by_name_.emplace(current->name, phases_.size() - 1);
      } else if (current) {
        if (s.find('=') != std::string::npos && current->equation.empty()) {
          current->equation = s;
        } else {
          applyProperty(*current, s);
        }
      }
    } else if (sec == Section::SolutionSpecies) {
      if (!indented) {
        if (s.find('=') == std::string::npos) { current = nullptr; continue; }
        aqueous_.push_back(DbReaction{});
        current = &aqueous_.back();
        current->equation = s;
        current->name = definedSpecies(s, master_species_set);
        if (!current->name.empty())
          aqueous_by_species_.emplace(current->name, aqueous_.size() - 1);
      } else if (current) {
        applyProperty(*current, s);
      }
    } else {
      current = nullptr;
    }
  }
  return true;
}

std::vector<std::string> DatabaseInfo::elementsAndStates() const {
  std::vector<std::string> v;
  v.reserve(entries_.size());
  for (const auto& e : entries_) v.push_back(e.element);
  return v;
}

std::optional<MasterSpecies>
DatabaseInfo::findByElement(const std::string& name) const {
  auto it = by_element_.find(normalizeElement(name));
  if (it == by_element_.end()) return std::nullopt;
  return entries_[it->second];
}

std::optional<std::string>
DatabaseInfo::elementForSpecies(const std::string& species) const {
  auto it = species_to_element_.find(species);
  if (it == species_to_element_.end()) return std::nullopt;
  return it->second;
}

double DatabaseInfo::atomicWeight(const std::string& name) const {
  if (auto m = findByElement(name); m && m->element_gfw_value > 0)
    return m->element_gfw_value;
  const std::string n = normalizeElement(name);
  const auto open = n.find('(');
  if (open == std::string::npos) return 0.0;
  if (auto parent = findByElement(n.substr(0, open));
      parent && parent->element_gfw_value > 0)
    return parent->element_gfw_value;
  return 0.0;
}

std::optional<DbReaction>
DatabaseInfo::findPhase(const std::string& name) const {
  auto it = phase_by_name_.find(name);
  if (it == phase_by_name_.end()) return std::nullopt;
  return phases_[it->second];
}

std::optional<DbReaction>
DatabaseInfo::findAqueous(const std::string& species) const {
  auto it = aqueous_by_species_.find(species);
  if (it == aqueous_by_species_.end()) return std::nullopt;
  return aqueous_[it->second];
}

}
