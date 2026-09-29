// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

%module thm_py

%{
#include "thm/Thermal.h"
#include "ord/OpenRoad.hh"
%}

%include "../../Exception-py.i"
%include <std_string.i>
%include <std_vector.i>

%include "thm/ThermalConfig.h"
%include "thm/PhysicsState.h"
%include "thm/Thermal.h"
