#include "auto_views.hpp"
#include "net_game.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

using namespace net;

constexpr std::uint32_t kSettingsSection = 0x0053129c;     // "NETWORK"
constexpr std::uint32_t kNameKey = 0x0054a69c;              // "NAME"
constexpr std::uint32_t kAddressKey = 0x0054a694;           // "ADDRESS"
constexpr std::uint32_t kPercentS = 0x005256dc;             // "%s"
constexpr std::uint32_t kCustomLevelPointer = 0x005614cc;   // a pointer to a pointer to the park image (0x1e00 bytes)
constexpr std::uint32_t kLevelSnapshot = 0x006a1958;        // the park image the host keeps for the guest
constexpr std::uint32_t kSkaterManager = 0x0056c580;

// the sign extension of a byte, as the dword the retail code stores
std::uint32_t sign8(std::uint8_t value) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(value))); }

}  // namespace

// 0x004dd890: forget the previous game and set the network state back to its start; a command line that asked for a network game goes on to open the session (the tail jump)
void NetGame::net_reset(std::uint32_t argument) {
    set_global_word(m_, kLastPoll, global_word(m_, kClock));
    set_global_word(m_, kLocalState, 0);
    set_global_word(m_, kRemoteState, 0);
    set_global_word(m_, 0x006a3a8c, 0);
    set_global_word(m_, 0x006a18ac, 0);
    set_global_word(m_, kLocalIndex, 0);
    set_global_word(m_, 0x006a3d14, 0);
    set_global_byte(m_, 0x006a3d34, 0);
    set_global_byte(m_, 0x006a3d35, 0);
    set_global_word(m_, 0x006a3d54, 0);
    set_global_word(m_, 0x006a3d1c, 1);
    set_global_word(m_, 0x006a3d20, 0x3c);
    set_global_word(m_, 0x006a3b1c, argument);
    set_global_byte(m_, kSessionOpen, 0);
    set_global_byte(m_, 0x006a3c12, 0);
    set_global_byte(m_, 0x006a3c13, 0);
    set_global_byte(m_, 0x006a3c93, 0);
    set_global_byte(m_, 0x006a3de1, 0);
    set_global_byte(m_, kSessionName, 0);
    set_global_byte(m_, 0x006a3de2, 0);
    set_global_word(m_, 0x006a3aac, 0x0a);
    set_global_byte(m_, kLostLink, 0);
    set_global_word(m_, 0x006a1934, 0x64);
    set_global_word(m_, 0x006a3dc8, 0x006a1938);
    set_global_byte(m_, 0x006a1938, 0);
    set_global_byte(m_, 0x006a3abc, 0);
    set_global_byte(m_, 0x006a3abd, 0);
    set_global_byte(m_, 0x006a3abe, 0);
    set_global_byte(m_, 0x006a18dc, 0);
    set_global_byte(m_, 0x006a18dd, 0);
    set_global_byte(m_, 0x006a18de, 0);
    engine(seam::net::FUN_004ddbf0);
    if (global_byte(m_, kNetRequested) != 0) {
        engine(seam::net::NetGame_SessionStart);                                         // the session start (a tail jump)
    }
}

// 0x004ddb70: close DirectPlay, clear its object and every network flag
void NetGame::net_close() {
    engine(seam::net::FUN_004db1a0, {kDirectPlay});
    m_.fill(kDirectPlay, 0, 0x88);
    input_reset();
    set_global_byte(m_, kNetFlag, 0);
    set_global_byte(m_, kNetRequested, 0);
    set_global_word(m_, kLocalState, 0);
    set_global_word(m_, kRemoteState, 0);
    set_global_word(m_, 0x006a3a8c, 0);
    set_global_word(m_, 0x006a18ac, 0);
    set_global_word(m_, kLocalIndex, 0);
    set_global_word(m_, 0x006a3d14, 0);
    set_global_byte(m_, 0x006a3d34, 0);
    set_global_byte(m_, 0x006a3d35, 0);
    set_global_word(m_, 0x006a3d54, 0);
    set_global_byte(m_, kSessionOpen, 0);
}

