#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace qtchem {

class EditableDatabase;

// Modal editor for a user database file. Three tabs:
//   * Aqueous species: structured table with Add/Edit/Remove
//   * Phases: structured table with Add/Edit/Remove
//   * Raw text: full-file editor as a fallback for non-structured edits
//
// Structured tabs are disabled if reparse() reports an unsupported file
// shape; raw editing remains available. The raw tab is read-only by
// default; the user must toggle "Enable raw editing" to opt in to making
// edits there, which then disables the structured tabs until the file is
// re-parsed.
class DatabaseEditorDialog : public QDialog {
  Q_OBJECT
 public:
  DatabaseEditorDialog(const QString& filePath, QWidget* parent = nullptr);
  ~DatabaseEditorDialog() override;

 protected:
  void closeEvent(QCloseEvent* e) override;

 private slots:
  void onSave();
  void onRawToggle(bool on);
  void onRawReparse();
  void onAddAqueous();
  void onEditAqueous();
  void onRemoveAqueous();
  void onAddPhase();
  void onEditPhase();
  void onRemovePhase();

 private:
  enum class AuthoritativeSource { Structured, Raw };

  void refreshAqueousTable();
  void refreshPhasesTable();
  void refreshRawFromStructured();
  void markDirty();
  bool confirmDiscard();
  void setStructuredEnabled(bool on);

  QString file_path_;
  EditableDatabase* db_;  // owned

  AuthoritativeSource source_ = AuthoritativeSource::Structured;
  bool dirty_ = false;

  QTabWidget* tabs_ = nullptr;
  QTableWidget* aqueous_table_ = nullptr;
  QTableWidget* phases_table_ = nullptr;
  QPushButton* aq_add_ = nullptr;
  QPushButton* aq_edit_ = nullptr;
  QPushButton* aq_remove_ = nullptr;
  QPushButton* ph_add_ = nullptr;
  QPushButton* ph_edit_ = nullptr;
  QPushButton* ph_remove_ = nullptr;

  QPlainTextEdit* raw_editor_ = nullptr;
  QCheckBox* raw_enable_box_ = nullptr;
  QPushButton* raw_reparse_btn_ = nullptr;

  QPushButton* save_btn_ = nullptr;
  QLabel* status_label_ = nullptr;
};

}
