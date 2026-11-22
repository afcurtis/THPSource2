#pragma once

#include "player_spooler.hpp"
#include "guest_view.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace opentony::runtime {

// Phase 4.11a2: the skater view's spool state, player_spool.cpp (0x004a62b0..0x004a6bf0) on retail addresses.
//
// The player select screen's skater view embeds a 0x794-byte block that decides which of the twenty skaters' model files to keep in memory and which to
// load next (it is the `spool` pointer PlayerSelect passes to view_init_slots, view + 0x?; the block's own address is what the functions below take as `this`):
//   +0x000 + 4 * i   the loaded `.psx` file of skater i (a buffer the spooler handed over; 0 none)
//   +0x054 + 4 * i + 0x50 * j   the index of part j's name in the skater i's list of names (a dword; the part window reads its low byte)
//   +0x644 the queue entry of the skater's `.psx` (-1 none)   +0x648 the entry of its `.psh` part manifest   +0x64c a third entry (always -1)
//   +0x650 the skater being loaded   +0x654 + 4 * i the model of skater i is loaded   +0x6a4 + 4 * i the preview object of skater i exists
//   +0x6f4 + 4 * i the preview object   +0x744 + 4 * i the costume remembered for skater i   +0x794 the view the block belongs to (the owner)
// The owner holds the part-name lists the manifest parse fills: +0x8a0 / +0x920 the animation parts (32 name pointers and their count) and, per skater i,
// +0x924 + 0x84 * i / +0x9a4 + 0x84 * i the model parts.
//
// The skater manager (current character, costume), the file names the manager builds, the part-manifest parser and the preview object are services.
namespace skaterspool {

constexpr std::uint32_t kSlots = 0x000;
constexpr std::uint32_t kParts = 0x054;
constexpr std::uint32_t kPartStride = 0x50;
constexpr std::uint32_t kPrimary = 0x644;
constexpr std::uint32_t kManifest = 0x648;
constexpr std::uint32_t kThird = 0x64c;
constexpr std::uint32_t kCurrent = 0x650;
constexpr std::uint32_t kLoaded = 0x654;
constexpr std::uint32_t kPreviewMade = 0x6a4;
constexpr std::uint32_t kPreviews = 0x6f4;
constexpr std::uint32_t kCostumes = 0x744;
constexpr std::uint32_t kOwner = 0x794;

constexpr std::uint32_t kOwnerAnimNames = 0x8a0;
constexpr std::uint32_t kOwnerAnimCount = 0x920;
constexpr std::uint32_t kOwnerModelNames = 0x924;
constexpr std::uint32_t kOwnerModelCount = 0x9a4;
constexpr std::uint32_t kOwnerStride = 0x84;

constexpr std::uint32_t kSkaterManager = 0x0056c580;
constexpr std::uint32_t kPlayerCount = 0x0056a888;
constexpr std::uint32_t kRows = 0x0053a3b8;
constexpr std::uint32_t kRowStride = 0x170;
constexpr std::uint32_t kRowFace = 0x150;
constexpr std::uint32_t kRowFlags = 0x15c;
constexpr std::uint32_t kSkaters = 20;
constexpr std::uint32_t kPartCount = 19;

constexpr std::uint32_t kMsgBadSkater = 0x005372c4;
constexpr std::uint32_t kMsgNoModelFile = 0x005372fc;
constexpr std::uint32_t kMsgPossibleLeak = 0x00526874;
constexpr std::uint32_t kMsgNoAnimNames = 0x00535c54;
constexpr std::uint32_t kMsgNoModelNames = 0x00535c38;
constexpr std::uint32_t kMsgNoAnimParts = 0x00535c28;
constexpr std::uint32_t kMsgNoModelParts = 0x00535c18;
constexpr std::uint32_t kMsgNoSkater = 0x0053730c;
constexpr std::uint32_t kMsgNoSpoolManager = 0x00537318;
constexpr std::uint32_t kMsgInvalidData = 0x0052cf88;
constexpr std::uint32_t kMsgCustomSpooled = 0x0053735c;
constexpr std::uint32_t kMsgTooBig = 0x0053732c;
constexpr std::uint32_t kMsgIdNotEmpty = 0x00537398;

class SkaterSpool final {
public:
    SkaterSpool(RetailMemory& memory, RetailServices& services, pspool::PlayerSpooler& spooler) : m_(memory), s_(services), p_(spooler) {}

    void set_slot_buffer(std::uint32_t self, std::int32_t slot, std::uint32_t buffer);                          // 0x004a62b0
    [[nodiscard]] std::uint32_t preview(std::uint32_t self, std::int32_t slot);                                 // 0x004a6340
    [[nodiscard]] bool slot_usable(std::uint32_t self, std::int32_t slot);                                      // 0x004a63b0
    void install_model(std::uint32_t self, std::int32_t region, std::int32_t slot);                             // 0x004a6440
    void resolve_part_indices(std::uint32_t self, std::int32_t skater, std::uint32_t names, std::int32_t name_count,
                              std::uint32_t parts, std::int32_t part_count);                                    // 0x004a6560
    void apply_part_indices(std::uint32_t self, std::uint32_t target, std::int32_t skater);                     // 0x004a6720
    [[nodiscard]] std::uint32_t loaded_count(std::uint32_t self);                                               // 0x004a67a0
    void begin(std::uint32_t self);                                                                             // 0x004a67c0
    void update(std::uint32_t self, std::int32_t current);                                                      // 0x004a6880
    void finish_loads(std::uint32_t self);                                                                      // 0x004a68f0
    void load_resources(std::uint32_t self, std::int32_t current);                                              // 0x004a6bf0

    // 0x004a6e60 (native in PlayerSelect): something is queued
    [[nodiscard]] bool busy(std::uint32_t self) {
        return !(guest::read_scalar<std::uint32_t>(m_, self + kPrimary) == 0xffffffffu && guest::read_scalar<std::uint32_t>(m_, self + kManifest) == 0xffffffffu && guest::read_scalar<std::uint32_t>(m_, self + kThird) == 0xffffffffu);
    }

private:
    [[nodiscard]] static std::uint32_t row(std::int32_t skater) { return kRows + static_cast<std::uint32_t>(skater) * kRowStride; }
    void service(std::uint32_t address, std::vector<std::uint32_t> args = {}, std::string text = {}) {
        s_.note(pspool::service_name(address), std::move(args), std::move(text));
    }
    [[nodiscard]] std::uint32_t service_value(std::uint32_t address) { return static_cast<std::uint32_t>(s_.value(pspool::service_name(address))); }

    RetailMemory& m_;
    RetailServices& s_;
    pspool::PlayerSpooler& p_;
};

}  // namespace skaterspool
}  // namespace opentony::runtime
