#include "auto_views.hpp"
#include "createobj_kit.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

constexpr Word kRegionTable = 0x0056d428;
constexpr Word kRegionStride = 0x44;
constexpr Word kRegionRaw = 0x0056d440;
constexpr Word kCustomRecords = 0x0056a488;
constexpr Word kCustomStride = 0x80;
constexpr Word kTextureManager = 0x0056a87c;     // the manager the create code is working with
constexpr Word kCustomSkaterGlobal = 0x00568648;
constexpr Word kCustomSkaterGlobalB = 0x0056864c;
constexpr Word kSelectionRecords = 0x0053a514;   // SkaterSelectionRecords: 0x170 bytes each
constexpr Word kSelectionEnd = 0x0053c1d3;

constexpr Word kNameBuffer = kScratch + 0x200;
constexpr Word kBlocks = kScratch + 0x300;       // the four palette buffers in a local array

constexpr Word kOpenLoad = 0x00448dd0;           // FileIO_OpenLoad(name, 1)
constexpr Word kDefinitionName = 0x00527d14;     // "sk2def.psx"
constexpr Word kProcessing = 0x00527cfc;         // "Processing psx file %d\n"
constexpr Word kRelocate = 0x004b5010;           // PSX_RelocateDefinitionPointers(image, index)
constexpr Word kInitFreeSlot = 0x004b4640;
constexpr Word kParseEnvironment = 0x004b2450;   // PSX_ParseEnvironment(slot)
constexpr Word kBlockSize = 0x0046f510;          // Mem_GetBlockSize(block)
constexpr Word kFindById = 0x0041dc30;
constexpr Word kLoadColours = 0x00422690;        // CustomSkater_LoadColours(slot) on the skater
constexpr Word kInitRecord = 0x00422f20;         // CustomSkater_Init(slot, manager, trick manager)
constexpr Word kTricksInitialize = 0x004bb4f0;   // Tricks_InitializeManager() on the storage
constexpr Word kUnloadTricks = 0x00492b20;       // UnloadGTricks()

constexpr Word kMsgLeak = 0x00526874;
constexpr Word kMsgNoCustom = 0x00527d3c;
constexpr Word kMsgNeedTextureDb = 0x0055fe10;
constexpr Word kMsgTooManyCustom = 0x00526584;
constexpr Word kMsgPossibleLeak = 0x005265b8;
constexpr Word kMsgNoDatabase = 0x00526650;
constexpr Word kMsgNoSet = 0x00526638;
constexpr Word kMsgNoTexture = 0x00526624;
constexpr Word kMsgNoFilename = 0x00526610;
constexpr Word kMsgNoSkintone = 0x005265f4;
constexpr Word kMsgNoSkaters = 0x0052665c;

}  // namespace

// ---- the custom definition holder -------------------------------------------------------------------------------------------------------------------------

// 0x0042f880 (fastcall): no skaters, no file image, no regions (-1)
Word CreateObjKit::definition_construct(Word self) {
    createobj_def_definition_at(m_, self).set_field_0(static_cast<Word>(kVtCustomDefinition));
    createobj_def_definition_at(m_, self).set_field_18(0);
    for (Word i = 0; i < 4; ++i) {
        set_word_at(m_, self + 4 + 4 * i, 0);
        set_word_at(m_, self + 0x1c + 4 * i, 0xffffffffu);
    }
    createobj_def_definition_at(m_, self).set_field_14(0);
    return self;
}

