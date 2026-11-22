#include "auto_views.hpp"
#include "createobj_kit.hpp"

namespace opentony::runtime::createobj {

using namespace opentony::runtime::autoviews;

using Word = CreateObjKit::Word;

namespace {

constexpr Word kGetSubDir = 0x004496f0;          // FileIO_GetSubDir() -> the current sub-directory (a string)
constexpr Word kSetSubDir = 0x00449700;          // FileIO_SetSubDir(name)
constexpr Word kSkatEditDir = 0x00526330;        // "skatedit\"
constexpr Word kPlayerSpoolHolder = 0x0056db4c;  // the player spool object
constexpr Word kPlayerSpoolPump = 0x004b55c0;
constexpr Word kPlayerSpoolReady = 0x004b58a0;
constexpr Word kPlayerSpoolTake = 0x004b5990;
constexpr Word kPlayerSpoolReset = 0x004b52b0;
constexpr Word kPlayerSpoolQueue = 0x004b5370;
constexpr Word kPlayerSpoolFinalize = 0x004b5580;
constexpr Word kSpoolFreeEntry = 0x1c;
constexpr Word kLoadCretex = 0x0041dea0;
constexpr Word kCretexName = 0x00525a0c;
constexpr Word kClearRegion = 0x004b32f0;        // PSX_ClearRegion(region, 1, 0)

}  // namespace

// the virtual destructor (slot 0, with the flag 1) of an object: the classes of this milestone run natively, any other is a service
void CreateObjKit::delete_object(Word object) {
    const Word target = word_at(m_, word_at(m_, object));
    switch (target) {
    case 0x0041d7b0: texture_database_scalar_delete(object, 1); break;
    case 0x0041dbc0: texture_manager_scalar_delete(object, 1); break;
    case 0x0041f350: part_database_scalar_delete(object, 1); break;
    case 0x004200b0: skater_scalar_delete(object, 1); break;
    case 0x004245a0: spooler_scalar_delete(object, 1); break;
    case 0x00424a60: stitcher_scalar_delete(object, 1); break;
    case 0x0042f8b0: definition_scalar_delete(object, 1); break;
    case 0x004b0b90: record_bank_scalar_delete(object, 1); break;
    case 0x004bb770: trick_manager_scalar_delete(object, 1); break;
    case 0x004c4400: singleton_scalar_delete(object, 1); break;
    case 0x0041cda0: keyconfig_scalar_delete(object, 1); break;
    default: engine(target, {object, 1}); break;
    }
}

// ---- the texture database ---------------------------------------------------------------------------------------------------------------------------------

// 0x0041d790: the database is empty
Word CreateObjKit::texture_database_construct(Word self) {
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtTextureDatabase));
    createobj_small_texture_at(m_, self).set_field_404(0);
    return self;
}

// 0x0041d7d0: every entry is freed (the count is read again after each one)
void CreateObjKit::texture_database_destruct(Word self) {
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtTextureDatabase));
    for (std::int32_t i = 0; i < s(createobj_small_texture_at(m_, self).field_404()); ++i) {
        delete_block(word_at(m_, self + 4 + 4 * u(i)));
        set_word_at(m_, self + 4 + 4 * u(i), 0);
    }
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x0041d7b0
Word CreateObjKit::texture_database_scalar_delete(Word self, Word flags) {
    texture_database_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// ---- the texture-set manager ------------------------------------------------------------------------------------------------------------------------------

// 0x0041db50: the base class, the database at +0x6608, the set count cleared and the "cretex" list loaded
Word CreateObjKit::texture_manager_construct(Word self) {
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
    texture_database_construct(self + 0x6608);
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtTextureManager));
    createobj_small_texture_at(m_, self).set_field_6604(0);
    method(kLoadCretex, self, {kCretexName});
    return self;
}

// 0x0041dbe0
void CreateObjKit::texture_manager_destruct(Word self) {
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtTextureManager));
    texture_database_destruct(self + 0x6608);
    createobj_small_texture_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x0041dbc0
