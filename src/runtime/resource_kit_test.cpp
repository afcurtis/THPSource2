// The resource layer on the retail memory model (Phase 5.4a, 7b): the words retail's file layer, PRE manager and font table hold after each call, with the
// engine behind them (the file backend, the allocator, the texture layer) played by a host that answers by address. The calls and arguments are the ones the
// retail code makes (build/s4/dec_*.c, read from the decompilation); the typed models in src/assets/ are validated against retail traces, and
// the standalone spike (scripts/standalone_spike.py) runs this Kit over a real session image.
#include "resource_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::resourcekit;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

template <typename T>
void show(const T& value) {
    if constexpr (std::is_convertible_v<T, std::string>) {
        std::cerr << '"' << value << '"';
    } else {
        std::cerr << "0x" << std::hex << static_cast<unsigned long long>(value) << std::dec;
    }
}

template <typename T>
void expect_eq(T actual, T expected, const char* what) {
    if (!(actual == expected)) {
        ++failures;
        std::cerr << "FAIL: " << what << ": ";
        show(actual);
        std::cerr << " != ";
        show(expected);
        std::cerr << "\n";
    }
}

constexpr std::uint32_t kGlobals = 0x00510000;        // .rdata .. .bss of the file layer, the PRE manager and the font table
constexpr std::uint32_t kGlobalsSize = 0x00090000;
constexpr std::uint32_t kHeap = 0x01000000;
constexpr std::uint32_t kHeapSize = 0x00100000;

constexpr std::uint32_t engine(std::uint32_t address) { return address; }

// The engine, by address. Records every call by name.
class Engine final : public ServiceHost {
public:
    explicit Engine(RetailMemory& memory) : m_(memory) {}

    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        calls.push_back(name);
        if (name == "assert") {
            ++asserts;
            return 0;
        }
        if (name.compare(0, 7, "engine_") != 0 && name.compare(0, 6, "vcall_") != 0) {
            return 0;
        }
        const std::uint32_t address = static_cast<std::uint32_t>(std::strtoul(name.c_str() + name.find('_') + 1, nullptr, 16));
        switch (address) {
        case 0x004e75b0:  // FileIO_BackendOpen(path, mode, 0)
            opened.push_back(m_.cstring(args[0]));
            return opened_ok ? 3 : 0xffffffffu;
        case 0x004e79f0:  // FileIO_BackendSeek(handle, offset, origin)
            return args[2] == 2 ? file_size : args[1];
        case 0x0046f490:  // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
        case 0x00466b70:  // CClass_OpNew(size)
        case 0x005002fe:  // CRT operator new(size)
            return allocate(args[0]);
        case 0x004494b0:  // FileIO_Continue: the whole file arrives in one step
            if (m_.u32(kLoading) != 0) {
                const std::uint32_t buffer = m_.u32(kReadBuffer);
                for (std::uint32_t at = 0; at < file_size; ++at) {
                    m_.set8(buffer + at, static_cast<std::uint32_t>(0xA0 + at));
                }
                m_.set32(kLoading, 0);
            }
            return 0;
        case 0x0046f2e0:  // Mem_Copy(destination, source, size)
            m_.copy(args[0], args[1], args[2]);
            return 0;
        case 0x005002a1:  // CRT_sprintf(destination, "%s", text)
            {
                const std::string text = m_.cstring(args[2]);
                m_.write(args[0], {reinterpret_cast<const std::uint8_t*>(text.c_str()), text.size() + 1});
            }
            return 0;
        case 0x0048a490:  // GetFree16Slot
            return 2;
        case 0x004d98d0:  // D3DTex_CreateExternalRecord(pixels, width, height, palette, 16, 0)
            records.push_back({args[0], args[1], args[2]});
            return 0x1000 + static_cast<std::uint32_t>(records.size());
        case 0x00456d10:  // FontGlyph_ConstructImage(image, pixels, width, height, 0, 0, 4, id)
            images.push_back(args);
            return args[0];
        default:
            return 0;
        }
    }

    std::uint32_t allocate(std::uint32_t size) {
        const std::uint32_t at = heap_next;
        heap_next += (size + 0x3f) & ~0x3fu;
        return at;
    }

    struct Record {
        std::uint32_t pixels, width, height;
    };

    RetailMemory& m_;
    std::vector<std::string> calls;
    std::vector<std::string> opened;
    std::vector<Record> records;
    std::vector<std::vector<std::uint32_t>> images;
    std::uint32_t heap_next = kHeap;
    std::uint32_t file_size = 0x20;
    bool opened_ok = true;
    int asserts = 0;
};

