#include "movie_source.h"

#include "video.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

namespace opentony::hal {
namespace {

#define OT_STR2(x) #x
#define OT_STR(x) OT_STR2(x)

// ---- the libraries, found and bound at run time ----------------------------------------------------------------------

struct Api {
    HMODULE util = nullptr;
    HMODULE codec = nullptr;
    HMODULE format = nullptr;

    // avutil
    AVFrame* (*frame_alloc)() = nullptr;
    void (*frame_free)(AVFrame**) = nullptr;
    void (*frame_unref)(AVFrame*) = nullptr;
    void (*log_set_level)(int) = nullptr;
    // avcodec
    const AVCodec* (*find_decoder)(enum AVCodecID) = nullptr;
    AVCodecContext* (*alloc_context)(const AVCodec*) = nullptr;
    int (*parameters_to_context)(AVCodecContext*, const AVCodecParameters*) = nullptr;
    int (*open_codec)(AVCodecContext*, const AVCodec*, AVDictionary**) = nullptr;
    int (*send_packet)(AVCodecContext*, const AVPacket*) = nullptr;
    int (*receive_frame)(AVCodecContext*, AVFrame*) = nullptr;
    void (*free_context)(AVCodecContext**) = nullptr;
    void (*flush_buffers)(AVCodecContext*) = nullptr;
    AVPacket* (*packet_alloc)() = nullptr;
    void (*packet_free)(AVPacket**) = nullptr;
    void (*packet_unref)(AVPacket*) = nullptr;
    // avformat
    int (*open_input)(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**) = nullptr;
    int (*find_stream_info)(AVFormatContext*, AVDictionary**) = nullptr;
    int (*read_frame)(AVFormatContext*, AVPacket*) = nullptr;
    int (*seek_frame)(AVFormatContext*, int, int64_t, int) = nullptr;
    void (*close_input)(AVFormatContext**) = nullptr;
};

Api g_api;
bool g_tried = false;
std::string g_detail;

std::string module_directory() {
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&module_directory), &self)) {
        return {};
    }
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(self, path, MAX_PATH);
    std::string text(path, length);
    const std::size_t slash = text.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : text.substr(0, slash + 1);
}

HMODULE load_library(const std::string& directories_note, const std::string& name, std::string& tried) {
    (void)directories_note;
    std::vector<std::string> places;
    char environment[MAX_PATH] = {};
    if (GetEnvironmentVariableA("OPENTONY_FFMPEG_DIR", environment, MAX_PATH) != 0) {
        std::string directory = environment;
        if (!directory.empty() && directory.back() != '\\' && directory.back() != '/') directory += '\\';
        places.push_back(directory);
    }
    places.push_back(module_directory());
    places.push_back(std::string());
    for (const std::string& place : places) {
        const std::string full = place + name;
        tried += (tried.empty() ? "" : "; ") + (place.empty() ? name + " (search path)" : full);
        if (HMODULE module = LoadLibraryExA(full.c_str(), nullptr, place.empty() ? 0 : LOAD_WITH_ALTERED_SEARCH_PATH)) {
            return module;
        }
    }
    return nullptr;
}

template <typename T>
bool bind(HMODULE module, const char* name, T& target, std::string& missing) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    if (target == nullptr) {
        missing += std::string(missing.empty() ? "" : ", ") + name;
        return false;
    }
    return true;
}

