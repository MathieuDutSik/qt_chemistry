#include "kernel/PhreeqcSession.h"
#include "kernel/DatabaseInfo.h"
#include "kernel/EquilibriumProblem.h"

#include <IPhreeqc.hpp>
#include <PhreeqcRM.h>

#include <clocale>
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
}

PhreeqcSession::PhreeqcSession()
    : impl_(std::make_unique<PhreeqcRM>(1, 1)) {
  impl_->SetErrorHandlerMode(0);
  impl_->SetScreenOn(false);
  impl_->SetPrintChemistryOn(false, false, false);
  // PhreeqcRM hosts (thread_count + 2) IPhreeqc instances internally:
  // workers [0..n-1], InitialPhreeqc at n, Utility at n+1. We drive all
  // keyword input through the Utility instance so it is independent of
  // any in-flight per-cell chemistry state.
  util_ = impl_->GetIPhreeqcPointer(impl_->GetThreadCount() + 1);
  util_->SetOutputStringOn(true);
  util_->SetErrorStringOn(true);
  util_->SetSelectedOutputStringOn(true);
  util_->SetOutputFileOn(false);
  util_->SetErrorFileOn(false);
  util_->SetSelectedOutputFileOn(false);
  util_->SetLogFileOn(false);
  util_->SetDumpFileOn(false);
}

PhreeqcSession::~PhreeqcSession() = default;

bool PhreeqcSession::loadDatabase(const std::string& path, std::string* err) {
  CNumericLocaleGuard locale_guard;
  // PhreeqcRM::LoadDatabase loads the database into every hosted IPhreeqc
  // instance, including our Utility instance.
  if (impl_->LoadDatabase(path) != IRM_OK) {
    database_loaded_ = false;
    if (err) *err = util_->GetErrorString();
    return false;
  }
  database_loaded_ = true;
  database_path_ = path;
  database_dirtied_ = false;
  return true;
}

static std::vector<std::vector<std::string>>
readSelectedOutputAsStrings(IPhreeqc& iph) {
  std::vector<std::vector<std::string>> rows;
  const int n_rows = iph.GetSelectedOutputRowCount();
  const int n_cols = iph.GetSelectedOutputColumnCount();
  rows.reserve(n_rows);
  for (int r = 0; r < n_rows; ++r) {
    std::vector<std::string> row;
    row.reserve(n_cols);
    for (int c = 0; c < n_cols; ++c) {
      VAR v;
      VarInit(&v);
      iph.GetSelectedOutputValue(r, c, &v);
      switch (v.type) {
        case TT_EMPTY:  row.emplace_back(""); break;
        case TT_ERROR:  row.emplace_back("<err>"); break;
        case TT_LONG:   row.emplace_back(std::to_string(v.lVal)); break;
        case TT_DOUBLE: row.emplace_back(std::to_string(v.dVal)); break;
        case TT_STRING: row.emplace_back(v.sVal ? v.sVal : ""); break;
      }
      VarClear(&v);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

SolveResult PhreeqcSession::runRawInput(const std::string& input) {
  SolveResult r;
  r.raw_input = input;
  if (!database_loaded_) {
    r.error_string = "No database loaded";
    return r;
  }
  CNumericLocaleGuard locale_guard;
  const int errors = util_->RunString(input.c_str());
  r.raw_output = util_->GetOutputString();
  r.warning_string = util_->GetWarningString();
  if (errors != 0) {
    r.error_string = util_->GetErrorString();
    r.ok = false;
  } else {
    r.selected_output = readSelectedOutputAsStrings(*util_);
    r.ok = true;
  }
  return r;
}

SolveResult PhreeqcSession::solveEquilibrium(const EquilibriumProblem& p,
                                             const DatabaseInfo* db) {
  // A previous override run may have mutated the species database; the
  // mutations persist across RunString calls. If we're now asked for the
  // database default, reload to restore the pristine model.
  if (p.activity_override == ActivityOverride::UseDatabase &&
      database_dirtied_ && !database_path_.empty()) {
    std::string err;
    loadDatabase(database_path_, &err);  // resets database_dirtied_
  }
  SolveResult r = runRawInput(p.toPhreeqcInput(db));
  if (p.activity_override != ActivityOverride::UseDatabase) {
    database_dirtied_ = true;
  }
  return r;
}

}
