#pragma once

#include "kernel/PhreeqcOutputParser.h"

#include <QMainWindow>
#include <memory>

class QComboBox;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;

namespace qtchem {

class DatabaseInfo;
class PhreeqcSession;
class SolutionPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

private slots:
  void onDatabaseChanged(int index);
  void onRun();
  void onShowDatabaseInfo();
  void onDuplicateDatabase();
  void onEditDatabase();
  void onShowSpeciesColumnHelp();

private:
  void populateDatabaseList(const QString& selectAbsolutePath = QString());
  void renderResults(const ParsedOutput& po);
  void renderTotalsTab();
  QString buildDatabaseInfoHtml() const;
  bool isUserWritableDatabase(int index) const;

  std::unique_ptr<PhreeqcSession> session_;
  std::shared_ptr<DatabaseInfo> db_info_;
  QString current_database_path_;
  QComboBox* db_combo_ = nullptr;
  QPushButton* info_btn_ = nullptr;
  QPushButton* duplicate_btn_ = nullptr;
  QPushButton* edit_btn_ = nullptr;
  QLabel* db_status_ = nullptr;
  SolutionPanel* solution_panel_ = nullptr;
  QTabWidget* result_tabs_ = nullptr;
  QTableWidget* species_table_ = nullptr;
  QTableWidget* si_table_ = nullptr;
  QTableWidget* assemblage_table_ = nullptr;
  QTableWidget* totals_table_ = nullptr;
  QComboBox* totals_unit_initial_ = nullptr;
  QComboBox* totals_unit_final_ = nullptr;
  QComboBox* totals_unit_delta_ = nullptr;
  QLabel* totals_unit_lbl_initial_ = nullptr;
  QLabel* totals_unit_lbl_final_ = nullptr;
  QLabel* totals_unit_lbl_delta_ = nullptr;
  QLabel* totals_volume_source_ = nullptr;
  ParsedOutput last_parsed_;
  QTableWidget* desc_table_ = nullptr;
  QPlainTextEdit* input_view_ = nullptr;
  QPlainTextEdit* output_view_ = nullptr;
  QString database_dir_;
  QString user_database_dir_;
};

}
