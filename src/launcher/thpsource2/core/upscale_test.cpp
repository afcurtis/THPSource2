// The texture upscaler's pipeline (core/upscale.hpp) and PNG codec (core/png.hpp), natively, with a stub model: nearest 4x plus a colour drift, which the
// pipeline's colour match must take out again. No GPU, no upscaler tool.
#include "png.hpp"
#include "upscale.hpp"
#include "upscale_setup.hpp"
#include "true_colour.hpp"

#include "hal/texture_key_hash.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using thpsource2::png::Image;
namespace up = thpsource2::upscale;
namespace png = thpsource2::png;

namespace {

int g_failures = 0;

void check(bool ok, const char* what, int line) {
    if (!ok) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

Image solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    Image image(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = image.at(x, y);
            p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        }
    }
    return image;
}

Image noise(int w, int h, unsigned seed, bool alpha) {
    std::mt19937 random(seed);
    Image image(w, h);
    for (std::size_t k = 0; k < image.rgba.size(); ++k) {
        image.rgba[k] = static_cast<std::uint8_t>(40 + random() % 170);   // away from 0 and 255, so a drift is never clamped
        if (k % 4 == 3) {
            image.rgba[k] = alpha ? ((random() % 3) == 0 ? 0 : 255) : 255;
        }
    }
    return image;
}

bool same(const Image& a, const Image& b) { return a.width == b.width && a.height == b.height && a.rgba == b.rgba; }

bool same_rgb(const Image& a, const Image& b, int tolerance) {
    if (a.width != b.width || a.height != b.height) {
        return false;
    }
    for (std::size_t k = 0; k < a.rgba.size(); ++k) {
        if (k % 4 != 3 && std::abs(a.rgba[k] - b.rgba[k]) > tolerance) {
            return false;
        }
    }
    return true;
}

void test_png() {
    const Image rgba = noise(37, 23, 1, true);
    Image back;
    std::string error;
    CHECK(png::decode(png::encode(rgba, true), back, error));
    CHECK(same(rgba, back));
    CHECK(png::decode(png::encode(rgba, false), back, error));
    CHECK(back.width == 37 && back.height == 23 && back.at(5, 5)[3] == 255 && back.at(5, 5)[0] == rgba.at(5, 5)[0]);
    std::vector<std::uint8_t> file = png::encode(rgba, true);
    file[40] ^= 0xff;   // inside IDAT: the CRC no longer matches
    CHECK(!png::decode(file, back, error));
    CHECK(!png::decode(std::vector<std::uint8_t>(10, 0), back, error));
}

