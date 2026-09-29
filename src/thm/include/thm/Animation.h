// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025-2025, The OpenROAD Authors

#pragma once

#include "thm/PhysicsState.h"
#include "thm/Thermal.h"

namespace utl {
class Logger;
}

namespace thm {

// Renders the requested animation from recorded histories.  Frames are
// annotated with iteration/time, peak C, derated WNS, HPWL and a fixed C
// legend.  GIF output uses the bundled GIF encoder; MP4 output writes frames
// and invokes ffmpeg when it is available (never a build dependency).
// Returns false (after logging a warning) if the animation could not be
// written.
bool writeAnimation(const PhysicsHistory& history,
                    const PhysicsHistory* reference,
                    const AnimationOptions& options,
                    utl::Logger* logger);

}  // namespace thm
