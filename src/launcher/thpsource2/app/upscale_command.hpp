// THPSource2 launcher: `--upscale-textures`, the texture upscaler from the command line (texture-upscaling plan T4; docs/LAUNCHER.md, "Upscaled textures").
#pragma once

#include "launcher/layout.hpp"

namespace thpsource2 {

// True when the command line asks for it; then `exit_code` is the run's (0: every selected texture written).
bool upscale_command(const opentony::layout::Layout& install, int argc, char** argv, int& exit_code);

}  // namespace thpsource2