bool load_api() {
    if (g_tried) return g_api.format != nullptr;
    g_tried = true;
    std::string tried;
    // the file names carry the major versions of the headers this was built with: a build of another major is refused here
    g_api.util = load_library("", "avutil-" OT_STR(LIBAVUTIL_VERSION_MAJOR) ".dll", tried);
    g_api.codec = g_api.util != nullptr ? load_library("", "avcodec-" OT_STR(LIBAVCODEC_VERSION_MAJOR) ".dll", tried) : nullptr;
    g_api.format = g_api.codec != nullptr ? load_library("", "avformat-" OT_STR(LIBAVFORMAT_VERSION_MAJOR) ".dll", tried) : nullptr;
    if (g_api.format == nullptr) {
        g_detail = "FFmpeg " OT_STR(LIBAVFORMAT_VERSION_MAJOR) " libraries did not load (tried " + tried + ")";
        g_api = Api();
        return false;
    }
    std::string missing;
    bool ok = true;
    ok &= bind(g_api.util, "av_frame_alloc", g_api.frame_alloc, missing);
    ok &= bind(g_api.util, "av_frame_free", g_api.frame_free, missing);
    ok &= bind(g_api.util, "av_frame_unref", g_api.frame_unref, missing);
    ok &= bind(g_api.util, "av_log_set_level", g_api.log_set_level, missing);
    ok &= bind(g_api.codec, "avcodec_find_decoder", g_api.find_decoder, missing);
    ok &= bind(g_api.codec, "avcodec_alloc_context3", g_api.alloc_context, missing);
    ok &= bind(g_api.codec, "avcodec_parameters_to_context", g_api.parameters_to_context, missing);
    ok &= bind(g_api.codec, "avcodec_open2", g_api.open_codec, missing);
    ok &= bind(g_api.codec, "avcodec_send_packet", g_api.send_packet, missing);
    ok &= bind(g_api.codec, "avcodec_receive_frame", g_api.receive_frame, missing);
    ok &= bind(g_api.codec, "avcodec_free_context", g_api.free_context, missing);
    ok &= bind(g_api.codec, "avcodec_flush_buffers", g_api.flush_buffers, missing);
    ok &= bind(g_api.codec, "av_packet_alloc", g_api.packet_alloc, missing);
    ok &= bind(g_api.codec, "av_packet_free", g_api.packet_free, missing);
    ok &= bind(g_api.codec, "av_packet_unref", g_api.packet_unref, missing);
    ok &= bind(g_api.format, "avformat_open_input", g_api.open_input, missing);
    ok &= bind(g_api.format, "avformat_find_stream_info", g_api.find_stream_info, missing);
    ok &= bind(g_api.format, "av_read_frame", g_api.read_frame, missing);
    ok &= bind(g_api.format, "av_seek_frame", g_api.seek_frame, missing);
    ok &= bind(g_api.format, "avformat_close_input", g_api.close_input, missing);
    if (!ok) {
        g_detail = "the FFmpeg libraries lack: " + missing;
        g_api = Api();
        return false;
    }
    g_api.log_set_level(AV_LOG_ERROR);
    g_detail = "loaded from " + tried.substr(tried.rfind("; ") == std::string::npos ? 0 : tried.rfind("; ") + 2);
    return true;
}

// float [-1, 1) to 16 bits
std::int16_t to_pcm16(float value) {
    return static_cast<std::int16_t>(std::lrintf(std::clamp(value * 32768.0f, -32768.0f, 32767.0f)));
}

// ---- the source ------------------------------------------------------------------------------------------------------

struct StoredFrame {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::vector<std::uint8_t> y, u, v, alpha;
    std::int32_t stride_y = 0, stride_uv = 0, stride_alpha = 0;
};

class FfmpegSource final : public MovieSource {
public:
    ~FfmpegSource() override {
        if (packet_) g_api.packet_free(&packet_);
        if (frame_) g_api.frame_free(&frame_);
        if (video_context_) g_api.free_context(&video_context_);
        if (audio_context_) g_api.free_context(&audio_context_);
        if (format_) g_api.close_input(&format_);
    }

