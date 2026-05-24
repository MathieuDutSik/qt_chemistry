#pragma once

#include <QMainWindow>
#include <memory>

class QComboBox;
class QPlainTextEdit;
class QLabel;
class QTabWidget;
class QTableWidget;

namespace qtchem {

class DatabaseInfo;
class PhreeqcSession;
class SolutionPanel;
struct ParsedOutput;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

private slots:
  void onDatabaseChanged(int index);
  void onRun();

private:
  void populateDatabaseList();
  void renderResults(const ParsedOutput& po);

  std::unique_ptr<PhreeqcSession> session_;
  std::shared_ptr<DatabaseInfo> db_info_;
  QComboBox* db_combo_ = nullptr;
  QLabel* db_status_ = nullptr;
  SolutionPanel* solution_panel_ = nullptr;
  QTabWidget* result_tabs_ = nullptr;
  QTableWidget* species_table_ = nullptr;
  QTableWidget* si_table_ = nullptr;
  QTableWidget* assemblage_table_ = nullptr;
  QTableWidget* totals_table_ = nullptr;
  QTableWidget* desc_table_ = nullptr;
  QPlainTextEdit* input_view_ = nullptr;
  QPlainTextEdit* output_view_ = nullptr;
  QString database_dir_;
};

}