struct Rig {
    RetailMemory memory;
    RetailServices services;
    Engine engine_host;
    ResourceKit kit;
    Rig() : engine_host(memory), kit(memory, services) {
        memory.add_region(kGlobals, kGlobalsSize);
        memory.add_region(kHeap, kHeapSize);
        memory.add_region(0x006a0000, 0x1000);   // the texture record list
        memory.set32(kFontVtable, 0x00449cb0);   // slot 0: Font_ScalarDelete
        services.attach(&engine_host);
    }
    void text(std::uint32_t at, const std::string& value) { memory.write(at, {reinterpret_cast<const std::uint8_t*>(value.c_str()), value.size() + 1}); }
};

constexpr std::uint32_t kName = 0x00570000;   // a name the test passes
constexpr std::uint32_t kPre = kHeap + 0x80000;

void test_backend_file() {
    Rig rig;
    rig.text(kName, "test.bin");
    rig.text(kSubDirName, "sub\\");
    const std::uint32_t size = rig.kit.open_resource(kName);
    expect_eq(size, 0x20u, "open answers the backend's size");
    expect_eq(rig.memory.cstring(kFileName), std::string("sub\\test.bin"), "FileName holds sub directory and name");
    expect_eq(rig.engine_host.opened.size(), std::size_t{1}, "one backend open");
    expect_eq(rig.engine_host.opened[0], std::string("sub\\test.bin"), "the backend path");
    expect_eq(rig.memory.u16(kReadHandle), std::uint16_t{3}, "the handle");
    expect_eq(rig.memory.u32(kReadSize), 0x20u, "bytes to read");
    expect_eq(rig.memory.u32(kFileSize), 0x20u, "FileSize");
    expect_eq(rig.memory.u32(kReadPending), 1u, "a file waits to be loaded");
    expect_eq(rig.memory.u32(kLastName), kName, "the last name");
    expect_eq(rig.memory.cstring(kNameCopy), std::string("test.bin"), "the name copy");
    expect_eq(rig.memory.u32(kLoadFromPre), 0u, "not from a PRE");

    const std::uint32_t destination = kHeap + 0x1000;
    rig.kit.load_resource(destination);
    expect_eq(rig.memory.u32(kReadBuffer), destination, "the read buffer");
    expect_eq(rig.memory.u8(destination), std::uint8_t{0xA0}, "the engine's step delivered the file");
    rig.kit.sync_resource();
    expect_eq(rig.memory.u32(kLoading), 0u, "loading is over after sync");
    expect_eq(rig.engine_host.asserts, 0, "no assertion");
}

