#pragma once

#include <string>

namespace qtchem {

// In-memory editable view of a PHREEQC .dat database file. Phase 2 stores
// the file as raw text; Phase 3 will extend this with structured blocks.
class EditableDatabase {
 public:
  bool load(const std::string& path, std::string* err = nullptr);

  const std::string& text() const { return text_; }
  void setText(std::string t) { text_ = std::move(t); }

  // Try to load the current text into a throwaway IPhreeqc instance to
  // check syntax. Returns true if PHREEQC accepts it. On failure, *err
  // receives the PHREEQC error message (untouched if err is null).
  bool validate(std::string* err = nullptr) const;

  // Atomically write the current text to `path` (write-to-temp + rename).
  bool saveAs(const std::string& path, std::string* err = nullptr) const;

 private:
  std::string text_;
};

}
