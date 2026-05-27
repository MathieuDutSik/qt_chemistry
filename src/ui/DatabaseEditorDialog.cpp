#include "ui/DatabaseEditorDialog.h"

#include "kernel/EditableDatabase.h"
#include "ui/ChemDelegate.h"
#include "ui/EntryEditForm.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace qtchem {

namespace {

QTableWidgetItem* textItem(const QString& s) {
  auto* it = new QTableWidgetItem(s);
  return it;
}

QTableWidgetItem* numItem(double v, bool present, char fmt = 'g',
                          int prec = 4) {
  auto* it = new QTableWidgetItem(
      present ? QString::number(v, fmt, prec) : QStringLiteral("—"));
  it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return it;
}

// Master-species detector: an aqueous entry whose reaction is a literal
// self-declaration "X = X" (modulo whitespace). PHREEQC uses these to
// attach activity-coefficient parameters to the master species; they are
// not real reactions.
bool isMasterEntry(const EditableEntry& e) {
  const std::string& eq = e.equation;
  const auto pos = eq.find('=');
  if (pos == std::string::npos) return false;
  auto trim = [](const std::string& s) {
    size_t a = 0; while (a < s.size() &&
        std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size(); while (b > a &&
        std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
  };
  const std::string lhs = trim(eq.substr(0, pos));
  const std::string rhs = trim(eq.substr(pos + 1));
  return !lhs.empty() && lhs == rhs;
}

QTableWidget* buildEntryTable(const QStringList& headers) {
  auto* t = new QTableWidget(0, headers.size());
  t->setHorizontalHeaderLabels(headers);
  t->horizontalHeader()->setStretchLastSection(true);
  t->verticalHeader()->setVisible(false);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setAlternatingRowColors(true);
  t->setSortingEnabled(true);
  return t;
}

}  // namespace

DatabaseEditorDialog::DatabaseEditorDialog(const QString& filePath,
                                           QWidget* parent)
    : QDialog(parent), file_path_(filePath), db_(new EditableDatabase) {
  setWindowTitle(tr("Edit database — %1").arg(QFileInfo(filePath).fileName()));
  resize(1100, 760);

  auto* outer = new QVBoxLayout(this);

  auto* header = new QLabel(
      tr("Editing user database: <code>%1</code>")
          .arg(QDir::toNativeSeparators(filePath).toHtmlEscaped()));
  header->setTextFormat(Qt::RichText);
  header->setStyleSheet(QStringLiteral("color:#444;"));
  outer->addWidget(header);

  tabs_ = new QTabWidget;
  outer->addWidget(tabs_, 1);

  // ----- Aqueous species tab -----
  {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    aqueous_table_ = buildEntryTable({tr("Species"), tr("Reaction"),
                                       tr("log K"), tr("ΔH"), tr("ΔH unit"),
                                       tr("γ a₀"), tr("γ b"),
                                       tr("Analytic coeffs")});
    aqueous_table_->setItemDelegateForColumn(
        1, new ChemDelegate(ChemDelegate::Mode::Reaction, this));
    lay->addWidget(aqueous_table_, 1);
    auto* btns = new QHBoxLayout;
    aq_add_    = new QPushButton(tr("Add…"));
    aq_edit_   = new QPushButton(tr("Edit…"));
    aq_remove_ = new QPushButton(tr("Remove"));
    btns->addWidget(aq_add_);
    btns->addWidget(aq_edit_);
    btns->addWidget(aq_remove_);
    btns->addStretch(1);
    lay->addLayout(btns);
    tabs_->addTab(page, tr("Aqueous species"));
  }
  // ----- Master species tab -----
  {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    auto* hint = new QLabel(tr(
        "<b>Master species</b> are the reference aqueous species for an "
        "element (or its redox state) — e.g. <code>Zn²⁺</code> for zinc. "
        "They appear in <code>SOLUTION_SPECIES</code> as "
        "<code>X = X</code> self-declarations so PHREEQC can attach an "
        "activity-coefficient form. They are not real reactions; the "
        "<i>log K</i> is conventionally 0."));
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#555;"));
    lay->addWidget(hint);
    master_table_ = buildEntryTable({tr("Species"), tr("γ a₀"), tr("γ b"),
                                      tr("Analytic coeffs")});
    lay->addWidget(master_table_, 1);
    auto* btns = new QHBoxLayout;
    ms_add_    = new QPushButton(tr("Add…"));
    ms_edit_   = new QPushButton(tr("Edit…"));
    ms_remove_ = new QPushButton(tr("Remove"));
    btns->addWidget(ms_add_);
    btns->addWidget(ms_edit_);
    btns->addWidget(ms_remove_);
    btns->addStretch(1);
    lay->addLayout(btns);
    tabs_->addTab(page, tr("Master species"));
  }
  // ----- Phases tab -----
  {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    phases_table_ = buildEntryTable({tr("Phase"), tr("Reaction"), tr("log K"),
                                      tr("ΔH"), tr("ΔH unit"),
                                      tr("Analytic coeffs")});
    phases_table_->setItemDelegateForColumn(
        1, new ChemDelegate(ChemDelegate::Mode::Reaction, this));
    lay->addWidget(phases_table_, 1);
    auto* btns = new QHBoxLayout;
    ph_add_    = new QPushButton(tr("Add…"));
    ph_edit_   = new QPushButton(tr("Edit…"));
    ph_remove_ = new QPushButton(tr("Remove"));
    btns->addWidget(ph_add_);
    btns->addWidget(ph_edit_);
    btns->addWidget(ph_remove_);
    btns->addStretch(1);
    lay->addLayout(btns);
    tabs_->addTab(page, tr("Phases"));
  }
  // ----- Raw text tab -----
  {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    auto* row = new QHBoxLayout;
    raw_enable_box_ = new QCheckBox(tr("Enable raw editing"));
    raw_enable_box_->setToolTip(tr(
        "Allow free-form edits in this tab. While enabled, the structured "
        "tabs are read-only — they go stale because raw edits may add, "
        "reorder, or remove sections beyond what the structured model "
        "understands. Click 'Re-parse from raw' to rebuild the structured "
        "view from the current text."));
    raw_reparse_btn_ = new QPushButton(tr("Re-parse from raw"));
    raw_reparse_btn_->setEnabled(false);
    row->addWidget(raw_enable_box_);
    row->addWidget(raw_reparse_btn_);
    row->addStretch(1);
    lay->addLayout(row);

    raw_editor_ = new QPlainTextEdit;
    raw_editor_->setReadOnly(true);
    raw_editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    raw_editor_->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    raw_editor_->setTabStopDistance(
        4 * raw_editor_->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
    lay->addWidget(raw_editor_, 1);
    tabs_->addTab(page, tr("Raw text"));
  }

  status_label_ = new QLabel;
  status_label_->setWordWrap(true);
  status_label_->setStyleSheet(QStringLiteral("color:#888;"));
  outer->addWidget(status_label_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  save_btn_ = buttons->button(QDialogButtonBox::Save);
  save_btn_->setEnabled(false);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &DatabaseEditorDialog::onSave);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  outer->addWidget(buttons);

  // Load the file.
  std::string err;
  if (!db_->load(filePath.toStdString(), &err)) {
    QMessageBox::critical(this, tr("Cannot open database"),
                          QString::fromStdString(err));
    QMetaObject::invokeMethod(this, &QDialog::reject, Qt::QueuedConnection);
    return;
  }
  raw_editor_->setPlainText(QString::fromStdString(db_->text()));

  // Try to parse into the structured model. If it fails, force raw mode.
  if (!db_->reparse(&err)) {
    raw_enable_box_->setChecked(true);
    raw_enable_box_->setEnabled(false);
    setStructuredEnabled(false);
    source_ = AuthoritativeSource::Raw;
    raw_editor_->setReadOnly(false);
    status_label_->setText(
        tr("Structured editing unavailable for this file: %1 "
           "Raw editing only; changes are validated on Save.")
            .arg(QString::fromStdString(err)));
  } else {
    refreshAqueousTable();
    refreshMasterTable();
    refreshPhasesTable();
    QString notice;
    if (db_->aqueousSectionCount() > 1 || db_->phaseSectionCount() > 1) {
      notice = tr("Note: this database has multiple SOLUTION_SPECIES or "
                  "PHASES sections; on Save they are merged into the first "
                  "occurrence of each. ");
    }
    status_label_->setText(
        notice + tr("Edits are validated by PHREEQC on Save; you cannot save "
                    "a file that PHREEQC cannot load."));
  }

  connect(aq_add_,    &QPushButton::clicked, this, &DatabaseEditorDialog::onAddAqueous);
  connect(aq_edit_,   &QPushButton::clicked, this, &DatabaseEditorDialog::onEditAqueous);
  connect(aq_remove_, &QPushButton::clicked, this, &DatabaseEditorDialog::onRemoveAqueous);
  connect(aqueous_table_, &QTableWidget::doubleClicked, this,
          &DatabaseEditorDialog::onEditAqueous);

  connect(ms_add_,    &QPushButton::clicked, this, &DatabaseEditorDialog::onAddMaster);
  connect(ms_edit_,   &QPushButton::clicked, this, &DatabaseEditorDialog::onEditMaster);
  connect(ms_remove_, &QPushButton::clicked, this, &DatabaseEditorDialog::onRemoveMaster);
  connect(master_table_, &QTableWidget::doubleClicked, this,
          &DatabaseEditorDialog::onEditMaster);

  connect(ph_add_,    &QPushButton::clicked, this, &DatabaseEditorDialog::onAddPhase);
  connect(ph_edit_,   &QPushButton::clicked, this, &DatabaseEditorDialog::onEditPhase);
  connect(ph_remove_, &QPushButton::clicked, this, &DatabaseEditorDialog::onRemovePhase);
  connect(phases_table_, &QTableWidget::doubleClicked, this,
          &DatabaseEditorDialog::onEditPhase);

  connect(raw_enable_box_, &QCheckBox::toggled, this,
          &DatabaseEditorDialog::onRawToggle);
  connect(raw_reparse_btn_, &QPushButton::clicked, this,
          &DatabaseEditorDialog::onRawReparse);
  connect(raw_editor_, &QPlainTextEdit::textChanged, this, [this]() {
    if (raw_editor_->isReadOnly()) return;
    source_ = AuthoritativeSource::Raw;
    markDirty();
  });
}

DatabaseEditorDialog::~DatabaseEditorDialog() {
  delete db_;
}

void DatabaseEditorDialog::setStructuredEnabled(bool on) {
  aqueous_table_->setEnabled(on);
  master_table_->setEnabled(on);
  phases_table_->setEnabled(on);
  aq_add_->setEnabled(on);
  aq_edit_->setEnabled(on);
  aq_remove_->setEnabled(on);
  ms_add_->setEnabled(on);
  ms_edit_->setEnabled(on);
  ms_remove_->setEnabled(on);
  ph_add_->setEnabled(on);
  ph_edit_->setEnabled(on);
  ph_remove_->setEnabled(on);
}

void DatabaseEditorDialog::markDirty() {
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

void DatabaseEditorDialog::refreshAqueousTable() {
  const QSignalBlocker b(aqueous_table_);
  aqueous_table_->setSortingEnabled(false);
  aqueous_row_to_index_.clear();
  const auto& list = db_->aqueousSpecies();
  for (size_t i = 0; i < list.size(); ++i)
    if (!isMasterEntry(list[i])) aqueous_row_to_index_.push_back(i);
  aqueous_table_->setRowCount(static_cast<int>(aqueous_row_to_index_.size()));
  for (size_t row = 0; row < aqueous_row_to_index_.size(); ++row) {
    const auto& e = list[aqueous_row_to_index_[row]];
    aqueous_table_->setItem(row, 0, textItem(QString::fromStdString(e.name)));
    aqueous_table_->setItem(row, 1, textItem(QString::fromStdString(e.equation)));
    aqueous_table_->setItem(row, 2, numItem(e.log_k, e.has_log_k, 'f', 3));
    aqueous_table_->setItem(row, 3, numItem(e.delta_h, e.has_delta_h, 'f', 3));
    aqueous_table_->setItem(row, 4, textItem(
        e.has_delta_h ? QString::fromStdString(e.delta_h_unit)
                      : QStringLiteral("—")));
    aqueous_table_->setItem(row, 5,
        numItem(e.gamma_a0, e.has_gamma, 'f', 3));
    aqueous_table_->setItem(row, 6,
        numItem(e.gamma_b,  e.has_gamma, 'f', 3));
    aqueous_table_->setItem(row, 7, textItem(
        e.analytic_coeffs.empty()
            ? QStringLiteral("—")
            : tr("%1 values").arg(e.analytic_coeffs.size())));
  }
  aqueous_table_->setSortingEnabled(true);
  aqueous_table_->resizeColumnsToContents();
}

void DatabaseEditorDialog::refreshMasterTable() {
  const QSignalBlocker b(master_table_);
  master_table_->setSortingEnabled(false);
  master_row_to_index_.clear();
  const auto& list = db_->aqueousSpecies();
  for (size_t i = 0; i < list.size(); ++i)
    if (isMasterEntry(list[i])) master_row_to_index_.push_back(i);
  master_table_->setRowCount(static_cast<int>(master_row_to_index_.size()));
  for (size_t row = 0; row < master_row_to_index_.size(); ++row) {
    const auto& e = list[master_row_to_index_[row]];
    master_table_->setItem(row, 0, textItem(QString::fromStdString(e.name)));
    master_table_->setItem(row, 1,
        numItem(e.gamma_a0, e.has_gamma, 'f', 3));
    master_table_->setItem(row, 2,
        numItem(e.gamma_b,  e.has_gamma, 'f', 3));
    master_table_->setItem(row, 3, textItem(
        e.analytic_coeffs.empty()
            ? QStringLiteral("—")
            : tr("%1 values").arg(e.analytic_coeffs.size())));
  }
  master_table_->setSortingEnabled(true);
  master_table_->resizeColumnsToContents();
}

void DatabaseEditorDialog::refreshPhasesTable() {
  const QSignalBlocker b(phases_table_);
  phases_table_->setSortingEnabled(false);
  const auto& list = db_->phases();
  phases_table_->setRowCount(static_cast<int>(list.size()));
  for (size_t i = 0; i < list.size(); ++i) {
    const auto& e = list[i];
    phases_table_->setItem(i, 0, textItem(QString::fromStdString(e.name)));
    auto* rxn = textItem(QString::fromStdString(e.equation));
    rxn->setData(ChemDelegate::kPhaseNameRole,
                 QString::fromStdString(e.name));
    phases_table_->setItem(i, 1, rxn);
    phases_table_->setItem(i, 2, numItem(e.log_k, e.has_log_k, 'f', 3));
    phases_table_->setItem(i, 3, numItem(e.delta_h, e.has_delta_h, 'f', 3));
    phases_table_->setItem(i, 4, textItem(
        e.has_delta_h ? QString::fromStdString(e.delta_h_unit)
                      : QStringLiteral("—")));
    phases_table_->setItem(i, 5, textItem(
        e.analytic_coeffs.empty()
            ? QStringLiteral("—")
            : tr("%1 values").arg(e.analytic_coeffs.size())));
  }
  phases_table_->setSortingEnabled(true);
  phases_table_->resizeColumnsToContents();
}

void DatabaseEditorDialog::refreshRawFromStructured() {
  db_->reserialize();
  const QSignalBlocker b(raw_editor_);
  raw_editor_->setPlainText(QString::fromStdString(db_->text()));
}

void DatabaseEditorDialog::onRawToggle(bool on) {
  if (on && source_ == AuthoritativeSource::Structured) {
    // We're flipping FROM structured TO raw. Make sure raw text reflects
    // the latest structured state, then disable structured tabs.
    refreshRawFromStructured();
    raw_editor_->setReadOnly(false);
    raw_reparse_btn_->setEnabled(true);
    setStructuredEnabled(false);
    status_label_->setText(
        tr("Raw editing enabled. Structured tabs are disabled until "
           "you press 'Re-parse from raw'."));
  } else if (!on) {
    raw_editor_->setReadOnly(true);
    raw_reparse_btn_->setEnabled(false);
    // We do NOT automatically reparse — the user must explicitly press
    // the button if they want to rebuild the structured view.
  }
}

void DatabaseEditorDialog::onRawReparse() {
  db_->setText(raw_editor_->toPlainText().toStdString());
  std::string err;
  if (!db_->reparse(&err)) {
    QMessageBox::warning(this, tr("Cannot re-parse"),
        tr("The current text could not be parsed into the structured "
           "model: %1").arg(QString::fromStdString(err)));
    return;
  }
  refreshAqueousTable();
  refreshPhasesTable();
  setStructuredEnabled(true);
  source_ = AuthoritativeSource::Structured;
  raw_enable_box_->setChecked(false);  // also re-enables read-only mode.
  status_label_->setText(tr("Structured view rebuilt from raw text."));
}

namespace {

// Map a current-row from a partitioned table to a model index in
// aqueousSpecies(), going via the species name in column 0 to survive
// sorting. The fallback `row_to_index` mapping (built at refresh time) is
// only valid before the user sorts; we use the name path for robustness.
size_t modelIndexFor(QTableWidget* table, int row,
                     const std::vector<EditableEntry>& list,
                     bool want_master) {
  if (row < 0 || !table->item(row, 0)) return list.size();
  const QString name = table->item(row, 0)->text();
  for (size_t i = 0; i < list.size(); ++i) {
    if (QString::fromStdString(list[i].name) != name) continue;
    if (isMasterEntry(list[i]) != want_master) continue;
    return i;
  }
  return list.size();
}

}  // namespace

void DatabaseEditorDialog::onAddAqueous() {
  EntryEditForm form(EditableEntry{}, EntryEditForm::Kind::Aqueous, this);
  if (form.exec() != QDialog::Accepted) return;
  db_->appendAqueous(form.entry());
  refreshAqueousTable();
  refreshMasterTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onEditAqueous() {
  const int row = aqueous_table_->currentRow();
  const auto& list = db_->aqueousSpecies();
  const size_t idx = modelIndexFor(aqueous_table_, row, list, /*master=*/false);
  if (idx == list.size()) return;
  EntryEditForm form(list[idx], EntryEditForm::Kind::Aqueous, this);
  if (form.exec() != QDialog::Accepted) return;
  db_->replaceAqueous(idx, form.entry());
  refreshAqueousTable();
  refreshMasterTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onRemoveAqueous() {
  const int row = aqueous_table_->currentRow();
  const auto& list = db_->aqueousSpecies();
  const size_t idx = modelIndexFor(aqueous_table_, row, list, /*master=*/false);
  if (idx == list.size()) return;
  const QString name = QString::fromStdString(list[idx].name);
  const auto ans = QMessageBox::question(this, tr("Remove species?"),
      tr("Remove aqueous species '%1'? This cannot be undone until "
         "you cancel the dialog without saving.").arg(name));
  if (ans != QMessageBox::Yes) return;
  db_->removeAqueous(idx);
  refreshAqueousTable();
  refreshMasterTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onAddMaster() {
  EntryEditForm form(EditableEntry{}, EntryEditForm::Kind::Master, this);
  if (form.exec() != QDialog::Accepted) return;
  db_->appendAqueous(form.entry());
  refreshAqueousTable();
  refreshMasterTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onEditMaster() {
  const int row = master_table_->currentRow();
  const auto& list = db_->aqueousSpecies();
  const size_t idx = modelIndexFor(master_table_, row, list, /*master=*/true);
  if (idx == list.size()) return;
  EntryEditForm form(list[idx], EntryEditForm::Kind::Master, this);
  if (form.exec() != QDialog::Accepted) return;
  db_->replaceAqueous(idx, form.entry());
  refreshAqueousTable();
  refreshMasterTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onRemoveMaster() {
  const int row = master_table_->currentRow();
  const auto& list = db_->aqueousSpecies();
  const size_t idx = modelIndexFor(master_table_, row, list, /*master=*/true);
  if (idx == list.size()) return;
  const QString name = QString::fromStdString(list[idx].name);
  const auto ans = QMessageBox::question(this, tr("Remove master species?"),
      tr("Remove master species '%1'? This may break references from "
         "SOLUTION_MASTER_SPECIES and from derived species — use with "
         "care. Cannot be undone until you cancel without saving.")
          .arg(name));
  if (ans != QMessageBox::Yes) return;
  db_->removeAqueous(idx);
  refreshAqueousTable();
  refreshMasterTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onAddPhase() {
  EntryEditForm form(EditableEntry{}, EntryEditForm::Kind::Phase, this);
  if (form.exec() != QDialog::Accepted) return;
  db_->appendPhase(form.entry());
  refreshPhasesTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onEditPhase() {
  const int row = phases_table_->currentRow();
  if (row < 0) return;
  const QString name = phases_table_->item(row, 0)->text();
  const auto& list = db_->phases();
  size_t idx = list.size();
  for (size_t i = 0; i < list.size(); ++i)
    if (QString::fromStdString(list[i].name) == name) { idx = i; break; }
  if (idx == list.size()) return;

  EntryEditForm form(list[idx], EntryEditForm::Kind::Phase, this);
  if (form.exec() != QDialog::Accepted) return;
  db_->replacePhase(idx, form.entry());
  refreshPhasesTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onRemovePhase() {
  const int row = phases_table_->currentRow();
  if (row < 0) return;
  const QString name = phases_table_->item(row, 0)->text();
  const auto& list = db_->phases();
  size_t idx = list.size();
  for (size_t i = 0; i < list.size(); ++i)
    if (QString::fromStdString(list[i].name) == name) { idx = i; break; }
  if (idx == list.size()) return;
  const auto ans = QMessageBox::question(this, tr("Remove phase?"),
      tr("Remove phase '%1'? This cannot be undone until you cancel "
         "the dialog without saving.").arg(name));
  if (ans != QMessageBox::Yes) return;
  db_->removePhase(idx);
  refreshPhasesTable();
  refreshRawFromStructured();
  markDirty();
}

void DatabaseEditorDialog::onSave() {
  if (source_ == AuthoritativeSource::Raw) {
    db_->setText(raw_editor_->toPlainText().toStdString());
  } else {
    db_->reserialize();
  }
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
