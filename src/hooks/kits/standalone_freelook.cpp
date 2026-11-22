// The free look of the standalone (runtime/free_look.hpp; docs: THPS2 post_phase_5/camera.md): the configuration from the environment, read once with the other options
// (standalone_options.cpp read_options). Nothing is set when the variables are absent, which is how every scenario and gate runs.
//
//   OT_FREELOOK=spring|stay, OT_FREELOOK_STICK=left|right, OT_FREELOOK_SENS=1..10, OT_FREELOOK_INVERT=x|y|xy: the camera stick (the launcher sets them)
//   OT_FREELOOK_FIXED=yaw,pitch[,distance]   a test switch: the camera held turned by these offsets (12-bit angles, the distance in the game's units; C, so 0x400, -0x100
//                                            or 1024). +yaw looks left, +pitch looks down. A malformed value is reported and ignored.
#include "standalone_options.hpp"

#include "../../runtime/free_look.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace opentony::kitdll {

namespace {

// one signed number in C notation, then a comma or the end; false when there is none
bool number(const char*& text, long& out) {
    char* end = nullptr;
    errno = 0;
    out = std::strtol(text, &end, 0);
    if (end == text || errno != 0 || (*end != ',' && *end != '\0') || out < -0x8000 || out > 0x7fff) {
        return false;
    }
    text = *end == ',' ? end + 1 : end;
    return true;
}

}  // namespace

namespace {
bool g_look_stick_left = false;   // OT_FREELOOK_STICK=left: the look stick is the left one (default: the right)
}

void read_freelook_options() {
    if (const char* mode = std::getenv("OT_FREELOOK"); mode != nullptr && *mode != '\0') {
        const bool stay = std::strcmp(mode, "stay") == 0;
        if (stay || std::strcmp(mode, "spring") == 0) {
            const char* stick = std::getenv("OT_FREELOOK_STICK");
            g_look_stick_left = stick != nullptr && std::strcmp(stick, "left") == 0;
            runtime::freelook::Config config;
            config.enabled = true;   // (stay is spring until C9)
            // OT_FREELOOK_SENS 1 to 10 (default 5) and OT_FREELOOK_INVERT x, y or xy; anything else is the default
            if (const char* sens = std::getenv("OT_FREELOOK_SENS"); sens != nullptr && *sens != '\0') {
                char* end = nullptr;
                const long value = std::strtol(sens, &end, 10);
                if (*end == '\0' && value >= 1 && value <= 10) {
                    config.sensitivity = static_cast<int>(value);
                }
            }
            if (const char* invert = std::getenv("OT_FREELOOK_INVERT"); invert != nullptr) {
                config.invert_x = std::strchr(invert, 'x') != nullptr;
                config.invert_y = std::strchr(invert, 'y') != nullptr;
            }
            runtime::freelook::set_config(config);
            std::printf("options: free look %s%s, %s stick, sensitivity %d%s%s\n", mode, stay ? " (acts as spring for now)" : "", g_look_stick_left ? "left" : "right",
                        config.sensitivity, config.invert_x ? ", invert x" : "", config.invert_y ? ", invert y" : "");
            std::fflush(stdout);
        } else {
            std::printf("options: OT_FREELOOK '%s' is not spring or stay; free look stays off\n", mode);
            std::fflush(stdout);
        }
    }
    const char* fixed = std::getenv("OT_FREELOOK_FIXED");
    if (fixed == nullptr || *fixed == '\0') {
        return;
    }
    const char* cursor = fixed;
    long yaw = 0, pitch = 0, distance = 0;
    bool valid = number(cursor, yaw) && number(cursor, pitch);
    if (valid && *cursor != '\0') {
        valid = number(cursor, distance) && *cursor == '\0';
    }
    if (!valid) {
        std::printf("options: OT_FREELOOK_FIXED '%s' is not yaw,pitch[,distance]; ignored\n", fixed);
        std::fflush(stdout);
        return;
    }
    runtime::freelook::Look look;
    look.yaw = static_cast<std::int32_t>(yaw);
    look.pitch = static_cast<std::int32_t>(pitch);
    look.distance = static_cast<std::int32_t>(distance);
    runtime::freelook::set_fixed(look);
    std::printf("options: free look fixed (a test switch): yaw %ld, pitch %ld, distance %ld\n", yaw, pitch, distance);
    std::fflush(stdout);
}

// every input poll, after the scripted input is merged: player one's look stick (no pad connected is a centred stick)
void freelook_each_poll(bool connected, const std::int16_t axes[4]) {
    if (!runtime::freelook::enabled()) {
        return;
    }
    runtime::freelook::Input input;
    if (connected) {
        input.x = axes[g_look_stick_left ? 0 : 2];
        input.y = axes[g_look_stick_left ? 1 : 3];
    }
    runtime::freelook::set_input(0, input);
    // OT_INPUT_TRACE=1: player one's look as the last camera update left it, each poll it changed (poll 0 is the first PCInput_Poll)
    static const bool trace = std::getenv("OT_INPUT_TRACE") != nullptr;
    static long polls = 0;
    static runtime::freelook::Look shown;
    const runtime::freelook::Look now = runtime::freelook::look(0);
    if (trace && !(now == shown)) {
        std::printf("input poll %ld: free look yaw %d pitch %d step %d\n", polls, static_cast<int>(now.yaw), static_cast<int>(now.pitch), static_cast<int>(now.step));
        std::fflush(stdout);
        shown = now;
    }
    ++polls;
}

}  // namespace opentony::kitdll
