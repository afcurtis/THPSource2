// The TRG helpers under the command interpreter on the retail memory model (milestone 10c, world_trig_commands.cpp), against a fake engine: the pulse by record type, the command
// list, the signal / activate / suspend / visible / kill senders and the payload skipper. What this pins is the Kit's reading of the disassembly (the calls, their arguments and the
// words left behind). TRG_SkipCommandPayload is also held against the value model's operand widths (src/trg/trg_runtime.cpp CommandCursor::skip_operands, validated on the shipped
// TRGs) for every opcode both know. The interpreter itself is replayed against retail by world_trig_commands_trace_test.cpp.
#include "world_kit.hpp"

#include "trg_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace opentony::runtime;
using namespace opentony::runtime::worldkit;

namespace {

int failures = 0;

void expect_eq(std::uint32_t actual, std::uint32_t expected, const std::string& what) {
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << ": 0x" << std::hex << actual << " != 0x" << expected << std::dec << "\n";
    }
}

void expect_calls(const RetailServices& services, const std::vector<RetailServices::Call>& expected, const std::string& what) {
    const auto& got = services.calls();
    if (got == expected) {
        return;
    }
    ++failures;
    std::cerr << "FAIL: " << what << ": calls differ\n  got:";
    for (const auto& c : got) {
        std::cerr << " " << c.name << "(";
        for (auto a : c.args) {
            std::cerr << std::hex << a << std::dec << ",";
        }
        std::cerr << ")";
    }
    std::cerr << "\n  expected:";
    for (const auto& c : expected) {
        std::cerr << " " << c.name << "(";
        for (auto a : c.args) {
            std::cerr << std::hex << a << std::dec << ",";
        }
        std::cerr << ")";
    }
    std::cerr << "\n";
}

using Call = RetailServices::Call;

constexpr std::uint32_t kGlobals = 0x00520000;      // .data .. .bss
constexpr std::uint32_t kGlobalsBytes = 0x60000;
constexpr std::uint32_t kHeap = 0x01000000;          // the records, the objects, the streams
constexpr std::uint32_t kHeapBytes = 0x20000;
constexpr std::uint32_t kLocals = 0x7ff0ea00;

constexpr std::uint32_t kRecords = 0x0056e210;
constexpr std::uint32_t kRecordCount = 0x0056e214;
constexpr std::uint32_t kAssertionLine = 0x00568638;
constexpr std::uint32_t kAssertionFile = 0x0056881c;
constexpr std::uint32_t kOwner = 0x0056a954;
constexpr std::uint32_t kPlayerRecord = 0x0056a960;
constexpr std::uint32_t kNumberOfPlayers = 0x00533f3c;
constexpr std::uint32_t kBaddyList = 0x0055f6bc;
constexpr std::uint32_t kGameList = 0x0056af40;
constexpr std::uint32_t kSuspendedList = 0x0056af44;
constexpr std::uint32_t kPowerUpList = 0x0056b830;
constexpr std::uint32_t kBitList = 0x0055f704;
constexpr std::uint32_t kBitFlag = 0x005244cc;

constexpr std::uint32_t kTable = kHeap;              // the record pointers
constexpr std::uint32_t kRecordArea = kHeap + 0x1000; // 0x40 bytes per record
constexpr std::uint32_t kObjects = kHeap + 0x8000;   // 0x200 bytes per object
constexpr std::uint32_t kLinks = kHeap + 0x10000;

struct World {
    RetailMemory memory;
    RetailServices services;
    WorldKit kit{memory, services};

    World() {
        memory.add_region(kGlobals, kGlobalsBytes);
        memory.add_region(kHeap, kHeapBytes);
        memory.add_region(kLocals, 0x100);
        memory.set32(kRecords, kTable);
        memory.set32(kRecordCount, 32);
        for (std::uint32_t k = 0; k < 32; ++k) {
            memory.set32(kTable + 4 * k, kRecordArea + 0x40 * k);
        }
    }
    std::uint32_t record(std::uint32_t index) const { return kRecordArea + 0x40 * index; }
    void type(std::uint32_t index, std::uint32_t type) { memory.set16(record(index), type); }
    std::uint32_t object(std::uint32_t n) const { return kObjects + 0x200 * n; }
    // a list of objects (linked through +0x20) made from these nodes (+0xb0)
    std::uint32_t list(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& objects_and_nodes) {
        std::uint32_t head = 0;
        for (auto it = objects_and_nodes.rbegin(); it != objects_and_nodes.rend(); ++it) {
            const std::uint32_t o = object(it->first);
            memory.set16(o + 0xb0, it->second);
            memory.set32(o + 0x20, head);
            head = o;
        }
        return head;
    }
    std::uint32_t links(const std::vector<std::uint32_t>& nodes, std::uint32_t at = kLinks) {
        memory.set16(at, static_cast<std::uint32_t>(nodes.size()));
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            memory.set16(at + 2 + 2 * static_cast<std::uint32_t>(k), nodes[k]);
        }
        return at;
    }
};

