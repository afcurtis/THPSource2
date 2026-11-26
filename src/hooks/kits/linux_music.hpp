#pragma once

#include "../../hal/audio.h"
#include "../../hal/ffmpeg/movie_source.h"

#include <filesystem>
#include <string_view>

namespace opentony::kitdll {

class LinuxMusic {
public:
    explicit LinuxMusic(const std::filesystem::path& game_dir);
    ~LinuxMusic();
    bool open(std::string_view name);
    bool start();
    void close();
    bool pump();
    void pause(bool enabled);
    void volume(std::int32_t value);

private:
    std::filesystem::path folder_;
    std::unique_ptr<hal::MovieSource> source_;
    OtHalHandle stream_ = OT_HAL_INVALID_HANDLE;
    bool ended_ = false;
    bool paused_ = false;
};

}  // namespace opentony::kitdll