void test_embedded_resource() {
    Rig rig;
    // a PRE buffer in slot 2: one record "a.dat" with a 6-byte payload (padded to 8)
    rig.kit.create_manager();
    const std::uint32_t manager = rig.memory.u32(kPreManager);
    expect(manager != 0, "the manager exists");
    expect_eq(rig.memory.u32(manager), kPreVtable, "the manager's vtable");
    expect_eq(rig.memory.u32(manager + 4 + 8), 0u, "slot 2 is empty");
    expect_eq(rig.memory.cstring(manager + 0x44), rig.memory.cstring(kPreSlotName), "the blank name");
    rig.memory.set32(kPre, 1);
    rig.text(kPre + 4, "a.dat");
    rig.memory.set32(kPre + 4 + 8, 6);
    const std::uint8_t payload[6] = {1, 2, 3, 4, 5, 6};
    rig.memory.write(kPre + 4 + 8 + 4, payload);
    rig.memory.set32(manager + 4 + 8, kPre);

    rig.text(kName, "A.DAT");
    expect_eq(rig.kit.open_resource(kName), 6u, "an embedded resource: its size (the name is matched in lower case)");
    expect_eq(rig.memory.u32(kLoadFromPre), 1u, "from a PRE");
    expect_eq(rig.memory.u32(kPreData), kPre + 4 + 8 + 4, "where the payload is");
    expect_eq(rig.memory.u32(kPreSize), 6u, "its size");
    expect(rig.engine_host.opened.empty(), "the backend was not asked");
    const std::uint32_t destination = kHeap + 0x2000;
    rig.kit.load_resource(destination);
    expect_eq(rig.memory.u8(destination + 5), std::uint8_t{6}, "the payload was copied");
    expect_eq(rig.memory.u32(kReadPending), 0u, "nothing waits");
    rig.kit.sync_resource();
    expect_eq(rig.memory.u32(kLoadFromPre), 0u, "the flag is cleared by sync");

    // a bitmap is never taken from a PRE, and neither is a name of sixteen characters or more
    rig.text(kName, "a.bmp");
    rig.kit.find_embedded(manager, kName, kPreSize);
    (void)rig.kit.open_resource(kName);
    expect_eq(rig.engine_host.opened.size(), std::size_t{1}, "a bitmap goes to the backend");
    expect_eq(rig.engine_host.asserts, 0, "no assertion");
}

void test_load_into_slot() {
    Rig rig;
    rig.kit.create_manager();
    const std::uint32_t manager = rig.memory.u32(kPreManager);
    rig.text(kName, "level.pre");
    rig.kit.load(manager, kName, 1);
    const std::uint32_t buffer = rig.memory.u32(manager + 4);
    expect(buffer != 0, "the first slot holds the buffer");
    expect_eq(rig.memory.u8(buffer), std::uint8_t{0xA0}, "the file is in it");
    expect_eq(rig.memory.cstring(manager + 0x44), std::string("level.pre"), "the slot is named after the file");
    expect_eq(rig.memory.u32(kLoading), 0u, "PRE_Load synchronises");
    expect_eq(rig.engine_host.asserts, 0, "no assertion");
}

void test_texture_records() {
    Rig rig;
    const std::uint32_t first = rig.kit.alloc_record();
    const std::uint32_t second = rig.kit.alloc_record();
    expect(first != 0 && second != first, "two records");
    expect_eq(rig.memory.u32(kRecordHead), first, "the list starts at the first");
    expect_eq(rig.memory.u32(kRecordTail), second, "and ends at the last");
    expect_eq(rig.memory.u32(first + 0x24), second, "the first links to the second");
    expect_eq(rig.memory.u32(second + 0x28), first, "and the second back");
    expect_eq(rig.memory.u32(second + 0x24), 0u, "the last has no next");
    expect_eq(rig.memory.u32(kRecordCount), 2u, "counted");
    expect_eq(rig.memory.u32(first + 0x10), 0u, "zeroed");
}

void test_sub_dir_rules() {
    Rig rig;
    rig.text(kName, "sub\\");
    rig.kit.set_sub_dir(kName);
    expect_eq(rig.memory.cstring(rig.kit.get_sub_dir()), std::string("sub\\"), "the sub directory is stored");
    expect_eq(rig.engine_host.asserts, 0, "a good sub directory asserts nothing");
    rig.text(kName, "nobackslash");
    rig.kit.set_sub_dir(kName);
    expect_eq(rig.engine_host.asserts, 1, "a sub directory must end in a backslash");
    rig.text(kName, "0123456789abcdef\\");
    rig.kit.set_sub_dir(kName);
    expect_eq(rig.engine_host.asserts, 2, "and be shorter than sixteen characters");
    rig.text(kName, "");
    rig.kit.set_sub_dir(kName);
    expect_eq(rig.engine_host.asserts, 2, "the empty sub directory is fine");

    rig.text(kName, "x");
    rig.memory.set32(kDirLength, 3);
    rig.memory.write(kFileName, {reinterpret_cast<const std::uint8_t*>("ab\\"), 3});
    rig.kit.add_dir_to_file_name(kName);
    expect_eq(rig.memory.cstring(kFileName), std::string("ab\\x"), "AddDirToFileName writes after the base directory");

    rig.text(0x00570100, "HaYsTaCk");
    rig.text(0x00570200, "stack");
    expect_eq(rig.kit.find_no_case(0x00570100, 0x00570200), 0x00570100u + 3, "String_FindNoCase finds the needle");
    rig.text(0x00570200, "zzz");
    expect_eq(rig.kit.find_no_case(0x00570100, 0x00570200), 0u, "and answers 0 when it is not there");
}

