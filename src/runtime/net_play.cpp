#include "auto_views.hpp"
#include "net_game.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

using namespace net;

constexpr std::uint32_t kSkaterManager = 0x0056c580;
constexpr std::uint32_t kSendPacket = seam::net::NetGame_SendPacket;           // DirectPlay send: (dp, descriptor, 0, 0x400)
constexpr std::uint32_t kEventSend = seam::net::NetGame_EventSend;            // (2, 5)
constexpr std::uint32_t kLinkLost = seam::net::NetGame_LinkLost;
constexpr std::uint32_t kStatePacket = 0x006a1840;          // the state packet the host and the guest exchange (type 5, 100 bytes)
constexpr std::uint32_t kCustomLevelPointer = 0x005614cc;
constexpr std::uint32_t kMessageBudget = 0x1e;

std::string le32(std::uint32_t value) {
    std::string out;
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }
    return out;
}

}  // namespace

// Game_FrameTick: the engine advances the clock; the replay reads the new value from the script
void NetGame::frame_tick() {
    engine(seam::hal::Game_FrameTick);
    set_global_word(m_, kClock, static_cast<std::uint32_t>(s_.value(engine_name(seam::hal::Game_FrameTick))));
}

// 0x004deb00: the match is about to start: the host moves to step 0xf, the guest to 0xe, and the state goes out
void NetGame::begin_match() {
    log_line(0x0054a864);                                           // "Starting: L%d"
    if (global_byte(m_, kNetFlag) == 0) {
        return;
    }
    log_line(0x0054a84c);                                           // "NETGAME_ReadyToPlay()"
    engine(seam::net::FUN_004dea30);
    set_global_word(m_, kLocalState, is_host() ? 0xf : 0xe);
    send_state();
}

// 0x004deb50: the match is over: give the career globals back, tell the other machine (three rounds of three ticks), drop the custom level, restore the skater and, if the link was lost, close
void NetGame::end_match() {
    log_line(0x0054a880);                                           // "NETGAME_EndLevel()"
    if (game_mode_runs_net() || global_word(m_, 0x00533f38) == 7) {
        restore_career_state();
    }
    if (global_byte(m_, kNetFlag) != 0) {
        const bool lost = global_byte(m_, kLostLink) != 0;
        set_global_word(m_, kLocalState, 0x16);
        set_global_word(m_, kLocalFlags, 0);
        if (!lost) {
            send_state();
            for (int round = 0; round < 3; ++round) {
                const std::uint32_t until = global_word(m_, kClock) + 3;
                if (global_word(m_, kClock) < until) {
                    do {
                        frame_tick();
                        update();
                    } while (global_word(m_, kClock) < until);
                }
                send_state();
            }
        }
        if (is_guest() || global_byte(m_, kLostLink) != 0) {
            set_global_word(m_, 0x006a3d4c, 0);
            set_custom_level(0x64, 0x0055fe10);
        }
        restore_level();
        const std::uint32_t character = global_word(m_, 0x006a3ab4);
        set_global_byte(m_, 0x006a18dc, 0);
        engine(0x004b0e60, {kSkaterManager, 0, character});
        engine(0x004b0fe0, {kSkaterManager, 0, global_word(m_, 0x006a3ab8), 1});
        set_global_byte(m_, 0x0055faf8, global_byte(m_, 0x006a3ab4));
    }
    if (global_byte(m_, kLostLink) != 0) {
        net_close();
    }
    set_global_word(m_, kLocalIndex, 0);
}