void test_stamp() {
    World w;
    w.kit.trg_send_pulse(3);   // a type-0 record: only the assertion location
    expect_eq(w.memory.u32(kAssertionLine), 0x332, "pulse: assertion line");
    if (w.memory.cstring(kAssertionFile) != "H:\\TonyHawk\\Pc2\\trig.cpp") {
        ++failures;
        std::cerr << "FAIL: pulse: assertion file " << w.memory.cstring(kAssertionFile) << "\n";
    }
    expect_calls(w.services, {}, "pulse to a type-0 record");
    w.memory.set32(kRecordCount, 4);
    w.kit.trg_send_pulse(10);  // past the record count: the assertion, then the record is read anyway (a type-0 one here)
    expect_calls(w.services, {{"assert", {0x00547a64}, ""}}, "pulse to a bad node");
}

void test_pulse() {
    {
        World w;
        w.type(2, 6);
        const std::uint32_t point = kHeap + 0x9000 - 0x100;
        w.memory.set32(point, 0x01234560);
        w.memory.set8(point + 7, 4);
        w.services.script("engine_004c5ac0", static_cast<std::int32_t>(point));
        w.kit.trg_send_pulse(2);
        expect_eq(w.memory.u8(point + 7), 5, "pulse: the command point's count");
        expect_eq(w.memory.u32(kAssertionLine), 0x354, "pulse: the command point assertion");
        expect_calls(w.services, {{"engine_004c5ac0", {2}, ""}, {"engine_004c5dc0", {0x01234560, 2, 0}, ""}}, "pulse to a command point");
    }
    {
        World w;
        w.type(1, 12);
        w.type(2, 14);
        w.type(3, 10);
        w.type(4, 11);
        w.type(5, 1);
        w.type(6, 5);
        w.type(7, 7);
        w.type(8, 2);
        w.type(9, 8);
        for (std::uint32_t k = 1; k <= 9; ++k) {
            w.kit.trg_send_pulse(k);
        }
        expect_calls(w.services, {{"engine_004bdbd0", {1}, ""}, {"engine_004bdbd0", {2}, ""}, {"engine_004aa420", {3}, ""}, {"engine_004aa420", {4}, ""},
                                  {"engine_004c5460", {5}, ""}, {"engine_004c5460", {6}, ""}, {"engine_004c5460", {7}, ""}},
                     "pulse by record type");
    }
    {
        World w;
        w.type(4, 0x1f5);
        const std::uint32_t angles = kHeap + 0x9000;
        for (std::uint32_t k = 0; k < 0x14; ++k) {
            w.memory.set8(angles + k, 0x10 + k);
        }
        w.memory.set32(kLocals + 0x40, 0x55555555);
        w.services.script("engine_004c8650", static_cast<std::int32_t>(angles));
        w.services.script("engine_00403d30", 0x01008800);
        w.kit.trg_send_pulse(4);
        expect_eq(w.memory.u32(kLocals + 0x40), 0, "pulse 0x1f5: the position vector cleared");
        expect_eq(w.memory.u32(kBitFlag), 1, "pulse 0x1f5: the sprite flag back on");
        expect_calls(w.services, {{"engine_004c8650", {kLocals + 0x40, 4}, ""}, {"engine_00403d30", {0x58}, ""},
                                  {"engine_0045c390", {0x01008800, kLocals + 0x40, 4, 0x1110, 0x1312, 0x1514, 0x1716, 0x18, 0x1a, 0x1c, 0x1e, 0x20, 0x22}, ""}},
                     "pulse to an extended record");
    }
}

