#include "standalone_texture_dump.hpp"

#include "../../platform/path_separator.h"
#include "../../hal/texture_key.hpp"
#include "standalone_png.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace texdump {
namespace {

namespace tex = opentony::hal::texture;

enum : std::uint8_t { kWorld = 1, kTwoD = 2 };       // use bits
enum : std::uint8_t { kWrap = 1, kClamp = 2 };       // wrap bits
enum : std::uint8_t { kMod2x = 1, kPlain = 2 };      // mod2x bits

struct Row {
    std::int32_t width = 0, height = 0;
    std::uint32_t format = 0;
    tex::Alpha alpha = tex::Alpha::Opaque;
    std::uint8_t use = 0, wrap = 0, mod = 0;
    bool is_volatile = false;
    std::string tag;
    bool drawn_now = false;   // uploaded for a draw by this process
};

struct Handle {
    std::int32_t width = 0, height = 0;
    std::uint32_t format = 0;
    std::uint64_t key = 0;
    Row* row = nullptr;
    std::uint32_t uploads = 0;
    std::vector<std::uint64_t> keys;            // the distinct keys this surface took, in order
    std::uint32_t volatile_written = 0;         // distinct keys at the last line of volatile.tsv
    std::uint32_t draws_scanned = 0;
};

bool g_on = false;
std::string g_dir, g_tag = "run";
std::map<std::uint64_t, Row> g_rows;
std::map<OtHalHandle, Handle> g_handles;
bool g_dirty = false;
std::uint32_t g_presents = 0;
std::uint32_t g_walk_after = 120;
std::uint32_t g_last_count = 0, g_stable = 0, g_walked_count = ~0u;
std::map<std::uint32_t, std::uint32_t> g_walked;                       // surface handle -> the version walked
std::set<std::pair<std::uint64_t, int>> g_seen_written;                 // (key, 0 draw / 1 walk)
std::vector<std::pair<std::uint64_t, int>> g_seen_pending;
std::uint32_t g_uploads = 0, g_partial = 0, g_new_keys = 0, g_png_failed = 0;

// What the dump costs while a game is played (T3): the time spent in it, in all and in the worst frame (a present to the next; a level's walk lands in one)
LARGE_INTEGER g_frequency{};
std::int64_t g_spent = 0, g_frame = 0, g_worst = 0;
struct Spend {
    LARGE_INTEGER start{};
    Spend() { QueryPerformanceCounter(&start); }
    ~Spend() {
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        g_spent += end.QuadPart - start.QuadPart;
        g_frame += end.QuadPart - start.QuadPart;
    }
};
double ms(std::int64_t ticks) { return g_frequency.QuadPart != 0 ? 1000.0 * static_cast<double>(ticks) / static_cast<double>(g_frequency.QuadPart) : 0.0; }

std::uint32_t(__cdecl* g_list)(std::uint32_t*, std::uint32_t*, std::uint32_t*, std::uint32_t) = nullptr;
int(__cdecl* g_texels)(std::uint32_t, std::uint32_t*, std::uint32_t*, std::uint32_t*, unsigned char*, std::uint32_t) = nullptr;

std::string path_of(const char* name) { return g_dir + OT_PATH_SEPARATOR + name; }

const char* use_name(std::uint8_t bits) { return bits == (kWorld | kTwoD) ? "both" : bits == kWorld ? "world" : bits == kTwoD ? "2d" : "unknown"; }
const char* wrap_name(std::uint8_t bits) { return bits == (kWrap | kClamp) ? "both" : bits == kWrap ? "wrap" : bits == kClamp ? "clamp" : "unknown"; }
const char* mod_name(std::uint8_t bits) { return bits == (kMod2x | kPlain) ? "both" : bits == kMod2x ? "yes" : bits == kPlain ? "no" : "unknown"; }

std::uint8_t parse_bits(const std::string& text, const char* both, const char* first, const char* second, std::uint8_t a, std::uint8_t b) {
    return text == both ? static_cast<std::uint8_t>(a | b) : text == first ? a : text == second ? b : 0;
}

tex::Alpha parse_alpha(const std::string& text) { return text == "cutout" ? tex::Alpha::Cutout : text == "smooth" ? tex::Alpha::Smooth : tex::Alpha::Opaque; }

std::uint32_t parse_format(const std::string& text) {
    for (const std::uint32_t format : {OT_HAL_PIXEL_ARGB8888, OT_HAL_PIXEL_ARGB1555, OT_HAL_PIXEL_ARGB4444, OT_HAL_PIXEL_RGB565}) {
        if (text == tex::format_name(format)) {
            return format;
        }
    }
    return 0;
}

void load_index() {
    std::ifstream in(path_of("index.tsv"));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string key_text, w, h, format, alpha, use, wrap, mod, vol, tag;
        std::getline(fields, key_text, '\t');
        std::getline(fields, w, '\t');
        std::getline(fields, h, '\t');
        std::getline(fields, format, '\t');
        std::getline(fields, alpha, '\t');
        std::getline(fields, use, '\t');
        std::getline(fields, wrap, '\t');
        std::getline(fields, mod, '\t');
        std::getline(fields, vol, '\t');
        std::getline(fields, tag, '\t');
        std::uint64_t key = 0;
        if (!tex::parse_key(key_text, key)) {
            continue;
        }
        Row row;
        row.width = std::atoi(w.c_str());
        row.height = std::atoi(h.c_str());
        row.format = parse_format(format);
        row.alpha = parse_alpha(alpha);
        row.use = parse_bits(use, "both", "world", "2d", kWorld, kTwoD);
        row.wrap = parse_bits(wrap, "both", "wrap", "clamp", kWrap, kClamp);
        row.mod = parse_bits(mod, "both", "yes", "no", kMod2x, kPlain);
        row.is_volatile = vol == "1";
        row.tag = tag;
        g_rows[key] = row;
    }
}

