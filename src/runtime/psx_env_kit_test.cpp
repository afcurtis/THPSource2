// The PSX region parser on the retail memory model (Phase 5.4a, 7b continued, part 4): one small region (an object, a model with one face, one material checksum, a
// pulse tag) parsed by PsxEnvKit against a fake engine that plays the functions under the parser (the allocator, PSX_FindTrailingTables, the material list). What this
// pins is the parser's own bookkeeping as read from the disassembly; equivalence with retail is tony/psx_env_oracle.py (the retail functions on random regions).
#include "psx_env_kit.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::psxenvkit;

namespace {

int failures = 0;

void expect_eq(std::uint32_t actual, std::uint32_t expected, const char* what) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": 0x" << std::hex << actual << " != 0x" << expected << std::dec << "\n";
    }
}

constexpr std::uint32_t kGlobals = 0x00510000;       // .rdata .. .bss: the slot records, the globals, the wib default
constexpr std::uint32_t kGlobalsSize = 0x00090000;
constexpr std::uint32_t kHeap = 0x01000000;
constexpr std::uint32_t kHeapSize = 0x00100000;
constexpr std::uint32_t kRaw = kHeap;                // the region image
constexpr std::uint32_t kSlot = 3;
constexpr std::uint32_t kRecord = psx::kSlotBase + kSlot * psx::kSlotStride;

class Engine final : public ServiceHost {
public:
    explicit Engine(RetailMemory& memory) : m_(memory) {}

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        if (name == "assert") {
            ++asserts;
            return 0;
        }
        if (name.compare(0, 7, "engine_") != 0) {
            return 0;
        }
        const auto address = static_cast<std::uint32_t>(std::strtoul(name.c_str() + 7, nullptr, 16));
        calls.push_back(address);
        switch (address) {
        case 0x004647c0:  // PSX_FinalizeEnvironment(slot): the parser's own tail
            finalize(args[0]);
            return 0;
        case 0x0047fd70:  // TRG_AllocObjectStorage2(size): zeroed
            return allocate(args[0]);
        case 0x004b20f0: {  // PSX_FindTrailingTables(raw): the word after the tag stream's terminator
            std::uint32_t at = args[0] + m_.u32(args[0] + 4);
            while (m_.u32(at) != 0xffffffffu) {
                at += m_.u32(at + 4) + 8;
            }
            return at + 4;
        }
        case 0x004b2030: {  // PSX_FindSceneMaterial(checksum)
            const auto it = materials.find(args[0]);
            return it == materials.end() ? 0 : it->second;
        }
        case 0x004b1f70: {  // PSX_AllocSceneMaterial(checksum)
            const std::uint32_t record = allocate(0x2c);
            m_.set32(record + 0x18, args[0]);
            materials[args[0]] = record;
            return record;
        }
        default:
            return 0;
        }
    }

    std::uint32_t allocate(std::uint32_t size) {
        const std::uint32_t at = next;
        next += (size + 0x3f) & ~0x3fu;
        return at;
    }

    RetailMemory& m_;
    std::function<void(std::uint32_t)> finalize;
    std::vector<std::uint32_t> calls;
    std::map<std::uint32_t, std::uint32_t> materials;
    std::uint32_t next = kHeap + 0x40000;
    int asserts = 0;
};

// The region: header, one object, one model with one face, a pulse tag, the trailing tables (one model name, one checksum, no palettes, no texture records).
void build_region(RetailMemory& m) {
    m.set32(kRaw + 0x04, 0x74);          // the tag stream
    m.set32(kRaw + 0x08, 1);             // objects
    const std::uint32_t object = kRaw + 0x0c;
    m.set16(object + 0x00, 0x1111);
    m.set16(object + 0x02, 0x2222);
    m.set32(object + 0x04, 0xaaaa0001);
    m.set32(object + 0x08, 0xbbbb0002);
    m.set32(object + 0x0c, 0xcccc0003);
    m.set16(object + 0x10, 0x33);
    m.set16(object + 0x12, 0x34);
    m.set16(object + 0x14, 0x35);
    m.set16(object + 0x16, 0);           // model 0
    m.set8(object + 0x18, 1);
    m.set8(object + 0x19, 2);
    m.set8(object + 0x1a, 3);
    m.set8(object + 0x1b, 4);
    m.set32(object + 0x20, 0x99);
    m.set32(kRaw + 0x30, 1);             // models
    m.set32(kRaw + 0x34, 0x38);          // the model's offset
    const std::uint32_t model = kRaw + 0x38;
    m.set8(model, 0x08);                 // the new style bounding box
    m.set16(model + 0x18, 0xffff);       // -> the default
    m.set16(model + 0x1a, 0xffff);       // unnamed
    m.set16(model + 0x06, 1);            // one face
    const std::uint32_t face = model + 0x1c;
    m.set32(face, (8u << 18) | 0x51);    // material, bit 4, bit 6; eight words long
    m.set32(face + 0x0c, 0x00010002);
    m.set32(face + 0x10, 0);             // the first checksum
    m.set32(kRaw + 0x74, 7);             // a pulse tag with eight bytes
    m.set32(kRaw + 0x78, 8);
    m.set32(kRaw + 0x84, 0xffffffffu);
    m.set32(kRaw + 0x88, 0xdddd);        // the model's name checksum
    m.set32(kRaw + 0x8c, 1);             // one material checksum
    m.set32(kRaw + 0x90, 0xabcd);
}

