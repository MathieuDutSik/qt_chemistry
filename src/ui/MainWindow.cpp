#include "ui/MainWindow.h"

#include "kernel/ChemFormat.h"
#include "kernel/DatabaseInfo.h"
#include "kernel/EquilibriumProblem.h"
#include "kernel/PhreeqcOutputParser.h"
#include "kernel/PhreeqcSession.h"
#include "kernel/UnitConvert.h"
#include "kernel/WaterDensity.h"
#include "ui/ChemDelegate.h"
#include "ui/DatabaseEditorDialog.h"
#include "ui/HtmlDelegate.h"
#include "ui/SolutionPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QMenu>
#include <QMenuBar>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
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
  auto* hh = t->horizontalHeader();
  hh->setStretchLastSection(false);
  hh->setSectionResizeMode(QHeaderView::ResizeToContents);
  t->verticalHeader()->setVisible(false);
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setAlternatingRowColors(true);
}

class NumericItem : public QTableWidgetItem {
 public:
  NumericItem(double v, const QString& text) : QTableWidgetItem(text), value_(v) {
    setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }
  bool operator<(const QTableWidgetItem& other) const override {
    if (auto* o = dynamic_cast<const NumericItem*>(&other))
      return value_ < o->value_;
    return QTableWidgetItem::operator<(other);
  }
 private:
  double value_;
};

QTableWidgetItem* numItem(double v, char fmt = 'g', int prec = 4) {
  return new NumericItem(v, QString::number(v, fmt, prec));
}

QTableWidgetItem* sciItem(double v) {
  return new NumericItem(v, QString::number(v, 'e', 3));
}

