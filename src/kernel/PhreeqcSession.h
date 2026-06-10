#pragma once

#include "kernel/PhreeqcOutputParser.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

class IPhreeqc;
class PhreeqcRM;

namespace qtchem {

class DatabaseInfo;
struct EquilibriumProblem;

struct SolveResult {
  std::string raw_input;
  std::string raw_output;
  std::vector<std::vector<std::string>> selected_output;
  std::string error_string;
  std::string warning_string;
  bool ok = false;
};

class PhreeqcSession {
public:
  PhreeqcSession();
  ~PhreeqcSession();

  PhreeqcSession(const PhreeqcSession&) = delete;
  PhreeqcSession& operator=(const PhreeqcSession&) = delete;

  bool loadDatabase(const std::string& path, std::string* err = nullptr);
  bool databaseLoaded() const { return database_loaded_; }
  const std::string& databasePath() const { return database_path_; }

  // Pass `db` if the problem uses an activity-coefficient override; it is
  // forwarded to EquilibriumProblem::toPhreeqcInput().
  SolveResult solveEquilibrium(const EquilibriumProblem&,
                               const DatabaseInfo* db = nullptr);

  SolveResult runRawInput(const std::string& phreeqc_input);

  // Full-precision element totals read directly from the selected-output
  // VAR table (typed) instead of parsed from the PHREEQC text dump.
  // Returns one row per element name in `elements`, in the same order; an
  // element whose column is absent from selected output gets NaN molality.
  // Must be called after a successful solve — reads state from the last
  // RunString.
  std::vector<ElementTotalRow> readTypedTotals(
      const std::vector<std::string>& elements) const;

  // Overwrite the molality / moles of matching entries in `po`'s last
  // frame's totals with the typed values from the last solve. Element
  // rows in the parsed output that have no typed counterpart (e.g. redox
  // states the user did not request) are left untouched. No-op if `po`
  // has no frames or the typed lookup fails.
  void refineParsedTotals(ParsedOutput& po,
                           const std::vector<std::string>& elements) const;

private:
  std::unique_ptr<PhreeqcRM> impl_;
  // Non-owning pointer to the Utility IPhreeqc instance hosted by `impl_`.
  // We run all keyword input on this instance — its behaviour is the same
  // as a free-standing IPhreeqc but with PhreeqcRM managing the lifetime
  // (and ensuring database state is replicated across all RM instances).
  IPhreeqc* util_ = nullptr;
  bool database_loaded_ = false;
  // True if the in-memory species database has been mutated by a previous
  // activity-override run (SOLUTION_SPECIES overrides persist across calls
  // to RunString, so we must reload before any non-overridden run).
  bool database_dirtied_ = false;
  std::string database_path_;
};

}
