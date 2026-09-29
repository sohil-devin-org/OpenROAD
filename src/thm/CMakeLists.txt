# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025-2025, The OpenROAD Authors

include("openroad")
swig_lib(NAME         thm
         NAMESPACE    thm
         I_FILE       src/thermal.i
         SCRIPTS      src/thermal.tcl
         SWIG_INCLUDES ${ODB_HOME}/src/swig/common
                       ${ODB_HOME}/src/swig/tcl
                       ${ODB_HOME}/include
)

# Physics engine without GUI / Tcl dependencies (unit tests link this).
add_library(thm_core
  src/FiniteVolumeSolver.cpp
  src/LeakageModel.cpp
  src/PhysicsState.cpp
  src/ThermalConfig.cpp
  src/ThermalGrid.cpp
  src/ThermalSolver.cpp
)

target_link_libraries(thm_core
  PUBLIC
    odb
    dbSta
  PRIVATE
    utl_lib
)

target_include_directories(thm_core
  PUBLIC
    include
  PRIVATE
    src
)

add_library(thm_lib
  src/PhysicsCoupling.cpp
  src/PlacementPhysics.cpp
  src/Thermal.cpp
  src/animation.cpp
  src/heatMap.cpp
)

target_link_libraries(thm_lib
  PUBLIC
    thm_core
    odb
    dbSta
    psm
    gpl_lib
  PRIVATE
    utl_lib
    gui_core
)

target_include_directories(thm_lib
  PUBLIC
    include
  PRIVATE
    src
)

target_sources(thm
  PRIVATE
    src/MakeThermal.cpp
)

target_include_directories(thm
  PUBLIC
    include
  PRIVATE
    src
)

target_link_libraries(thm
  PRIVATE
    ${TCL_LIBRARY}
    thm_lib
    gui_core
)

messages(
  TARGET thm
)

if (Python3_FOUND AND BUILD_PYTHON)
  swig_lib(NAME          thm_py
           NAMESPACE     thm
           LANGUAGE      python
           I_FILE        src/thermal-py.i
           SWIG_INCLUDES ${ODB_HOME}/src/swig/common
                         ${ODB_HOME}/src/swig/python
           SCRIPTS       ${CMAKE_CURRENT_BINARY_DIR}/thm_py.py
  )

  target_include_directories(thm_py
    PUBLIC
      include
  )

  target_link_libraries(thm_py
    PUBLIC
      thm_lib
      gui_core
      odb
  )
endif()

add_subdirectory(test)
