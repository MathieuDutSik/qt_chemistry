#include "ui/MainWindow.h"

#include "kernel/EquilibriumProblem.h"
#include "kernel/PhreeqcOutputParser.h"
#include "kernel/PhreeqcSession.h"
#include "ui/SolutionPanel.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#ifndef QTCHEM_DEFAULT_DATABASE_DIR
#  define QTCHEM_DEFAULT_DATABASE_DIR ""
#endif

namespace qtchem {

namespace {

void setHeaders(QTableWidget* t, const QStringList& h) {
  t->setColumnCount(h.size());
  t->setHorizontalHeaderLabels(h);
  t->horizontalHeader()->setStretchLastSection(true);
  t->verticalHeader()->setVisible(false);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setAlternatingRowColors(true);
}

QTableWidgetItem* numItem(double v, char fmt = 'g', int prec = 4) {
  auto* it = new QTableWidgetItem(QString::number(v, fmt, prec));
  it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return it;
}

QTableWidgetItem* sciItem(double v) {
  auto* it = new QTableWidgetItem(QString::number(v, 'e', 3));
  it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  return it;
}

QTableWidgetItem* textItem(const QString& s) {
  return new QTableWidgetItem(s);
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), session_(std::make_unique<PhreeqcSession>()) {
  setWindowTitle(QStringLiteral("qt_chemistry — MVP"));
  resize(1500, 900);
  database_dir_ = QString::fromUtf8(QTCHEM_DEFAULT_DATABASE_DIR);

  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  auto* top = new QHBoxLayout;
  top->addWidget(new QLabel(tr("Database:")));
  db_combo_ = new QComboBox; db_combo_->setMinimumWidth(280);
  top->addWidget(db_combo_);
  db_status_ = new QLabel(tr("(not loaded)"));
  top->addWidget(db_status_); top->addStretch(1);
  root->addLayout(top);

  auto* split = new QSplitter(Qt::Horizontal);

  solution_panel_ = new SolutionPanel;
  split->addWidget(solution_panel_);

  result_tabs_ = new QTabWidget;

  totals_table_ = new QTableWidget(0, 3);
  setHeaders(totals_table_, {tr("Element"), tr("Molality (mol/kgw)"), tr("Moles")});
  result_tabs_->addTab(totals_table_, tr("Element totals"));

  desc_table_ = new QTableWidget(0, 2);
  setHeaders(desc_table_, {tr("Quantity"), tr("Value")});
  result_tabs_->addTab(desc_table_, tr("Solution properties"));

  species_table_ = new QTableWidget(0, 7);
  setHeaders(species_table_, {tr("Element"), tr("Species"),
                              tr("Molality"), tr("Activity"),
                              tr("log m"), tr("log a"), tr("log γ")});
  result_tabs_->addTab(species_table_, tr("Species"));

  si_table_ = new QTableWidget(0, 5);
  setHeaders(si_table_, {tr("Phase"), tr("SI"), tr("log IAP"),
                         tr("log K"), tr("Formula")});
  result_tabs_->addTab(si_table_, tr("Saturation indices"));

  assemblage_table_ = new QTableWidget(0, 5);
  setHeaders(assemblage_table_, {tr("Phase"), tr("SI"),
                                 tr("Initial (mol)"), tr("Final (mol)"),
                                 tr("Δ (mol)")});
  result_tabs_->addTab(assemblage_table_, tr("Phase assemblage"));

  input_view_ = new QPlainTextEdit;
  input_view_->setReadOnly(true);
  input_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
  input_view_->setPlaceholderText(tr("PHREEQC input as sent"));
  result_tabs_->addTab(input_view_, tr("Raw input"));

  output_view_ = new QPlainTextEdit;
  output_view_->setReadOnly(true);
  output_view_->setLineWrapMode(QPlainTextEdit::NoWrap);
  result_tabs_->addTab(output_view_, tr("Raw output"));

  split->addWidget(result_tabs_);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 2);
  root->addWidget(split, 1);

  setCentralWidget(central);
  statusBar()->showMessage(tr("Ready."));

  populateDatabaseList();
  connect(db_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MainWindow::onDatabaseChanged);
  connect(solution_panel_, &SolutionPanel::runRequested,
          this, &MainWindow::onRun);

