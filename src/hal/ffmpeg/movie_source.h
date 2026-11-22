#ifndef OPENTONY_HAL_FFMPEG_MOVIE_SOURCE_H
#define OPENTONY_HAL_FFMPEG_MOVIE_SOURCE_H

// The movie and music decoder of the HAL (Phase 5.3b, docs/HAL.md "Movie"): the game's Bink files (`Movies/*.dat`,
// `Music/LTIX*.dat`, magic "BIKi") decoded by FFmpeg instead of binkw32.dll.
//
// FFmpeg is the pinned, decode-only, LGPL build of re/config/ffmpeg.yml (the Bink demuxer and the three Bink decoders). It is
// loaded at run time from DLLs (avutil, avcodec, avformat: `OPENTONY_FFMPEG_DIR`, else the directory of the module that holds
// this code, else the default search path), so a player can swap in a build of their own with the same major versions, and a
// program that never opens a movie never needs them. `available()` says whether they load.
//
// A source produces what the HAL movie service (src/hal/movie.h) consumes: planar YUV frames that `convert_to_pixels` turns
// into the frame target's format, and interleaved signed 16-bit PCM for an audio stream (src/hal/audio.h).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace opentony::hal {

struct MovieInfo {
    bool has_video = false;
    bool has_audio = false;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t frames = 0;  // video frames in the file (the container's count)
    std::int32_t fps_num = 0;
    std::int32_t fps_den = 1;
    std::int32_t audio_rate = 0;
    std::int32_t audio_channels = 0;
};

// One decoded picture, planar 4:2:0 (the format Bink decodes to). `alpha` is null unless the file carries one.
struct VideoFrame {
    std::int32_t width = 0;
    std::int32_t height = 0;
    const std::uint8_t* y = nullptr;
    const std::uint8_t* u = nullptr;
    const std::uint8_t* v = nullptr;
    const std::uint8_t* alpha = nullptr;
    std::int32_t stride_y = 0;
    std::int32_t stride_uv = 0;
    std::int32_t stride_alpha = 0;
};

class MovieSource {
public:
    virtual ~MovieSource() = default;

    // Opens a Bink file. `want_video` / `want_audio` choose which streams are decoded; the other is skipped, so a music file read
    // for its sound never decodes its picture. Returns null and fills `error` when the libraries do not load, the file does not
    // open or it has none of the wanted streams.
    static std::unique_ptr<MovieSource> open(const std::string& path, bool want_video, bool want_audio, std::string& error);

    // True when the FFmpeg DLLs load; otherwise `detail` says what was tried.
    static bool available(std::string& detail);

    virtual const MovieInfo& info() const = 0;

    // The next picture, or false at the end of the file. The planes stay valid until the next call.
    virtual bool next_video(VideoFrame& frame) = 0;

    // Up to `frames` interleaved sample frames (`audio_channels` 16-bit values each). Returns how many were written; 0 at the end.
    virtual std::size_t read_audio(std::int16_t* out, std::size_t frames) = 0;

    // The same sound as 32-bit float in [-1, 1) (what the decoder produced, before the conversion to 16 bits).
    virtual std::size_t read_audio_float(float* out, std::size_t frames) = 0;

    // Back to the first frame and sample (a looping clip or track).
    virtual bool rewind() = 0;
};

// Writes `frame` into `pixels` (`pitch` bytes per row, `frame.height` rows) as an OtHalPixelFormat (src/hal/video.h):
// ARGB8888 or RGB565. Returns false for a format the movie service does not use.
bool convert_to_pixels(const VideoFrame& frame, std::uint32_t pixel_format, void* pixels, std::int32_t pitch);

}  // namespace opentony::hal

#endif