void test_steps() {
    // bleed: the one filled texel's colour spreads; alpha is kept
    Image cut = solid(5, 3, 0, 0, 0, 0);
    std::uint8_t* centre = cut.at(2, 1);
    centre[0] = 200; centre[1] = 100; centre[2] = 50; centre[3] = 255;
    const Image bled = up::bleed(cut);
    CHECK(bled.at(0, 0)[0] == 200 && bled.at(4, 2)[1] == 100 && bled.at(4, 0)[2] == 50);
    CHECK(bled.at(0, 0)[3] == 0 && bled.at(2, 1)[3] == 255);
    CHECK(same(up::bleed(solid(4, 4, 9, 9, 9, 0)), solid(4, 4, 9, 9, 9, 0)));   // nothing to spread from
    CHECK(up::has_transparency(cut) && !up::has_transparency(solid(2, 2, 1, 1, 1, 255)));

    // pad: tiled or edge-repeated; crop takes it back off
    const Image tile = noise(8, 4, 2, false);
    const Image wrapped = up::pad(tile, 16, true);
    CHECK(wrapped.width == 40 && wrapped.height == 36);
    CHECK(std::equal(wrapped.at(0, 0), wrapped.at(0, 0) + 4, tile.at(0, 0)));   // -16 mod 8 = 0, -16 mod 4 = 0
    CHECK(std::equal(wrapped.at(15, 15), wrapped.at(15, 15) + 4, tile.at(7, 3)));
    const Image clamped = up::pad(tile, 16, false);
    CHECK(std::equal(clamped.at(0, 0), clamped.at(0, 0) + 4, tile.at(0, 0)));
    CHECK(std::equal(clamped.at(39, 35), clamped.at(39, 35) + 4, tile.at(7, 3)));
    CHECK(same(up::crop(wrapped, 16), tile) && same(up::crop(clamped, 16), tile));

    // nearest and the box mean undo each other; bicubic of a flat picture is flat
    CHECK(same(up::box_down(up::nearest(tile, 4), 4), tile));
    CHECK(same(up::bicubic(solid(3, 3, 10, 20, 30, 255), 4), solid(12, 12, 10, 20, 30, 255)));

    // the colour match takes a model's drift out
    Image drifted = up::nearest(tile, 4);
    for (std::size_t k = 0; k < drifted.rgba.size(); k += 4) {
        drifted.rgba[k] = static_cast<std::uint8_t>(drifted.rgba[k] + 25);
        drifted.rgba[k + 2] = static_cast<std::uint8_t>(drifted.rgba[k + 2] - 15);
    }
    up::match_colour(drifted, tile, 4);
    CHECK(same_rgb(up::box_down(drifted, 4), tile, 0));

    const Image grey = up::alpha_as_grey(cut);
    CHECK(grey.at(2, 1)[0] == 255 && grey.at(0, 0)[1] == 0 && grey.at(0, 0)[3] == 255);
}

// The stub model: every PNG of `input`, nearest 4x, red +20, blue -10, into `output`.
int g_model_runs = 0;
bool g_model_fails = false;
bool stub_model(const std::string& name, const fs::path& input, const fs::path& output, std::string& error) {
    ++g_model_runs;
    if (g_model_fails || name.empty()) {
        error = "stub failure";
        return false;
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(input)) {
        Image image;
        if (!png::read(entry.path(), image, error)) {
            return false;
        }
        Image big = up::nearest(image, 4);
        for (std::size_t k = 0; k < big.rgba.size(); k += 4) {
            big.rgba[k] = static_cast<std::uint8_t>(std::min(255, big.rgba[k] + 20));
            big.rgba[k + 2] = static_cast<std::uint8_t>(std::max(0, big.rgba[k + 2] - 10));
        }
        if (!png::write(output / entry.path().filename(), big, true, error)) {
            return false;
        }
    }
    return true;
}

