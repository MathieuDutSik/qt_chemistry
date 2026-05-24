add_library(qtchem_kernel STATIC
  EquilibriumProblem.h
  EquilibriumProblem.cpp
  PhreeqcSession.h
  PhreeqcSession.cpp
  PhreeqcOutputParser.h
  PhreeqcOutputParser.cpp
  DatabaseInfo.h
  DatabaseInfo.cpp)

target_include_directories(qtchem_kernel PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(qtchem_kernel PUBLIC IPhreeqc)
