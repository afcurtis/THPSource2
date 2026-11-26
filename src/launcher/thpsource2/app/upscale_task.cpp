// THPSource2 launcher: the "Upscale textures" button's work (upscale_task.hpp).
#include "upscale_task.hpp"

#include "platform.hpp"
#include "true_colour.hpp"
#include "upscale_setup.hpp"


#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <span>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace thpsource2 {

namespace fs = std::filesystem;
namespace us = upscale_setup;

namespace {

std::string megabytes(std::uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; }

// ---- the downloads ----
bool download_all(const opentony::layout::Layout& where, const upscale::Options& options, const std::function<void(const std::string&)>& log,
                  UpscaleProgress& progress, std::string& error) {
    const fs::path folder = where.upscaler();
    const std::vector<us::Fetch> plan = us::download_plan(folder, options);
    if (plan.empty()) {
        log("download: the upscaler and the model are there already");
        return true;
    }
    progress.phase = 1;
    progress.done = 0;
    progress.total = us::download_bytes(plan);
    std::error_code ec;
    fs::create_directories(folder / "models", ec);
    const fs::path curl = system_program("curl.exe");
#ifdef _WIN32
    const fs::path tar = system_program("tar.exe");
    if (!fs::is_regular_file(curl, ec) || !fs::is_regular_file(tar, ec)) {
        error = "curl.exe or tar.exe is missing from Windows (both come with Windows 10 1803 and later).";
        return false;
    }
#endif
    for (const us::Fetch& fetch : plan) {
        if (progress.cancel) {
            error = "Cancelled. Start continues where it stopped.";
            return false;
        }
        log("download: " + fetch.what + ", " + megabytes(fetch.size) + " from " + fetch.url);
        std::vector<std::string> said;
        std::string run_error;
        const long code = run_process(curl, {"--location", "--fail", "--silent", "--show-error", "--retry", "2",
                                            "--connect-timeout", "15", "--max-time", "180", "--output", utf8_of(fetch.to), fetch.url}, folder, {},
                                      [&said](const std::string& line) { said.push_back(line); }, nullptr, run_error);
        if (code != 0) {
            error = fetch.what + ": the download failed (" + (run_error.empty() ? "curl exit " + std::to_string(code) : run_error) + ")" +
                    (said.empty() ? std::string() : ": " + said.back()) + ". Is the PC online?";
            fs::remove(fetch.to, ec);
            return false;
        }
        if (!us::check_download(fetch, error)) {
            return false;
        }
        if (fetch.zip) {
            const fs::path unpack = folder / "unpack";
            fs::remove_all(unpack, ec);
            fs::create_directories(unpack, ec);
#ifdef _WIN32
            const long unzip = run_process(tar, {"-xf", utf8_of(fetch.to), "-C", utf8_of(unpack)}, folder, {}, nullptr, nullptr, run_error);
#else
            const long unzip = run_process("unzip", {"-q", utf8_of(fetch.to), "-d", utf8_of(unpack)}, folder, {}, nullptr, nullptr, run_error);
#endif
            const fs::path member = unpack / fs::path(fetch.member).make_preferred();
            if (unzip != 0 || !fs::is_regular_file(member, ec)) {
                error = fetch.what + ": the zip could not be unpacked (" + (run_error.empty() ? "tar exit " + std::to_string(unzip) : run_error) + ")";
                return false;
            }
            fs::rename(member, fetch.program, ec);
            if (ec) {
                error = "cannot install " + utf8_of(fetch.program) + ": " + ec.message();
                return false;
            }
#ifndef _WIN32
            fs::permissions(fetch.program, fs::perms::owner_exec, fs::perm_options::add, ec);
            if (ec) {
                fs::remove(fetch.program);
                error = "cannot make the native upscaler executable: " + ec.message();
                return false;
            }
#endif
            const fs::path licence = member.parent_path() / "LICENSE";
            if (fs::is_regular_file(licence, ec)) {
                fs::copy_file(licence, folder / "LICENSE-upscayl-bin.txt", fs::copy_options::overwrite_existing, ec);
            }
            fs::remove_all(unpack, ec);
            fs::remove(fetch.to, ec);
            if (!fs::is_regular_file(fetch.program, ec)) {
                error = fetch.what + ": " + utf8_of(fetch.program) + " could not be written";
                return false;
            }
        }
        progress.done += fetch.size;
    }
    log("download: done");
    return true;
}

// ---- the census ----
struct CensusJob {
    const us::CensusRun* run;
    bool finished = false;
    double seconds = 0;
};

bool run_census(const opentony::layout::Layout& where, const fs::path& game, int jobs, const std::function<void(const std::string&)>& log, UpscaleProgress& progress,
                std::string& error) {
    std::error_code ec;
    const fs::path dump = where.texture_dump();
    const fs::path runs_root = dump / "runs";
    const fs::path spike = where.build() / "opentony_spike.exe";
    const fs::path standalone = where.build() / "opentony_standalone.dll";
    const fs::path image = where.static_image();
    const fs::path empty = where.unpacked() / "empty.tsv";
    const fs::path pkr = fs::is_regular_file(game / "ALL.PKR", ec) ? game / "ALL.PKR" : game / "all.pkr";
    if (!fs::is_regular_file(spike, ec) || !fs::is_regular_file(standalone, ec)) {
        error = "opentony_spike.exe or opentony_standalone.dll is missing from " + utf8_of(where.build());
        return false;
    }
    if (!fs::is_regular_file(image, ec) || !fs::is_directory(where.package_data(), ec)) {
        error = "the game's files are not prepared yet: start the game once with PLAY, then come back";
        return false;
    }
    if (!fs::is_regular_file(empty, ec)) {
        std::ofstream(empty, std::ios::binary).flush();
    }
    const std::span<const us::CensusRun> recipe = us::census();
    std::vector<CensusJob> todo;
    std::vector<fs::path> run_dirs;
    for (const us::CensusRun& run : recipe) {
        run_dirs.push_back(runs_root / run.name);
        if (!fs::is_regular_file(runs_root / run.name / "done.txt", ec)) {
            todo.push_back({&run});
        }
    }
    progress.phase = 2;
    progress.total = recipe.size();
    progress.done = recipe.size() - todo.size();
    log("census: " + std::to_string(recipe.size()) + " runs, " + std::to_string(recipe.size() - todo.size()) + " done already, " + std::to_string(todo.size()) +
        " to run, " + std::to_string(jobs) + " at a time, no window");
    std::mutex mutex;
    std::size_t next = 0;
    int failed = 0;
    const auto worker = [&] {
        for (;;) {
            CensusJob* job = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (next >= todo.size() || progress.cancel) {
                    return;
                }
                job = &todo[next++];
            }
            const us::CensusRun& run = *job->run;
            const fs::path dir = runs_root / run.name;
            std::error_code e;
            fs::remove_all(dir, e);
            fs::create_directories(dir / "saves", e);
            fs::create_directories(dir / "config", e);
            if (run.level >= 0) {
                std::ofstream(dir / "scenario.txt", std::ios::binary) << us::scenario_text(run);
            }
            const auto started = std::chrono::steady_clock::now();
            std::chrono::steady_clock::time_point done_at{};
            bool killed_after_done = false;
            const auto keep_going = [&]() {
                const auto now = std::chrono::steady_clock::now();
                if (progress.cancel) {
                    return false;
                }
                if (done_at == std::chrono::steady_clock::time_point{} && fs::exists(dir / "session.done", e)) {
                    done_at = now;
                }
                // some keyed sessions write session.done and then never exit (with the dump on, 11-17): the dump is complete then, the run is ended 15 s later
                if (done_at != std::chrono::steady_clock::time_point{} && now - done_at > std::chrono::seconds(15)) {
                    killed_after_done = true;
                    return false;
                }
                return now - started < std::chrono::seconds(1200);
            };
            std::string run_error;
            const long code = run_isolated(spike, {utf8_of(image), utf8_of(standalone), "-", utf8_of(empty)}, game,
                                           us::census_environment(run, {game, where.package_data(), pkr, dir}), dir / "run.txt", keep_going, run_error);
            job->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            fs::remove(dir / "census.otcap", e);
            job->finished = fs::exists(dir / "session.done", e) && fs::is_regular_file(dir / "index.tsv", e);
            if (job->finished) {
                std::ofstream(dir / "done.txt") << job->seconds << "\n";
            }
            std::lock_guard<std::mutex> lock(mutex);
            ++progress.done;
            if (!job->finished && !progress.cancel) {
                ++failed;
            }
            if (!progress.cancel) {
                log("census: " + std::string(run.name) + (job->finished ? " done" : " FAILED") + " in " + std::to_string(static_cast<int>(job->seconds)) + " s" +
                    (killed_after_done ? " (ended after session.done)" : "") + (job->finished ? "" : " (exit " + std::to_string(code) + (run_error.empty() ? "" : ", " + run_error) +
                                                                                   "; its log: " + utf8_of(dir / "run.txt") + ")"));
            }
        }
    };
    std::vector<std::thread> threads;
    for (int k = 0; k < jobs; ++k) {
        threads.emplace_back(worker);
    }
    for (std::thread& t : threads) {
        t.join();
    }
    if (progress.cancel) {
        error = "Cancelled. The textures found so far are kept; Start continues where it stopped.";
        return false;
    }
    std::string merge_error;
    const int keys = us::merge_dumps(run_dirs, dump, merge_error);
    if (keys < 0) {
        error = "census: " + merge_error;
        return false;
    }
    log("census: " + std::to_string(keys) + " textures in " + utf8_of(dump) + (failed != 0 ? ", " + std::to_string(failed) + " runs failed" : ""));
    if (failed == 0) {
        std::ofstream(dump / "census.done") << us::census_hash() << "\n";
        fs::remove_all(runs_root, ec);
    }
    return true;   // a failed run leaves a gap (its textures stay original); the next press of the button runs it again
}

