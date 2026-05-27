#include "kernel/EditableDatabase.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef QTCHEM_TEST_DATABASE_DIR
#  define QTCHEM_TEST_DATABASE_DIR ""
#endif

static int fail(const char* msg) {
  std::fprintf(stderr, "FAIL: %s\n", msg);
  return 1;
}

static int roundTrip(const std::string& db_path) {
  qtchem::EditableDatabase db;
  std::string err;
  if (!db.load(db_path, &err)) {
    std::fprintf(stderr, "load %s: %s\n", db_path.c_str(), err.c_str());
    return fail("load");
  }
  // Original file must validate.
  if (!db.validate(&err)) {
    std::fprintf(stderr, "original validate failed for %s: %s\n",
                 db_path.c_str(), err.c_str());
    return fail("original validate");
  }
  if (!db.reparse(&err)) {
    std::fprintf(stderr, "reparse %s: %s\n", db_path.c_str(), err.c_str());
    return fail("reparse");
  }
  if (db.aqueousSpecies().empty() && db.phases().empty())
    return fail("no entries parsed");
  // Round-trip: regenerate from structured model and ensure PHREEQC still
  // accepts it.
  db.reserialize();
  if (!db.validate(&err)) {
    std::fprintf(stderr, "reserialized %s rejected: %s\n",
                 db_path.c_str(), err.c_str());
    return fail("reserialized validate");
  }
  std::printf("OK %s — %zu species, %zu phases\n",
              db_path.c_str(),
              db.aqueousSpecies().size(),
              db.phases().size());
  return 0;
}

int main() {
  const std::string db_dir = QTCHEM_TEST_DATABASE_DIR;
  if (db_dir.empty()) return fail("QTCHEM_TEST_DATABASE_DIR not set");

  const std::vector<std::string> databases = {
      "phreeqc.dat",
      "wateq4f.dat",
      "minteq.dat",
      "minteq.v4.dat",
      "sit.dat",
      "iso.dat",
      "llnl.dat",
      "pitzer.dat",
  };
  for (const auto& name : databases) {
    if (int rc = roundTrip(db_dir + "/" + name); rc != 0) return rc;
  }
  return 0;
}
