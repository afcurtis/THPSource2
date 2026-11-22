#include "auto_views.hpp"
#include "createobj_kit.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

constexpr Word kRegionTable = 0x0056d428;        // PsxRegionNamesBySlot: 0x44-byte region records
constexpr Word kRegionStride = 0x44;
constexpr Word kModelTables = 0x0056d43c;        // PsxModelTablesByRegion (a field of the region record)
constexpr Word kRegionRaw = 0x0056d440;          // PsxRawRegionsBySlot
constexpr Word kRegionObjects = 0x0056d438;      // PsxObjectTablesByRegion
constexpr Word kRegionAnimations = 0x0056d444;   // AnimationTablesByRegion
constexpr Word kRegionHierarchies = 0x0056d448;  // AnimationHierarchyTablesByRegion
constexpr Word kRegionFlags = 0x0056d467;        // a byte of the region record
constexpr Word kCustomRecords = 0x0056a488;
constexpr Word kCustomStride = 0x80;

constexpr Word kNames = kScratch + 0x1000;       // the part manifest's name pointers (a local array of 0x78 words)
constexpr Word kTextures = kScratch + 0x1200;    // and its texture names
constexpr Word kManifestCount = kScratch + 0x1400;
constexpr Word kMap = kScratch + 0x1800;         // the part map (0x78 pairs) built in a local
constexpr Word kLog = kScratch + 0x1c00;         // the stitch log (0x78 words) built in a local
constexpr Word kListA = kScratch + 0x600;        // a checksum list kept in a local
constexpr Word kCountCell = kScratch + 0x5f0;
constexpr Word kNameBuffer = kScratch + 0x200;   // "sk2def%d" and "CUSTOM%d" built in locals

constexpr Word kLoadManifest = 0x004b47a0;       // PlayerSpool_LoadPartManifest(file, names, textures, &count, 0x78)
constexpr Word kFindRegion = 0x004b3230;         // PSX_FindLoadedRegionByName(name)
constexpr Word kInitFreeSlot = 0x004b4640;       // PSX_InitFreeSlot()
constexpr Word kFindMaterial = 0x004b2030;
constexpr Word kFindNoCase = 0x004b82b0;         // String_FindNoCase(haystack, needle)
constexpr Word kFreeUnusedMaterials = 0x004b2390;
constexpr Word kClearSourceLookup = 0x0048a420;
constexpr Word kNoCheck = 0x0046f470;            // Mem_NoCheck(block)
constexpr Word kAssignPartOrder = 0x00480cd0;    // Skater_AssignModelPartOrder(index, order) on the object
constexpr Word kSelectNamedRegion = 0x0047fe30;  // Object_SelectNamedPsxRegion(name) on the object
constexpr Word kFatten = 0x0048fd60;             // Skater_Fatten(width, 0, build)
constexpr Word kFindBodyGroup = 0x0041fe50;      // TextureSet_FindBodyGroup(name, group) on the part database

constexpr Word kMsgDatabaseGone = 0x00526370;
constexpr Word kMsgNameExists = 0x00526350;
constexpr Word kMsgNotUnique = 0x0052633c;
constexpr Word kMsgNoPartDatabase = 0x005262c4;
constexpr Word kMsgOnlyOnce = 0x005262d8;
constexpr Word kMsgNoBuildModels = 0x00526264;
constexpr Word kMsgNoModels = 0x00526258;
constexpr Word kMsgNoTexture = 0x0052624c;
constexpr Word kMsgNoUsage = 0x00526240;
constexpr Word kMsgNeedDatabase = 0x00526220;
constexpr Word kMsgNeedSkater = 0x005263b4;
constexpr Word kMsgNoSourceRegion = 0x005263fc;
constexpr Word kMsgNotBuilt = 0x005261cc;
constexpr Word kMsgNoRegion = 0x005261c0;
constexpr Word kMsgTooManyChecksums = 0x0052619c;
constexpr Word kMsgNoChecksums = 0x0052618c;
constexpr Word kMsgTooManyGroups = 0x00526058;
constexpr Word kMsgWrongParts = 0x005260bc;
constexpr Word kMsgBadAnim = 0x0052609c;
constexpr Word kMsgEmpty = 0x0055fe10;
constexpr Word kMsgNoSkaterToFree = 0x00526518;
constexpr Word kMsgNoSkaterToBuild = 0x00526674;
constexpr Word kMsgNoObject = 0x00526668;
constexpr Word kMsgNoStitchSource = 0x005263fc;
constexpr Word kMsgNoPartDb = 0x005262c4;
constexpr Word kPartDatabaseName = 0x00526008;   // "sk2def"
constexpr Word kAnimsName = 0x00525ffc;          // "anims_fe"
constexpr Word kEmptyName = 0x00526218;
constexpr Word kSk2defFormat = 0x0052602c;       // "sk2def%d"
constexpr Word kCustomFormat = 0x00526364;       // "CUSTOM%d"

}  // namespace

