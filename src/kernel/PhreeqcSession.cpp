#include "kernel/PhreeqcSession.h"
#include "kernel/EquilibriumProblem.h"

#include <IPhreeqc.hpp>

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

PhreeqcSession::PhreeqcSession() : impl_(std::make_unique<IPhreeqc>()) {
  impl_->SetOutputStringOn(true);
  impl_->SetErrorStringOn(true);
  impl_->SetSelectedOutputStringOn(true);
  impl_->SetOutputFileOn(false);
  impl_->SetErrorFileOn(false);
  impl_->SetSelectedOutputFileOn(false);
  impl_->SetLogFileOn(false);
  impl_->SetDumpFileOn(false);
}

PhreeqcSession::~PhreeqcSession() = default;

bool PhreeqcSession::loadDatabase(const std::string& path, std::string* err) {
  CNumericLocaleGuard locale_guard;
  const int errors = impl_->LoadDatabase(path.c_str());
  if (errors != 0) {
    database_loaded_ = false;
    if (err) *err = impl_->GetErrorString();
    return false;
  }
  database_loaded_ = true;
  database_path_ = path;
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
  const int errors = impl_->RunString(input.c_str());
  r.raw_output = impl_->GetOutputString();
  r.warning_string = impl_->GetWarningString();
  if (errors != 0) {
    r.error_string = impl_->GetErrorString();
    r.ok = false;
  } else {
    r.selected_output = readSelectedOutputAsStrings(*impl_);
    r.ok = true;
  }
  return r;
}

SolveResult PhreeqcSession::solveEquilibrium(const EquilibriumProblem& p) {
  return runRawInput(p.toPhreeqcInput());
}

}
