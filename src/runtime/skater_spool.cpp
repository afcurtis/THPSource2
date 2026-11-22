#include "auto_views.hpp"
#include "skater_spool.hpp"

namespace opentony::runtime::skaterspool {

using namespace opentony::runtime::autoviews;

namespace {

// the engine entry points the view's spool state reaches (reported by address)
constexpr std::uint32_t kNew = 0x00466b70;                 // operator new(size)
constexpr std::uint32_t kBlockSize = 0x0046f510;           // Mem_GetBlockSize(block)
constexpr std::uint32_t kParseEnvironment = 0x004b2450;    // PSX_ParseEnvironment(slot)
constexpr std::uint32_t kDecrementUsage = 0x004b2120;      // PSX_DecrementMaterialUsage(slot)
constexpr std::uint32_t kFreeUnused = 0x004b2390;          // PSX_FreeUnusedMaterials()
constexpr std::uint32_t kPaletteClear = 0x0048a420;        // Palette_ClearSourceLookup()
constexpr std::uint32_t kNothingH = 0x00489490;            // Options_NothingH()
constexpr std::uint32_t kPartSet = 0x00480cd0;             // part window: set the part j to a value (this, j, value)
constexpr std::uint32_t kCharacterOfPlayer = 0x004b0c60;   // SkaterManager: the character player k uses (this, k)
constexpr std::uint32_t kCostumeOfPlayer = 0x004b0cd0;     // SkaterManager: the costume player k uses (this, k)
constexpr std::uint32_t kCurrentCharacter = 0x004b0c30;    // SkaterManager: the current character (this)
constexpr std::uint32_t kCurrentCostume = 0x004b0c40;      // SkaterManager: the current costume (this)
constexpr std::uint32_t kModelName = 0x004b1700;           // Skater_SelectModelName(character, costume): the file's stem
constexpr std::uint32_t kPartsManifest = 0x004b4940;       // PlayerSpool_ParsePartsManifest(buffer, name, names, 0, count, 0x20)
constexpr std::uint32_t kCopyPacketTables = 0x004a5080;    // PlayerSelect_CopyPacketTables(buffer)
constexpr std::uint32_t kPreviewConstruct = 0x00457420;    // the preview object's constructor (this, file, 0x40, 0x40, 0)
constexpr std::uint32_t kResetView = 0x004a6ea0;           // SkaterView_ResetSpool (native in PlayerSelect)
constexpr const char* kAlloc = "alloc";                    // FileIO_AllocLoadedBuffer(size, heap, 1, 0)
constexpr const char* kMemDelete = "mem_delete";           // Mem_Delete(block)

}  // namespace

// 0x004a62b0: keep the loaded file of one skater, freeing the one it had.
void SkaterSpool::set_slot_buffer(std::uint32_t self, std::int32_t slot, std::uint32_t buffer) {
    s_.check(slot >= 0 && slot < static_cast<std::int32_t>(kSkaters), kMsgBadSkater);
    const std::uint32_t at = self + kSlots + 4 * static_cast<std::uint32_t>(slot);
    if (word_at(m_, at) != 0) {
        s_.note(kMemDelete, {word_at(m_, at)});
    }
    set_word_at(m_, at, buffer);
}

// 0x004a6340: the preview object of a skater
std::uint32_t SkaterSpool::preview(std::uint32_t self, std::int32_t slot) {
    s_.check(slot >= 0 && slot < static_cast<std::int32_t>(kSkaters), kMsgBadSkater);
    return word_at(m_, self + kPreviews + 4 * static_cast<std::uint32_t>(slot));
}

// 0x004a63b0: the skater has a model file in memory, or is a custom skater (whose model is built)
bool SkaterSpool::slot_usable(std::uint32_t self, std::int32_t slot) {
    s_.check(slot >= 0 && slot < static_cast<std::int32_t>(kSkaters), kMsgBadSkater);
    return word_at(m_, self + kSlots + 4 * static_cast<std::uint32_t>(slot)) != 0 || (byte_at(m_, row(slot) + kRowFlags) & 2) != 0;
}

// 0x004a6440: put the skater's model file into PSX region `region`: free what the region held, copy the file into a fresh block and parse it.
void SkaterSpool::install_model(std::uint32_t self, std::int32_t region, std::int32_t slot) {
    if (!slot_usable(self, slot)) {
        return;
    }
    const std::uint32_t file = word_at(m_, self + kSlots + 4 * static_cast<std::uint32_t>(slot));
    s_.check(file != 0, kMsgNoModelFile);
    const std::uint32_t raw_at = 0x0056d428 + 0x18 + static_cast<std::uint32_t>(region) * 0x44;
    if (word_at(m_, raw_at) != 0) {
        service(kDecrementUsage, {static_cast<std::uint32_t>(region)});
        service(kFreeUnused);
        service(kPaletteClear);
        service(kNothingH);
        s_.note(kMemDelete, {word_at(m_, raw_at)});
        set_word_at(m_, raw_at, 0);
    }
    s_.check(word_at(m_, raw_at) == 0, kMsgPossibleLeak);
    service(kBlockSize, {file});
    const std::uint32_t size = service_value(kBlockSize);
    s_.note(kAlloc, {size, 0, 1, 0});
    const std::uint32_t copy = static_cast<std::uint32_t>(s_.value(kAlloc));
    set_word_at(m_, raw_at, copy);
    m_.copy(copy, file, size);      // (Mem_Copy, the engine's memcpy, runs for real in the probe)
    service(kParseEnvironment, {static_cast<std::uint32_t>(region)});
}

// 0x004a6560: for each of the model's part names (`parts`, `part_count` pointers) find its position in the list of names the manifest gave (`names`, `name_count`
// pointers) and store it for this skater; a part that is not in the list keeps what it had.
void SkaterSpool::resolve_part_indices(std::uint32_t self, std::int32_t skater, std::uint32_t names, std::int32_t name_count, std::uint32_t parts,
                                       std::int32_t part_count) {
    s_.check(names != 0, kMsgNoAnimNames);
    s_.check(parts != 0, kMsgNoModelNames);
    s_.check(name_count != 0, kMsgNoAnimParts);
    s_.check(part_count != 0, kMsgNoModelParts);
    if (part_count <= 0) {
        return;
    }
    std::uint32_t table = self + kParts + 4 * static_cast<std::uint32_t>(skater);
    for (std::int32_t j = 0; j < part_count; ++j, table += kPartStride) {
        const std::string wanted = m_.cstring(word_at(m_, parts + 4 * static_cast<std::uint32_t>(j)));
        for (std::int32_t i = 0; i < name_count; ++i) {
            if (m_.cstring(word_at(m_, names + 4 * static_cast<std::uint32_t>(i))) == wanted) {
                set_word_at(m_, table, static_cast<std::uint32_t>(i));
                break;
            }
        }
    }
}

// 0x004a6720: hand the part window the stored index of every part.
void SkaterSpool::apply_part_indices(std::uint32_t self, std::uint32_t target, std::int32_t skater) {
    s_.check(target != 0, kMsgNoSkater);
    std::uint32_t at = self + kParts + 4 * static_cast<std::uint32_t>(skater);
    for (std::uint32_t j = 0; j < kPartCount; ++j, at += kPartStride) {
        service(kPartSet, {target, j, byte_at(m_, at)});
    }
}

// 0x004a67a0
std::uint32_t SkaterSpool::loaded_count(std::uint32_t self) {
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < kSkaters; ++i) {
        if (word_at(m_, self + kSlots + 4 * i) != 0) {
            ++count;
        }
    }
    return count;
}

