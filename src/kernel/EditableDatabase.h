#pragma once

#include <string>
#include <vector>

namespace qtchem {

// Common shape for one SOLUTION_SPECIES or PHASES entry. Structured fields
// are first-class; anything we don't model (e.g. -no_check, -millero,
// -llnl_gamma, isotope refs) is preserved in extra_lines, verbatim.
struct EditableEntry {
  // For an aqueous species: the species being defined (derived from the
  // RHS of the equation, e.g. "MgCO3" in "Mg+2 + CO3-2 = MgCO3").
  // For a phase: the phase name (e.g. "Calcite").
  std::string name;
  std::string equation;        // "CaCO3 = CO3-2 + Ca+2"
  bool has_log_k = false;
  double log_k = 0.0;
  bool has_delta_h = false;
  double delta_h = 0.0;
  std::string delta_h_unit;    // "kcal" / "kJ" / ""
  std::vector<double> analytic_coeffs;  // -analytical_expression
  bool has_gamma = false;
  double gamma_a0 = 0.0;
  double gamma_b = 0.0;
  std::vector<std::string> extra_lines;  // preserved verbatim
};

// In-memory editable view of a PHREEQC .dat database file.
//
// Two modes of editing coexist:
//   * Structured: aqueousSpecies() / phases() expose entries, mutated via
//     replace*/append*/remove*. Other sections are preserved verbatim.
//   * Raw text: setText() replaces the whole buffer. Subsequent reparse()
//     may or may not succeed.
//
// text() always reflects the latest serialization of the structured model,
// or the latest raw text if the buffer was set via setText() and reparse()
// has not been called since.
class EditableDatabase {
 public:
  bool load(const std::string& path, std::string* err = nullptr);

  // Returns the latest text — either the file as loaded, the serialization
  // of the current structured model, or whatever was last passed to
  // setText().
  const std::string& text() const { return text_; }

  // Replace the entire buffer (used by the raw-text editor). Does NOT
  // re-parse; the structured view is now stale until reparse() is called.
  void setText(std::string t);

  // Parse the current text() into prologue + aqueous + between + phases +
  // epilogue. Returns true if the file structure matches our supported
  // shape (one SOLUTION_SPECIES section and one PHASES section, in either
  // order). On failure, fills *err with a short description.
  bool reparse(std::string* err = nullptr);

  bool structuredAvailable() const { return structured_ok_; }

  // Number of SOLUTION_SPECIES / PHASES section headers found in the
  // original file. PHREEQC accepts multiple of each (they concatenate);
  // when >1, we merge all entries into the first occurrence on save and
  // leave the later headers as empty sections.
  int aqueousSectionCount() const { return aqueous_section_count_; }
  int phaseSectionCount() const { return phase_section_count_; }

  const std::vector<EditableEntry>& aqueousSpecies() const { return aqueous_; }
  const std::vector<EditableEntry>& phases() const { return phases_; }

  void replaceAqueous(size_t i, EditableEntry e);
  void appendAqueous(EditableEntry e);
  void removeAqueous(size_t i);
  void replacePhase(size_t i, EditableEntry e);
  void appendPhase(EditableEntry e);
  void removePhase(size_t i);

  // Regenerate text_ from the current structured model. Returns the new
  // text. Has no effect (returns current text_) if structured editing is
  // unavailable.
  const std::string& reserialize();

  // Run the current text through a throwaway IPhreeqc instance to check
  // syntax. Returns true if PHREEQC accepts it.
  bool validate(std::string* err = nullptr) const;

  // Atomically write the current text to `path` (write-to-temp + rename).
  bool saveAs(const std::string& path, std::string* err = nullptr) const;

 private:
  std::string text_;

  bool structured_ok_ = false;

  // The file is decomposed into an ordered sequence of segments. Each
  // segment is either verbatim text (prologue, inter-section content,
  // epilogue, or non-managed sections) or a managed-section header
  // marker. On serialize we walk segments in order; the first
  // section-header marker of each type emits the full aqueous_/phases_
  // list, subsequent markers emit only the header line.
  struct Segment {
    enum class Kind { Verbatim, AqueousHeader, PhaseHeader };
    Kind kind = Kind::Verbatim;
    std::vector<std::string> lines;  // verbatim lines; for headers, the single header line
  };
  std::vector<Segment> segments_;

  std::vector<EditableEntry> aqueous_;
  std::vector<EditableEntry> phases_;
  int aqueous_section_count_ = 0;
  int phase_section_count_ = 0;
};

}
