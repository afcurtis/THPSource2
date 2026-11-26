#pragma once

#include <filesystem>

namespace opentony::kitdll {

class LinuxGuestMemory;
void run_linux_initializers(LinuxGuestMemory& arena, const std::filesystem::path& game_dir = {}, bool try_boot = false);

}  // namespace opentony::kitdll
