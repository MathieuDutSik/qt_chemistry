#include "ui/DatabaseEditorDialog.h"

#include "kernel/EditableDatabase.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace qtchem {

DatabaseEditorDialog::DatabaseEditorDialog(const QString& filePath,
                                           QWidget* parent)
    : QDialog(parent), file_path_(filePath), db_(new EditableDatabase) {
  setWindowTitle(tr("Edit database — %1").arg(QFileInfo(filePath).fileName()));
  resize(900, 700);

  auto* layout = new QVBoxLayout(this);

  auto* header = new QLabel(
      tr("Editing user database: <code>%1</code>")
          .arg(QDir::toNativeSeparators(filePath).toHtmlEscaped()));
  header->setTextFormat(Qt::RichText);
  header->setStyleSheet(QStringLiteral("color:#444;"));
  layout->addWidget(header);

  editor_ = new QPlainTextEdit;
  editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
  editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  editor_->setTabStopDistance(4 * editor_->fontMetrics().horizontalAdvance(
                                      QLatin1Char(' ')));
  layout->addWidget(editor_, 1);

  status_label_ = new QLabel;
  status_label_->setWordWrap(true);
  status_label_->setStyleSheet(QStringLiteral("color:#888;"));
  layout->addWidget(status_label_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  save_btn_ = buttons->button(QDialogButtonBox::Save);
  save_btn_->setEnabled(false);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &DatabaseEditorDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  std::string err;
  if (!db_->load(filePath.toStdString(), &err)) {
    QMessageBox::critical(this, tr("Cannot open database"),
                          QString::fromStdString(err));
    QMetaObject::invokeMethod(this, &QDialog::reject, Qt::QueuedConnection);
    return;
  }
  editor_->setPlainText(QString::fromStdString(db_->text()));
  dirty_ = false;
  status_label_->setText(
      tr("Edits are validated by PHREEQC on Save; you cannot save a file "
         "that PHREEQC cannot load."));
  connect(editor_, &QPlainTextEdit::textChanged, this,
          &DatabaseEditorDialog::onTextChanged);
}

DatabaseEditorDialog::~DatabaseEditorDialog() {
  delete db_;
}

void DatabaseEditorDialog::onTextChanged() {
  if (!dirty_) {
    dirty_ = true;
    save_btn_->setEnabled(true);
    setWindowTitle(tr("Edit database — %1 *")
                       .arg(QFileInfo(file_path_).fileName()));
  }
}

bool DatabaseEditorDialog::confirmDiscard() {
  if (!dirty_) return true;
  const auto ans = QMessageBox::question(this, tr("Discard changes?"),
      tr("You have unsaved changes. Discard them?"),
      QMessageBox::Discard | QMessageBox::Cancel);
  return ans == QMessageBox::Discard;
}

void DatabaseEditorDialog::closeEvent(QCloseEvent* e) {
  if (confirmDiscard()) e->accept();
  else                  e->ignore();
}

void DatabaseEditorDialog::onSave() {
  db_->setText(editor_->toPlainText().toStdString());
  std::string err;
  if (!db_->validate(&err)) {
    auto* msg = new QMessageBox(QMessageBox::Warning,
        tr("PHREEQC rejected the database"),
        tr("PHREEQC could not load the edited database. Fix the errors "
           "below and try again; the file on disk has not been changed."),
        QMessageBox::Ok, this);
    msg->setDetailedText(QString::fromStdString(err));
    msg->setAttribute(Qt::WA_DeleteOnClose);
    msg->exec();
    return;
  }
  if (!db_->saveAs(file_path_.toStdString(), &err)) {
    QMessageBox::critical(this, tr("Save failed"),
                          QString::fromStdString(err));
    return;
  }
  dirty_ = false;
  accept();
}

}
