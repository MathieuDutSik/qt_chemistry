#pragma once

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
