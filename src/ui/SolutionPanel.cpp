#include "ui/SolutionPanel.h"

#include "kernel/DatabaseInfo.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qtchem {

namespace {
constexpr int kColElement = 0;
constexpr int kColMaster  = 1;
constexpr int kColTotal   = 2;
constexpr int kColUnits   = 3;
}

SolutionPanel::SolutionPanel(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);

  auto* state = new QFormLayout;
  temp_ = new QDoubleSpinBox;
  temp_->setRange(0, 350); temp_->setSuffix(" °C"); temp_->setValue(25);
  state->addRow(tr("Temperature"), temp_);

  pressure_ = new QDoubleSpinBox;
  pressure_->setRange(0.001, 2000); pressure_->setSuffix(" atm");
  pressure_->setValue(1);
  state->addRow(tr("Pressure"), pressure_);

  auto* ph_row = new QHBoxLayout;
  ph_mode_ = new QComboBox;
  ph_mode_->addItem(tr("Fixed"), int(PhSpec::Fixed));
  ph_mode_->addItem(tr("Charge balance"), int(PhSpec::ChargeBalance));
  ph_mode_->addItem(tr("Phase equilibrium"), int(PhSpec::PhaseEquilibrium));
  ph_value_ = new QDoubleSpinBox;
  ph_value_->setRange(-2, 16); ph_value_->setDecimals(3); ph_value_->setValue(7);
  ph_row->addWidget(ph_mode_); ph_row->addWidget(ph_value_);
  state->addRow(tr("pH"), ph_row);

  auto* ph_extra = new QHBoxLayout;
  charge_el_ = new QLineEdit; charge_el_->setPlaceholderText(tr("element for charge"));
  ph_phase_ = new QLineEdit; ph_phase_->setPlaceholderText(tr("phase, e.g. Calcite"));
  ph_si_ = new QDoubleSpinBox; ph_si_->setRange(-10, 10); ph_si_->setValue(0);
  ph_si_->setDecimals(2); ph_si_->setPrefix(tr("SI="));
  ph_extra->addWidget(new QLabel(tr("Charge el:")));
  ph_extra->addWidget(charge_el_);
  ph_extra->addWidget(new QLabel(tr("Phase:")));
  ph_extra->addWidget(ph_phase_);
  ph_extra->addWidget(ph_si_);
  state->addRow(QString(), ph_extra);

  activity_override_ = new QComboBox;
  activity_override_->addItem(tr("From database (default)"),
                              int(ActivityOverride::UseDatabase));
  activity_override_->addItem(tr("Debye–Hückel limiting law  (γ from -A z² √I)"),
                              int(ActivityOverride::DebyeHuckel));
  activity_override_->addItem(tr("Ideal — γ = 1  (schoolbook)"),
                              int(ActivityOverride::Ideal));
  activity_override_->setToolTip(tr(
      "Overrides the database's activity-coefficient model by re-emitting "
      "every aqueous reaction with -gamma. Use 'Debye–Hückel' for the pure "
      "limiting law (valid only at very low ionic strength) or 'Ideal' for "
      "the schoolbook case where activity equals molality."));
  state->addRow(tr("Activity model"), activity_override_);
  root->addLayout(state);

  db_hint_ = new QLabel(tr(
      "Enter <b>elements</b>, not species. Notation: <code>N</code> = total N, "
      "<code>N(5)</code> = nitrate, <code>C(4)</code> = inorganic C, "
      "<code>S(6)</code> = sulfate. Master species column shows the "
      "charged form. Typing a species (e.g. <code>NO3-</code>) is "
      "auto-translated when possible."));
  db_hint_->setWordWrap(true);
  db_hint_->setStyleSheet("color: #555; font-size: 11px;");
  root->addWidget(db_hint_);

  table_ = new QTableWidget(0, 4);
  table_->setHorizontalHeaderLabels(
      {tr("Element"), tr("Master species"), tr("Total"), tr("Units")});
  auto* hh = table_->horizontalHeader();
  hh->setStretchLastSection(true);
  hh->setSectionResizeMode(kColElement, QHeaderView::ResizeToContents);
  hh->setSectionResizeMode(kColMaster,  QHeaderView::ResizeToContents);
  table_->verticalHeader()->setVisible(false);
  root->addWidget(table_, 1);

  auto* btns = new QHBoxLayout;
  auto* add = new QPushButton(tr("+ row"));
  auto* rem = new QPushButton(tr("− row"));
  auto* sample = new QPushButton(tr("Load sample (seawater)"));
  auto* clear = new QPushButton(tr("Clear"));
  auto* run = new QPushButton(tr("▶ Run equilibrium"));
  run->setDefault(true);
  btns->addWidget(add); btns->addWidget(rem); btns->addStretch(1);
  btns->addWidget(sample); btns->addWidget(clear); btns->addWidget(run);
  root->addLayout(btns);

  connect(add, &QPushButton::clicked, this, &SolutionPanel::onAddRow);
  connect(rem, &QPushButton::clicked, this, &SolutionPanel::onRemoveRow);
  connect(sample, &QPushButton::clicked, this, &SolutionPanel::loadSampleSeawater);
  connect(clear, &QPushButton::clicked, this, &SolutionPanel::loadEmpty);
  connect(run, &QPushButton::clicked, this, &SolutionPanel::runRequested);
  connect(table_, &QTableWidget::cellChanged,
          this, &SolutionPanel::onCellChanged);

  loadSampleSeawater();
}