void put(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

void test_pipeline() {
    const fs::path root = fs::temp_directory_path() / "opentony_upscale_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path dump = root / "dump", out = root / "out", table = root / "classes.tsv";
    fs::create_directories(dump);
    // a 32 x 32 opaque wrapped world texture, a 16 x 16 cut-out world texture (fence), an 8 x 16 cut-out glyph (2D), an 8 x 8 world texture (too small)
    const Image ground = noise(32, 32, 3, false), fence = noise(16, 16, 4, true), glyph = noise(8, 16, 5, true), tiny = noise(8, 8, 6, false);
    std::string error;
    CHECK(png::write(dump / "00000000000000a1.png", ground, true, error));
    CHECK(png::write(dump / "00000000000000a2.png", fence, true, error));
    CHECK(png::write(dump / "00000000000000a3.png", glyph, true, error));
    CHECK(png::write(dump / "00000000000000a4.png", tiny, true, error));
    put(dump / "index.tsv",
        "# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag\n"
        "00000000000000a1\t32\t32\targb1555\topaque\tworld\twrap\tno\t0\tpc-L0\n"
        "00000000000000a2\t16\t16\targb1555\tcutout\tworld\tclamp\tno\t0\tpc-L0\n"
        "00000000000000a3\t8\t16\targb1555\tcutout\t2d\tclamp\tno\t0\tpc-L0\n"
        "00000000000000a4\t8\t8\targb1555\topaque\tworld\twrap\tno\t0\tpc-L0\n");
    put(table,
        "# key\twidth\theight\tclass\tevidence\tcensus\n"
        "00000000000000a1\t32\t32\tworld\tdrawn\t1\n"
        "00000000000000a2\t16\t16\tworld\tdrawn\t1\n"
        "00000000000000a3\t8\t16\t2d\tdrawn\t1\n"
        "00000000000000a4\t8\t8\tworld\tdrawn\t1\n"
        "00000000000000a5\t8\t8\t2d\trule:global\t1\n");   // in the table, never dumped
    const auto quiet = [](const std::string&) {};

    up::Options options;
    options.model_world = "stub-a";
    options.model_2d = "stub-b";
    g_model_runs = 0;
    std::vector<std::pair<int, int>> told;   // the progress callback: the count first, then after each batch
    up::Report report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr, [&](int done, int total) { told.emplace_back(done, total); });
    CHECK(told.size() == 3 && told.front() == std::make_pair(0, 3) && told.back() == std::make_pair(3, 3));
    CHECK(report.in_table == 5 && report.selected == 3 && report.small == 1 && report.not_dumped == 1 && report.written == 3 && report.failed == 0);
    CHECK(g_model_runs == 2);   // one run a class
    Image result;
    CHECK(png::read(out / "00000000000000a1.png", result, error) && result.width == 128 && result.height == 128);
    CHECK(same_rgb(up::box_down(result, 4), ground, 0));   // the drift is gone
    CHECK(!up::has_transparency(result));
    CHECK(png::read(out / "00000000000000a2.png", result, error) && result.width == 64);
    for (int y = 0; y < 16; ++y) {   // the model's alpha (here nearest) keeps the fence's cut-outs where they were
        for (int x = 0; x < 16; ++x) {
            CHECK((result.at(x * 4 + 1, y * 4 + 2)[3] >= 128) == (fence.at(x, y)[3] == 255));
        }
    }
    CHECK(png::read(out / "00000000000000a3.png", result, error) && result.width == 32 && result.height == 64);
    bool exact = true;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 32; ++x) {
            exact = exact && result.at(x, y)[3] == glyph.at(x / 4, y / 4)[3];
        }
    }
    CHECK(exact);   // a 2D cut-out's outline is the original's, by nearest
    CHECK(!fs::exists(out / "00000000000000a4.png") && !fs::exists(out / ".work") && fs::exists(out / "stamp.txt"));

    // the same stamp: nothing is done again (a stopped run resumes)
    g_model_runs = 0;
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr);
    CHECK(report.selected == 0 && report.done_before == 3 && g_model_runs == 0);

    // another stamp (2x, bicubic alpha, nearest 2D): the old set goes, the new one is 2x
    options.scale = 2;
    options.alpha = up::AlphaMethod::Bicubic;
    options.model_2d = up::kNearest;
    g_model_runs = 0;
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr);
    CHECK(report.selected == 3 && report.written == 3 && g_model_runs == 1);
    CHECK(png::read(out / "00000000000000a1.png", result, error) && result.width == 64);
    CHECK(same_rgb(up::box_down(result, 2), ground, 0));
    CHECK(png::read(out / "00000000000000a3.png", result, error) && same(result, up::nearest(glyph, 2)));
    CHECK(png::read(out / "00000000000000a2.png", result, error) && result.width == 32 && up::has_transparency(result));

    // a class left out, a subset of keys, a model that fails, a cancel
    fs::remove_all(out);
    options = up::Options();
    options.model_world = up::kNone;
    options.model_2d = up::kNearest;
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr);
    CHECK(report.left_out == 3 && report.written == 1);
    options.model_world = "stub-a";
    options.keys = {"00000000000000a1"};
    g_model_fails = true;
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr);
    CHECK(report.in_table == 1 && report.failed == 1 && report.written == 0 && !fs::exists(out / "00000000000000a1.png"));
    g_model_fails = false;
    const std::atomic<bool> cancel{true};
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, &cancel);
    CHECK(report.cancelled && report.written == 0);
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr);
    CHECK(report.written == 1 && fs::exists(out / "00000000000000a1.png"));

    // true-colour originals (graphics-2 G5): a picture of the dump's size wins over the dump's, one of another size does not; the stamp changes
    fs::remove_all(out);
    const fs::path colours = root / "colours";
    fs::create_directories(colours);
    const Image ground_24 = noise(32, 32, 77, false);
    CHECK(png::write(colours / "00000000000000a1.png", ground_24, false, error));
    CHECK(png::write(colours / "00000000000000a3.png", noise(4, 4, 78, true), true, error));   // not the glyph's size: the dump's is used
    options = up::Options();
    options.model_world = "stub-a";
    options.model_2d = up::kNearest;
    options.originals = colours;
    report = up::run(dump, table, out, options, "stub", stub_model, quiet, nullptr);
    CHECK(report.written == 3 && report.failed == 0);
    CHECK(png::read(out / "00000000000000a1.png", result, error) && same_rgb(up::box_down(result, 4), ground_24, 0));   // matched to the true colour, not the dump
    CHECK(png::read(out / "00000000000000a3.png", result, error) && same(result, up::nearest(glyph, 4)));
    CHECK(up::stamp_text(options, "stub", 1).find("originals true-colour") != std::string::npos);
    options.originals.clear();
    CHECK(up::stamp_text(options, "stub", 1).find("originals") == std::string::npos);   // a set made before keeps its stamp

    up::Options bad;
    bad.scale = 3;
    CHECK(up::run(dump, table, out, bad, "stub", stub_model, quiet, nullptr).failed == 1);
    CHECK(up::run(root / "nowhere", table, out, up::Options(), "stub", stub_model, quiet, nullptr).failed == 1);
    fs::remove_all(root, ec);
}

