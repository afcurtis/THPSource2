#pragma once

#include "collision_kit.hpp"
#include "math_kit.hpp"

#include <array>
#include <cstdint>

namespace opentony::runtime::rail {

// Phase 4.15a: rail.cpp, the rails of a level. A rail is a chain of type-10/type-11 trigger nodes (the level's node table, `OffsetList` / `NumNodes`); the game keeps one runtime record per chain
// (a 0x28-byte object on an intrusive list) and a lookup table that numbers the rail nodes. Like the rest of Phase 4 the functions work on the retail memory at the retail addresses; the node
// decoders (`TRG_DecodeNodePosition`, `TRG_GetNodeLinks`, owned by the trigger-file sub-milestone), the allocator and the object operators are services reported by address.

namespace globals {

constexpr std::uint32_t kCurrentLevel = 0x0056a898;        // CurrentLevel: the trigger word of a node sits 6 bytes further on in the levels 10 and up (except 13)
constexpr std::uint32_t kNumberOfPlayers = 0x00533f3c;     // GNumberOfPlayers (a rail node says in which player counts it is used)
constexpr std::uint32_t kOffsetList = 0x0056e210;          // OffsetList: pointer to the array of node pointers
constexpr std::uint32_t kNumNodes = 0x0056e214;            // NumNodes
constexpr std::uint32_t kLookup = 0x0056b858;              // pointer to the rail lookup table (4 bytes per rail: the node, the index of the rail node that leads to it)
constexpr std::uint32_t kLookupCount = 0x0056b85c;         // number of entries of the lookup table, entry 0 included
constexpr std::uint32_t kListHead = 0x0056b860;            // RailRuntimeListHead
constexpr std::uint32_t kCheckSegments = 0x005614c4;       // byte: when set, a rail segment is tested against the world before the chain goes on

}  // namespace globals

// a node of the level's node table: the type is the first word
namespace node {

constexpr std::uint16_t kRailPoint = 0x0a;                 // a rail node that is a point of a rail
constexpr std::uint16_t kRailStart = 0x0b;                 // (made a kRailPoint while the rails are created)

}  // namespace node

// the trigger word of a rail node (the word after its position)
namespace word {

constexpr std::uint16_t kTypeMask = 0x000f;                // the kind of rail
constexpr std::uint16_t kPlayerMask = 0x0030;              // in which player counts the rail exists (0: in all)
constexpr std::uint16_t kDone = 0x8000;                    // the rail has been given a runtime record

}  // namespace word

// a runtime record (CRail, 0x28 bytes)
namespace record {

constexpr std::uint32_t kVtable = 0x00;                    // 0x005196a4 (0x005184b4 once destroyed)
constexpr std::uint32_t kState = 0x04;                     // byte: 0 off, 1 on
constexpr std::uint32_t kKind = 0x05;                      // byte: the trigger word's type bits of the first node
constexpr std::uint32_t kFirstNode = 0x06;                 // word: the node the chain starts at
constexpr std::uint32_t kMinimum = 0x08;                   // three dwords: the bounding box of the chain
constexpr std::uint32_t kMaximum = 0x14;                   // three dwords
constexpr std::uint32_t kPrevious = 0x20;
constexpr std::uint32_t kNext = 0x24;
constexpr std::uint32_t kBytes = 0x28;

constexpr std::uint32_t kRailVtable = 0x005196a4;
constexpr std::uint32_t kBaseVtable = 0x005184b4;
constexpr std::uint32_t kDestructor = 0x004aaf10;          // slot 0 of the rail vtable

}  // namespace record

constexpr std::uint32_t kDecodeNodePosition = 0x004c8650;  // TRG_DecodeNodePosition(out, node) -> the address after the node's position (the position is written to `out`)
constexpr std::uint32_t kGetNodeLinks = 0x004c8550;        // TRG_GetNodeLinks(node) -> the node's link table (a word count, then the linked nodes)
constexpr std::uint32_t kAllocLoadedBuffer = 0x0046f490;   // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
constexpr std::uint32_t kOperatorNew = 0x00466b70;         // CClass_OpNew(size)
constexpr std::uint32_t kOperatorDelete = 0x00466bb0;      // CClass_OpDelete(block)
constexpr std::uint32_t kMemDelete = 0x0046f4d0;           // Mem_Delete(block)
constexpr std::uint32_t kLookupBytes = 0x1f40;             // the lookup table (2000 entries)
constexpr std::uint32_t kLookupEntries = 2000;

// where the port builds the position the node decoder writes (the retail code keeps it in a stack local)
constexpr std::uint32_t kPositionScratch = pcplatform::kScratchBase + 0x400;

class RailKit final : public collision::CollisionKit {
public:
    RailKit(RetailMemory& memory, RetailServices& services) : CollisionKit(memory, services), math_(memory, services) {}