    bool init(const std::string& path, bool want_video, bool want_audio, std::string& error) {
        if (g_api.open_input(&format_, path.c_str(), nullptr, nullptr) < 0) {
            error = "cannot open " + path;
            return false;
        }
        if (g_api.find_stream_info(format_, nullptr) < 0) {
            error = "no stream information in " + path;
            return false;
        }
        int picture_stream = -1;  // the video stream, whether or not it is decoded: its length is the length of the sound
        for (unsigned index = 0; index < format_->nb_streams; ++index) {
            const AVCodecParameters* parameters = format_->streams[index]->codecpar;
            if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && picture_stream < 0) {
                picture_stream = static_cast<int>(index);
                if (want_video) video_stream_ = picture_stream;
            } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO && want_audio && audio_stream_ < 0) {
                audio_stream_ = static_cast<int>(index);
            }
        }
        if (video_stream_ < 0 && audio_stream_ < 0) {
            error = path + " has no " + std::string(want_video ? "video" : "") + (want_video && want_audio ? " or " : "") +
                    (want_audio ? "audio" : "") + " stream";
            return false;
        }
        if (video_stream_ >= 0 && !open_decoder(video_stream_, video_context_, error)) return false;
        if (audio_stream_ >= 0 && !open_decoder(audio_stream_, audio_context_, error)) return false;
        if (picture_stream >= 0) {
            const AVStream* stream = format_->streams[picture_stream];
            info_.has_video = video_stream_ >= 0;
            info_.width = stream->codecpar->width;
            info_.height = stream->codecpar->height;
            info_.fps_num = stream->avg_frame_rate.num;
            info_.fps_den = stream->avg_frame_rate.den != 0 ? stream->avg_frame_rate.den : 1;
            info_.frames = static_cast<std::int32_t>(stream->nb_frames);
            if (info_.frames == 0 && stream->duration > 0 && info_.fps_num > 0) {
                // the Bink demuxer sets no frame count; its duration is the count in time-base units (1 / frame rate)
                info_.frames = static_cast<std::int32_t>((stream->duration * stream->time_base.num * info_.fps_num +
                                                          static_cast<std::int64_t>(stream->time_base.den) * info_.fps_den / 2) /
                                                         (static_cast<std::int64_t>(stream->time_base.den) * info_.fps_den));
            }
        }
        if (audio_stream_ >= 0) {
            const AVCodecParameters* parameters = format_->streams[audio_stream_]->codecpar;
            info_.has_audio = true;
            // the stream's rate is the container's: the RDFT decoder doubles the rate of its context (it counts every channel's samples)
            info_.audio_rate = parameters->sample_rate;
            info_.audio_channels = audio_context_->ch_layout.nb_channels > 0 ? audio_context_->ch_layout.nb_channels
                                                                           : parameters->ch_layout.nb_channels;
            // Bink plays the sound for as long as the picture lasts (frames / frame rate) and no longer: the last audio block
            // usually runs past that, and binkw32.dll leaves the excess out (measured: its output is silent from exactly there)
            if (info_.frames > 0 && info_.fps_num > 0) {
                audio_limit_ = static_cast<std::int64_t>(info_.frames) * info_.audio_rate * info_.fps_den / info_.fps_num;
            }
        }
        packet_ = g_api.packet_alloc();
        frame_ = g_api.frame_alloc();
        return packet_ != nullptr && frame_ != nullptr;
    }

    const MovieInfo& info() const override { return info_; }

    bool next_video(VideoFrame& out) override {
        while (video_queue_.empty() && pump()) {
        }
        if (video_queue_.empty()) return false;
        current_ = std::move(video_queue_.front());
        video_queue_.pop_front();
        out.width = current_.width;
        out.height = current_.height;
        out.y = current_.y.data();
        out.u = current_.u.data();
        out.v = current_.v.data();
        out.alpha = current_.alpha.empty() ? nullptr : current_.alpha.data();
        out.stride_y = current_.stride_y;
        out.stride_uv = current_.stride_uv;
        out.stride_alpha = current_.stride_alpha;
        return true;
    }

    std::size_t read_audio(std::int16_t* out, std::size_t frames) override {
        const std::size_t channels = static_cast<std::size_t>(std::max(info_.audio_channels, 1));
        std::vector<float> floats(frames * channels);
        const std::size_t got = read_audio_float(floats.data(), frames);
        for (std::size_t i = 0; i < got * channels; ++i) out[i] = to_pcm16(floats[i]);
        return got;
    }

    std::size_t read_audio_float(float* out, std::size_t frames) override {
        const std::size_t channels = static_cast<std::size_t>(std::max(info_.audio_channels, 1));
        while (audio_queue_.size() < frames * channels && pump()) {
        }
        std::size_t available = std::min(audio_queue_.size() / channels, frames);
        if (audio_limit_ >= 0) {
            available = std::min<std::size_t>(available, static_cast<std::size_t>(std::max<std::int64_t>(audio_limit_ - audio_delivered_, 0)));
        }
        std::copy_n(audio_queue_.begin(), available * channels, out);
        audio_queue_.erase(audio_queue_.begin(), audio_queue_.begin() + static_cast<std::ptrdiff_t>(available * channels));
        audio_delivered_ += static_cast<std::int64_t>(available);
        if (audio_limit_ >= 0 && audio_delivered_ >= audio_limit_) audio_queue_.clear();
        return available;
    }

    bool rewind() override {
        if (g_api.seek_frame(format_, -1, 0, AVSEEK_FLAG_BACKWARD) < 0) return false;
        if (video_context_) g_api.flush_buffers(video_context_);
        if (audio_context_) g_api.flush_buffers(audio_context_);
        video_queue_.clear();
        audio_queue_.clear();
        audio_delivered_ = 0;
        ended_ = false;
        return true;
    }

