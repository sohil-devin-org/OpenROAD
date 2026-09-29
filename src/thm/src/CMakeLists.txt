# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

include("openroad")

swig_lib(NAME      thm
         NAMESPACE thm
         I_FILE    thermal.i
         SWIG_INCLUDES ${OPENSTA_HOME}
         SCRIPTS   thermal.tcl
)

target_sources(thm
  PRIVATE
    MakeThermal.cpp
    Thermal.cpp
    heatMap.cpp
    hotspot.cpp
)

target_include_directories( thm
  PUBLIC
    ../include
  PRIVATE
    # A side-effect of including OpenSTA swig files
    ${OPENSTA_HOME}
    ${OPENSTA_HOME}/include/sta
)

target_link_libraries(thm
  PRIVATE
    utl_lib
    odb
    OpenSTA
    dbSta
    gui_core
    Boost::boost
)

messages(
  TARGET thm
  OUTPUT_DIR ..
)