Word CreateObjKit::texture_manager_scalar_delete(Word self, Word flags) {
    texture_manager_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// ---- the part database ------------------------------------------------------------------------------------------------------------------------------------

// 0x0041f370: the region the database was built from is cleared
void CreateObjKit::part_database_destruct(Word self) {
    set_word_at(m_, self, kVtPartDatabase);
    engine(kClearRegion, {word_at(m_, self + 0x1c78), 1, 0});
    set_word_at(m_, self, kVtBase);
}

// 0x0041f350
Word CreateObjKit::part_database_scalar_delete(Word self, Word flags) {
    part_database_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// ---- the spooler ------------------------------------------------------------------------------------------------------------------------------------------

// 0x00424610: every one of the 0x40 entries is marked free, and the spooler is idle
void CreateObjKit::spooler_clear(Word self) {
    for (Word i = 0; i < 0x40; ++i) {
        set_word_at(m_, self + 8 + 0x2c * i + kSpoolFreeEntry, 1);
    }
    createobj_small_spooler_at(m_, self).set_field_4(0);
}

// 0x00424550
Word CreateObjKit::spooler_construct(Word self) {
    createobj_small_spooler_at(m_, self).set_field_0(static_cast<Word>(kVtSpooler));
    spooler_clear(self);
    return self;
}

// 0x004245c0
void CreateObjKit::spooler_destruct(Word self) {
    createobj_small_spooler_at(m_, self).set_field_0(static_cast<Word>(kVtSpooler));
    spooler_clear(self);
    createobj_small_spooler_at(m_, self).set_field_0(static_cast<Word>(kVtBase));
}

// 0x004245a0
Word CreateObjKit::spooler_scalar_delete(Word self, Word flags) {
    spooler_destruct(self);
    if ((flags & 1) != 0) {
        operator_delete(self);
    }
    return self;
}

// 0x00424630: one step of the bitmap spooling. A loaded bitmap is attached to the skater that asked for it; then the next queued request starts loading. Answers 1 when the spooler
// has nothing running and nothing queued, 0 otherwise.
Word CreateObjKit::spooler_pump(Word self) {
    Word idle = 1;
    const Word saved = kScratch + 0x400;                                      // the caller's sub-directory, kept in a local
    copy_string(saved, engine_value(kGetSubDir));
    engine(kSetSubDir, {kSkatEditDir}, text_at(kSkatEditDir));
    if (createobj_small_spooler_at(m_, self).field_4() != 0) {
        const Word player_spool = global_word(m_, kPlayerSpoolHolder);
        method(kPlayerSpoolPump, player_spool);
        if ((method(kPlayerSpoolReady, player_spool, {createobj_small_spooler_at(m_, self).field_b1c()}) & 0xff) == 0) {
            idle = 0;
        } else {
            const Word buffer = method(kPlayerSpoolTake, player_spool, {createobj_small_spooler_at(m_, self).field_b1c()});
            attach_loaded_bitmap(createobj_small_spooler_at(m_, self).field_b30(), buffer, createobj_small_spooler_at(m_, self).field_b08(), createobj_small_spooler_at(m_, self).field_b20(), self + 0xb0c, createobj_small_spooler_at(m_, self).field_b2c(), createobj_small_spooler_at(m_, self).field_b28());
            delete_block(buffer);
            createobj_small_spooler_at(m_, self).set_field_4(0);
            if (spool_.find_free(self) != 0) {
                idle = 0;
            }
        }
    }
    if (createobj_small_spooler_at(m_, self).field_4() == 0 && spool_.find_free(self) != 0) {
        m_.copy(self + 0xb08, self + 8, 0x2c);
        createobj_small_spooler_at(m_, self).set_field_24(1);
        const Word player_spool = global_word(m_, kPlayerSpoolHolder);
        method(kPlayerSpoolReset, player_spool);
        createobj_small_spooler_at(m_, self).set_field_b1c(static_cast<Word>(method(kPlayerSpoolQueue, player_spool, {self + 0xb0c, 1, 1, 0xffffffffu})));
        method(kPlayerSpoolFinalize, global_word(m_, kPlayerSpoolHolder));
        createobj_small_spooler_at(m_, self).set_field_4(1);
        idle = 0;
    }
    engine(kSetSubDir, {saved}, text_at(saved));
    return idle & 0xff;
}

}  // namespace opentony::runtime::createobj