// ---- the part database ------------------------------------------------------------------------------------------------------------------------------------

// PlayerSpool_LoadPartManifest(file, names, textures, &count, 0x78): reads the manifest `file`, fills the name pointers (and the texture names, when asked for) and the count; answers the
// block it read the manifest into. The service's outputs are the scripted values: the count, then the names, then the texture names.
Word CreateObjKit::load_manifest(Word file, Word names, Word textures, Word count_address) {
    const char* service = "engine_004b47a0";
    engine(kLoadManifest, {file, names, textures, count_address, 0x78}, text_at(file));
    if (s_.live()) {
        return static_cast<Word>(s_.value(service));  // (the count and the names are already in memory; EAX is the block)
    }
    const Word count = static_cast<Word>(s_.value(service));
    set_word_at(m_, count_address, count);
    for (Word k = 0; k < count; ++k) {
        set_word_at(m_, names + 4 * k, static_cast<Word>(s_.value(service)));
    }
    if (textures != 0) {
        for (Word k = 0; k < count; ++k) {
            set_word_at(m_, textures + 4 * k, static_cast<Word>(s_.value(service)));
        }
    }
    return static_cast<Word>(s_.value(service));
}

// 0x0041f4b0 (thiscall, one argument): the part database reads the manifest: every part's name, its body group (head, upper body, lower body, board) and the index of its texture
// name among the group's names (a new name is added to the group)
void CreateObjKit::process_spooler_entry(Word self, Word file) {
    const Word count_address = self + 0x1864;
    set_word_at(m_, count_address, 0);
    set_word_at(m_, self + 0x1c68, 0);
    set_word_at(m_, self + 0x1c6c, 0);
    set_word_at(m_, self + 0x1c70, 0);
    set_word_at(m_, self + 0x1c74, 0);
    const Word block = load_manifest(file, kNames, kTextures, count_address);
    Word part = self + 4;
    for (std::int32_t i = 0; i < s(word_at(m_, count_address)); ++i) {
        copy_string(part, word_at(m_, kNames + 4 * u(i)));
        const std::int32_t group = parts_.group_index(part);
        std::int32_t index = s(method(kFindBodyGroup, self, {word_at(m_, kTextures + 4 * u(i)), u(group)}));
        if (index == -1) {
            check(s(word_at(m_, self + 0x1c68 + 4 * u(group))) < 0x10, kMsgTooManyGroups);
            index = s(word_at(m_, self + 0x1c68 + 4 * u(group)));
            set_word_at(m_, self + 0x1c68 + 4 * u(group), u(index + 1));
            copy_string(self + 0x1868 + (u(group) + u(index) * 4) * 0x10, word_at(m_, kTextures + 4 * u(i)));
        }
        set_word_at(m_, part + 0x18, u(group));
        set_word_at(m_, part + 0x10, u(index));
        part += 0x34;
    }
    delete_block(block);
}

// 0x0041f7c0 (thiscall, one argument): the animation index of every part is the position of its name in the animation manifest
void CreateObjKit::find_animation_indexes(Word self, Word file) {
    set_global_word(m_, kManifestCount, 0);
    const Word block = load_manifest(file, kNames, 0, kManifestCount);
    const Word count = global_word(m_, kManifestCount);
    check(count == 0x13, kMsgWrongParts);
    for (std::int32_t i = 0; i < s(word_at(m_, self + 0x1864)); ++i) {
        const Word part = self + 4 + 0x34 * u(i);
        set_word_at(m_, part + 0x14, 0xffffffffu);
        for (std::int32_t k = 0; k < s(count); ++k) {
            const Word name = word_at(m_, kNames + 4 * u(k));
            if (engine_value(kFindNoCase, {name, part}, text_at(name)) != 0) {
                set_word_at(m_, part + 0x14, u(k));
            }
        }
        check(int_at(m_, part + 0x14) != -1, kMsgBadAnim);
    }
    delete_block(block);
}