void flush() {
    if (g_dirty) {
        std::string text = "# key\twidth\theight\tformat\talpha\tuse\twrap\tmod2x\tvolatile\ttag\n";
        for (const auto& [key, row] : g_rows) {
            text += tex::key_text(key) + "\t" + std::to_string(row.width) + "\t" + std::to_string(row.height) + "\t" + tex::format_name(row.format) + "\t" +
                    tex::alpha_name(row.alpha) + "\t" + use_name(row.use) + "\t" + wrap_name(row.wrap) + "\t" + mod_name(row.mod) + "\t" + (row.is_volatile ? "1" : "0") + "\t" +
                    row.tag + "\n";
        }
        const std::string temporary = path_of("index.tsv.tmp");
        if (FILE* out = std::fopen(temporary.c_str(), "wb")) {
            const bool written = std::fwrite(text.data(), 1, text.size(), out) == text.size();
            std::fclose(out);
            if (written && MoveFileExA(temporary.c_str(), path_of("index.tsv").c_str(), MOVEFILE_REPLACE_EXISTING)) {
                g_dirty = false;
            }
        }
    }
    if (!g_seen_pending.empty()) {
        if (FILE* out = std::fopen(path_of("seen.tsv").c_str(), "ab")) {
            for (const auto& [key, how] : g_seen_pending) {
                std::fprintf(out, "%s\t%s\t%s\n", g_tag.c_str(), tex::key_text(key).c_str(), how == 0 ? "draw" : "walk");
            }
            std::fclose(out);
            g_seen_pending.clear();
        }
    }
    FILE* out = nullptr;
    for (auto& [handle, info] : g_handles) {
        if (info.keys.size() > 1 && info.keys.size() != info.volatile_written) {
            if (out == nullptr) {
                out = std::fopen(path_of("volatile.tsv").c_str(), "ab");
                if (out == nullptr) {
                    break;
                }
            }
            std::fprintf(out, "%s\t%u\t%dx%d\t%s\t%u\t%u\t%s\t%s\n", g_tag.c_str(), static_cast<unsigned>(handle), info.width, info.height, tex::format_name(info.format),
                         static_cast<unsigned>(info.keys.size()), static_cast<unsigned>(info.uploads), tex::key_text(info.keys.front()).c_str(), tex::key_text(info.keys.back()).c_str());
            info.volatile_written = static_cast<std::uint32_t>(info.keys.size());
        }
    }
    if (out != nullptr) {
        std::fclose(out);
    }
}