// 0x004dece0: may the game stay paused / go on? The local pause bit (0x20000) says both machines are in a state where a pause is allowed; a pause that nobody confirms in 1200 ticks, or one
// that either side forces (0x80000), is accepted
bool NetGame::pause_agreement() {
    bool ok = true;
    if (global_byte(m_, kNetFlag) == 0 || !game_mode_runs_net() || global_byte(m_, kLostLink) != 0) {
        return true;
    }
    if (global_int(m_, 0x005685f4) < 0x78 && global_int(m_, kLocalState) < 0x12) {
        ok = false;
    }
    const std::uint32_t remote = global_word(m_, kRemotePlayer);
    const std::uint32_t local = global_word(m_, kLocalPlayer);
    const std::uint32_t mode = global_word(m_, 0x00533f38);
    if (remote != 0 && local != 0 && mode != 9) {
        const std::uint32_t mine = word_at(m_, local + 0x30b8);
        const std::uint32_t theirs = word_at(m_, remote + 0x30b8);
        if (!((mine == 0 || mine == 7) && (theirs == 0 || theirs == 7) && word_at(m_, local + 0x2f64) == 0)) {
            ok = false;
        }
        if (mode == 5 && (word_at(m_, local + 0x2a8) != 0 || word_at(m_, local + 0x2858) != 0)) {
            ok = false;
        }
    }
    set_local_flag(0x20000, ok ? 1 : 0);
    bool timeout_check = ok ? false : true;
    if (mode != 9 && (global_word(m_, kRemoteFlags) & 0x20000) == 0) {
        ok = false;
        timeout_check = true;
    }
    if (timeout_check && global_int(m_, kLocalState) >= 0x12) {
        const std::uint32_t since = global_word(m_, 0x006a3d3c);
        if (since != 0 && global_word(m_, kClock) - since > 0x4b0) {
            log_line(0x0054a8ac);                                   // "NETGAME_IsGameOverOK FORCED!!"
            set_local_flag(0x20000, 1);
            ok = true;
        }
    }
    if ((global_word(m_, kRemoteFlags) & 0x80000) != 0 || (global_word(m_, kLocalFlags) & 0x80000) != 0) {
        set_local_flag(0x20000, 1);
        ok = true;
        if (global_word(m_, 0x00533f38) == 9) {
            set_global_word(m_, 0x0056db64, 2);
        }
    }
    return ok;
}

// 0x004dee50: the game is over on this side: wait for the other machine to reach its end step (or for a quit / a lost link / 60 ticks with the quit flag), sending the event now and then
void NetGame::disconnected() {
    log_line(0x0054a928);                                           // "Game Over: sm%1.1f ts%d"
    if (global_byte(m_, kNetFlag) == 0) {
        return;
    }
    bool again = true;
    std::uint32_t next_ping = 0;
    set_global_word(m_, kLocalState, 0x14);
    set_global_word(m_, kRemoteState, 0);
    engine(seam::net::FUN_004dec50);
    const std::uint32_t started = global_word(m_, kClock);
    for (;;) {
        set_local_flag(0x2200, 0);
        if (global_word(m_, kClock) > next_ping) {
            next_ping = global_word(m_, kClock) + 0x1e;
            engine(kEventSend, {2, 5});
            log_line(0x0052faf8);
        }
        update();
        frame_tick();
        if (global_int(m_, kRemoteState) >= 0x14 && global_int(m_, kLocalState) >= 0x15) {
            again = false;
        }
        if (abort_requested() != 0 || global_byte(m_, kLostLink) != 0 || global_int(m_, kRemoteState) < 0x12) {
            log_line(0x0054a914);                                   // "QUIT: line %d"
            again = false;
        }
        if (global_word(m_, kClock) - started > 0x3c && (global_word(m_, kLocalFlags) & 0x80000) != 0) {
            log_line(0x0054a914);
            break;
        }
        if (!again) {
            break;
        }
    }
    engine(seam::net::FUN_004dec50);
    if (global_byte(m_, kLostLink) == 0) {
        set_global_byte(m_, kStatusText, 0);
    }
    log_line(0x0054a8f0);                                           // "P1: %d points, %d tags, %d steals"
    log_line(0x0054a8cc);                                           // "P2: ..."
}

// 0x004defc0: the run ended: ask for the end of the pause (flag 0x80000); in the tag game also move the game on
void NetGame::end_run() {
    log_line(0x0054a944);                                           // "NETGAME_EndRun"
    set_local_flag(0x80000, 1);
    if (global_word(m_, 0x00533f38) == 9) {
        set_global_word(m_, 0x0056db64, 2);
    }
}