void test_command_list() {
    World w;
    w.type(5, 12);
    w.type(9, 10);
    w.kit.trg_execute_command_list(w.links({5, 9}));
    w.kit.trg_execute_command_list(w.links({}));
    expect_calls(w.services, {{"engine_004bdbd0", {5}, ""}, {"engine_004aa420", {9}, ""}}, "command list");
}

void test_signal() {
    World w;
    w.type(3, 1);
    w.type(4, 7);
    w.type(5, 2);
    w.memory.set32(kBaddyList, w.list({{0, 3}, {1, 5}}));
    w.memory.set32(kGameList, w.list({{2, 4}, {3, 3}}));
    w.memory.set16(w.object(1) + 0x68, 0x40);
    w.kit.trg_send_signal_to_nodes(w.links({3, 4, 5}));
    expect_eq(w.memory.u16(w.object(0) + 0x68), 1, "signal: baddy of node 3");
    expect_eq(w.memory.u16(w.object(1) + 0x68), 0x40, "signal: node 5 is not an object node");
    expect_eq(w.memory.u16(w.object(2) + 0x68), 1, "signal: game object of node 4");
    expect_eq(w.memory.u16(w.object(3) + 0x68), 1, "signal: game object of node 3");
    expect_calls(w.services, {}, "signal");
    w.kit.trg_send_signal_to_nodes(w.links({}));
    expect_calls(w.services, {{"assert", {0x00547a8c}, ""}}, "signal from an unlinked node");
    expect_eq(w.memory.u32(kAssertionLine), 0x4f8, "signal: assertion line");
}

void test_activate_suspend() {
    World w;
    w.type(3, 1);
    w.type(4, 2);
    w.memory.set32(kBaddyList, w.list({{0, 3}}));
    w.memory.set32(kGameList, w.list({{1, 4}, {2, 3}}));
    w.memory.set32(kSuspendedList, w.list({{3, 3}, {4, 9}}));
    const std::uint32_t links = w.links({3, 4});
    w.kit.trg_apply_activation_or_suspend_list(links, 5);
    w.kit.trg_apply_activation_or_suspend_list(links, 4);
    w.kit.trg_apply_activation_or_suspend_list(links, 6);
    expect_calls(w.services, {{"engine_004802f0", {w.object(0), kBaddyList}, ""}, {"engine_004802f0", {w.object(2), kGameList}, ""},
                              {"engine_004803b0", {w.object(3)}, ""},
                              {"assert", {0x00547b00}, ""}, {"engine_004803b0", {w.object(3)}, ""}},
                 "activate and suspend");
}

void test_visible() {
    for (int players : {1, 2}) {
        for (bool second : {false, true}) {
            World w;
            const std::uint32_t first_skater = kHeap + 0xa000;
            w.memory.set32(kPlayerRecord, first_skater);
            w.memory.set32(kOwner, second ? first_skater + 0x4000 : first_skater);
            w.memory.set32(kNumberOfPlayers, static_cast<std::uint32_t>(players));
            w.type(10, 2);
            w.type(11, 2);
            w.memory.set16(w.record(11) + 2, 1);    // one payload word: the checksum at +6, rounded up to +8
            w.memory.set32(w.record(11) + 8, 0xabcd1234);
            w.type(12, 5);
            w.type(13, 3);
            w.type(14, 0x1f5);
            w.memory.set32(kPowerUpList, w.list({{0, 7}, {1, 12}}));
            w.memory.set16(w.object(1) + 4, 0x8000);
            const std::uint32_t item = kHeap + 0x9000;
            w.memory.set16(item + 4, 0x0100);
            // the node's links: GetNodeLinks of a type-2 record is the record + 2
            w.links({11, 12, 13, 14}, w.record(10) + 2);
            w.services.script("engine_004b1ef0", static_cast<std::int32_t>(item));
            w.services.script("engine_004b1ef0", static_cast<std::int32_t>(item));
            w.kit.trg_send_visible(10, 0);
            const std::uint32_t bits = players == 1 ? 0x41 : (second ? 0x01 : 0x40);
            expect_eq(w.memory.u16(item + 4), 0x0100 | bits, "visible 0: the item hidden");
            expect_eq(w.memory.u16(w.object(1) + 4), 0x8000 | bits, "visible 0: the power-up hidden");
            w.kit.trg_send_visible(10, 1);
            expect_eq(w.memory.u16(item + 4), 0x0100, "visible 1: the item shown");
            expect_eq(w.memory.u16(w.object(1) + 4), 0x8000, "visible 1: the power-up shown");
            expect_calls(w.services, {{"engine_004b1ef0", {0xabcd1234}, ""}, {"assert", {0x00547f00}, ""}, {"assert", {0x00547f00}, ""},
                                      {"engine_004b1ef0", {0xabcd1234}, ""}, {"assert", {0x00547f00}, ""}, {"assert", {0x00547f00}, ""}},
                         "visible");
        }
    }
}

