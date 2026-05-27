#include "ui/MainWindow.h"

#include "kernel/ChemFormat.h"
#include "kernel/DatabaseInfo.h"
#include "kernel/EquilibriumProblem.h"
#include "kernel/PhreeqcOutputParser.h"
#include "kernel/PhreeqcSession.h"
#include "ui/ChemDelegate.h"
#include "ui/HtmlDelegate.h"
#include "ui/SolutionPanel.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
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
  t->horizontalHeader()->setStretchLastSection(true);
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
  top->addStretch(1);
  root->addLayout(top);

  auto* split = new QSplitter(Qt::Horizontal);

  solution_panel_ = new SolutionPanel;
  split->addWidget(solution_panel_);

  result_tabs_ = new QTabWidget;

  totals_table_ = new QTableWidget(0, 0);
  result_tabs_->addTab(totals_table_, tr("Element totals"));
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
  result_tabs_->addTab(species_table_, tr("Species"));

  si_table_ = new QTableWidget(0, 6);
  setHeaders(si_table_, {tr("Phase"), tr("Reaction"), tr("log K"),
                         tr("SI"), tr("log IAP"), tr("Formula")});
  si_table_->setItemDelegateForColumn(
      1, new ChemDelegate(ChemDelegate::Mode::Reaction, this));
  result_tabs_->addTab(si_table_, tr("Saturation indices"));

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
    auto info = std::make_shared<DatabaseInfo>();
    if (info->load(path.toStdString())) {
      db_info_ = info;
      current_database_path_ = path;
      solution_panel_->setDatabaseInfo(db_info_);
      info_btn_->setEnabled(true);
    }
  } else {
    db_status_->setText(tr("load failed"));
    output_view_->setPlainText(QString::fromStdString(err));
    statusBar()->showMessage(tr("Database load failed"));
    info_btn_->setEnabled(false);
  }
}

void MainWindow::onRun() {
  QStringList warnings;
  const auto problem = solution_panel_->buildProblem(&warnings);
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
}

void MainWindow::renderResults(const ParsedOutput& po) {
  const Frame empty_frame;
  const Frame& initial = po.frames.empty() ? empty_frame : po.frames.front();
  const Frame& final = po.frames.empty() ? empty_frame : po.frames.back();
  const bool two_frames = po.frames.size() >= 2;

  // ----- Element totals (with initial/final/Δ when two frames exist) -----
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

  if (two_frames) {
    setHeaders(totals_table_,
               {tr("Element"), tr("Master species"),
                tr("Initial (mol/kgw)"), tr("Final (mol/kgw)"),
                tr("Δ (mol/kgw)"), tr("Δ (g/kgw)")});
  } else {
    setHeaders(totals_table_,
               {tr("Element"), tr("Master species"),
                tr("Molality (mol/kgw)"), tr("Mass (g/kgw)"), tr("Moles")});
  }
  totals_table_->setRowCount(ordered_roots.size());
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
      if (init) totals_table_->setItem(i, 2, sciItem(init->molality));
      else      totals_table_->setItem(i, 2, textItem(QStringLiteral("—")));
      if (fin)  totals_table_->setItem(i, 3, sciItem(fin->molality));
      else      totals_table_->setItem(i, 3, textItem(QStringLiteral("—")));
      const double dm = (fin ? fin->molality : 0.0)
                      - (init ? init->molality : 0.0);
      totals_table_->setItem(i, 4, sciItem(dm));
      if (aw > 0.0)
        totals_table_->setItem(i, 5, sciItem(dm * aw));
      else
        totals_table_->setItem(i, 5, textItem(QStringLiteral("—")));
    } else {
      const double mol = fin ? fin->molality : (init ? init->molality : 0.0);
      const double moles = fin ? fin->moles : (init ? init->moles : 0.0);
      totals_table_->setItem(i, 2, sciItem(mol));
      totals_table_->setItem(i, 3,
          aw > 0.0 ? sciItem(mol * aw) : textItem(QStringLiteral("—")));
      totals_table_->setItem(i, 4, sciItem(moles));
    }
  }

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
    si_table_->setItem(i, 5, textItem(QString::fromStdString(s.formula)));
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
