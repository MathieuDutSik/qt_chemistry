#include "kernel/EditableDatabase.h"

#include <IPhreeqc.hpp>

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdio>
#include <fstream>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace qtchem {

namespace {

class CNumericLocaleGuard {
 public:
  CNumericLocaleGuard() {
    const char* current = std::setlocale(LC_NUMERIC, nullptr);
    if (current) saved_.assign(current);
    std::setlocale(LC_NUMERIC, "C");
  }
  ~CNumericLocaleGuard() {
    std::setlocale(LC_NUMERIC, saved_.empty() ? "C" : saved_.c_str());
  }
 private:
  std::string saved_;
};

double parseDoubleC(const std::string& s) {
  std::istringstream is(s);
  is.imbue(std::locale::classic());
  double v = 0.0;
  is >> v;
  return is.fail() ? 0.0 : v;
}

std::string trim(std::string s) {
  auto issp = [](unsigned char c) { return std::isspace(c); };
  while (!s.empty() && issp(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && issp(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}

std::vector<std::string> tokenize(const std::string& s) {
  std::vector<std::string> v;
  std::istringstream is(s);
  std::string t;
  while (is >> t) v.push_back(t);
  return v;
}

std::string stripComment(const std::string& l) {
  auto h = l.find('#');
  return h == std::string::npos ? l : l.substr(0, h);
}

bool isIndented(const std::string& raw) {
  return !raw.empty() && std::isspace(static_cast<unsigned char>(raw[0]));
}

bool isKeywordLine(const std::string& s) {
  if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) return false;
    if (!std::isupper(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return s.size() >= 3;
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
      if (i + 1 < t.size() && (t[i+1] == '+' || t[i+1] == '-')) ++i;
      continue;
    }
    return false;
  }
  return true;
}

std::string definedSpecies(const std::string& equation) {
  const auto eq_pos = equation.find('=');
  if (eq_pos == std::string::npos) return {};
  const std::string rhs = trim(equation.substr(eq_pos + 1));
  std::vector<std::string> tokens;
  for (const auto& t : tokenize(rhs))
    if (t != "+" && !isNumeric(t)) tokens.push_back(t);
  return tokens.empty() ? std::string{} : tokens.back();
}

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find('\n', i);
    if (j == std::string::npos) {
      if (i < s.size()) out.push_back(s.substr(i));
      break;
    }
    std::string line = s.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    i = j + 1;
  }
  return out;
}

std::string joinLines(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& l : lines) { out.append(l); out.push_back('\n'); }
  return out;
}

// Recognize the property keywords we understand on indented continuation
// lines of a species/phase entry. Returns the canonical key ("log_k",
// "delta_h", "analytic", "gamma") or "" if not recognized.
std::string propertyKey(const std::string& line) {
  const auto toks = tokenize(line);
  if (toks.empty()) return {};
  std::string key = toks[0];
  if (!key.empty() && key[0] == '-') key.erase(0, 1);
  if (key == "log_k" || key == "logk") return "log_k";
  if (key == "delta_h") return "delta_h";
  if (key == "analytical_expression" || key == "analytic" || key == "a_e")
    return "analytic";
  if (key == "gamma") return "gamma";
  return {};
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c){ return std::tolower(c); });
  return s;
}

bool isKnownDeltaHUnit(const std::string& u) {
  const std::string lu = toLower(u);
  return lu == "kcal" || lu == "kj" || lu == "j" || lu == "cal";
}

// Returns true if the line was fully consumed in canonical form. On false,
// the caller should preserve the original line in extra_lines.
bool applyKnownProperty(EditableEntry& e, const std::string& key,
                        const std::string& line) {
  const auto toks = tokenize(line);
  if (key == "log_k") {
    if (toks.size() != 2) return false;
    e.has_log_k = true; e.log_k = parseDoubleC(toks[1]);
    return true;
  }
  if (key == "delta_h") {
    if (toks.size() < 2 || toks.size() > 3) return false;
    if (toks.size() == 3 && !isKnownDeltaHUnit(toks[2])) return false;
    e.has_delta_h = true; e.delta_h = parseDoubleC(toks[1]);
    if (toks.size() == 3) e.delta_h_unit = toks[2];
    return true;
  }
  if (key == "analytic") {
    if (toks.size() < 2) return false;
    e.analytic_coeffs.clear();
    for (size_t i = 1; i < toks.size(); ++i)
      e.analytic_coeffs.push_back(parseDoubleC(toks[i]));
    return true;
  }
  if (key == "gamma") {
    if (toks.size() != 3) return false;
    e.has_gamma = true;
    e.gamma_a0 = parseDoubleC(toks[1]);
    e.gamma_b = parseDoubleC(toks[2]);
    return true;
  }
  return false;
}

