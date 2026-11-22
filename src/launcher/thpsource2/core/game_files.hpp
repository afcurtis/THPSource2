// THPSource2 launcher: the player's game files. A game folder holds THawk2.exe (the Activision Value re-release's, or the original 2000 release's
// unprotected one) and ALL.PKR (and, for the music and
// the movies, Music\ and med\Movies\); opentony.exe reads them from there. The files can be brought in from an installed game, from the disc (a drive
// letter or a copy of it: SETUP\data, SETUP\med) or from a disc image (.iso, .bin, .img). Nothing of the game is in this program.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace thpsource2 {

// re/config/native_hooks.yml builds.canonical (tests/test_launcher.py keeps it equal to opentony_launcher.cpp's)
constexpr const char* kCanonicalSha256 = "f2c7ca7cbc31abd8f748bd4afdc1e30aa1a6700ce91893b618450fd16172669c";
// re/xref/config.yml variants.original_2000: the original 2000 release's unprotected THawk2.exe (opentony.exe rebuilds its data, original_2000_static.inc),
// and the copy-protected one on that release's disc (SafeDisc 2, data encrypted: refused with the reason)
constexpr const char* kOriginal2000Sha256 = "003152194111a3e564c1a458da2a02cc1187a4f47c147a4126f5d37913cfee8a";
constexpr const char* kProtectedDiscSha256 = "bca39d172fff978c270a065903a394260de45f171f67ad00d9a094f66b35a9d9";

enum class GameBuild { Unknown, ValueRerelease, Original2000, Original2000Protected };
GameBuild build_of_sha256(const std::string& sha256);
bool build_supported(GameBuild build);
// why a THawk2.exe of `build` cannot be used (empty when it can)
std::string build_problem(GameBuild build, const std::string& sha256);

std::string sha256_file(const std::filesystem::path& path, const std::function<bool(std::uint64_t)>& progress = {});
std::string sha256_bytes(const void* data, std::size_t size);

// a file or directory of `directory` by name, whatever its case (a copied install may have any)
std::filesystem::path find_child(const std::filesystem::path& directory, std::string_view name);

struct GameCheck {
    std::filesystem::path exe;   // empty when missing
    std::filesystem::path pkr;
    GameBuild build = GameBuild::Unknown;
    std::string exe_sha256;
    bool prepared = false;        // thawk2_static.img and data\ exist in the unpacked folder, Data\Unpacked (opentony.exe --prepare ran)
    bool music = false;
    bool movies = false;
    bool exe_supported() const { return build_supported(build); }
    bool usable() const { return !pkr.empty() && (exe_supported() || (exe.empty() && prepared)); }
    std::string summary() const;
};
// `unpacked` is where opentony.exe keeps what it read out of the game (Data\Unpacked of the install). `hash` false skips the SHA-256 of THawk2.exe (a quick
// look; the build is then taken to be the Value re-release, which says only that the file exists)
GameCheck check_game_folder(const std::filesystem::path& folder, const std::filesystem::path& unpacked, bool hash = true);

// One file the import copies: its place in the source (a path in the folder or the image) and where it goes in the game folder
struct ImportItem {
    std::string source;          // relative, '/' separated
    std::string target;          // relative, '/' separated
    std::uint64_t size = 0;
    bool required = false;
};
struct ImportPlan {
    bool from_image = false;
    std::filesystem::path source;   // the folder or the image file
    std::vector<ImportItem> items;
    std::uint64_t total_bytes() const;
    std::string error;              // why there is no plan
};
// A folder: an installed game (THawk2.exe beside ALL.PKR) or a disc (SETUP\data). A file: a disc image.
ImportPlan plan_import(const std::filesystem::path& source);

struct ImportProgress {
    std::atomic<std::uint64_t> done{0};
    std::atomic<std::uint64_t> total{0};
    std::atomic<bool> cancel{false};
};
// copies the plan's files into `destination` (existing files of the same size are kept); false with `error` on the first failure
bool run_import(const ImportPlan& plan, const std::filesystem::path& destination, ImportProgress& progress, std::string& error);

}  // namespace thpsource2