// 0x004df370: send a line of text (the chat message) to the other machine: a type 0 packet, the text and its NUL
void NetGame::send_string(std::uint32_t text) {
    if (global_byte(m_, kNetFlag) == 0) {
        return;
    }
    if (text != 0x006a3c93) {
        set_string(0x006a3c93, m_.cstring(text, 0x100));
    }
    const std::string line = m_.cstring(text, 0x100);
    // the descriptor (three leftover words, the size and the pointer to the packet that follows it) and the packet {type 0, text, NUL}, as the call sees them
    std::string record(12, '\0');
    record += le32(static_cast<std::uint32_t>(line.size()) + 0x12);
    record += std::string(4, '\0');
    record += std::string(4, '\0');
    record += line;
    record.push_back('\0');
    engine(kSendPacket, {kDirectPlay, kStackLocal, 0, 0x400}, record);
}

// 0x004df6d0: build the state packet (type 5: the clock, the step, the skater and costume, the options, the score, the park name, the lap data) and send it
void NetGame::send_state() {
    if (global_byte(m_, kNetFlag) == 0) {
        return;
    }
    engine(seam::net::FUN_004dec50);
    set_global_word(m_, kStatePacket, 5);
    set_global_word(m_, 0x006a1844, global_word(m_, kClock));
    set_global_byte(m_, 0x006a1848, global_byte(m_, kLocalState));
    const bool host = is_host();
    std::uint32_t slot;
    if (host) {
        set_global_word(m_, kLocalIndex, 0);
        set_global_word(m_, kRemoteIndex, 1);
        slot = 0;
    } else {
        set_global_word(m_, kLocalIndex, 1);
        set_global_word(m_, kRemoteIndex, 0);
        slot = 1;
    }
    set_global_word(m_, 0x006a3ab4, engine_value(0x004b0c60, {kSkaterManager, slot}));
    set_global_word(m_, 0x006a3ab8, engine_value(0x004b0cd0, {kSkaterManager, slot}));
    std::uint32_t special;
    if ((byte_at(m_, 0x0053a514 + 0x170 * global_word(m_, 0x006a3ab4)) & 2) != 0) {
        set_global_word(m_, 0x006a3ab4, 0x10);
        set_global_byte(m_, 0x006a3abd, 1);
        special = 1;
    } else {
        set_global_byte(m_, 0x006a3abd, 0);
        special = 0;
    }
    set_global_byte(m_, 0x006a184f, special);
    const std::uint32_t level = global_byte(m_, 0x00561c90);
    set_global_half(m_, 0x006a1852, global_half(m_, 0x006a3ab4));
    set_global_byte(m_, 0x006a184e, global_byte(m_, 0x006a3abc));
    set_global_half(m_, 0x006a1854, global_half(m_, 0x006a3ab8));
    set_global_byte(m_, 0x006a1850, global_byte(m_, 0x006a3abe));
    set_global_byte(m_, 0x006a1849, level);
    set_string(0x006a1884, m_.cstring(level == 0xd ? 0x0053d7b4 : 0x0055fe10, 0x100));
    set_global_byte(m_, 0x006a184a, global_byte(m_, 0x00561c74));
    set_global_byte(m_, 0x006a184c, global_byte(m_, 0x006a3aac));
    set_global_half(m_, 0x006a185a, global_half(m_, 0x006a3aa8));
    set_global_byte(m_, 0x006a184d, global_byte(m_, 0x0056db64));
    set_global_half(m_, 0x006a1864, global_half(m_, 0x00561bfc));
    if (host) {
        set_global_half(m_, 0x006a185c, global_half(m_, 0x0056b7dc));
        set_global_half(m_, 0x006a185e, global_half(m_, 0x0056b79c));
        set_global_half(m_, 0x006a1860, global_half(m_, 0x0056b7b0));
        set_global_half(m_, 0x006a1862, global_half(m_, 0x0056b7a4));
        set_global_byte(m_, 0x006a184b, global_byte(m_, 0x0056a688));
    } else {
        set_global_half(m_, 0x006a185c, global_half(m_, 0x0056b7e0));
        set_global_half(m_, 0x006a185e, global_half(m_, 0x0056b798));
        set_global_half(m_, 0x006a1860, global_half(m_, 0x0056b7b4));
        set_global_half(m_, 0x006a1862, global_half(m_, 0x0056b7a0));
        set_global_byte(m_, 0x006a184b, global_byte(m_, 0x0056a689));
    }
    const std::uint32_t player = global_word(m_, kLocalPlayer);
    std::int32_t state = global_int(m_, kLocalState);
    set_global_word(m_, 0x006a1870, 0);
    if (player != 0 && state >= 0xf) {
        set_global_word(m_, 0x006a1868, word_at(m_, player + 0x2a8) + word_at(m_, player + 0x16c));
        set_global_half(m_, 0x006a1856, half_at(m_, player + 0x2cdc));
        set_global_half(m_, 0x006a1858, half_at(m_, player + 0x2ce0));
        set_global_word(m_, 0x006a186c, word_at(m_, player + 0x2cc8) - global_word(m_, 0x005685f4));
        if (state == 0x14) {
            set_global_word(m_, 0x006a186c, 0xffffffffu);
        }
        set_global_word(m_, 0x006a1870, global_word(m_, 0x0056a8d0));
        if (global_word(m_, 0x00533f38) == 8) {
            table_take(0x006a16b0, 0x006a1874, 0);
            table_take(0x006a39b0, 0x006a187c, 1);
            state = global_int(m_, kLocalState);
        }
    }
    if (state >= 0x14) {
        set_global_word(m_, 0x006a186c, 0xffffffffu);
    }
    set_global_word(m_, 0x006a3aa0, global_word(m_, 0x006a1868));
    std::string descriptor(12, '\0');
    descriptor += le32(0x64);
    descriptor += le32(kStatePacket);
    engine(kSendPacket, {kDirectPlay, kStackLocal, 0, 0x400}, descriptor);
    set_global_byte(m_, 0x006a3d25, 0);
}

