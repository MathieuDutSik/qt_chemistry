#include "kernel/DatabaseInfo.h"

#include <cctype>
#include <fstream>
#include <locale>
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

}

bool DatabaseInfo::load(const std::string& path, std::string* err) {
  entries_.clear();
  by_element_.clear();
  species_to_element_.clear();

  std::ifstream f(path);
  if (!f) { if (err) *err = "cannot open " + path; return false; }

  std::string line;
  bool inSection = false;
  while (std::getline(f, line)) {
    std::string s = trim(stripComment(line));
    if (s.empty()) continue;
    if (isKeywordLine(s)) {
      inSection = (s == "SOLUTION_MASTER_SPECIES");
      continue;
    }
    if (!inSection) continue;

    auto toks = tokenize(s);
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

}