// 0x0042f8d0 (fastcall): the four skaters are deleted, the file image freed
void CreateObjKit::definition_destruct(Word self) {
    createobj_def_definition_at(m_, self).set_field_0(static_cast<Word>(kVtCustomDefinition));
    for (Word i = 0; i < 4; ++i) {
        const Word skater = word_at(m_, self + 4 + 4 * i);
        if (skater != 0) {
            skater_delete(skater);
        }
        set_word_at(m_, self + 4 + 4 * i, 0);
    }
    if (createobj_def_definition_at(m_, self).field_18() != 0) {
        delete_block(createobj_def_definition_at(m_, self).field_18());
    }
    createobj_def_definition_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x0042f8b0 (thiscall, one argument)
Word CreateObjKit::definition_scalar_delete(Word self, Word flags) {
    definition_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x0042f950 (thiscall, three arguments): the definition file is opened and its region published under the name "sk2def<index>"
void CreateObjKit::load_definition_psx(Word self, Word slot, Word unused, Word index) {
    (void)unused;
    check(word_at(m_, self + 0x18) == 0, kMsgLeak);
    const Word image = engine_value(kOpenLoad, {kDefinitionName, 1}, text_at(kDefinitionName));
    s_.note("printf_log", {kProcessing});
    engine(kRelocate, {image, index});
    const Word region = engine_value(kInitFreeSlot);
    set_word_at(m_, self + 0x1c + slot * 4, region);
    set_word_at(m_, kRegionRaw + region * kRegionStride, image);
    engine(kParseEnvironment, {word_at(m_, self + 0x1c + slot * 4)});
    set_string(kRegionTable + word_at(m_, self + 0x1c + slot * 4) * kRegionStride, "sk2def" + std::to_string(s(index)));
}

// 0x0042fa10 (fastcall): every custom skater that exists gets its definition region (a copy of the file image) and its skater object
void CreateObjKit::load_all_definitions(Word self) {
    check(word_at(m_, self + 0x18) == 0, kMsgLeak);
    const Word image = engine_value(kOpenLoad, {kDefinitionName, 1}, text_at(kDefinitionName));
    set_word_at(m_, self + 0x18, image);
    for (Word i = 0; i < 4; ++i) {
        set_word_at(m_, kBlocks + 4 * i, byte_at(m_, kCustomRecords + kCustomStride * i + 0x26) == 0 ? 0u : alloc(0x9000, 1));
    }
    Word manager = global_word(m_, kCachedManager);
    if (manager == 0) {
        const Word block = operator_new(0x6a10);
        manager = block != 0 ? texture_manager_construct(block) : 0;
    }
    for (Word i = 0; i < 4; ++i) {
        if (byte_at(m_, kCustomRecords + kCustomStride * i + 0x26) == 0) {
            continue;
        }
        const Word size = engine_value(kBlockSize, {word_at(m_, self + 0x18)});
        const Word copy = alloc(size, 1);
        const Word length = engine_value(kBlockSize, {word_at(m_, self + 0x18)});
        m_.copy(copy, word_at(m_, self + 0x18), length);                                  // (Mem_Copy: plain memory, run natively)
        engine(kRelocate, {copy, i});
        const Word region = engine_value(kInitFreeSlot);
        set_word_at(m_, self + 0x1c + 4 * i, region);
        set_word_at(m_, kRegionRaw + region * kRegionStride, copy);
        engine(kParseEnvironment, {word_at(m_, self + 0x1c + 4 * i)});
        set_string(kRegionTable + word_at(m_, self + 0x1c + 4 * i) * kRegionStride, "sk2def" + std::to_string(i));
        set_word_at(m_, self + 4 + 4 * i, custom_skater_create(i, i, 1, word_at(m_, kBlocks + 4 * i), manager));
        check(word_at(m_, self + 4 + 4 * i) != 0, kMsgNoCustom);
    }
    delete_block(word_at(m_, self + 0x18));
    set_word_at(m_, self + 0x18, 0);
    if (global_word(m_, kCachedManager) == 0 && manager != 0) {
        delete_object(manager);
    }
}

// ---- the custom skaters -----------------------------------------------------------------------------------------------------------------------------------

// 0x004236d0 (cdecl): every selection record that is a custom skater (flag 2) gets its record initialised (at most four)
void CreateObjKit::init_all_custom_skaters() {
    set_global_word(m_, kCustomSkaterGlobal, 0);
    set_global_word(m_, kCustomSkaterGlobalB, 0);
    check(global_word(m_, kCachedManager) != 0, kMsgNeedTextureDb);
    const Word block = operator_new(0x140c);
    const Word tricks = block != 0 ? method(kTricksInitialize, block) : 0;
    std::int32_t count = 0;
    for (Word record = kSelectionRecords;; record += 0x170) {
        if ((byte_at(m_, record) & 2) != 0) {
            check(count < 4, kMsgTooManyCustom);
            engine(kInitRecord, {u(count), global_word(m_, kCachedManager), tricks});
            ++count;
        }
        if (static_cast<std::int32_t>(record + 0x170) > static_cast<std::int32_t>(kSelectionEnd)) {
            break;
        }
    }
    if (tricks != 0) {
        delete_object(tricks);
    }
}

// 0x00423800 (cdecl, five arguments): the skater object of custom skater `slot`, built for the definition region `index`: with the texture sets of its record applied (the head, torso,
// legs, shoes, board), then the part database released
Word CreateObjKit::custom_skater_create(Word slot, Word index, Word spooling, Word palette_buffer, Word manager) {
    check(global_word(m_, kTextureManager) == 0, kMsgPossibleLeak);
    Word created = manager;
    if (manager == 0) {
        const Word block = operator_new(0x6a10);
        created = block != 0 ? texture_manager_construct(block) : 0;
    }
    set_global_word(m_, kTextureManager, created);
    check(created != 0, kMsgNoDatabase);
    const Word record = kCustomRecords + slot * kCustomStride;
    const Word set = method(kFindById, global_word(m_, kTextureManager), {word_at(m_, record + 0x64)});
    check(set != 0, kMsgNoSet);
    check(word_at(m_, set + 0x30) != 0, kMsgNoTexture);
    check(word_at(m_, set + 0x30) != 0xfffffff8u, kMsgNoFilename);
    check(byte_at(m_, word_at(m_, set + 0x30) + 0xf) != '0', kMsgNoSkintone);
    const Word block = operator_new(0x31c);
    Word skater = 0;
    if (block != 0) {
        skater = skater_construct(block, index, 0, palette_buffer);
    }
    const Word tone = u(static_cast<std::int8_t>(byte_at(m_, word_at(m_, set + 0x30) + 0xf)) - 0x31);
    set_word_at(m_, skater + 0x68, tone);
    method(kLoadColours, skater, {slot});
    set_word_at(m_, skater + 4, spooling);
    attach_region(skater, slot);
    apply_body_group(skater, 0, word_at(m_, record + 0x64), word_at(m_, skater + 0x68));
    apply_preview_groups(skater, word_at(m_, record + 0x6c), word_at(m_, record + 0x74), word_at(m_, record + 0x78), word_at(m_, skater + 0x68));
    apply_body_group(skater, 6, word_at(m_, record + 0x7c), word_at(m_, skater + 0x68));
    release_part_database(skater);
    if (spooling == 0) {
        parts_.free_palettes(skater);
    }
    if (manager == 0 && global_word(m_, kTextureManager) != 0) {
        delete_object(global_word(m_, kTextureManager));
    }
    set_global_word(m_, kTextureManager, 0);
    return skater;
}

// 0x00423b50 (cdecl, two arguments): when the spooler has nothing left running, the palette buffers of the skaters are freed
Word CreateObjKit::free_all_custom_skaters(Word count, Word skaters) {
    check(skaters != 0, kMsgNoSkaters);
    if (spooler_pump(kSpooler) != 0) {
        for (; s(count) > 0; --count, skaters += 4) {
            if (word_at(m_, skaters) != 0) {
                parts_.free_palettes(word_at(m_, skaters));
            }
        }
        return 1;
    }
    return 0;
}

// 0x00423bd0 (cdecl): the player spool starts over and the spooler's queue is emptied
void CreateObjKit::reset_spool() {
    method(0x004b52b0, global_word(m_, 0x0056db4c));
    spooler_clear(kSpooler);
}

}  // namespace opentony::runtime::createobj