// 0x004dfe50: the options are agreed: step 7, then wait (running the update, the frame tick and the quit keys) until the other side reaches 10, and send the state
void NetGame::wait_for_options() {
    if (global_byte(m_, kNetFlag) == 0) {
        return;
    }
    std::int32_t remembered = global_int(m_, kRemoteState);
    std::uint32_t next_ping = 0;
    bool done = false;
    log_line(0x0054aa20);                                           // "NETGAME_SyncOptions()"
    set_global_word(m_, kLocalState, 7);
    send_state();
    engine(kEventSend, {2, 5});
    set_global_word(m_, 0x006a3d58, 0);
    do {
        if (global_int(m_, kLocalState) >= 0xa) {
            done = true;
        }
        update();
        frame_tick();
        const bool quit = abort_requested() != 0;
        if (quit || global_byte(m_, kLostLink) != 0 || (global_int(m_, kRemoteState) == 5 && remembered >= 7) || global_byte(m_, 0x006a1417) != 0) {
            set_local_state(5);
            set_global_byte(m_, 0x0056aff9, 1);
            set_pending_buttons(0x10);
            log_line(0x0054aa10);                                   // "ESC Options"
            done = true;
        }
        if (global_word(m_, kClock) > next_ping) {
            next_ping = global_word(m_, kClock) + 0x3c;
            engine(kEventSend, {2, 5});
            log_line(0x0052faf8);
        }
        if (global_int(m_, kRemoteState) > 0) {
            remembered = global_int(m_, kRemoteState);
        }
    } while (!done);
    log_line(0x0054a9e4);                                           // "Options set: L%d G%d GenerateMapState=%d"
    log_line(0x0054a9d4);                                           // "Decks: %d,%d"
    log_line(0x0054a9c4);                                           // "Local: %d,%d"
    log_line(0x0054a9b0);                                           // "Remote: %d,%d"
    stash_career_state();
    send_state();
}