// 0x004ddc00: read the player name (a host only) and the session name from the options file, then the lobby's default host name
void NetGame::load_settings() {
    log_line(0x0054a6a4);                                           // "NETGAME_Load ..."
    if (!is_guest()) {
        set_string(kScratchDefault, crt::format(m_.cstring(kPercentS), {m_.cstring(kPlayerName, 0x100)}));
        load_setting(kSettingsSection, kNameKey, kScratchDefault, kScratchOut, 0x100);
        set_string(kPlayerName, m_.cstring(kScratchOut, 0x100));
    }
    set_string(kScratchDefault, crt::format(m_.cstring(kPercentS), {m_.cstring(kSessionName, 0x100)}));
    load_setting(kSettingsSection, kAddressKey, kScratchDefault, kScratchOut, 0x100);
    set_string(kSessionName, m_.cstring(kScratchOut, 0x100));
    engine(0x0047fc70);
}

// 0x004ddce0: write the player name and, if there is one, the session name to the options file
void NetGame::save_settings() {
    log_line(0x0054a6b4);                                           // "NETGAME_Save ..."
    set_string(kScratchDefault, crt::format(m_.cstring(kPercentS), {m_.cstring(kPlayerName, 0x100)}));
    save_setting(kSettingsSection, kNameKey, kScratchDefault);
    if (!m_.cstring(kSessionName, 0x100).empty()) {
        set_string(kScratchDefault, crt::format(m_.cstring(kPercentS), {m_.cstring(kSessionName, 0x100)}));
        save_setting(kSettingsSection, kAddressKey, kScratchDefault);
    }
}

// 0x004dddb0: the name of the session record a lookup finds, unless the record has a password that is not the one typed in the lobby
std::uint32_t NetGame::resolve_name(std::uint32_t key) {
    if (global_byte(m_, kSessionOpen) == 0) {
        return 0;
    }
    const std::uint32_t record = engine_value(seam::net::FUN_004dba20, {key});
    if (record == 0) {
        return 0;
    }
    const std::uint32_t password = word_at(m_, record + 0x3c);
    if (password != 0 && !crt::equal_nocase(m_.cstring(kAddressText, 0x100), m_.cstring(password, 0x100))) {
        log_line(0x0054a6c4);                                       // "Password %s != %s"
        return 0;
    }
    return word_at(m_, record + 0x38);
}

// 0x004dde10: join a session: DirectPlay connects to `target`; the session's name becomes the player's
bool NetGame::connect(std::uint32_t target, std::uint32_t buffer) {
    if (global_byte(m_, kSessionOpen) == 0) {
        return false;
    }
    const std::uint32_t name = resolve_name(target);
    set_string(buffer, m_.cstring(global_word(m_, 0x0052eac0), 0x100));
    set_string(0x006a18df, m_.cstring(global_word(m_, 0x0052eabc), 0x100));
    const std::int32_t result = static_cast<std::int32_t>(engine_value(seam::net::FUN_004dbc50, {kDirectPlay, target, buffer}));
    if (result < 0) {
        return false;
    }
    if (name != 0) {
        set_string(kPlayerName, m_.cstring(name, 0x100));
    }
    set_global_byte(m_, kNetFlag, 1);
    set_global_byte(m_, kLostLink, 0);
    return true;
}

// 0x004ddf30: a skater was created: the host's player 0 and the guest's player 1 are this machine's, the other is the remote player
void NetGame::register_player(std::uint32_t skater) {
    log_line(0x0054a6d8);                                           // "NETGAME_SetupPlayer %X %d"
    if (global_byte(m_, kNetFlag) != 0) {
        set_half_at(m_, skater + 0x166, half_at(m_, skater + 0xf6));
        set_half_at(m_, skater + 0x164, half_at(m_, skater + 0xf4));
        const std::uint32_t index = word_at(m_, skater + 0x2cc4);
        std::uint32_t local = skater;
        const bool host = global_word(m_, kIsHost) != 0;
        if ((host && index == 0) || (!host && index != 0)) {
            set_global_word(m_, kLocalPlayer, skater);
        } else {
            set_global_word(m_, kRemotePlayer, skater);
            local = global_word(m_, kLocalPlayer);
        }
        if (local != 0) {
            set_global_word(m_, kLocalIndex, word_at(m_, local + 0x2cc4));
            set_global_word(m_, 0x006a3d14, word_at(m_, local + 0x2cc4));
        }
        if (global_word(m_, kRemotePlayer) != 0) {
            set_global_word(m_, kRemoteIndex, word_at(m_, global_word(m_, kRemotePlayer) + 0x2cc4));
        }
    }
    const std::uint32_t source = global_byte(m_, 0x006a3d18) != 0 ? global_word(m_, kRemoteIndex) : global_word(m_, 0x006a3d14);
    set_global_word(m_, 0x006a3d14, source & 1);
}

