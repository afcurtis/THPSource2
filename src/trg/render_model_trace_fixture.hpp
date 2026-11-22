#pragma once

// The retail model-submission trace fixture (scripts/make_render_trace_fixture.py documents the
// format): its parser and the entry state of one M3D_SubmitModelState call, shared by the replay of
// the value-owned model (render_model_retail_trace_test.cpp) and of its retail-memory form
// (src/runtime/render_model_kit_trace_test.cpp).

#include "render_model_submit.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <istream>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace opentony::trg::trace_fixture {

inline std::vector<std::uint8_t> unhex(const std::string& text) {
    std::vector<std::uint8_t> bytes;
    if (text == "-") {
        return bytes;
    }
    bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

inline std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t o) {
    std::uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
inline std::int32_t s32(const std::vector<std::uint8_t>& b, std::size_t o) {
    return static_cast<std::int32_t>(u32(b, o));
}
inline std::uint16_t u16(const std::vector<std::uint8_t>& b, std::size_t o) {
    std::uint16_t v;
    std::memcpy(&v, b.data() + o, 2);
    return v;
}
inline std::int16_t s16(const std::vector<std::uint8_t>& b, std::size_t o) {
    return static_cast<std::int16_t>(u16(b, o));
}
inline float f32(const std::vector<std::uint8_t>& b, std::size_t o) {
    float v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
inline std::uint32_t hex32(const std::string& text) { return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16)); }

struct ImagePrefix {
    std::size_t words{};
    std::vector<std::uint32_t> data;
};

inline ImagePrefix parse_image(const std::string& token) {
    const auto colon = token.find(':');
    ImagePrefix prefix;
    prefix.words = std::stoul(token.substr(0, colon));
    const auto bytes = unhex(token.substr(colon + 1));
    prefix.data.resize(prefix.words);
    std::memcpy(prefix.data.data(), bytes.data(), bytes.size());
    return prefix;
}

struct ExpectedEvent {
    std::string kind;
    std::vector<std::string> fields;
};

struct Submit {
    long n{};
    std::uint32_t flags{};
    std::uint32_t cursor{};
    std::uint32_t end{};
    std::uint32_t vertices{};
    std::uint32_t level{};
    std::uint32_t soft{};
    std::uint32_t texmode{};
    std::int16_t pushback{};
    // P packet, V viewport record, A 0x563a18 (0x118 bytes), B 0x564340 (0x50), Q 0x562098 (0x18),
    // H 0x59d328 (0x18), M the object behind DAT_005620e0 (0x90), W 0x57e878 (0xc), G the secondary
    // heap at entry, F FogYonScale
    std::vector<std::uint8_t> packet, viewport, a, b, q, h, m, w, g, fog;
    std::vector<ExpectedEvent> events;
    std::uint32_t cursor_out{};
    std::vector<std::uint8_t> heap, heap2;
    bool has_exit{};
};

inline RetailRenderGlobals build_globals(const Submit& s, float winding, float fog_scale) {
    RetailRenderGlobals g;
    g.winding_threshold = winding;
    g.fog_yon_scale = fog_scale;
    g.viewport = {u16(s.viewport, 0), u16(s.viewport, 2), u16(s.viewport, 4), u16(s.viewport, 6),
                  u16(s.viewport, 8), u16(s.viewport, 10), u16(s.viewport, 14),
                  u16(s.viewport, 0x10), u16(s.viewport, 0x12)};
    for (std::size_t i = 0; i < 9; ++i) {
        g.matrix_a[i] = s16(s.a, i * 2);                       // 0x563a18
        g.matrix_b[i] = s16(s.a, 0xf8 + i * 2);                // 0x563b10
        g.matrix_c[i] = s16(s.b, i * 2);                       // 0x564340
        g.camera_matrix[i] = s16(s.m, 0x74 + i * 2);           // DAT_005620e0 + 0x74
    }
    for (std::size_t i = 0; i < 3; ++i) {
        g.translation_a[i] = s32(s.a, 0xe0 + i * 4);           // 0x563af8
        g.translation_b[i] = s32(s.a, 0x10c + i * 4);          // 0x563b24
        g.offset_c[i] = s16(s.b, 0x12 + i * 2);                // 0x564352
        g.back_vector[i] = f32(s.w, i * 4);                    // 0x57e878
    }
    g.camera_value = s32(s.m, 8);
    g.back_bias = f32(s.q, 0);                                 // 0x562098
    g.z_pushback = f32(s.q, 0x10);                             // 0x5620a8
    g.grey_source = f32(s.a, 0x44);                            // 0x563a5c
    g.depth_offset_2000 = s16(s.a, 0x4c);                      // 0x563a64
    g.depth_offset_4000 = s16(s.a, 0x64);                      // 0x563a7c
    g.depth_offset_6000 = s16(s.a, 0x70);                      // 0x563a88
    g.reverse_winding = u16(s.a, 0xf0);                        // 0x563b08
    g.scene_color = u32(s.a, 0x5c);                            // 0x563a74
    g.model_color_word = u32(s.b, 0x44);                       // 0x564384
    g.selector_source = u32(s.b, 0x18);                        // 0x564358
    g.face_state_bits = u32(s.b, 0x3c);                        // 0x56437c
    g.current_level = s.level;
    g.software_renderer = s.soft;
    g.texture_mode_bits = s.texmode;
    g.model_pushback = s.pushback;
    g.secondary_heap_cursor = u32(s.h, 8);                     // 0x59d330
    return g;
}

