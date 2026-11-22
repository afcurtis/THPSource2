#pragma once

#include "retail_memory.hpp"
#include "retail_services.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace opentony::runtime {

// Phase 4.11a: the PSX region slots, the scene-material table and the queued PSX spooler of spool.cpp (0x004b1ac0..0x004b5010).
//
// The retail game keeps up to 20 loaded PSX regions in a table of 0x44-byte slots (0x0056d428). A region is a raw file image
// (SKWARE.PSX style: header, then trailing tables of model checksums, material pointers and texture references, see
// trailing_tables()) plus a set of published pointers into it (objects, models, animation tables, colour tables). Scene materials
// are shared 0x2c-byte records hashed by checksum into 512 buckets (0x0056cc18) and reference counted by the regions that use
// them. The spooler (a 20-entry ring at 0x0056d9c8) loads queued regions one step per call.
//
// Everything is read and written at its retail address through RetailMemory; the engine (allocator, file layer, texture
// objects, the region parser) is reached through named services, scripted by the replay.
namespace psx {

// ---- the slot table ---------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kSlotBase = 0x0056d428;
constexpr std::uint32_t kSlotStride = 0x44;
constexpr std::int32_t kSlotCount = 20;
constexpr std::uint32_t kSlotName = 0x00;            // 9 characters and a NUL; an empty name is a free slot
constexpr std::uint32_t kSlotState = 0x0a;           // 0 unusable, 1 loaded environment/level region, 2 animation resource
constexpr std::uint32_t kSlotKeep = 0x0b;            // a region with this set survives Spool_ClearAllPSXs
constexpr std::uint32_t kSlotChecksums = 0x0c;       // the model-name checksum table (the first trailing table)
constexpr std::uint32_t kSlotObjects = 0x10;         // the published 0x4c-stride object array (an object with a vtable)
constexpr std::uint32_t kSlotModels = 0x14;          // the published model pointer table (the count is the word before it)
constexpr std::uint32_t kSlotRaw = 0x18;             // the raw file image
constexpr std::uint32_t kSlotAnimTables = 0x1c;
constexpr std::uint32_t kSlotAnimHierarchy = 0x20;
constexpr std::uint32_t kSlotHooks = 0x24;
constexpr std::uint32_t kSlotRgb = 0x28;
constexpr std::uint32_t kSlotWib = 0x2c;
constexpr std::uint32_t kSlotPulse = 0x30;
constexpr std::uint32_t kSlotModelCount = 0x38;      // u16
constexpr std::uint32_t kSlotTexturesInRam = 0x3b;
constexpr std::uint32_t kSlotTexturesShrunk = 0x3c;
constexpr std::uint32_t kSlotHeapType = 0x3d;        // 0 or 1 selects the loader's heap
constexpr std::uint32_t kSlotFlag3e = 0x3e;
constexpr std::uint32_t kSlotFlag3f = 0x3f;
constexpr std::uint32_t kSlotFileSize = 0x40;

// ---- the other globals --------------------------------------------------------------------------------------------------------
constexpr std::uint32_t kMaterialBuckets = 0x0056cc18;      // 512 dwords
constexpr std::uint32_t kMaterialIterNode = 0x0056d418;     // the iterator's next node
constexpr std::uint32_t kEnvTables = 0x0056d41c;            // two dwords: the trailing tables of the attached environment regions
constexpr std::uint32_t kMaterialIterBucket = 0x0056d424;
constexpr std::uint32_t kSavedImages = 0x0056d978;          // 20 dwords: copies of a region's first 0x7530 bytes
constexpr std::uint32_t kQueueBase = 0x0056d9c8;            // 20 entries of 0x11 bytes
constexpr std::uint32_t kQueueStride = 0x11;
constexpr std::int32_t kQueueCount = 20;
constexpr std::uint32_t kQueueActive = 0x0d;
constexpr std::uint32_t kQueueKind = 0x0e;           // 0 plain, 1 environment (attach after parsing), 2 animation resource
constexpr std::uint32_t kQueueSlot = 0x0f;
constexpr std::uint32_t kQueueHeap = 0x10;
constexpr std::uint32_t kEnvCurrent = 0x0056db1c;
constexpr std::uint32_t kEnvAttached = 0x0056db20;          // two dwords
constexpr std::uint32_t kEnvObjectListHead = 0x0056db28;
constexpr std::uint32_t kEnvCursor = 0x0056db2c;
constexpr std::uint32_t kQueueRead = 0x0056db30;
constexpr std::uint32_t kQueueWrite = 0x0056db34;
constexpr std::uint32_t kBitsResourceList = 0x0056db38;
constexpr std::uint32_t kPaletteBase = 0x0056db3c;
constexpr std::uint32_t kPaletteFlag = 0x0056db40;
constexpr std::uint32_t kMaterialBytes = 0x0056db44;        // 0x30 per live material record
constexpr std::uint32_t kSpoolMode = 0x0056db48;
constexpr std::uint32_t kEnvSlots = 0x00537c80;             // two dwords: the region slots of the attached environments (-1 none)
constexpr std::uint32_t kLoading = 0x005615d0;
constexpr std::uint32_t kPsxSuffix = 0x0053802c;            // the string appended to a queued name

constexpr std::uint32_t kMaterialStride = 0x2c;
constexpr std::uint32_t kSkaterSelectionRecords = 0x0053a514;      // 0x170 bytes per skater selection; bit 1 of the first byte marks a custom skater
constexpr std::uint32_t kGameModeSelector = 0x00533f38;
constexpr std::uint32_t kCustomSkaterIndex = 0x0056a854;
constexpr std::uint32_t kPlayer = 0x0056a858;
constexpr std::uint32_t kPlayerTwo = 0x0056a85c;
constexpr std::uint32_t kSkaterPartTables = 0x00568648;           // pointers to the skaters' part tables (+0x6c holds the board material)
constexpr std::uint32_t kReplaySkaters = 0x0056b8d4;
constexpr std::uint32_t kDefaultAnimations = 0x00533f2c;
constexpr std::uint32_t kReplayAnimations = 0x005365ac;

// ---- assertion messages (the address of the format string names the assertion) ----------------------------------------------------
constexpr std::uint32_t kMsgNoFreeSlot = 0x0052adf0;
constexpr std::uint32_t kMsgModelPointerState = 0x00537cf0;
constexpr std::uint32_t kMsgNoModelTable = 0x00537ce0;
constexpr std::uint32_t kMsgSavedImage = 0x00537cb8;
constexpr std::uint32_t kMsgBadRegionNumber = 0x00537d88;
constexpr std::uint32_t kMsgPsxNotUsable = 0x00537d5c;
constexpr std::uint32_t kMsgChecksumNotFound = 0x00537d28;
constexpr std::uint32_t kMsgMaterialRefCount = 0x00537db4;
constexpr std::uint32_t kMsgDecBadRegion = 0x0052dce4;
constexpr std::uint32_t kMsgDecNullName = 0x0052dcc8;
constexpr std::uint32_t kMsgDecNotUsable = 0x0052dc90;
constexpr std::uint32_t kMsgDecNumModels = 0x0052dc5c;
constexpr std::uint32_t kMsgDecNumChecksums = 0x0052dc3c;
constexpr std::uint32_t kMsgDecTooMuchUsage = 0x0052dc24;
constexpr std::uint32_t kMsgDecZeroUsage = 0x0052dc04;
constexpr std::uint32_t kMsgAttachBadRegion = 0x00537e0c;
constexpr std::uint32_t kMsgTexturesNotInRam = 0x00537ea8;
constexpr std::uint32_t kMsgBadNumColours = 0x00537e80;
constexpr std::uint32_t kMsgDuplicateTexture = 0x00537e30;
constexpr std::uint32_t kMsgClearTexturesNotLoaded = 0x00537ed8;
constexpr std::uint32_t kMsgTexturesInRamFlag = 0x00537ebc;
constexpr std::uint32_t kMsgDetachBadIndex = 0x00534e9c;
constexpr std::uint32_t kMsgDetachVoid = 0x00537f60;
constexpr std::uint32_t kMsgConfusedEnviroLists = 0x00537f48;
constexpr std::uint32_t kMsgPatchingEnviroList = 0x00537f0c;
constexpr std::uint32_t kMsgClearBadRegion = 0x00537f80;
constexpr std::uint32_t kMsgSequencerItems = 0x00538004;
constexpr std::uint32_t kMsgVastAnims = 0x00537fd8;
constexpr std::uint32_t kMsgVastFrames = 0x00537fa8;
constexpr std::uint32_t kMsgNameTooLong = 0x0053804c;
constexpr std::uint32_t kMsgQueueOverflow = 0x00538034;
constexpr std::uint32_t kMsgUnknownFileType = 0x005380c4;
constexpr std::uint32_t kMsgBadSpoolingMode = 0x005380ec;
constexpr std::uint32_t kMsgHeapType = 0x0053806c;
constexpr std::uint32_t kMsgUnknownQueueType = 0x005380a0;
constexpr std::uint32_t kMsgEnvNameTooLong = 0x00538084;
constexpr std::uint32_t kMsgAnimAfterHierarchy = 0x00538164;
constexpr std::uint32_t kMsgNoPacketTerminator = 0x00537270;
constexpr std::uint32_t kMsgChecksumsNotAfterPacket = 0x00538140;
constexpr std::uint32_t kMsgBadNumColors = 0x0053725c;
constexpr std::uint32_t kMsgShrinkLoadedTextures = 0x00538100;
constexpr std::uint32_t kMsgNoDefaultAnimations = 0x00538220;
constexpr std::uint32_t kMsgBadAnimNumber = 0x005381f8;
constexpr std::uint32_t kMsgBadExtraAnimNumber = 0x005381c0;
constexpr std::uint32_t kMsgSourceBytes = 0x00538190;
constexpr std::uint32_t kMsgNoModelFilePointer = 0x00538278;
constexpr std::uint32_t kMsgNoModelFile = 0x005372fc;
constexpr std::uint32_t kMsgModelFileMissing = 0x0055fe10;
constexpr std::uint32_t kMsgModelHasHooks = 0x00538268;
constexpr std::uint32_t kMsgModelHasWib = 0x00538254;
constexpr std::uint32_t kMsgModelHasPulse = 0x00538240;
constexpr std::uint32_t kMsgPossibleLeak = 0x00526874;
constexpr std::uint32_t kMsgNoFilenamePointer = 0x005370b4;
constexpr std::uint32_t kMsgNoFilename = 0x005269a0;
constexpr std::uint32_t kMsgFilenameTooLong = 0x005382bc;
constexpr std::uint32_t kMsgNoSpaceForNames = 0x005382a8;
constexpr std::uint32_t kMsgNoSpaceForPartCount = 0x00538290;
constexpr std::uint32_t kMsgTooManyPieces = 0x005382f8;
constexpr std::uint32_t kMsgNoPartsFound = 0x005382d4;
constexpr std::uint32_t kMsgNoRegion = 0x00526164;
constexpr std::uint32_t kMsgNoModelToSearch = 0x00526150;
constexpr std::uint32_t kMsgNeedChecksumCount = 0x00526138;
constexpr std::uint32_t kMsgNeedChecksumList = 0x00526124;
constexpr std::uint32_t kMsgNoRegionB = 0x00526118;
constexpr std::uint32_t kMsgTooManyChecksums = 0x00526104;
constexpr std::uint32_t kMsgReverseEngineer = 0x005260d4;
constexpr std::uint32_t kMsgBoardTextureMissing = 0x0053831c;
constexpr std::uint32_t kMsgModelIndexRange = 0x00538338;
constexpr std::uint32_t kMsgNoCustomSkater = 0x00538354;

class PsxSpool final {
public:
    PsxSpool(RetailMemory& memory, RetailServices& services) : m_(memory), s_(services) {}