// 0x0041fc80 (thiscall, one argument): the checksums of the textures each part of `region` uses (at most five a part)
void CreateObjKit::build_checksums(Word self, Word region) {
    check(word_at(m_, self + 0x1864) == half_at(m_, region + 0x38), kMsgNotBuilt);
    check(region != 0, kMsgNoRegion);
    Word models = word_at(m_, region + 0x14);
    std::int32_t total = 0;
    for (std::int32_t i = 0; i < s(word_at(m_, self + 0x1864)); ++i) {
        const Word model = word_at(m_, models);
        models += 4;
        set_global_word(m_, kCountCell, 0);
        collect_checksums(self, region, model, kListA, kCountCell);
        const std::int32_t count = s(global_word(m_, kCountCell));
        check(count < 6, kMsgTooManyChecksums);
        for (std::int32_t k = 0; k < count; ++k) {
            set_word_at(m_, self + 0x20 + 0x34 * u(i) + 4 * u(k), word_at(m_, kListA + 4 * u(k)));
        }
        total += count;
        set_word_at(m_, self + 0x34 + 0x34 * u(i), u(count));
    }
    check(0 < total, kMsgNoChecksums);
}

// 0x0041f1e0 (thiscall, two arguments): the part database of custom skater `index` (0..9), read from the region "sk2def<index>"
Word CreateObjKit::part_database_construct(Word self, Word index, Word unused) {
    (void)unused;
    set_word_at(m_, self, kVtPartDatabase);
    for (Word i = 0; i < 0x78; ++i) {
        set_word_at(m_, self + 0x34 + 0x34 * i, 0);
    }
    set_word_at(m_, self + 0x1864, 0);
    set_word_at(m_, self + 0x1c68, 0);
    set_word_at(m_, self + 0x1c6c, 0);
    set_word_at(m_, self + 0x1c70, 0);
    set_word_at(m_, self + 0x1c74, 0);
    check(s(index) >= 0 && s(index) < 10, kMsgEmpty);
    set_string(kNameBuffer, "sk2def" + std::to_string(s(index)));
    set_word_at(m_, self + 0x1c78, engine_value(kFindRegion, {kNameBuffer}, text_at(kNameBuffer)));
    check(int_at(m_, self + 0x1c78) != -1, 0x00526010);
    process_spooler_entry(self, kPartDatabaseName);
    find_animation_indexes(self, kAnimsName);
    build_checksums(self, kRegionTable + word_at(m_, self + 0x1c78) * kRegionStride);
    return self;
}

// ---- the skater object ------------------------------------------------------------------------------------------------------------------------------------