private:
    bool open_decoder(int stream, AVCodecContext*& context, std::string& error) {
        const AVCodecParameters* parameters = format_->streams[stream]->codecpar;
        const AVCodec* codec = g_api.find_decoder(parameters->codec_id);
        if (codec == nullptr) {
            error = "this FFmpeg build has no decoder for the stream";
            return false;
        }
        context = g_api.alloc_context(codec);
        if (context == nullptr || g_api.parameters_to_context(context, parameters) < 0 ||
            g_api.open_codec(context, codec, nullptr) < 0) {
            error = "cannot open the stream's decoder";
            return false;
        }
        return true;
    }

    // Reads and decodes one packet into the queues; false once the file and the decoders are drained.
    bool pump() {
        if (ended_) return false;
        const int read = g_api.read_frame(format_, packet_);
        if (read < 0) {
            ended_ = true;
            if (video_context_) g_api.send_packet(video_context_, nullptr);
            if (audio_context_) g_api.send_packet(audio_context_, nullptr);
            drain(video_context_, true);
            drain(audio_context_, false);
            return !video_queue_.empty() || !audio_queue_.empty();
        }
        if (packet_->stream_index == video_stream_) {
            g_api.send_packet(video_context_, packet_);
            drain(video_context_, true);
        } else if (packet_->stream_index == audio_stream_) {
            g_api.send_packet(audio_context_, packet_);
            drain(audio_context_, false);
        }
        g_api.packet_unref(packet_);
        return true;
    }

    void drain(AVCodecContext* context, bool video) {
        if (context == nullptr) return;
        while (g_api.receive_frame(context, frame_) >= 0) {
            if (video) {
                store_video(*frame_);
            } else {
                store_audio(*frame_);
            }
            g_api.frame_unref(frame_);
        }
    }

    static void copy_plane(std::vector<std::uint8_t>& target, const std::uint8_t* source, int stride, int width, int height) {
        target.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
        for (int row = 0; row < height; ++row) {
            std::memcpy(target.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                        source + static_cast<std::ptrdiff_t>(row) * stride, static_cast<std::size_t>(width));
        }
    }

    void store_video(const AVFrame& frame) {
        StoredFrame stored;
        stored.width = frame.width;
        stored.height = frame.height;
        const int chroma_width = (frame.width + 1) / 2;
        const int chroma_height = (frame.height + 1) / 2;
        copy_plane(stored.y, frame.data[0], frame.linesize[0], frame.width, frame.height);
        copy_plane(stored.u, frame.data[1], frame.linesize[1], chroma_width, chroma_height);
        copy_plane(stored.v, frame.data[2], frame.linesize[2], chroma_width, chroma_height);
        stored.stride_y = frame.width;
        stored.stride_uv = chroma_width;
        if (frame.format == AV_PIX_FMT_YUVA420P && frame.data[3] != nullptr) {
            copy_plane(stored.alpha, frame.data[3], frame.linesize[3], frame.width, frame.height);
            stored.stride_alpha = frame.width;
        }
        video_queue_.push_back(std::move(stored));
    }

    // Bink audio decodes to float: planar for the DCT variant, interleaved for the RDFT one. The HAL's streams take 16-bit PCM.
    void store_audio(const AVFrame& frame) {
        const int channels = std::max(info_.audio_channels, 1);
        const std::size_t base = audio_queue_.size();
        audio_queue_.resize(base + static_cast<std::size_t>(frame.nb_samples) * static_cast<std::size_t>(channels));
        const bool interleaved = frame.format == AV_SAMPLE_FMT_FLT;
        for (int channel = 0; channel < channels; ++channel) {
            const float* plane = reinterpret_cast<const float*>(frame.extended_data[interleaved ? 0 : channel]);
            if (plane == nullptr) continue;
            for (int sample = 0; sample < frame.nb_samples; ++sample) {
                const float value = interleaved ? plane[static_cast<std::size_t>(sample) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)]
                                                : plane[sample];
                audio_queue_[base + static_cast<std::size_t>(sample) * static_cast<std::size_t>(channels) + static_cast<std::size_t>(channel)] = value;
            }
        }
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* video_context_ = nullptr;
    AVCodecContext* audio_context_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    int video_stream_ = -1;
    int audio_stream_ = -1;
    bool ended_ = false;
    std::int64_t audio_limit_ = -1;      // sample frames Bink plays; -1: no limit
    std::int64_t audio_delivered_ = 0;
    MovieInfo info_;
    std::deque<StoredFrame> video_queue_;
    StoredFrame current_;
    std::deque<float> audio_queue_;
};

}  // namespace

