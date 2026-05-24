#include "ui/MainWindow.h"

#include <QApplication>
#include <clocale>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("qt_chemistry");
  QApplication::setOrganizationName("qt_chemistry");
  std::setlocale(LC_NUMERIC, "C");
  qtchem::MainWindow w;
  w.show();
  return app.exec();
}