    // ---- region slots ---------------------------------------------------------------------------------------------------------
    void init();                                                    // 0x004b1ac0 Spool_Init
    [[nodiscard]] std::int32_t find_free_slot();                    // 0x004b1bb0
    void save_region_image(std::int32_t slot);                      // 0x004b1c20 (a copy of the first 0x7530 bytes)
    void restore_region_image(std::int32_t slot);                   // 0x004b1c70
    [[nodiscard]] std::uint32_t model_pointer(std::uint32_t object);          // 0x004b1d10 (object: slot at +0x1f, model index at +0x1a)
    [[nodiscard]] std::uint32_t find_model_index(std::uint32_t checksum, std::int32_t region);   // 0x004b1de0
    [[nodiscard]] std::uint32_t model_name_checksum(std::uint32_t object);    // 0x004b1f40
    [[nodiscard]] std::int32_t find_region_by_name(std::uint32_t name);       // 0x004b3230
    void clear_named_region(std::uint32_t name);                    // 0x004b3270
    void clear_all_psxs();                                          // 0x004b32b0
    void clear_region(std::int32_t region, std::uint32_t free_materials, std::uint32_t keep_objects);   // 0x004b32f0
    void clear_unused_regions();                                    // 0x004b3460
    void clear_environment_regions();                               // 0x004b34b0
    [[nodiscard]] std::int32_t init_free_slot();                    // 0x004b4640

