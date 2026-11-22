// Replays the retail model-submission traces (re/evidence/fixtures/render-model-*.txt, 279 calls) through the retail-memory form of the chain (RenderKit::model_submit_state, render_model.cpp).
// Each M3D_SubmitModelState call's entry state is laid out in a RetailMemory at the retail addresses (the packet, the viewport record, the camera object, the texture records and the ordering table
// at addresses of the test's own), the Kit runs on it, and what it leaves must equal the retail capture's exit state (the arena cursor, the vertex records, the secondary heap) and, word for word,
// what the trace-validated value-owned model (src/trg/render_model_submit.*, RetailModelRenderer) leaves on the same entry: every arena slot, every bucket head, the vertex records, the corner
// colours and the grey. The words the model does not keep (the faces walked, the model's pushback) are checked against the packet and the fixture.
//
//   opentony_render_model_kit_trace_test FIXTURE.txt [--mutate]
//
// --mutate changes one word of the Kit's first vertex record before the comparison and must make the replay fail (the comparison has teeth).
//
// Exit codes: 0 all match, 1 mismatch, 2 usage/format error.
#include "render_kit.hpp"
#include "render_model_trace_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::trg;
using namespace opentony::trg::trace_fixture;

namespace {

int failures = 0;
long line_no = 0;
long current_submit = 0;
bool mutate = false;
long submits = 0;
long slots_compared = 0;

void fail(const std::string& message) {
    ++failures;
    if (failures <= 25) {
        std::cerr << "submit " << current_submit << ": " << message << "\n";
    }
}

std::string hex(std::uint32_t value) {
    std::ostringstream os;
    os << "0x" << std::hex << value;
    return os.str();
}

// the test's own addresses for what the trace holds by value
constexpr std::uint32_t kOwn = 0x7e000000;
constexpr std::uint32_t kOwnViewport = kOwn;
constexpr std::uint32_t kOwnCamera = kOwn + 0x1000;
constexpr std::uint32_t kOwnTable = kOwn + 0x2000;            // the ordering table: 0x1000 buckets of 8 bytes
constexpr std::uint32_t kOwnTextures = kOwn + 0x10000;        // 0x40 bytes per texture record
constexpr std::uint32_t kOwnPacket = kOwn + 0x100000;

constexpr std::uint32_t kArenaCursor = 0x00560fd0;
constexpr std::uint32_t kArenaEnd = 0x00568628;
constexpr std::uint32_t kBucketHeads = 0x0056433c;
constexpr std::uint32_t kViewportEdges = 0x00563a38;
constexpr std::uint32_t kCamera = 0x005620e0;
constexpr std::uint32_t kRampPointer = 0x00563a84;
constexpr std::uint32_t kModelId = 0x00563a78;
constexpr std::uint32_t kPushbacks = 0x005808ac;
constexpr std::uint32_t kModelPushback = 0x0056e848;
constexpr std::uint32_t kLevel = 0x0056a898;
constexpr std::uint32_t kSoftware = 0x029da3a0;
constexpr std::uint32_t kTextureMode = 0x006a6964;
constexpr std::uint32_t kLights = 0x0057e888;
constexpr std::uint32_t kFaceColours = 0x00580888;
constexpr std::uint32_t kGrey = 0x0059d32c;
constexpr std::uint32_t kFacesWalked = 0x0059d338;
constexpr std::uint32_t kSecondaryCursor = 0x0059d330;

// Address ranges, merged, added as regions (a write must stay inside one region).
class Layout {
public:
    void need(std::uint32_t address, std::size_t size) { ranges_.emplace_back(address, static_cast<std::uint64_t>(address) + size); }
    void build(RetailMemory& memory) {
        std::sort(ranges_.begin(), ranges_.end());
        std::vector<std::pair<std::uint64_t, std::uint64_t>> merged;
        for (const auto& [lo, hi] : ranges_) {
            if (!merged.empty() && lo <= merged.back().second) {
                merged.back().second = std::max(merged.back().second, hi);
            } else {
                merged.emplace_back(lo, hi);
            }
        }
        for (const auto& [lo, hi] : merged) {
            memory.add_region(static_cast<std::uint32_t>(lo), static_cast<std::size_t>(hi - lo));
        }
    }

private:
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges_;
};

std::uint32_t constant_address(const std::string& name) { return static_cast<std::uint32_t>(std::stoul(name.substr(1), nullptr, 16)); }

void replay(const Submit& s, float winding, float fog_scale, const std::map<std::string, std::vector<std::uint8_t>>& constants) {
    current_submit = s.n;
    ++submits;
    const EntryTables tables = entry_tables(s);

    // ---- the value-owned model on the entry state (as render_model_retail_trace_test.cpp runs it) ----
    RetailSubmitInputs inputs;
    inputs.packet = s.packet;
    inputs.flags = s.flags;
    inputs.video_playing = !tables.video.empty() && tables.video.front();
    inputs.light_color = [&](std::uint32_t index) { return tables.lights.at(index); };
    inputs.ramp_color = [&](std::uint32_t index) { return tables.ramp.at(index); };
    inputs.texture_lookup = [&](std::uint32_t material) {
        const auto it = tables.textures.find(material);
        return it == tables.textures.end() ? RetailTextureRecord{} : RetailTextureRecord{u32(it->second, 0x10), s16(it->second, 0x14), s16(it->second, 0x16)};
    };
    RetailModelRenderer model(build_globals(s, winding, s.fog.size() == 4 ? f32(s.fog, 0) : fog_scale), s.cursor, s.end, [&](std::uint32_t address) {
        const auto it = tables.seeds.find(address);
        return it == tables.seeds.end() ? RetailPolygonImage{} : it->second;
    });
    for (const auto& [bucket, head] : tables.heads) model.set_bucket_head(bucket, head);
    std::vector<RetailVertexRecord> secondary_in(s.g.size() / 28);
    for (std::size_t i = 0; i < secondary_in.size(); ++i) {
        for (std::size_t w = 0; w < 7; ++w) secondary_in[i][w] = u32(s.g, i * 28 + w * 4);
    }
    model.seed_secondary_heap(secondary_in);
    std::uint32_t highest_cursor = s.cursor;
    std::array<std::uint32_t, 4> colours{};
    model.set_sink([&](const RetailRenderEvent& event) {
        highest_cursor = std::max(highest_cursor, event.cursor);
        if (event.kind == RetailRenderEvent::Kind::build) colours = event.colors;
    });
    bool model_ran = true;
    try {
        model_ran = model.submit(inputs);
    } catch (const std::exception& error) {
        fail(std::string("the model threw: ") + error.what());
        return;
    }

    // ---- the entry state in retail memory ----
    RetailMemory memory;
    Layout layout;
    layout.need(0x00518000, 0x00088000);                       // .rdata constants up to the secondary heap and the words after it
    layout.need(kSoftware & ~0xfffu, 0x1000);
    layout.need(kTextureMode & ~0xfffu, 0x1000);
    layout.need(kOwn, (kOwnPacket - kOwn) + s.packet.size() + 0x100);
    layout.need(s.cursor & ~0xfffu, ((s.end + 0xfffu) & ~0xfffu) - (s.cursor & ~0xfffu));
    for (const auto& [address, image] : tables.seeds) layout.need(address, kRetailPolygonSlotSize);
    for (const auto& [material, tex] : tables.textures) layout.need(material, 0x18);
    const std::uint32_t ramp = u32(s.a, kRampPointer - 0x00563a18);
    for (const auto& [index, value] : tables.ramp) layout.need(ramp + 4 * index, 4);
    layout.build(memory);

    for (const auto& [name, bytes] : constants) memory.write(constant_address(name), bytes);
    memory.write(0x00563a18, s.a);
    memory.write(0x00564340, s.b);
    memory.write(0x00562098, s.q);
    memory.write(0x0059d328, s.h);
    memory.write(0x0057e878, s.w);
    if (!s.g.empty()) memory.write(0x0058f328, s.g);
    if (s.fog.size() == 4) memory.write(0x00549d98, s.fog);
    memory.write(kOwnViewport, s.viewport);
    memory.set32(kViewportEdges, kOwnViewport);
    memory.write(kOwnCamera, s.m);
    memory.set32(kCamera, kOwnCamera);
    memory.write(kOwnPacket, s.packet);
    memory.set32(kLevel, s.level);
    memory.set32(kSoftware, s.soft);
    memory.set32(kTextureMode, s.texmode);
    memory.set16(kPushbacks + 2 * (s.level * 2000 + memory.u32(kModelId)), static_cast<std::uint16_t>(s.pushback));
    memory.set32(kArenaCursor, s.cursor);
    memory.set32(kArenaEnd, s.end);
    for (const auto& [address, image] : tables.seeds) {
        for (std::size_t w = 0; w < kRetailPolygonSlotWords; ++w) memory.set32(address + static_cast<std::uint32_t>(4 * w), image[w]);
    }
    memory.set32(kBucketHeads, kOwnTable);
    for (const auto& [bucket, head] : tables.heads) memory.set32(kOwnTable + 8 * bucket, head);
    std::uint32_t texture = kOwnTextures;
    for (const auto& [material, tex] : tables.textures) {
        memory.set32(material + 0x14, texture);
        memory.write(texture, tex);
        texture += 0x40;
    }
    for (const auto& [index, value] : tables.lights) memory.set32(kLights + 4 * index, value);
    for (const auto& [index, value] : tables.ramp) memory.set32(ramp + 4 * index, value);

    RetailServices services;
    for (const bool playing : tables.video) services.script("engine_004cde70", playing ? 1 : 0);
    renderkit::RenderKit kit(memory, services);
    try {
        kit.model_submit_state(kOwnPacket, s.flags);
    } catch (const std::exception& error) {
        fail(std::string("the Kit threw: ") + error.what());
        return;
    }
    const std::uint32_t vertex_count = u16(s.packet, 2);
    if (mutate && current_submit == 1 && vertex_count != 0) {
        memory.set32(kRetailVertexHeapBase, memory.u32(kRetailVertexHeapBase) ^ 1);
    }

    // ---- against the retail exit ----
    const auto expect = [&](const std::string& what, std::uint32_t want, std::uint32_t got) {
        if (want != got) fail(what + ": expected " + hex(want) + ", the Kit " + hex(got));
    };
    if (s.has_exit) {
        expect("arena cursor after (retail)", s.cursor_out, memory.u32(kArenaCursor));
        for (std::size_t i = 0; i * 28 < s.heap.size(); ++i) {
            const std::uint16_t source_flags = u16(s.packet, 0x1c + i * 8 + 6);
            for (std::size_t w = 0; w < 7; ++w) {
                // a record of the pointer form (source flag 2, no 0x10) receives only its flags and its pointer
                if ((source_flags & 0x12) == 2 && w != 4 && w != 6) continue;
                const std::uint32_t at = kRetailVertexHeapBase + static_cast<std::uint32_t>(i * 28 + w * 4);
                if (u32(s.heap, i * 28 + w * 4) != memory.u32(at)) {
                    expect("vertex " + std::to_string(i) + " word " + std::to_string(w) + " (retail)", u32(s.heap, i * 28 + w * 4), memory.u32(at));
                    break;
                }
            }
        }
        expect("secondary heap cursor (retail)", kRetailSecondaryHeapBase + static_cast<std::uint32_t>(s.heap2.size()), memory.u32(kSecondaryCursor));
        for (std::size_t at = 0; at + 4 <= s.heap2.size(); at += 4) {
            if (u32(s.heap2, at) != memory.u32(kRetailSecondaryHeapBase + static_cast<std::uint32_t>(at))) {
                expect("secondary heap byte " + std::to_string(at) + " (retail)", u32(s.heap2, at), memory.u32(kRetailSecondaryHeapBase + static_cast<std::uint32_t>(at)));
                break;
            }
        }
    }

    // ---- against the model ----
    expect("arena cursor after (model)", model.arena_cursor(), memory.u32(kArenaCursor));
    for (std::uint32_t slot = s.cursor; slot <= highest_cursor + kRetailPolygonSlotSize && slot + kRetailPolygonSlotSize <= s.end; slot += kRetailPolygonSlotSize) {
        const RetailPolygonImage& want = model.slot(slot);
        ++slots_compared;
        for (std::size_t w = 0; w < kRetailPolygonSlotWords; ++w) {
            const std::uint32_t got = memory.u32(slot + static_cast<std::uint32_t>(4 * w));
            if (want[w] != got) {
                expect("slot " + hex(slot) + " word " + std::to_string(w) + " (model)", want[w], got);
                break;
            }
        }
    }
    for (std::uint32_t bucket = 0; bucket < kRetailBucketCount; ++bucket) {
        if (model.bucket_head(bucket) != memory.u32(kOwnTable + 8 * bucket)) {
            expect("bucket " + hex(bucket) + " head (model)", model.bucket_head(bucket), memory.u32(kOwnTable + 8 * bucket));
        }
    }
    const auto& heap = model.vertex_heap();
    for (std::uint32_t i = 0; i < vertex_count; ++i) {
        for (std::uint32_t w = 0; w < 7; ++w) {
            const std::uint32_t got = memory.u32(kRetailVertexHeapBase + i * 28 + w * 4);
            if (heap[i][w] != got) {
                expect("vertex " + std::to_string(i) + " word " + std::to_string(w) + " (model)", heap[i][w], got);
                break;
            }
        }
    }
    const bool walked = model_ran && (s.flags & 4) == 0;
    if (walked) {
        for (std::uint32_t corner = 0; corner < 4; ++corner) {
            expect("corner colour " + std::to_string(corner) + " (model)", colours[corner], memory.u32(kFaceColours + 4 * corner));
        }
        if ((s.flags & 8) != 0) expect("grey (model)", model.grey_color(), memory.u32(kGrey));
        expect("model pushback", static_cast<std::uint32_t>(static_cast<std::int32_t>(s.pushback)), memory.u32(kModelPushback));
    }
    expect("faces walked", walked ? u16(s.packet, 6) : 0u, memory.u32(kFacesWalked));
}

}  // namespace

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
    const int status = read_fixture(fixture, constants, line_no, std::cerr, [&](const Submit& s, float winding, float fog_scale) {
        replay(s, winding, fog_scale, constants);
    });
    if (status != 0) {
        return status;
    }
    std::cout << "submits " << submits << ", arena slots compared " << slots_compared << "\n";
    if (submits == 0) {
        std::cerr << "fixture holds no submits\n";
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " mismatches\n";
        return 1;
    }
    return 0;
}
