// THPSource2 launcher: the "Upscale textures" button's work (texture-upscaling plan T5; docs/LAUNCHER.md, "Upscaled textures"), on the task thread: download the
// tool and the model (curl.exe and tar.exe of Windows 10 and later), the full texture census (the shipped recipe, every run headless with no window, side by
// side), the merge of the runs' dumps, then the pipeline (core/upscale.cpp) into Data\Upscaled. Cancel stops between files, runs and batches and keeps what is done.
#pragma once

#include "launcher/layout.hpp"
#include "upscale.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace thpsource2 {

struct UpscaleProgress {
    std::atomic<int> phase{0};               // 1 download, 2 census, 3 upscale
    std::atomic<std::uint64_t> done{0};      // of the phase: bytes, census runs, textures upscaled (or failed) of this run
    std::atomic<std::uint64_t> total{0};
    std::atomic<bool> cancel{false};
};

// The model runner the pipeline takes: `tool` over a folder with a model of `models`. A tool that finds no Vulkan device is said in plain words.
upscale::ModelRunner upscaler_model_runner(const std::filesystem::path& tool, const std::filesystem::path& models);
// The stamp's name for the tool: its file name and size (a new release of it is another stamp)
std::string upscaler_stamp_name(const std::filesystem::path& tool);


struct UpscaleSteps {
    bool download = true;
    bool census = true;
    bool upscale = true;
};

// The whole job (or the steps asked for: the command line's --download-upscaler and --texture-census). `game` is the game folder (THawk2.exe, ALL.PKR); `jobs`
// census runs go side by side. False with `error` when it failed or was cancelled.
bool run_upscale_task(const opentony::layout::Layout& where, const std::filesystem::path& game, const upscale::Options& options, int jobs,
                      const std::function<void(const std::string&)>& log, UpscaleProgress& progress, std::string& error, UpscaleSteps steps = {});

}  // namespace thpsource2