void test_kill() {
    World w;
    const std::uint32_t skater = kHeap + 0xa000;
    w.memory.set32(kOwner, skater);
    w.type(20, 2);                              // the node the kill comes from: links at record + 2
    w.type(21, 9);
    w.memory.set16(w.record(21) + 2, 0);        // no payload words: the checksum at +4
    w.memory.set32(w.record(21) + 4, 0x11112222);
    w.type(22, 1);
    w.type(23, 10);
    w.type(24, 500);
    w.type(25, 1);
    w.type(26, 12);                             // a trick object: the platform's command list target
    w.links({21, 22, 23, 24, 25}, w.record(20) + 2);
    w.links({26}, w.record(25) + 6);            // node 25's own links (type 1: record + 6)
    // node 22: a baddy and a game object, killed through their vtables (slot 2 / slot 6)
    const std::uint32_t vtable = kHeap + 0x9800;
    for (std::uint32_t slot = 0; slot < 8; ++slot) {
        w.memory.set32(vtable + 4 * slot, 0x00470000 + 0x10 * slot);
    }
    w.memory.set32(kBaddyList, w.list({{0, 22}}));
    // node 25: an armed platform in the game list
    w.memory.set32(kGameList, w.list({{1, 22}, {2, 25}}));
    w.memory.set32(w.object(0), vtable);
    w.memory.set32(w.object(1), vtable);
    w.memory.set16(w.object(0) + 0xb4, 0xfffe);
    w.memory.set16(w.object(2) + 4, 0x100);
    w.memory.set32(w.object(2) + 0x178, 0x10);
    const std::uint32_t bit = kHeap + 0x9c00;
    w.memory.set32(kBitList, bit);
    w.memory.set8(bit + 0x3b, 2);
    w.memory.set16(bit + 0xe, 24);
    const std::uint32_t item = kHeap + 0x9000;
    w.services.script("engine_004b1ef0", static_cast<std::int32_t>(item));
    w.kit.trg_send_kill_from_node(20, 1);
    expect_eq(w.memory.u16(w.object(2) + 4), 0x120, "kill: the platform marked");
    expect_eq(w.memory.u8(w.object(2) + 0x171), 1, "kill: the platform's bounce flag");
    expect_eq(w.memory.u32(kAssertionLine), 0x332, "kill: the last assertion line is the platform's pulse");
    expect_calls(w.services, {{"engine_004b1ef0", {0x11112222}, ""}, {"engine_00448860", {item, 0, 0xffff}, ""},
                              {"engine_00470060", {w.object(0), 0xfffffffe, 0x0056af28, 0}, ""}, {"engine_00470060", {w.object(1), 0, 0x0056af28, 0}, ""},
                              {"engine_004aa410", {23}, ""}, {"engine_00403e40", {bit}, ""},
                              {"engine_004bdbd0", {26}, ""}, {"engine_0049f4c0", {w.object(2), skater, skater + 0x4c}, ""}},
                 "kill, mode 1");
    // mode 0: the item hidden, the objects through slot 2; the platform, already marked 0x20 in +0x178, is passed over and its node killed in the lists
    World z;
    z.type(20, 2);
    z.type(21, 9);
    z.memory.set16(z.record(21) + 2, 0);
    z.memory.set32(z.record(21) + 4, 0x11112222);
    z.type(25, 1);
    z.links({21, 25}, z.record(20) + 2);
    z.memory.set32(kGameList, z.list({{2, 25}}));
    z.memory.set32(z.object(2), vtable);
    z.memory.set16(z.object(2) + 4, 0x100);
    z.memory.set32(z.object(2) + 0x178, 0x30);
    for (std::uint32_t slot = 0; slot < 8; ++slot) {
        z.memory.set32(vtable + 4 * slot, 0x00470000 + 0x10 * slot);
    }
    z.memory.set16(item + 4, 0x0002);
    z.services.script("engine_004b1ef0", static_cast<std::int32_t>(item));
    z.kit.trg_send_kill_from_node(20, 0);
    expect_eq(z.memory.u16(item + 4), 0x43, "kill mode 0: the item hidden");
    expect_eq(z.memory.u16(z.object(2) + 4), 0x120, "kill mode 0: the platform marked");
    expect_calls(z.services, {{"engine_004b1ef0", {0x11112222}, ""}, {"engine_00470020", {z.object(2)}, ""}}, "kill, mode 0");
}

