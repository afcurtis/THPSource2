#include "auto_views.hpp"
#include "front.hpp"

namespace opentony::runtime::frontend {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t u(std::int32_t v) { return static_cast<std::uint32_t>(v); }

constexpr std::uint32_t kManager = 0x0056c580;              // the skater manager (the this of its methods)
constexpr std::uint32_t kSelectionRecords = 0x0053a514;     // flags of the character table: bit 0 the steamer sounds, bit 1 a custom skater
constexpr std::uint32_t kCharacterNames = 0x0053a3d8;
constexpr std::uint32_t kCharacterStride = 0x170;
constexpr std::uint32_t kLevelTable = 0x0053c1f8;
constexpr std::uint32_t kLevelStride = 0x1ac;
constexpr std::uint32_t kCustomObjects = 0x00568648;        // per player: the custom skater object
constexpr std::uint32_t kModelRegions = 0x0056a848;         // per player: the model region
constexpr std::uint32_t kRegionTables = 0x0056d43c;         // the PSX region tables, 0x44 bytes per region
constexpr std::uint32_t kLoading = 0x0056db40;
constexpr std::uint32_t kPark = 0x005614c4;
constexpr std::uint32_t kWaiting = 0x005618c8;
constexpr std::uint32_t kQuitTimer = 0x00561cc0;
constexpr std::uint32_t kQuitFlag = 0x00561cbc;

}  // namespace

// 0x004521c0: bring one player's skater into memory: the custom skater (a definition and an object) or the stock model of the character
void FrontScreens::skater_load_player_resources(std::uint32_t player) {
    const std::uint32_t slot = engine_value(0x004b0c60, {kManager, player});
    const std::uint32_t record = engine_value(0x004b1430, {kManager, player});
    if (record != 0) {
        engine(0x004b14a0, {kManager, player});
        const std::uint32_t mapped = engine_value(0x004b1980, {player});
        // retail pushes the player ahead of the index and leaves it on the stack for this call: (character, slot)
        engine(0x004b18d0, s_.live() ? std::vector<std::uint32_t>{mapped, player} : std::vector<std::uint32_t>{mapped});
        const std::uint32_t second = engine_value(0x004b1980, {player});
        const std::uint32_t x = engine_value(0x004b50a0, {player, second});
        const std::uint32_t memory = operator_new(0x2c);
        const std::uint32_t definition = memory != 0 ? engine_value(0x0042f880, {memory}) : 0;
        engine(0x0042f950, {definition, player, 0, x});
        if (definition != 0) {
            delete_object(definition);
        }
        const std::uint32_t created = engine_value(0x00423800, {player, x, 0, 0, 0, 0});
        set_word_at(m_, kCustomObjects + 4 * player, created);
        const std::uint32_t region = engine_value(0x004203c0, {created});
        set_word_at(m_, kModelRegions + 4 * player, region);
        engine(0x004b14a0, {kManager, player});
        return;
    }
    std::uint32_t region;
    if ((byte_at(m_, kSelectionRecords + slot * kCharacterStride) & 2) == 0) {
        const std::uint32_t costume = engine_value(0x004b0cd0, {kManager, player});
        const std::uint32_t name = engine_value(0x004b1700, {slot, costume});
        region = engine_value(0x004b43e0, {name, player});
    } else {
        const std::uint32_t memory = operator_new(0x2c);
        const std::uint32_t definition = memory != 0 ? engine_value(0x0042f880, {memory}) : 0;
        const std::uint32_t x = engine_value(0x004b50a0, {player, slot});
        const std::uint32_t custom = engine_value(0x004b1810, {slot});
        engine(0x0042f950, {definition, custom, 0, x});
        if (definition != 0) {
            delete_object(definition);
        }
        const std::uint32_t custom_again = engine_value(0x004b1810, {slot});
        const std::uint32_t created = engine_value(0x00423800, {custom_again, x, 0, 0, 0, 0});
        set_word_at(m_, kCustomObjects + 4 * player, created);
        region = engine_value(0x004203c0, {created});
    }
    set_word_at(m_, kModelRegions + 4 * player, region);
}

// 0x00452390: a region shares another's animation tables (and its raw region when asked)
void FrontScreens::psx_share_animation_region(std::uint32_t region, std::uint32_t source, std::uint32_t raw) {
    check(region != 0xffffffffu, 0x0052c9a4);
    const std::uint32_t to = region * 0x44;
    const std::uint32_t from = source * 0x44;
    set_word_at(m_, kRegionTables + 0xc + to, word_at(m_, kRegionTables + 0xc + from));
    set_word_at(m_, kRegionTables + 0x8 + to, word_at(m_, kRegionTables + 0x8 + from));
    set_word_at(m_, kRegionTables + from, word_at(m_, kRegionTables + to));
    if (raw != 0) {
        set_word_at(m_, kRegionTables + 0x4 + to, word_at(m_, kRegionTables + 0x4 + from));
    }
}