// 0x0041ff50 (thiscall, three arguments): a skater object for custom skater `index`: its part database, the palette buffer (the caller's, or one allocated: nine pages of 0x1000
// bytes) and empty texture names
Word CreateObjKit::skater_construct(Word self, Word index, Word unused, Word palette_buffer) {
    (void)unused;
    createobj_object_skater_at(m_, self).set_field_0(static_cast<Word>(kVtMySkater));
    createobj_object_skater_at(m_, self).set_field_84(0xffffffffu);
    createobj_object_skater_at(m_, self).set_field_68(0);
    createobj_object_skater_at(m_, self).set_field_8(0);
    createobj_object_skater_at(m_, self).set_field_18(static_cast<Word>(index));
    const Word block = operator_new(0x208c);
    createobj_object_skater_at(m_, self).set_field_280(static_cast<Word>(block != 0 ? part_database_construct(block, index, 0) : 0));
    createobj_object_skater_at(m_, self).set_field_70(0);
    createobj_object_skater_at(m_, self).set_field_74(0);
    createobj_object_skater_at(m_, self).set_field_78(0);
    createobj_object_skater_at(m_, self).set_field_7c(0);
    if (palette_buffer == 0) {
        palette_buffer = alloc(0x9000, 1);
    }
    createobj_object_skater_at(m_, self).set_field_88(static_cast<Word>(palette_buffer));
    for (Word i = 0; i < 9; ++i) {
        set_word_at(m_, self + 0x140 + 4 * i, 0);
        set_word_at(m_, self + 0x164 + 0x10 * i, 0);
        set_word_at(m_, self + 0x168 + 0x10 * i, 0x80);
        set_word_at(m_, self + 0x16c + 0x10 * i, 0x80);
        set_word_at(m_, self + 0x8c + 4 * i, createobj_object_skater_at(m_, self).field_88() + 0x1000 * i);
        copy_string(self + 0x288 + 0x10 * i, kEmptyName);
    }
    createobj_object_skater_at(m_, self).set_field_1f4(0);
    createobj_object_skater_at(m_, self).set_field_27c(0);
    createobj_object_skater_at(m_, self).set_field_4(0);
    createobj_object_skater_at(m_, self).set_field_c(0);
    createobj_object_skater_at(m_, self).set_field_6c(0xffffffffu);
    return self;
}