    // ---- scene materials ------------------------------------------------------------------------------------------------------
    [[nodiscard]] std::uint32_t alloc_material(std::uint32_t checksum);       // 0x004b1f70
    void unlink_material(std::uint32_t material);                   // 0x004b1fd0
    [[nodiscard]] std::uint32_t find_material(std::uint32_t checksum);        // 0x004b2030
    void material_iter_begin();                                     // 0x004b2060
    [[nodiscard]] std::uint32_t material_iter_next();               // 0x004b2080
    [[nodiscard]] std::uint32_t trailing_tables(std::uint32_t region_image);  // 0x004b20f0 (the address after the -1 chunk tag)
    void decrement_material_usage(std::int32_t region);             // 0x004b2120
    void free_unused_materials();                                   // 0x004b2390
    void nothing_a() {}                                             // 0x004b2440

    // ---- environments ---------------------------------------------------------------------------------------------------------
    void attach_environment(std::int32_t region);                   // 0x004b2ac0
    void detach_environment(std::uint32_t index);                   // 0x004b3090
    [[nodiscard]] std::int32_t env_index(std::int32_t region);      // 0x004b3990

    // ---- the named-resource list and the queue --------------------------------------------------------------------------------------
    void parse_named_resource_tag(std::uint32_t region_image, std::uint32_t tag);   // 0x004b34f0
    [[nodiscard]] std::uint32_t find_named_resource(std::uint32_t name);      // 0x004b3680
    void queue_copy_name(std::uint32_t first, std::uint32_t second);          // 0x004b3750
    [[nodiscard]] std::uint32_t queue_resource(std::uint32_t name, std::int32_t kind, std::uint32_t a, std::uint32_t b);   // 0x004b37a0
    [[nodiscard]] std::uint32_t spooler();                          // 0x004b39b0
    void drain_spooler();                                           // 0x004b3df0
    [[nodiscard]] bool busy();                                      // 0x004b3e20