// 0x004a67c0: start over: nothing queued, nothing loaded, and every skater's costume as the players have it.
void SkaterSpool::begin(std::uint32_t self) {
    s_.check(global_word(m_, pspool::kGlobalManager) != 0, kMsgNoSpoolManager);
    p_.reset(global_word(m_, pspool::kGlobalManager));
    set_word_at(m_, self + kPrimary, 0xffffffffu);
    set_word_at(m_, self + kManifest, 0xffffffffu);
    set_word_at(m_, self + kThird, 0xffffffffu);
    for (std::uint32_t ch = 0; ch < kSkaters; ++ch) {
        set_word_at(m_, self + kLoaded + 4 * ch, 0);
        for (std::int32_t k = 0; static_cast<std::int32_t>(global_word(m_, kPlayerCount)) > 0 && k < static_cast<std::int32_t>(global_word(m_, kPlayerCount)); ++k) {
            service(kCharacterOfPlayer, {kSkaterManager, static_cast<std::uint32_t>(k)});
            if (service_value(kCharacterOfPlayer) == ch) {
                service(kCostumeOfPlayer, {kSkaterManager, static_cast<std::uint32_t>(k)});
                set_word_at(m_, self + kCostumes + 4 * ch, service_value(kCostumeOfPlayer));
            }
        }
    }
}

