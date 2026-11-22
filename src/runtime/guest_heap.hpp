#pragma once

#include <cstdint>
#include <functional>
#include <map>

namespace opentony::runtime::guest {

// Milestone 11b (docs/PORT64_AUDIT.md D2): the C runtime heap of the standalone, inside guest memory. Every allocation retail makes through
// its C runtime (malloc, operator new: `crt_new` in src/hooks/kits/standalone_seams.cpp) and every surface's pixels (standalone_surfaces.cpp)
// come from here, so the block's address is a 32-bit guest address on the 32-bit and the 64-bit builds alike, and the addresses are the same
// in every run (the host heap's depended on the environment block and the Wine version: PHASE_5_SUMMARY.md, Known traps).
//
// The heap manages the guest range [base, base + size) and keeps its bookkeeping on the host side: nothing of it is in guest memory, so a
// game write past the end of a block cannot corrupt the heap. Each block is preceded by kGap unused bytes (where retail's C runtime keeps
// its block header), so a small overrun of one block lands there and not in the data of the next.
//
// Policy (deterministic): address-ordered first fit over the freed spans, coalesced with both neighbours; a span that reaches the top gives
// its bytes back to the top. Without a fitting free span the block is cut from the top, and `commit` is asked for the pages the top grows
// into (granules of kCommitGranule; the 32-bit and 64-bit hosts commit reserved pages, a test commits nothing). Blocks are not cleared: as
// with retail's malloc, a block holds what was there before (zero in fresh pages).
class GuestHeap {
public:
    // Make [address, address + size) readable and writable; false when that is impossible (the heap is then full).
    using Commit = std::function<bool(std::uint32_t address, std::uint32_t size)>;

    static constexpr std::uint32_t kAlign = 16;
    static constexpr std::uint32_t kGap = 16;
    static constexpr std::uint32_t kCommitGranule = 0x00100000;

    GuestHeap(std::uint32_t base, std::uint32_t size, Commit commit = {});

    // A block of at least `size` bytes (size 0 is one byte), aligned to kAlign; 0 when the heap cannot hold it.
    [[nodiscard]] std::uint32_t allocate(std::uint32_t size);
    // Frees a block allocate() returned; 0 is ignored. Any other address (never allocated, already freed, inside a block) throws
    // std::invalid_argument: retail's heap would be corrupt from there on.
    void release(std::uint32_t address);

    [[nodiscard]] bool owns(std::uint32_t address) const noexcept { return used_.count(address) != 0; }
    // The block's usable size (its request rounded up to kAlign, or a little more when the free span it came from had a remainder too
    // small for another block); 0 when `address` is not a block.
    [[nodiscard]] std::uint32_t size_of(std::uint32_t address) const noexcept;
    [[nodiscard]] bool in_range(std::uint32_t address) const noexcept { return address - base_ < size_; }

    struct Stats {
        std::uint64_t allocations = 0;
        std::uint64_t releases = 0;
        std::uint32_t live_blocks = 0;
        std::uint32_t live_bytes = 0;     // usable bytes of the live blocks
        std::uint32_t top = 0;            // first guest address never handed out
        std::uint32_t peak_top = 0;
        std::uint32_t committed_end = 0;  // end of the committed pages
        std::uint32_t free_spans = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;

    [[nodiscard]] std::uint32_t base() const noexcept { return base_; }
    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }

private:
    [[nodiscard]] std::uint32_t take_free(std::uint32_t& span);
    [[nodiscard]] std::uint32_t take_top(std::uint32_t span);
    void give_back(std::uint32_t start, std::uint32_t span);

    std::uint32_t base_;
    std::uint32_t size_;
    Commit commit_;
    std::uint64_t top_;        // [base_, top_) has been handed out at some time
    std::uint64_t committed_;  // [base_, committed_) is committed
    std::uint32_t peak_top_ = 0;
    std::map<std::uint32_t, std::uint32_t> free_;  // span start -> span bytes (gap included), address order, never adjacent
    std::map<std::uint32_t, std::uint32_t> used_;  // block address -> span bytes
    std::uint64_t allocations_ = 0;
    std::uint64_t releases_ = 0;
    std::uint32_t live_bytes_ = 0;
};

}  // namespace opentony::runtime::guest
