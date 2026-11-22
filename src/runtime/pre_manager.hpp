#pragma once

#include "player_spooler.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.11a2: the rest of pre.cpp on retail addresses: the PRE manager's destructor, registering a buffer under a name, the name lookup, the debugging dump, and the
// one-liners after it (src/assets/pre_runtime.* models the constructor, loader and unloader on a different, value-owned model).
//
// The manager object (0x144 bytes, the pointer kept at 0x0056b834): +0 vtable, +4 sixteen pointers to the PRE buffers it owns (0 free), +0x44 sixteen sixteen-byte names.
namespace pre {

constexpr std::uint32_t kGlobalManager = 0x0056b834;
constexpr std::uint32_t kVtManager = 0x005196a0;
constexpr std::uint32_t kSlots = 4;
constexpr std::uint32_t kNames = 0x44;
constexpr std::uint32_t kSlotCount = 16;
constexpr std::uint32_t kNameStride = 16;

constexpr std::uint32_t kMsgNoManager = 0x0053746c;
constexpr std::uint32_t kMsgNoBuffer = 0x005374d8;
constexpr std::uint32_t kMsgNoFilename = 0x005269a0;
constexpr std::uint32_t kMsgAlreadyLoaded = 0x005374bc;
constexpr std::uint32_t kMsgOutOfSlots = 0x005374a4;

// the strings the dump prints (the log service reports the address of the format)
constexpr std::uint32_t kDumpBanner = 0x00537564;
constexpr std::uint32_t kDumpChecking = 0x00537550;
constexpr std::uint32_t kDumpAddress = 0x00537534;
constexpr std::uint32_t kDumpFooter = 0x0053751c;
constexpr std::uint32_t kDumpNotEmpty = 0x00537500;

class PreManager final {
public:
    PreManager(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    void destroy_global();                                                          // 0x004a8f90
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flags);           // 0x004a9060
    void destruct(std::uint32_t self);                                              // 0x004a9080
    void register_buffer(std::uint32_t self, std::uint32_t buffer, std::uint32_t name);   // 0x004a91d0
    [[nodiscard]] bool find(std::uint32_t self, std::uint32_t name);                // 0x004a9510
    void dump(std::uint32_t self, std::uint32_t label);                             // 0x004a9580
    void dump_global(std::uint32_t label);                                          // 0x004a95f0
    [[nodiscard]] std::uint32_t return_zero() { return 0; }                         // 0x004a9610
    void nothing_a() {}                                                             // 0x004a9620
    void nothing_b() {}                                                             // 0x004a9630
    void nothing_c() {}                                                             // 0x004a9640

private:
    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace pre
}  // namespace opentony::runtime
