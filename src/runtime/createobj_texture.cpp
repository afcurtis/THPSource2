#include "auto_views.hpp"
#include "createobj_kit.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

constexpr Word kRegionTable = 0x0056d428;        // PsxRegionNamesBySlot: 0x44-byte region records
constexpr Word kRegionStride = 0x44;
constexpr Word kSaveDir = kScratch + 0x400;      // the caller's sub-directory, kept in a local of 16 bytes
constexpr Word kLocalName = kScratch + 0x100;    // a bitmap name built in a local (32 bytes)
constexpr Word kCountCell = kScratch + 0x5f0;    // the count of a checksum list kept in a local
constexpr Word kListA = kScratch + 0x600;        // checksum lists kept in locals
constexpr Word kListB = kScratch + 0x700;
constexpr Word kListC = kScratch + 0x800;

constexpr Word kGetSubDir = 0x004496f0;
constexpr Word kSetSubDir = 0x00449700;
constexpr Word kSkatEditDir = 0x00526330;
constexpr Word kFindMaterial = 0x004b2030;       // PSX_FindSceneMaterial(checksum) -> the material record
constexpr Word kFindTrailingTables = 0x004b20f0; // PSX_FindTrailingTables(raw region) -> the tables
constexpr Word kScorePalette = 0x004b4e00;       // PSX_ScoreMaterialPaletteUsage(checksum)
constexpr Word kLoadBitmap = 0x004674d0;         // Skater_LoadBitmap(checksum, name)
constexpr Word kLoadBmpIntoMaterial = 0x00457e10;// Image_LoadBmpIntoMaterial(checksum, data, name)
constexpr Word kSetPaletteHandle = 0x00422460;   // CreatePart_SetPaletteHandle(slot, checksum)
constexpr Word kApplyColourPreset = 0x00422c10;  // CreatePart_ApplyColourPreset(slot, flag)
constexpr Word kUpdateAnimation = 0x004214f0;    // CreatePart_UpdateTextureAnimation(slot, flag)
constexpr Word kFindSetById = 0x0041dc30;        // TextureSet_FindById(id) on the texture-set manager
constexpr Word kFindBodyGroup = 0x0041fe50;      // TextureSet_FindBodyGroup(name, group) on the part database
constexpr Word kTextureManager = 0x0056a87c;     // the manager the create code is working with

// assertion messages (addresses of the format strings)
constexpr Word kMsgEmpty = 0x0055fe10;
constexpr Word kMsgBadRegion = 0x00526170;
constexpr Word kMsgNoRegion = 0x00526164;
constexpr Word kMsgNoModel = 0x00526150;
constexpr Word kMsgNoCount = 0x00526138;
constexpr Word kMsgNoList = 0x00526124;
constexpr Word kMsgNoRaw = 0x00526118;
constexpr Word kMsgTooManyChecksums = 0x00526104;
constexpr Word kMsgNoReverse = 0x005260d4;
constexpr Word kMsgNoModelHere = 0x005262b8;
constexpr Word kMsgNoBoard = 0x00526298;
constexpr Word kMsgNoPartDatabase = 0x005262c4;
constexpr Word kMsgNoSourceRegion = 0x005263fc;
constexpr Word kMsgNotBuilt = 0x005261cc;
constexpr Word kMsgNoChecksums = 0x00526424;
constexpr Word kMsgNoTexture = 0x00526410;
constexpr Word kMsgNoChecksumsInPart = 0x0052649c;
constexpr Word kMsgNotFound = 0x00526470;
constexpr Word kMsgNotUnique = 0x00526438;
constexpr Word kMsgZeroChecksum = 0x005264e0;
constexpr Word kMsgNoData = 0x00526510;
constexpr Word kMsgNoManager = 0x0052630c;
constexpr Word kMsgNoSet = 0x005262fc;

// the literals the part names are compared with (.data strings)
constexpr Word kPelvis = 0x00525f70;
constexpr Word kThigh = 0x005264d8;
constexpr Word kHead = 0x00525fd8;
constexpr Word kChest = 0x00525fc8;
constexpr Word kLeftBicep = 0x00525f94;
constexpr Word kLeftForearm = 0x00525f84;
constexpr Word kLeftHand = 0x00525f78;
constexpr Word kLeftThigh = 0x00525f64;
constexpr Word kLeftShin = 0x00525f58;
constexpr Word kLeftShoe = 0x00525f4c;

}  // namespace

// ---- the checksums of the materials a part uses -----------------------------------------------------------------------------------------------------------