void report(const char* when) {
    std::printf("texdump: %s: %u uploads (%u partial), %zu keys in the index (%u new this run, %u PNG failed); %.0f ms in the dump, the worst frame %.1f ms\n", when,
                static_cast<unsigned>(g_uploads), static_cast<unsigned>(g_partial), g_rows.size(), static_cast<unsigned>(g_new_keys), static_cast<unsigned>(g_png_failed),
                ms(g_spent), ms((std::max)(g_worst, g_frame)));
    std::fflush(stdout);
}

void at_exit() {
    if (g_on) {
        flush();
        report("at exit");
    }
}

void note_seen(std::uint64_t key, int how) {
    if (g_seen_written.insert({key, how}).second) {
        g_seen_pending.push_back({key, how});
    }
}

// the row of a key: made, with its PNG, when it is new
Row* row_for(std::uint64_t key, std::int32_t width, std::int32_t height, std::uint32_t format, const void* rows, std::size_t size) {
    const auto found = g_rows.find(key);
    if (found != g_rows.end()) {
        return &found->second;
    }
    tex::Alpha alpha;
    const std::vector<std::uint8_t> rgba = tex::to_rgba8(width, height, format, rows, size, alpha);
    if (rgba.empty()) {
        return nullptr;
    }
    Row row;
    row.width = width;
    row.height = height;
    row.format = format;
    row.alpha = alpha;
    row.tag = g_tag;
    const std::string png = g_dir + OT_PATH_SEPARATOR + tex::key_text(key) + ".png";
    if (GetFileAttributesA(png.c_str()) == INVALID_FILE_ATTRIBUTES && !standalone_write_png(png, width, height, 4, rgba.data())) {
        ++g_png_failed;
    }
    ++g_new_keys;
    g_dirty = true;
    return &(g_rows[key] = row);
}

void set_volatile(Handle& info) {
    for (const std::uint64_t key : info.keys) {
        const auto found = g_rows.find(key);
        if (found != g_rows.end() && !found->second.is_volatile) {
            found->second.is_volatile = true;
            g_dirty = true;
        }
    }
}

void note_draw(Handle& info, std::uint8_t use, const OtHalDrawState* state) {
    Row& row = *info.row;
    const std::uint8_t wrap = (state->flags & OT_HAL_DRAW_WRAP) != 0 ? kWrap : kClamp;
    const std::uint8_t mod = (state->flags & OT_HAL_DRAW_MODULATE_2X) != 0 ? kMod2x : kPlain;
    if ((row.use | use) != row.use || (row.wrap | wrap) != row.wrap || (row.mod | mod) != row.mod) {
        row.use |= use;
        row.wrap |= wrap;
        row.mod |= mod;
        g_dirty = true;
    }
}

