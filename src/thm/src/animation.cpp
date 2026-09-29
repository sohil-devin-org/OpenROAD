// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#include "thm/Animation.h"

#include "utl/Logger.h"

namespace thm {

bool writeAnimation(const PhysicsHistory& history,
                    const PhysicsHistory* reference,
                    const AnimationOptions& options,
                    utl::Logger* logger)
{
  logger->warn(utl::THM,
               90,
               "Animation export is not implemented yet; {} frames were not "
               "written to {}.",
               history.size(),
               options.file);
  return false;
}

}  // namespace thm
