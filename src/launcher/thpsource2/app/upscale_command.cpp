#include "upscale_command.hpp"
#include "launcher/layout.hpp"

#include "platform.hpp"
#include "upscale.hpp"
#include "true_colour.hpp"
#include "upscale_setup.hpp"
#include "upscale_task.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <thread>
#include <fstream>
#include <string>
#include <vector>

namespace thpsource2 {

namespace fs = std::filesystem;

namespace {

void usage() {
    std::fputs(
        "THPSource2.exe --upscale-textures [options]\n"
        "  --model3d NAME     world textures: a model in --models (NAME.param + NAME.bin), nearest, or none (default ultrasharp-4x)\n"
        "  --model2d NAME     HUD, fonts, menus: the same choices (default nearest)\n"
        "  --scale 2|4        (default 4)\n"
        "  --alpha model|bicubic   how a world texture's alpha is upscaled (default model)\n"
        "  --keys FILE        only the keys listed in FILE, one a line\n"
        "  --dump DIR         the dumped originals (default Data\\TextureDump)\n"
        "  --table FILE       the class table (default Data\\Upscaler\\texture_classes.tsv)\n"
        "  --out DIR          the upscaled set (default Data\\Upscaled)\n"
        "  --tool EXE         the ncnn upscaler (default Data\\Upscaler\\upscayl-bin.exe)\n"
        "  --models DIR       its models (default Data\\Upscaler\\models)\n"
        "Progress goes to the console and to Logs\\upscale.txt.\n",
        stdout);
}

}  // namespace

bool upscale_command(const opentony::layout::Layout& install, int argc, char** argv, int& exit_code) {
    bool wanted = false, census = false, download = false;
    fs::path game = install.game();
    for (int k = 1; k < argc; ++k) {
        const std::string argument = argv[k];
        wanted = wanted || argument == "--upscale-textures";
        census = census || argument == "--texture-census";
        download = download || argument == "--download-upscaler";
        if (argument == "--game" && k + 1 < argc) {
            game = path_from_utf8(argv[k + 1]);
        }
    }
    if (!wanted && !census && !download) {
        return false;
    }
    // a WIN32 program has no console of its own: print to the one it was started from, if any
#ifdef _WIN32   // (a Linux program always has its terminal's output)
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
    if (census || download) {   // the button's first steps on their own (upscale_task.cpp): the downloads, the census into Data\TextureDump
        UpscaleProgress progress;
        std::string error;
        UpscaleSteps steps;
        steps.download = download;
        steps.census = census;
        steps.upscale = false;
        const int jobs = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 6);
        const bool ok = run_upscale_task(install, game, upscale_setup::chosen_options(), jobs, [](const std::string& line) {
            std::printf("%s\n", line.c_str());
            std::fflush(stdout);
        }, progress, error, steps);
        if (!ok) {
            std::printf("error: %s\n", error.c_str());
        }
        exit_code = ok ? 0 : 1;
        return true;
    }
    upscale::Options options = upscale_setup::chosen_options();
    fs::path dump = install.texture_dump(), table = install.upscaler() / "texture_classes.tsv", out = install.upscaled();
    fs::path tool = install.upscaler() / opentony::layout::program_file("upscayl-bin"), models = install.upscaler() / "models", keys;
    for (int k = 1; k < argc; ++k) {
        const std::string argument = argv[k];
        const bool has_value = k + 1 < argc;
        if (argument == "--upscale-textures") {
            continue;
        } else if (argument == "--model3d" && has_value) {
            options.model_world = argv[++k];
        } else if (argument == "--model2d" && has_value) {
            options.model_2d = argv[++k];
        } else if (argument == "--scale" && has_value) {
            options.scale = std::atoi(argv[++k]);
        } else if (argument == "--alpha" && has_value) {
            const std::string value = argv[++k];
            options.alpha = value == "bicubic" ? upscale::AlphaMethod::Bicubic : upscale::AlphaMethod::Model;
        } else if (argument == "--keys" && has_value) {
            keys = path_from_utf8(argv[++k]);
        } else if (argument == "--dump" && has_value) {
            dump = path_from_utf8(argv[++k]);
        } else if (argument == "--table" && has_value) {
            table = path_from_utf8(argv[++k]);
        } else if (argument == "--out" && has_value) {
            out = path_from_utf8(argv[++k]);
        } else if (argument == "--tool" && has_value) {
            tool = path_from_utf8(argv[++k]);
        } else if (argument == "--models" && has_value) {
            models = path_from_utf8(argv[++k]);
        } else {
            usage();
            exit_code = 2;
            return true;
        }
    }
    if (dump == install.texture_dump() && true_colour::is_current(install.package_data(), install.true_colour())) {
        options.originals = install.true_colour();   // the 24-bit bitmaps in all 8 bits (graphics-2 G5), as the button uses them
    }
    if (!keys.empty()) {
        std::ifstream in(keys);
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.size() >= 16 && line[0] != '#') {
                options.keys.push_back(line.substr(0, 16));
            }
        }
    }
    std::error_code ec;
    fs::create_directories(install.logs(), ec);
    std::ofstream log_file(install.logs() / "upscale.txt", std::ios::app);
    const auto log = [&log_file](const std::string& line) {
        char when[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
        log_file << when << " " << line << "\n";
        log_file.flush();
    };
    log("upscale: dump " + utf8_of(dump) + ", out " + utf8_of(out) + ", world " + options.model_world + ", 2d " + options.model_2d + ", scale " +
        std::to_string(options.scale) + ", alpha " + (options.alpha == upscale::AlphaMethod::Model ? "model" : "bicubic") +
        (options.keys.empty() ? std::string() : ", " + std::to_string(options.keys.size()) + " keys") +
        (options.originals.empty() ? std::string() : ", originals " + utf8_of(options.originals)));

    if (!fs::is_regular_file(table, ec) && table == install.upscaler() / "texture_classes.tsv") {   // the shipped table, compiled in (upscale_setup.cpp)
        std::string error;
        if (!upscale_setup::write_class_table(table, error)) {
            log("error: " + error);
        }
    }
    const upscale::Report report = upscale::run(dump, table, out, options, upscaler_stamp_name(tool), upscaler_model_runner(tool, models), log, nullptr);
    for (const std::string& error : report.errors) {
        log("error: " + error);
    }
    exit_code = report.failed == 0 ? 0 : 1;
    return true;
}

}  // namespace thpsource2