// T5: the shipped data, the download plan and check, a census run's environment, the merge of the runs' dumps, the state
void test_setup() {
    namespace us = thpsource2::upscale_setup;
    namespace upscale = thpsource2::upscale;
    const fs::path root = fs::temp_directory_path() / "opentony_upscale_setup_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    std::string error;

    // the pick and the pins
    const upscale::Options chosen = us::chosen_options();
    CHECK(chosen.model_world == "ultrasharp-4x" && chosen.model_2d == upscale::kNearest && chosen.scale == 4 && chosen.alpha == upscale::AlphaMethod::Model);
    CHECK(us::model("ultrasharp-4x") != nullptr && us::model("no such model") == nullptr && std::string(us::tool().name) == "upscayl-bin");
    // the class table comes back as the file (comments, columns, a line end after the last line)
    const std::string table = us::class_table_text();
    CHECK(table.rfind("# The texture class table", 0) == 0 && table.back() == '\n' && table.find("\tworld\t") != std::string::npos && table.size() > 100000);
    CHECK(us::write_class_table(root / "Upscaler" / "texture_classes.tsv", error));
    std::map<std::string, upscale::Class> classes;
    std::uint64_t hash = 0;
    CHECK(upscale::read_classes(root / "Upscaler" / "texture_classes.tsv", classes, hash, error) && classes.size() > 5000);

    // the census: level runs and sessions, its hash stable
    CHECK(us::census().size() > 100 && us::census_hash() == us::census_hash());
    const us::CensusRun* level = nullptr;
    const us::CensusRun* session = nullptr;
    for (const us::CensusRun& r : us::census()) {
        level = level == nullptr && r.level >= 0 && r.psx && std::string(r.actions).empty() ? &r : level;
        session = session == nullptr && r.level < 0 ? &r : session;
    }
    CHECK(level != nullptr && session != nullptr);
    if (level != nullptr && session != nullptr) {
        const std::string scenario = us::scenario_text(*level);
        CHECK(scenario.rfind("level " + std::to_string(level->level) + "\nframes 600\nsha256 ", 0) == 0);
        const auto env = us::census_environment(*level, {root / "game", root / "data", root / "game" / "ALL.PKR", root / "runs" / level->name});
        const auto value = [&env](const char* name) {
            for (const auto& [k, v] : env) {
                if (k == name) {
                    return v;
                }
            }
            return std::string("<absent>");
        };
        CHECK(value("OT_PSX_TEXTURES") == "1" && value("OT_UNLOCK") == us::census_unlock() && value("OT_TEXTURE_DUMP_TAG") == level->name);
        CHECK(value("OT_SCENARIO").find("scenario.txt") != std::string::npos && value("OPENTONY_SESSION_TRACE") == std::to_string(4000 + 8 * 600));
        CHECK(value("OT_VIDEO_SDL") == "<absent>" && value("OPENTONY_SESSION_KEYS") == "<absent>" && value("OPENTONY_SAVES_DIR").find("saves") != std::string::npos);
        const auto senv = us::census_environment(*session, {root / "game", root / "data", root / "game" / "ALL.PKR", root / "runs" / session->name});
        bool keys = false, no_scenario = true;
        for (const auto& [k, v] : senv) {
            keys = keys || (k == "OPENTONY_SESSION_KEYS" && v == session->keys && !v.empty());
            no_scenario = no_scenario && k != "OT_SCENARIO";
        }
        CHECK(keys && no_scenario);
    }

    // the downloads: a folder with nothing needs the tool and the model's two files; a wrong file is deleted
    const fs::path upscaler = root / "Upscaler";
    std::vector<us::Fetch> plan = us::download_plan(upscaler, chosen);
    CHECK(plan.size() == 3 && plan[0].zip && plan[1].to.filename() == "ultrasharp-4x.param" && plan[2].to.filename() == "ultrasharp-4x.bin");
    upscale::Options nearest_only = chosen;
    nearest_only.model_world = upscale::kNearest;
    CHECK(us::download_plan(upscaler, nearest_only).size() == 1);
    us::Fetch local;
    local.what = "a test file";
    local.to = root / "file.bin";
    { std::ofstream(local.to, std::ios::binary) << "abc"; }
    local.size = 3;
    local.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";   // SHA-256 of "abc"
    CHECK(us::check_download(local, error) && fs::exists(local.to));
    local.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    CHECK(!us::check_download(local, error) && !fs::exists(local.to) && error.find("SHA-256") != std::string::npos);
    { std::ofstream(local.to, std::ios::binary) << "abcd"; }
    CHECK(!us::check_download(local, error) && !fs::exists(local.to));

    // the merge: two runs and a dump that is there already; a known key's use and wrap are widened, a new key's picture moved
    const fs::path out = root / "TextureDump", a = root / "runs" / "a", b = root / "runs" / "b";
    for (const fs::path& d : {out, a, b}) {
        fs::create_directories(d);
    }
    const std::string head = "# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag\n";
    std::ofstream(out / "index.tsv") << head << "00000000000000aa\t8\t8\targb1555\topaque\tworld\tclamp\tno\t0\tplay\n";
    std::ofstream(a / "index.tsv") << head << "00000000000000aa\t8\t8\targb1555\topaque\tworld\twrap\tno\t0\ta\n"
                                   << "00000000000000bb\t8\t8\targb1555\tcutout\t2d\tclamp\tno\t0\ta\n";
    std::ofstream(b / "index.tsv") << head << "00000000000000bb\t8\t8\targb1555\tcutout\tworld\tclamp\tyes\t0\tb\n"
                                   << "00000000000000cc\t8\t8\targb1555\topaque\tunknown\tunknown\tunknown\t0\tb\n";
    for (const auto& [dir, key] : {std::pair{out, "00000000000000aa"}, std::pair{a, "00000000000000aa"}, std::pair{a, "00000000000000bb"},
                                   std::pair{b, "00000000000000bb"}, std::pair{b, "00000000000000cc"}}) {
        std::ofstream(dir / (std::string(key) + ".png")) << "x";
    }
    CHECK(us::merge_dumps({a, b}, out, error) == 3);
    std::map<std::string, upscale::DumpRow> rows;
    CHECK(upscale::read_index(out / "index.tsv", rows, error) && rows.size() == 3);
    CHECK(rows["00000000000000aa"].wrap == upscale::Wrap::Both && rows["00000000000000bb"].alpha == upscale::Alpha::Cutout);
    CHECK(fs::exists(out / "00000000000000bb.png") && fs::exists(out / "00000000000000cc.png") && !fs::exists(a / "00000000000000bb.png"));
    {
        std::ifstream in(out / "index.tsv");
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find("00000000000000bb\t8\t8\targb1555\tcutout\tboth\tclamp\tboth\t0\ta") != std::string::npos);
    }
    CHECK(us::merge_dumps({a, b}, out, error) == 3);   // again: nothing doubles

    // the state: nothing upscaled, then a set with a stamp
    us::State s = us::state(upscaler, out, root / "Upscaled", chosen);
    CHECK(!s.tool && !s.models && !s.census && s.dumped == 3 && s.upscaled == 0 && !us::set_exists(s));
    fs::create_directories(root / "Upscaled");
    std::ofstream(root / "Upscaled" / "00000000000000aa.png") << "x";
    std::ofstream(root / "Upscaled" / "stamp.txt") << "pipeline 1\n";
    std::ofstream(out / "census.done") << us::census_hash() << "\n";
    s = us::state(upscaler, out, root / "Upscaled", chosen);
    CHECK(s.census && s.upscaled == 1 && us::set_exists(s));
    fs::remove_all(root, ec);
}