#ifdef __linux__
bool scan_pc_bitmaps(const opentony::layout::Layout& where, const std::function<void(const std::string&)>& log,
                     UpscaleProgress& progress, std::string& error) {
    progress.phase = 2;
    const auto source = where.package_data();
    const auto dump = where.true_colour();
    if (!true_colour::is_current(source, dump)) {
        log("scan: matching the PC replacement bitmaps to their in-game texture keys");
        const auto result = true_colour::build(source, dump, [&](int done, int total) {
            progress.done = done;
            progress.total = total;
            return !progress.cancel;
        });
        if (result.cancelled || result.failed != 0) {
            error = result.cancelled ? "Cancelled. Start continues where it stopped." : "Could not prepare the PC texture bitmaps.";
            return false;
        }
    }

    const auto known_table = where.upscaler() / "texture_classes.tsv";
    std::map<std::string, upscale::Class> known;
    std::uint64_t hash = 0;
    if (!us::write_class_table(known_table, error) || !upscale::read_classes(known_table, known, hash, error)) {
        return false;
    }
    std::vector<fs::path> images;
    std::error_code ec;
    for (fs::directory_iterator it(dump, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".png") {
            images.push_back(it->path());
        }
    }
    if (images.empty() || ec) {
        error = "No PC texture bitmaps found. Prepare the game files first.";
        return false;
    }
    std::sort(images.begin(), images.end());
    const auto index_path = dump / "index.tsv";
    const auto table_path = where.upscaler() / "pc_bitmap_classes.tsv";
    const auto staged_index = dump / "index.tsv.tmp";
    const auto staged_table = where.upscaler() / "pc_bitmap_classes.tsv.tmp";
    std::ofstream index(staged_index);
    std::ofstream table(staged_table);
    progress.done = 0;
    progress.total = images.size();
    for (const auto& path : images) {
        if (progress.cancel) {
            error = "Cancelled. Start continues where it stopped.";
            return false;
        }
        png::Image picture;
        if (!png::read(path, picture, error)) {
            return false;
        }
        const auto key = path.stem().string();
        const auto cls = known.find(key);
        const bool two_d = cls != known.end() && cls->second == upscale::Class::TwoD;
        const auto alpha = upscale::has_transparency(picture) ? "cutout" : "opaque";
        const auto dimensions = key + "\t" + std::to_string(picture.width) + "\t" + std::to_string(picture.height) + "\t";
        index << dimensions << "1555\t" << alpha << "\t" << (two_d ? "2d\tclamp" : "world\tunknown") << "\t0\t0\tpc-bitmap\n";
        table << dimensions << (two_d ? "2d" : "world") << "\n";
        ++progress.done;
    }
    index.close();
    table.close();
    if (!index || !table) {
        error = "Could not write the PC texture index.";
        return false;
    }
    fs::rename(staged_table, table_path, ec);
    if (!ec) {
        fs::rename(staged_index, index_path, ec);
    }
    if (ec) {
        error = "Could not publish the PC texture index: " + ec.message();
        return false;
    }
    log("scan: " + std::to_string(images.size()) + " keyed PC texture bitmaps");
    return true;
}
#endif

}  // namespace

