// THPSource2 launcher: the "Upscale textures" button's portable half (upscale_setup.hpp).
#include "upscale_setup.hpp"

#include "game_files.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace thpsource2::upscale_setup {

namespace fs = std::filesystem;

namespace {

#include "upscale_tables.inc"

std::uint64_t fnv(std::uint64_t hash, const std::string& text) {
    for (const unsigned char c : text) {
        hash = (hash ^ c) * 0x100000001b3ull;
    }
    return (hash ^ 0xff) * 0x100000001b3ull;   // a separator, so "ab"+"c" and "a"+"bc" differ
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) {
            return fields;
        }
        start = tab + 1;
    }
}

std::string native(const fs::path& path) {
    fs::path p = path;
    return p.make_preferred().string();
}

bool same_size(const fs::path& file, std::uint64_t size) {
    std::error_code ec;
    return fs::is_regular_file(file, ec) && fs::file_size(file, ec) == size;
}

// the index's flag columns as bits (scripts/texture_census.py USE, WRAP, MOD), so a union is an OR
int bits_of(const std::string& value, const char* one, const char* two) {
    return value == one ? 1 : value == two ? 2 : value == "both" ? 3 : 0;
}
std::string name_of(int bits, const char* one, const char* two) { return bits == 1 ? one : bits == 2 ? two : bits == 3 ? "both" : "unknown"; }

struct IndexRow {
    std::vector<std::string> fields;   // key width height format alpha use wrap mod2x volatile tag
};

bool read_index_rows(const fs::path& index, std::vector<IndexRow>& rows) {
    std::ifstream in(index);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        IndexRow row{split_tabs(line)};
        if (row.fields.size() >= 10 && row.fields[0].size() == 16) {
            rows.push_back(std::move(row));
        }
    }
    return true;
}

int count_pngs(const fs::path& folder, std::uintmax_t* bytes) {
    int count = 0;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".png") {
            ++count;
            if (bytes != nullptr) {
                std::error_code size_ec;
                const std::uintmax_t size = it->file_size(size_ec);
                *bytes += size_ec ? 0 : size;
            }
        }
    }
    return count;
}

}  // namespace

const Download& tool() {
#ifdef __linux__
    static constexpr Download linux_tool{
        "upscayl-bin",
        "https://github.com/upscayl/upscayl-ncnn/releases/download/20251207-174704/upscayl-bin-20251207-174704-linux.zip",
        3952825,
        "a9fab3c770b62f2b7a35d8d6d61eb4e8b3aef79128b665c919d080b85a2292f2",
        "upscayl-bin-20251207-174704-linux/upscayl-bin",
        "AGPL-3.0",
        "https://github.com/upscayl/upscayl-ncnn",
    };
    return linux_tool;
#else
    return kTool;
#endif
}

fs::path tool_program(const fs::path& folder) {
#ifdef __linux__
    return folder / "upscayl-bin";
#else
    return folder / "upscayl-bin.exe";
#endif
}
std::span<const ModelPin> models() { return kModels; }
const ModelPin* model(const std::string& name) {
    for (const ModelPin& m : kModels) {
        if (name == m.save_as) {
            return &m;
        }
    }
    return nullptr;
}
std::span<const CensusRun> census() { return kCensusRuns; }
std::string census_unlock() { return kCensusUnlock; }

std::uint64_t census_hash() {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    hash = fnv(hash, kCensusUnlock);
    for (const CensusRun& r : kCensusRuns) {
        hash = fnv(hash, std::string(r.name) + (r.psx ? "psx" : "pc") + std::to_string(r.level) + "/" + std::to_string(r.skater) + "/" + std::to_string(r.look) + "/" +
                             std::to_string(r.frames) + r.actions + r.keys + std::to_string(r.deliveries) + (r.unlock != nullptr ? r.unlock : "-"));
    }
    return hash;
}

upscale::Options chosen_options() {
    upscale::Options o;
    o.model_world = kChosenWorld;
    o.model_2d = kChosen2d;
    o.scale = kChosenScale;
    o.alpha = kChosenAlphaModel ? upscale::AlphaMethod::Model : upscale::AlphaMethod::Bicubic;
    return o;
}

std::string class_table_text() {
    std::string text;
    text.reserve(210000);
    for (const char* line : kClassRows) {
        text += line;
        text += '\n';
    }
    return text;
}