// 0x0041f900 (thiscall on the part database, four arguments): the checksums of the textures the faces of `model` use, appended to `list` (`count` is the number held; no checksum twice).
// A face names a texture record (+0x10) and, when its flag 0x20 is set, a palette (+0x1c) that the region's trailing table of materials must agree with.
void CreateObjKit::collect_checksums(Word self, Word region, Word model, Word list, Word count) {
    check(region == kRegionTable + word_at(m_, self + 0x1c78) * kRegionStride, kMsgBadRegion);
    check(region != 0, kMsgNoRegion);
    check(model != 0, kMsgNoModel);
    check(count != 0, kMsgNoCount);
    check(list != 0, kMsgNoList);
    const Word raw = word_at(m_, region + 0x18);
    check(raw != 0, kMsgNoRaw);
    const Word table_index = word_at(m_, word_at(m_, region + 0x14) - 4);
    const Word tables = engine_value(kFindTrailingTables, {raw});
    const Word materials = word_at(m_, tables + table_index * 4);
    const Word first = tables + table_index * 4 + 4;
    Word face = model + 0x1c + (static_cast<Word>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
    for (Word f = 0; f < half_at(m_, model + 6); ++f) {
        bool found = false;
        Word hit = 0;
        for (Word k = 0; k < materials; ++k) {
            const Word candidate = word_at(m_, first + 4 * k);
            const Word texture = word_at(m_, face + 0x10);
            if ((word_at(m_, face) & 0x20) != 0) {
                found = word_at(m_, texture + 0x18) == word_at(m_, candidate + 0x18) && word_at(m_, candidate + 0xc) == word_at(m_, face + 0x1c);
            } else {
                found = word_at(m_, texture + 0x18) == word_at(m_, candidate + 0x18);
            }
            if (found) {
                hit = k;
                break;
            }
        }
        if (found) {
            const Word checksum = word_at(m_, word_at(m_, first + 4 * hit) + 0x18);
            bool listed = false;
            for (std::int32_t k = 0; k < s(word_at(m_, count)); ++k) {
                if (word_at(m_, list + 4 * u(k)) == checksum) {
                    listed = true;
                }
            }
            if (!listed) {
                check(s(word_at(m_, count)) < 0x20, kMsgTooManyChecksums);
                set_word_at(m_, list + 4 * word_at(m_, count), checksum);
                set_word_at(m_, count, word_at(m_, count) + 1);
            }
        }
        check(found, kMsgNoReverse);
        face += (word_at(m_, face) >> 18) * 4;
    }
}

// the loop of the three material functions: the checksums of every visible part of `region` called `name` (compared with the part database's names)
void CreateObjKit::collect_named(Word database, Word region, Word name, Word list, Word count) {
    const std::string wanted = m_.cstring(name);
    const Word models = word_at(m_, region + 0x14);
    for (Word i = 0; i < half_at(m_, region + 0x38); ++i) {
        const Word model = word_at(m_, models + 4 * i);
        if ((byte_at(m_, model) & 0x20) == 0 && m_.cstring(database + 4 + 0x34 * i, 0x20) == wanted) {
            collect_checksums(database, region, model, list, count);
        }
    }
}

// 0x00421910 (thiscall on the skater, two arguments): among the textures of the parts called `name`, the biggest (or the smallest when `largest` is 0) by the size of the rectangle
// it covers in the picture memory (the width and height are bytes, their product is the area)
Word CreateObjKit::select_material_checksum(Word self, Word name, Word largest) {
    const Word database = word_at(m_, self + 0x280);
    const Word region = kRegionTable + word_at(m_, database + 0x1c78) * kRegionStride;
    check(region != 0, kMsgNoSourceRegion);
    check(word_at(m_, word_at(m_, self + 0x280) + 0x1864) == half_at(m_, region + 0x38), kMsgNotBuilt);
    set_global_word(m_, kCountCell, 0);
    collect_named(word_at(m_, self + 0x280), region, name, kListA, kCountCell);
    const Word count = global_word(m_, kCountCell);
    check(count != 0, kMsgNoChecksums);
    Word biggest = 0;
    Word smallest = 0xffffffff;
    Word biggest_checksum = 0;
    Word smallest_checksum = 0;
    for (std::int32_t i = 0; i < s(count); ++i) {
        const Word checksum = word_at(m_, kListA + 4 * u(i));
        const Word material = engine_value(kFindMaterial, {checksum});
        check(material != 0, kMsgNoTexture);
        const Word width = static_cast<std::uint8_t>(byte_at(m_, material + 10) - byte_at(m_, material) + 1);
        const Word height = static_cast<std::uint8_t>(byte_at(m_, material + 11) - byte_at(m_, material + 1) + 1);
        const Word area = height * width;
        if (area > biggest) {
            biggest_checksum = checksum;
            biggest = area;
        }
        if (area < smallest) {
            smallest_checksum = checksum;
            smallest = area;
        }
    }
    return (largest & 0xff) != 0 ? biggest_checksum : smallest_checksum;
}

// 0x00421ba0 (thiscall on the skater, three arguments): the checksum of the texture the parts called `wanted` use and the parts called `other` (and `third` when given) do not; a
// part with a single texture answers it at once
Word CreateObjKit::find_material_checksum(Word self, Word wanted, Word other, Word third) {
    check(word_at(m_, self + 0x280) != 0, kMsgNoPartDatabase);
    const Word database = word_at(m_, self + 0x280);
    const Word region = kRegionTable + word_at(m_, database + 0x1c78) * kRegionStride;
    check(region != 0, kMsgNoSourceRegion);
    check(word_at(m_, database + 0x1864) == half_at(m_, region + 0x38), kMsgNotBuilt);
    constexpr Word kCountWanted = kCountCell;
    constexpr Word kCountOther = kCountCell + 4;
    constexpr Word kCountThird = kCountCell + 8;
    set_global_word(m_, kCountWanted, 0);
    set_global_word(m_, kCountOther, 0);
    set_global_word(m_, kCountThird, 0);
    collect_named(database, region, wanted, kListA, kCountWanted);
    check(global_word(m_, kCountWanted) != 0, kMsgNoChecksumsInPart);
    if (global_word(m_, kCountWanted) == 1) {
        return global_word(m_, kListA);
    }
    collect_named(database, region, other, kListB, kCountOther);
    if (third != 0) {
        collect_named(database, region, third, kListC, kCountThird);
    }
    Word candidates = 0;
    Word result = 0;
    for (std::int32_t i = 0; i < s(global_word(m_, kCountWanted)); ++i) {
        const Word checksum = word_at(m_, kListA + 4 * u(i));
        Word kept = checksum;
        for (std::int32_t k = 0; k < s(global_word(m_, kCountOther)); ++k) {
            if (checksum == word_at(m_, kListB + 4 * u(k))) {
                kept = 0;
            }
        }
        for (std::int32_t k = 0; k < s(global_word(m_, kCountThird)); ++k) {
            if (checksum == word_at(m_, kListC + 4 * u(k))) {
                kept = 0;
            }
        }
        if (kept != 0) {
            ++candidates;
            result = kept;
        }
    }
    check(result != 0, kMsgNotFound);
    check(candidates == 1, kMsgNotUnique);
    return result;
}

// 0x00422020 (thiscall on the skater, two arguments): the checksum of the texture of one of the nine slots of the body (the head's two textures, the chest, the arms, the legs)
Word CreateObjKit::select_slot_checksum(Word self, Word slot, Word flag) {
    check(word_at(m_, self + 0x280) != 0, kMsgNoPartDatabase);
    switch (slot) {
    case 0: return select_material_checksum(self, kHead, 1);
    case 1: return select_material_checksum(self, kHead, 0);
    case 2: return find_material_checksum(self, kChest, kLeftBicep, 0);
    case 3: return find_material_checksum(self, kLeftBicep, kChest, flag != 0 ? kLeftForearm : 0);
    case 4: return find_material_checksum(self, kLeftForearm, kLeftHand, 0);
    case 5: return find_material_checksum(self, kLeftHand, kLeftForearm, 0);
    case 6: return find_material_checksum(self, kLeftThigh, kPelvis, 0);
    case 7: return find_material_checksum(self, kLeftShin, kLeftThigh, 0);
    case 8: return find_material_checksum(self, kLeftShoe, kLeftShin, 0);
    default: return 0;
    }
}

// 0x00420420 (thiscall on the skater, two arguments): the checksums of the textures every part of the build region uses, and among those of the board the one that uses the most
// of the picture memory's palettes: the board's checksum
void CreateObjKit::find_board_checksum(Word self, Word list, Word count) {
    check(list != 0, kMsgEmpty);
    check(count != 0, kMsgEmpty);
    check(int_at(m_, self + 0x84) != -1, kMsgEmpty);
    const Word region_slot = word_at(m_, self + 0x84);
    const Word models = word_at(m_, kRegionTable + region_slot * kRegionStride + 0x14);
    check(models != 0, kMsgEmpty);
    Word anim = self + 0x1c;
    for (Word i = 0; i < 0x13; ++i, anim += 4) {
        const Word model = word_at(m_, models + 4 * i);
        check(model != 0, kMsgNoModelHere);
        const Word database = word_at(m_, self + 0x280);
        const Word region = kRegionTable + word_at(m_, database + 0x1c78) * kRegionStride;
        collect_checksums(database, region, model, list, count);
        Word best = 0;
        if (word_at(m_, anim) == 0x10) {
            set_global_word(m_, kCountCell, 0);
            collect_checksums(word_at(m_, self + 0x280), region, model, kListA, kCountCell);
            for (std::int32_t k = 0; k < s(global_word(m_, kCountCell)); ++k) {
                const Word checksum = word_at(m_, kListA + 4 * u(k));
                const std::int32_t score = s(engine_value(kScorePalette, {checksum}));
                if (score > s(best)) {
                    set_word_at(m_, self + 0x6c, checksum);
                    best = u(score);
                }
            }
        }
    }
    check(int_at(m_, self + 0x6c) != -1, kMsgNoBoard);
    log_line(0x00526280);
}

// ---- applying a texture to a part -------------------------------------------------------------------------------------------------------------------------

// 0x004221a0 (thiscall on the skater, six arguments): the bitmap `name` (with its skin tone digit) goes to slot `slot` unless the slot already holds it. When the skater spools its
// bitmaps the request is queued; otherwise the bitmap is loaded now and the slot's palette, colour preset and animation follow.
void CreateObjKit::apply_texture(Word self, Word checksum, Word name, Word tone, Word slot, Word request_a, Word request_b) {
    check(checksum != 0, kMsgZeroChecksum);
    copy_string(kLocalName, name);
    if (global_byte(m_, kLocalName + 7) != '0') {
        set_global_byte(m_, kLocalName + 7, tone + 0x31);
    }
    if (slot == 2) {
        createobj_texture_apply_at(m_, self).set_field_1f8(static_cast<Word>(find_material_checksum(self, kPelvis, kThigh, 0)));
    }
    const Word stored = self + slot * 0x10 + 0x288;
    if (m_.cstring(stored, 0x20) != m_.cstring(kLocalName, 0x20)) {
        if (createobj_texture_apply_at(m_, self).field_4() != 0) {
            spool_.add(kSpooler, checksum, kLocalName, s(slot), request_a, request_b, self);
            return;
        }
        engine(kLoadBitmap, {checksum, kLocalName}, text_at(kLocalName));
        if (slot == 2) {
            engine(kLoadBitmap, {createobj_texture_apply_at(m_, self).field_1f8(), kLocalName}, text_at(kLocalName));
        }
        copy_string(stored, kLocalName);
        method(kSetPaletteHandle, self, {slot, checksum});
        method(kApplyColourPreset, self, {slot, request_b});
        method(kUpdateAnimation, self, {slot, request_b});
    }
}

// 0x00422340 (thiscall on the skater, four arguments): the preview texture of the board, named like the bitmap but for the letter `s` or `l` at offset 4
void CreateObjKit::apply_preview_texture(Word self, Word checksum, Word name, Word kind, Word request) {
    check(checksum != 0, kMsgZeroChecksum);
    copy_string(kLocalName, name);
    set_global_byte(m_, kLocalName + 4, kind != 1 ? 0x73 : 0x6c);
    if (m_.cstring(self + 0x308, 0x20) != m_.cstring(kLocalName, 0x20)) {
        if (createobj_texture_apply_at(m_, self).field_4() != 0) {
            spool_.add(kSpooler, checksum, kLocalName, 8, request, 0, self);
            return;
        }
        engine(kLoadBitmap, {checksum, kLocalName}, text_at(kLocalName));
        method(kSetPaletteHandle, self, {8, checksum});
        method(kUpdateAnimation, self, {8, 0});
    }
}

// 0x00422b30 (thiscall on the skater, six arguments): a bitmap the spooler has loaded goes into the material of its checksum (and the second material of the legs), its name into the
// slot, and the palette, colour preset and animation of the slot follow
void CreateObjKit::attach_loaded_bitmap(Word self, Word data, Word checksum, Word slot, Word name, Word unused, Word flag) {
    (void)unused;
    check(data != 0, kMsgNoData);
    engine(kLoadBmpIntoMaterial, {checksum, data, name}, text_at(name));
    if (slot == 2) {
        engine(kLoadBmpIntoMaterial, {word_at(m_, self + 0x1f8), data, name}, text_at(name));
    }
    copy_string(self + slot * 0x10 + 0x288, name);
    method(kSetPaletteHandle, self, {slot, checksum});
    const Word preset = slot == 2 && flag != 0 ? 1u : 0u;
    method(kApplyColourPreset, self, {slot, preset});
    method(kUpdateAnimation, self, {slot, preset});
}

// 0x004208a0 (thiscall on the skater, three arguments): the texture set `id` of kind `kind` (0 head and face, 1 torso, 4 legs, 5 shoes, 6 board preview) is applied to the slots it
// covers; a set of another body group than the skater's resets the texture names. The caller's sub-directory is restored afterwards.
void CreateObjKit::apply_body_group(Word self, Word kind, Word id, Word tone) {
    copy_string(kSaveDir, engine_value(kGetSubDir));
    engine(kSetSubDir, {kSkatEditDir}, text_at(kSkatEditDir));
    check(global_word(m_, kTextureManager) != 0, kMsgNoManager);
    const Word set = method(kFindSetById, global_word(m_, kTextureManager), {id});
    check(set != 0, kMsgNoSet);
    if (set == 0) {
        return;
    }
    const Word database = createobj_texture_apply_at(m_, self).field_280();
    auto body_group = [&](Word group) { return method(kFindBodyGroup, database, {set + 8, group}); };
    auto texture_of = [&](Word field) { return word_at(m_, set + field); };
    switch (kind) {
    case 0:
        if (createobj_texture_apply_at(m_, self).field_70() != body_group(0)) {
            createobj_texture_apply_at(m_, self).set_field_70(static_cast<Word>(body_group(0)));
            reset_texture_names(self);
        }
        apply_texture(self, select_slot_checksum(self, 0, 0), texture_of(0x30) + 8, tone, 0, id, 0);
        if (byte_at(m_, set + 0x41) == 0) {
            createobj_texture_apply_at(m_, self).set_field_144(0);
        } else {
            apply_texture(self, select_slot_checksum(self, 1, 0), texture_of(0x34) + 8, tone, 1, id, 0);
        }
        break;
    case 1: {
        if (createobj_texture_apply_at(m_, self).field_74() != body_group(1)) {
            createobj_texture_apply_at(m_, self).set_field_74(static_cast<Word>(body_group(1)));
            reset_texture_names(self);
        }
        const Word chest = select_slot_checksum(self, 2, 0);
        const Word texture = texture_of(0x30);
        apply_texture(self, chest, texture + 8, tone, 2, id, byte_at(m_, texture + 0x18) == 0 ? 0u : 1u);
        apply_texture(self, select_slot_checksum(self, 3, static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int8_t>(byte_at(m_, set + 0x41))))), texture_of(0x34) + 8, tone, 3, id, 0);
        if (byte_at(m_, set + 0x41) == 0) {
            createobj_texture_apply_at(m_, self).set_field_150(0);
        } else {
            apply_texture(self, select_slot_checksum(self, 4, 0), texture_of(0x38) + 8, tone, 4, id, 0);
        }
        apply_texture(self, select_slot_checksum(self, 5, 0), texture_of(0x3c) + 8, tone, 5, id, 0);
        break;
    }
    case 4:
        if (createobj_texture_apply_at(m_, self).field_78() != body_group(2)) {
            createobj_texture_apply_at(m_, self).set_field_78(static_cast<Word>(body_group(2)));
            reset_texture_names(self);
        }
        apply_texture(self, select_slot_checksum(self, 6, 0), texture_of(0x30) + 8, tone, 6, id, 0);
        createobj_texture_apply_at(m_, self).set_field_80(static_cast<Word>(byte_at(m_, set + 0x41) == 0 ? 1u : 0u));
        break;
    case 5:
        apply_texture(self, select_slot_checksum(self, 7, 0), texture_of(0x30) + 8, tone, 7, id, 0);
        break;
    case 6:
        apply_preview_texture(self, select_slot_checksum(self, 8, 0), texture_of(0x30) + 8, createobj_texture_apply_at(m_, self).field_80(), id);
        break;
    default:
        break;
    }
    engine(kSetSubDir, {kSaveDir}, text_at(kSaveDir));
}

// 0x00422d80 (thiscall on the skater, four arguments): the torso, the legs and (when the legs' set has a second texture) the shoes
void CreateObjKit::apply_preview_groups(Word self, Word torso, Word legs, Word shoes, Word tone) {
    apply_body_group(self, 1, torso, tone);
    apply_body_group(self, 4, legs, tone);
    check(global_word(m_, kTextureManager) != 0, kMsgNoManager);
    const Word set = method(kFindSetById, global_word(m_, kTextureManager), {legs});
    if (byte_at(m_, set + 0x41) != 0) {
        apply_body_group(self, 5, shoes, tone);
        return;
    }
    createobj_texture_apply_at(m_, self).set_field_15c(0);
}

}  // namespace opentony::runtime::createobj
