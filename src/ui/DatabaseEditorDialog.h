#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QPushButton;
class QLabel;

namespace qtchem {

class EditableDatabase;

// Modal editor for a user database file. Phase 2: a single raw-text tab
// with validate-on-save. Phase 3 will add structured tabs for
// SOLUTION_SPECIES and PHASES.
class DatabaseEditorDialog : public QDialog {
  Q_OBJECT
 public:
  // `filePath` is the absolute path to a user-writable .dat file.
  DatabaseEditorDialog(const QString& filePath, QWidget* parent = nullptr);
  ~DatabaseEditorDialog() override;

 protected:
  void closeEvent(QCloseEvent* e) override;

 private slots:
  void onSave();
  void onTextChanged();

 private:
  bool confirmDiscard();

  QString file_path_;
  EditableDatabase* db_;  // owned
  QPlainTextEdit* editor_ = nullptr;
  QPushButton* save_btn_ = nullptr;
  QLabel* status_label_ = nullptr;
  bool dirty_ = false;
};

}