bool write_class_table(const fs::path& file, std::string& error) {
    const std::string text = class_table_text();
    std::error_code ec;
    if (same_size(file, text.size())) {
        std::ifstream in(file, std::ios::binary);
        const std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (old == text) {
            return true;
        }
    }
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        error = "cannot write " + file.string();
        return false;
    }
    return true;
}

// ---- the downloads ----

std::vector<Fetch> download_plan(const fs::path& folder, const upscale::Options& options) {
    std::vector<Fetch> plan;
    std::error_code ec;
    if (!fs::is_regular_file(tool_program(folder), ec)) {
        const auto& binary = tool();
        Fetch f;
        f.what = std::string("the upscaler (") + binary.name + ", " + binary.licence + ")";
        f.url = binary.url;
        f.to = folder / "upscayl-bin.zip";
        f.size = binary.size;
        f.sha256 = binary.sha256;
        f.zip = true;
        f.member = binary.member;
        f.program = tool_program(folder);
        plan.push_back(f);
    }
    std::vector<std::string> names;
    for (const std::string& name : {options.model_world, options.model_2d}) {
        if (name != upscale::kNearest && name != upscale::kNone && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    for (const std::string& name : names) {
        const ModelPin* m = model(name);
        if (m == nullptr) {
            continue;
        }
        for (const auto& [pin, extension] : {std::pair{m->param, ".param"}, std::pair{m->bin, ".bin"}}) {
            const fs::path to = folder / "models" / (name + extension);
            if (same_size(to, pin.size)) {
                continue;
            }
            Fetch f;
            f.what = std::string("the model ") + name + extension + " (" + m->licence + ")";
            f.url = pin.url;
            f.to = to;
            f.size = pin.size;
            f.sha256 = pin.sha256;
            plan.push_back(f);
        }
    }
    return plan;
}

std::uint64_t download_bytes(const std::vector<Fetch>& plan) {
    std::uint64_t total = 0;
    for (const Fetch& f : plan) {
        total += f.size;
    }
    return total;
}

bool check_download(const Fetch& fetch, std::string& error) {
    std::error_code ec;
    const std::uintmax_t size = fs::is_regular_file(fetch.to, ec) ? fs::file_size(fetch.to, ec) : 0;
    if (size != fetch.size) {
        error = fetch.what + ": " + std::to_string(size) + " bytes downloaded, " + std::to_string(fetch.size) + " expected";
        fs::remove(fetch.to, ec);
        return false;
    }
    const std::string sha = sha256_file(fetch.to);
    if (sha != fetch.sha256) {
        error = fetch.what + ": the file's SHA-256 is not the pinned one (" + sha + "); it was deleted";
        fs::remove(fetch.to, ec);
        return false;
    }
    return true;
}

// ---- the census ----

int census_deliveries(const CensusRun& run) { return run.level < 0 ? run.deliveries : 4000 + 8 * run.frames; }

std::string scenario_text(const CensusRun& run) {
    std::string text = "level " + std::to_string(run.level) + "\nframes " + std::to_string(run.frames) + "\nsha256 " + kScenarioSha256 + "\n";
    if (run.skater >= 0) {
        text += "skater " + std::to_string(run.skater) + "\n";
    }
    if (run.look >= 0) {
        text += "look " + std::to_string(run.look) + "\n";
    }
    std::istringstream actions(run.actions);
    std::string line;
    while (std::getline(actions, line)) {
        if (!line.empty()) {
            text += "action " + line + "\n";
        }
    }
    return text;
}

std::vector<std::pair<std::string, std::string>> census_environment(const CensusRun& run, const CensusPaths& paths) {
    std::vector<std::pair<std::string, std::string>> env = {
        {"OT_SPIKE_CALL", "boot"},
        {"OPENTONY_GAME_DIR", native(paths.game) + "\\"},
        {"OPENTONY_DATA_DIR", native(paths.data)},
        {"OPENTONY_PKR", native(paths.pkr)},
        {"OPENTONY_LOG_DIR", native(paths.run_dir)},
        {"OPENTONY_SAVES_DIR", native(paths.run_dir / "saves")},     // a career run's save never reaches the player's Saves
        {"OPENTONY_CONFIG_DIR", native(paths.run_dir / "config")},   // nor the player's options: the census runs on the game's own
        {"OPENTONY_SESSION_TRACE", std::to_string(census_deliveries(run))},
        {"OPENTONY_SESSION_EVERY", "100000"},
        {"OPENTONY_SESSION_SKIP_MOVIES", "1"},
        {"OT_TEXTURE_DUMP", native(paths.run_dir)},   // with no OT_VIDEO_SDL: no window, the frames are recorded, not drawn
        {"OT_TEXTURE_DUMP_TAG", run.name},
        {"OT_PSX_TEXTURES", run.psx ? "1" : "0"},
        {"OT_UNLOCK", run.unlock != nullptr ? run.unlock : kCensusUnlock},
    };
    if (run.level < 0) {
        env.emplace_back("OPENTONY_SESSION_KEYS", run.keys);
    } else {
        env.emplace_back("OT_SCENARIO", native(paths.run_dir / "scenario.txt"));
        env.emplace_back("OT_SCENARIO_OUT", native(paths.run_dir / "census.otcap"));
        if (run.frames > 1200) {   // the capture buffer holds about 2,500 frames by default (scripts/texture_census.py)
            env.emplace_back("OT_SCENARIO_MAPPING_MB", std::to_string(run.frames / 12 + 16));
        }
    }
    return env;
}

int merge_dumps(const std::vector<fs::path>& runs, const fs::path& out, std::string& error) {
    std::error_code ec;
    fs::create_directories(out, ec);
    std::vector<std::string> order;
    std::map<std::string, IndexRow> rows;
    const auto add = [&](IndexRow row, const fs::path& from) {
        const std::string key = row.fields[0];
        auto it = rows.find(key);
        if (it == rows.end()) {
            if (!from.empty()) {
                const fs::path picture = out / (key + ".png");
                if (!fs::exists(picture, ec)) {
                    fs::rename(from / (key + ".png"), picture, ec);
                    if (ec) {
                        ec.clear();
                        fs::copy_file(from / (key + ".png"), picture, ec);
                    }
                    if (ec || !fs::exists(picture)) {
                        return;   // no picture: the key is left out (the pipeline reports it not dumped)
                    }
                }
            }
            order.push_back(key);
            rows.emplace(key, std::move(row));
            return;
        }
        std::vector<std::string>& f = it->second.fields;
        f[5] = name_of(bits_of(f[5], "world", "2d") | bits_of(row.fields[5], "world", "2d"), "world", "2d");
        f[6] = name_of(bits_of(f[6], "wrap", "clamp") | bits_of(row.fields[6], "wrap", "clamp"), "wrap", "clamp");
        f[7] = name_of(bits_of(f[7], "yes", "no") | bits_of(row.fields[7], "yes", "no"), "yes", "no");
        if (row.fields[8] == "1") {
            f[8] = "1";
        }
    };
    std::vector<IndexRow> existing;
    read_index_rows(out / "index.tsv", existing);
    for (IndexRow& row : existing) {
        add(std::move(row), fs::path());
    }
    for (const fs::path& run : runs) {
        std::vector<IndexRow> these;
        if (!read_index_rows(run / "index.tsv", these)) {
            continue;
        }
        for (IndexRow& row : these) {
            add(std::move(row), run);
        }
    }
    std::string text = "# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag\n";
    for (const std::string& key : order) {
        const std::vector<std::string>& f = rows[key].fields;
        for (std::size_t k = 0; k < 10; ++k) {
            text += f[k] + (k == 9 ? "\n" : "\t");
        }
    }
    const fs::path temporary = out / "index.tsv.tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) {
            error = "cannot write " + temporary.string();
            return -1;
        }
    }
    fs::rename(temporary, out / "index.tsv", ec);
    if (ec) {
        error = "cannot replace " + (out / "index.tsv").string() + ": " + ec.message();
        return -1;
    }
    return static_cast<int>(order.size());
}

// ---- what is on disk ----

State state(const fs::path& upscaler, const fs::path& dump, const fs::path& upscaled, const upscale::Options& options) {
    State s;
    std::error_code ec;
    s.tool = fs::is_regular_file(tool_program(upscaler), ec);
    s.models = true;
    for (const Fetch& f : download_plan(upscaler, options)) {
        s.models = s.models && f.zip;   // only the tool is missing (or nothing)
    }
    std::ifstream done(dump / "census.done");
    std::string line;
    s.census = std::getline(done, line) && line == std::to_string(census_hash());
    s.dumped = count_pngs(dump, nullptr);
    s.upscaled = count_pngs(upscaled, &s.upscaled_bytes);
    s.stamp = fs::is_regular_file(upscaled / "stamp.txt", ec);
    return s;
}

std::uint64_t estimate_bytes(int scale) { return scale == 2 ? 180ull << 20 : 709ull << 20; }

}  // namespace thpsource2::upscale_setup
