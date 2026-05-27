#include "kernel/EditableDatabase.h"

#include <IPhreeqc.hpp>

#include <clocale>
#include <cstdio>
#include <fstream>
#include <memory>
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

}

bool EditableDatabase::load(const std::string& path, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open " + path;
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  text_ = ss.str();
  return true;
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