SolutionPanel::~SolutionPanel() = default;

void SolutionPanel::setDatabaseInfo(std::shared_ptr<const DatabaseInfo> info) {
  db_info_ = std::move(info);
  refreshAllMasterSpecies();
}

void SolutionPanel::refreshMasterSpecies(int row) {
  if (row < 0 || row >= table_->rowCount()) return;
  auto* el = table_->item(row, kColElement);
  const QString name = el ? el->text().trimmed() : QString();
  QString text;
  QColor bg;
  if (!db_info_) {
    text = QStringLiteral("(load db)");
  } else if (name.isEmpty()) {
    text.clear();
  } else if (auto m = db_info_->findByElement(name.toStdString())) {
    text = QString::fromStdString(m->species);
  } else if (auto el2 = db_info_->elementForSpecies(name.toStdString())) {
    text = tr("→ %1 (auto)").arg(QString::fromStdString(*el2));
    bg = QColor(255, 240, 200);
  } else {
    text = tr("(unknown)");
    bg = QColor(255, 220, 220);
  }
  auto* item = table_->item(row, kColMaster);
  if (!item) {
    item = new QTableWidgetItem;
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, kColMaster, item);
  }
  item->setText(text);
  item->setBackground(bg.isValid() ? QBrush(bg) : QBrush());
}

void SolutionPanel::refreshAllMasterSpecies() {
  for (int r = 0; r < table_->rowCount(); ++r) refreshMasterSpecies(r);
}

void SolutionPanel::onCellChanged(int row, int col) {
  if (col == kColElement) refreshMasterSpecies(row);
}

void SolutionPanel::setRow(int row, const QString& el, double total,
                           const QString& units) {
  QSignalBlocker block(table_);
  table_->setItem(row, kColElement, new QTableWidgetItem(el));
  auto* master = new QTableWidgetItem;
  master->setFlags(master->flags() & ~Qt::ItemIsEditable);
  table_->setItem(row, kColMaster, master);
  auto* it1 = new QTableWidgetItem(QString::number(total, 'g', 6));
  it1->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  table_->setItem(row, kColTotal, it1);
  table_->setItem(row, kColUnits, new QTableWidgetItem(units));
  refreshMasterSpecies(row);
}