// Format a finite double like PHREEQC's databases: %g-ish, but with extra
// precision to avoid silent round-off on common values.
std::string fmtDouble(double v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

std::string serializeEntry(const EditableEntry& e, bool is_phase) {
  std::string out;
  if (is_phase && !e.name.empty()) {
    out.append(e.name);
    out.push_back('\n');
  }
  // Indented equation line. For aqueous species the equation IS the
  // header line (not indented in databases by convention), but PHREEQC
  // accepts indented too, and aligning it makes the structured output
  // more uniform.
  if (!e.equation.empty()) {
    if (is_phase) out.append("    ");
    out.append(e.equation);
    out.push_back('\n');
  }
  if (e.has_log_k) {
    out.append("    log_k     ");
    out.append(fmtDouble(e.log_k));
    out.push_back('\n');
  }
  if (e.has_delta_h) {
    out.append("    delta_h   ");
    out.append(fmtDouble(e.delta_h));
    if (!e.delta_h_unit.empty()) {
      out.push_back(' ');
      out.append(e.delta_h_unit);
    }
    out.push_back('\n');
  }
  if (!e.analytic_coeffs.empty()) {
    out.append("    -analytical_expression");
    for (double c : e.analytic_coeffs) {
      out.push_back(' ');
      out.append(fmtDouble(c));
    }
    out.push_back('\n');
  }
  if (e.has_gamma) {
    out.append("    -gamma    ");
    out.append(fmtDouble(e.gamma_a0));
    out.push_back(' ');
    out.append(fmtDouble(e.gamma_b));
    out.push_back('\n');
  }
  for (const auto& xl : e.extra_lines) {
    out.append(xl);
    out.push_back('\n');
  }
  return out;
}

// Parse a single section body (lines after the section header, up to the
// next keyword) into entries.
//
// Entry-start detection differs by section type:
//   - PHASES: a phase always starts with a column-0, non-empty line that
//     does NOT contain '=' (the phase name). The '=' equation line then
//     follows on subsequent indented line(s).
//   - SOLUTION_SPECIES: databases differ — some put the defining equation
//     at column 0 (phreeqc.dat), some indent it (wateq4f.dat). So we
//     detect entry start by "this is a `=`-containing line whose first
//     token is not a known property keyword", regardless of indentation.
std::vector<EditableEntry> parseSectionBody(
    const std::vector<std::string>& lines, bool is_phase) {
  std::vector<EditableEntry> entries;
  EditableEntry* cur = nullptr;
  for (const auto& raw : lines) {
    const std::string nc = stripComment(raw);
    const std::string t = trim(nc);
    if (t.empty()) {
      if (cur) cur->extra_lines.push_back(raw);
      continue;
    }

    if (is_phase) {
      const bool indented = isIndented(nc);
      const bool has_eq = t.find('=') != std::string::npos;
      // Phase equation can appear at column 0 (SIT / NEA style: name on
      // one line, equation on the next, both unindented) or indented
      // (phreeqc.dat style). Either way it always comes between the
      // phase name and the property block.
      if (cur && cur->equation.empty() && has_eq) {
        cur->equation = t;
        continue;
      }
      if (!indented) {
        if (has_eq) {
          // Stray equation we can't attach (e.g. between phases). Keep
          // verbatim on the previous entry so it's not silently dropped.
          if (cur) cur->extra_lines.push_back(raw);
          continue;
        }
        entries.push_back(EditableEntry{});
        cur = &entries.back();
        cur->name = t;
        continue;
      }
      if (!cur) continue;
      const std::string key = propertyKey(t);
      bool handled = false;
      if (!key.empty()) handled = applyKnownProperty(*cur, key, t);
      if (!handled) cur->extra_lines.push_back(raw);
      continue;
    }

    // Aqueous species:
    const std::string key = propertyKey(t);
    const bool is_property = !key.empty();
    const bool has_eq = t.find('=') != std::string::npos;
    if (has_eq && !is_property) {
      entries.push_back(EditableEntry{});
      cur = &entries.back();
      cur->equation = t;
      cur->name = definedSpecies(t);
      continue;
    }
    if (!cur) continue;  // continuation outside any entry — ignore.
    bool handled = false;
    if (is_property) handled = applyKnownProperty(*cur, key, t);
    if (!handled) cur->extra_lines.push_back(raw);
  }
  return entries;
}

}  // namespace

bool EditableDatabase::load(const std::string& path, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  text_ = ss.str();
  structured_ok_ = false;
  aqueous_.clear();
  phases_.clear();
  segments_.clear();
  aqueous_section_count_ = 0;
  phase_section_count_ = 0;
  return true;
}

void EditableDatabase::setText(std::string t) {
  text_ = std::move(t);
  structured_ok_ = false;  // raw edit invalidates structured view.
}