QTableWidgetItem* textItem(const QString& s) {
  return new QTableWidgetItem(s);
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), session_(std::make_unique<PhreeqcSession>()) {
  setWindowTitle(QStringLiteral("Chemical Equilibrium"));
  resize(1500, 900);
  database_dir_ = QString::fromUtf8(QTCHEM_DEFAULT_DATABASE_DIR);
  user_database_dir_ =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
      + QStringLiteral("/databases");
  QDir().mkpath(user_database_dir_);

  auto* central = new QWidget(this);
  auto* root = new QVBoxLayout(central);

  auto* top = new QHBoxLayout;
  top->addWidget(new QLabel(tr("Database:")));
  db_combo_ = new QComboBox; db_combo_->setMinimumWidth(280);
  top->addWidget(db_combo_);
  db_status_ = new QLabel(tr("(not loaded)"));
  top->addWidget(db_status_);
  info_btn_ = new QPushButton(tr("Information…"));
  info_btn_->setEnabled(false);
  top->addWidget(info_btn_);
  duplicate_btn_ = new QPushButton(tr("Duplicate…"));
  duplicate_btn_->setEnabled(false);
  duplicate_btn_->setToolTip(
      tr("Copy the current database to your user databases folder so you "
         "can edit it. The original is never modified."));
  top->addWidget(duplicate_btn_);
  edit_btn_ = new QPushButton(tr("Edit…"));
  edit_btn_->setEnabled(false);
  edit_btn_->setToolTip(
      tr("Edit this user database. Built-in databases are read-only; "
         "use Duplicate… first to create an editable copy."));
  top->addWidget(edit_btn_);
  top->addStretch(1);
  root->addLayout(top);

  auto* split = new QSplitter(Qt::Horizontal);

  solution_panel_ = new SolutionPanel;
  split->addWidget(solution_panel_);

  result_tabs_ = new QTabWidget;

  totals_table_ = new QTableWidget(0, 0);
  {
    auto* totals_tab = new QWidget;
    auto* totals_lay = new QVBoxLayout(totals_tab);
    totals_lay->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Units —")));
    auto make_combo = [this]() {
      auto* cb = new QComboBox;
      for (const auto& u : supportedUnits())
        cb->addItem(QString::fromStdString(u));
      cb->setCurrentText(QStringLiteral("mol/kgw"));
      connect(cb, &QComboBox::currentTextChanged, this,
              [this](const QString&) { renderTotalsTab(); });
      return cb;
    };
    totals_unit_lbl_initial_ = new QLabel(tr("Initial:"));
    totals_unit_initial_     = make_combo();
    totals_unit_lbl_final_   = new QLabel(tr("Final:"));
    totals_unit_final_       = make_combo();
    totals_unit_lbl_delta_   = new QLabel(tr("Δ:"));
    totals_unit_delta_       = make_combo();
    bar->addWidget(totals_unit_lbl_initial_);
    bar->addWidget(totals_unit_initial_);
    bar->addWidget(totals_unit_lbl_final_);
    bar->addWidget(totals_unit_final_);
    bar->addWidget(totals_unit_lbl_delta_);
    bar->addWidget(totals_unit_delta_);
    totals_metric_lbl_ = new QLabel(tr("Error:"));
    totals_metric_ = new QComboBox;
    totals_metric_->addItem(tr("max"));
    totals_metric_->addItem(tr("min"));
    totals_metric_->addItem(tr("mean L1"));
    totals_metric_->addItem(tr("RMS L2"));
    totals_metric_->setCurrentIndex(2);  // mean L1
    connect(totals_metric_, &QComboBox::currentTextChanged, this,
            [this](const QString&) { renderTotalsTab(); });
    bar->addWidget(totals_metric_lbl_);
    bar->addWidget(totals_metric_);
    bar->addStretch(1);
    totals_volume_source_ = new QLabel;
    totals_volume_source_->setStyleSheet("color: #555;");
    bar->addWidget(totals_volume_source_);
    totals_lay->addLayout(bar);
    totals_lay->addWidget(totals_table_, 1);

    result_tabs_->addTab(totals_tab, tr("Element totals"));
  }
  desc_table_ = new QTableWidget(0, 0);
  result_tabs_->addTab(desc_table_, tr("Solution properties"));

  species_table_ = new QTableWidget(0, 8);
  setHeaders(species_table_, {tr("Element"), tr("Species"), tr("Pretty"),
                              tr("Molality"), tr("Activity"),
                              tr("log m"), tr("log a"), tr("log γ")});
  species_table_->setItemDelegateForColumn(
      2, new ChemDelegate(ChemDelegate::Mode::Species, this));
  species_table_->setSortingEnabled(true);
  species_table_->horizontalHeader()->setSortIndicatorShown(true);
  species_table_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
  const QStringList speciesHeaderTips = {
      tr("Master element associated with this aqueous species."),
      tr("Aqueous species as written in the PHREEQC database."),
      tr("Typeset species name (subscripts and superscripts)."),
      tr("Molality m: moles of species per kg of water (mol/kgw)."),
      tr("Activity a = γ·m: the thermodynamically effective concentration "
         "(dimensionless, ref. state 1 mol/kgw)."),
      tr("log₁₀(m): base-10 logarithm of molality."),
      tr("log₁₀(a): base-10 logarithm of activity. Drives the equilibrium "
         "(this is what appears in mass-action expressions)."),
      tr("log₁₀(γ): base-10 logarithm of the activity coefficient. "
         "γ = 1 in ideal/infinitely-dilute solutions; deviation from 0 "
         "reflects non-ideality from the chosen activity model.")};
  for (int c = 0; c < speciesHeaderTips.size(); ++c) {
    if (auto* h = species_table_->horizontalHeaderItem(c))
      h->setToolTip(speciesHeaderTips[c]);
  }
  auto* species_tab = new QWidget;
  auto* species_lay = new QVBoxLayout(species_tab);
  species_lay->setContentsMargins(0, 0, 0, 0);
  auto* species_help = new QLabel(
      tr("<a href=\"#\">What do these columns mean?</a>"));
  species_help->setTextFormat(Qt::RichText);
  species_help->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
  connect(species_help, &QLabel::linkActivated, this,
          &MainWindow::onShowSpeciesColumnHelp);
  species_lay->addWidget(species_help);
  species_lay->addWidget(species_table_, 1);
  result_tabs_->addTab(species_tab, tr("Species"));

  si_table_ = new QTableWidget(0, 5);
  setHeaders(si_table_, {tr("Phase"), tr("Reaction"), tr("log K"),
                         tr("SI"), tr("log IAP")});
  si_table_->setItemDelegateForColumn(
      1, new ChemDelegate(ChemDelegate::Mode::Reaction, this));
  const QStringList siHeaderTips = {
      tr("Mineral / gas phase the row reports on."),
      tr("Dissolution reaction as written in the database "
         "(e.g. Calcite ⇌ Ca²⁺ + CO₃²⁻). The log K and IAP are "
         "expressed in terms of this reaction."),
      tr("log₁₀ K — equilibrium constant of the dissolution reaction "
         "at the run's T and P. From the database."),
      tr("Saturation index: SI = log IAP − log K. >0 supersaturated "
         "(can precipitate); =0 at equilibrium; <0 undersaturated "
         "(will dissolve if present)."),
      tr("log₁₀ IAP — log of the ion-activity product computed from "
         "the actual species activities in the modelled solution.")};
  for (int c = 0; c < siHeaderTips.size(); ++c) {
    if (auto* h = si_table_->horizontalHeaderItem(c))
      h->setToolTip(siHeaderTips[c]);
  }
  auto* si_tab = new QWidget;
  auto* si_lay = new QVBoxLayout(si_tab);
  si_lay->setContentsMargins(0, 0, 0, 0);
  auto* si_help = new QLabel(
      tr("<a href=\"#\">What do these columns mean?</a>"));
  si_help->setTextFormat(Qt::RichText);
  si_help->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
  connect(si_help, &QLabel::linkActivated, this,
          &MainWindow::onShowSaturationColumnHelp);
  si_lay->addWidget(si_help);
  si_lay->addWidget(si_table_, 1);
  result_tabs_->addTab(si_tab, tr("Saturation indices"));

  assemblage_table_ = new QTableWidget(0, 6);
  setHeaders(assemblage_table_, {tr("Phase"), tr("Reaction"), tr("SI"),
                                 tr("Initial (mol)"), tr("Final (mol)"),
                                 tr("Δ (mol)")});
  assemblage_table_->setItemDelegateForColumn(
      1, new ChemDelegate(ChemDelegate::Mode::Reaction, this));
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
  connect(info_btn_, &QPushButton::clicked,
          this, &MainWindow::onShowDatabaseInfo);
  connect(duplicate_btn_, &QPushButton::clicked,
          this, &MainWindow::onDuplicateDatabase);
  connect(edit_btn_, &QPushButton::clicked,
          this, &MainWindow::onEditDatabase);
  connect(solution_panel_, &SolutionPanel::runRequested,
          this, &MainWindow::onRun);

  // Menu bar — Mode picker.
  auto* mode_menu = menuBar()->addMenu(tr("&Mode"));
  auto* mode_group = new QActionGroup(this);
  mode_group->setExclusive(true);
  mode_direct_action_ = mode_menu->addAction(tr("&Direct call"));
  mode_direct_action_->setCheckable(true);
  mode_direct_action_->setChecked(true);
  mode_direct_action_->setActionGroup(mode_group);
  mode_mc_action_ = mode_menu->addAction(
      tr("Computation under &uncertainty (Monte Carlo)"));
  mode_mc_action_->setCheckable(true);
  mode_mc_action_->setActionGroup(mode_group);
  connect(mode_mc_action_, &QAction::toggled, this, [this](bool on) {
    solution_panel_->setUncertaintyEnabled(on);
    if (!on) {
      last_mc_active_ = false;
      last_mc_result_ = MonteCarloResult{};
      renderTotalsTab();
    }
  });
}