// 0x004de010: is this skater the remote player's?
bool NetGame::is_remote_player(std::uint32_t skater) {
    return global_byte(m_, kNetFlag) != 0 && word_at(m_, skater + 0x2cc4) == global_word(m_, kRemoteIndex);
}

// 0x004de0a0: the pause handshake. With `request` set it says whether the other machine accepted; with it clear it says whether the other machine asked
std::uint32_t NetGame::pause_state(std::uint32_t request, std::uint32_t on) {
    if (global_byte(m_, kNetFlag) == 0) {
        return 0;
    }
    std::uint32_t remote = global_word(m_, kRemoteFlags);
    if (global_word(m_, 0x00561c04) == 0 && (remote & 0x200) == 0) {
        set_local_flag(0x2000, 0);
        remote = global_word(m_, kRemoteFlags);
    }
    std::uint32_t result = 0;
    if ((request & 0xff) != 0) {
        if (on != 0) {
            set_global_word(m_, kLocalFlags, global_word(m_, kLocalFlags) | 0x2000);
            return 0;
        }
        if ((remote & 0x200) == 0 || (remote & 0x2000) == 0) {
            return 0;
        }
        return global_int(m_, 0x006a18b4) == 1 ? (request & 0xff) : 0;
    }
    if ((remote & 0x200) != 0) {
        result = 0;
    } else if ((remote & 0x2000) != 0) {
        result = 1;
    }
    if (global_word(m_, 0x00561c04) == 0) {
        return 0;
    }
    if (global_word(m_, kRemoteState) == 0x14 && global_word(m_, 0x00561bfc) != 0xd) {
        result = 1;
    }
    return result;
}

// 0x004de170: poll the keyboard and say whether Escape is down
std::uint32_t NetGame::abort_requested() {
    engine(seam::hal::PCInput_Poll);
    return engine_value(0x004e41b0, {1}) & 0xff;
}

// 0x004de180: the park the match is played on: kind 0x64 is the stock level; any other copies the park's name and, on the guest, saves the received image
void NetGame::set_custom_level(std::int32_t kind, std::uint32_t name) {
    log_line(0x0054a6f8);                                           // "NETGAME_SetCustomLevel( %d, %s)"
    set_global_byte(m_, 0x006a3abc, 0);
    set_global_word(m_, 0x006a1934, static_cast<std::uint32_t>(kind));
    if (kind == 0x64) {
        set_global_byte(m_, 0x005614c4, 0);
        if (global_word(m_, 0x005614bc) != 1) {
            set_global_word(m_, 0x005614bc, 0);
        }
        engine(0x00432980, {0, 1});
        return;
    }
    if (name == 0) {
        return;
    }
    set_string(global_word(m_, 0x006a3dc8), m_.cstring(name, 0x100));
    set_global_byte(m_, 0x006a3abc, 1);
    set_global_byte(m_, 0x005614c4, 1);
    if (is_guest()) {
        engine(0x004f62c0, {name, 1, word_at(m_, global_word(m_, kCustomLevelPointer)), 0x1e00});
    }
    if (kind != 0) {
        engine(0x00432980, {name, 1});
    }
    if (is_host()) {
        m_.copy(kLevelSnapshot, word_at(m_, global_word(m_, kCustomLevelPointer)), 0x1e00);
    }
}

// 0x004de270: the host puts the park it kept back as the current one
void NetGame::restore_level() {
    if (is_host() && global_byte(m_, 0x006a3abc) != 0) {
        m_.copy(word_at(m_, global_word(m_, kCustomLevelPointer)), kLevelSnapshot, 0x1e00);
        set_global_byte(m_, 0x005614c4, 1);
        set_global_word(m_, 0x005614bc, 3);
    }
}

// 0x004de2b0: the guest loads the park it was sent (unless the stock level is played)
std::uint32_t NetGame::load_custom_level() {
    if (global_byte(m_, kNetFlag) == 0) {
        return 1;
    }
    const std::uint32_t kind = global_word(m_, 0x006a1934);
    if (kind == 0x64) {
        return 1;
    }
    log_line(0x0054a730);                                           // "NETGAME_LoadCustomLevel: %d, %s"
    return engine_value(0x004f6140, {0x006a1938, kind, 1, word_at(m_, global_word(m_, kCustomLevelPointer))});
}

// 0x004de300: a guest answers with the value the host sent; a host (or a single-player game) with the argument
std::uint32_t NetGame::tint_or(std::uint32_t fallback) {
    if (global_byte(m_, kNetFlag) != 0 && global_word(m_, kIsHost) == 0) {
        return global_word(m_, 0x006a3d4c);
    }
    return fallback;
}

