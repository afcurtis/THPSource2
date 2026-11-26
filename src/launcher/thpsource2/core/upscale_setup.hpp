// THPSource2 launcher: what the "Upscale textures" button needs besides the pipeline (texture-upscaling plan T5; docs/LAUNCHER.md, "Upscaled textures"). Portable:
// the shipped data (the census recipe, the class table and the download pins, compiled in from re/config by scripts/make_upscale_tables.py), the downloads' plan
// and check, one census run's environment, the merge of the runs' dumps, and what is on disk. The app runs the processes (curl.exe, tar.exe, the game, the tool).
#pragma once

#include "upscale.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace thpsource2::upscale_setup {

struct FilePin {
    const char* url;
    std::uint64_t size;
    const char* sha256;
};
struct Download {   // the tool: a zip whose `member` is the program
    const char* name;
    const char* url;
    std::uint64_t size;
    const char* sha256;
    const char* member;
    const char* licence;
    const char* source;
};
struct ModelPin {
    const char* save_as;   // the name the pipeline runs it by: models\<save_as>.param + .bin
    const char* licence;
    const char* author;
    FilePin param;
    FilePin bin;
};
// One run of the census recipe (re/config/texture_census.yml): a level run (the scenario bootstrap launches the level) or a keyed session from the boot
struct CensusRun {
    const char* name;
    bool psx;
    int level;             // -1: a session
    int skater;            // -1: the scenario's
    int look;
    int frames;
    const char* actions;   // the scenario's action lines, one a line
    const char* keys;      // a session's OPENTONY_SESSION_KEYS
    int deliveries;        // a session's
    const char* unlock;    // the run's own OT_UNLOCK; nullptr: the recipe's
    bool undrawn_2d;
};

const Download& tool();
std::filesystem::path tool_program(const std::filesystem::path& folder);
std::span<const ModelPin> models();
const ModelPin* model(const std::string& name);
std::span<const CensusRun> census();
std::string census_unlock();
std::uint64_t census_hash();   // of the recipe: a finished census is not run again while it is the same
upscale::Options chosen_options();   // the person's pick (re/config/upscaler.yml `chosen`)

// The class table, written back as re/config/texture_classes.tsv byte for byte (the pipeline reads a file, and the set's stamp holds its hash)
std::string class_table_text();
bool write_class_table(const std::filesystem::path& file, std::string& error);

// ---- the downloads ----
struct Fetch {
    std::string what;              // for the progress line
    std::string url;
    std::filesystem::path to;      // the file the download writes
    std::uint64_t size = 0;
    std::string sha256;
    bool zip = false;              // the tool's zip: unpacked, its member moved to `program`
    std::string member;
    std::filesystem::path program;
};
// What is missing in `folder` (Data\Upscaler) for `options`: the tool unless `program` exists, each model a class uses unless both its files are there and the right
// size. A model the pins do not name is not fetched (the pipeline then reports it missing).
std::vector<Fetch> download_plan(const std::filesystem::path& folder, const upscale::Options& options);
std::uint64_t download_bytes(const std::vector<Fetch>& plan);
// A downloaded file's size and SHA-256; a wrong one is deleted and the error says which
bool check_download(const Fetch& fetch, std::string& error);

// ---- the census ----
struct CensusPaths {
    std::filesystem::path game;       // the game folder (the run's working directory)
    std::filesystem::path data;       // Data\Unpacked\data (OPENTONY_DATA_DIR)
    std::filesystem::path pkr;        // ALL.PKR
    std::filesystem::path run_dir;    // Data\TextureDump\runs\<name>: the dump, the log, the scenario file, an empty saves and config folder
};
// The run's environment (every OT_ and OPENTONY_ variable the game reads that the run needs; the app starts from its own environment without any of them)
std::vector<std::pair<std::string, std::string>> census_environment(const CensusRun& run, const CensusPaths& paths);
std::string scenario_text(const CensusRun& run);   // a level run's OT_SCENARIO file
int census_deliveries(const CensusRun& run);

// Folds the runs' dumps (each <run>\index.tsv and its PNGs) into `out` (Data\TextureDump), which may hold a dump already (the player's own, Debug mode): a key
// new to `out` gets its PNG moved and its row; a known one has its use, wrap and mod2x widened (the union over runs, as scripts/texture_census.py merges).
// Returns the number of keys in `out` after it, -1 with `error`.
int merge_dumps(const std::vector<std::filesystem::path>& runs, const std::filesystem::path& out, std::string& error);

// ---- what is on disk ----
struct State {
    bool tool = false;            // Data\Upscaler\upscayl-bin.exe
    bool models = false;          // every model the pick needs
    bool census = false;          // Data\TextureDump\census.done with this recipe's hash
    int dumped = 0;               // PNGs in Data\TextureDump
    int upscaled = 0;             // PNGs in Data\Upscaled
    std::uintmax_t upscaled_bytes = 0;
    bool stamp = false;           // Data\Upscaled\stamp.txt is there: a set the pipeline made (and finished, or was stopped)
};
State state(const std::filesystem::path& upscaler, const std::filesystem::path& dump, const std::filesystem::path& upscaled, const upscale::Options& options);
// A set exists that the game can use: at least one upscaled PNG and a stamp
inline bool set_exists(const State& s) { return s.upscaled > 0 && s.stamp; }
// The disk a full set takes (T4.5, measured on the person's PC: 709 MB at 4x; 2x is a quarter of the texels)
std::uint64_t estimate_bytes(int scale);

}  // namespace thpsource2::upscale_setup
