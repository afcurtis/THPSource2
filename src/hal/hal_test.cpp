#include "audio.h"
#include "clock.h"
#include "hal.h"
#include "input.h"
#include "movie.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
    if (!condition) {
        std::fprintf(stderr, "hal_test.cpp:%d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

// A fake monotonic clock: sleeping advances it exactly; `work_ns` is what the
// game spends between deliveries.
uint64_t fake_now = 0;
std::vector<uint64_t> fake_sleeps;

uint64_t OT_HAL_CALL fake_now_ns() {
    return fake_now;
}

void OT_HAL_CALL fake_sleep_ns(uint64_t ns) {
    fake_sleeps.push_back(ns);
    fake_now += ns;
}

const OtHalClockBackend kFakeClock = {"fake", nullptr, nullptr, fake_now_ns, fake_sleep_ns};

void reset_fake(uint64_t now) {
    fake_now = now;
    fake_sleeps.clear();
}

void test_service_names() {
    CHECK(std::strcmp(ot_hal_service_name(OT_HAL_INPUT), "input") == 0);
    CHECK(ot_hal_service_name(OT_HAL_SERVICE_COUNT) == nullptr);
    CHECK(ot_hal_service_parse("Clock", 5) == OT_HAL_CLOCK);
    CHECK(ot_hal_service_parse("movie=sdl", 5) == OT_HAL_MOVIE);
    CHECK(ot_hal_service_parse("fs", 2) == -1);
}

void test_paced_deliveries_sleep_to_due_time() {
    OtHalClockPacer pacer;
    reset_fake(5000000000ull);
    ot_hal_clock_pacer_start(&pacer, &kFakeClock, OT_HAL_PACE_REALTIME);
    fake_now += 4000000; // 4 ms of game work
    ot_hal_clock_pacer_tick(&pacer, 16);
    CHECK(fake_sleeps.size() == 1 && fake_sleeps[0] == 12000000);
    CHECK(fake_now == 5000000000ull + 16000000);
    ot_hal_clock_pacer_tick(&pacer, 16); // no work: a full interval
    CHECK(fake_sleeps.size() == 2 && fake_sleeps[1] == 16000000);
}

void test_small_lead_does_not_sleep() {
    OtHalClockPacer pacer;
    reset_fake(0);
    ot_hal_clock_pacer_start(&pacer, &kFakeClock, OT_HAL_PACE_REALTIME);
    fake_now += 15500000; // due in 0.5 ms: within the slack
    ot_hal_clock_pacer_tick(&pacer, 16);
    CHECK(fake_sleeps.empty());
}

void test_falling_behind_resyncs_without_sprinting() {
    OtHalClockPacer pacer;
    reset_fake(0);
    ot_hal_clock_pacer_start(&pacer, &kFakeClock, OT_HAL_PACE_REALTIME);
    fake_now += 100000000; // 100 ms behind: catch up, no sleep, no resync
    ot_hal_clock_pacer_tick(&pacer, 16);
    CHECK(fake_sleeps.empty() && pacer.resyncs == 0);
    fake_now += 400000000; // a loading screen
    ot_hal_clock_pacer_tick(&pacer, 16);
    CHECK(pacer.resyncs == 1 && pacer.due_ns == fake_now);
    ot_hal_clock_pacer_tick(&pacer, 16); // back on schedule from here
    CHECK(fake_sleeps.size() == 1 && fake_sleeps[0] == 16000000);
}

void test_unpaced_never_touches_the_clock() {
    OtHalClockPacer pacer;
    reset_fake(0);
    ot_hal_clock_pacer_start(&pacer, &kFakeClock, OT_HAL_PACE_UNPACED);
    for (int index = 0; index < 100; ++index) {
        ot_hal_clock_pacer_tick(&pacer, 16);
    }
    CHECK(fake_sleeps.empty() && fake_now == 0);
}

// USB HID usages (SDL scancodes) map to the set-1 codes retail stores.
void test_keymap() {
    CHECK(ot_hal_key_from_usb(0x04) == 0x1e);            // A
    CHECK(ot_hal_key_from_usb(0x28) == OT_KEY_RETURN);   // the frontend confirm key
    CHECK(ot_hal_key_from_usb(0x14) == OT_KEY_Q);
    CHECK(ot_hal_key_from_usb(0x42) == OT_KEY_F9);       // screenshot
    CHECK(ot_hal_key_from_usb(0x3d) == OT_KEY_F4);
    CHECK(ot_hal_key_from_usb(0x29) == OT_KEY_ESCAPE);
    CHECK(ot_hal_key_from_usb(0x52) == OT_KEY_UP);
    CHECK(ot_hal_key_from_usb(0x51) == OT_KEY_DOWN);
    CHECK(ot_hal_key_from_usb(0x50) == OT_KEY_LEFT);
    CHECK(ot_hal_key_from_usb(0x4f) == OT_KEY_RIGHT);
    CHECK(ot_hal_key_from_usb(0xe2) == OT_KEY_LEFT_ALT);
    CHECK(ot_hal_key_from_usb(0xe6) == OT_KEY_RIGHT_ALT);
    CHECK(ot_hal_key_from_usb(0x58) == 0x9c);            // keypad enter is extended
    CHECK(ot_hal_key_from_usb(0) == 0);
    CHECK(ot_hal_key_from_usb(0x1000) == 0);
    // Distinct usages map to distinct codes, except the two backslash keys.
    int owners[256] = {0};
    for (uint32_t usage = 0; usage < 0x100; ++usage) {
        const uint8_t code = ot_hal_key_from_usb(usage);
        if (code != 0 && usage != 0x32) {
            CHECK(owners[code] == 0);
            owners[code] = static_cast<int>(usage);
        }
    }
}

// A video backend that records what the movie backend asks of it.
struct VideoLog {
    std::vector<std::string> calls;
    OtHalTextureDesc created{};
    std::vector<uint8_t> uploaded;
    OtHalDrawState state{};
    std::vector<OtHalVertex> quad;
    uint32_t topology = 0;
} video_log;

OtHalHandle OT_HAL_CALL log_texture_create(const OtHalTextureDesc *desc) {
    video_log.calls.push_back("texture_create");
    video_log.created = *desc;
    return 7;
}

void OT_HAL_CALL log_texture_update(OtHalHandle texture, const OtHalRect *area, const void *pixels, int32_t pitch) {
    video_log.calls.push_back(texture == 7 ? "texture_update" : "texture_update?");
    const uint8_t *bytes = static_cast<const uint8_t *>(pixels);
    video_log.uploaded.assign(bytes, bytes + static_cast<size_t>(area->h) * static_cast<size_t>(pitch));
}

void OT_HAL_CALL log_texture_destroy(OtHalHandle texture) {
    video_log.calls.push_back(texture == 7 ? "texture_destroy" : "texture_destroy?");
}

void OT_HAL_CALL log_begin_frame() { video_log.calls.push_back("begin_frame"); }

void OT_HAL_CALL log_clear(uint32_t flags, uint32_t color, float, const OtHalRect *area) {
    video_log.calls.push_back(flags == OT_HAL_CLEAR_COLOR && color == 0xff000000u && area == nullptr ? "clear" : "clear?");
}

void OT_HAL_CALL log_set_scissor(const OtHalRect *area) {
    video_log.calls.push_back(area == nullptr ? "set_scissor" : "set_scissor?");
}

void OT_HAL_CALL log_draw(const OtHalDrawState *state, uint32_t topology, const OtHalVertex *vertices, uint32_t count) {
    video_log.calls.push_back("draw");
    video_log.state = *state;
    video_log.topology = topology;
    video_log.quad.assign(vertices, vertices + count);
}

void OT_HAL_CALL log_present() { video_log.calls.push_back("present"); }

OtHalVideoBackend logging_video() {
    OtHalVideoBackend video{};
    video.name = "log";
    video.texture_create = log_texture_create;
    video.texture_update = log_texture_update;
    video.texture_destroy = log_texture_destroy;
    video.begin_frame = log_begin_frame;
    video.clear = log_clear;
    video.set_scissor = log_set_scissor;
    video.draw = log_draw;
    video.present = log_present;
    return video;
}

void test_movie_on_video() {
    static const OtHalVideoBackend video = logging_video();
    const OtHalMovieBackend *movie = ot_hal_movie_on_video(&video);
    CHECK(movie->open(4, 2, OT_HAL_PIXEL_RGB565, OT_HAL_MOVIE_FULLSCREEN) == 1);
    CHECK(video_log.created.width == 4 && video_log.created.height == 2 &&
          video_log.created.format == OT_HAL_PIXEL_RGB565);
    OtHalMovieFrame frame{};
    CHECK(movie->lock_frame(&frame) == 1);
    CHECK(frame.width == 4 && frame.height == 2 && frame.pitch == 8 && frame.format == OT_HAL_PIXEL_RGB565);
    std::memset(frame.pixels, 0xab, 16);
    movie->unlock_frame();
    video_log.calls.clear();
    // Own size: texel for pixel, no filtering, pixel centres on integers.
    movie->present(nullptr);
    const std::vector<std::string> first = {"texture_update", "begin_frame", "clear", "set_scissor", "draw", "present"};
    CHECK(video_log.calls == first);
    CHECK(video_log.uploaded.size() == 16 && video_log.uploaded[15] == 0xab);
    CHECK(video_log.state.texture == 7 && video_log.state.flags == 0);
    CHECK(video_log.topology == OT_HAL_TRIANGLE_FAN && video_log.quad.size() == 4);
    CHECK(video_log.quad[0].x == -0.5f && video_log.quad[0].y == -0.5f && video_log.quad[0].u == 0.0f);
    CHECK(video_log.quad[2].x == 3.5f && video_log.quad[2].y == 1.5f && video_log.quad[2].v == 1.0f);
    // Nothing new decoded: no upload. Scaled: filtered.
    video_log.calls.clear();
    const OtHalRect target = {10, 20, 8, 4};
    movie->present(&target);
    CHECK(video_log.calls.size() == 5 && video_log.calls[0] == "begin_frame");
    CHECK(video_log.state.flags == OT_HAL_DRAW_FILTER);
    CHECK(video_log.quad[0].x == 9.5f && video_log.quad[2].x == 17.5f && video_log.quad[2].y == 23.5f);
    video_log.calls.clear();
    movie->close();
    CHECK(video_log.calls.size() == 1 && video_log.calls[0] == "texture_destroy");
    CHECK(movie->lock_frame(&frame) == 0);
    CHECK(movie->open(4, 2, OT_HAL_PIXEL_INDEX8, 0) == 0); // no indexed frames
}

bool near(float value, float expected) {
    return value > expected - 1e-4f && value < expected + 1e-4f;
}

// DirectSound units: 10^(mB/2000); the pan attenuates the other channel.
void test_audio_gains() {
    float left = 0.0f;
    float right = 0.0f;
    ot_hal_audio_gains(0, 0, &left, &right);
    CHECK(near(left, 1.0f) && near(right, 1.0f));
    ot_hal_audio_gains(-2000, 0, &left, &right);
    CHECK(near(left, 0.1f) && near(right, 0.1f));
    ot_hal_audio_gains(0, 2000, &left, &right); // to the right: the left is attenuated
    CHECK(near(left, 0.1f) && near(right, 1.0f));
    ot_hal_audio_gains(-600, -2000, &left, &right);
    CHECK(near(right, 0.0501187f) && near(left, 0.501187f));
    ot_hal_audio_gains(OT_HAL_VOLUME_SILENT, 0, &left, &right);
    CHECK(left == 0.0f && right == 0.0f);
    ot_hal_audio_gains(0, OT_HAL_PAN_LEFT, &left, &right);
    CHECK(left == 1.0f && right == 0.0f);
}

void test_audio_render() {
    // 16-bit stereo: four frames.
    const int16_t stereo[8] = {16384, -16384, 0, 32767, -32768, 0, 8192, 8192};
    const OtHalPcmFormat format16 = {22050, 2, 16};
    float out[16] = {};
    uint32_t position = 0;
    CHECK(ot_hal_audio_render(&format16, stereo, sizeof(stereo), &position, 0, 1.0f, 0.5f, out, 3) == 3);
    CHECK(position == 12);
    CHECK(near(out[0], 0.5f) && near(out[1], -0.25f) && near(out[3], 32767.0f / 65536.0f) && near(out[4], -1.0f));
    // A one-shot stops at the end; a looping voice wraps.
    CHECK(ot_hal_audio_render(&format16, stereo, sizeof(stereo), &position, 0, 1.0f, 1.0f, out, 4) == 1);
    CHECK(position == 16);
    CHECK(ot_hal_audio_render(&format16, stereo, sizeof(stereo), &position, 0, 1.0f, 1.0f, out, 4) == 0);
    position = 12;
    CHECK(ot_hal_audio_render(&format16, stereo, sizeof(stereo), &position, 1, 1.0f, 1.0f, out, 3) == 3);
    CHECK(position == 8 && near(out[0], 0.25f) && near(out[2], 0.5f) && near(out[4], 0.0f));
    // Rendering exactly to the end of a looping sound leaves it at the start
    // (DirectSound never reports a play cursor equal to the buffer size).
    position = 8;
    CHECK(ot_hal_audio_render(&format16, stereo, sizeof(stereo), &position, 1, 1.0f, 1.0f, out, 2) == 2);
    CHECK(position == 0);
    // 8-bit mono: unsigned, both channels.
    const unsigned char mono[3] = {128, 255, 0};
    const OtHalPcmFormat format8 = {11025, 1, 8};
    position = 1;
    CHECK(ot_hal_audio_render(&format8, mono, sizeof(mono), &position, 0, 1.0f, 1.0f, out, 8) == 2);
    CHECK(near(out[0], 127.0f / 128.0f) && near(out[1], 127.0f / 128.0f) && near(out[2], -1.0f) && position == 3);
}

} // namespace

int main() {
    test_audio_gains();
    test_audio_render();
    test_movie_on_video();
    test_keymap();
    test_service_names();
    test_paced_deliveries_sleep_to_due_time();
    test_small_lead_does_not_sleep();
    test_falling_behind_resyncs_without_sprinting();
    test_unpaced_never_touches_the_clock();
    if (failures != 0) {
        std::fprintf(stderr, "hal_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("hal_test: ok");
    return 0;
}
