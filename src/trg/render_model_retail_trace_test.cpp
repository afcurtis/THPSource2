// Replays a retail model-submission trace (see scripts/make_render_trace_fixture.py) through
// RetailModelRenderer: each M3D_SubmitModelState call is rebuilt from its raw entry state and the
// native event stream (polygon builds, near clips, depth classification, bucket links, arena
// cursor moves) plus the exit vertex heaps must equal the retail capture.
//
//   opentony_render_model_retail_trace_test FIXTURE.txt [--mutate]
//
// --mutate flips one bit of the first classified polygon's expected bucket and must make the
// replay fail (proves the comparison has teeth).
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "render_model_submit.hpp"
#include "render_model_trace_fixture.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace opentony::trg;
using namespace opentony::trg::trace_fixture;

namespace {

int failures = 0;
long line_no = 0;
long current_submit = 0;
bool mutate = false;

void fail(const std::string& message) {
    ++failures;
    if (failures <= 25) {
        std::cerr << "submit " << current_submit << " (line " << line_no << "): " << message << "\n";
    }
}

// Compare the used prefix of an arena image. Words that retail leaves stale are masked: the
// low three bytes of word 1, word 3 (unused), and the bucket link unless the polygon was linked.
void compare_image(const std::string& what, const ImagePrefix& expected,
                   const RetailPolygonImage& actual, bool check_link) {
    for (std::size_t w = 0; w < expected.words; ++w) {
        if (w == 3 || (w == 0 && !check_link)) continue;
        std::uint32_t mask = w == 1 ? 0xff000000U : 0xffffffffU;
        if ((expected.data[w] & mask) != (actual[w] & mask)) {
            std::ostringstream os;
            os << what << " word " << w << ": retail 0x" << std::hex << expected.data[w]
               << " native 0x" << actual[w];
            fail(os.str());
            return;
        }
    }
}

struct Stats {
    long submits = 0, polygons = 0, events = 0, near = 0, classified = 0, linked = 0, splits = 0,
         rejected = 0;
    long flag_counts[16] = {};
} stats;

void replay(const Submit& s, float winding, float fog_scale) {
    current_submit = s.n;
    RetailRenderGlobals globals = build_globals(s, winding, s.fog.size() == 4 ? f32(s.fog, 0) : fog_scale);

    RetailSubmitInputs inputs;
    inputs.packet = s.packet;
    inputs.flags = s.flags;
    const EntryTables tables = entry_tables(s);
    std::map<std::uint32_t, RetailTextureRecord> textures;
    for (const auto& [material, tex] : tables.textures) {
        textures[material] = {u32(tex, 0x10), s16(tex, 0x14), s16(tex, 0x16)};
    }
    const auto& seeds = tables.seeds;
    const auto& heads = tables.heads;
    const auto& lights = tables.lights;
    const auto& ramp = tables.ramp;
    inputs.video_playing = !tables.video.empty() && tables.video.front();
    inputs.light_color = [&](std::uint32_t index) { return lights.at(index); };
    inputs.ramp_color = [&](std::uint32_t index) { return ramp.at(index); };
    inputs.texture_lookup = [&](std::uint32_t material) {
        const auto it = textures.find(material);
        return it == textures.end() ? RetailTextureRecord{} : it->second;
    };

    RetailModelRenderer renderer(globals, s.cursor, s.end, [&](std::uint32_t address) {
        const auto it = seeds.find(address);
        return it == seeds.end() ? RetailPolygonImage{} : it->second;
    });
    for (const auto& [bucket, head] : heads) renderer.set_bucket_head(bucket, head);
    std::vector<RetailVertexRecord> secondary_in(s.g.size() / 28);
    for (std::size_t i = 0; i < secondary_in.size(); ++i) {
        for (std::size_t w = 0; w < 7; ++w) secondary_in[i][w] = u32(s.g, i * 28 + w * 4);
    }
    renderer.seed_secondary_heap(secondary_in);
    std::vector<RetailRenderEvent> actual;
    renderer.set_sink([&](const RetailRenderEvent& event) { actual.push_back(event); });
    try {
        renderer.submit(inputs);
    } catch (const std::exception& error) {
        fail(std::string("native threw: ") + error.what());
        return;
    }

    ++stats.submits;
    for (int bit = 0; bit < 16; ++bit) {
        if (s.flags & (1U << bit)) ++stats.flag_counts[bit];
    }
    if (actual.size() != s.events.size()) {
        std::ostringstream os;
        os << "event count: retail " << s.events.size() << " native " << actual.size();
        fail(os.str());
    }
    bool linked = false;
    bool mutated = false;
    for (std::size_t i = 0; i < std::min(actual.size(), s.events.size()); ++i) {
        const auto& want = s.events[i];
        const auto& got = actual[i];
        ++stats.events;
        using Kind = RetailRenderEvent::Kind;
        const auto name = [&] {
            switch (got.kind) {
            case Kind::build: return "build";
            case Kind::build_end: return "build_end";
            case Kind::clip_bucket: return "clip_bucket";
            case Kind::clip_bucket_end: return "clip_bucket_end";
            case Kind::near_clip: return "near_clip";
            case Kind::near_clip_end: return "near_clip_end";
            case Kind::classify: return "classify";
            case Kind::classify_end: return "classify_end";
            case Kind::video: return "video";
            }
            return "?";
        }();
        if (want.kind != name) {
            fail("event " + std::to_string(i) + ": retail " + want.kind + " native " + name);
            break;
        }
        const auto expect = [&](const char* field, std::uint32_t retail, std::uint32_t native) {
            if (retail != native) {
                std::ostringstream os;
                os << "event " << i << " " << want.kind << " " << field << ": retail 0x" << std::hex
                   << retail << " native 0x" << native;
                fail(os.str());
            }
        };
        switch (got.kind) {
        case Kind::build:
            expect("cursor", hex32(want.fields[0]), got.cursor);
            expect("selector", hex32(want.fields[1]), got.selector);
            if (s.flags & 8) expect("grey", hex32(want.fields[2]), got.a);
            {
                // Triangles never read the fourth color; retail leaves the previous face's there.
                const auto colors = unhex(want.fields[3]);
                const std::size_t used = (got.selector & 0x10) != 0 ? 3 : 4;
                for (std::size_t c = 0; c < used; ++c) {
                    const std::string label = "face color " + std::to_string(c);
                    expect(label.c_str(), u32(colors, c * 4), got.colors[c]);
                }
            }
            break;
        case Kind::build_end:
            expect("cursor", hex32(want.fields[0]), got.cursor);
            break;
        case Kind::clip_bucket:
            expect("poly", hex32(want.fields[0]), got.poly);
            expect("cursor", hex32(want.fields[1]), got.cursor);
            expect("selector", hex32(want.fields[2]), got.selector);
            expect("clip flags", hex32(want.fields[3]), got.a);
            compare_image("clip_bucket image", parse_image(want.fields[4]), got.image, false);
            linked = false;
            ++stats.polygons;
            break;
        case Kind::clip_bucket_end:
            expect("poly", hex32(want.fields[0]), got.poly);
            expect("cursor", hex32(want.fields[1]), got.cursor);
            compare_image("clip_bucket_end image", parse_image(want.fields[2]), got.image, linked);
            if (linked) ++stats.linked; else ++stats.rejected;
            break;
        case Kind::near_clip:
            expect("poly", hex32(want.fields[0]), got.poly);
            ++stats.near;
            break;
        case Kind::near_clip_end:
            expect("poly", hex32(want.fields[0]), got.poly);
            expect("return", hex32(want.fields[1]), got.a);
            compare_image("near_clip image", parse_image(want.fields[2]), got.image, false);
            break;
        case Kind::classify:
            expect("poly", hex32(want.fields[0]), got.poly);
            expect("offset bits", hex32(want.fields[1]), got.a);
            expect("param4 bits", hex32(want.fields[2]), got.b);
            expect("selector", hex32(want.fields[3]), got.selector);
            break;
        case Kind::classify_end: {
            expect("poly", hex32(want.fields[0]), got.poly);
            std::uint32_t bucket = hex32(want.fields[1]);
            if (mutate && !mutated) {
                bucket ^= 1;
                mutated = true;
            }
            expect("bucket", bucket, got.a);
            expect("previous head", hex32(want.fields[2]), got.b);
            compare_image("classify image", parse_image(want.fields[3]), got.image, false);
            linked = true;
            ++stats.classified;
            break;
        }
        case Kind::video:
            expect("result", hex32(want.fields[0]), got.a);
            break;
        }
    }
    if (s.has_exit) {
        if (renderer.arena_cursor() != s.cursor_out) {
            std::ostringstream os;
            os << "arena cursor after: retail 0x" << std::hex << s.cursor_out << " native 0x"
               << renderer.arena_cursor();
            fail(os.str());
        }
        const auto& heap = renderer.vertex_heap();
        for (std::size_t i = 0; i * 28 < s.heap.size(); ++i) {
            const std::uint16_t src_flags = u16(s.packet, 0x1c + i * 8 + 6);
            for (std::size_t w = 0; w < 7; ++w) {
                // A record built from the pointer form (source flag 2, no 0x10) only receives
                // its flags word and its secondary-heap pointer.
                if ((src_flags & 0x12) == 2 && w != 4 && w != 6) continue;
                if (u32(s.heap, i * 28 + w * 4) != heap[i][w]) {
                    std::ostringstream os;
                    os << "vertex " << i << " word " << w << " (source flags 0x" << std::hex
                       << src_flags << "): retail 0x" << u32(s.heap, i * 28 + w * 4) << " native 0x"
                       << heap[i][w];
                    fail(os.str());
                    break;
                }
            }
        }
        const auto heap2 = renderer.secondary_heap();
        const std::size_t base_offset = renderer.secondary_heap_start() - kRetailSecondaryHeapBase;
        if (base_offset + heap2.size() * 28 != s.heap2.size()) {
            std::ostringstream os;
            os << "secondary heap size: retail " << s.heap2.size() << " bytes, native start "
               << base_offset << " + " << heap2.size() * 28;
            fail(os.str());
        } else {
            for (std::size_t i = 0; i < heap2.size(); ++i) {
                for (std::size_t w = 0; w < 7; ++w) {
                    const std::size_t at = base_offset + i * 28 + w * 4;
                    if (u32(s.heap2, at) != heap2[i][w]) {
                        std::ostringstream os;
                        os << "secondary heap record " << i << " word " << w << ": retail 0x"
                           << std::hex << u32(s.heap2, at) << " native 0x" << heap2[i][w]
                           << " (flags 0x" << s.flags << ")";
                        fail(os.str());
                        break;
                    }
                }
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " FIXTURE.txt [--mutate]\n";
        return 2;
    }
    mutate = argc > 2 && std::string(argv[2]) == "--mutate";
    std::ifstream fixture(argv[1]);
    if (!fixture) {
        std::cerr << "cannot open fixture " << argv[1] << "\n";
        return 2;
    }
    std::map<std::string, std::vector<std::uint8_t>> constants;
    if (read_fixture(fixture, constants, line_no, std::cerr, replay) != 0) {
        return 2;
    }
    // The native constant table must equal what the executable holds.
    const auto check_const = [&](const char* name, std::size_t offset, std::uint32_t bits) {
        const auto it = constants.find(name);
        if (it == constants.end() || it->second.size() < offset + 4 || u32(it->second, offset) != bits) {
            fail(std::string("constant ") + name + "+" + std::to_string(offset) + " differs");
        }
    };
    if (!constants.empty()) {
        check_const("f518910", 0, 0x39800000U);
        check_const("f518d70", 0, 0x3f800000U);
        check_const("f51993c", 0x00, 0x41233333U);
        check_const("f51993c", 0x04, 0x3d800000U);
        check_const("f51993c", 0x08, 0x37800000U);
        check_const("f51993c", 0x0c, 0x3f000000U);
        check_const("f51993c", 0x10, 0x3f7d70a4U);
        check_const("f51993c", 0x14, 0x3f333333U);
        check_const("f51993c", 0x18, 0x3f733333U);
        check_const("f51993c", 0x1c, 0x467ff000U);
        check_const("f549d50", 0x00, 0U);
        check_const("f549d50", 0x04, 1U);
        check_const("f549d50", 0x08, 3U);
        check_const("f549d50", 0x0c, 2U);
        check_const("f549d50", 0x10, 0x3f933333U);
    }
    std::cout << "submits " << stats.submits << ", events " << stats.events << ", polygons "
              << stats.polygons << " (linked " << stats.linked << ", rejected " << stats.rejected
              << "), classified " << stats.classified << ", near clips " << stats.near
              << ", state-flag counts:";
    for (int bit = 0; bit < 12; ++bit) std::cout << " " << stats.flag_counts[bit];
    std::cout << "\n";
    if (stats.submits == 0) {
        std::cerr << "fixture holds no submits\n";
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " mismatches\n";
        return 1;
    }
    return 0;
}
