// Native TrickScript_Update (0x00492ea0) and the input-recognition chain under it, Phase 4.7a:
// TrickInput_DerivePattern 0x00492120, TrickInput_RefreshHistory 0x00492190,
// TrickInput_RecordState 0x00491c90, TrickInput_MatchHistory 0x00492560, the history consume
// helper 0x00492030, TrickInput_SelectSequence 0x004925e0, TrickScript_QueueStart 0x00492290,
// TrickScript_StartPending 0x00492400, TrickScript_Start 0x00491b80 and the history query
// 0x004924f0.
//
// Skater layout used here (raw retail offsets): 32 history records of 8 bytes at +0x2a14
// {u8 action, u8 pressed, u32 timestamp at +4}, write index +0x2b14, per-button latches
// +0x2b18[0x11], a ten-entry start queue of 0x10 bytes at +0x2b38 (script pointer, enqueue
// clock, action, timestamp), its count +0x2bd8 and head +0x2bdc.
#include "trick_script.hpp"

namespace opentony::runtime {
namespace {

constexpr std::size_t kHistory = 0x2a14;
constexpr std::size_t kHistoryWrite = 0x2b14;
constexpr std::size_t kLatches = 0x2b18;
constexpr std::size_t kQueue = 0x2b38;
constexpr std::size_t kQueueCount = 0x2bd8;
constexpr std::size_t kQueueHead = 0x2bdc;
constexpr int kHistorySize = 0x20;
constexpr int kQueueSize = 10;

// DAT_005369c8: the composite action pattern for the 16 combinations of the four direction
// bytes (+0x80/+0x90/+0xa0/+0xb0 weights 4/8/1/2) and the analog lean thresholds.
constexpr std::array<std::int32_t, 16> kPatternTable{0, 1, 2, 2, 3, 5, 7, 7, 4, 6, 8, 8, 4, 6, 8, 8};

// Address of the static string "Trick" (0x00536bc0) stored at +0x29c4 by TrickScript_Start.
constexpr std::uint32_t kTrickName = 0x00536bc0;

[[nodiscard]] std::size_t record_offset(int index) { return kHistory + static_cast<std::size_t>(index) * 8; }
[[nodiscard]] std::uint8_t record_action(const RetailImage& s, int index) { return s.u8(record_offset(index)); }
[[nodiscard]] std::uint8_t record_pressed(const RetailImage& s, int index) { return s.u8(record_offset(index) + 1); }
[[nodiscard]] std::uint32_t record_time(const RetailImage& s, int index) { return s.u32(record_offset(index) + 4); }

[[nodiscard]] int step_back(int index) { return index - 1 < 0 ? kHistorySize - 1 : index - 1; }

[[nodiscard]] bool is_direction_code(std::int16_t code) { return code >= 1 && code <= 8; }

class Recognizer final {
public:
    Recognizer(RetailImage& skater, const TrickWorld& world, TrickServices& services)
        : s_(skater), w_(world), svc_(services) {}

    void refresh_history();
    void select_sequence();
    void start_pending();

private:
    [[nodiscard]] const RetailImage& controller() const {
        if (w_.controller == nullptr) {
            throw TrickScriptFault("trick recognition needs the controller image");
        }
        return *w_.controller;
    }
    [[nodiscard]] std::int32_t derive_pattern() const;
    void record_state(std::uint8_t button, std::uint8_t value);
    // TrickInput_MatchHistory (0x00492560).
    [[nodiscard]] int match(int start, int direction, std::uint32_t max_age, int stop, bool skip_special) const;
    // FUN_00492030: clear the newest history record of `action` with `pressed`.
    void consume(std::uint8_t action, std::uint8_t pressed);
    void queue_start(std::uint32_t script, std::int16_t action, std::uint32_t timestamp, std::uint32_t flag_4000,
                     std::uint32_t flag_2000, std::uint32_t flag_8000);
    void clear_history_actions();
    [[nodiscard]] std::uint16_t table_word(std::ptrdiff_t index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= w_.key_table.size()) {
            throw TrickScriptFault("trick key table read out of range");
        }
        return w_.key_table[static_cast<std::size_t>(index)];
    }
    void assert_failed() { svc_.call(s_, {TrickService::Assert, {0}}); }

