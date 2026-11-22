#pragma once

#include "pc_platform.hpp"

#include <cstdint>

namespace opentony::runtime::heap {

// Phase 4.14b: the game's heap (mem.cpp, 0x0046e7f0..0x0046f52f). Two zones of retail memory are cut into blocks; a block is an 8-byte header (`size << 4 | tag` and, in a free block, the next free
// block) followed by `size` bytes of payload. The free blocks of a zone form a list ordered by address (`kFreeHead[zone]`), `AddToFreeList` merges a block that touches its neighbours, and
// `kUsed[zone]` counts the bytes handed out (header included). Zone 1 also keeps a "low memory" flag: set when its use reaches 98 % of the zone. Like the rest of Phase 4 the functions work
// on the retail memory at the retail addresses, so a captured image replays byte for byte, down to the junk in the tag nibble of a free block (which the arithmetic of the merge carries into
// the size field, exactly as retail's does).
//
// The 4-byte shift of `Mem_New` / `Mem_Delete` (0x0046f490 / 0x0046f4d0 ..) is the debugging wrapper the original had around the `X` variants; the checks that would stop the PlayStation
// build (`Bang`, an out-of-memory screen that never returns) report the PSX print routine 0x004e8730 as a service and return 0.
namespace mem {

constexpr std::uint32_t kFreeHead = 0x0056a964;        // dword per zone: the first free block (0: none)
constexpr std::uint32_t kUsed = 0x0056a96c;            // dword per zone: bytes handed out (zone 1's is 0x0056a970)
constexpr std::uint32_t kZones = 0x0056a97c;           // per zone the first and the end address (8 bytes each)
constexpr std::uint32_t kLowFlag = 0x0056a98c;         // dword: zone 1 has reached its limit
constexpr std::uint32_t kLimit = 0x0056a990;           // dword: 98 % of zone 1
constexpr std::uint32_t kCounter = 0x0056a994;         // dword: allocation counter (31 bits)
constexpr std::uint32_t kWindow = kFreeHead;           // the globals the probe captures
constexpr std::uint32_t kWindowBytes = 0x38;

constexpr std::uint32_t kFillWord = 0x33333333;        // what a fresh block is filled with

}  // namespace mem

class HeapKit final : public pcplatform::PcKit {
public:
    HeapKit(RetailMemory& memory, RetailServices& services) : PcKit(memory, services) {}

    void add_to_free_list(std::uint32_t block, std::uint32_t zone);                                           // 0x0046e7f0 AddToFreeList
    void init();                                                                                              // 0x0046e8c0 Mem_Init
    std::uint32_t new_x(std::uint32_t size, std::uint32_t zone, std::uint32_t flag, std::uint32_t placement); // 0x0046e9c0 Mem_NewX
    void bang(std::uint32_t a, std::uint32_t b, std::uint32_t c);                                             // 0x0046ee20 Bang
    std::uint32_t new_top(std::uint32_t size);                                                                // 0x0046ee40 Mem_NewTop
    void delete_x(std::uint32_t payload);                                                                     // 0x0046f000 Mem_DeleteX
    void shrink_x(std::uint32_t payload, std::uint32_t size);                                                 // 0x0046f0e0 Mem_ShrinkX
    std::uint32_t block_size_x(std::uint32_t payload);                                                        // 0x0046f250 Mem_GetBlockSizeX
    std::uint32_t free_bytes(std::int8_t zone);                                                               // 0x0046f2c0 Mem_GetFree
    std::uint32_t copy(std::uint32_t destination, std::uint32_t source, std::int32_t count);                  // 0x0046f2e0 Mem_Copy
    std::uint32_t copy_bytes(std::uint32_t destination, std::uint32_t source, std::int32_t count);            // 0x0046f3b0 Mem_CopyBytes
    void swap(std::uint32_t a, std::uint32_t b, std::int32_t count);                                          // 0x0046f3e0 Mem_Swap
    std::uint32_t super_debug_usage() { return 0; }                                                           // 0x0046f460 Mem_SuperDebugUsage
    void no_check() {}                                                                                        // 0x0046f470 Mem_NoCheck
    void check_for_leaks() {}                                                                                 // 0x0046f480 SuperMem_CheckForLeaks
    std::uint32_t alloc_loaded_buffer(std::uint32_t size, std::uint32_t zone, std::uint32_t flag, std::uint32_t placement);   // 0x0046f490 FileIO_AllocLoadedBuffer
    void release(std::uint32_t payload);                                                                      // 0x0046f4d0 Mem_Delete
    void shrink(std::uint32_t payload, std::uint32_t size);                                                   // 0x0046f4e0 Mem_Shrink
    std::uint32_t block_size(std::uint32_t payload);                                                          // 0x0046f510 Mem_GetBlockSize

private:
    using Word = std::uint32_t;
    void fill(Word address, Word words);
    void update_low_flag();
    // the retail "cut the tail off a free block" header: (hdr & ~0xf) - (taken << 4) - 0x71, whose low nibble is then pulled to the junk nibble already at the new block
    Word tail_header(Word header, Word taken_size, Word old_word) const;
};

}  // namespace opentony::runtime::heap
