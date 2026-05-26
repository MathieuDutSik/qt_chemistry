#pragma once

#include "kernel/EquilibriumProblem.h"

#include <QStringList>
#include <QWidget>
#include <memory>

class QTableWidget;
class QDoubleSpinBox;
class QComboBox;
class QLineEdit;
class QLabel;

namespace qtchem {

class DatabaseInfo;

class SolutionPanel : public QWidget {
  Q_OBJECT
public:
  explicit SolutionPanel(QWidget* parent = nullptr);
  ~SolutionPanel() override;

  void setDatabaseInfo(std::shared_ptr<const DatabaseInfo> info);

  EquilibriumProblem buildProblem(QStringList* warnings = nullptr) const;
  void loadSampleSeawater();
  void loadEmpty();

signals:
  void runRequested();

private slots:
  void onAddRow();
  void onRemoveRow();
  void onCellChanged(int row, int col);

private:
  void setRow(int row, const QString& el, double total, const QString& units);
  void refreshMasterSpecies(int row);
  void refreshAllMasterSpecies();

  QTableWidget* table_ = nullptr;
  QDoubleSpinBox* temp_ = nullptr;
  QDoubleSpinBox* pressure_ = nullptr;
  QComboBox* ph_mode_ = nullptr;
  QDoubleSpinBox* ph_value_ = nullptr;
  QLineEdit* ph_phase_ = nullptr;
  QDoubleSpinBox* ph_si_ = nullptr;
  QLineEdit* charge_el_ = nullptr;
  QLabel* db_hint_ = nullptr;
  QComboBox* activity_override_ = nullptr;

  std::shared_ptr<const DatabaseInfo> db_info_;
};

}