    // ---- the trigger word and the node table ----
    std::uint32_t read_trigger_word(std::uint32_t node);                       // 0x004a9f70 TRG_ReadRailTriggerWord
    std::uint32_t rail_type(std::uint32_t node);                               // 0x004a9fd0 Rail_Type
    std::uint32_t flags_pointer(std::uint32_t node);                           // 0x004a9ff0 Rail_FlagsPointer
    std::uint32_t is_rail_node(std::uint32_t node);                            // 0x004aa040 Rail_IsRailNode
    std::uint32_t get_link(std::uint32_t node, std::uint32_t index);           // 0x004aa090 Rail_GetLink
    std::uint32_t num_links(std::uint32_t node);                               // 0x004aa1c0 Rail_NumLinks
    std::uint32_t next_node(std::uint32_t node);                               // 0x004aa220 Rail_NextNode
    std::uint32_t prev_node(std::uint32_t node);                               // 0x004aa290 Rail_PrevNode
    std::uint32_t prev_node_quick(std::uint32_t node, std::uint32_t start);    // 0x004aa330 Rail_PrevNodeQuick
    void set_state(std::uint32_t node, std::uint32_t state);                   // 0x004aa3c0 Rail_SetState
    void clear_state(std::uint32_t node) { set_state(node, 0); }               // 0x004aa410 Rail_ClearState
    void activate_node(std::uint32_t node) { set_state(node, 1); }             // 0x004aa420 Rail_ActivateNode
    std::uint32_t index_of_node(std::uint32_t node);                           // 0x004aa430 GetIndexOfNode
    void setup_lookup();                                                       // 0x004aa4b0 SetupLookup

    // ---- the runtime records ----
    void rail_init();                                                          // 0x004aa870 Rail_Init
    void delete_all();                                                         // 0x004aa8a0 Rail_DeleteAll
    std::uint32_t crail_destruct(std::uint32_t self, std::uint32_t flags);     // 0x004aaf10 CRail_destruct (thiscall)
    void crail_destroy(std::uint32_t self);                                    // 0x004aaf30 CRail_destroy (thiscall)
    void create_rails();                                                       // 0x004aa5d0 Rail_CreateRails
    std::uint32_t create_runtime_record(std::uint32_t self, std::uint32_t node);   // 0x004aa8c0 Rail_CreateRuntimeRecord (thiscall, one argument: the CRail constructor)

private:
    using Word = std::uint32_t;
    using Vec = std::array<Word, 3>;
    // the node decoder's position (the service writes it to the scratch)
    Vec decode_position(Word node);
    // 0x00465f60 Math_NormalizeFixed12 (a vector of the retail memory in place; the port keeps the vector in a local; the squares go through the GTE registers, as retail's)
    void normalize_fixed12(Vec& v);
    // TRG_DecodeNodePosition: the service writes the position to the scratch, the address after it is the answer
    Word decode_node(Word node);
    // the address of the node's trigger word (0 when it holds the "none" value 0xffff)
    Word trigger_pointer(Word node);
    void take_filled(Word destination, Word bytes, const std::string& service);
    void destroy_object(Word object);

    mathkit::MathKit math_;
};

}  // namespace opentony::runtime::rail
