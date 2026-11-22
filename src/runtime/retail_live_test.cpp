// Phase 5.0: the live forms of RetailMemory (the process's own memory at the retail addresses) and
// RetailServices (each reported call made for real by a host). Live memory needs the addresses to
// be real, so the test maps one page at a fixed address below 4 GiB.
#include "pc_platform.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"
#include "tests/test_check.hpp"

#include <sys/mman.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using opentony::runtime::MemoryFault;
using opentony::runtime::RetailMemory;
using opentony::runtime::RetailServices;
using opentony::runtime::ServiceFault;
using opentony::runtime::ServiceHost;

constexpr std::uint32_t kPage = 0x3ff00000;
constexpr std::size_t kPageBytes = 0x1000;

bool probe(std::uint32_t address, std::size_t size, bool write) {
    (void)write;
    return address >= kPage && address + size <= kPage + kPageBytes;
}

struct Host final : ServiceHost {
    std::vector<std::string> calls;
    std::uint32_t call(const std::string& name, const std::vector<std::uint32_t>& args, const std::string&) override {
        calls.push_back(name);
        return args.empty() ? 0 : args[0] * 2;
    }
};

// a Kit-shaped user of the protected PcKit helpers
struct Probe final : opentony::runtime::pcplatform::PcKit {
    using PcKit::PcKit;
    std::uint32_t value_of(std::uint32_t address, std::uint32_t arg) { return engine_value(address, {arg}); }
    std::uint32_t answer(std::uint32_t address) { return engine_answer(address, {}, 7); }
};

template <typename F>
bool throws_memory_fault(F f) {
    try {
        f();
    } catch (const MemoryFault&) {
        return true;
    }
    return false;
}

template <typename F>
bool throws_service_fault(F f) {
    try {
        f();
    } catch (const ServiceFault&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    void* page = mmap(reinterpret_cast<void*>(static_cast<std::uintptr_t>(kPage)), kPageBytes, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (page != reinterpret_cast<void*>(static_cast<std::uintptr_t>(kPage))) {
        std::cout << "SKIP live memory: cannot map a page at 0x3ff00000\n";
        return 0;
    }
    auto* raw = static_cast<std::uint8_t*>(page);

    // live memory is the process's memory at the same address, both ways
    RetailMemory live;
    live.make_live(&probe, false);
    CHECK(live.live());
    live.set32(kPage + 0x10, 0x11223344);
    CHECK(raw[0x10] == 0x44 && raw[0x13] == 0x11);
    raw[0x20] = 0xfe;
    CHECK(live.u8(kPage + 0x20) == 0xfe);
    CHECK(live.s8(kPage + 0x20) == -2);
    live.copy(kPage + 0x40, kPage + 0x10, 4);
    CHECK(live.u32(kPage + 0x40) == 0x11223344);
    live.fill(kPage + 0x80, 0x5a, 3);
    CHECK(live.u32(kPage + 0x80) == 0x005a5a5a);
    // `contains` asks the probe; without per-access checks an access is not checked
    CHECK(live.contains(kPage, 4));
    CHECK(!live.contains(kPage + kPageBytes - 2, 4));
    // live memory has no regions to add
    CHECK(throws_memory_fault([&] { live.add_region(kPage, 0x10); }));
    // an aliased window (the replays' scratch area) is somewhere else in the process
    live.alias_live(0x7ff00000, 0x100, raw + 0x800);
    live.set32(0x7ff00010, 0xcafef00d);
    CHECK(raw[0x810] == 0x0d && raw[0x813] == 0xca);
    CHECK(live.u32(0x7ff00010) == 0xcafef00d);
    CHECK(live.live_address(0x7ff00010) == kPage + 0x810);
    CHECK(live.live_address(kPage + 4) == kPage + 4);
    CHECK(live.contains(0x7ff000fc, 4));
    CHECK(!live.contains(0x7ff000fe, 4));
    CHECK(throws_memory_fault([&] { (void)live.u32(0x7ff000fe); }));

    // with every access checked, the probe turns a bad access into a MemoryFault
    RetailMemory guarded;
    guarded.make_live(&probe, true);
    CHECK(guarded.u32(kPage + 0x10) == 0x11223344);
    CHECK(throws_memory_fault([&] { (void)guarded.u32(kPage + kPageBytes - 2); }));
    CHECK(throws_memory_fault([&] { guarded.set8(kPage - 1, 0); }));

    // 11b: over a guest arena, guest address A is host address arena_base + A (here a mapping anywhere stands for guest 0x00400000..)
    {
        constexpr std::uint32_t kGuest = 0x00400000;
        void* host_pages = mmap(nullptr, kPageBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(host_pages != MAP_FAILED);
        auto* bytes = static_cast<std::uint8_t*>(host_pages);
        RetailMemory arena;
        arena.make_live(nullptr, false, reinterpret_cast<std::uintptr_t>(bytes) - kGuest);
        CHECK(arena.arena_base() == reinterpret_cast<std::uintptr_t>(bytes) - kGuest);
        arena.set32(kGuest + 8, 0x01020304);
        CHECK(bytes[8] == 0x04 && bytes[11] == 0x01);
        bytes[0x40] = 'h';
        bytes[0x41] = 'i';
        bytes[0x42] = 0;
        CHECK(arena.cstring(kGuest + 0x40) == "hi");
        CHECK(arena.contains(kGuest, 4));   // no probe: everything is taken as mapped
        // an alias only where host pointers are guest addresses
        CHECK(throws_memory_fault([&] { arena.alias_live(0x7ff00000, 0x100, bytes); }));
        munmap(host_pages, kPageBytes);
    }

    // the model is unchanged: an address outside every region faults
    RetailMemory model;
    model.add_region(0x1000, 0x10);
    CHECK(!model.live());
    CHECK(throws_memory_fault([&] { (void)model.u32(kPage + 0x10); }));

    // live services: a call is made by the host and nothing is recorded; its value is the result
    Host host;
    RetailServices services;
    services.attach(&host);
    CHECK(services.live());
    services.note("engine_00400000", {21});
    CHECK(services.value("engine_00400000") == 42);
    CHECK(services.calls().empty());
    CHECK(host.calls.size() == 1);
    // a value no call answers (or asked twice) is a replay-only pattern
    CHECK(throws_service_fault([&] { (void)services.value("engine_00400000"); }));
    services.note("engine_00400010", {1});
    CHECK(throws_service_fault([&] { (void)services.value("engine_00400020"); }));

    // the Kit helpers on top
    RetailMemory kit_memory;
    kit_memory.make_live(&probe, false);
    Probe kit(kit_memory, services);
    CHECK(kit.value_of(0x00400030, 5) == 10);
    CHECK(host.calls.back() == "engine_00400030");
    CHECK(throws_service_fault([&] { (void)kit.answer(0x00400040); }));

    // the model form of the same helpers still scripts values
    RetailServices replay;
    replay.script("engine_00400030", 99);
    RetailMemory replay_memory;
    Probe replay_kit(replay_memory, replay);
    CHECK(replay_kit.value_of(0x00400030, 5) == 99);
    CHECK(replay.calls().size() == 1 && replay.calls()[0].args == std::vector<std::uint32_t>{5});
    CHECK(replay_kit.answer(0x00400040) == 7);

    munmap(page, kPageBytes);
    std::cout << "retail live memory and services: ok\n";
    return 0;
}
