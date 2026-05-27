add_library(qtchem_kernel STATIC
  EquilibriumProblem.h
  EquilibriumProblem.cpp
  PhreeqcSession.h
  PhreeqcSession.cpp
  PhreeqcOutputParser.h
  PhreeqcOutputParser.cpp
  DatabaseInfo.h
  DatabaseInfo.cpp
  EditableDatabase.h
  EditableDatabase.cpp
  ChemFormat.h
  ChemFormat.cpp)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core)
target_link_libraries(qtchem_kernel PUBLIC Qt6::Core)

target_include_directories(qtchem_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(qtchem_kernel PUBLIC IPhreeqc)