namespace {

// the ten globals the match borrows (saved to the copy, then cleared) and where the copy lives: {global, copy}
constexpr std::uint32_t kStash[10][2] = {
    {0x0056b760, 0x006a3da4}, {0x0056db8c, 0x006a3da8}, {0x0056a890, 0x006a3db4}, {0x0056a868, 0x006a3db0}, {0x0056b7f4, 0x006a3da0},
    {0x0056a8f8, 0x006a3d9c}, {0x0056a948, 0x006a3dbc}, {0x00533fa0, 0x006a3d98}, {0x0056a904, 0x006a3db8}, {0x0056b7ec, 0x006a3dc0},
};

}  // namespace

// 0x004de320: before a match, remember the current skater's trick-slot byte and the career globals the match overwrites, and clear them
void NetGame::stash_career_state() {
    if (global_byte(m_, 0x006a3d60) != 0) {
        return;
    }
    const std::uint32_t slot_number = global_word(m_, 0x00561c90);
    const std::uint32_t slot = engine_value(0x004b0c60, {kSkaterManager, 0});
    const std::uint32_t config = engine_value(0x00416050, {slot});
    const std::uint32_t cell = config + slot_number + 0x26;
    set_global_word(m_, 0x006a3d5c, sign8(byte_at(m_, cell)));
    set_byte_at(m_, cell, 0);
    log_line(0x0054a754);                                           // "NETGAME_SaveCareerData: Level %d FlagMask=%02Xh"
    for (const auto& [global, copy] : kStash) {
        set_word_at(m_, copy, word_at(m_, global));
    }
    for (const auto& [global, copy] : kStash) {
        (void)copy;
        set_word_at(m_, global, 0);
    }
    set_global_byte(m_, 0x006a3d60, 1);
}

// 0x004de420: after the match put them back (the last of the ten, 0x0056b7ec, is not restored)
void NetGame::restore_career_state() {
    const std::uint32_t slot_number = global_word(m_, 0x00561c90);
    const std::uint32_t slot = engine_value(0x004b0c60, {kSkaterManager, 0});
    const std::uint32_t config = engine_value(0x00416050, {slot});
    set_byte_at(m_, config + slot_number + 0x26, global_byte(m_, 0x006a3d5c));
    log_line(0x0054a788);                                           // "NETGAME_RestoreCareerData: Level %d FlagMask=%02Xh"
    for (std::size_t i = 0; i < 9; ++i) {
        set_word_at(m_, kStash[i][0], word_at(m_, kStash[i][1]));
    }
    set_global_byte(m_, 0x006a3d60, 0);
}

// 0x004de4d0 / 0x004de500: set or clear flag bits in this machine's / the other machine's word
void NetGame::set_local_flag(std::uint32_t mask, std::uint32_t on) {
    set_global_word(m_, kLocalFlags, (on & 0xff) != 0 ? (global_word(m_, kLocalFlags) | mask) : (global_word(m_, kLocalFlags) & ~mask));
}

void NetGame::set_remote_flag(std::uint32_t mask, std::uint32_t on) {
    set_global_word(m_, kRemoteFlags, (on & 0xff) != 0 ? (global_word(m_, kRemoteFlags) | mask) : (global_word(m_, kRemoteFlags) & ~mask));
}

// 0x004de5c0: the local player was tagged: tell the other machine (flag 0x100)
void NetGame::tag_acknowledged(std::uint32_t player) {
    if (global_byte(m_, kNetFlag) != 0 && player == global_word(m_, kLocalPlayer)) {
        set_local_flag(0x100, 1);
    }
}

// 0x004de5f0: nine sparks fly from the remote skater when the other machine's player was hit
void NetGame::spawn_hit_sparks() {
    if (global_int(m_, 0x0055f72c) >= 0xfa) {
        return;
    }
    const std::uint32_t remote = global_word(m_, kRemotePlayer);
    if (remote == 0) {
        return;
    }
    engine(0x0049c8d0, {remote});
    engine(0x00406070, {remote + 0x14});
    engine(0x00406090, {kStackLocal}, std::string("\xc2\x01\x84\x03\x00\x00", 6));      // {0x1c2, 0x384, 0} as three words
    set_global_word(m_, 0x005244cc, 0);
    const std::string offset("\x00\x00\x00\x00\x00\x90\x01\x00\x00\x00\x00\x00", 12);   // {0, 0x19000, 0}
    for (int i = 0; i < 9; ++i) {
        const std::uint32_t block = engine_value(0x00403d30, {0x6c});
        if (block != 0) {
            engine(0x0040b060, {block, remote, kStackLocal, 0xffffffd8u, 0x1000, 5}, offset);
        }
    }
    set_global_word(m_, 0x005244cc, 1);
}

