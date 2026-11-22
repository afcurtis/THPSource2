#pragma once

#include "psx_spool.hpp"
#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <vector>

namespace opentony::runtime::psxenvkit {

// Phase 5.4a, continued (7b): the region parser of the PSX spooler on the retail memory model. PsxSpool (src/runtime/psx_spool.*) keeps the spooler's queue, the slot
// records at 0x0056d428 and the material list; what it hands to "the parser" is a service (`parse_environment`) because the parser is 522 instructions of its own. A
// program with no retail code needs it, and the two functions here are the parser and its tail, transcribed from the disassembly (build/s5/asm_parse_env.txt,
// build/s5/asm_finalize.txt) and checked against the retail functions by tony/psx_env_oracle.py:
//
//   PSX_ParseEnvironment 0x004b2450 (cdecl, slot)   makes the object table from the raw image (the loader hands it a file in place), relocates the model offsets, turns
//                                                   the checksum list into scene materials, reads both palette groups into the palette source lookup, loads the texture
//                                                   references and walks the tag stream after the models (wib 6, pulse 7, zone blockmap 10, animation tables 0x2a/0x2c,
//                                                   named resources 0x45, hierarchy 'HIER', rgb 'RGBs')
//   PSX_FinalizeEnvironment 0x004647c0 (cdecl, slot) walks the models once more (bounding box flags, face flag fixes, the per-slot model summary) and links the objects
//
// The slot record's words are the ones PsxSpool names (kSlot*). The callees are reached by address, as everywhere in the Kits: the resolver answers with the native
// or the seam. The assertion file and line words (0x0056881c, 0x00568638) are not written, as in every Kit; a failed check is reported.
class PsxEnvKit final {
public:
    PsxEnvKit(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    void parse_environment(std::uint32_t slot);       // 0x004b2450
    void finalize_environment(std::uint32_t slot);    // 0x004647c0

private:
    [[nodiscard]] static std::uint32_t slot_record(std::uint32_t slot) { return psx::kSlotBase + slot * psx::kSlotStride; }
    std::uint32_t call(std::uint32_t address, std::vector<std::uint32_t> args = {});
    void build_object_table(std::uint32_t record, std::uint32_t raw, std::int32_t objects);
    // the palette groups of the trailing tables: `wide` is the 256-colour group
    void read_palettes(std::uint32_t& entry, std::uint32_t& data, std::uint32_t count, std::uint32_t stride_words, std::uint32_t colours, std::uint32_t fill_words,
                       std::uint32_t kind);

    RetailMemory& m_;
    RetailServices& s_;
};

}  // namespace opentony::runtime::psxenvkit