// 0x004200d0 (fastcall): the build region's models, tables and name are released, the materials the skater held are counted down, the part database and the palette buffers go
void CreateObjKit::skater_destruct(Word self) {
    createobj_object_skater_at(m_, self).set_field_0(static_cast<Word>(kVtMySkater));
    if (static_cast<std::int32_t>(createobj_object_skater_at(m_, self).field_84()) != -1) {
        const Word record = kRegionTable + createobj_object_skater_at(m_, self).field_84() * kRegionStride;
        Word table = word_at(m_, record + 0x14);
        for (Word i = 0; i < 0x13; ++i) {
            check(word_at(m_, table) != 0, kMsgNoBuildModels);
            delete_block(word_at(m_, table));
            set_word_at(m_, table, 0);
            table += 4;
        }
        const Word models = word_at(m_, record + 0x14);
        check(models - 4 != 0, kMsgNoModels);
        delete_block(models - 4);
        set_word_at(m_, kRegionHierarchies + createobj_object_skater_at(m_, self).field_84() * kRegionStride, 0);
        set_word_at(m_, kRegionAnimations + createobj_object_skater_at(m_, self).field_84() * kRegionStride, 0);
        set_word_at(m_, kRegionRaw + createobj_object_skater_at(m_, self).field_84() * kRegionStride, 0);
        set_byte_at(m_, kRegionTable + createobj_object_skater_at(m_, self).field_84() * kRegionStride, 0);
        set_word_at(m_, kRegionObjects + createobj_object_skater_at(m_, self).field_84() * kRegionStride, 0);
        for (std::int32_t i = 0; i < s(createobj_object_skater_at(m_, self).field_27c()); ++i) {
            const Word material = engine_value(kFindMaterial, {word_at(m_, self + 0x1fc + 4 * u(i))});
            check(material != 0, kMsgNoTexture);
            check(half_at(m_, material + 0x10) != 0, kMsgNoUsage);
            set_half_at(m_, material + 0x10, half_at(m_, material + 0x10) - 1);
        }
        check(createobj_object_skater_at(m_, self).field_280() != 0 || createobj_object_skater_at(m_, self).field_27c() != 0, kMsgNeedDatabase);
        if (createobj_object_skater_at(m_, self).field_280() != 0) {
            delete_object(createobj_object_skater_at(m_, self).field_280());
        }
        parts_.free_palettes(self);
        engine(kFreeUnusedMaterials);
        engine(kClearSourceLookup);
    }
    createobj_object_skater_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x004200b0 (thiscall, one argument)
Word CreateObjKit::skater_scalar_delete(Word self, Word flags) {
    skater_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00422ec0 (cdecl): the skater object is deleted through its virtual destructor
void CreateObjKit::skater_delete(Word skater) {
    check(skater != 0, kMsgNoSkaterToFree);
    if (skater != 0) {
        delete_object(skater);
    }
}

// 0x004206c0 (fastcall): the skater has chosen its textures: the materials it uses are counted up (their usage keeps them from being freed) and the part database goes
void CreateObjKit::release_part_database(Word self) {
    check(word_at(m_, self + 0x27c) == 0, kMsgOnlyOnce);
    check(word_at(m_, self + 0x280) != 0, kMsgNoPartDatabase);
    find_board_checksum(self, self + 0x1fc, self + 0x27c);
    for (std::int32_t i = 0; i < s(word_at(m_, self + 0x27c)); ++i) {
        const Word material = engine_value(kFindMaterial, {word_at(m_, self + 0x1fc + 4 * u(i))});
        check(material != 0, kMsgNoTexture);
        set_half_at(m_, material + 0x10, half_at(m_, material + 0x10) + 1);
    }
    check(word_at(m_, self + 0x280) != 0, kMsgNoPartDatabase);
    if (word_at(m_, self + 0x280) != 0) {
        delete_object(word_at(m_, self + 0x280));
    }
    set_word_at(m_, self + 0x280, 0);
}

// ---- the regions ------------------------------------------------------------------------------------------------------------------------------------------

// 0x00420fa0 (thiscall): a table of 0x13 empty model pointers (its count in the word before) for the region the skater builds
void CreateObjKit::init_model_cache_slot(Word self) {
    const Word record = kRegionTable + word_at(m_, self + 0x84) * kRegionStride;
    set_word_at(m_, record + 0x14, alloc(0x50, 0));
    engine(kNoCheck, {word_at(m_, record + 0x14)});
    set_word_at(m_, record + 0x14, word_at(m_, record + 0x14) + 4);
    set_word_at(m_, word_at(m_, record + 0x14) - 4, 0x13);
    for (Word i = 0; i < 0x13; ++i) {
        set_word_at(m_, word_at(m_, record + 0x14) + 4 * i, 0);
    }
}

// 0x00420c10 (thiscall, one argument): a region of its own for the skater, a copy of the source region's record under the name CUSTOM<index>
void CreateObjKit::attach_region(Word self, Word skater_slot) {
    check(word_at(m_, self + 0x280) != 0, kMsgDatabaseGone);
    set_word_at(m_, self + 0x284, skater_slot);
    set_word_at(m_, self + 0x84, engine_value(kInitFreeSlot));
    const std::string name = "CUSTOM" + std::to_string(s(word_at(m_, self + 0x18)));
    set_string(kNameBuffer, name);
    check(engine_value(kFindRegion, {kNameBuffer}, text_at(kNameBuffer)) == 0xffffffffu, kMsgNameExists);
    const Word record = kRegionTable + word_at(m_, self + 0x84) * kRegionStride;
    copy_string(record, kNameBuffer);
    m_.copy(record, kRegionTable + word_at(m_, word_at(m_, self + 0x280) + 0x1c78) * kRegionStride, 0x44);
    set_string(kNameBuffer + 0x40, name);
    copy_string(record, kNameBuffer + 0x40);
    set_word_at(m_, kRegionHierarchies + word_at(m_, self + 0x84) * kRegionStride, 0);
    set_word_at(m_, kRegionAnimations + word_at(m_, self + 0x84) * kRegionStride, 0);
    set_word_at(m_, kRegionRaw + word_at(m_, self + 0x84) * kRegionStride, 0);
    set_word_at(m_, kRegionObjects + word_at(m_, self + 0x84) * kRegionStride, 0);
    check(word_at(m_, word_at(m_, self + 0x280) + 0x1c78) != word_at(m_, self + 0x84), kMsgNotUnique);
    init_model_cache_slot(self);
    reset_texture_names(self);
}

// 0x00421410 (fastcall): every texture name is empty again, the parts are stitched and the skater needs its scale applied
void CreateObjKit::reset_texture_names(Word self) {
    for (Word i = 0; i < 9; ++i) {
        copy_string(self + 0x288 + 0x10 * i, kEmptyName);
    }
    stitch_parts(self);
    set_word_at(m_, self + 8, 1);
}

// 0x00421460 (thiscall, one argument): every slot's order is told to the game object
void CreateObjKit::assign_model_part_order(Word self, Word object) {
    check(object != 0, kMsgNeedSkater);
    for (Word i = 0; i < 0x13; ++i) {
        method(kAssignPartOrder, object, {i, byte_at(m_, self + 0x1c + 4 * i)});
    }
}

// 0x00421690 (fastcall): the parts the skater's four body-group choices select are shown and the others hidden; the models are copied to the build region with the stitching of the
// seams, and the seams welded
void CreateObjKit::stitch_parts(Word self) {
    check(word_at(m_, self + 0x280) != 0, kMsgNoPartDb);
    const Word database = word_at(m_, self + 0x280);
    const Word source = kRegionTable + word_at(m_, database + 0x1c78) * kRegionStride;
    check(source != 0, kMsgNoSourceRegion);
    check(word_at(m_, word_at(m_, self + 0x280) + 0x1864) == half_at(m_, source + 0x38), kMsgNotBuilt);
    Word models = word_at(m_, source + 0x14);
    for (Word i = 0; i < half_at(m_, source + 0x38); ++i) {
        const Word model = word_at(m_, models + 4 * i);
        const Word part = word_at(m_, self + 0x280) + 0x34 * i;
        bool visible = false;
        for (Word k = 0; k < 4; ++k) {
            if (k == word_at(m_, part + 0x1c) && word_at(m_, self + 0x70 + 4 * k) == word_at(m_, part + 0x14)) {
                visible = true;
            }
        }
        if (visible) {
            set_half_at(m_, model, half_at(m_, model) & 0xffdf);
        } else {
            set_byte_at(m_, model, byte_at(m_, model) | 0x20);
        }
    }
    parts_.build_part_map(self, kMap);
    const Word build = kRegionTable + word_at(m_, self + 0x84) * kRegionStride;
    const Word block = operator_new(4);
    Word stitcher = 0;
    if (block != 0) {
        stitcher = stitcher_construct(block, kRegionTable + word_at(m_, word_at(m_, self + 0x280) + 0x1c78) * kRegionStride, build, kMap);
    }
    const Word src = kRegionTable + word_at(m_, word_at(m_, self + 0x280) + 0x1c78) * kRegionStride;
    weld(src, src, kRegionTable + word_at(m_, self + 0x84) * kRegionStride, kMap);
    if (stitcher != 0) {
        delete_object(stitcher);
    }
}

// 0x00422e30 (thiscall, one argument): the object takes the skater's scale (a byte of the custom record, 0x5a .. 0x7d): the three scale shorts, the region's flag and the fatten of the
// model
void CreateObjKit::apply_body_scale(Word self, Word object) {
    if (word_at(m_, self + 8) == 0) {
        return;
    }
    std::int32_t scale = byte_at(m_, kCustomRecords + word_at(m_, self + 0x284) * kCustomStride + 0x23);
    scale = scale < 0x5a ? 0x5a : scale;
    scale = scale > 0x7d ? 0x7d : scale;
    set_byte_at(m_, object + 5, byte_at(m_, object + 5) | 2);
    if (word_at(m_, self + 0xc) == 0 || word_at(m_, self + 0xc) == 1) {
        const Word value = word_at(m_, self + 0xc) == 0 ? 0x1000u : 0xb54u;
        set_half_at(m_, object + 0x28, value);
        set_half_at(m_, object + 0x2a, value);
        set_half_at(m_, object + 0x2c, value);
    }
    set_byte_at(m_, kRegionFlags + static_cast<Word>(byte_at(m_, object + 0x1f)) * kRegionStride, 0);
    if (s_.live()) {
        engine(kFatten, {object, u(scale), 0, word_at(m_, self + 0xc)});   // (the object is the call's ECX; the probe's stub reports the stack arguments alone)
    } else {
        engine(kFatten, {u(scale), 0, word_at(m_, self + 0xc)});
    }
    set_word_at(m_, self + 8, 0);
}

// 0x00423be0 (cdecl, two arguments): the game object `object` takes the skater's region, part order and scale
void CreateObjKit::build_object(Word skater, Word object) {
    check(skater != 0, kMsgNoSkaterToBuild);
    check(object != 0, kMsgNoObject);
    const Word record = parts_.region_record(skater);
    method(kSelectNamedRegion, object, {record}, text_at(record));
    assign_model_part_order(skater, object);
    apply_body_scale(skater, object);
}

}  // namespace opentony::runtime::createobj