// 0x00452d30: holding both start buttons (or the second pad's) for 220 ticks quits the level load; true once it has
bool FrontScreens::quit_combo() {
    if (global_byte(m_, kQuitFlag) == 0) {
        if (!((global_byte(m_, 0x0056b0d8) != 0 && global_byte(m_, 0x0056b0e8) != 0) || (global_byte(m_, 0x0056b244) != 0 && global_byte(m_, 0x0056b254) != 0))) {
            set_global_word(m_, kQuitTimer, 0);
        } else {
            if (global_word(m_, kQuitTimer) == 0) {
                set_global_word(m_, kQuitTimer, global_word(m_, front::kSimTick));
            }
            if (global_word(m_, front::kSimTick) - global_word(m_, kQuitTimer) > 0xdc) {
                if ((engine_value(0x0047ac40) & 0xff) != 0) {
                    engine(0x0047ae10, {2});
                }
                clear_screen();
                set_global_byte(m_, kQuitFlag, 1);
                return true;
            }
        }
        if (global_byte(m_, 0x00561cbd) == 0) {
            return false;
        }
    }
    return true;
}

// 0x00452430: poll the pads while the level loads; the back button (or the quit combination) ends the wait
std::uint32_t FrontScreens::wait_for_button(std::uint32_t skip) {
    input_update_action_states();
    if ((global_byte(m_, 0x0056aff9) != 0 && (skip & 0xff) == 0) || quit_combo()) {
        set_global_word(m_, kWaiting, 1);
        set_global_byte(m_, 0x0056aff9, 0);
    }
    if (global_word(m_, front::kPlayerCount) != 2) {
        return global_word(m_, kWaiting);
    }
    if ((global_byte(m_, 0x0056b165) != 0 && (skip & 0xff) == 0) || quit_combo()) {
        set_global_byte(m_, 0x0056b165, 0);
        set_global_word(m_, kWaiting, 1);
        return 1;
    }
    return global_word(m_, kWaiting);
}