MainWindow::~MainWindow() = default;

void MainWindow::populateDatabaseList(const QString& selectAbsolutePath) {
  const QSignalBlocker block(db_combo_);
  db_combo_->clear();

  const auto builtin = QDir(database_dir_).entryInfoList(
      QStringList() << QStringLiteral("*.dat"), QDir::Files, QDir::Name);
  for (const auto& fi : builtin) {
    db_combo_->addItem(fi.fileName(), fi.absoluteFilePath());
    db_combo_->setItemData(db_combo_->count() - 1, false, Qt::UserRole + 1);
  }

  const auto userFiles = QDir(user_database_dir_).entryInfoList(
      QStringList() << QStringLiteral("*.dat"), QDir::Files, QDir::Name);
  if (!userFiles.isEmpty()) {
    if (!builtin.isEmpty()) db_combo_->insertSeparator(db_combo_->count());
    for (const auto& fi : userFiles) {
      db_combo_->addItem(fi.fileName(), fi.absoluteFilePath());
      db_combo_->setItemData(db_combo_->count() - 1, true, Qt::UserRole + 1);
    }
  }

  if (builtin.isEmpty() && userFiles.isEmpty()) {
    db_combo_->addItem(tr("(no .dat files in %1)").arg(database_dir_));
  }

  int selectIdx = -1;
  if (!selectAbsolutePath.isEmpty()) {
    for (int i = 0; i < db_combo_->count(); ++i) {
      if (db_combo_->itemData(i).toString() == selectAbsolutePath) {
        selectIdx = i; break;
      }
    }
  }
  if (selectIdx < 0) selectIdx = db_combo_->findText(QStringLiteral("phreeqc.dat"));
  if (selectIdx < 0) {
    for (int i = 0; i < db_combo_->count(); ++i) {
      if (!db_combo_->itemData(i).toString().isEmpty()) { selectIdx = i; break; }
    }
  }
  if (selectIdx >= 0) db_combo_->setCurrentIndex(selectIdx);
  onDatabaseChanged(db_combo_->currentIndex());
}

bool MainWindow::isUserWritableDatabase(int index) const {
  if (index < 0) return false;
  return db_combo_->itemData(index, Qt::UserRole + 1).toBool();
}

void MainWindow::onDatabaseChanged(int index) {
  const QString path = db_combo_->itemData(index).toString();
  if (path.isEmpty()) {
    db_status_->setText(tr("(no database)"));
    duplicate_btn_->setEnabled(false);
    edit_btn_->setEnabled(false);
    info_btn_->setEnabled(false);
    return;
  }
  const bool userWritable = isUserWritableDatabase(index);
  std::string err;
  if (session_->loadDatabase(path.toStdString(), &err)) {
    db_status_->setText(userWritable
                            ? tr("loaded ✓ — user copy (editable)")
                            : tr("loaded ✓ — built-in (read-only)"));
    statusBar()->showMessage(tr("Loaded %1").arg(QFileInfo(path).fileName()));
    auto info = std::make_shared<DatabaseInfo>();
    if (info->load(path.toStdString())) {
      db_info_ = info;
      current_database_path_ = path;
      solution_panel_->setDatabaseInfo(db_info_);
      info_btn_->setEnabled(true);
    }
    duplicate_btn_->setEnabled(true);
    edit_btn_->setEnabled(userWritable);
  } else {
    db_status_->setText(tr("load failed"));
    output_view_->setPlainText(QString::fromStdString(err));
    statusBar()->showMessage(tr("Database load failed"));
    info_btn_->setEnabled(false);
    duplicate_btn_->setEnabled(false);
    edit_btn_->setEnabled(false);
  }
}

void MainWindow::onEditDatabase() {
  if (current_database_path_.isEmpty()) return;
  if (!isUserWritableDatabase(db_combo_->currentIndex())) return;
  DatabaseEditorDialog dlg(current_database_path_, this);
  if (dlg.exec() != QDialog::Accepted) return;
  // The user saved edits to disk; reload the database so the running
  // session sees the new content, and refresh the parsed info / panel.
  onDatabaseChanged(db_combo_->currentIndex());
  statusBar()->showMessage(
      tr("Saved %1").arg(QFileInfo(current_database_path_).fileName()));
}

void MainWindow::onRun() {
  const bool monte_carlo = mode_mc_action_ && mode_mc_action_->isChecked();
  QStringList warnings;
  const auto problem = solution_panel_->buildProblem(&warnings);

  if (!monte_carlo) {
    last_mc_result_ = MonteCarloResult{};
    last_mc_active_ = false;
    const auto r = session_->solveEquilibrium(problem, db_info_.get());
    input_view_->setPlainText(QString::fromStdString(r.raw_input));
    output_view_->setPlainText(QString::fromStdString(r.raw_output));
    if (!warnings.isEmpty()) {
      input_view_->appendPlainText(QStringLiteral("\n# qt_chemistry notes:\n# ") +
                                   warnings.join(QStringLiteral("\n# ")));
    }
    if (!r.ok) {
      statusBar()->showMessage(tr("Run failed — see Raw output / status"));
      output_view_->appendPlainText(QStringLiteral("\n---- ERROR ----\n") +
                                    QString::fromStdString(r.error_string));
      return;
    }
    const auto po = parsePhreeqcOutput(r.raw_output);
    renderResults(po);
    const size_t nspec = po.frames.empty() ? 0 : po.frames.back().species.size();
    statusBar()->showMessage(
        tr("OK — %1 frame(s), %2 species, %3 SI, %4 equilibrium phases%5")
            .arg(po.frames.size())
            .arg(nspec)
            .arg(po.saturation.size())
            .arg(po.assemblage.size())
            .arg(po.has_reaction_step
                     ? tr("  (reaction step ran)") : QString()));
    return;
  }

  const auto spec = solution_panel_->buildUncertaintySpec();
  const auto budget = solution_panel_->buildMonteCarloBudget();
  statusBar()->showMessage(tr("Running Monte Carlo…"));
  QApplication::processEvents();
  auto mc = runMonteCarlo(problem, spec, budget, *session_, db_info_.get());
  if (!mc.error.empty()) {
    statusBar()->showMessage(tr("MC failed: %1")
                                  .arg(QString::fromStdString(mc.error)));
    return;
  }
  last_mc_result_ = std::move(mc);
  last_mc_active_ = true;
  // Show the baseline output in the raw panes so the existing view-flow stays
  // useful; the MC stats drive the totals tab.
  renderResults(last_mc_result_.baseline);
  statusBar()->showMessage(
      tr("OK — %1 MC runs in %2 s (baseline + %3 perturbed)")
          .arg(last_mc_result_.runs_completed + 1)
          .arg(QString::number(last_mc_result_.elapsed_seconds, 'f', 2))
          .arg(last_mc_result_.runs_completed));
}