upscale::ModelRunner upscaler_model_runner(const fs::path& tool, const fs::path& models) {
    return [tool, models](const std::string& name, const fs::path& input, const fs::path& output, std::string& error) {
        std::error_code ec;
        if (!fs::is_regular_file(tool, ec)) {
            error = "no upscaler at " + utf8_of(tool);
            return false;
        }
        if (!fs::is_regular_file(models / (name + ".param"), ec) || !fs::is_regular_file(models / (name + ".bin"), ec)) {
            error = "no model " + name + " in " + utf8_of(models);
            return false;
        }
        std::vector<std::string> tail;
        const long code = run_process(tool, {"-i", utf8_of(input), "-o", utf8_of(output), "-m", utf8_of(models), "-n", name, "-s", "4", "-f", "png"},
                                      tool.parent_path(), {}, [&tail](const std::string& line) {
                                          if (!line.empty() && line.find('%') == std::string::npos) {   // not its per-file progress
                                              tail.push_back(line);
                                              if (tail.size() > 6) {
                                                  tail.erase(tail.begin());
                                              }
                                          }
                                      },
                                      nullptr, error);
        if (code != 0) {
            std::string said;
            for (const std::string& line : tail) {
                said += " | " + line;
            }
            const bool vulkan = said.find("vk") != std::string::npos || said.find("Vulkan") != std::string::npos || said.find("gpu") != std::string::npos ||
                                said.find("GPU") != std::string::npos;
            error = vulkan ? "the upscaler found no graphics card it can use (it needs a GPU with Vulkan: NVIDIA, AMD or Intel, with a current driver)" + said
                           : (error.empty() ? "the upscaler exited with " + std::to_string(code) : error) + said;
            return false;
        }
        return true;
    };
}

