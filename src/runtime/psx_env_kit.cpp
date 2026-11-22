#include "auto_views.hpp"
#include "psx_env_kit.hpp"
#include "seam_addresses_generated.hpp"

#include <cstdio>
#include <utility>

namespace opentony::runtime::psxenvkit {

using namespace opentony::runtime::autoviews;

namespace {

using psx::kPaletteBase;
using psx::kPaletteFlag;
using psx::kQueueBase;
using psx::kQueueKind;
using psx::kQueueRead;
using psx::kQueueStride;
using psx::kSlotAnimHierarchy;
using psx::kSlotAnimTables;
using psx::kSlotChecksums;
using psx::kSlotHooks;
using psx::kSlotModelCount;
using psx::kSlotModels;
using psx::kSlotName;
using psx::kSlotObjects;
using psx::kSlotPulse;
using psx::kSlotRaw;
using psx::kSlotRgb;
using psx::kSlotState;
using psx::kSlotTexturesInRam;
using psx::kSlotWib;

// the retail functions the parser reaches (reported by address; the resolver answers with the native or the seam)
constexpr std::uint32_t kAllocObjectStorage = 0x0047fd70;     // TRG_AllocObjectStorage2(size): zeroed storage
constexpr std::uint32_t kInitObjectRecord = 0x0047fdb0;       // PSX_InitObjectRecord (thiscall)
constexpr std::uint32_t kDestroyObjectRecord = 0x0047fe20;    // PSX_DestroyObjectRecord (thiscall)
constexpr std::uint32_t kFindTrailingTables = 0x004b20f0;     // PSX_FindTrailingTables(raw)
constexpr std::uint32_t kFindSceneMaterial = 0x004b2030;      // PSX_FindSceneMaterial(checksum)
constexpr std::uint32_t kAllocSceneMaterial = 0x004b1f70;     // PSX_AllocSceneMaterial(checksum): the record
constexpr std::uint32_t kFreeUnusedMaterials = 0x004b2390;    // PSX_FreeUnusedMaterials()
constexpr std::uint32_t kClearSourceLookup = 0x0048a420;      // Palette_ClearSourceLookup()
constexpr std::uint32_t kFindSourceNode = 0x0048a400;         // Palette_FindSourceLookupNode(palette address)
constexpr std::uint32_t kCreatePalette = 0x0048a7e0;          // Palette_CreateFromPsxColors(address, colours, kind): the node
constexpr std::uint32_t kConvertColors = 0x004da680;          // Palette_ConvertColors(address, colours, count): the converted copy
constexpr std::uint32_t kGetCheat = 0x00416980;               // Career_GetCheat(0xd)
constexpr std::uint32_t kLoadTextureReferences = 0x004b2b70;  // PSX_LoadTextureReferences(slot, flags)
constexpr std::uint32_t kFinalize = 0x004647c0;               // PSX_FinalizeEnvironment(slot)
constexpr std::uint32_t kEnvIndex = 0x004b3990;               // Spool_GetEnvIndex(slot)
constexpr std::uint32_t kLoadZoneBlockmap = 0x004667e0;       // M3D_LoadZoneBlockmap(index, data)
constexpr std::uint32_t kPrepareWib = 0x00464a80;             // M3D_PrepareTextureWib(data)
constexpr std::uint32_t kParseNamedResource = 0x004b34f0;     // BITS_ParseNamedResourceTag(raw, data)

// assertion messages
constexpr std::uint32_t kMsgNoGeometry = 0x00537de8;          // "Environmental PSX has no geometry"
constexpr std::uint32_t kMsgNumChecksums = 0x0052dc3c;        // "Bad Numchecksums %d, region %s"
constexpr std::uint32_t kMsgZeroFaceLength = 0x00537dd4;      // "Zero face length"
constexpr std::uint32_t kMsgOldBoundingBox = 0x00533d10;      // "Old style bounding box found"

constexpr std::uint32_t kWibDefault = 0x00533944;             // a halfword: the value of a model's +0x18 when the file left -1

// the globals that mark "the bits PSX is being read" (the cheat palette fill is skipped for it)
constexpr std::uint32_t kBitsRegion = 0x0056af58;
constexpr std::uint32_t kBitsRegionFlag = 0x0056af5c;
constexpr std::uint32_t kFillWord = 0x42104210;

constexpr std::uint32_t kMaxChecksums = 0x3e8;
constexpr std::uint32_t kObjectSize = 0x4c;
constexpr std::uint32_t kRawObjectSize = 0x24;

// the tags the stream after the models can hold
constexpr std::int32_t kTagEnd = -1;
constexpr std::int32_t kTagWib = 6;
constexpr std::int32_t kTagPulse = 7;
constexpr std::int32_t kTagZoneBlockmap = 10;
constexpr std::int32_t kTagAnimations = 0x2a;
constexpr std::int32_t kTagAnimationsAlt = 0x2c;
constexpr std::int32_t kTagNamedResource = 0x45;
constexpr std::int32_t kTagHierarchy = 0x52454948;      // "HIER"
constexpr std::int32_t kTagRgb = 0x73424752;            // "RGBs"

// slot record bytes the parser writes that PsxSpool has no name for
constexpr std::uint32_t kSlotHasAnimations = 0x09;      // set by tag 0x2a/0x2c, cleared at the start (it is the byte after the 9-character name)
constexpr std::uint32_t kSlotModelFlag = 0x3a;          // the "no model needs the slow path" byte of PSX_FinalizeEnvironment

}  // namespace

std::uint32_t PsxEnvKit::call(std::uint32_t address, std::vector<std::uint32_t> args) {
    char name[24];
    std::snprintf(name, sizeof name, "engine_%08x", address);
    s_.note(name, std::move(args));
    return static_cast<std::uint32_t>(s_.value(name));
}

// 0x004b2450 -------------------------------------------------------------------------------------------------------------------

void PsxEnvKit::build_object_table(std::uint32_t record, std::uint32_t raw, std::int32_t objects) {
    const std::uint32_t block = call(kAllocObjectStorage, {static_cast<std::uint32_t>(objects) * kObjectSize + 4});
    std::uint32_t table = 0;
    if (block != 0) {
        set_word_at(m_, block, static_cast<std::uint32_t>(objects));
        table = block + 4;
        call(seam::crt::FUN_00500400, {table, kObjectSize, static_cast<std::uint32_t>(objects), kInitObjectRecord, kDestroyObjectRecord});
    }
    set_word_at(m_, record + kSlotObjects, table);
    std::uint32_t source = raw + 0xc;
    std::uint32_t target = table;
    for (std::int32_t i = 0; i < objects; ++i, source += kRawObjectSize, target += kObjectSize) {
        set_half_at(m_, target + 4, half_at(m_, source));
        set_half_at(m_, target + 6, half_at(m_, source + 2));
        set_word_at(m_, target + 8, word_at(m_, source + 4));
        set_word_at(m_, target + 0xc, word_at(m_, source + 8));
        set_word_at(m_, target + 0x10, word_at(m_, source + 0xc));
        set_half_at(m_, target + 0x14, half_at(m_, source + 0x10));
        set_half_at(m_, target + 0x16, half_at(m_, source + 0x12));
        set_half_at(m_, target + 0x18, half_at(m_, source + 0x14));
        set_half_at(m_, target + 0x1a, half_at(m_, source + 0x16));
        set_byte_at(m_, target + 0x1c, byte_at(m_, source + 0x18));
        set_byte_at(m_, target + 0x1d, byte_at(m_, source + 0x19));
        set_byte_at(m_, target + 0x1e, byte_at(m_, source + 0x1a));
        set_byte_at(m_, target + 0x1f, byte_at(m_, source + 0x1b));
        set_word_at(m_, target + 0x24, word_at(m_, source + 0x20));
    }
}

// One group of palettes of the trailing tables: `count` entries of `stride_words` dwords, each a checksum (the entry's first word) and the colours after it. A palette
// the source lookup does not know yet is created and converted; the cheat 0xd turns the colours of a region that is not the bits PSX into one fill value first.
void PsxEnvKit::read_palettes(std::uint32_t& entry, std::uint32_t& data, std::uint32_t count, std::uint32_t stride_words, std::uint32_t colours,
                              std::uint32_t fill_words, std::uint32_t kind) {
    for (; count != 0; --count) {
        if (global_word(m_, kBitsRegionFlag) == 0 && (call(kGetCheat, {0xd}) & 0xff) != 0 && global_byte(m_, kPaletteFlag) != 0) {
            for (std::uint32_t i = 0; i < fill_words; ++i) {
                set_word_at(m_, data + i * 4, kFillWord);
            }
        }
        const std::uint32_t address = word_at(m_, entry) + global_word(m_, kPaletteBase);
        std::uint32_t node = call(kFindSourceNode, {address});
        if (node == 0) {
            // (retail's caller pushes a fourth word, 1; the function never reads it)
            node = call(kCreatePalette, {address, data, kind});
            set_word_at(m_, node + 0xc, call(kConvertColors, {address, data, colours}));
        }
        entry += stride_words * 4;
        data += stride_words * 4;
    }
}

void PsxEnvKit::parse_environment(std::uint32_t slot) {
    const std::uint32_t record = slot_record(slot);
    const std::uint32_t raw = word_at(m_, record + kSlotRaw);
    set_global_word(m_, 0x0056db1c, 0);       // PsxEnvironmentCursor: the tag the wib table came from
    const std::int32_t objects = static_cast<std::int32_t>(word_at(m_, raw + 8));
    if (objects > 0) {
        build_object_table(record, raw, objects);
    }

    const std::uint32_t models_at = raw + 0xc + static_cast<std::uint32_t>(objects) * kRawObjectSize;
    const std::uint32_t queue = kQueueBase + global_word(m_, kQueueRead) * kQueueStride;
    s_.check(!(byte_at(m_, queue + kQueueKind) == 1 && objects == 0), kMsgNoGeometry);
    const std::uint32_t model_count = word_at(m_, models_at);
    const std::uint32_t models = models_at + 4;
    for (std::uint32_t i = 0; i < model_count; ++i) {
        set_word_at(m_, models + i * 4, word_at(m_, models + i * 4) + raw);
    }
    set_word_at(m_, record + kSlotModels, models);

    const std::uint32_t trailing = call(kFindTrailingTables, {raw});
    set_word_at(m_, record + kSlotChecksums, trailing);
    const std::uint32_t checksum_count = word_at(m_, trailing + model_count * 4);
    const std::uint32_t checksums = trailing + 4 + model_count * 4;
    std::uint32_t tags = word_at(m_, raw + 4) + raw;
    s_.check(checksum_count < kMaxChecksums + 1, kMsgNumChecksums);

    // the checksum list becomes the scene materials the faces point at
    const std::uint32_t palette_base = global_word(m_, kPaletteBase);
    if (palette_base != 0) {
        for (std::uint32_t i = 0; i < checksum_count; ++i) {
            set_word_at(m_, checksums + i * 4, word_at(m_, checksums + i * 4) + palette_base);
        }
    }
    for (std::uint32_t i = 0; i < checksum_count; ++i) {
        std::uint32_t material = call(kFindSceneMaterial, {word_at(m_, checksums + i * 4)});
        if (material == 0) {
            material = call(kAllocSceneMaterial, {word_at(m_, checksums + i * 4)});
        }
        const std::uint32_t uses = half_at(m_, material + 0x10);
        if (uses != 0xffff) {
            set_half_at(m_, material + 0x10, uses + 1);
        }
        set_word_at(m_, checksums + i * 4, material);
    }
    for (std::uint32_t i = 0; i < model_count; ++i) {
        const std::uint32_t model = word_at(m_, models + i * 4);
        std::uint32_t face = model + 0x1c + (static_cast<std::uint32_t>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
        for (std::uint32_t faces = half_at(m_, model + 6); faces != 0; --faces) {
            const std::uint32_t word = word_at(m_, face);
            if ((word & 1) != 0) {
                set_word_at(m_, face + 0x10, word_at(m_, checksums + word_at(m_, face + 0x10) * 4));
            }
            s_.check((word_at(m_, face) & 0xfffc0000) != 0, kMsgZeroFaceLength);
            face += (word_at(m_, face) >> 0x12) * 4;
        }
    }
    call(kFreeUnusedMaterials);
    call(kClearSourceLookup);
    set_global_word(m_, kBitsRegion, 0);
    set_global_word(m_, kBitsRegionFlag, 0);
    if (byte_at(m_, record + kSlotName) == 'b' && byte_at(m_, record + kSlotName + 1) == 'i' && byte_at(m_, record + kSlotName + 2) == 't' && byte_at(m_, record + kSlotName + 3) == 's' &&
        byte_at(m_, record + kSlotName + 4) == 0) {
        set_global_word(m_, kBitsRegion, 1);
        set_global_word(m_, kBitsRegionFlag, 1);
    }

    // the palettes: 16-colour entries (a checksum and 32 bytes of colours), then 256-colour ones (a checksum and 512 bytes)
    const std::uint32_t again = call(kFindTrailingTables, {raw});
    std::uint32_t group = again + 4 + (checksum_count + model_count) * 4;
    const std::uint32_t small_count = word_at(m_, group);
    std::uint32_t entry = group + 4;
    std::uint32_t data = group + 8;
    read_palettes(entry, data, small_count, 9, 0x10, 8, 1);
    const std::uint32_t wide_count = word_at(m_, entry);
    entry += 4;
    data = entry + 4;
    read_palettes(entry, data, wide_count, 0x81, 0x100, 0x80, 2);

    set_byte_at(m_, record + kSlotTexturesInRam, 1);
    call(kLoadTextureReferences, {slot, 0});
    set_global_word(m_, kBitsRegion, 0);
    set_global_word(m_, kBitsRegionFlag, 0);
    call(kFinalize, {slot});
    set_byte_at(m_, record + kSlotHasAnimations, 0);
    set_word_at(m_, record + kSlotWib, 0);
    set_word_at(m_, record + kSlotPulse, 0);
    set_word_at(m_, record + kSlotRgb, 0);

    // the tags after the models: [tag, size, size bytes of data] until -1
    for (std::int32_t tag = static_cast<std::int32_t>(word_at(m_, tags)); tag != kTagEnd; tag = static_cast<std::int32_t>(word_at(m_, tags))) {
        const std::uint32_t size = word_at(m_, tags + 4);
        const std::uint32_t body = tags + 8;
        if (tag > kTagAnimationsAlt) {
            if (tag == kTagNamedResource) {
                call(kParseNamedResource, {raw, body});
            } else if (tag == kTagHierarchy) {
                set_word_at(m_, record + kSlotAnimHierarchy, body);
            } else if (tag == kTagRgb) {
                set_word_at(m_, record + kSlotRgb, body);
            }
        } else if (tag == kTagAnimationsAlt || tag == kTagAnimations) {
            set_word_at(m_, record + kSlotAnimTables, body);
            set_byte_at(m_, record + kSlotHasAnimations, 1);
        } else if (tag == kTagWib) {
            set_word_at(m_, record + kSlotWib, body);
            call(kPrepareWib, {body});
            set_global_word(m_, 0x0056db1c, tags);
        } else if (tag == kTagPulse) {
            set_word_at(m_, record + kSlotPulse, body);
        } else if (tag == kTagZoneBlockmap) {
            const std::uint32_t index = call(kEnvIndex, {slot});
            call(kLoadZoneBlockmap, {index, body});
        }
        tags = body + size;
    }
    set_byte_at(m_, record + kSlotState, 1);
}

// 0x004647c0 -------------------------------------------------------------------------------------------------------------------

void PsxEnvKit::finalize_environment(std::uint32_t slot) {
    const std::uint32_t record = slot_record(slot);
    const std::uint32_t models = word_at(m_, record + kSlotModels);
    std::uint32_t model = word_at(m_, models);
    const std::int32_t model_count = static_cast<std::int32_t>(word_at(m_, models - 4));
    std::uint32_t shared_flags = 0;       // retail's local at 0x10: the count of flag-1 entries, kept across the models
    std::uint32_t unnamed = 0;            // the models whose halfword at +0x1a is -1
    std::uint32_t usable = 1;

    for (std::int32_t m = 0; m < model_count; ++m) {
        const std::uint32_t entries = half_at(m_, model + 2);
        const std::uint32_t total = half_at(m_, model + 4);
        const std::uint32_t faces = half_at(m_, model + 6);
        std::uint32_t any = 0;            // the OR of every face's flags
        std::uint32_t all = 0xffffffff;   // the AND of the high halves of their word at +0xc
        if (half_at(m_, model + 0x1a) == 0xffff) {
            ++unnamed;
        }
        s_.check(((byte_at(m_, model) >> 3) & 1) != 0, kMsgOldBoundingBox);
        if (half_at(m_, model + 0x18) == 0xffff) {
            set_half_at(m_, model + 0x18, global_half(m_, kWibDefault));
        }
        std::uint32_t entry = model + 0x1c;
        for (std::uint32_t i = 0; i < entries; ++i, entry += 8) {
            const std::uint32_t flags = byte_at(m_, entry + 6);
            if ((flags & 2) != 0) {
                const std::uint32_t value = half_at(m_, entry + 2);
                set_half_at(m_, entry + 2, 0);
                set_half_at(m_, entry, value << 3);
            }
            if ((flags & 0x10) != 0) {
                set_byte_at(m_, model + 1, byte_at(m_, model + 1) | 1);
            }
            if ((flags & 1) != 0) {
                ++shared_flags;
            }
        }
        if (shared_flags + entries > 100) {
            usable = 0;
        }
        if (total == faces + entries) {
            // the entries after these are the second set: they inherit the flags and the position of the first
            std::uint32_t from = entry - entries * 8;
            std::uint32_t to = entry;
            for (std::uint32_t i = 0; i < entries; ++i, from += 8, to += 8) {
                if ((byte_at(m_, from + 6) & 1) != 0) {
                    set_byte_at(m_, to + 6, byte_at(m_, to + 6) | 1);
                }
                if ((byte_at(m_, from + 6) & 2) != 0) {
                    set_byte_at(m_, to + 6, byte_at(m_, to + 6) | 2);
                    set_half_at(m_, to, half_at(m_, from));
                    set_half_at(m_, to + 2, 0);
                }
            }
            entry = from;
        }
        std::uint32_t face = entry + total * 8;
        for (std::uint32_t left = faces; left != 0; --left) {
            const std::uint32_t low = byte_at(m_, face);
            set_half_at(m_, face + 0xc, half_at(m_, face + 0xc) << 3);
            if ((low & 0x40) == 0) {
                set_word_at(m_, face, word_at(m_, face) ^ 0x80);
            }
            if ((word_at(m_, face) & 0x800) == 0) {
                set_word_at(m_, face + 8, word_at(m_, face + 8) & 0xffffff);
            }
            std::uint32_t word = word_at(m_, face);
            if ((word & 3) == 3 && (word & 0x20) != 0) {
                set_word_at(m_, face + 0x1c, word_at(m_, word_at(m_, face + 0x10) + 0xc));
            }
            word = word_at(m_, face);
            any |= word;
            all &= word_at(m_, face + 0xc) >> 16;
            if ((word & 0x10) != 0) {
                set_byte_at(m_, face + 7, byte_at(m_, face + 6));
            }
            face += (word_at(m_, face) >> 0x12) * 4;
        }
        if ((all & 1) != 0) {
            set_byte_at(m_, model, byte_at(m_, model) | 0x10);
        }
        if ((any & 0xc0) == 0) {
            set_byte_at(m_, model, byte_at(m_, model) | 0x20);
        }
        if ((any & 0x1000) != 0) {
            set_byte_at(m_, model, byte_at(m_, model) | 0x40);
        }
        if ((any & 4) != 0) {
            set_byte_at(m_, model, byte_at(m_, model) | 4);
        }
        if ((byte_at(m_, model) & 5) != 0) {
            usable = 0;
        }
        model = face;
    }
    set_byte_at(m_, record + kSlotModelFlag, usable);
    set_half_at(m_, record + kSlotModelCount, unnamed);

    // the objects: a singly linked list through +0x20, each stamped with its region, with two flags from the model it shows
    const std::uint32_t raw = word_at(m_, record + kSlotRaw);
    const std::int32_t objects = static_cast<std::int32_t>(word_at(m_, raw + 8));
    std::uint32_t object = word_at(m_, record + kSlotObjects);
    for (std::int32_t i = 0; i < objects; ++i, object += 0x4c) {
        set_word_at(m_, object + 0x20, i < objects - 1 ? object + 0x4c : 0);
        set_byte_at(m_, object + 0x1f, slot);
        const std::uint32_t table = word_at(m_, slot_record(slot & 0xff) + kSlotModels);
        if ((byte_at(m_, word_at(m_, table + half_at(m_, object + 0x1a) * 4)) & 0x10) != 0) {
            set_byte_at(m_, object + 4, byte_at(m_, object + 4) | 0x20);
        }
        const std::uint32_t stamped = slot_record(byte_at(m_, object + 0x1f)) + kSlotModels;
        if ((byte_at(m_, word_at(m_, word_at(m_, stamped) + half_at(m_, object + 0x1a) * 4)) & 0x20) != 0) {
            set_byte_at(m_, object + 5, byte_at(m_, object + 5) | 0x10);
        }
    }
}

}  // namespace opentony::runtime::psxenvkit