// every surface of the store with a texture format that no draw uploaded: the textures the camera has not seen yet
void walk() {
    if (g_list == nullptr || g_texels == nullptr) {
        return;
    }
    LARGE_INTEGER walk_start{};
    QueryPerformanceCounter(&walk_start);
    const std::uint32_t count = g_list(nullptr, nullptr, nullptr, 0);
    std::vector<std::uint32_t> handles(count), versions(count), caps(count);
    g_list(handles.data(), versions.data(), caps.data(), count);
    std::map<std::uint32_t, std::uint32_t> by_caps;
    std::uint32_t with_format = 0, known = 0, found_new = 0, skipped = 0, oversize = 0;
    std::vector<unsigned char> pixels(1u << 20);
    std::set<std::uint64_t> keys_walked;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t width = 0, height = 0, format = 0;
        const auto before = g_walked.find(handles[index]);
        if (before != g_walked.end() && before->second == versions[index]) {
            continue;   // walked at this version already
        }
        g_walked[handles[index]] = versions[index];
        if (g_texels(handles[index], &width, &height, &format, pixels.data(), static_cast<std::uint32_t>(pixels.size())) == 0 || format == 0 || width == 0 || height == 0) {
            ++skipped;
            continue;
        }
        if (width > 256 || height > 256) {   // a screen or an offscreen surface, not a texture (the device's own limit is 256)
            ++oversize;
            continue;
        }
        ++with_format;
        ++by_caps[caps[index]];
        const std::size_t size = static_cast<std::size_t>(width) * height * tex::bytes_per_texel(format);
        const std::uint64_t key = tex::key(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), format, pixels.data(), size);
        if (!keys_walked.insert(key).second) {
            continue;
        }
        const bool was_known = g_rows.count(key) != 0;
        Row* row = row_for(key, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height), format, pixels.data(), size);
        if (row == nullptr) {
            continue;
        }
        if (row->drawn_now) {
            ++known;
        } else {
            note_seen(key, 1);
            if (!was_known) {
                ++found_new;
            }
        }
    }
    std::string by;
    for (const auto& [flags, n] : by_caps) {
        char cell[48];
        std::snprintf(cell, sizeof cell, " 0x%x x%u", static_cast<unsigned>(flags), static_cast<unsigned>(n));
        by += cell;
    }
    LARGE_INTEGER walk_end{};
    QueryPerformanceCounter(&walk_end);
    std::printf("texdump: walk at present %u: %u surfaces in the store, %u with a texture format (caps:%s), %u distinct keys already drawn, %u only walked (%u new to the index), "
                "%u skipped, %u over 256; %.0f ms\n",
                static_cast<unsigned>(g_presents), static_cast<unsigned>(count), static_cast<unsigned>(with_format), by.c_str(), static_cast<unsigned>(known),
                static_cast<unsigned>(keys_walked.size() - known), static_cast<unsigned>(found_new), static_cast<unsigned>(skipped), static_cast<unsigned>(oversize),
                ms(walk_end.QuadPart - walk_start.QuadPart));
    std::fflush(stdout);
    flush();
}

}  // namespace

bool start(HMODULE kits) {
    const char* directory = std::getenv("OT_TEXTURE_DUMP");
    if (directory == nullptr || *directory == '\0') {
        return false;
    }
    g_dir = directory;
    while (g_dir.size() > 1 && (g_dir.back() == '\\' || g_dir.back() == '/')) {
        g_dir.pop_back();
    }
    CreateDirectoryA(g_dir.c_str(), nullptr);
    const DWORD attributes = GetFileAttributesA(g_dir.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        std::fprintf(stderr, "texdump: cannot use %s\n", g_dir.c_str());
        return false;
    }
    if (const char* tag = std::getenv("OT_TEXTURE_DUMP_TAG"); tag != nullptr && *tag != '\0') {
        g_tag = tag;
        std::replace(g_tag.begin(), g_tag.end(), '\t', ' ');
    }
    if (const char* walk_after = std::getenv("OT_TEXTURE_DUMP_WALK"); walk_after != nullptr && *walk_after != '\0') {
        g_walk_after = static_cast<std::uint32_t>(std::strtoul(walk_after, nullptr, 10));
    }
    g_list = reinterpret_cast<decltype(g_list)>(GetProcAddress(kits, "ot_standalone_surface_list"));
    g_texels = reinterpret_cast<decltype(g_texels)>(GetProcAddress(kits, "ot_standalone_surface_texels"));
    load_index();
    QueryPerformanceFrequency(&g_frequency);
    g_on = true;
    std::atexit(&at_exit);
    std::printf("texdump: %s, tag %s, %zu keys in the index, walk after %u presents%s\n", g_dir.c_str(), g_tag.c_str(), g_rows.size(), static_cast<unsigned>(g_walk_after),
                g_list == nullptr || g_texels == nullptr ? " (the DLL cannot walk the store)" : "");
    std::fflush(stdout);
    return true;
}

void on_texture_create(OtHalHandle handle, const OtHalTextureDesc* desc) {
    if (!g_on) {
        return;
    }
    Handle info;
    info.width = desc->width;
    info.height = desc->height;
    info.format = desc->format;
    g_handles[handle] = info;
}