void SolutionPanel::onAddRow() {
  const int r = table_->rowCount();
  table_->insertRow(r);
  setRow(r, QString(), 0.0, QStringLiteral("mol/kgw"));
  table_->editItem(table_->item(r, kColElement));
}

void SolutionPanel::onRemoveRow() {
  const auto rows = table_->selectionModel()->selectedRows();
  if (!rows.isEmpty()) {
    table_->removeRow(rows.first().row());
  } else if (table_->rowCount() > 0) {
    table_->removeRow(table_->rowCount() - 1);
  }
}

void SolutionPanel::loadEmpty() {
  table_->setRowCount(0);
  temp_->setValue(25); pressure_->setValue(1);
  ph_mode_->setCurrentIndex(0); ph_value_->setValue(7);
  ph_phase_->clear(); ph_si_->setValue(0); charge_el_->clear();
}

void SolutionPanel::loadSampleSeawater() {
  loadEmpty();
  temp_->setValue(25); pressure_->setValue(1);
  ph_mode_->setCurrentIndex(0); ph_value_->setValue(8.22);
  struct R { const char* el; double v; const char* u; };
  const R rows[] = {
      {"Na", 10.77, "g/kgw"}, {"K", 0.399, "g/kgw"},
      {"Ca", 0.412, "g/kgw"}, {"Mg", 1.291, "g/kgw"},
      {"Cl", 19.353, "g/kgw"}, {"S(6)", 2.712, "g/kgw"},
      {"C(4)", 0.139, "g/kgw"},
  };
  for (const auto& r : rows) {
    const int i = table_->rowCount();
    table_->insertRow(i);
    setRow(i, QString::fromLatin1(r.el), r.v, QString::fromLatin1(r.u));
  }
}

EquilibriumProblem SolutionPanel::buildProblem(QStringList* warnings) const {
  EquilibriumProblem p;
  p.title = "qt_chemistry run";
  p.temperature_c = temp_->value();
  p.pressure_atm = pressure_->value();
  p.water_kg = 1.0;
  p.ph.kind = PhSpec(ph_mode_->currentData().toInt());
  p.ph.value = ph_value_->value();
  p.ph.phase = ph_phase_->text().toStdString();
  p.ph.target_si = ph_si_->value();
  p.ph.charge_element = charge_el_->text().toStdString();
  p.activity_override =
      ActivityOverride(activity_override_->currentData().toInt());

  for (int r = 0; r < table_->rowCount(); ++r) {
    auto* el = table_->item(r, kColElement);
    auto* tt = table_->item(r, kColTotal);
    auto* un = table_->item(r, kColUnits);
    if (!el || el->text().trimmed().isEmpty()) continue;
    SolutionComponent c;
    const std::string typed = el->text().trimmed().toStdString();
    c.element = typed;
    if (db_info_) {
      if (!db_info_->findByElement(typed)) {
        if (auto translated = db_info_->elementForSpecies(typed)) {
          c.element = *translated;
          if (warnings) {
            *warnings << tr("Row %1: '%2' is a species — using element '%3'.")
                             .arg(r + 1)
                             .arg(QString::fromStdString(typed))
                             .arg(QString::fromStdString(*translated));
          }
        } else if (warnings) {
          *warnings << tr("Row %1: '%2' is not a known element or master "
                          "species in this database.")
                           .arg(r + 1)
                           .arg(QString::fromStdString(typed));
        }
      }
    }
    c.total = tt ? tt->text().toDouble() : 0.0;
    c.units = un ? un->text().trimmed().toStdString() : "mol/kgw";
    if (c.units.empty()) c.units = "mol/kgw";
    p.components.push_back(c);
  }

  p.phases = {
      {"Calcite", 0.0, 0.0}, {"Aragonite", 0.0, 0.0},
      {"Gypsum", 0.0, 0.0}, {"Halite", 0.0, 0.0},
  };
  return p;
}

}