// 0x004de7c0: a tag happened (only in the tag game, mode 9): remember who was hit and who hit
void NetGame::player_tagged(std::uint32_t tagger, std::uint32_t tagged, std::uint32_t had_flag) {
    (void)tagged;
    if (global_word(m_, 0x00533f38) == 9) {
        if ((had_flag & 0xff) != 0) {
            set_local_flag(0x1000, 1);
        }
        set_local_flag(0x800, tagger == global_word(m_, 0x0056a858) ? 1 : 0);
    }
    log_line(0x0054a7c0);                                           // "TAG: %d Tagged %d"
}

// 0x004de9c0: clear the flag words and the received-input state
void NetGame::input_reset() {
    set_global_word(m_, 0x006a3aa8, 0);
    set_global_word(m_, 0x006a18c8, 0);
    set_global_word(m_, kLocalFlags, 0);
    set_global_word(m_, kRemoteFlags, 0);
    set_global_word(m_, 0x006a3ab0, 0);
    set_global_word(m_, 0x006a18d0, 0);
    set_global_byte(m_, 0x006a3abd, 0);
    set_global_byte(m_, 0x006a3abe, 0);
    if (is_guest() || global_byte(m_, kNetFlag) == 0) {
        set_global_byte(m_, 0x006a3abc, 0);
    }
    set_global_byte(m_, 0x006a18dc, 0);
    set_global_byte(m_, 0x006a18dd, 0);
    set_global_byte(m_, 0x006a18de, 0);
    set_global_byte(m_, 0x006a3d26, 0);
    set_global_word(m_, 0x00561c04, 0);
    set_global_word(m_, 0x0056a8e0, 0);
}

// 0x004df030: the waiting / connection lost banner, drawn over the game: the status text in a big font (red when the link is lost)
void NetGame::status_text_draw() {
    if (!game_mode_runs_net()) {
        return;
    }
    const bool lost = global_byte(m_, kLostLink) != 0;
    if (!((global_byte(m_, kNetFlag) != 0 && global_byte(m_, kStatusText) != 0) || lost)) {
        return;
    }
    const std::uint32_t scale = engine_value(0x00473500);
    engine(0x004734f0, {0xc8});
    if (lost) {
        set_string(kStatusText, m_.cstring(global_word(m_, 0x0052eae8), 0x100));
        engine(0x00473580, {0xff, 0x37, 0x37, 0});
    }
    engine(0x004739e0, {0x100, 0xce, kStatusText, 0});
    engine(0x004734f0, {scale});
}

// 0x004df1d0: the next filled slot of a 16-slot ring from its head (cleared when `consume` is set); the head moves past it
std::uint32_t NetGame::ring_pop(std::uint32_t ring, std::uint32_t consume) {
    std::uint32_t head = word_at(m_, ring);
    std::uint32_t found = 0;
    std::uint32_t round = 1;
    for (;;) {
        if (found != 0) {
            break;
        }
        found = word_at(m_, ring + 4 * head + 8);
        if ((consume & 0xff) != 0 && found != 0) {
            set_word_at(m_, ring + 4 * head + 8, 0);
        }
        ++head;
        if (static_cast<std::int32_t>(head) >= 0x10) {
            head = 0;
        }
        const std::uint32_t previous = round++;
        if (static_cast<std::int32_t>(previous) >= 0x10) {
            break;
        }
    }
    set_word_at(m_, ring, head);
    return found;
}

// 0x004df220: empty the slot that holds `value`
void NetGame::ring_remove(std::uint32_t ring, std::uint32_t value) {
    for (std::uint32_t k = 0; k < 0x10; ++k) {
        if (word_at(m_, ring + 8 + 4 * k) == value) {
            set_word_at(m_, ring + 4 * k + 8, 0);
            return;
        }
    }
}

// 0x004df250: queue a value on the shared ring (a service) while a game is running and the ring is not frozen
void NetGame::ring_push(std::uint32_t value) {
    if (global_byte(m_, kNetFlag) != 0 && global_byte(m_, 0x006a3d19) == 0) {
        engine(seam::net::FUN_004df170, {value, 0x006a1560});
    }
}