// What the events of one submit say about its entry state: the light-array and ramp entries
// M3D_ResolveFaceColor reads, each material's 0x20-byte runtime texture record (the one its +0x14
// points to), the prior content of arena slots (first sighting), the bucket heads the polygons were
// linked in front of (first sighting) and Video_Playing's answers in order.
struct EntryTables {
    std::map<std::uint32_t, std::uint32_t> lights, ramp;
    std::map<std::uint32_t, std::vector<std::uint8_t>> textures;
    std::map<std::uint32_t, RetailPolygonImage> seeds;
    std::map<std::uint32_t, std::uint32_t> heads;
    std::vector<bool> video;
};

inline EntryTables entry_tables(const Submit& s) {
    EntryTables t;
    const auto parse_table = [](const std::string& text, std::map<std::uint32_t, std::uint32_t>& out) {
        if (text == "-") return;
        std::istringstream list(text);
        std::string item;
        while (std::getline(list, item, ',')) {
            const auto eq = item.find('=');
            out[static_cast<std::uint32_t>(std::stoul(item.substr(0, eq)))] = hex32(item.substr(eq + 1));
        }
    };
    for (const auto& ev : s.events) {
        if (ev.kind == "build") {
            parse_table(ev.fields[8], t.lights);
            parse_table(ev.fields[9], t.ramp);
            const std::uint32_t material = hex32(ev.fields[4]);
            auto tex = unhex(ev.fields[5]);
            if (!tex.empty()) {
                t.textures[material] = std::move(tex);
            }
            const std::uint32_t cursor = hex32(ev.fields[0]);
            for (int half = 0; half < 2; ++half) {
                const auto pre = unhex(ev.fields[6 + half]);
                const std::uint32_t address = cursor + half * kRetailPolygonSlotSize;
                if (!pre.empty() && !t.seeds.count(address)) {
                    RetailPolygonImage image;
                    std::memcpy(image.data(), pre.data(), sizeof(image));
                    t.seeds[address] = image;
                }
            }
        } else if (ev.kind == "classify_end") {
            const std::uint32_t bucket = hex32(ev.fields[1]);
            if (!t.heads.count(bucket)) t.heads[bucket] = hex32(ev.fields[2]);
        } else if (ev.kind == "video") {
            t.video.push_back(hex32(ev.fields[0]) != 0);
        }
    }
    return t;
}

// Reads a fixture, calling `replay` for each complete submit with the fixture's winding threshold and
// fog scale. Returns 0, or 2 for a format error (described on `errors`).
inline int read_fixture(std::istream& fixture, std::map<std::string, std::vector<std::uint8_t>>& constants,
                        long& line_no, std::ostream& errors,
                        const std::function<void(const Submit&, float winding, float fog_scale)>& replay) {
    std::string line;
    Submit current;
    bool open = false;
    while (std::getline(fixture, line)) {
        ++line_no;
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "K") {
            std::string name, hex;
            in >> name >> hex;
            constants[name] = unhex(hex);
        } else if (tag == "S") {
            current = Submit{};
            open = true;
            std::string flags, cursor, end;
            in >> current.n >> flags >> cursor >> end >> current.vertices;
            std::uint32_t ignored_normals, ignored_faces;
            in >> ignored_normals >> ignored_faces;
            current.flags = hex32(flags);
            current.cursor = hex32(cursor);
            current.end = hex32(end);
        } else if (!open) {
            errors << "line " << line_no << ": record outside a submit\n";
            return 2;
        } else if (tag == "L") {
            std::string texmode;
            int pushback;
            in >> current.level >> current.soft >> texmode >> pushback;
            current.texmode = hex32(texmode);
            current.pushback = static_cast<std::int16_t>(pushback);
        } else if (tag == "P" || tag == "V" || tag == "A" || tag == "B" || tag == "Q" || tag == "H" ||
                   tag == "M" || tag == "W" || tag == "G" || tag == "F") {
            std::string hex;
            in >> hex;
            auto bytes = unhex(hex);
            (tag == "P" ? current.packet : tag == "V" ? current.viewport : tag == "A" ? current.a
             : tag == "B" ? current.b : tag == "Q" ? current.q : tag == "H" ? current.h
             : tag == "M" ? current.m : tag == "W" ? current.w : tag == "G" ? current.g : current.fog) = std::move(bytes);
        } else if (tag == "e") {
            ExpectedEvent ev;
            in >> ev.kind;
            std::string field;
            while (in >> field) ev.fields.push_back(field);
            current.events.push_back(std::move(ev));
        } else if (tag == "X") {
            std::string cursor, heap, heap2;
            in >> cursor >> heap >> heap2;
            current.cursor_out = hex32(cursor);
            current.heap = unhex(heap);
            current.heap2 = unhex(heap2);
            current.has_exit = true;
        } else if (tag == "Z") {
            const auto& th = constants["f518484"];
            const auto& fg = constants["f549d98"];
            replay(current, f32(th, 0), f32(fg, 0));
            open = false;
        } else {
            errors << "line " << line_no << ": unknown record " << tag << "\n";
            return 2;
        }
    }
    return 0;
}

} // namespace opentony::trg::trace_fixture