// 0x004524a0: load a level: the loading screen, the sound banks, the players, the resources of the level and the panel, the script, then the goals screen
void FrontScreens::load_game() {
    set_global_byte(m_, kLoading, 1);
    set_global_word(m_, 0x00561c50, 0);
    set_global_word(m_, 0x00561c54, 0);
    set_global_byte(m_, 0x0056aff9, 0);
    engine(0x004302c0, {0, 0, 1, 0});
    engine(0x0045d830, {0});
    engine(0x004dfe50);
    if (engine_value(0x00469400) != 0) {
        return;
    }
    if (global_word(m_, 0x0056a89c) != 0) {
        engine(0x00466a00);
    }
    if (global_byte(m_, kPark) != 0) {
        engine(0x004301b0);
    }
    const std::uint32_t level = global_word(m_, front::kCurrentLevel);
    s_.note(kSvcPrintfLog, {0x0052caac});
    set_global_word(m_, 0x0052dd14, word_at(m_, 0x0053c244 + level * kLevelStride));
    engine(0x004c9320, {0x12a6cc58});
    engine(0x00448f40, {2000});
    std::uint32_t bank;
    if (global_word(m_, front::kPlayerCount) == 2) {
        s_.note(kSvcPrintfLog, {0x0052caa4});
        bank = 0x0052ca98;
    } else {
        const std::uint32_t character = engine_value(0x004b0c30, {kManager});
        if ((byte_at(m_, kSelectionRecords + character * kCharacterStride) & 1) != 0) {
            s_.note(kSvcPrintfLog, {0x0052ca88});
            bank = 0x0052ca7c;
        } else {
            s_.note(kSvcPrintfLog, {0x0052ca70});
            bank = 0x0052ca64;
        }
    }
    engine(0x004ad020, {bank}, m_.cstring(bank, 0x20));
    engine(0x004ad180, {word_at(m_, 0x0053c23c + level * kLevelStride)}, m_.cstring(word_at(m_, 0x0053c23c + level * kLevelStride), 0x20));
    engine(0x004ada20, {0});
    engine(0x00448f40, {4});
    const std::uint32_t count = (global_word(m_, front::kPlayerCount) != 2 || global_word(m_, front::kGameMode) == 10) ? 1 : 2;
    set_global_byte(m_, kLoading, 0);
    for (int pass = 0; pass < 2; ++pass) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t slot = engine_value(0x004b18a0, {i != 0 ? 1u : 0u});
            const bool custom = (byte_at(m_, kSelectionRecords + slot * kCharacterStride) & 2) != 0;
            if (custom == (pass == 0)) {
                skater_load_player_resources(i);
                s_.note(kSvcPrintfLog, {0x0052ca4c});
            }
        }
    }
    set_global_byte(m_, kLoading, 1);
    set_global_byte(m_, 0x0056dcac, engine_value(0x004b37a0, {0x0052ca44, 0, 0, 1}, m_.cstring(0x0052ca44, 0x10)));
    set_global_byte(m_, 0x0056dd5c, engine_value(0x004b37a0, {0x0052ca3c, 0, 0, 1}, m_.cstring(0x0052ca3c, 0x10)));
    set_global_byte(m_, 0x0056dcb4, 0xff);
    engine(0x004b3df0);
    if (byte_at(m_, 0x0053c240 + level * kLevelStride) != 0) {
        s_.note(kSvcPrintfLog, {0x0052ca14});
        set_global_byte(m_, 0x0056dcad, engine_value(0x004b37a0, {0x0052ca08, 0, 0, 1}, m_.cstring(0x0052ca08, 0x10)));
        engine(0x004b3df0);
    }
    if (global_byte(m_, kPark) == 0) {
        const std::uint32_t file = global_word(m_, 0x0052dd14);
        engine(0x004c5130, {file}, m_.cstring(file, 0x20));
        engine(0x004c8050);
        engine(0x004c8130);
        engine(0x004aa5d0);
    } else {
        engine(0x00432840);
        engine(0x00432860, {1, 0});
        engine(0x00432850);
        engine(0x004c8130);
        engine(0x004aa5d0);
        if (global_word(m_, front::kGameMode) == 7) {
            check(global_word(m_, 0x0056867c) == 0, 0x0052c644);
            check(global_word(m_, 0x0056e208) != 0, 0x0052c624);
            const std::uint32_t name = word_at(m_, 0x0056e1b8 + 4 * global_word(m_, 0x0056867c));
            engine(0x004c4c50, {name}, m_.cstring(name, 0x40));
        } else {
            engine(0x004c4c50, {0x0052ca00}, m_.cstring(0x0052ca00, 0x10));
        }
    }
    if (global_half(m_, 0x0056b8d4) != 0) {
        engine(0x004cddc0);
    }
    const std::uint32_t animation = engine_value(0x004b46a0, {0x0052c9f8, 1}, m_.cstring(0x0052c9f8, 0x10));
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t slot = engine_value(0x004b18a0, {i});
        psx_share_animation_region(word_at(m_, kModelRegions + 4 * i), animation, word_at(m_, kSelectionRecords + slot * kCharacterStride) & 2);
    }
    engine(0x00448f40, {2000});
    engine(0x004a8ee0);
    const std::uint32_t manager = global_word(m_, 0x0056b834);
    engine(0x004a90f0, {manager, 0x0052c9ec, 1}, m_.cstring(0x0052c9ec, 0x20));
    // the three panel bitmaps: the score panel, the special bar and the PAUSED picture
    struct Bitmap {
        std::uint32_t name;
        std::uint32_t variable;
        std::uint32_t flag_offset;
    };
    for (const Bitmap& bitmap : {Bitmap{0x0052c9e0, 0x0056b73c, 5}, Bitmap{0x0052c9d0, 0x0056b710, 5}, Bitmap{0x0052c9c4, 0x0056b70c, 0xa}}) {
        const std::uint32_t memory = operator_new(0x5c);
        const std::uint32_t object = memory != 0 ? engine_value(0x00457420, {memory, bitmap.name, 1, 0x40, 0x40}, m_.cstring(bitmap.name, 0x20)) : 0;
        set_word_at(m_, bitmap.variable, object);
        engine(0x00457120, {object});
        set_byte_at(m_, object + bitmap.flag_offset, 1);
    }
    // a thiscall whose object is the last bitmap made above (ECX is left as it was after the loop)
    engine(0x0048ad50, s_.live() ? std::vector<std::uint32_t>{global_word(m_, 0x0056b70c)} : std::vector<std::uint32_t>{});
    engine(0x004a9330, {manager, 0x0052c9ec}, m_.cstring(0x0052c9ec, 0x20));
    engine(0x004a8f90);
    engine(0x00403680);
    engine(0x00448f40, {4});
    const std::uint32_t waiting = global_word(m_, kWaiting);
    set_global_byte(m_, kLoading, 0);
    if (waiting != 0) {
        return;
    }
    engine(0x004691e0);
    engine(0x00432330);
    engine(0x004c4e30);
    set_global_word(m_, 0x00561c58, global_word(m_, 0x00561c50));
    set_global_word(m_, 0x00561c5c, global_word(m_, 0x00561c54));
    engine(0x00489610);
    if (global_byte(m_, kPark) != 0) {
        engine(0x004302c0, {0, 0, 1, 0});
    }
    engine(0x0045db40);
    if (global_byte(m_, kPark) != 0) {
        engine(0x0042fc70);
    }
    wait_for_button(0);
}