void test_fonts() {
    Rig rig;
    // a font of two glyphs: the count, two 16-byte records (packed size, height, 0, width), the palette (16 words), then the 4-bit pixels
    const std::uint32_t data = kHeap + 0x40000;
    rig.memory.set32(data, 2);
    const std::uint32_t records[2][4] = {{4, 2, 0, 4}, {6, 3, 0, 5}};   // [0]=packed size, [1]=height, [2]=?, [3]=width
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < 4; ++k) {
            rig.memory.set32(data + 4 + 16 * i + 4 * k, records[i][k]);
        }
    }
    for (int k = 0; k < 16; ++k) {
        rig.memory.set16(data + 4 + 32 + 2 * k, 0x100 + k);
    }
    rig.text(kName, "tiny.fnt");
    const std::uint32_t font = rig.kit.font_create(data, kName);
    expect(font != 0, "a font object");
    expect_eq(rig.memory.u32(kFontTable), font, "the first free slot holds it");
    expect_eq(rig.memory.cstring(kFontNames), std::string("tiny.fnt"), "named after the file");
    expect_eq(rig.memory.u32(font), kFontVtable, "the vtable");
    expect_eq(rig.memory.u32(font + 0x38), 2u, "the glyph count");
    expect_eq(rig.memory.u32(font + 0x30), 0x1000u, "the scale");
    expect_eq(rig.engine_host.images.size(), std::size_t{3}, "two glyphs and the blank one");
    const std::uint32_t pixels = data + 2 * 16 + 36;
    expect_eq(rig.engine_host.images[0][1], pixels, "the first glyph's pixels follow the palette");
    expect_eq(rig.engine_host.images[0][2], 4u, "its width");
    expect_eq(rig.engine_host.images[0][3], 2u, "its height");
    expect_eq(rig.engine_host.images[1][1], pixels + 2 * 2 * 4, "the second glyph's pixels follow the first (2 bytes per cell, height 2 x packed 4)");
    const std::uint32_t table = rig.memory.u32(font + 0x34);
    expect_eq(rig.memory.u8(table + 4), std::uint8_t{4}, "the first glyph's width");
    expect_eq(rig.memory.u8(table + 5), std::uint8_t{2}, "the first glyph's height");
    expect_eq(rig.memory.u8(rig.memory.u32(table) + 5), std::uint8_t{1}, "the glyph image is marked");
    expect_eq(rig.memory.u32(font + 0x3c), ((rig.memory.u32(kPalX) >> 4) & 0x3fu) | ((2 + rig.memory.u32(kPalY)) * 0x40), "the palette id");
    expect_eq(rig.memory.u32(kLastClut), 0x1000u + 3, "the last texture record");

    expect_eq(rig.kit.font_find(kName), font, "Font_Find");
    rig.kit.font_unload(kName);
    expect_eq(rig.memory.u32(kFontTable), 0u, "Font_Unload empties the slot");
    expect_eq(rig.engine_host.asserts, 0, "no assertion");
    expect_eq(rig.kit.font_find(kName), 0u, "retail clears the pointer and keeps the name, so the slot is still found");
    rig.text(kName, "other.fnt");
    (void)rig.kit.font_find(kName);
    expect_eq(rig.engine_host.asserts, 1, "a font that was never loaded asserts");
}

}  // namespace

int main() {
    test_backend_file();
    test_embedded_resource();
    test_load_into_slot();
    test_texture_records();
    test_sub_dir_rules();
    test_fonts();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "resource kit: all checks passed\n";
    return 0;
}