bool EditableDatabase::reparse(std::string* err) {
  structured_ok_ = false;
  aqueous_.clear();
  phases_.clear();
  segments_.clear();
  aqueous_section_count_ = 0;
  phase_section_count_ = 0;

  const auto lines = splitLines(text_);
  if (lines.empty()) {
    if (err) *err = "empty file.";
    return false;
  }

  // Walk lines. Build a segment list and, in parallel, collect lines that
  // belong to SOLUTION_SPECIES / PHASES bodies into two pools that we'll
  // parse into entries afterwards.
  enum class State { Outside, InAqueous, InPhases };
  State state = State::Outside;
  Segment cur;
  cur.kind = Segment::Kind::Verbatim;
  std::vector<std::string> aqueous_body;
  std::vector<std::string> phases_body;

  auto flushVerbatim = [&]() {
    if (cur.kind == Segment::Kind::Verbatim && !cur.lines.empty()) {
      segments_.push_back(std::move(cur));
      cur = Segment{};
      cur.kind = Segment::Kind::Verbatim;
    }
  };

  for (const auto& raw : lines) {
    const std::string t = trim(stripComment(raw));
    if (isKeywordLine(t)) {
      // Crossing a keyword boundary: terminate the current verbatim
      // segment, then handle this keyword.
      if (t == "SOLUTION_SPECIES") {
        flushVerbatim();
        Segment h;
        h.kind = Segment::Kind::AqueousHeader;
        h.lines.push_back(raw);
        segments_.push_back(std::move(h));
        ++aqueous_section_count_;
        state = State::InAqueous;
      } else if (t == "PHASES") {
        flushVerbatim();
        Segment h;
        h.kind = Segment::Kind::PhaseHeader;
        h.lines.push_back(raw);
        segments_.push_back(std::move(h));
        ++phase_section_count_;
        state = State::InPhases;
      } else {
        // Some other keyword section — content goes verbatim. Switch
        // state back to Outside so this line and its body are preserved.
        state = State::Outside;
        cur.lines.push_back(raw);
      }
      continue;
    }
    switch (state) {
      case State::Outside:    cur.lines.push_back(raw); break;
      case State::InAqueous:  aqueous_body.push_back(raw); break;
      case State::InPhases:   phases_body.push_back(raw); break;
    }
  }
  flushVerbatim();

  if (aqueous_section_count_ == 0 && phase_section_count_ == 0) {
    if (err) *err = "file has no SOLUTION_SPECIES or PHASES section.";
    return false;
  }

  aqueous_ = parseSectionBody(aqueous_body, /*is_phase=*/false);
  phases_  = parseSectionBody(phases_body,  /*is_phase=*/true);

  structured_ok_ = true;
  return true;
}

void EditableDatabase::replaceAqueous(size_t i, EditableEntry e) {
  if (i < aqueous_.size()) aqueous_[i] = std::move(e);
}

void EditableDatabase::appendAqueous(EditableEntry e) {
  aqueous_.push_back(std::move(e));
}

void EditableDatabase::removeAqueous(size_t i) {
  if (i < aqueous_.size()) aqueous_.erase(aqueous_.begin() + i);
}

void EditableDatabase::replacePhase(size_t i, EditableEntry e) {
  if (i < phases_.size()) phases_[i] = std::move(e);
}

void EditableDatabase::appendPhase(EditableEntry e) {
  phases_.push_back(std::move(e));
}

void EditableDatabase::removePhase(size_t i) {
  if (i < phases_.size()) phases_.erase(phases_.begin() + i);
}

const std::string& EditableDatabase::reserialize() {
  if (!structured_ok_) return text_;
  std::string out;
  bool wrote_aqueous = false;
  bool wrote_phases = false;
  for (const auto& seg : segments_) {
    switch (seg.kind) {
      case Segment::Kind::Verbatim:
        out.append(joinLines(seg.lines));
        break;
      case Segment::Kind::AqueousHeader:
        out.append(joinLines(seg.lines));  // header line
        if (!wrote_aqueous) {
          for (const auto& e : aqueous_)
            out.append(serializeEntry(e, /*is_phase=*/false));
          wrote_aqueous = true;
        }
        break;
      case Segment::Kind::PhaseHeader:
        out.append(joinLines(seg.lines));  // header line
        if (!wrote_phases) {
          for (const auto& e : phases_)
            out.append(serializeEntry(e, /*is_phase=*/true));
          wrote_phases = true;
        }
        break;
    }
  }
  text_ = std::move(out);
  return text_;
}

bool EditableDatabase::validate(std::string* err) const {
  CNumericLocaleGuard locale_guard;
  auto iph = std::make_unique<IPhreeqc>();
  iph->SetOutputFileOn(false);
  iph->SetErrorFileOn(false);
  iph->SetSelectedOutputFileOn(false);
  iph->SetLogFileOn(false);
  iph->SetDumpFileOn(false);
  iph->SetOutputStringOn(false);
  iph->SetErrorStringOn(true);
  const int errors = iph->LoadDatabaseString(text_.c_str());
  if (errors != 0) {
    if (err) *err = iph->GetErrorString();
    return false;
  }
  return true;
}

bool EditableDatabase::saveAs(const std::string& path, std::string* err) const {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (err) *err = "cannot create " + tmp;
      return false;
    }
    out.write(text_.data(), static_cast<std::streamsize>(text_.size()));
    if (!out) {
      if (err) *err = "write failed: " + tmp;
      std::remove(tmp.c_str());
      return false;
    }
  }
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (err) *err = "rename failed: " + tmp + " -> " + path;
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

}
