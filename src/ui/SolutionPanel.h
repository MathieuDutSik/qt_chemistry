#pragma once

#include "kernel/EquilibriumProblem.h"

#include <QWidget>

class QTableWidget;
class QDoubleSpinBox;
class QComboBox;
class QLineEdit;

namespace qtchem {

class SolutionPanel : public QWidget {
  Q_OBJECT
public:
  explicit SolutionPanel(QWidget* parent = nullptr);

  EquilibriumProblem buildProblem() const;
  void loadSampleSeawater();
  void loadEmpty();

signals:
  void runRequested();

private slots:
  void onAddRow();
  void onRemoveRow();

private:
  void setRow(int row, const QString& el, double total, const QString& units);

  QTableWidget* table_ = nullptr;
  QDoubleSpinBox* temp_ = nullptr;
  QDoubleSpinBox* pressure_ = nullptr;
  QComboBox* ph_mode_ = nullptr;
  QDoubleSpinBox* ph_value_ = nullptr;
  QLineEdit* ph_phase_ = nullptr;
  QDoubleSpinBox* ph_si_ = nullptr;
  QLineEdit* charge_el_ = nullptr;
};

}