// 0x004a6880: one step of the screen's update: restart the spool when the costume changed, let the manager read, take what it finished, and queue the next skater.
void SkaterSpool::update(std::uint32_t self, std::int32_t current) {
    service(kCurrentCharacter, {kSkaterManager});
    const std::uint32_t character = service_value(kCurrentCharacter);
    service(kCurrentCostume, {kSkaterManager});
    const std::uint32_t costume = service_value(kCurrentCostume);
    if (word_at(m_, self + kCostumes + 4 * character) != costume) {
        service(kResetView, {self});
    }
    p_.pump(global_word(m_, pspool::kGlobalManager));
    if (busy(self)) {
        finish_loads(self);
    }
    if (!busy(self)) {
        load_resources(self, current);
    }
}

// 0x004a68f0: take what the manager finished: the skater's `.psx` goes into its slot, the `.psh` manifest names the parts.
void SkaterSpool::finish_loads(std::uint32_t self) {
    const std::uint32_t manager = global_word(m_, pspool::kGlobalManager);
    auto skater_costume = [&](std::uint32_t skater) { return word_at(m_, self + kCostumes + 4 * skater); };
    if (word_at(m_, self + kPrimary) != 0xffffffffu && p_.entry_ready(manager, static_cast<std::int32_t>(word_at(m_, self + kPrimary)))) {
        const std::uint32_t buffer = p_.take_buffer(manager, static_cast<std::int32_t>(word_at(m_, self + kPrimary)));
        s_.check(buffer != 0, kMsgInvalidData);
        const std::uint32_t current = word_at(m_, self + kCurrent);
        s_.check((byte_at(m_, row(static_cast<std::int32_t>(current)) + kRowFlags) & 2) == 0, kMsgCustomSpooled);
        service(kModelName, {current, skater_costume(current)});
        (void)service_value(kModelName);
        service(kBlockSize, {buffer});
        const std::uint32_t size = service_value(kBlockSize);
        s_.check(static_cast<std::int32_t>(size) <= 0x10800, kMsgTooBig);
        service(kCopyPacketTables, {buffer});
        set_slot_buffer(self, static_cast<std::int32_t>(word_at(m_, self + kCurrent)), buffer);
        service(kCurrentCharacter, {kSkaterManager});
        if (service_value(kCurrentCharacter) == word_at(m_, self + kCurrent)) {
            p_.virtual_call(word_at(m_, self + kOwner), 4);
        }
        set_word_at(m_, self + kPrimary, 0xffffffffu);
    }
    if (word_at(m_, self + kManifest) != 0xffffffffu && p_.entry_ready(manager, static_cast<std::int32_t>(word_at(m_, self + kManifest)))) {
        const std::uint32_t buffer = p_.take_buffer(manager, static_cast<std::int32_t>(word_at(m_, self + kManifest)));
        s_.check(buffer != 0, kMsgInvalidData);
        const std::uint32_t current = word_at(m_, self + kCurrent);
        s_.check((byte_at(m_, row(static_cast<std::int32_t>(current)) + kRowFlags) & 2) == 0, kMsgCustomSpooled);
        const std::uint32_t owner = word_at(m_, self + kOwner);
        const std::uint32_t names = owner + kOwnerModelNames + kOwnerStride * current;
        const std::uint32_t count_at = owner + kOwnerModelCount + kOwnerStride * current;
        service(kModelName, {current, skater_costume(current)});
        const std::uint32_t name = service_value(kModelName);
        service(kPartsManifest, {buffer, name, names, 0, count_at, 0x20});
        resolve_part_indices(self, static_cast<std::int32_t>(current), owner + kOwnerAnimNames, static_cast<std::int32_t>(word_at(m_, owner + kOwnerAnimCount)), names,
                             static_cast<std::int32_t>(word_at(m_, count_at)));
        s_.note(kMemDelete, {buffer});
        set_word_at(m_, self + kManifest, 0xffffffffu);
    }
    if (!busy(self)) {
        set_word_at(m_, self + kLoaded + 4 * word_at(m_, self + kCurrent), 1);
    }
}