void on_texture_update(OtHalHandle handle, const OtHalRect* area, const void* pixels, std::int32_t pitch) {
    if (!g_on) {
        return;
    }
    const Spend spend;
    const auto it = g_handles.find(handle);
    if (it == g_handles.end()) {
        return;
    }
    Handle& info = it->second;
    const std::size_t texel = tex::bytes_per_texel(info.format);
    if (texel == 0 || area->x != 0 || area->y != 0 || area->w != info.width || area->h != info.height) {
        ++g_partial;
        return;
    }
    const std::size_t row_bytes = static_cast<std::size_t>(info.width) * texel;
    std::vector<std::uint8_t> packed;
    const void* rows = pixels;
    if (static_cast<std::size_t>(pitch) != row_bytes) {
        packed.reserve(row_bytes * static_cast<std::size_t>(info.height));
        for (std::int32_t y = 0; y < info.height; ++y) {
            const auto* line = static_cast<const std::uint8_t*>(pixels) + static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch);
            packed.insert(packed.end(), line, line + row_bytes);
        }
        rows = packed.data();
    }
    const std::size_t size = row_bytes * static_cast<std::size_t>(info.height);
    const std::uint64_t key = tex::key(info.width, info.height, info.format, rows, size);
    ++g_uploads;
    ++info.uploads;
    if (info.row != nullptr && info.key == key) {
        return;
    }
    info.key = key;
    info.draws_scanned = 0;   // the scan budget is per key: a surface refilled with other texels (the main menu's wheel, one picture per item) is scanned again
    info.row = row_for(key, info.width, info.height, info.format, rows, size);
    if (info.row == nullptr) {
        return;
    }
    if (!info.row->drawn_now) {
        info.row->drawn_now = true;
        note_seen(key, 0);
    }
    if (std::find(info.keys.begin(), info.keys.end(), key) == info.keys.end()) {
        info.keys.push_back(key);
        if (info.keys.size() > 1) {
            set_volatile(info);
        }
    }
}

void on_texture_destroy(OtHalHandle handle) {
    if (!g_on) {
        return;
    }
    flush();   // its volatile line, before the handle's number is reused
    g_handles.erase(handle);
}

void on_draw(const OtHalDrawState* state, const OtHalVertex* vertices, std::uint32_t count) {
    if (!g_on || state->texture == OT_HAL_INVALID_HANDLE) {
        return;
    }
    const Spend spend;
    const auto it = g_handles.find(state->texture);
    if (it == g_handles.end() || it->second.row == nullptr) {
        return;
    }
    Handle& info = it->second;
    std::uint8_t use = 0;
    if ((info.row->use & kWorld) == 0 && info.draws_scanned < 64) {
        ++info.draws_scanned;
        for (std::uint32_t k = 0; k < count; ++k) {
            if (vertices[k].rhw != 1.0f) {
                use = kWorld;
                break;
            }
        }
        if (use == 0) {
            use = kTwoD;
        }
    } else if ((info.row->use & kWorld) != 0) {
        use = kWorld;
    }
    note_draw(info, use, state);
}

void on_sprite(const OtHalDrawState* state) {
    if (!g_on || state->texture == OT_HAL_INVALID_HANDLE) {
        return;
    }
    const auto it = g_handles.find(state->texture);
    if (it == g_handles.end() || it->second.row == nullptr) {
        return;
    }
    note_draw(it->second, kTwoD, state);
}

void on_present() {
    if (!g_on) {
        return;
    }
    g_worst = (std::max)(g_worst, g_frame);   // the frame that ends here; this present's own work (a walk) counts to the next
    g_frame = 0;
    const Spend spend;
    ++g_presents;
    if (g_presents % 60 != 0) {
        return;
    }
    if (g_walk_after != 0 && g_list != nullptr) {
        const std::uint32_t count = g_list(nullptr, nullptr, nullptr, 0);
        g_stable = count == g_last_count ? g_stable + 60 : 0;
        g_last_count = count;
        if (g_stable >= g_walk_after && count != g_walked_count) {
            g_walked_count = count;
            walk();
        }
    }
    flush();
}

}  // namespace texdump
