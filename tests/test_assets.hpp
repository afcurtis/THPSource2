#pragma once

// Where the native tests find the player's own game files, and how they say so when those files are not there.
//
// The files are not in the repository. `tony media extract` and `tony assets ...` produce them under build/ in a checkout:
//   build/assets/all-pkr/files/{data,newtex}/   the unpacked ALL.PKR
//   build/disc/files/SETUP/data/ALL.PKR          the package itself
// CMake passes the checkout root as OPENTONY_SOURCE_DIR. Two environment variables override the defaults:
//   OPENTONY_ASSET_ROOT   the folder that holds data/ and newtex/
//   OPENTONY_ALL_PKR      the ALL.PKR file
//
// A missing file is not a failure. have_file / have_dir print "skipped: missing ..." and remember it; the test returns exit_code() from main, which is
// kSkipExit (77) if any game file was missing. CMake sets SKIP_RETURN_CODE 77 on these tests, so ctest lists them as skipped rather than passed.

#include <cstdlib>
#include <filesystem>
#include <iostream>

#ifndef OPENTONY_SOURCE_DIR
#error "OPENTONY_SOURCE_DIR must be defined by CMake (the checkout root)"
#endif

namespace opentony::test {

inline constexpr int kSkipExit = 77;

[[nodiscard]] inline std::filesystem::path env_or(
    const char* name,
    const std::filesystem::path& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::filesystem::path(value) : fallback;
}

// The folder that holds data/ and newtex/.
[[nodiscard]] inline std::filesystem::path asset_root() {
    return env_or(
        "OPENTONY_ASSET_ROOT",
        std::filesystem::path(OPENTONY_SOURCE_DIR) / "build/assets/all-pkr/files");
}

[[nodiscard]] inline std::filesystem::path data_dir() { return asset_root() / "data"; }

[[nodiscard]] inline std::filesystem::path newtex_dir() { return asset_root() / "newtex"; }

[[nodiscard]] inline std::filesystem::path all_pkr() {
    return env_or(
        "OPENTONY_ALL_PKR",
        std::filesystem::path(OPENTONY_SOURCE_DIR) / "build/disc/files/SETUP/data/ALL.PKR");
}

[[nodiscard]] inline bool& missing_game_file() {
    static bool missing = false;
    return missing;
}

[[nodiscard]] inline bool have_file(const std::filesystem::path& path) {
    if (std::filesystem::is_regular_file(path)) {
        return true;
    }
    std::cout << "skipped: missing " << path.string() << '\n';
    missing_game_file() = true;
    return false;
}

[[nodiscard]] inline bool have_dir(const std::filesystem::path& path) {
    if (std::filesystem::is_directory(path)) {
        return true;
    }
    std::cout << "skipped: missing " << path.string() << '\n';
    missing_game_file() = true;
    return false;
}

// What main returns: 0, or kSkipExit when some game file was missing (the CHECKs that did run have all passed).
[[nodiscard]] inline int exit_code() { return missing_game_file() ? kSkipExit : 0; }

} // namespace opentony::test