  if (db_combo_->count() > 0) onDatabaseChanged(db_combo_->currentIndex());
}

MainWindow::~MainWindow() = default;

void MainWindow::populateDatabaseList() {
  db_combo_->clear();
  QDir d(database_dir_);
  const auto entries = d.entryInfoList(
      QStringList() << QStringLiteral("*.dat"), QDir::Files, QDir::Name);
  for (const auto& fi : entries)
    db_combo_->addItem(fi.fileName(), fi.absoluteFilePath());
  if (entries.isEmpty())
    db_combo_->addItem(tr("(no .dat files in %1)").arg(database_dir_));
  const int idx = db_combo_->findText(QStringLiteral("phreeqc.dat"));
  if (idx >= 0) db_combo_->setCurrentIndex(idx);
}

void MainWindow::onDatabaseChanged(int index) {
  const QString path = db_combo_->itemData(index).toString();
  if (path.isEmpty()) { db_status_->setText(tr("(no database)")); return; }
  std::string err;
  if (session_->loadDatabase(path.toStdString(), &err)) {
    db_status_->setText(tr("loaded ✓"));
    statusBar()->showMessage(tr("Loaded %1").arg(QFileInfo(path).fileName()));
  } else {
    db_status_->setText(tr("load failed"));
    output_view_->setPlainText(QString::fromStdString(err));
    statusBar()->showMessage(tr("Database load failed"));
  }
}

void MainWindow::onRun() {
  const auto problem = solution_panel_->buildProblem();
  const auto r = session_->solveEquilibrium(problem);
  input_view_->setPlainText(QString::fromStdString(r.raw_input));
  output_view_->setPlainText(QString::fromStdString(r.raw_output));
  if (!r.ok) {
    statusBar()->showMessage(tr("Run failed — see Raw output / status"));
    output_view_->appendPlainText(QStringLiteral("\n---- ERROR ----\n") +
                                  QString::fromStdString(r.error_string));
    return;
  }
  const auto po = parsePhreeqcOutput(r.raw_output);
  renderResults(po);
  statusBar()->showMessage(
      tr("OK — %1 species, %2 saturation indices, %3 equilibrium phases")
          .arg(po.species.size())
          .arg(po.saturation.size())
          .arg(po.assemblage.size()));
}

void MainWindow::renderResults(const ParsedOutput& po) {
  totals_table_->setRowCount(po.totals.size());
  for (size_t i = 0; i < po.totals.size(); ++i) {
    const auto& t = po.totals[i];
    totals_table_->setItem(i, 0, textItem(QString::fromStdString(t.element)));
    totals_table_->setItem(i, 1, sciItem(t.molality));
    totals_table_->setItem(i, 2, sciItem(t.moles));
  }

  desc_table_->setRowCount(po.description.size());
  int r = 0;
  for (const auto& kv : po.description) {
    desc_table_->setItem(r, 0, textItem(QString::fromStdString(kv.first)));
    desc_table_->setItem(r, 1, textItem(QString::fromStdString(kv.second)));
    ++r;
  }

  species_table_->setRowCount(po.species.size());
  for (size_t i = 0; i < po.species.size(); ++i) {
    const auto& s = po.species[i];
    species_table_->setItem(i, 0, textItem(QString::fromStdString(s.element)));
    species_table_->setItem(i, 1, textItem(QString::fromStdString(s.name)));
    species_table_->setItem(i, 2, sciItem(s.molality));
    species_table_->setItem(i, 3, sciItem(s.activity));
    species_table_->setItem(i, 4, numItem(s.log_molality, 'f', 3));
    species_table_->setItem(i, 5, numItem(s.log_activity, 'f', 3));
    species_table_->setItem(i, 6, numItem(s.log_gamma, 'f', 3));
  }

  si_table_->setRowCount(po.saturation.size());
  for (size_t i = 0; i < po.saturation.size(); ++i) {
    const auto& s = po.saturation[i];
    si_table_->setItem(i, 0, textItem(QString::fromStdString(s.phase)));
    si_table_->setItem(i, 1, numItem(s.si, 'f', 3));
    si_table_->setItem(i, 2, numItem(s.log_iap, 'f', 3));
    si_table_->setItem(i, 3, numItem(s.log_k, 'f', 3));
    si_table_->setItem(i, 4, textItem(QString::fromStdString(s.formula)));
  }

  assemblage_table_->setRowCount(po.assemblage.size());
  for (size_t i = 0; i < po.assemblage.size(); ++i) {
    const auto& a = po.assemblage[i];
    assemblage_table_->setItem(i, 0, textItem(QString::fromStdString(a.phase)));
    assemblage_table_->setItem(i, 1, numItem(a.si, 'f', 3));
    assemblage_table_->setItem(i, 2, sciItem(a.initial_moles));
    assemblage_table_->setItem(i, 3, sciItem(a.final_moles));
    assemblage_table_->setItem(i, 4, sciItem(a.delta_moles));
  }

  for (QTableWidget* t : {totals_table_, species_table_, si_table_,
                          assemblage_table_, desc_table_})
    t->resizeColumnsToContents();
}

}