// 0x004df280: empty a ring / table object (head, count and the sixteen entries)
void NetGame::ring_clear(std::uint32_t ring) {
    set_word_at(m_, ring + 8, 0);
    set_word_at(m_, ring, 0);
    set_word_at(m_, ring + 4, 0);
    for (std::uint32_t k = 0; k < 0x10; ++k) {
        set_word_at(m_, ring + 0xc + 8 * k, 0);
        set_word_at(m_, ring + 0x10 + 8 * k, 0);
    }
}

// 0x004df300: add {key, value} to a table of sixteen pairs (at +0xc) unless it is there; a full table logs the assertion
void NetGame::table_add(std::uint32_t table, std::uint32_t key, std::uint32_t value) {
    std::uint32_t slot = 0;
    for (; slot < 0x10; ++slot) {
        const std::uint32_t existing = word_at(m_, table + 8 * slot + 0xc);
        if (existing == key && word_at(m_, table + 8 * slot + 0x10) == value) {
            return;
        }
        if (existing == 0) {
            break;
        }
    }
    if (slot >= 0x10) {
        log_line(0x0054a2e8);                                       // the assertion text (file and line)
        return;
    }
    set_word_at(m_, table + slot * 8 + 0xc, key);
    set_word_at(m_, table + slot * 8 + 0x10, value);
    set_word_at(m_, table + 8, word_at(m_, table + 8) + 1);
}

// 0x004dfa00: take the next entry of a table of pairs, starting at its head: {key, value} go to `out`; with `consume` the pair is removed. Returns the count left (0 and the head and
// count reset when the table is empty)
std::uint32_t NetGame::table_take(std::uint32_t table, std::uint32_t out, std::uint32_t consume) {
    set_word_at(m_, out, 0);
    set_word_at(m_, out + 4, 0);
    std::int32_t index = int_at(m_, table);
    std::uint32_t key = 0;
    for (int round = 1; round <= 16; ++round) {
        if (key != 0) {
            break;
        }
        std::uint32_t masked = static_cast<std::uint32_t>(index) & 0x8000000fu;
        if (static_cast<std::int32_t>(masked) < 0) {
            masked = ((masked - 1) | 0xfffffff0u) + 1;
        }
        index = static_cast<std::int32_t>(masked);
        key = word_at(m_, table + static_cast<std::uint32_t>(index) * 8 + 0xc);
        if (key == 0) {
            ++index;
        }
    }
    if (key == 0) {
        set_word_at(m_, table + 8, 0);
        set_word_at(m_, table, 0);
        return 0;
    }
    const std::uint32_t at = table + static_cast<std::uint32_t>(index) * 8;
    set_word_at(m_, out, word_at(m_, at + 0xc));
    set_word_at(m_, out + 4, word_at(m_, at + 0x10));
    if ((consume & 0xff) != 0) {
        set_word_at(m_, at + 0xc, 0);
        set_word_at(m_, at + 0x10, 0);
        set_word_at(m_, table + 8, word_at(m_, table + 8) - 1);
    }
    set_word_at(m_, table, static_cast<std::uint32_t>(index));
    return word_at(m_, table + 8);
}

// 0x004dfda0: remove the pair {key, value} from a table
bool NetGame::table_remove(std::uint32_t table, std::uint32_t key, std::uint32_t value) {
    for (std::uint32_t slot = 0; slot < 0x10; ++slot) {
        if (word_at(m_, table + 8 * slot + 0xc) == key && word_at(m_, table + 8 * slot + 0x10) == value) {
            set_word_at(m_, table + slot * 8 + 0xc, 0);
            set_word_at(m_, table + slot * 8 + 0x10, 0);
            set_word_at(m_, table + 8, word_at(m_, table + 8) - 1);
            return true;
        }
    }
    return false;
}

// 0x004df2c0: a point was scored by `player`: if it is the local player, count it and remember {packet key, value} in the points table
void NetGame::on_point(std::uint32_t a0, std::uint32_t player, std::uint32_t value, std::uint32_t packet) {
    (void)a0;
    if (player == global_word(m_, kLocalPlayer)) {
        set_global_word(m_, 0x006a3aa8, global_word(m_, 0x006a3aa8) + 1);
        set_global_byte(m_, 0x006a3d25, 1);
        table_add(0x006a16b0, word_at(m_, packet + 4), value);
    }
}

}  // namespace opentony::runtime::pcplatform