// A 24-bit bitmap file: bottom row first, rows padded to four bytes. `rgb` is top down, three bytes a texel.
std::vector<std::uint8_t> make_bmp(int w, int h, const std::vector<std::uint8_t>& rgb, int bits = 24) {
    const int row = ((w * 3) + 3) & ~3;
    std::vector<std::uint8_t> f(54 + static_cast<std::size_t>(row) * static_cast<std::size_t>(h), 0);
    const auto put = [&f](std::size_t at, std::uint32_t v, int n) {
        for (int k = 0; k < n; ++k) {
            f[at + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(v >> (8 * k));
        }
    };
    f[0] = 'B'; f[1] = 'M';
    put(2, static_cast<std::uint32_t>(f.size()), 4);
    put(10, 54, 4);
    put(14, 40, 4);
    put(18, static_cast<std::uint32_t>(w), 4);
    put(22, static_cast<std::uint32_t>(h), 4);
    put(26, 1, 2);
    put(28, static_cast<std::uint32_t>(bits), 2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p = &rgb[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3];
            std::uint8_t* d = &f[54 + static_cast<std::size_t>(row) * static_cast<std::size_t>(h - 1 - y) + static_cast<std::size_t>(x) * 3];
            d[0] = p[2]; d[1] = p[1]; d[2] = p[0];   // BGR
        }
    }
    return f;
}