// 0x004dffb0: the same for the loading step: 0xb, then 0xc when the other side is at 0xb
void NetGame::wait_for_loading() {
    if (global_byte(m_, kNetFlag) == 0) {
        return;
    }
    std::uint32_t next_ping = 0;
    bool done = false;
    log_line(0x0054aa4c);                                           // "NETGAME_SyncLoading()"
    set_global_word(m_, kLocalState, 0xb);
    set_global_word(m_, kRemoteState, 0);
    send_state();
    engine(kEventSend, {2, 5});
    set_global_word(m_, 0x006a3d58, 0);
    do {
        if (global_int(m_, kRemoteState) >= 0xb) {
            set_global_word(m_, kLocalState, 0xc);
            send_state();
            done = true;
        }
        update();
        frame_tick();
        const bool quit = abort_requested() != 0;
        if (quit || global_byte(m_, kLostLink) != 0 || global_int(m_, kRemoteState) == 5) {
            set_local_state(5);
            set_global_byte(m_, 0x0056aff9, 1);
            set_pending_buttons(0x10);
            log_line(0x0054aa38);                                   // "ESC Sync loading"
            done = true;
        }
        if (global_word(m_, kClock) > next_ping) {
            next_ping = global_word(m_, kClock) + 0x3c;
            engine(kEventSend, {2, 5});
            log_line(0x0052faf8);
        }
    } while (!done);
    send_state();
}

// 0x004e1230: an event message: {kind, value} words go to the event handler
void NetGame::on_event_message(std::uint32_t message) {
    const std::uint32_t body = word_at(m_, message + 0x10);
    engine(seam::net::FUN_004e0120, {half_at(m_, body + 8), half_at(m_, body + 0xa)});
}

// 0x004e1250: the host sent the park: keep the header, fill the editor's image from the snapshot, make it the current park
void NetGame::on_custom_level(std::uint32_t message) {
    log_line(0x0054abd0);                                           // "Receiving Custom Level"
    m_.copy(0x006a1930, word_at(m_, message + 0x10), 0x2028);
    if (global_word(m_, kCustomLevelPointer) == 0) {
        engine(0x004326a0);                                         // EditorMaps_Alloc makes the image and the pointer to it
        if (!s_.live()) {   // (live, it stored the pointer itself)
            set_global_word(m_, kCustomLevelPointer, static_cast<std::uint32_t>(s_.value(engine_name(0x004326a0))));
        }
    }
    m_.copy(word_at(m_, global_word(m_, kCustomLevelPointer)), 0x006a1958, 0x1e00);
    set_custom_level(global_int(m_, 0x006a1934), 0x006a1938);
    set_global_byte(m_, 0x005614c4, 1);
    set_global_word(m_, 0x005614bc, 3);
    log_line(0x0054abb0);                                           // "Custom Level Received %1.2f"
}

// 0x004e1370: a DirectPlay system message: a player joined (3) or left (5), the session was lost (0x31), the player count (0x104)
void NetGame::on_system_message(std::uint32_t dp, std::uint32_t message) {
    (void)dp;
    const std::uint32_t body = word_at(m_, message + 0x10);
    const std::uint32_t type = word_at(m_, body);
    if (type == 0x31) {
        log_line(0x0054ac2c);                                       // "Net Session Lost!"
        engine(kLinkLost);
        return;
    }
    if (type > 0x31) {
        if (type == 0x104) {
            const std::uint32_t count = word_at(m_, body + 0x30);
            set_global_word(m_, 0x006a05e8, count);
            log_line(0x0054ac10);                                   // "Net Session: %d Players"
        }
        return;
    }
    if (type - 3 > 0x1e) {
        return;
    }
    if (type == 3) {
        set_global_word(m_, 0x006a05e8, word_at(m_, body + 0xc));
        const std::uint32_t name = word_at(m_, body + 0x20);
        set_string(0x006a18df, m_.cstring(name != 0 ? name : 0x0054ac78, 0x100));
        engine(seam::net::FUN_004dbcc0);
        log_line(0x0054ac50);                                       // "NetGame: "%s" has joined %d Players"
    } else if (type == 5) {
        log_line(0x0054ac40);                                       // ""%s" has left"
        engine(kLinkLost);
    }
}

}  // namespace opentony::runtime::pcplatform