bool MovieSource::available(std::string& detail) {
    const bool ok = load_api();
    detail = g_detail;
    return ok;
}

std::unique_ptr<MovieSource> MovieSource::open(const std::string& path, bool want_video, bool want_audio, std::string& error) {
    if (!load_api()) {
        error = g_detail;
        return nullptr;
    }
    auto source = std::make_unique<FfmpegSource>();
    if (!source->init(path, want_video, want_audio, error)) return nullptr;
    return source;
}

// ---- pixels ----------------------------------------------------------------------------------------------------------

namespace {

std::uint8_t clamp8(int value) { return static_cast<std::uint8_t>(std::clamp(value, 0, 255)); }

// Bink's own YUV to RGB, recovered from binkw32.dll's output (docs/HAL.md "Movie"): not a matrix product but four integer tables, summed
// and clamped. R = F[y] + Cr[v], B = F[y] + Cb[u], G = F[y] - Gu[u] - Gv[v], each table entry the truncation toward zero of a constant
// times the offset (luma 1.1643835 * (max(y, 16) - 16); chroma 1.1384 * {1.402, 1.772, 0.344136, 0.714136} * (c - 128)), and the chroma is
// replicated, not interpolated. Measured to give Bink's pixels exactly: 0 differences in 691,200 pixels of 9 frames of 3 movies, and
// the whole of every movie in tests/test_movie_compare.py.
struct ConversionTables {
    std::int32_t luma[256];
    std::int32_t red[256];
    std::int32_t blue[256];
    std::int32_t green_u[256];
    std::int32_t green_v[256];
    ConversionTables() {
        for (int i = 0; i < 256; ++i) {
            luma[i] = static_cast<std::int32_t>(1.1643835 * (std::max(i, 16) - 16));
            red[i] = static_cast<std::int32_t>(1.402 * 1.1384 * (i - 128));
            blue[i] = static_cast<std::int32_t>(1.772 * 1.1384 * (i - 128));
            green_u[i] = static_cast<std::int32_t>(0.344136 * 1.1384 * (i - 128));
            green_v[i] = static_cast<std::int32_t>(0.714136 * 1.1384 * (i - 128));
        }
    }
};

void yuv_to_rgb(int y, int u, int v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    static const ConversionTables tables;
    r = clamp8(tables.luma[y] + tables.red[v]);
    g = clamp8(tables.luma[y] - tables.green_u[u] - tables.green_v[v]);
    b = clamp8(tables.luma[y] + tables.blue[u]);
}

}  // namespace

bool convert_to_pixels(const VideoFrame& frame, std::uint32_t pixel_format, void* pixels, std::int32_t pitch) {
    if (pixel_format != OT_HAL_PIXEL_ARGB8888 && pixel_format != OT_HAL_PIXEL_RGB565) return false;
    for (std::int32_t row = 0; row < frame.height; ++row) {
        std::uint8_t* line = static_cast<std::uint8_t*>(pixels) + static_cast<std::ptrdiff_t>(row) * pitch;
        for (std::int32_t column = 0; column < frame.width; ++column) {
            const int y = frame.y[row * frame.stride_y + column];
            const int u = frame.u[(row / 2) * frame.stride_uv + column / 2];
            const int v = frame.v[(row / 2) * frame.stride_uv + column / 2];
            std::uint8_t r, g, b;
            yuv_to_rgb(y, u, v, r, g, b);
            if (pixel_format == OT_HAL_PIXEL_ARGB8888) {
                const std::uint8_t a = frame.alpha != nullptr ? frame.alpha[row * frame.stride_alpha + column] : 255;
                const std::uint32_t value = (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
                std::memcpy(line + column * 4, &value, 4);
            } else {
                const std::uint16_t value = static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                std::memcpy(line + column * 2, &value, 2);
            }
        }
    }
    return true;
}

}  // namespace opentony::hal