struct Rig {
    RetailMemory memory;
    RetailServices services;
    Engine host;
    PsxEnvKit kit;
    Rig() : host(memory), kit(memory, services) {
        memory.add_region(kGlobals, kGlobalsSize);
        memory.add_region(kHeap, kHeapSize);
        memory.set16(0x00533944, 0x1234);
        memory.set32(kRecord + psx::kSlotRaw, kRaw);
        build_region(memory);
        host.finalize = [this](std::uint32_t slot) { kit.finalize_environment(slot); };
        services.attach(&host);
    }
};

void test_parse_and_finalize() {
    Rig rig;
    auto& m = rig.memory;
    rig.kit.parse_environment(kSlot);

    expect_eq(rig.host.asserts, 0, "no assertion fails");
    expect_eq(m.u32(kRecord + psx::kSlotChecksums), kRaw + 0x88, "the trailing tables");
    expect_eq(m.u32(kRecord + psx::kSlotModels), kRaw + 0x34, "the model table");
    expect_eq(m.u32(kRaw + 0x34), kRaw + 0x38, "the model offset is relocated");
    expect_eq(m.u32(kRecord + psx::kSlotPulse), kRaw + 0x7c, "the pulse table");
    expect_eq(m.u32(kRecord + psx::kSlotWib), 0, "no wib table");
    expect_eq(m.u8(kRecord + psx::kSlotState), 1, "the region is usable");
    expect_eq(m.u8(kRecord + psx::kSlotTexturesInRam), 1, "textures in RAM");

    // the checksum list and the face now hold the scene material
    const std::uint32_t material = rig.host.materials.at(0xabcd);
    expect_eq(m.u32(kRaw + 0x90), material, "the checksum list holds the material");
    expect_eq(m.u32(kRaw + 0x38 + 0x1c + 0x10), material, "the face points at the material");
    expect_eq(m.u16(material + 0x10), 1, "one use");

    // PSX_FinalizeEnvironment: the model and its face
    const std::uint32_t model = kRaw + 0x38;
    expect_eq(m.u16(model + 0x18), 0x1234, "the default for a model without one");
    expect_eq(m.u16(kRecord + psx::kSlotModelCount), 1, "one unnamed model");
    expect_eq(m.u8(model), 0x18, "bit 4: every face has the high half bit");
    expect_eq(m.u32(model + 0x1c + 0x0c), 0x00010010, "the face's low half is shifted by three");
    expect_eq(m.u8(kRecord + 0x3a), 1, "the model summary byte");

    // the object table: one 0x4c record with the source copied in, linked, stamped, flagged from the model
    const std::uint32_t table = m.u32(kRecord + psx::kSlotObjects);
    expect_eq(m.u32(table - 4), 1, "one object");
    expect_eq(m.u16(table + 4), 0x1131, "+4 from the source's +0, with bit 5 set because the model has bit 4");
    expect_eq(m.u32(table + 8), 0xaaaa0001, "+8 from the source's +4");
    expect_eq(m.u32(table + 0x10), 0xcccc0003, "+0x10 from the source's +0xc");
    expect_eq(m.u8(table + 0x1c), 1, "bytes at +0x1c");
    expect_eq(m.u8(table + 0x1f), kSlot, "stamped with the slot");
    expect_eq(m.u32(table + 0x20), 0, "the last object has no next");
    expect_eq(m.u32(table + 0x24), 0x99, "+0x24 from the source's +0x20");
}

void test_existing_material_keeps_its_saturated_count() {
    Rig rig;
    auto& m = rig.memory;
    const std::uint32_t material = rig.host.allocate(0x2c);
    m.set32(material + 0x18, 0xabcd);
    m.set16(material + 0x10, 0xffff);
    rig.host.materials[0xabcd] = material;
    rig.kit.parse_environment(kSlot);
    expect_eq(m.u16(material + 0x10), 0xffff, "a count at its largest stays there");
    expect_eq(m.u32(kRaw + 0x90), material, "the existing material is used");
}

void test_a_region_without_geometry_asserts_only_when_queued_as_an_environment() {
    Rig rig;
    auto& m = rig.memory;
    m.set32(kRaw + 0x08, 0);              // no objects: the model count then sits at +0xc, its offset table at +0x10
    m.set32(kRaw + 0x0c, 1);
    m.set32(kRaw + 0x10, 0x38);
    m.set8(psx::kQueueBase + psx::kQueueKind, 1);
    rig.kit.parse_environment(kSlot);
    expect_eq(rig.host.asserts >= 1 ? 1 : 0, 1, "an environment queue entry with no objects fails the geometry check");
}

}  // namespace

int main() {
    test_parse_and_finalize();
    test_existing_material_keeps_its_saturated_count();
    test_a_region_without_geometry_asserts_only_when_queued_as_an_environment();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "psx env kit: all checks passed\n";
    return 0;
}
