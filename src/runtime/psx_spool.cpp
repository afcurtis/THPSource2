#include "auto_views.hpp"
#include "psx_spool.hpp"

#include <vector>

namespace opentony::runtime::psx {

using namespace opentony::runtime::autoviews;

namespace {

// The services the region code reaches (named as the replay fixtures record them).
constexpr const char* kAlloc = "alloc";                    // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
constexpr const char* kMemDelete = "mem_delete";           // Mem_Delete(block)
constexpr const char* kObjectDelete = "virtual_delete";     // a virtual destructor call: (object, 3)
constexpr const char* kTextureRelease = "texture_release"; // D3DTex_ReleaseMaterialTexture(texture)
constexpr const char* kPaletteClear = "palette_clear";     // Palette_ClearSourceLookup()
constexpr const char* kBlockmapClear = "blockmap_clear";   // Blockmap_ClearRuntimeGrid(index)
constexpr const char* kFileOpenResource = "file_open_resource";   // FileIO_OpenResource(name): the size
constexpr const char* kFileLoadResource = "file_load_resource";   // FileIO_LoadResource(buffer)
constexpr const char* kFileContinue = "file_continue";     // FileIO_Continue()
constexpr const char* kParseEnvironment = "parse_environment";    // PSX_ParseEnvironment(slot)

}  // namespace

bool PsxSpool::names_equal(std::uint32_t a, std::uint32_t b, std::uint32_t limit) const {
    // 0x004c9f50: equal ignoring case, at most `limit` characters; the letters fold on signed bytes, so bytes above 0x7f do not
    if (a == 0) {
        return b == 0;
    }
    if (b == 0) {
        return false;
    }
    auto fold = [](std::uint8_t c) {
        return static_cast<std::uint8_t>((static_cast<std::int8_t>(c) >= 0x41 && static_cast<std::int8_t>(c) <= 0x5a) ? c + 0x20 : c);
    };
    for (;;) {
        const std::uint8_t x = fold(byte_at(m_, a++));
        const std::uint8_t y = fold(byte_at(m_, b++));
        if (x != y) {
            return false;
        }
        if (x == 0 || --limit == 0) {
            return true;
        }
    }
}

void PsxSpool::parse_environment(std::uint32_t slot) {
    s_.note(kParseEnvironment, {slot});
    if (parse_hook_) {
        parse_hook_(slot);
    }
}

// ---- region slots -------------------------------------------------------------------------------------------------------------

void PsxSpool::init() {
    // (the assertion "MAXPSXS is too big" tests a constant: it never fires)
    for (std::int32_t i = 0; i < kQueueCount; ++i) {
        set_byte_at(m_, kQueueBase + static_cast<std::uint32_t>(i) * kQueueStride + kQueueActive, 0);
    }
    set_global_word(m_, kQueueRead, 0);
    set_global_word(m_, kQueueWrite, 0);
    for (std::int32_t i = 0; i < kSlotCount; ++i) {
        const std::uint32_t slot = slot_addr(i);
        set_byte_at(m_, slot + kSlotName, 0);
        set_byte_at(m_, slot + kSlotKeep, 0);
        set_byte_at(m_, slot + kSlotState, 0);
        set_word_at(m_, slot + kSlotModels, 0);
        set_word_at(m_, slot + kSlotRaw, 0);
        if (const std::uint32_t objects = word_at(m_, slot + kSlotObjects); objects != 0) {
            s_.note(kObjectDelete, {objects, 3});
        }
        set_word_at(m_, slot + kSlotObjects, 0);
        set_word_at(m_, slot + kSlotAnimTables, 0);
        set_word_at(m_, slot + kSlotAnimHierarchy, 0);
        set_word_at(m_, slot + kSlotPulse, 0);
        set_word_at(m_, slot + kSlotWib, 0);
        set_word_at(m_, slot + kSlotHooks, 0);
        set_byte_at(m_, slot + kSlotTexturesInRam, 0);
        set_byte_at(m_, slot + kSlotTexturesShrunk, 0);
        set_byte_at(m_, slot + kSlotHeapType, 1);
        set_byte_at(m_, slot + kSlotFlag3e, 0);
        set_byte_at(m_, slot + kSlotFlag3f, 0);
    }
    set_global_word(m_, kBitsResourceList, 0);
    for (std::uint32_t i = 0; i < 0x200; ++i) {
        set_word_at(m_, kMaterialBuckets + i * 4, 0);
    }
    set_global_word(m_, kEnvObjectListHead, 0);
    set_global_word(m_, kEnvCursor, 0);
    set_global_word(m_, kEnvSlots, 0xffffffff);
    set_global_word(m_, kEnvSlots + 4, 0xffffffff);
}

std::int32_t PsxSpool::find_free_slot() {
    std::int32_t found = -1;
    for (std::int32_t i = 0; i < kSlotCount; ++i) {
        if (byte_at(m_, slot_addr(i) + kSlotName) == 0) {
            found = i;
            break;
        }
    }
    s_.check(found != -1, kMsgNoFreeSlot);
    return found;
}

void PsxSpool::save_region_image(std::int32_t slot) {
    const std::uint32_t raw = word_at(m_, slot_addr(slot) + kSlotRaw);
    s_.note(kAlloc, {0x7530, 1, 1, 0});
    const std::uint32_t copy = static_cast<std::uint32_t>(s_.value(kAlloc));
    set_word_at(m_, kSavedImages + static_cast<std::uint32_t>(slot) * 4, copy);
    m_.copy(copy, raw, 0x7530);
}

void PsxSpool::restore_region_image(std::int32_t slot) {
    const std::uint32_t saved = word_at(m_, kSavedImages + static_cast<std::uint32_t>(slot) * 4);
    s_.check(saved != 0, kMsgSavedImage);
    m_.copy(word_at(m_, slot_addr(slot) + kSlotRaw), saved, 0x7530);
    s_.note(kMemDelete, {saved});
    set_word_at(m_, kSavedImages + static_cast<std::uint32_t>(slot) * 4, 0);
}

std::uint32_t PsxSpool::model_pointer(std::uint32_t object) {
    const std::uint32_t slot = slot_addr(byte_at(m_, object + 0x1f));
    s_.check(byte_at(m_, slot + kSlotState) != 0, kMsgModelPointerState);
    const std::uint32_t table = word_at(m_, slot + kSlotModels);
    s_.check(table != 0, kMsgNoModelTable);
    return word_at(m_, table + static_cast<std::uint32_t>(half_at(m_, object + 0x1a)) * 4);
}

std::uint32_t PsxSpool::find_model_index(std::uint32_t checksum, std::int32_t region) {
    s_.check(region >= 0 && region <= 0x13, kMsgBadRegionNumber);
    const std::uint32_t slot = slot_addr(region);
    s_.check(byte_at(m_, slot + kSlotState) != 0, kMsgPsxNotUsable);
    std::uint32_t at = word_at(m_, slot + kSlotChecksums);
    const std::uint32_t count = word_at(m_, word_at(m_, slot + kSlotModels) - 4);
    for (std::uint32_t i = 0; i < count; ++i, at += 4) {
        if (word_at(m_, at) == checksum) {
            return i;
        }
    }
    s_.check(false, kMsgChecksumNotFound);
    return 0;
}

std::uint32_t PsxSpool::model_name_checksum(std::uint32_t object) {
    const std::uint32_t slot = slot_addr(byte_at(m_, object + 0x1f));
    return word_at(m_, word_at(m_, slot + kSlotChecksums) + static_cast<std::uint32_t>(half_at(m_, object + 0x1a)) * 4);
}

std::int32_t PsxSpool::find_region_by_name(std::uint32_t name) {
    for (std::int32_t i = 0; i < kSlotCount; ++i) {
        if (names_equal(name, slot_addr(i) + kSlotName, 0xffffffff)) {
            return i;
        }
    }
    return -1;
}

void PsxSpool::clear_named_region(std::uint32_t name) {
    const std::int32_t found = find_region_by_name(name);
    if (byte_at(m_, slot_addr(found) + kSlotState) != 0) {
        clear_region(find_region_by_name(name), 1, 0);
    }
}

void PsxSpool::clear_all_psxs() {
    drain_spooler();
    clear_environment_regions();
    for (std::int32_t i = 0; i < kSlotCount; ++i) {
        if (byte_at(m_, slot_addr(i) + kSlotKeep) == 0) {
            clear_region(i, 1, 0);
        }
    }
    free_unused_materials();
    nothing_a();
}

void PsxSpool::clear_region(std::int32_t region, std::uint32_t free_materials, std::uint32_t keep_objects) {
    if (region == -1) {
        return;
    }
    s_.check(region >= 0 && region <= 0x13, kMsgClearBadRegion);
    const std::uint32_t slot = slot_addr(region);
    if (byte_at(m_, slot + kSlotName) == 0) {
        return;
    }
    if (byte_at(m_, slot + kSlotState) == 2) {
        if (const std::uint32_t raw = word_at(m_, slot + kSlotRaw); raw != 0) {
            s_.note(kMemDelete, {raw});
        }
        set_word_at(m_, slot + kSlotRaw, 0);
        set_byte_at(m_, slot + kSlotName, 0);
        return;
    }
    decrement_material_usage(region);
    if (keep_objects == 0) {
        if (const std::uint32_t objects = word_at(m_, slot + kSlotObjects); objects != 0) {
            s_.note(kObjectDelete, {objects, 3});
        }
        set_word_at(m_, slot + kSlotObjects, 0);
        set_word_at(m_, slot + kSlotAnimTables, 0);
        set_word_at(m_, slot + kSlotPulse, 0);
        set_word_at(m_, slot + kSlotWib, 0);
        set_byte_at(m_, slot + kSlotKeep, 0);
        set_byte_at(m_, slot + kSlotState, 0);
        set_word_at(m_, slot + kSlotHooks, 0);
        set_word_at(m_, slot + kSlotModels, 0);
        set_byte_at(m_, slot + kSlotName, 0);
        set_byte_at(m_, slot + kSlotTexturesInRam, 0);
        set_byte_at(m_, slot + kSlotTexturesShrunk, 0);
        set_byte_at(m_, slot + kSlotHeapType, 1);
        const std::int32_t index = env_index(region);
        if (index != -1) {
            s_.note(kBlockmapClear, {static_cast<std::uint32_t>(index)});
            detach_environment(static_cast<std::uint32_t>(index));
            set_word_at(m_, kEnvSlots + static_cast<std::uint32_t>(index) * 4, 0xffffffff);
        }
        if (const std::uint32_t raw = word_at(m_, slot + kSlotRaw); raw != 0) {
            s_.note(kMemDelete, {raw});
        }
        set_word_at(m_, slot + kSlotRaw, 0);
    }
    if (free_materials != 0) {
        free_unused_materials();
        s_.note(kPaletteClear);
    }
}

void PsxSpool::clear_unused_regions() {
    for (std::int32_t i = 0; i < kSlotCount; ++i) {
        const std::uint32_t slot = slot_addr(i);
        if (word_at(m_, slot + kSlotRaw) != 0 && static_cast<std::uint32_t>(i) != global_word(m_, kEnvSlots) &&
            static_cast<std::uint32_t>(i) != global_word(m_, kEnvSlots + 4) && byte_at(m_, slot + kSlotKeep) == 0) {
            clear_region(i, 0, 0);
        }
    }
    free_unused_materials();
}

void PsxSpool::clear_environment_regions() {
    clear_region(static_cast<std::int32_t>(global_word(m_, kEnvSlots)), 0, 0);
    clear_region(static_cast<std::int32_t>(global_word(m_, kEnvSlots + 4)), 0, 0);
    set_global_word(m_, kEnvSlots, 0xffffffff);
    set_global_word(m_, kEnvSlots + 4, 0xffffffff);
    free_unused_materials();
    set_global_word(m_, kEnvCursor, 0);
}

std::int32_t PsxSpool::init_free_slot() {
    const std::int32_t index = find_free_slot();
    const std::uint32_t slot = slot_addr(index);
    set_word_at(m_, slot + kSlotAnimTables, 0);
    set_word_at(m_, slot + kSlotPulse, 0);
    set_word_at(m_, slot + kSlotWib, 0);
    set_byte_at(m_, slot + kSlotKeep, 0);
    set_byte_at(m_, slot + kSlotState, 0);
    set_word_at(m_, slot + kSlotHooks, 0);
    set_word_at(m_, slot + kSlotModels, 0);
    set_byte_at(m_, slot + kSlotName, 0);
    set_byte_at(m_, slot + kSlotTexturesInRam, 0);
    set_byte_at(m_, slot + kSlotTexturesShrunk, 0);
    set_byte_at(m_, slot + kSlotHeapType, 1);
    set_word_at(m_, slot + kSlotRaw, 0);
    return index;
}

// ---- scene materials ----------------------------------------------------------------------------------------------------------

std::uint32_t PsxSpool::alloc_material(std::uint32_t checksum) {
    s_.note(kAlloc, {kMaterialStride, 0, 1, 0});
    const std::uint32_t record = static_cast<std::uint32_t>(s_.value(kAlloc));
    set_global_word(m_, kMaterialBytes, global_word(m_, kMaterialBytes) + 0x30);
    const std::uint32_t bucket = kMaterialBuckets + (checksum & 0x1ff) * 4;
    const std::uint32_t head = word_at(m_, bucket);
    set_word_at(m_, record + 0x28, 0);
    set_word_at(m_, record + 0x24, head);
    set_word_at(m_, bucket, record);
    if (const std::uint32_t next = word_at(m_, record + 0x24); next != 0) {
        set_word_at(m_, next + 0x28, record);
    }
    set_word_at(m_, record + 0x18, checksum);
    set_byte_at(m_, record + 0x12, 0);
    set_half_at(m_, record + 0x10, 0);
    return record;
}

void PsxSpool::unlink_material(std::uint32_t material) {
    if (const std::uint32_t next = word_at(m_, material + 0x24); next != 0) {
        set_word_at(m_, next + 0x28, word_at(m_, material + 0x28));
    }
    if (const std::uint32_t previous = word_at(m_, material + 0x28); previous != 0) {
        set_word_at(m_, previous + 0x24, word_at(m_, material + 0x24));
    }
    const std::uint32_t bucket = kMaterialBuckets + (word_at(m_, material + 0x18) & 0x1ff) * 4;
    if (material == word_at(m_, bucket)) {
        set_word_at(m_, bucket, word_at(m_, material + 0x24));
    }
    s_.note(kMemDelete, {material});
    set_global_word(m_, kMaterialBytes, global_word(m_, kMaterialBytes) - 0x30);
}

std::uint32_t PsxSpool::find_material(std::uint32_t checksum) {
    std::uint32_t node = word_at(m_, kMaterialBuckets + (checksum & 0x1ff) * 4);
    while (node != 0 && word_at(m_, node + 0x18) != checksum) {
        node = word_at(m_, node + 0x24);
    }
    return node;
}

void PsxSpool::material_iter_begin() {
    const std::uint32_t head = global_word(m_, kMaterialBuckets);
    set_global_word(m_, kMaterialIterBucket, 0);
    set_global_word(m_, kMaterialIterNode, head);
}

std::uint32_t PsxSpool::material_iter_next() {
    std::int32_t index = global_int(m_, kMaterialIterBucket);
    if (index >= 0x200) {
        return 0;
    }
    if (const std::uint32_t node = global_word(m_, kMaterialIterNode); node != 0) {
        set_global_word(m_, kMaterialIterNode, word_at(m_, node + 0x24));
        return node;
    }
    // the node chain is exhausted: scan the following buckets (the scan can run on into the iterator's own cell)
    std::uint32_t at = kMaterialBuckets + static_cast<std::uint32_t>(index) * 4;
    for (;;) {
        const std::uint32_t head = word_at(m_, at + 4);
        at += 4;
        ++index;
        set_global_word(m_, kMaterialIterNode, head);
        if (at >= kMaterialIterNode) {
            set_global_word(m_, kMaterialIterBucket, static_cast<std::uint32_t>(index));
            return 0;
        }
        if (head == 0) {
            continue;
        }
        set_global_word(m_, kMaterialIterBucket, static_cast<std::uint32_t>(index));
        if (index >= 0x200) {
            return 0;
        }
        set_global_word(m_, kMaterialIterNode, word_at(m_, head + 0x24));
        return head;
    }
}

std::uint32_t PsxSpool::trailing_tables(std::uint32_t region_image) {
    std::uint32_t at = region_image + word_at(m_, region_image + 4);
    while (word_at(m_, at) != 0xffffffff) {
        at = at + word_at(m_, at + 4) + 8;
    }
    return at + 4;
}

void PsxSpool::decrement_material_usage(std::int32_t region) {
    s_.check(region >= 0 && region <= 0x14, kMsgDecBadRegion);
    const std::uint32_t slot = slot_addr(region);
    s_.check(byte_at(m_, slot + kSlotName) != 0, kMsgDecNullName);
    s_.check(byte_at(m_, slot + kSlotState) != 0, kMsgDecNotUsable);
    const std::uint32_t raw = word_at(m_, slot + kSlotRaw);
    if (byte_at(m_, slot + kSlotState) == 2) {
        return;
    }
    const std::uint32_t models = word_at(m_, raw + 0xc + word_at(m_, raw + 8) * 0x24);
    s_.check(models < 10000, kMsgDecNumModels);
    std::uint32_t at = trailing_tables(raw) + 4 + models * 4;
    std::uint32_t count = word_at(m_, at - 4);
    s_.check(count < 0x3e9, kMsgDecNumChecksums);
    for (; count != 0; --count, at += 4) {
        const std::uint32_t material = word_at(m_, at);
        s_.check(half_at(m_, material + 0x10) < 10, kMsgDecTooMuchUsage);
        s_.check(short_at(m_, material + 0x10) != 0, kMsgDecZeroUsage);
        set_half_at(m_, material + 0x10, static_cast<std::uint32_t>(half_at(m_, material + 0x10) - 1));
    }
}

void PsxSpool::free_unused_materials() {
    material_iter_begin();
    for (std::uint32_t material = material_iter_next(); material != 0; material = material_iter_next()) {
        if (half_at(m_, material + 0x10) != 0) {
            continue;
        }
        if (byte_at(m_, material + 0x12) != 0) {
            const std::uint32_t node = word_at(m_, material + 0x1c);
            s_.check(half_at(m_, node + 4) != 0, kMsgMaterialRefCount);
            set_half_at(m_, word_at(m_, material + 0x1c) + 4, static_cast<std::uint32_t>(half_at(m_, word_at(m_, material + 0x1c) + 4) - 1));
            // Options_NothingF(u16 +0x20, u16 +0x22) is an empty function
        }
        s_.note(kTextureRelease, {word_at(m_, material + 0x14)});
        unlink_material(material);
    }
}

// ---- environments -------------------------------------------------------------------------------------------------------------

std::int32_t PsxSpool::env_index(std::int32_t region) {
    if (static_cast<std::uint32_t>(region) == global_word(m_, kEnvSlots)) {
        return 0;
    }
    return static_cast<std::uint32_t>(region) == global_word(m_, kEnvSlots + 4) ? 1 : -1;
}

void PsxSpool::attach_environment(std::int32_t region) {
    const std::uint32_t objects = word_at(m_, slot_addr(region) + kSlotObjects);
    if (global_word(m_, kEnvObjectListHead) == 0) {
        set_global_word(m_, kEnvObjectListHead, objects);
    } else {
        std::uint32_t tail = objects;
        for (std::uint32_t next = word_at(m_, objects + 0x20); next != 0; next = word_at(m_, next + 0x20)) {
            tail = next;
        }
        set_word_at(m_, tail + 0x20, global_word(m_, kEnvObjectListHead));
        set_global_word(m_, kEnvObjectListHead, objects);
    }
    const std::int32_t index = env_index(region);
    s_.check(index != -1, kMsgAttachBadRegion);
    set_word_at(m_, kEnvAttached + static_cast<std::uint32_t>(index) * 4, global_word(m_, kEnvCurrent));
}

void PsxSpool::detach_environment(std::uint32_t index) {
    index &= 0xff;
    s_.check(index < 2, kMsgDetachBadIndex);
    s_.check(int_at(m_, kEnvSlots + index * 4) != -1, kMsgDetachVoid);
    const std::int32_t region = int_at(m_, kEnvSlots + index * 4);
    set_word_at(m_, kEnvAttached + index * 4, 0);
    const std::uint32_t head = global_word(m_, kEnvObjectListHead);
    const std::uint32_t mine = word_at(m_, slot_addr(region) + kSlotObjects);
    const std::int32_t other_region = int_at(m_, kEnvSlots + (index ^ 1) * 4);
    std::uint32_t other = 0;
    if (other_region == -1 || (other = word_at(m_, slot_addr(other_region) + kSlotObjects)) == 0) {
        set_global_word(m_, kEnvObjectListHead, 0);
        return;
    }
    if (mine == head) {
        set_global_word(m_, kEnvObjectListHead, other);
        return;
    }
    s_.check(other == head, kMsgConfusedEnviroLists);
    s_.check(false, kMsgPatchingEnviroList);
    std::uint32_t tail = other;
    for (std::uint32_t next = word_at(m_, other + 0x20); next != mine; next = word_at(m_, next + 0x20)) {
        tail = next;
    }
    set_word_at(m_, tail + 0x20, 0);
}

// ---- the named-resource list and the queue ---------------------------------------------------------------------------------------

void PsxSpool::parse_named_resource_tag(std::uint32_t region_image, std::uint32_t tag) {
    s_.check(word_at(m_, region_image + 8) == 0 && word_at(m_, region_image + 0xc) == 0, kMsgSequencerItems);
    const std::uint32_t tables = trailing_tables(region_image);
    s_.note(kAlloc, {8, 0, 1, 0});
    const std::uint32_t node = static_cast<std::uint32_t>(s_.value(kAlloc));
    set_word_at(m_, node + 4, global_word(m_, kBitsResourceList));
    set_word_at(m_, node, tag);
    std::uint32_t count = word_at(m_, tag);
    std::uint32_t at = tag + 4;
    set_global_word(m_, kBitsResourceList, node);
    s_.check(count < 200, kMsgVastAnims);
    for (; count != 0; --count) {
        std::uint32_t frames = word_at(m_, at + 8);
        s_.check(frames < 1000, kMsgVastFrames);
        at += 12;
        for (; frames != 0; --frames, at += 8) {
            const std::uint32_t picture = word_at(m_, tables + 4 + word_at(m_, at + 4) * 4);
            set_word_at(m_, at + 4, picture);
            set_byte_at(m_, picture + 6, byte_at(m_, picture + 6) | 0x20);
            set_byte_at(m_, word_at(m_, at + 4) + 4, byte_at(m_, word_at(m_, at + 4) + 4) + 1);
            set_byte_at(m_, word_at(m_, at + 4) + 9, byte_at(m_, word_at(m_, at + 4) + 9) + 1);
            set_byte_at(m_, word_at(m_, at + 4) + 10, byte_at(m_, word_at(m_, at + 4) + 10) + 1);
            set_byte_at(m_, word_at(m_, at + 4) + 0xb, byte_at(m_, word_at(m_, at + 4) + 0xb) + 1);
        }
    }
}

std::uint32_t PsxSpool::find_named_resource(std::uint32_t name) {
    // names compare on at most eight characters, ignoring bit 5 of each (so case, and a few other pairs of characters)
    auto fold = [&](std::uint32_t at) { return static_cast<std::uint8_t>(byte_at(m_, at) & 0xdf); };
    for (std::uint32_t node = global_word(m_, kBitsResourceList); node != 0; node = word_at(m_, node + 4)) {
        const std::uint32_t tag = word_at(m_, node);
        const std::uint32_t count = word_at(m_, tag);
        std::uint32_t entry = tag + 4;
        for (std::uint32_t n = 0; n < count; ++n) {
            std::uint8_t d = fold(name);
            std::uint8_t c = fold(entry);
            std::int32_t i = 0;
            bool match = false;
            if (d == c) {
                for (;;) {
                    if (d == 0) {
                        break;
                    }
                    if (c == 0 || i >= 8) {
                        break;
                    }
                    d = fold(name + static_cast<std::uint32_t>(i) + 1);
                    c = fold(entry + static_cast<std::uint32_t>(i) + 1);
                    ++i;
                    if (d != c) {
                        break;
                    }
                }
            }
            if (d == 0 && c == 0) {
                match = true;
            } else if (i == 8) {
                match = true;
            }
            if (match) {
                return entry + 12;
            }
            entry += word_at(m_, entry + 8) * 8 + 12;
        }
    }
    return 0;
}

void PsxSpool::queue_copy_name(std::uint32_t first, std::uint32_t second) {
    std::uint32_t out = kQueueBase + global_word(m_, kQueueWrite) * kQueueStride;
    for (const std::uint32_t source : {first, second}) {
        for (std::uint32_t at = source; byte_at(m_, at) != 0; ++at, ++out) {
            set_byte_at(m_, out, byte_at(m_, at));
        }
    }
    set_byte_at(m_, out, 0);
}

std::uint32_t PsxSpool::queue_resource(std::uint32_t name, std::int32_t kind, std::uint32_t a, std::uint32_t b) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(kSlotCount); ++i) {
        const std::uint32_t slot = slot_addr(static_cast<std::int32_t>(i));
        if (byte_at(m_, slot + kSlotName) != 0 && names_equal(name, slot + kSlotName, 0xffffffff)) {
            return i;
        }
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(kQueueCount); ++i) {
        const std::uint32_t entry = kQueueBase + i * kQueueStride;
        if (byte_at(m_, entry + kQueueActive) == 0) {
            continue;
        }
        std::uint32_t at = 0;
        bool same = true;
        while (byte_at(m_, name + at) != 0) {
            if (byte_at(m_, name + at) != byte_at(m_, entry + at)) {
                same = false;
                break;
            }
            ++at;
        }
        if (same && byte_at(m_, entry + at) == '.') {
            return byte_at(m_, entry + kQueueSlot);
        }
    }
    const std::uint32_t slot_index = static_cast<std::uint32_t>(find_free_slot());
    const std::uint32_t slot = slot_addr(static_cast<std::int32_t>(slot_index));
    if (kind == 0 || kind == 2) {
        std::uint32_t out = slot + kSlotName;
        std::int32_t n = 0;
        for (std::uint8_t c = byte_at(m_, name); c != 0 && n < 9; ++n, ++out) {
            set_byte_at(m_, out, byte_at(m_, name + static_cast<std::uint32_t>(n)));
            c = byte_at(m_, name + static_cast<std::uint32_t>(n) + 1);
        }
        s_.check(n < 9, kMsgNameTooLong);
        set_byte_at(m_, out, 0);
    }
    set_byte_at(m_, slot + kSlotHeapType, b);
    const std::uint32_t write = global_word(m_, kQueueWrite);
    const std::uint32_t entry = kQueueBase + write * kQueueStride;
    s_.check(byte_at(m_, entry + kQueueActive) == 0, kMsgQueueOverflow);
    queue_copy_name(name, kPsxSuffix);
    set_byte_at(m_, kQueueBase + global_word(m_, kQueueWrite) * kQueueStride + kQueueKind, kind == 2 ? 2 : (kind != 0 ? 1 : 0));
    const std::uint32_t at = kQueueBase + global_word(m_, kQueueWrite) * kQueueStride;
    set_byte_at(m_, at + kQueueSlot, slot_index);
    set_byte_at(m_, at + kQueueActive, 1);
    set_byte_at(m_, at + kQueueHeap, a);
    set_global_word(m_, kQueueWrite, (global_word(m_, kQueueWrite) + 1) % 0x14);
    return slot_index;
}

