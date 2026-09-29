// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "thm/MakeThermal.h"

#include "tcl.h"
#include "utl/decode.h"

extern "C" {
extern int Thm_Init(Tcl_Interp* interp);
}

namespace thm {

extern const char* thm_tcl_inits[];

void initThermal(Tcl_Interp* tcl_interp)
{
  Thm_Init(tcl_interp);
  utl::evalTclInit(tcl_interp, thm::thm_tcl_inits);
}

}  // namespace thm