// 0x00452a80: the unlock checks after a level: the next level, the all-gaps character, a character cleared, all golds, the game cleared with every skater;
// true (and a status change) when one of them found something to announce
bool FrontScreens::check_unlocks(std::uint32_t resume) {
    if (global_byte(m_, 0x006a3b21) != 0) {
        return false;
    }
    set_global_word(m_, 0x00561b8c, resume);
    auto highest = [&]() { return static_cast<std::int32_t>(engine_value(0x00415a90, {1, 0})); };
    if (global_int(m_, 0x0055fd78) != highest()) {
        const std::int32_t level = highest();
        if (level < 8 || (highest() > 9 && highest() < 0xd)) {
            set_global_word(m_, 0x0055fd78, u(highest()));
            set_status(0x1406, 0x31);
            return true;
        }
        (void)level;
    }
    if (global_byte(m_, kPark) == 0 && (engine_value(0x00415590) & 0xff) != 0 && (global_word(m_, 0x0056a68c) & 4) == 0 && global_int(m_, front::kCurrentLevel) < 10) {
        set_global_word(m_, 0x0056a68c, global_word(m_, 0x0056a68c) | 4);
        engine(0x00416f80, {0x80});
        set_status(0x1423, 0x35);
        play(0x17c);
        return true;
    }
    bool any_custom_cleared = false;
    bool all_custom_perfect = true;
    bool any_custom_gold = false;
    bool all_cleared = true;
    for (std::uint32_t character = 0; character < 20; ++character) {
        const std::uint32_t config = 0x00568a6c + character * 0x104;
        const bool cleared = (engine_value(0x00416740, {character}) & 0xff) != 0;
        const std::uint32_t flags = word_at(m_, kSelectionRecords + character * kCharacterStride);
        if (cleared) {
            set_global_word(m_, 0x0055fe0c, character);
            if ((flags & 2) == 0) {
                if ((byte_at(m_, config + 1) & 1) == 0) {
                    set_byte_at(m_, config + 1, byte_at(m_, config + 1) | 1);
                    set_status(0x1477, 0x34);
                    return true;
                }
            } else if ((global_word(m_, 0x0056a68c) & 0x20) == 0) {
                set_global_word(m_, 0x0056a68c, global_word(m_, 0x0056a68c) | 0x20);
                set_status(0x145f, 0x34);
                return true;
            }
        }
        if ((flags & 2) == 0) {
            if ((flags & 0x80) == 0) {
                if (byte_at(m_, config + 3) != 3) {
                    all_custom_perfect = false;
                }
                if ((engine_value(0x00416740, {character}) & 0xff) == 0) {
                    all_cleared = false;
                }
            }
        } else {
            if (byte_at(m_, config + 3) == 3) {
                any_custom_cleared = true;
            }
            if ((engine_value(0x00416740, {character}) & 0xff) != 0) {
                any_custom_gold = true;
            }
        }
    }
    if (!any_custom_gold) {
        all_cleared = false;
    }
    if (any_custom_cleared && all_custom_perfect && (global_word(m_, 0x0056a68c) & 8) == 0) {
        set_global_word(m_, 0x0056a68c, global_word(m_, 0x0056a68c) | 8);
        s_.note(kSvcPrintfLog, {0x0052cb00});
        set_status(0x14a9, 0x36);
        return true;
    }
    if (all_cleared && (global_word(m_, 0x0056a68c) & 0x10) == 0) {
        set_global_word(m_, 0x0056a68c, global_word(m_, 0x0056a68c) | 0x10);
        s_.note(kSvcPrintfLog, {0x0052cac4});
        set_status(0x14b4, 0x37);
        return true;
    }
    return false;
}

// 0x00452d00: an unlock screen ends: check for the next announcement, and go on to the status the first check was asked for
void FrontScreens::unlock_continue() {
    if (!check_unlocks(global_word(m_, 0x00561b8c))) {
        set_status(0x14c2, global_word(m_, 0x00561b8c));
    }
}

}  // namespace opentony::runtime::frontend