std::uint32_t PsxSpool::spooler() {
    const std::uint32_t read = global_word(m_, kQueueRead);
    const std::uint32_t entry = kQueueBase + read * kQueueStride;
    if (byte_at(m_, entry + kQueueActive) == 0) {
        return 0;
    }
    const std::uint32_t mode = global_word(m_, kSpoolMode);
    if (mode == 0) {
        s_.note(kFileOpenResource, {entry});
        const std::uint32_t size = static_cast<std::uint32_t>(s_.value(kFileOpenResource));
        const std::uint8_t kind = byte_at(m_, entry + kQueueKind);
        if (kind > 2) {
            s_.assert_failed(kMsgUnknownQueueType);
        } else {
            std::uint32_t slot_index = byte_at(m_, entry + kQueueSlot);
            if (kind == 1) {
                if (global_word(m_, kEnvSlots) == 0xffffffff) {
                    set_global_word(m_, kEnvSlots, slot_index);
                } else if (global_word(m_, kEnvSlots + 4) == 0xffffffff) {
                    set_global_word(m_, kEnvSlots + 4, slot_index);
                } else {
                    const std::uint8_t victim = byte_at(m_, kEnvSlots + global_word(m_, kEnvCursor) * 4);
                    set_byte_at(m_, entry + kQueueSlot, victim);
                    clear_region(victim, 0, 0);
                    slot_index = byte_at(m_, entry + kQueueSlot);
                    set_word_at(m_, kEnvSlots + global_word(m_, kEnvCursor) * 4, slot_index);
                }
                slot_index = byte_at(m_, entry + kQueueSlot);
                std::uint32_t out = slot_addr(static_cast<std::int32_t>(slot_index)) + kSlotName;
                std::int32_t n = 0;
                for (std::uint8_t c = byte_at(m_, entry); c != '.' && n < 9; ++n, ++out) {
                    set_byte_at(m_, out, byte_at(m_, entry + static_cast<std::uint32_t>(n)));
                    c = byte_at(m_, entry + static_cast<std::uint32_t>(n) + 1);
                }
                s_.check(n < 9, kMsgEnvNameTooLong);
                set_byte_at(m_, out, 0);
            }
            slot_index = byte_at(m_, entry + kQueueSlot);
            const std::uint32_t slot = slot_addr(static_cast<std::int32_t>(slot_index));
            const std::uint8_t heap = byte_at(m_, slot + kSlotHeapType);
            s_.check(heap == 0 || heap == 1, kMsgHeapType);
            s_.note(kAlloc, {size, byte_at(m_, slot + kSlotHeapType), 1, 0});
            set_word_at(m_, slot + kSlotRaw, static_cast<std::uint32_t>(s_.value(kAlloc)));
            set_word_at(m_, slot + kSlotFileSize, size);
            s_.note(kFileLoadResource, {word_at(m_, slot + kSlotRaw)});
        }
        const std::uint32_t slot_index = byte_at(m_, entry + kQueueSlot);
        set_global_word(m_, kSpoolMode, 1);
        set_byte_at(m_, slot_addr(static_cast<std::int32_t>(slot_index)) + kSlotTexturesShrunk, byte_at(m_, entry + kQueueHeap));
        return slot_index;
    }
    if (mode == 1) {
        const std::uint32_t loading = global_word(m_, kLoading);
        if (loading != 0) {
            return loading;
        }
        set_global_word(m_, kSpoolMode, 2);
        return 0;
    }
    if (mode == 2) {
        const std::uint8_t kind = byte_at(m_, entry + kQueueKind);
        const std::uint32_t slot_index = byte_at(m_, entry + kQueueSlot);
        if (kind == 0) {
            parse_environment(slot_index);
        } else if (kind == 1) {
            parse_environment(slot_index);
            attach_environment(static_cast<std::int32_t>(byte_at(m_, entry + kQueueSlot)));
            const std::uint32_t tables = trailing_tables(word_at(m_, slot_addr(byte_at(m_, entry + kQueueSlot)) + kSlotRaw));
            const std::int32_t index = env_index(byte_at(m_, entry + kQueueSlot));
            set_word_at(m_, kEnvTables + static_cast<std::uint32_t>(index) * 4, tables);
        } else if (kind != 2) {
            s_.assert_failed(kMsgUnknownFileType);
        }
        set_byte_at(m_, entry + kQueueActive, 0);
        const std::int32_t next = global_int(m_, kQueueRead) + 1;
        set_global_word(m_, kSpoolMode, 0);
        set_global_word(m_, kQueueRead, static_cast<std::uint32_t>(next % 0x14));
        return static_cast<std::uint32_t>(next / 0x14);
    }
    s_.assert_failed(kMsgBadSpoolingMode);
    return 0;
}

void PsxSpool::drain_spooler() {
    while (global_word(m_, kLoading) != 0 || byte_at(m_, kQueueBase + global_word(m_, kQueueRead) * kQueueStride + kQueueActive) != 0) {
        static_cast<void>(spooler());
        s_.note(kFileContinue);
    }
}

bool PsxSpool::busy() {
    return global_word(m_, kLoading) != 0 || byte_at(m_, kQueueBase + global_word(m_, kQueueRead) * kQueueStride + kQueueActive) != 0;
}

}  // namespace opentony::runtime::psx