void MainWindow::renderTotalsTab() {
  const Frame empty_frame;
  const Frame& initial = last_parsed_.frames.empty()
                             ? empty_frame : last_parsed_.frames.front();
  const Frame& final = last_parsed_.frames.empty()
                           ? empty_frame : last_parsed_.frames.back();
  const bool two_frames = last_parsed_.frames.size() >= 2;

  // Toolbar visibility: single-frame uses only the "Initial" combo as the
  // single Total unit; two-frame shows all three.
  totals_unit_lbl_initial_->setVisible(true);
  totals_unit_initial_->setVisible(true);
  totals_unit_lbl_initial_->setText(two_frames ? tr("Initial:") : tr("Total:"));
  totals_unit_lbl_final_->setVisible(two_frames);
  totals_unit_final_->setVisible(two_frames);
  totals_unit_lbl_delta_->setVisible(two_frames);
  totals_unit_delta_->setVisible(two_frames);

  // Resolve the frame-wide conversion context from PHREEQC's description.
  // Single-frame: use the only frame. Two-frame: use the final frame's
  // description for Initial+Final columns (they share a solution), but for
  // Δ the choice is somewhat arbitrary — use final as well.
  const auto res = buildConvertContext(final.description, 25.0);
  const UnitConvertContext ctx = res.context;
  const char* src_label =
      res.source == VolumeSource::Database          ? "from DB"
    : res.source == VolumeSource::FromDatabaseDensity ? "from DB (ρ)"
                                                     : "Kell EOS";
  totals_volume_source_->setText(tr("volume: %1").arg(QString(src_label)));

  // Collate rows by element root.
  std::map<std::string, const ElementTotalRow*> init_by_root, final_by_root;
  std::vector<std::string> ordered_roots;
  auto add_root = [&](const std::string& r) {
    if (std::find(ordered_roots.begin(), ordered_roots.end(), r)
        == ordered_roots.end())
      ordered_roots.push_back(r);
  };
  for (const auto& t : initial.totals) {
    const std::string r = elementRoot(t.element);
    init_by_root[r] = &t; add_root(r);
  }
  for (const auto& t : final.totals) {
    const std::string r = elementRoot(t.element);
    final_by_root[r] = &t; add_root(r);
  }

  const std::string u_initial = totals_unit_initial_->currentText().toStdString();
  const std::string u_final   = totals_unit_final_->currentText().toStdString();
  const std::string u_delta   = totals_unit_delta_->currentText().toStdString();

  const bool show_mc = last_mc_active_ && !two_frames;
  const QString metric =
      show_mc ? totals_metric_->currentText() : QString();
  totals_metric_lbl_->setVisible(show_mc);
  totals_metric_->setVisible(show_mc);

  std::map<std::string, const ElementAggregate*> mc_by_root;
  if (show_mc) {
    for (const auto& e : last_mc_result_.elements)
      mc_by_root[e.element] = &e;
  }

  if (two_frames) {
    setHeaders(totals_table_,
               {tr("Element"), tr("Master species"),
                tr("Initial (%1)").arg(QString::fromStdString(u_initial)),
                tr("Final (%1)").arg(QString::fromStdString(u_final)),
                tr("Δ (%1)").arg(QString::fromStdString(u_delta))});
  } else if (show_mc) {
    setHeaders(totals_table_,
               {tr("Element"), tr("Master species"),
                tr("Total (%1)").arg(QString::fromStdString(u_initial)),
                tr("%1 (%2)").arg(metric,
                                  QString::fromStdString(u_initial))});
  } else {
    setHeaders(totals_table_,
               {tr("Element"), tr("Master species"),
                tr("Total (%1)").arg(QString::fromStdString(u_initial))});
  }
  totals_table_->setRowCount(ordered_roots.size());
  auto cell = [&](double molality, double aw, const std::string& unit) {
    UnitConvertInputs in;
    in.molality_mol_per_kgw = molality;
    if (aw > 0.0) in.molar_mass_g_per_mol = aw;
    auto v = convertFromMolality(in, ctx, unit);
    return v ? sciItem(*v) : textItem(QStringLiteral("—"));
  };
  auto metric_value = [&](const ScalarAggregate& sa) -> double {
    if (sa.n_samples == 0) return 0.0;
    if (metric == tr("max"))     return sa.max_value - sa.baseline;
    if (metric == tr("min"))     return sa.min_value - sa.baseline;
    if (metric == tr("mean L1")) return sa.mean_l1;
    if (metric == tr("RMS L2"))  return sa.rms_l2;
    return sa.mean_l1;
  };
  for (size_t i = 0; i < ordered_roots.size(); ++i) {
    const std::string& root = ordered_roots[i];
    auto* init = init_by_root.count(root) ? init_by_root[root] : nullptr;
    auto* fin  = final_by_root.count(root) ? final_by_root[root] : nullptr;
    const std::string label = fin ? fin->element
                                   : (init ? init->element : root);
    totals_table_->setItem(i, 0, textItem(QString::fromStdString(label)));
    QString master;
    double aw = 0.0;
    if (db_info_) {
      if (auto m = db_info_->findByElement(label))
        master = QString::fromStdString(m->species);
      aw = db_info_->atomicWeight(label);
    }
    totals_table_->setItem(i, 1, textItem(master));
    if (two_frames) {
      totals_table_->setItem(i, 2,
          init ? cell(init->molality, aw, u_initial)
               : textItem(QStringLiteral("—")));
      totals_table_->setItem(i, 3,
          fin ? cell(fin->molality, aw, u_final)
              : textItem(QStringLiteral("—")));
      const double dm = (fin ? fin->molality : 0.0)
                      - (init ? init->molality : 0.0);
      totals_table_->setItem(i, 4, cell(dm, aw, u_delta));
    } else {
      const double mol = fin ? fin->molality
                              : (init ? init->molality : 0.0);
      totals_table_->setItem(i, 2, cell(mol, aw, u_initial));
      if (show_mc) {
        auto it = mc_by_root.find(root);
        if (it != mc_by_root.end() && it->second->molality.n_samples > 0) {
          totals_table_->setItem(i, 3,
              cell(metric_value(it->second->molality), aw, u_initial));
        } else {
          totals_table_->setItem(i, 3, textItem(QStringLiteral("—")));
        }
      }
    }
  }
}