    // ---- animation tables and model files (psx_spool_anim.cpp) ---------------------------------------------------------------------
    void strip_anim(std::int32_t region);                           // 0x004b3e50 Spool_StripAnim
    void build_animation_tables(std::int32_t region, std::uint32_t list);     // 0x004b40a0 (list: two -1 terminated lists of animation numbers)
    [[nodiscard]] std::int32_t load_model_file(std::uint32_t name, std::uint32_t kind);   // 0x004b43e0 Skater_LoadModelFile
    [[nodiscard]] std::int32_t load_animation_resource(std::uint32_t name, std::int32_t skater);   // 0x004b46a0
    std::uint32_t load_part_manifest(std::uint32_t name, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d);   // 0x004b47a0 (answers the loaded image)
    std::uint32_t parse_parts_manifest(std::uint32_t image, std::uint32_t name, std::uint32_t names, std::uint32_t starts, std::uint32_t count, std::int32_t limit);   // 0x004b4940
    void collect_model_materials(std::uint32_t slot, std::uint32_t model, std::uint32_t checksums, std::uint32_t count);   // 0x004b4ad0
    [[nodiscard]] std::uint32_t score_material_palette_usage(std::uint32_t checksum);   // 0x004b4e00
    [[nodiscard]] std::uint32_t select_material_for_model(std::uint32_t skater);        // 0x004b4e70
    void relocate_definition_pointers(std::uint32_t region_image, std::uint32_t delta);   // 0x004b5010
    [[nodiscard]] std::uint32_t player_model_slot(std::uint32_t character, std::uint32_t skater);   // 0x004b50a0

    // ---- texture references (psx_spool_tex.cpp) ------------------------------------------------------------------------------------
    void load_texture_references(std::uint32_t slot, std::uint32_t flags);       // 0x004b2b70
    void clear_texture_references(std::uint32_t slot);                           // 0x004b2f70

    // The region parser (PSX_ParseEnvironment, 0x004b2450) is another model; a caller that needs what it publishes into the slot installs it here
    // (the replay emulates the probe's stubbed parser).
    void set_parse_hook(std::function<void(std::uint32_t)> hook) { parse_hook_ = std::move(hook); }

    // bytes retail leaves as stack leftovers (the texture rectangle words written from uninitialised locals): the replay does not compare them
    [[nodiscard]] const std::vector<std::pair<std::uint32_t, std::uint32_t>>& unspecified() const noexcept { return unspecified_; }

private:
    // FileIO_OpenLoad(name, 1) with the name as text: a replay reports the text and the heap; live the name is written to the scratch window (9a)
    std::uint32_t open_load_text(const std::string& text);
    [[nodiscard]] static std::uint32_t slot_addr(std::int32_t slot) {
        return kSlotBase + static_cast<std::uint32_t>(slot) * kSlotStride;
    }
    [[nodiscard]] bool names_equal(std::uint32_t a, std::uint32_t b, std::uint32_t limit) const;
    template <typename Count, typename Item, typename Push>
    void collect_core(std::uint32_t slot, std::uint32_t model, bool have_count, bool have_list, Count count, Item item, Push push);

    RetailMemory& m_;
    RetailServices& s_;
    void parse_environment(std::uint32_t slot);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> unspecified_;
    std::function<void(std::uint32_t)> parse_hook_;
};

}  // namespace psx
}  // namespace opentony::runtime