// 0x004a6bf0: restart the manager and, working outward from the skater on show, queue the files of the first skater within six whose model is not loaded
// (and free the models of the skaters furthest away when more than six are held).
void SkaterSpool::load_resources(std::uint32_t self, std::int32_t current) {
    const std::uint32_t manager = global_word(m_, pspool::kGlobalManager);
    p_.reset(manager);
    set_word_at(m_, self + kPrimary, 0xffffffffu);
    set_word_at(m_, self + kManifest, 0xffffffffu);
    set_word_at(m_, self + kThird, 0xffffffffu);
    const auto order = pspool::PlayerSpooler::ring_values(static_cast<std::int32_t>(kSkaters), current);
    if (loaded_count(self) > 6) {
        for (std::size_t k = 7; k < 20; ++k) {
            const auto skater = static_cast<std::uint32_t>(order[k]);
            set_slot_buffer(self, static_cast<std::int32_t>(skater), 0);
            set_word_at(m_, self + kLoaded + 4 * skater, 0);
        }
    }
    for (std::size_t k = 0; k < 20; ++k) {
        const auto skater = static_cast<std::uint32_t>(order[k]);
        if (word_at(m_, self + kLoaded + 4 * skater) != 0) {
            if (k + 1 >= 6) {
                break;
            }
            continue;
        }
        const bool custom = (byte_at(m_, row(static_cast<std::int32_t>(skater)) + kRowFlags) & 2) != 0;
        if (!custom) {
            s_.check(word_at(m_, self + kManifest) == 0xffffffffu, kMsgIdNotEmpty);
            service(kModelName, {skater, word_at(m_, self + kCostumes + 4 * skater)});
            const std::string stem = m_.cstring(service_value(kModelName));
            set_word_at(m_, self + kManifest, static_cast<std::uint32_t>(p_.queue_text(manager, stem + ".psh", 0, 0, 0xffffffffu)));
            s_.check(word_at(m_, self + kPrimary) == 0xffffffffu, kMsgIdNotEmpty);
            service(kModelName, {skater, word_at(m_, self + kCostumes + 4 * skater)});
            const std::string stem2 = m_.cstring(service_value(kModelName));
            set_word_at(m_, self + kPrimary, static_cast<std::uint32_t>(p_.queue_text(manager, stem2 + ".psx", 1, 1, 0x10800)));
        }
        set_word_at(m_, self + kCurrent, skater);
        if (word_at(m_, self + kPreviewMade + 4 * skater) == 0) {
            service(kNew, {0x5c});
            std::uint32_t object = service_value(kNew);
            if (object != 0) {
                service(kPreviewConstruct, {object, word_at(m_, row(static_cast<std::int32_t>(skater)) + kRowFace), 0x40, 0x40, 0});
                object = service_value(kPreviewConstruct);
            }
            set_word_at(m_, self + kPreviews + 4 * skater, object);
            set_word_at(m_, self + kPreviewMade + 4 * skater, 1);
        }
        break;
    }
    if (busy(self)) {
        p_.force_finalize(manager);
    }
}

}  // namespace opentony::runtime::skaterspool