    RetailImage& s_;
    const TrickWorld& w_;
    TrickServices& svc_;
};

std::int32_t Recognizer::derive_pattern() const {
    const RetailImage& c = controller();
    std::uint32_t index = c.u8(0xa0) + (c.u8(0xb0) + (c.u8(0x80) + c.u8(0x90) * 2U) * 2U) * 2U;
    if (s_.s8(0x31a2) < -0x28) {
        index |= 1;
    }
    if (s_.s8(0x31a2) > 0x28) {
        index |= 2;
    }
    if (s_.s8(0x31a1) < -0x28) {
        index |= 4;
    }
    if (s_.s8(0x31a1) > 0x28) {
        index |= 8;
    }
    return kPatternTable.at(index);
}

void Recognizer::record_state(std::uint8_t button, std::uint8_t value) {
    if (value != 0 && value != 1) {
        assert_failed();
    }
    if (button >= 0x11) {
        assert_failed();
    }
    if (s_.u8(kLatches + button) == value) {
        return;
    }
    s_.set8(kLatches + button, value);
    const int write = s_.s32(kHistoryWrite);
    s_.set8(record_offset(write), button);
    s_.set8(record_offset(write) + 1, value);
    s_.set32(record_offset(write) + 4, w_.clock);
    const int next = write + 1;
    s_.set32(kHistoryWrite, next == kHistorySize ? 0U : static_cast<std::uint32_t>(next));
}

void Recognizer::refresh_history() {
    const std::int32_t pattern = derive_pattern();
    for (std::uint8_t button = 1; button <= 8; ++button) {
        record_state(button, pattern == button ? 1 : 0);
    }
    const RetailImage& c = controller();
    record_state(9, c.u8(0x20));
    record_state(10, c.u8(0x10));
    record_state(0xb, c.u8(0x30));
    record_state(0xc, c.u8(0x00));
    record_state(0xe, c.u8(0x50));
    record_state(0x10, c.u8(0x70));
}

int Recognizer::match(int start, int direction, std::uint32_t max_age, int stop, bool skip_special) const {
    int index = start - direction;
    if (index < 0) {
        index += kHistorySize;
    }
    while (true) {
        const std::uint8_t action = record_action(s_, index);
        if (action != 0 && (!skip_special || (action != 6 && action != 5 && action != 7 && action != 8))) {
            if (max_age < w_.clock - record_time(s_, index)) {
                return -1;
            }
            if (record_pressed(s_, index) != 0) {
                return index;
            }
        }
        index = step_back(index);
        if (index == stop) {
            return -1;
        }
    }
}

void Recognizer::consume(std::uint8_t action, std::uint8_t pressed) {
    if (pressed > 1) {
        assert_failed();
    }
    if (action >= 0x11) {
        assert_failed();
    }
    const int write = s_.s32(kHistoryWrite);
    int index = step_back(write);
    if (index == write) {
        return;
    }
    while (record_action(s_, index) != action || record_pressed(s_, index) != pressed) {
        index = step_back(index);
        if (index == write) {
            return;
        }
    }
    s_.set8(record_offset(index), 0);
}

void Recognizer::clear_history_actions() {
    for (int i = 0; i < kHistorySize; ++i) {
        s_.set8(record_offset(i), 0);
    }
}

// TrickScript_QueueStart (0x00492290). `script` is the resolved script pointer, `action` the
// trigger button, `timestamp` the history time of the matching press.
void Recognizer::queue_start(std::uint32_t script, std::int16_t action, std::uint32_t timestamp,
                             std::uint32_t flag_4000, std::uint32_t flag_2000, std::uint32_t flag_8000) {
    if (s_.s32(kQueueCount) == kQueueSize) {
        return;
    }
    const auto clock = static_cast<std::int32_t>(w_.clock);
    if (action == 0xc) {
        s_.set32(0x29dc, script);
        s_.set32(0x2be0, timestamp);
        s_.set32(0x29e4, flag_4000);
        if (flag_4000 != 0) {
            const auto expiry = static_cast<std::int32_t>(timestamp + 0x3cU);
            s_.set32(0x2be0, static_cast<std::uint32_t>(expiry));
            if (clock <= expiry) {
                s_.set32(0x2be0, static_cast<std::uint32_t>(clock - 1));
                return;
            }
        }
    } else if (flag_2000 == 0) {
        s_.set32(0x29e0, 0);
        const int slot = (s_.s32(kQueueHead) + s_.s32(kQueueCount)) % kQueueSize;
        const std::size_t base = kQueue + static_cast<std::size_t>(slot) * 0x10;
        s_.set32(base, script);
        s_.set32(base + 4, w_.clock);
        s_.set32(base + 0xc, timestamp);
        s_.set32(base + 8, static_cast<std::uint32_t>(static_cast<std::int32_t>(action)));
        s_.set32(kQueueCount, s_.u32(kQueueCount) + 1);
    } else {
        if (s_.s32(0x2dd4) != 0 && flag_8000 != 0) {
            s_.set32(0x2f24, 0);
            s_.set32(0x2dd4, 0);
            s_.set32(0x2c68, 0);
            s_.set32(0x29e0, 0);
            clear_history_actions();
        }
        const std::int32_t state = s_.s32(0x30b8);
        if (state != 2 && s_.s32(0x2dd4) == 0) {
            s_.set32(0x29e0, script);
            s_.set32(0x2be4, timestamp);
            if ((state == 0 || state == 7) && w_.event_latch != 1) {
                svc_.call(s_, {TrickService::MaybeManual, {0, 1}});
            }
            if (flag_8000 != 0) {
                clear_history_actions();
            }
        }
    }
}

// TrickInput_SelectSequence (0x004925e0): walk the generated per-player table
// [count][code * count][stream-relative s16][flags] and queue the first sequence the button
// history satisfies. `prev`/`last` are the last two codes of an entry (for a one-code entry
// `prev` reads the count word, as retail does).
void Recognizer::select_sequence() {
    if (s_.s32(0x2f64) != 0) {
        return;
    }
    std::ptrdiff_t at = 0;
    while (true) {
        const std::int32_t count = static_cast<std::int16_t>(table_word(at));
        if (count == 0) {
            return;
        }
        const std::ptrdiff_t rel = at + count + 1;
        const std::uint16_t flags = table_word(rel + 1);
        const auto script = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int16_t>(table_word(rel))) + static_cast<std::int32_t>(w_.tricks_base));
        at = rel + 2;
        const auto flags_s = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(flags)));
        const std::uint32_t max_age = flags & 0x3ffU;
        const std::uint32_t f2000 = flags_s & 0x2000U;
        const bool skip_special = (flags_s & 0x400U) != 0;
        const std::uint32_t f4000 = flags_s & 0x4000U;
        const std::uint32_t f8000 = flags & 0x8000U;
        const std::int32_t state = s_.s32(0x30b8);
        if (f2000 == 0 && state != 1 && state != 2 && state != 3) {
            continue;
        }
        if (f8000 != 0 && (s_.u32(0x2f70) & 0xfffff000U) == 0) {
            continue;
        }
        const auto last = static_cast<std::int16_t>(table_word(rel - 1));
        const auto prev = static_cast<std::int16_t>(table_word(rel - 2));
        std::ptrdiff_t older = rel - 3;
        const int newest = step_back(s_.s32(kHistoryWrite));
        const auto action_at = [&](int index) { return static_cast<std::int32_t>(record_action(s_, index)); };
        const auto time_at = [&](int index) { return record_time(s_, index); };
        const auto consume_prev = [&] { consume(static_cast<std::uint8_t>(prev), 1); };
        const auto consume_last = [&] { consume(static_cast<std::uint8_t>(last), 1); };

        if (count == 2 && is_direction_code(prev) && !is_direction_code(last)) {
            // A direction pattern followed by a button.
            const int first = match(newest, 0, max_age, newest, skip_special);
            if (first == -1) {
                continue;
            }
            const int second = match(first, 1, max_age, newest, skip_special);
            const std::int32_t pattern = derive_pattern();
            enum class Go { Next, Both, Last } go = Go::Next;
            std::uint32_t timestamp = 0;
            if (pattern != 0) {
                if (second != -1) {
                    go = Go::Both;
                } else if (pattern == prev && action_at(first) == last) {
                    timestamp = time_at(first);
                    go = Go::Last;
                }
            } else if (second != -1) {
                go = Go::Both;
            }
            if (go == Go::Both) {
                if (action_at(first) == prev && action_at(second) == last) {
                    timestamp = time_at(second);
                    if (f2000 == 0) {
                        consume_prev();
                        consume_last();
                    }
                    queue_start(script, last, timestamp, f4000, f2000, f8000);
                    continue;
                }
                if (action_at(second) == prev && action_at(first) == last) {
                    if (f2000 == 0) {
                        consume_prev();
                    }
                    timestamp = time_at(first);
                    go = Go::Last;
                } else if (pattern == prev) {
                    if (action_at(first) == last) {
                        timestamp = time_at(first);
                        go = Go::Last;
                    } else if (action_at(second) == last) {
                        timestamp = time_at(second);
                        go = Go::Last;
                    } else {
                        go = Go::Next;
                    }
                } else {
                    go = Go::Next;
                }
            }
            if (go == Go::Last) {
                if (f2000 == 0) {
                    consume_last();
                }
                queue_start(script, last, timestamp, f4000, f2000, f8000);
            }
            continue;
        }

        // A longer sequence read backwards through the history.
        const bool same_kind = is_direction_code(last) == is_direction_code(prev);
        const int first = match(newest, 0, max_age, newest, skip_special);
        if (first == -1) {
            return;
        }
        const int second = match(first, 1, max_age, newest, skip_special);
        if (second == -1) {
            continue;
        }
        std::uint32_t timestamp = 0;
        if (action_at(second) == prev && action_at(first) == last) {
            timestamp = time_at(first);
        } else {
            if (same_kind) {
                continue;
            }
            const std::int32_t first_action = action_at(first);
            if (first_action != prev || action_at(second) != last) {
                continue;
            }
            timestamp = first_action == last ? time_at(first) : time_at(second);
        }
        std::array<std::int32_t, 10> extra{};
        int extra_count = 0;
        std::int32_t remaining = count - 2;
        int cursor_index = second;
        bool complete = remaining == 0;
        while (!complete) {
            const auto code = static_cast<std::uint16_t>(table_word(older));
            --older;
            cursor_index = match(cursor_index, 1, max_age, newest, skip_special);
            if (cursor_index == -1 || static_cast<std::uint32_t>(action_at(cursor_index)) != code) {
                break;
            }
            if (extra_count < 10) {
                extra[static_cast<std::size_t>(extra_count++)] = static_cast<std::int16_t>(code);
            }
            if (--remaining == 0) {
                complete = true;
            }
        }
        if (!complete) {
            continue;
        }
        if (f2000 == 0) {
            consume_prev();
            consume_last();
            for (int k = 0; k < extra_count; ++k) {
                consume(static_cast<std::uint8_t>(extra[static_cast<std::size_t>(k)]), 1);
            }
        }
        queue_start(script, last, timestamp, f4000, f2000, f8000);
        return;
    }
}

