add_subdirectory(kernel)
add_subdirectory(ui)

qt_add_executable(qt_chemistry main.cpp)
target_link_libraries(qt_chemistry PRIVATE qtchem_ui)
set_target_properties(qt_chemistry PROPERTIES
  MACOSX_BUNDLE OFF
  WIN32_EXECUTABLE OFF)
