#include "auto_views.hpp"
#include "net_game.hpp"
#include "seam_addresses_generated.hpp"

namespace opentony::runtime::pcplatform {

using namespace opentony::runtime::autoviews;

namespace {

using namespace net;

constexpr std::uint32_t kPollMessages = seam::net::NetGame_PollMessages;         // DirectPlay: how many messages wait
constexpr std::uint32_t kLatency = seam::net::NetGame_Latency;              // DirectPlay: the round trip in ticks
constexpr std::uint32_t kNextMessage = seam::net::NetGame_NextMessage;
constexpr std::uint32_t kHandleMessage = seam::net::NetGame_HandleMessage;
constexpr std::uint32_t kEventSend = seam::net::NetGame_EventSend;
constexpr std::uint32_t kLinkLost = seam::net::NetGame_LinkLost;
constexpr std::uint32_t kSendLevel = seam::net::NetGame_SendLevel;
constexpr std::uint32_t kSendSkater = seam::net::NetGame_SendSkater;
constexpr std::uint32_t kPadClearOne = 0x004895c0;
constexpr std::uint32_t kAudioPause = seam::hal::Audio_PauseLooping;
constexpr std::uint32_t kAudioResume = seam::hal::Audio_ResumeLooping;
constexpr std::uint32_t kClockTarget = 0x006a3d20;          // the ticks between two state packets
constexpr std::uint32_t kClockRetry = 0x006a3d1c;
constexpr std::uint32_t kStatusLine = kStatusText;

// sprintf(status line, text): the texts have no conversions
void print_status(PcKit&, RetailMemory& m, std::uint32_t format) {
    std::string text = m.cstring(format, 0x100);
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    bytes.push_back(0);
    m.write(kStatusLine, bytes);
}

}  // namespace

// 0x004e02d0: the per-frame network update. It follows the other machine's step of the handshake (the switch on this machine's step sets how often to send and what to do), sends the
// state when it is due, drains the incoming messages (up to 0x1f, or 0xc9 after the loading steps), keeps the remote skater's score and flags in step, and decides when a silent
// or quit game is over
void NetGame::update() {
    if (global_byte(m_, kNetFlag) == 0) {
        goto tail;
    }
    {
        bool changed = false;
        std::int32_t budget = 0x1e;
        const std::uint32_t start = global_word(m_, kClock);
        set_global_word(m_, 0x006a3df0, global_word(m_, 0x006a3df0) + 1);
        set_global_byte(m_, 0x006a3d24, 0);
        std::int32_t remote_state;
        if (global_word(m_, kLocalState) == global_word(m_, kLocalPrev)) {
            remote_state = global_int(m_, kRemoteState);
        } else {
            changed = true;
            set_global_word(m_, 0x006a3b0c, global_word(m_, kClock));
            send_state();
            remote_state = global_int(m_, kRemoteState);
        }
        if (changed || global_int(m_, kRemoteState) != global_int(m_, kRemotePrev)) {
            log_line(0x0054ab6c);                                   // "Net State change: L:%d -> %d R:%d -> %d T:%1.2f"
            remote_state = global_int(m_, kRemoteState);
        }
        if (global_int(m_, kRemotePrev) == 7 && remote_state == 5) {
            set_global_byte(m_, 0x006a1417, 1);
        }
        set_global_word(m_, kRemotePrev, static_cast<std::uint32_t>(remote_state));
        set_global_word(m_, kLocalPrev, global_word(m_, kLocalState));
        set_local_flag(0x200, global_word(m_, 0x00561c04) != 0 ? 1 : 0);
        const std::int32_t latency = static_cast<std::int32_t>(engine_value(kLatency, {kDirectPlay}));
        (void)engine_value(kPollMessages, {kDirectPlay});
        const std::uint32_t local_skater = global_word(m_, kLocalPlayer);
        if (local_skater != 0) {
            set_global_word(m_, 0x006a3b00, word_at(m_, local_skater + 0x2cc8) - global_word(m_, 0x005685f4));
        }
        const std::uint32_t state = global_word(m_, kLocalState) - 1;
        // ---- what to do in this step: the sending interval (kClockTarget) and retry (kClockRetry) and the step's own work ----
        auto interval = [&](std::uint32_t target, std::uint32_t retry) {
            set_global_word(m_, kClockTarget, target);
            set_global_word(m_, kClockRetry, retry);
        };
        if (state > 0x14) {
            interval(0x3c, 0x3c);
        } else {
            switch (state + 1) {
            case 2:
                interval(0x3c, 0x3c);
                if (global_int(m_, kRemoteState) >= 2) {
                    const std::uint32_t mine = global_word(m_, 0x006a3aac);
                    const std::uint32_t theirs = global_word(m_, 0x006a18cc);
                    set_global_word(m_, kLocalState, 3);
                    if (mine != theirs) {
                        log_line(0x0054ab44);                       // "Network versions don't match! %d %d"
                    }
                }
                break;
            case 5:
            case 6:
                interval(0x3c, 0xc8);
                break;
            case 7:
                set_global_word(m_, kClockTarget, 0x14);
                set_global_word(m_, kClockRetry, 0x3c);
                if (global_int(m_, kRemoteState) >= 7) {
                    set_global_word(m_, kLocalState, 8);
                }
                break;
            case 8:
            case 13:
                budget = 0xc8;
                set_global_word(m_, kClockTarget, 0x1e);
                set_global_word(m_, kClockRetry, 0xc8);
                set_global_word(m_, kLocalState, 9);
                break;
            case 9:
                if (changed) {
                    set_global_word(m_, kClockTarget, 0x3c);
                    set_global_word(m_, kClockRetry, 0xc8);
                    set_global_word(m_, 0x006a395c, global_word(m_, kClock));
                    set_global_word(m_, 0x006a1438, global_word(m_, kClock) + 6);
                }
                if (global_byte(m_, 0x006a3abc) == global_byte(m_, 0x006a18dc) && global_byte(m_, 0x006a3abd) == global_byte(m_, 0x006a18de) && global_byte(m_, 0x006a3abe) == global_byte(m_, 0x006a18dd)) {
                    set_global_word(m_, kLocalState, 0xa);
                    break;
                }
                if (global_word(m_, kClock) > global_word(m_, 0x006a395c) && is_host() && global_byte(m_, 0x006a3abc) != 0 && global_byte(m_, 0x006a18dc) == 0) {
                    set_global_word(m_, 0x006a395c, global_word(m_, kClock) + 0xf0);
                    engine(kSendLevel);
                }
                if (global_word(m_, kClock) > global_word(m_, 0x006a1438) && global_byte(m_, 0x006a3abd) != global_byte(m_, 0x006a18de)) {
                    set_global_word(m_, 0x006a1438, global_word(m_, kClock) + 0xf0);
                    engine(kSendSkater);
                }
                break;
            case 14:
                if (changed) {
                    engine(kAudioPause);
                }
                set_global_word(m_, kClockTarget, 0x1e);
                set_global_word(m_, kClockRetry, 0xc8);
                set_global_word(m_, 0x0056a8e0, 1);
                set_global_byte(m_, 0x006a3d26, 1);
                set_global_byte(m_, kStatusText, 0);
                if (!is_host()) {
                    set_global_word(m_, kLocalState, 0xf);
                }
                break;
            case 15:
                if (changed) {
                    engine(kAudioPause);
                }
                set_global_word(m_, kClockTarget, 0x1e);
                print_status(*this, m_, global_word(m_, 0x0052eac4));
                set_global_word(m_, kClockRetry, 0x3c);
                set_global_word(m_, 0x0056a8e0, 1);
                set_global_byte(m_, 0x006a3d26, 1);
                if (word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8) == 0) {
                    set_word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8, 0x1e);
                }
                if (global_word(m_, 0x00561bfc) == 0x33) {
                    set_global_word(m_, kLocalState, 0xe);
                    break;
                }
                if (global_int(m_, kRemoteState) >= 0xf) {
                    send_state();
                    set_global_word(m_, kLocalState, 0x10);
                }
                break;
            case 16:
                set_global_word(m_, kClockTarget, 0x14);
                print_status(*this, m_, global_word(m_, 0x0052eb04));
                set_global_word(m_, kClockRetry, 0x3c);
                set_global_word(m_, 0x0056a8e0, 1);
                set_global_byte(m_, 0x006a3d26, 1);
                if (global_word(m_, 0x00533f38) == 9) {
                    set_global_word(m_, 0x0056db64, 0);
                }
                if (global_word(m_, 0x00561bfc) == 0x33) {
                    set_global_word(m_, kLocalState, 0xe);
                    break;
                }
                if (global_int(m_, kRemoteState) == 0xe) {
                    set_global_word(m_, kLocalState, 0xf);
                }
                if (is_guest()) {
                    set_word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8, word_at(m_, global_word(m_, kRemotePlayer) + 0x2cc8));
                }
                if (word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8) == 0) {
                    set_word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8, 0x1e);
                }
                if (global_int(m_, kRemoteState) >= 0x10) {
                    const std::int32_t tick = global_int(m_, 0x005685f4);
                    if (int_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8) > tick && int_at(m_, global_word(m_, kRemotePlayer) + 0x2cc8) > tick) {
                        send_state();
                        set_global_word(m_, kLocalState, 0x11);
                    }
                }
                break;
            case 17:
                set_global_word(m_, kClockTarget, 0x14);
                print_status(*this, m_, global_word(m_, 0x0052eb04));
                set_global_word(m_, kClockRetry, 0x3c);
                set_global_word(m_, 0x0056a8e0, 1);
                set_global_byte(m_, 0x006a3d26, 1);
                if (global_word(m_, 0x00561bfc) == 0x33) {
                    set_global_word(m_, kLocalState, 0xe);
                    break;
                }
                if (global_int(m_, kRemoteState) < 0x10) {
                    set_global_word(m_, kLocalState, 0xf);
                }
                if (global_int(m_, kRemoteState) >= 0x11) {
                    set_global_word(m_, 0x0056a8e0, 0);
                    set_global_byte(m_, 0x006a3d26, 0);
                    send_state();
                    set_global_word(m_, kLocalState, 0x12);
                    set_global_byte(m_, kStatusText, 0);
                }
                break;
            case 18:
                if (changed) {
                    engine(kAudioResume);
                    set_global_word(m_, kClockTarget, 0x3c);
                    set_global_word(m_, kClockRetry, 4);
                }
                if (latency > 0x28 || (global_word(m_, kRemoteFlags) & 0x8000) != 0) {
                    set_global_word(m_, kClockRetry, global_word(m_, kClockRetry) + 1);
                } else if (latency < 1 || (global_word(m_, kRemoteFlags) & 0x10000) != 0) {
                    set_global_word(m_, kClockRetry, global_word(m_, kClockRetry) - 1);
                }
                if (global_int(m_, kRemoteState) == 0x14 && global_word(m_, 0x00533f38) != 9) {
                    set_word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8, global_word(m_, 0x005685f4) - 1);
                }
                if (global_word(m_, 0x006a3df0) % 3 == 0 && global_byte(m_, 0x006a3d25) != 0) {
                    send_state();
                }
                break;
            case 20:
                set_global_word(m_, kClockTarget, 0x14);
                set_global_word(m_, kClockRetry, 0x3c);
                if (global_int(m_, kRemoteState) >= 0x14) {
                    set_global_word(m_, kLocalState, 0x15);
                    send_state();
                }
                break;
            case 21:
                set_global_word(m_, kClockTarget, 0x14);
                set_global_word(m_, kClockRetry, 0x3c);
                break;
            case 3:
            case 10:
                break;
            default:
                interval(0x3c, 0x3c);
                break;
            }
        }
        // ---- the clock: when the state is due, and the retry packet ----
        if (global_word(m_, kClock) < global_word(m_, 0x006a3dfc)) {
            set_global_word(m_, kLastPoll, global_word(m_, kClock));
            set_global_word(m_, 0x006a3df8, 0);
            set_global_word(m_, 0x006a3df4, 0);
        }
        set_global_word(m_, 0x006a3dfc, global_word(m_, kClock));
        if (global_word(m_, kClock) >= global_word(m_, 0x006a3df8)) {
            send_state();
            set_global_word(m_, 0x006a3df8, global_word(m_, kClock) + global_word(m_, kClockTarget));
        } else if (global_word(m_, kClock) >= global_word(m_, 0x006a3df4)) {
            engine(seam::net::FUN_004df420);
            std::int32_t retry = global_int(m_, kClockRetry);
            if (retry < 1) {
                retry = 1;
                set_global_word(m_, kClockRetry, 1);
            } else if (retry > 0x1e) {
                retry = 0x1e;
                set_global_word(m_, kClockRetry, 0x1e);
            }
            set_global_word(m_, 0x006a3df4, global_word(m_, kClock) + static_cast<std::uint32_t>(retry));
        }
        // ---- a silent link in the playing steps ----
        const std::int32_t step = global_int(m_, kLocalState);
        if (step >= 0xd && step <= 0x14 && global_word(m_, kClock) - global_word(m_, kLastPoll) > 0x258 && global_word(m_, 0x006a3df0) % 20 == 0) {
            log_line(0x0054ab2c);                                   // "No Messages for %d VBs"
            engine(kEventSend, {2, 5});
            if (global_word(m_, kClock) - global_word(m_, kLastPoll) > 0x708 && global_byte(m_, kLostLink) == 0) {
                log_line(0x0054ab14);                               // "Cutting Connection!"
                engine(kLinkLost);
            }
        }
        // ---- the incoming messages ----
        std::int32_t left = budget;
        if (static_cast<std::int32_t>(engine_value(kPollMessages, {kDirectPlay})) > 0) {
            for (;;) {
                const std::uint32_t message = engine_value(kNextMessage, {kDirectPlay});
                engine(kHandleMessage, {kDirectPlay, message});
                // (what the handler does that the update reads back: a state message sets the "remote changed" byte, and handling takes time)
                // (live, the handler stored them itself)
                if (!s_.live()) {
                    set_global_byte(m_, 0x006a3d24, static_cast<std::uint32_t>(s_.value(engine_name(kHandleMessage))));
                    set_global_word(m_, kClock, static_cast<std::uint32_t>(s_.value(engine_name(kHandleMessage))));
                    set_global_word(m_, kRemoteState, static_cast<std::uint32_t>(s_.value(engine_name(kHandleMessage))));          // (a state message carries the other machine's step)
                }
                const std::int32_t before = left--;
                if (before < 0) {
                    break;
                }
                if (static_cast<std::int32_t>(engine_value(kPollMessages, {kDirectPlay})) <= 0) {
                    break;
                }
            }
        }
        if (global_int(m_, kLocalState) >= 0x12) {
            set_global_word(m_, 0x00568618, 1);
        }
        // ---- the remote skater: score and position in step ----
        const std::uint32_t remote = global_word(m_, kRemotePlayer);
        if (remote != 0) {
            set_global_word(m_, 0x006a1920, word_at(m_, remote + 0x2cc8) - global_word(m_, 0x005685f4));
            if (global_byte(m_, 0x006a3d24) != 0) {
                set_global_word(m_, 0x006a1924, half_at(m_, remote + 0xf6));
                set_global_word(m_, 0x006a1928, static_cast<std::uint32_t>(short_at(m_, remote + 0xf4)));
            } else {
                set_half_at(m_, remote + 0xf6, global_half(m_, 0x006a1924));
                set_half_at(m_, global_word(m_, kRemotePlayer) + 0xf4, global_half(m_, 0x006a1928));
                set_remote_flag(0x400, 1);
            }
        }
        const std::int32_t remote_clock = global_int(m_, 0x006a1920);
        const std::uint32_t local = global_word(m_, kLocalPlayer);
        const std::int32_t step_now = global_int(m_, kLocalState);
        bool reset_timer = false;
        if (global_int(m_, 0x006a3b00) > 0 && remote_clock > 0) {
            reset_timer = true;
        } else if (local == 0 || step_now < 0x12) {
            reset_timer = true;
        }
        if (!reset_timer) {
            if (step_now == 0x12) {
                const std::uint32_t mode = word_at(m_, local + 0x30b8);
                if ((mode == 0 || mode == 7) && word_at(m_, local + 0x2dd4) == 0) {
                    const std::uint32_t index = global_word(m_, kLocalIndex);
                    const std::uint32_t kept = byte_at(m_, 0x0056b018 + 364 * index);
                    engine(kPadClearOne, {index});
                    set_byte_at(m_, 0x0056b018 + 364 * global_word(m_, kLocalIndex), kept);
                    set_byte_at(m_, 0x0056b0a8 + 364 * global_word(m_, kLocalIndex), 1);
                }
            }
            const std::uint32_t since = global_word(m_, 0x006a3d3c);
            if (since == 0) {
                log_line(0x0054ab00);                               // "Net: Time is up!"
                set_global_word(m_, 0x006a3d3c, global_word(m_, kClock));
            } else {
                const std::uint32_t game_mode = global_word(m_, 0x00533f38);
                const std::uint32_t timeout = game_mode == 8 ? 0xb4 : game_mode == 9 ? 0 : 0x12c;
                bool expired = global_word(m_, kClock) - since > timeout && global_word(m_, 0x006a3df0) % 20 == 0;
                if (!expired) {
                    expired = abort_requested() != 0;
                }
                if (expired) {
                    set_word_at(m_, global_word(m_, kLocalPlayer) + 0x30b8, 0);
                    set_global_word(m_, 0x00568818, 0xffffffffu);
                    engine(seam::net::FUN_004dec50);
                    if (global_word(m_, 0x00533f38) == 9) {
                        set_global_word(m_, 0x0056db64, 2);
                    }
                    bool finish = false;
                    if (global_word(m_, 0x00561bfc) == 0) {
                        finish = global_word(m_, kClock) - global_word(m_, 0x006a3d3c) > timeout * 2;
                    }
                    if (!finish) {
                        finish = abort_requested() != 0;
                    }
                    if (finish) {
                        engine(0x00466c10);
                        end_run();
                        engine(0x0044e0f0);
                        set_global_word(m_, 0x006a3d3c, global_word(m_, kClock));
                    }
                }
            }
        } else {
            set_global_word(m_, 0x006a3d3c, 0);
        }
        if (global_word(m_, kClock) - start > 0x3c) {
            log_line(0x0054aadc);                                   // "NETGAME_Poll() took %d Vblanks"
        }
    }
tail:
    if (global_byte(m_, kLostLink) != 0 && game_mode_runs_net()) {
        if (global_word(m_, kLocalPlayer) != 0) {
            set_word_at(m_, global_word(m_, kLocalPlayer) + 0x2cc8, 0);
        }
        if (global_word(m_, kRemotePlayer) != 0) {
            set_word_at(m_, global_word(m_, kRemotePlayer) + 0x2cc8, 0);
            set_word_at(m_, global_word(m_, kRemotePlayer) + 0x30b8, 0);
        }
    }
    if (global_word(m_, kLocalPlayer) != 0 && global_word(m_, kRemotePlayer) != 0 && (global_word(m_, kRemoteFlags) & 0x4000) != 0) {
        set_local_flag(0x100000, 0);
    }
    if ((global_word(m_, kLocalFlags) & 0x2000) != 0 && (global_word(m_, kRemoteFlags) & 0x2000) != 0 && is_guest()) {
        set_string(0x0056881c, m_.cstring(0x0054a97c, 0x100));
        set_global_word(m_, 0x00568638, 0x10c3);
        s_.assert_failed(0x0054aabc);                               // "Both players are pause master!"
        set_local_flag(0x2000, 0);
    }
}

}  // namespace opentony::runtime::pcplatform