void MainWindow::renderResults(const ParsedOutput& po) {
  last_parsed_ = po;
  renderTotalsTab();

  const Frame empty_frame;
  const Frame& initial = po.frames.empty() ? empty_frame : po.frames.front();
  const Frame& final = po.frames.empty() ? empty_frame : po.frames.back();
  const bool two_frames = po.frames.size() >= 2;

  // ----- Description -----
  if (two_frames) {
    setHeaders(desc_table_,
               {tr("Quantity"), tr("Initial"), tr("Final")});
    std::vector<std::string> keys;
    auto add_key = [&](const std::string& k) {
      if (std::find(keys.begin(), keys.end(), k) == keys.end())
        keys.push_back(k);
    };
    for (const auto& kv : initial.description) add_key(kv.first);
    for (const auto& kv : final.description) add_key(kv.first);
    desc_table_->setRowCount(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
      desc_table_->setItem(i, 0, textItem(QString::fromStdString(keys[i])));
      auto it1 = initial.description.find(keys[i]);
      auto it2 = final.description.find(keys[i]);
      desc_table_->setItem(i, 1, textItem(
          it1 != initial.description.end()
              ? QString::fromStdString(it1->second) : QStringLiteral("—")));
      desc_table_->setItem(i, 2, textItem(
          it2 != final.description.end()
              ? QString::fromStdString(it2->second) : QStringLiteral("—")));
    }
  } else {
    setHeaders(desc_table_, {tr("Quantity"), tr("Value")});
    desc_table_->setRowCount(final.description.size());
    int r = 0;
    for (const auto& kv : final.description) {
      desc_table_->setItem(r, 0, textItem(QString::fromStdString(kv.first)));
      desc_table_->setItem(r, 1, textItem(QString::fromStdString(kv.second)));
      ++r;
    }
  }

  // ----- Species (final state) -----
  const bool prev_sort = species_table_->isSortingEnabled();
  species_table_->setSortingEnabled(false);
  species_table_->setRowCount(final.species.size());
  for (size_t i = 0; i < final.species.size(); ++i) {
    const auto& s = final.species[i];
    species_table_->setItem(i, 0, textItem(QString::fromStdString(s.element)));
    species_table_->setItem(i, 1, textItem(QString::fromStdString(s.name)));
    // Pretty column: raw species name; ChemDelegate renders it.
    auto* pretty = textItem(QString::fromStdString(s.name));
    if (db_info_) {
      if (auto rxn = db_info_->findAqueous(s.name)) {
        pretty->setToolTip(
            QStringLiteral("<p>%1<br>log K = %2</p>")
                .arg(htmlPrettifyReaction(rxn->equation),
                     rxn->has_log_k ? QString::number(rxn->log_k, 'f', 3)
                                    : tr("(unknown)")));
      }
    }
    species_table_->setItem(i, 2, pretty);
    species_table_->setItem(i, 3, sciItem(s.molality));
    species_table_->setItem(i, 4, sciItem(s.activity));
    species_table_->setItem(i, 5, numItem(s.log_molality, 'f', 3));
    species_table_->setItem(i, 6, numItem(s.log_activity, 'f', 3));
    species_table_->setItem(i, 7, numItem(s.log_gamma, 'f', 3));
  }
  species_table_->setSortingEnabled(prev_sort);

  // For Reaction columns the cell text is the raw PHREEQC equation
  // ("CaCO3 = CO3-2 + Ca+2"); ChemDelegate renders it. The phase name is
  // stashed in kPhaseNameRole so the delegate can apply (g)/(aq) tags.
  auto reactionFor = [&](const std::string& phase) -> QString {
    if (!db_info_) return {};
    if (auto r = db_info_->findPhase(phase))
      return QString::fromStdString(r->equation);
    return {};
  };

  si_table_->setRowCount(po.saturation.size());
  for (size_t i = 0; i < po.saturation.size(); ++i) {
    const auto& s = po.saturation[i];
    si_table_->setItem(i, 0, textItem(QString::fromStdString(s.phase)));
    auto* rxn = textItem(reactionFor(s.phase));
    rxn->setData(ChemDelegate::kPhaseNameRole,
                 QString::fromStdString(s.phase));
    si_table_->setItem(i, 1, rxn);
    si_table_->setItem(i, 2, numItem(s.log_k, 'f', 3));
    si_table_->setItem(i, 3, numItem(s.si, 'f', 3));
    si_table_->setItem(i, 4, numItem(s.log_iap, 'f', 3));
  }

  assemblage_table_->setRowCount(po.assemblage.size());
  for (size_t i = 0; i < po.assemblage.size(); ++i) {
    const auto& a = po.assemblage[i];
    assemblage_table_->setItem(i, 0, textItem(QString::fromStdString(a.phase)));
    auto* rxn = textItem(reactionFor(a.phase));
    rxn->setData(ChemDelegate::kPhaseNameRole,
                 QString::fromStdString(a.phase));
    assemblage_table_->setItem(i, 1, rxn);
    assemblage_table_->setItem(i, 2, numItem(a.si, 'f', 3));
    assemblage_table_->setItem(i, 3, sciItem(a.initial_moles));
    assemblage_table_->setItem(i, 4, sciItem(a.final_moles));
    assemblage_table_->setItem(i, 5, sciItem(a.delta_moles));
  }

  for (QTableWidget* t : {totals_table_, species_table_, si_table_,
                          assemblage_table_, desc_table_})
    t->resizeColumnsToContents();
}