std::string upscaler_stamp_name(const fs::path& tool) {
    std::error_code ec;
    return utf8_of(tool.filename()) + " " + std::to_string(fs::is_regular_file(tool, ec) ? fs::file_size(tool, ec) : 0);
}

bool run_upscale_task(const opentony::layout::Layout& where, const fs::path& game, const upscale::Options& given, int jobs,
                      const std::function<void(const std::string&)>& log, UpscaleProgress& progress, std::string& error, UpscaleSteps steps) {
    std::error_code ec;
    fs::create_directories(where.logs(), ec);
    std::ofstream log_file(where.logs() / "upscale.txt", std::ios::app);
    std::mutex log_mutex;
    const auto say = [&](const std::string& line) {
        std::lock_guard<std::mutex> lock(log_mutex);
        char when[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        log_file << when << " " << line << "\n";
        log_file.flush();
        log(line);
    };
    const auto start = std::chrono::steady_clock::now();
    if (steps.download && !download_all(where, given, say, progress, error)) {
        say("stopped: " + error);
        return false;
    }
    if (steps.census) {
#ifdef __linux__
        if (!scan_pc_bitmaps(where, say, progress, error)) {
            say("stopped: " + error);
            return false;
        }
#else
        const us::State before = us::state(where.upscaler(), where.texture_dump(), where.upscaled(), given);
        if (before.census) {
            say("census: done already (" + std::to_string(before.dumped) + " textures in " + utf8_of(where.texture_dump()) + ")");
        } else if (!run_census(where, game, jobs, say, progress, error)) {
            say("stopped: " + error);
            return false;
        }
#endif
    }
    if (!steps.upscale) {
        return true;
    }
    progress.phase = 3;
    progress.done = 0;
    progress.total = 0;
#ifdef __linux__
    const fs::path dump = where.true_colour();
    const fs::path table = where.upscaler() / "pc_bitmap_classes.tsv";
#else
    const fs::path dump = where.texture_dump();
    const fs::path table = where.upscaler() / "texture_classes.tsv";
    if (!us::write_class_table(table, error)) {
        say("stopped: " + error);
        return false;
    }
#endif
    // the PC port's 24-bit bitmaps in all 8 bits (graphics-2 G5): the originals the model starts from where there is one, so the 15-bit banding is not upscaled
    upscale::Options options = given;
#ifdef __linux__
    // The model runs a whole batch before the pipeline can publish files or handle cancellation.
    options.batch = std::min(options.batch, 16);
#endif
    std::error_code colour_ec;
    if (options.originals.empty() && fs::is_directory(where.package_data() / "newtex", colour_ec)) {
        if (!true_colour::is_current(where.package_data(), where.true_colour())) {
            say("true colour: reading the 24-bit bitmaps of " + utf8_of(where.package_data()));
            const true_colour::Result made = true_colour::build(where.package_data(), where.true_colour());
            say("true colour: " + std::to_string(made.written) + " written, " + std::to_string(made.unchanged) + " there already, " + std::to_string(made.failed) + " failed");
        }
        if (true_colour::is_current(where.package_data(), where.true_colour())) {
            options.originals = where.true_colour();
        }
    }
    const fs::path tool = us::tool_program(where.upscaler());
    say("upscale: world " + options.model_world + ", 2d " + options.model_2d + ", scale " + std::to_string(options.scale) + " into " + utf8_of(where.upscaled()));
    const upscale::Report report = upscale::run(dump, table, where.upscaled(), options, upscaler_stamp_name(tool),
                                                upscaler_model_runner(tool, where.upscaler() / "models"), say, &progress.cancel, [&progress](int done, int total) {
                                                    progress.total = static_cast<std::uint64_t>(total);
                                                    progress.done = static_cast<std::uint64_t>(done);
                                                });
    for (const std::string& e : report.errors) {
        say("error: " + e);
    }
    const int seconds = static_cast<int>(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
    if (report.cancelled) {
        error = "Cancelled. The textures upscaled so far are kept; Start continues where it stopped.";
        say("stopped: " + error);
        return false;
    }
    if (report.failed != 0) {
        error = std::to_string(report.failed) + " textures could not be upscaled" + (report.errors.empty() ? std::string() : ": " + report.errors.front());
        say("finished with errors after " + std::to_string(seconds) + " s: " + error);
        return false;
    }
    if (report.written + report.done_before == 0) {
        error = "No matching textures were upscaled.";
        say("stopped: " + error);
        return false;
    }
    say("finished in " + std::to_string(seconds / 60) + " min " + std::to_string(seconds % 60) + " s: " + std::to_string(report.written) + " written, " +
        std::to_string(report.done_before) + " there already, " + megabytes(report.bytes) + " new");
    return true;
}

}  // namespace thpsource2
