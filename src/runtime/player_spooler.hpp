#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.11a2: the player spool manager of spooler.cpp (0x004b50e0..0x004b5b6d) on retail addresses.
//
// One 0xa10-byte manager object (the pointer is kept at 0x0056db4c) holds a queue of 0x40 entries of 0x28 bytes. The select screens queue the `.psh` and `.psx`
// files of a skater's model through it and the screen's update loop steps it. Entry i starts at `manager + 4 + i * 0x28`:
//   +0x00 processed (the file has been read or the region parsed)      +0x01 name (the stored copy; a `.psx` name ends at its dot)
//   +0x14 kind (0 a raw file kept in `buffer`, 1 a PSX region held by the PSX spooler)    +0x18 the region slot the PSX spooler answered (-1 none)
//   +0x1c the buffer the file was read into     +0x20 the heap it was read into (0 or 1)     +0x24 the size to reserve (-1: the file's own size)
// and the manager keeps +0xa04 the number of entries queued, +0xa08 the entry being loaded and +0xa0c the mode (0 idle, 1 loading, 2 waiting to be finalised).
// The byte at 0x005615dc says the manager is stepping the file layer (FileIO_Continue asserts it is clear).
//
// Everything is read and written at its retail address; the engine (allocator, file layer, the PSX spooler and region parser) is a named service whose
// result the replay scripts. The PSX region spooler (psx_spool.*) is another native model: here its entry points are services.
namespace pspool {

constexpr std::uint32_t kGlobalManager = 0x0056db4c;
constexpr std::uint32_t kStepping = 0x005615dc;      // byte: the manager is using the file layer
constexpr std::uint32_t kLoading = 0x005615d0;       // the file layer's "a read is in progress" word
constexpr std::uint32_t kVtManager = 0x00519738;
constexpr std::uint32_t kVtBase = 0x005184b4;

constexpr std::uint32_t kEntries = 4;
constexpr std::uint32_t kEntryStride = 0x28;
constexpr std::uint32_t kEntryCount = 0x40;
constexpr std::uint32_t kCount = 0xa04;
constexpr std::uint32_t kCurrent = 0xa08;
constexpr std::uint32_t kMode = 0xa0c;
constexpr std::uint32_t kManagerSize = 0xa10;

constexpr std::uint32_t kEntryProcessed = 0x00;
constexpr std::uint32_t kEntryName = 0x01;
constexpr std::uint32_t kEntryKind = 0x14;
constexpr std::uint32_t kEntryHandle = 0x18;
constexpr std::uint32_t kEntryBuffer = 0x1c;
constexpr std::uint32_t kEntryHeap = 0x20;
constexpr std::uint32_t kEntryPad = 0x24;

// assertion messages (the address of the format string names the assertion)
constexpr std::uint32_t kMsgAlreadyCreated = 0x00538374;
constexpr std::uint32_t kMsgTooManyItems = 0x005383e4;
constexpr std::uint32_t kMsgIrregularName = 0x005383cc;
constexpr std::uint32_t kMsgUnrecognizedHeap = 0x005383b0;
constexpr std::uint32_t kMsgNotInQueue = 0x00538438;
constexpr std::uint32_t kMsgNoDataForLoaded = 0x00538454;
constexpr std::uint32_t kMsgNotAvailable = 0x00538480;
constexpr std::uint32_t kMsgWrongFileType = 0x005384a8;
constexpr std::uint32_t kMsgStillLoading = 0x005384d8;
constexpr std::uint32_t kMsgNoBufferToKill = 0x005384f4;
constexpr std::uint32_t kMsgFileNotFound = 0x0052b948;
constexpr std::uint32_t kMsgPadTooSmall = 0x005383fc;
constexpr std::uint32_t kMsgNotEnoughMemory = 0x0052b904;

inline std::string service_name(std::uint32_t address) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "engine_%08x", address);
    return buffer;
}

class PlayerSpooler final {
public:
    PlayerSpooler(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    std::uint32_t create();                                                       // 0x004b50e0 (returns the manager)
    void destroy();                                                               // 0x004b5190
    void ring_order(std::uint32_t out, std::int32_t count, std::int32_t start);   // 0x004b51b0 (writes `count` indices, nearest to `start` first)
    [[nodiscard]] static std::vector<std::int32_t> ring_values(std::int32_t count, std::int32_t start);
    std::uint32_t construct(std::uint32_t self);                                  // 0x004b5200
    std::uint32_t scalar_delete(std::uint32_t self, std::uint32_t flags);         // 0x004b5240
    void destruct(std::uint32_t self);                                            // 0x004b5260
    void reset(std::uint32_t self);                                               // 0x004b52b0
    void process_pending(std::uint32_t self);                                     // 0x004b5300
    void finalize(std::uint32_t self);                                            // 0x004b5350
    std::int32_t queue(std::uint32_t self, std::uint32_t name, std::uint32_t force_raw, std::uint32_t heap, std::uint32_t pad);   // 0x004b5370
    std::int32_t queue_text(std::uint32_t self, const std::string& name, std::uint32_t force_raw, std::uint32_t heap, std::uint32_t pad);
    void force_finalize(std::uint32_t self);                                      // 0x004b5580
    [[nodiscard]] bool idle(std::uint32_t self);                                  // 0x004b55a0
    void pump(std::uint32_t self);                                                // 0x004b55c0
    void load_queued_item(std::uint32_t self);                                    // 0x004b5620
    void advance_entry(std::uint32_t self);                                       // 0x004b57d0
    [[nodiscard]] bool has_resource(std::uint32_t self, std::uint32_t name);      // 0x004b5810
    [[nodiscard]] bool acquire(std::uint32_t self, std::uint32_t name);           // 0x004b5830
    [[nodiscard]] bool entry_ready(std::uint32_t self, std::int32_t index);       // 0x004b58a0
    std::uint32_t take_buffer_named(std::uint32_t self, std::uint32_t name);      // 0x004b5920
    std::uint32_t take_buffer(std::uint32_t self, std::int32_t index);            // 0x004b5990
    void release_entry(std::uint32_t self, std::int32_t index);                   // 0x004b5a00
    [[nodiscard]] std::int32_t find_entry(std::uint32_t self, std::uint32_t name, bool loaded_only);   // 0x004b5b00

    // a call through an object's vtable slot, reported as the service the slot's address names
    void virtual_call(std::uint32_t object, std::uint32_t slot, std::vector<std::uint32_t> args = {});

private:
    [[nodiscard]] static std::uint32_t entry(std::uint32_t self, std::int32_t index) {
        return self + kEntries + static_cast<std::uint32_t>(index) * kEntryStride;
    }
    void service(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        s_.note(service_name(address), std::move(args), std::move(text));
    }
    [[nodiscard]] std::uint32_t service_value(std::uint32_t address) { return static_cast<std::uint32_t>(s_.value(service_name(address))); }

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace pspool
}  // namespace opentony::runtime