void MainWindow::onShowSpeciesColumnHelp() {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Species table columns"));
  dlg.resize(680, 520);
  auto* layout = new QVBoxLayout(&dlg);
  auto* browser = new QTextBrowser;
  browser->setOpenExternalLinks(true);
  browser->setHtml(tr(
      "<h2>Species table columns</h2>"
      "<p>Each row is one aqueous species present (or theoretically "
      "possible) in the final equilibrium solution.</p>"
      "<h3>Element</h3>"
      "<p>The PHREEQC master element associated with this species "
      "(e.g. <code>C</code> for <code>CO3-2</code>). Used to group rows.</p>"
      "<h3>Species / Pretty</h3>"
      "<p><b>Species</b> is the raw name as written in the PHREEQC "
      "database (<code>CO3-2</code>). <b>Pretty</b> is the same name "
      "typeset with subscripts and superscripts (CO<sub>3</sub><sup>2−</sup>). "
      "Hover the Pretty cell to see the formation reaction and its "
      "<i>log K</i>.</p>"
      "<h3>Molality (mol/kgw)</h3>"
      "<p>Symbol <i>m</i>. Moles of the species per kilogram of <i>water</i> "
      "(not solution). This is the &ldquo;how much is there&rdquo; quantity. "
      "Species with mathematically valid formation reactions are listed "
      "even when their molality is essentially zero, because trace amounts "
      "can still couple to redox or pH equilibria.</p>"
      "<h3>Activity</h3>"
      "<p>Symbol <i>a</i>. The thermodynamically effective concentration "
      "that appears in mass-action (equilibrium) expressions. Related to "
      "molality by the activity coefficient γ:</p>"
      "<p style='margin-left:2em;'><i>a = γ · m</i></p>"
      "<p>In ideal / infinitely-dilute solutions γ = 1 and <i>a = m</i>. "
      "In real solutions γ deviates from 1 because of ion-ion "
      "interactions; the chosen activity-coefficient model (Davies, "
      "Truesdell-Jones, Pitzer, SIT, LLNL B-dot) determines how γ is "
      "computed. The reference state is hypothetical 1 mol/kgw with "
      "ideal-dilute behaviour, so <i>a</i> is dimensionless.</p>"
      "<h3>log m</h3>"
      "<p>Base-10 logarithm of molality: log<sub>10</sub>(<i>m</i>). "
      "Convenient because aqueous concentrations span many decades. "
      "A value of <code>−7</code> means 10⁻⁷ mol/kgw.</p>"
      "<h3>log a</h3>"
      "<p>Base-10 logarithm of activity: log<sub>10</sub>(<i>a</i>). "
      "<b>This is what actually drives equilibria.</b> pH, for example, "
      "is defined as <code>−log a(H+)</code>, not <code>−log m(H+)</code>. "
      "Saturation indices and reaction quotients are written in terms of "
      "log <i>a</i> too.</p>"
      "<h3>log γ</h3>"
      "<p>Base-10 logarithm of the activity coefficient: "
      "log<sub>10</sub>(γ) = log <i>a</i> − log <i>m</i>. "
      "Tells you how far the species deviates from ideal-dilute behaviour. "
      "<code>0</code> means ideal (γ = 1). Negative values are typical for "
      "charged species at non-negligible ionic strength. The magnitude "
      "depends on charge and the activity model in use; see the "
      "&ldquo;Information…&rdquo; dialog (next to the database selector) "
      "for the model loaded.</p>"
      "<hr>"
      "<h3>Reading reactions in the Saturation indices / Phase assemblage "
      "tables</h3>"
      "<p>Mineral phases are written as <i>solid ⇌ aqueous products</i>, "
      "so the renderer tags the LHS leader with <code>(s)</code> and "
      "matching RHS species with <code>(aq)</code>. Gas phases get "
      "<code>(g)</code>/<code>(aq)</code> instead. In the database "
      "editor's Aqueous species tab, equations like "
      "<code>Zn²⁺ ⇌ Zn²⁺</code> are master-species self-declarations — "
      "both sides are the same aqueous species — and are rendered as "
      "<code>Zn²⁺ (master species)</code>. They tell PHREEQC the canonical "
      "name of the species for an element; you usually don't need to "
      "touch them.</p>"
      "<h3>Pseudo-elements: <code>Oxg</code>, <code>Ntg</code>, "
      "<code>Mtg</code>, <code>Sg</code></h3>"
      "<p>These are <b>not</b> separate chemical elements. They are "
      "PHREEQC conventions for <i>redox-uncoupled</i> dissolved forms of "
      "common gases:</p>"
      "<ul>"
      "<li><code>Oxg</code> → aqueous O₂ that does <b>not</b> equilibrate "
      "with the O(-II)/O(0) couple of water. Use this when you want to "
      "specify dissolved oxygen without forcing the whole solution into "
      "redox equilibrium with it.</li>"
      "<li><code>Ntg</code> → aqueous N₂ uncoupled from other nitrogen "
      "redox states (NO₃⁻, NH₄⁺ …). Without it, low-pe waters would "
      "collapse all N onto N₂ at equilibrium, which is rarely physical.</li>"
      "<li><code>Mtg</code> → aqueous CH₄ uncoupled from other carbon "
      "redox states. Same motivation: prevents forced redox equilibrium "
      "between CH₄, CO₂, and dissolved organic species.</li>"
      "<li><code>Sg</code> / <code>H2Sg</code> → aqueous H₂S uncoupled "
      "from the S(-II)/S(VI) couple.</li>"
      "</ul>"
      "<p>The trailing <code>g</code> in the name is a PHREEQC tag, not a "
      "physical-state marker — these species are dissolved in solution, "
      "not gaseous.</p>"));
  layout->addWidget(browser, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

void MainWindow::onShowSaturationColumnHelp() {
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Saturation indices columns"));
  dlg.resize(680, 520);
  auto* layout = new QVBoxLayout(&dlg);
  auto* browser = new QTextBrowser;
  browser->setOpenExternalLinks(true);
  browser->setHtml(tr(
      "<h2>Saturation indices columns</h2>"
      "<p>Each row reports how close the modelled solution is to "
      "equilibrium with one mineral or gas <b>phase</b>. PHREEQC writes "
      "the dissolution reaction of that phase and compares the activities "
      "currently in solution against the database's equilibrium constant.</p>"
      "<h3>Phase</h3>"
      "<p>The mineral, gas, or organic-matter phase the row refers to "
      "(e.g. <code>Calcite</code>, <code>CO2(g)</code>). The same name "
      "is what you would put in an <code>EQUILIBRIUM_PHASES</code> block.</p>"
      "<h3>Reaction</h3>"
      "<p>The dissolution reaction <i>as written in the database</i>, "
      "e.g. Calcite ⇌ Ca<sup>2+</sup> + CO<sub>3</sub><sup>2−</sup>. "
      "Both <i>log K</i> and <i>log IAP</i> are expressed in terms of "
      "this exact stoichiometry — the direction matters.</p>"
      "<h3>log K</h3>"
      "<p>Base-10 logarithm of the equilibrium constant <i>K</i> for the "
      "reaction above, at the run's temperature and pressure. From the "
      "database. For Calcite at 25 °C, log K ≈ −8.48.</p>"
      "<h3>SI &nbsp;(saturation index)</h3>"
      "<p>The headline number. Defined as:</p>"
      "<p style='margin-left:2em;'><i>SI = log IAP − log K</i></p>"
      "<ul>"
      "<li><b>SI &gt; 0</b> — supersaturated: thermodynamically the phase "
      "<i>can</i> precipitate. Kinetics may stop it (e.g. seawater is "
      "supersaturated for both calcite and aragonite at the surface).</li>"
      "<li><b>SI = 0</b> — at equilibrium with the phase.</li>"
      "<li><b>SI &lt; 0</b> — undersaturated: if the phase is present it "
      "will dissolve toward equilibrium.</li>"
      "</ul>"
      "<p>SI is a <i>thermodynamic</i> driving force — it tells you the "
      "direction and the distance from equilibrium, not the rate.</p>"
      "<h3>log IAP &nbsp;(ion-activity product)</h3>"
      "<p>Base-10 logarithm of the activity product computed from the "
      "<i>actual</i> species activities in the modelled solution, raised "
      "to the stoichiometric coefficients of the reaction. For "
      "Calcite ⇌ Ca<sup>2+</sup> + CO<sub>3</sub><sup>2−</sup>:</p>"
      "<p style='margin-left:2em;'><i>IAP = a(Ca<sup>2+</sup>) · "
      "a(CO<sub>3</sub><sup>2−</sup>)</i></p>"
      "<p>Mostly useful as a diagnostic — the actionable number is SI.</p>"));
  layout->addWidget(browser, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

void MainWindow::onDuplicateDatabase() {
  if (current_database_path_.isEmpty()) return;
  const QFileInfo src(current_database_path_);

  QDialog dlg(this);
  dlg.setWindowTitle(tr("Duplicate database"));
  auto* form = new QFormLayout;

  auto* srcLabel = new QLabel(src.fileName());
  srcLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(tr("Source:"), srcLabel);

  auto* destEdit = new QLineEdit;
  destEdit->setText(src.completeBaseName() + QStringLiteral("-copy.dat"));
  destEdit->selectAll();
  form->addRow(tr("Save as:"), destEdit);

  auto* hint = new QLabel(
      tr("The copy will be saved in:\n%1\nThe original is never modified.")
          .arg(QDir::toNativeSeparators(user_database_dir_)));
  hint->setStyleSheet(QStringLiteral("color:#666;"));
  hint->setWordWrap(true);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  auto* outer = new QVBoxLayout(&dlg);
  outer->addLayout(form);
  outer->addWidget(hint);
  outer->addWidget(buttons);

  static const QRegularExpression kBadChars(
      QStringLiteral("[/\\\\:?*\"<>|]"));

  while (dlg.exec() == QDialog::Accepted) {
    QString name = destEdit->text().trimmed();
    if (!name.endsWith(QStringLiteral(".dat"), Qt::CaseInsensitive))
      name += QStringLiteral(".dat");
    if (name == QStringLiteral(".dat") || name.contains(kBadChars)) {
      QMessageBox::warning(&dlg, tr("Invalid name"),
          tr("Please enter a valid filename (no path separators or %1)."
             ).arg(QStringLiteral("/\\:?*\"<>|")));
      continue;
    }
    const QString dest = user_database_dir_ + QStringLiteral("/") + name;
    if (QFile::exists(dest)) {
      const auto ans = QMessageBox::question(&dlg, tr("Overwrite?"),
          tr("A user database named '%1' already exists. Overwrite it?")
              .arg(name));
      if (ans != QMessageBox::Yes) continue;
      if (!QFile::remove(dest)) {
        QMessageBox::critical(&dlg, tr("Cannot overwrite"),
            tr("Failed to remove existing file:\n%1").arg(dest));
        continue;
      }
    }
    if (!QFile::copy(current_database_path_, dest)) {
      QMessageBox::critical(&dlg, tr("Copy failed"),
          tr("Could not copy database to:\n%1").arg(dest));
      continue;
    }
    QFile::setPermissions(dest,
        QFile::ReadOwner | QFile::WriteOwner |
        QFile::ReadGroup | QFile::ReadOther);
    statusBar()->showMessage(tr("Created user database %1").arg(name));
    populateDatabaseList(dest);
    return;
  }
}

void MainWindow::onShowDatabaseInfo() {
  if (!db_info_) return;
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Database information"));
  dlg.resize(720, 560);
  auto* layout = new QVBoxLayout(&dlg);
  auto* browser = new QTextBrowser;
  browser->setOpenExternalLinks(true);
  browser->setHtml(buildDatabaseInfoHtml());
  layout->addWidget(browser, 1);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

namespace {

QString activityValidityNote(const QString& model) {
  if (model.contains("Pitzer", Qt::CaseInsensitive))
    return MainWindow::tr(
        "Suitable for brines and concentrated solutions, "
        "typically up to saturation. Coverage is limited to the ion pairs "
        "for which Pitzer virial coefficients have been parameterised "
        "(usually the Na–K–Mg–Ca–Cl–SO₄–HCO₃–CO₃–OH–H–H₂O system, "
        "with database-specific extensions).");
  if (model.contains("SIT", Qt::CaseInsensitive))
    return MainWindow::tr(
        "Specific-ion Interaction Theory. Typical validity up to I ≈ 4 m. "
        "Common in radioactive-waste compilations (OECD/NEA TDB).");
  if (model.contains("LLNL", Qt::CaseInsensitive) ||
      model.contains("B-dot", Qt::CaseInsensitive))
    return MainWindow::tr(
        "Extended Debye–Hückel with a B-dot linear correction term "
        "(Helgeson-Kirkham-Flowers). Suitable for dilute to moderately "
        "saline waters at variable T and P; typically I ≲ 2–3 m.");
  if (model.contains("Truesdell", Qt::CaseInsensitive))
    return MainWindow::tr(
        "Davies equation as fallback; Truesdell–Jones (extended "
        "Debye–Hückel with per-ion size parameters) where parameters are "
        "tabulated. Typically valid up to I ≈ 2 m.");
  if (model.contains("Davies", Qt::CaseInsensitive))
    return MainWindow::tr(
        "Davies equation: a simplified extended Debye–Hückel form. "
        "Typically valid up to I ≈ 0.5 m.");
  return {};
}

}  // namespace

QString MainWindow::buildDatabaseInfoHtml() const {
  if (!db_info_) return tr("(no database loaded)");
  const auto& info = *db_info_;
  const QFileInfo fi(current_database_path_);
  const QString model = QString::fromStdString(info.activityModelName());
  const QString desc  = QString::fromStdString(info.databaseDescription());

  QString html;
  html += QStringLiteral("<h2>%1</h2>").arg(fi.fileName());
  html += QStringLiteral("<p style='color:#666; font-size:11px;'>%1</p>")
              .arg(fi.absoluteFilePath().toHtmlEscaped());

  html += QStringLiteral("<h3>Activity coefficient model</h3>");
  html += QStringLiteral("<p><b>%1</b></p>").arg(model);
  const QString note = activityValidityNote(model);
  if (!note.isEmpty())
    html += QStringLiteral("<p>%1</p>").arg(note);
  html += QStringLiteral(
      "<p style='color:#555;'>All PHREEQC activity models are members of the "
      "Debye–Hückel family or its empirical extensions. There is no separate "
      "&ldquo;pure Debye–Hückel&rdquo; (Güntelberg) option; Davies is the "
      "default short-range model.</p>");

  if (!desc.isEmpty()) {
    html += QStringLiteral("<h3>Provenance / intended use</h3>");
    html += QStringLiteral("<p>%1</p>").arg(desc);
  }

  html += QStringLiteral("<h3>Statistics</h3>");
  html += QStringLiteral("<ul>");
  html += QStringLiteral("<li>%1 master species entries (elements + redox states)</li>")
              .arg(info.entries().size());
  html += QStringLiteral("<li>%1 aqueous species with parsed formation reactions</li>")
              .arg(info.aqueousReactions().size());
  html += QStringLiteral("<li>%1 minerals / gases (phases)</li>")
              .arg(info.phases().size());
  html += QStringLiteral("</ul>");

  // Element list, compact.
  QStringList elems;
  for (const auto& e : info.entries()) {
    if (e.element.find('(') == std::string::npos)
      elems << QString::fromStdString(e.element);
  }
  if (!elems.isEmpty()) {
    const QString mono =
        QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    html += QStringLiteral("<h3>Elements</h3>"
                            "<p style='font-family:\"%1\";'>%2</p>")
                .arg(mono.toHtmlEscaped(),
                     elems.join(QStringLiteral(", ")));
  }
  return html;
}

}
