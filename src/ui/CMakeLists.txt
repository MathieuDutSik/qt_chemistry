qt_add_library(qtchem_ui STATIC
  MainWindow.h
  MainWindow.cpp
  SolutionPanel.h
  SolutionPanel.cpp
  HtmlDelegate.h
  HtmlDelegate.cpp
  ChemDelegate.h
  ChemDelegate.cpp
  DatabaseEditorDialog.h
  DatabaseEditorDialog.cpp
  EntryEditForm.h
  EntryEditForm.cpp)

target_include_directories(qtchem_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(qtchem_ui
  PUBLIC qtchem_kernel Qt6::Core Qt6::Widgets)

target_compile_definitions(qtchem_ui PRIVATE
  QTCHEM_DEFAULT_DATABASE_DIR="${QTCHEM_IPHREEQC_DATABASE_DIR}")

set_target_properties(qtchem_ui PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)
