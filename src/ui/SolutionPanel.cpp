#include "ui/SolutionPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>

namespace qtchem {

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

  root->addLayout(state);

  table_ = new QTableWidget(0, 3);
  table_->setHorizontalHeaderLabels({tr("Element"), tr("Total"), tr("Units")});
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
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

  loadSampleSeawater();
}

void SolutionPanel::setRow(int row, const QString& el, double total,
                           const QString& units) {
  table_->setItem(row, 0, new QTableWidgetItem(el));
  auto* it1 = new QTableWidgetItem(QString::number(total, 'g', 6));
  it1->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  table_->setItem(row, 1, it1);
  table_->setItem(row, 2, new QTableWidgetItem(units));
}

void SolutionPanel::onAddRow() {
  const int r = table_->rowCount();
  table_->insertRow(r);
  setRow(r, QString(), 0.0, QStringLiteral("mol/kgw"));
  table_->editItem(table_->item(r, 0));
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

EquilibriumProblem SolutionPanel::buildProblem() const {
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

  for (int r = 0; r < table_->rowCount(); ++r) {
    auto* el = table_->item(r, 0);
    auto* tt = table_->item(r, 1);
    auto* un = table_->item(r, 2);
    if (!el || el->text().trimmed().isEmpty()) continue;
    SolutionComponent c;
    c.element = el->text().trimmed().toStdString();
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
