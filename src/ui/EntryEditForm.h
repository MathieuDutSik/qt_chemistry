#pragma once

#include "kernel/EditableDatabase.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QDialogButtonBox;

namespace qtchem {

// Modal form for editing a single SOLUTION_SPECIES or PHASES entry.
// Used for both Add and Edit; on accept(), entry() returns the new value.
class EntryEditForm : public QDialog {
  Q_OBJECT
 public:
  EntryEditForm(EditableEntry initial, bool is_phase, QWidget* parent = nullptr);

  // Valid only after exec() == Accepted.
  const EditableEntry& entry() const { return entry_; }

 private slots:
  void onAccept();

 private:
  bool is_phase_;
  EditableEntry entry_;

  QLineEdit* name_edit_ = nullptr;       // phases only (species name derived)
  QLabel* name_label_ = nullptr;         // species: shown derived from RHS
  QLineEdit* equation_edit_ = nullptr;

  QCheckBox* has_log_k_ = nullptr;
  QLineEdit* log_k_edit_ = nullptr;

  QCheckBox* has_delta_h_ = nullptr;
  QLineEdit* delta_h_edit_ = nullptr;
  QLineEdit* delta_h_unit_edit_ = nullptr;

  QCheckBox* has_gamma_ = nullptr;
  QLineEdit* gamma_a0_edit_ = nullptr;
  QLineEdit* gamma_b_edit_ = nullptr;

  QLineEdit* analytic_edit_ = nullptr;   // space-separated doubles

  QPlainTextEdit* extras_view_ = nullptr;  // read-only, shows verbatim
};

}