// TrickScript_StartPending (0x00492400).
void Recognizer::start_pending() {
    if (s_.s32(0x2c68) != 0 || s_.s32(0x2f64) != 0 || s_.s32(kQueueCount) == 0) {
        return;
    }
    const std::int32_t state = s_.s32(0x30b8);
    if (state != 1 && state != 2 && state != 3) {
        s_.set32(kQueueCount, 0);
        return;
    }
    const std::int32_t head = s_.s32(kQueueHead);
    const std::size_t base = kQueue + static_cast<std::size_t>(head) * 0x10;
    if (w_.clock - s_.u32(base + 4) < 0xb4U) {
        s_.set32(0x2a0c, s_.u32(base + 8));
        s_.set32(0x2a10, s_.u32(base + 0xc));
        for (int i = 0; i < kHistorySize; ++i) {
            if (record_pressed(s_, i) != 0 && record_time(s_, i) < s_.u32(0x2a10)) {
                s_.set8(record_offset(i), 0);
            }
        }
        trick_script_start(s_, s_.u32(base));
    }
    s_.set32(kQueueCount, s_.u32(kQueueCount) - 1);
    s_.set32(kQueueHead, static_cast<std::uint32_t>((head + 1) % kQueueSize));
}

} // namespace

bool trick_history_query(const RetailImage& skater) {
    // FUN_004924f0: false when a release of the selected button newer than the selection
    // time is already in the history.
    const int write = skater.s32(kHistoryWrite);
    int index = step_back(write);
    if (index != write) {
        do {
            if (record_action(skater, index) == skater.u32(0x2a0c) && record_pressed(skater, index) == 0
                && skater.u32(0x2a10) < record_time(skater, index)) {
                return false;
            }
            index = step_back(index);
        } while (index != write);
    }
    return true;
}

