#include "linux_music.hpp"

#include "../../hal/sdl/sdl_backends.h"
#include "../../launcher/thpsource2/core/ini.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace opentony::kitdll {
namespace {

const auto& audio = ot_hal_sdl_audio;

}  // namespace

LinuxMusic::LinuxMusic(const std::filesystem::path& game_dir) : folder_(game_dir / "Music") {}
LinuxMusic::~LinuxMusic() { close(); }

bool LinuxMusic::open(std::string_view name) {
    close();
    std::error_code ec;
    auto path = folder_ / std::filesystem::path(name).filename();
    if (!std::filesystem::is_regular_file(path, ec)) {
        for (std::filesystem::directory_iterator it(folder_, ec), end; !ec && it != end; it.increment(ec)) {
            if (thpsource2::equal_nocase(it->path().filename().string(), name)) {
                path = it->path();
                break;
            }
        }
    }
    std::string error;
    source_ = hal::MovieSource::open(path.string(), false, true, error);
    if (!source_) {
        std::fprintf(stderr, "music: %s\n", error.c_str());
        return false;
    }
    std::printf("music: opened %s\n", path.filename().c_str());
    return true;
}

bool LinuxMusic::start() {
    if (!source_) {
        return false;
    }
    const auto& info = source_->info();
    if (info.audio_channels < 1 || info.audio_channels > 2) {
        return false;
    }
    const OtHalPcmFormat pcm{static_cast<std::uint32_t>(info.audio_rate), static_cast<std::uint16_t>(info.audio_channels), 16};
    stream_ = audio.stream_open(&pcm);
    return stream_ != OT_HAL_INVALID_HANDLE && pump();
}

void LinuxMusic::close() {
    if (stream_ != OT_HAL_INVALID_HANDLE) {
        audio.stream_close(stream_);
    }
    stream_ = OT_HAL_INVALID_HANDLE;
    source_.reset();
    ended_ = false;
    paused_ = false;
}

bool LinuxMusic::pump() {
    if (stream_ == OT_HAL_INVALID_HANDLE) {
        return false;
    }
    if (paused_) {
        return true;
    }
    const auto& info = source_->info();
    const auto frame_bytes = static_cast<std::uint32_t>(info.audio_channels) * 2;
    const auto target = static_cast<std::uint32_t>(info.audio_rate) * frame_bytes / 2;
    std::array<std::int16_t, 2048> samples;
    while (!ended_ && audio.stream_queued(stream_) < target) {
        const auto frames = source_->read_audio(samples.data(), 1024);
        if (frames == 0) {
            ended_ = true;
            break;
        }
        const auto bytes = static_cast<std::uint32_t>(frames) * frame_bytes;
        if (audio.stream_write(stream_, samples.data(), bytes) != bytes) {
            ended_ = true;
            break;
        }
    }
    if (ended_ && audio.stream_queued(stream_) == 0) {
        close();
        return false;
    }
    return true;
}

void LinuxMusic::pause(bool enabled) {
    if (stream_ != OT_HAL_INVALID_HANDLE) {
        paused_ = enabled;
        audio.stream_pause(stream_, enabled);
    }
}

void LinuxMusic::volume(std::int32_t value) {
    if (stream_ != OT_HAL_INVALID_HANDLE) {
        const auto mb = value <= 0 ? OT_HAL_VOLUME_SILENT : value >= 32768 ? 0 :
            static_cast<std::int32_t>(std::lround(2000.0 * std::log10(value / 32768.0)));
        audio.stream_set_volume(stream_, mb);
    }
}

}  // namespace opentony::kitdll