// the operand widths of the value model, for the opcodes it knows (the cursor aligns on its buffer index: the command sits at the Kit address's offset mod 4)
std::optional<std::size_t> model_width(std::uint16_t opcode, const std::vector<std::byte>& bytes, std::uint32_t base) {
    const std::size_t pad = base & 3;
    std::vector<std::byte> file(pad);
    file.insert(file.end(), bytes.begin(), bytes.end());
    opentony::trg::CommandCursor cursor(file, 0, pad, file.size());
    (void)cursor.read_u16();
    try {
        cursor.skip_operands(opcode, pad);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return cursor.position() - pad;
}

void test_skip_payload() {
    // retail's skipper never learnt these interpreter commands: it asserts and steps over the opcode word only
    const std::set<std::uint32_t> unknown_to_retail{0x96, 0x97, 0x98, 0x9d, 0x9e, 0x9f, 0xa1, 0xa2};
    int compared = 0;
    for (std::uint32_t base : {0x01004000u, 0x01004002u}) {
        for (std::uint32_t opcode = 0; opcode < 0x100; ++opcode) {
            World w;
            std::vector<std::uint8_t> bytes(0x80, 0x41);
            bytes[0] = static_cast<std::uint8_t>(opcode);
            bytes[1] = 0;
            if (opcode == 0x02) {
                const std::uint8_t names[] = {'a', 'b', 0, 0, 0, 0};
                std::copy(std::begin(names), std::end(names), bytes.begin() + 2);
            } else if (opcode == 0x85 || opcode == 0x8d) {
                // one box (aligned, six dwords) then the 0xff word
                const std::uint32_t first = base + 2 + (opcode == 0x8d ? 4 : 0);
                bytes[first - base] = 1;
                bytes[first - base + 1] = 0;
                const std::uint32_t end = ((first + 3) & ~3u) + 0x18;
                bytes[end - base] = 0xff;
                bytes[end - base + 1] = 0;
            } else {
                bytes[9] = 0;   // a string's end
            }
            w.memory.write(base, bytes);
            const std::uint32_t next = w.kit.trg_skip_command_payload(base);
            std::vector<std::byte> raw(bytes.size());
            for (std::size_t k = 0; k < bytes.size(); ++k) {
                raw[k] = static_cast<std::byte>(bytes[k]);
            }
            const auto width = model_width(static_cast<std::uint16_t>(opcode), raw, base);
            const bool asserted = !w.services.calls().empty();
            if (unknown_to_retail.count(opcode) != 0 || !width) {
                expect_eq(asserted, 1, "skip: retail asserts on opcode " + std::to_string(opcode));
                expect_eq(next, base + 2, "skip: an unknown opcode steps over its word, opcode " + std::to_string(opcode));
                if (asserted) {
                    expect_eq(w.memory.u32(kAssertionLine), 0x5d3, "skip: assertion line");
                }
                continue;
            }
            ++compared;
            expect_eq(asserted, 0, "skip: no assertion for opcode " + std::to_string(opcode));
            expect_eq(next - base, static_cast<std::uint32_t>(*width), "skip: width of opcode " + std::to_string(opcode) + " at base " + std::to_string(base & 3));
        }
    }
    World w;
    w.memory.set16(kHeap + 0x4000, 0xffff);
    expect_eq(w.kit.trg_skip_command_payload(kHeap + 0x4000), 0, "skip: the terminator");
    if (compared != 2 * 68) {   // the 68 opcodes of retail's table, at two alignments
        ++failures;
        std::cerr << "FAIL: skip: only " << compared << " opcodes compared with the value model\n";
    }
}

}  // namespace

int main() {
    try {
        test_stamp();
        test_pulse();
        test_command_list();
        test_signal();
        test_activate_suspend();
        test_visible();
        test_kill();
        test_skip_payload();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "world_trig_commands_test: ok\n";
    return 0;
}