void write_bytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// True-colour bitmaps (graphics-2 G5): the key is the game's key of the 1555 surface, the picture keeps all 8 bits, the key colour is black and clear
void test_true_colour() {
    namespace tc = thpsource2::true_colour;
    const int w = 18, h = 5;   // 18 x 3 = 54 bytes a row: padded to 56
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w * h * 3));
    for (std::size_t k = 0; k < rgb.size(); ++k) {
        rgb[k] = static_cast<std::uint8_t>((k * 7 + 3) % 251);
    }
    rgb[0] = 255; rgb[1] = 0; rgb[2] = 250;   // the top-left texel is the key colour (R > 0xf7, G = 0, B > 0xf7)
    std::uint64_t key = 0;
    png::Image picture;
    bool keyed = false;
    CHECK(tc::convert(make_bmp(w, h, rgb), key, picture, keyed) && keyed && picture.width == w && picture.height == h);
    // the surface by hand: 1555, top down, the key colour 0
    std::vector<std::uint8_t> surface;
    for (int k = 0; k < w * h; ++k) {
        const std::uint8_t r = rgb[static_cast<std::size_t>(k) * 3], g = rgb[static_cast<std::size_t>(k) * 3 + 1], b = rgb[static_cast<std::size_t>(k) * 3 + 2];
        const std::uint16_t texel = k == 0 ? 0 : static_cast<std::uint16_t>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3) | 0x8000u);
        surface.push_back(static_cast<std::uint8_t>(texel & 0xff));
        surface.push_back(static_cast<std::uint8_t>(texel >> 8));
    }
    CHECK(key == ot_hal_texture_key(w, h, 2, surface.data(), surface.size()));
    CHECK(picture.at(0, 0)[3] == 0 && picture.at(0, 0)[0] == 0 && picture.at(1, 0)[3] == 255);
    CHECK(picture.at(5, 3)[0] == rgb[(3 * static_cast<std::size_t>(w) + 5) * 3] && picture.at(5, 3)[2] == rgb[(3 * static_cast<std::size_t>(w) + 5) * 3 + 2]);   // all 8 bits, not cut to 5
    // not a bitmap this takes
    CHECK(!tc::convert(make_bmp(w, h, rgb, 8), key, picture, keyed));
    CHECK(!tc::convert(std::vector<std::uint8_t>(20, 0), key, picture, keyed));
    CHECK(!tc::convert(make_bmp(300, 2, std::vector<std::uint8_t>(300 * 2 * 3, 9)), key, picture, keyed));   // over the largest texture

    // the folder: a level's bitmap, the same bitmap in another level (one file), a second one, an 8-bit one (skipped)
    const fs::path root = fs::temp_directory_path() / "opentony_true_colour_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    write_bytes(root / "newtex" / "Hangar" / "A.BMP", make_bmp(w, h, rgb));
    write_bytes(root / "newtex" / "Venice" / "A.BMP", make_bmp(w, h, rgb));
    write_bytes(root / "newbmp" / "B.bmp", make_bmp(16, 16, std::vector<std::uint8_t>(16 * 16 * 3, 200)));
    write_bytes(root / "newtex" / "C.BMP", make_bmp(w, h, rgb, 8));
    const fs::path out = root / "out";
    CHECK(!tc::is_current(root, out));
    int last_total = 0;
    const tc::Result made = tc::build(root, out, [&last_total](int, int total) { last_total = total; return true; });
    CHECK(made.written == 2 && made.skipped == 2 && made.failed == 0 && !made.cancelled && last_total == 4);
    CHECK(tc::count_files(out) == 2 && tc::is_current(root, out));
    char name[40];
    std::snprintf(name, sizeof name, "%016llx.png", static_cast<unsigned long long>(key));
    png::Image back;
    std::string error;
    CHECK(png::read(out / name, back, error) && back.width == w && back.height == h && back.at(0, 0)[3] == 0 && back.at(2, 2)[1] == picture.at(2, 2)[1]);
    CHECK(tc::build(root, out).unchanged == 2);   // a second run writes nothing
    write_bytes(root / "newtex" / "D.BMP", make_bmp(16, 16, std::vector<std::uint8_t>(16 * 16 * 3, 77)));
    CHECK(!tc::is_current(root, out));   // a new bitmap makes the set old
    CHECK(tc::build(root, out, [](int done, int) { return done < 1; }).cancelled);   // cancel stops
    fs::remove_all(root, ec);
}

}  // namespace

int main() {
    test_true_colour();
    test_png();
    test_steps();
    test_pipeline();
    test_setup();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    std::printf("upscale: all checks passed\n");
    return 0;
}
