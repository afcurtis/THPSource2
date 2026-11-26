#pragma once

#include "../../runtime/host_options.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace opentony::kitdll {

struct Aspect {
    double ratio = 4.0 / 3.0;
    bool window = false;
    bool stretch = false;
};

inline Aspect parse_aspect(const char* text) {
    Aspect aspect;
    if (text == nullptr) {
        return aspect;
    }
    int width = 0, height = 0;
    if (std::strcmp(text, "window") == 0 || std::strcmp(text, "auto") == 0) {
        aspect.window = true;
    } else if (std::strcmp(text, "stretch") == 0) {
        aspect.stretch = true;
    } else if (std::sscanf(text, "%d:%d", &width, &height) == 2 && width > 0 && height > 0) {
        aspect.ratio = static_cast<double>(width) / height;
    }
    return aspect;
}

// The game keeps its 480-line projection and needs even widths.
inline std::int32_t picture_width(double ratio) {
    const auto width = static_cast<std::int32_t>(480.0 * (std::clamp)(ratio, 4.0 / 3.0, 32.0 / 9.0) + 0.5);
    return (width + 1) & ~1;
}

// The two 4.12 projection factors keep the vertical view unchanged in Hor+ mode.
inline std::uint32_t wide_pixel_aspect(std::int32_t width, bool fixed) {
    return fixed ? 0x1000u : static_cast<std::uint32_t>(std::lround(4096.0 * std::sqrt(640.0 / width)));
}

inline void read_hud_layout(runtime::HostOptions& options) {
    const auto percent = [](const char* name, double low, double high, double normal) {
        const char* text = std::getenv(name);
        return text != nullptr && *text != '\0' ? (std::clamp)(std::strtod(text, nullptr), low, high) : normal;
    };
    const char* anchor = std::getenv("OT_HUD_ANCHOR");
    options.hud_anchor = anchor != nullptr && *anchor != '\0' && std::strcmp(anchor, "0") != 0;
    options.hud_scale = static_cast<float>(percent("OT_HUD_SCALE", 25.0, 200.0, 100.0) / 100.0);
    options.hud_margin = static_cast<float>(percent("OT_HUD_MARGIN", 0.0, 10.0, 0.0) / 100.0);
}

}  // namespace opentony::kitdll