// TrickScript_Start (0x00491b80).
void trick_script_start(RetailImage& skater, std::uint32_t cursor) {
    skater.set32(0x2c68, 1);
    skater.set32(0x29c8, 1);
    skater.set32(0x2c6c, 0);
    skater.set32(0x2f58, 0);
    skater.set32(0x2f54, 0);
    skater.set32(0x29d8, 0);
    skater.set32(0x29d0, 0);
    skater.set32(0x29d4, 0);
    skater.set32(0x29cc, cursor);
    skater.set16(0x29f2, 0);
    skater.set16(0x29f0, 0);
    skater.set32(0x2e2c, 0);
    skater.set32(0x108, 0x10000);
    skater.set16(0x29c0, 0x7b);
    skater.set32(0x29c4, kTrickName);
    skater.set32(0x29e0, 0);
    skater.set32(0x3210, 0);
}

void trick_input_refresh_history(RetailImage& skater, const TrickWorld& world, TrickServices& services) {
    Recognizer(skater, world, services).refresh_history();
}

void trick_input_select_sequence(RetailImage& skater, const TrickWorld& world, TrickServices& services) {
    Recognizer(skater, world, services).select_sequence();
}

void trick_script_start_pending(RetailImage& skater, const TrickWorld& world, TrickServices& services) {
    Recognizer(skater, world, services).start_pending();
}

void trick_script_update(RetailImage& skater, const TrickWorld& world, TrickServices& services) {
    trick_input_refresh_history(skater, world, services);
    trick_input_select_sequence(skater, world, services);
    trick_script_start_pending(skater, world, services);
    if (skater.s32(0x29c8) != 0) {
        trick_script_execute(skater, world, services);
    }
}

} // namespace opentony::runtime
