cmake_minimum_required(VERSION 3.21)
project(qt_chemistry LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/qt /opt/homebrew/opt/qt6)
endif()

option(QTCHEM_BUILD_TESTS "Build unit tests" ON)

set(IPHREEQC_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(IPHREEQC_FORTRAN_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/iphreeqc EXCLUDE_FROM_ALL)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

set(QTCHEM_IPHREEQC_DATABASE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/external/iphreeqc/database"
  CACHE PATH "Default directory containing PHREEQC .dat files")

add_subdirectory(src)

if(QTCHEM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
