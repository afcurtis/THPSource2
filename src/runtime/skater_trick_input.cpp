#include "skater_views.hpp"

// Milestone 10g: the trick recognition of physics.cpp on retail memory, written from the machine code and the Phase 4 model (trick_recognition.cpp, which replays 3,363 retail updates word for word over
// a byte image): the pad history (a ring of 32 records of 8 bytes at +0x2a14: the button, whether it went down, the clock; the write index +0x2b14 and the latches +0x2b18), its refresh from the pad profile
// (0x00492190 with 0x00492120 and 0x00491c90), the match of a record walking back through the ring (0x00492560), the clearing of a used record (0x00492030), the selection of the first sequence of the
// player's generated table that the history satisfies (0x004925e0), the queue of the scripts that wait to start (0x00492290, +0x2b38.. with 10 entries of 16 bytes) and the start of the first one that is
// not too old (0x00492400), and the per-frame TrickScript_Update (0x00492ea0) that runs all of them and the interpreter. The script interpreter, the manual and the assertion stamps are as retail's.

namespace opentony::runtime::skaterkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kHistory = 0x2a14;                 // 32 records of 8 bytes: button, down, (2 spare), clock
constexpr Word kHistoryWrite = 0x2b14;
constexpr Word kLatches = 0x2b18;                 // one byte per button
constexpr Word kQueue = 0x2b38;                   // 10 entries of 16 bytes: script, clock, action, timestamp
constexpr Word kQueueCount = 0x2bd8;
constexpr Word kQueueHead = 0x2bdc;
constexpr std::int32_t kHistorySize = 0x20;
constexpr std::int32_t kQueueSize = 10;

constexpr Word kPatternTable = 0x005369c8;        // the direction pattern by the profile and stick bits
constexpr Word kClock = 0x005685f4;
constexpr Word kEventLatch = 0x00568658;
constexpr Word kPlayerOne = 0x0056a858;
constexpr Word kTableOne = 0x00568a50;            // the generated trick table of the first player (the other player's is at 0x00568a4c)
constexpr Word kTableTwo = 0x00568a4c;
constexpr Word kTricksBase = 0x0056a894;
constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
constexpr Word kRefreshHistory = 0x00492190;     // TrickScript_Update calls its four parts as functions (services here, natives of their own)
constexpr Word kSelectSequence = 0x004925e0;
constexpr Word kStartPending = 0x00492400;
constexpr Word kMaybeManual = 0x00497bb0;         // CBruce__MaybeManual(0, 1) (thiscall)
const std::string kPhysicsFile = "H:\\TonyHawk\\Pc2\\physics.cpp";   // 0x00536a30

constexpr Word kBoolText = 0x00536bfc;            // the assertion messages of the two checks
constexpr Word kIndexText = 0x00536be0;

inline std::int32_t signed16(Word value) { return static_cast<std::int16_t>(value & 0xffff); }
inline Word word_of(std::int32_t value) { return static_cast<Word>(value); }

}  // namespace

// 0x00492120 (thiscall): the direction pattern (0..8) of the stick and the pad profile's four direction bytes
Word SkaterKit::derive_trick_pattern(Word self) {
    const Word profile = skater_at(m_, self).action_record_bank().address;
    Word index = byte_at(m_, profile + 0xa0) + 2 * (byte_at(m_, profile + 0xb0) + 2 * (byte_at(m_, profile + 0x80) + 2 * byte_at(m_, profile + 0x90)));
    const std::int8_t second = static_cast<std::int8_t>(skater_at(m_, self).field_31a2());
    const std::int8_t first = static_cast<std::int8_t>(skater_at(m_, self).field_31a1());
    if (second < -0x28) {
        index |= 1;
    }
    if (second > 0x28) {
        index |= 2;
    }
    if (first < -0x28) {
        index |= 4;
    }
    if (first > 0x28) {
        index |= 8;
    }
    return word_at(m_, kPatternTable + 4 * index);
}

// 0x00491c90 (thiscall, two arguments): a changed button state goes into the history ring
void SkaterKit::record_trick_state(Word self, Word button, Word value) {
    set_global_word(m_, kAssertionLine, 0xd4b);
    set_string(kAssertionFile, kPhysicsFile);
    check((value & 0xff) == 0 || (value & 0xff) == 1, kBoolText);
    set_global_word(m_, kAssertionLine, 0xd4c);
    set_string(kAssertionFile, kPhysicsFile);
    check((button & 0xff) <= 0x10, kIndexText);
    const Word latch = self + kLatches + (button & 0xff);
    if (byte_at(m_, latch) == (value & 0xff)) {
        return;
    }
    set_byte_at(m_, latch, value);
    set_byte_at(m_, self + skater_at(m_, self).field_2b14() * 8 + kHistory, button);
    set_byte_at(m_, self + skater_at(m_, self).field_2b14() * 8 + kHistory + 1, value);
    set_word_at(m_, self + skater_at(m_, self).field_2b14() * 8 + kHistory + 4, global_word(m_, kClock));
    const Word next = skater_at(m_, self).field_2b14() + 1;
    skater_at(m_, self).set_field_2b14(static_cast<Word>(next));
    if (next == static_cast<Word>(kHistorySize)) {
        skater_at(m_, self).set_field_2b14(0);
    }
}

// 0x00492190 (thiscall)
void SkaterKit::refresh_trick_history(Word self) {
    const Word pattern = derive_trick_pattern(self);
    for (Word button = 1; button <= 8; ++button) {
        record_trick_state(self, button, pattern == button ? 1 : 0);
    }
    record_trick_state(self, 9, byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x20));
    record_trick_state(self, 0xa, byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x10));
    record_trick_state(self, 0xb, byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x30));
    record_trick_state(self, 0xc, byte_at(m_, skater_at(m_, self).action_record_bank().address));
    record_trick_state(self, 0xe, byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x50));
    record_trick_state(self, 0x10, byte_at(m_, skater_at(m_, self).action_record_bank().address + 0x70));
}

// 0x00492560 (thiscall, five arguments): walking back from (start - direction) to the record before `stop`, the first record of a button (not one of the four special ones when `skip_special`) that went
// down, or -1 when one is older than max_age first
std::int32_t SkaterKit::match_trick_history(Word self, Word start, Word direction, Word max_age, Word stop, Word skip_special) {
    std::int32_t index = static_cast<std::int32_t>(start - direction);
    if (index < 0) {
        index += 0x20;
    }
    const Word clock = global_word(m_, kClock);
    while (true) {
        const Word at = self + word_of(index) * 8;
        const Word action = byte_at(m_, at + kHistory);
        if (action != 0 && !(skip_special != 0 && (action == 6 || action == 5 || action == 7 || action == 8))) {
            if (clock - word_at(m_, at + kHistory + 4) > max_age) {
                return -1;
            }
            if (byte_at(m_, at + kHistory + 1) != 0) {
                return index;
            }
        }
        --index;
        if (index < 0) {
            index = 0x1f;
        }
        if (word_of(index) == stop) {
            return -1;
        }
    }
}

// 0x00492030 (thiscall, two arguments): the newest record of the button with the state is cleared, so that a matched press is not used twice
void SkaterKit::consume_trick_record(Word self, Word action, Word pressed) {
    set_global_word(m_, kAssertionLine, 0xdb2);
    set_string(kAssertionFile, kPhysicsFile);
    check((pressed & 0xff) == 0 || (pressed & 0xff) == 1, kBoolText);
    set_global_word(m_, kAssertionLine, 0xdb3);
    set_string(kAssertionFile, kPhysicsFile);
    check((action & 0xff) <= 0x10, kIndexText);
    const std::int32_t write = static_cast<std::int32_t>(skater_at(m_, self).field_2b14());
    std::int32_t index = write - 1;
    if (index < 0) {
        index = 0x1f;
    }
    while (index != write) {
        const Word at = self + word_of(index) * 8;
        if (byte_at(m_, at + kHistory) == (action & 0xff) && byte_at(m_, at + kHistory + 1) == (pressed & 0xff)) {
            set_byte_at(m_, at + kHistory, 0);
            return;
        }
        --index;
        if (index < 0) {
            index = 0x1f;
        }
    }
}

// 0x00492290 (thiscall, six arguments): a selected script waits in the queue (the jump button's script, action 0xc, is kept apart; one with the 0x2000 flag starts at once)
void SkaterKit::queue_trick_start(Word self, Word script, Word action, Word timestamp, Word flag_4000, Word flag_2000, Word flag_8000) {
    const auto clear_history = [&]() {
        for (Word k = 0; k < 0x20; ++k) {
            set_byte_at(m_, self + kHistory + 8 * k, 0);
        }
    };
    if (static_cast<std::int32_t>(skater_at(m_, self).field_2bd8()) == kQueueSize) {
        return;
    }
    if (action == 0xc) {
        skater_at(m_, self).set_field_29dc(static_cast<Word>(script));
        skater_at(m_, self).set_field_2be0(static_cast<Word>(timestamp));
        skater_at(m_, self).set_field_29e4(static_cast<Word>(flag_4000));
        if (flag_4000 == 0) {
            return;
        }
        const Word expiry = timestamp + 0x3c;
        skater_at(m_, self).set_field_2be0(static_cast<Word>(expiry));
        if (static_cast<std::int32_t>(expiry) < global_int(m_, kClock)) {
            return;
        }
        skater_at(m_, self).set_field_2be0(static_cast<Word>(global_word(m_, kClock) - 1));
        return;
    }
    if (flag_2000 == 0) {
        skater_at(m_, self).set_field_29e0(0);
        const std::int32_t slot = (static_cast<std::int32_t>(skater_at(m_, self).field_2bdc()) + static_cast<std::int32_t>(skater_at(m_, self).field_2bd8())) % kQueueSize;
        const Word entry = self + word_of(slot) * 16;
        set_word_at(m_, entry + kQueue, script);
        set_word_at(m_, entry + kQueue + 4, global_word(m_, kClock));
        set_word_at(m_, entry + kQueue + 0xc, timestamp);
        set_word_at(m_, entry + kQueue + 8, action);
        skater_at(m_, self).set_field_2bd8(static_cast<Word>(skater_at(m_, self).field_2bd8() + 1));
        return;
    }
    if (static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0 && flag_8000 != 0) {
        skater_at(m_, self).set_field_2f24(0);
        skater_at(m_, self).set_field_2dd4(0);
        skater_at(m_, self).set_field_2c68(0);
        skater_at(m_, self).set_field_29e0(0);
        clear_history();
    }
    const Word state = skater_at(m_, self).physics_state();
    if (state == 2 || static_cast<Word>(skater_at(m_, self).field_2dd4()) != 0) {
        return;
    }
    skater_at(m_, self).set_field_29e0(static_cast<Word>(script));
    skater_at(m_, self).set_field_2be4(static_cast<Word>(timestamp));
    if ((state == 0 || state == 7) && global_int(m_, kEventLatch) != 1) {
        thiscall(kMaybeManual, self, {0, 1});
    }
    if (flag_8000 != 0) {
        clear_history();
    }
}

// 0x00492400 (thiscall): the first queued script starts when the skater is in a state that runs tricks and the entry is under 0xb4 frames old (older ones are dropped)
void SkaterKit::start_pending_trick(Word self) {
    if (skater_at(m_, self).field_2c68() != 0 || skater_at(m_, self).field_2f64() != 0 || skater_at(m_, self).field_2bd8() == 0) {
        return;
    }
    const Word state = skater_at(m_, self).physics_state();
    if (state != 1 && state != 2 && state != 3) {
        skater_at(m_, self).set_field_2bd8(0);
        return;
    }
    const Word entry = self + (skater_at(m_, self).field_2bdc() << 4);
    if (global_word(m_, kClock) - word_at(m_, entry + kQueue + 4) < 0xb4) {
        skater_at(m_, self).set_field_2a0c(static_cast<Word>(word_at(m_, entry + kQueue + 8)));
        skater_at(m_, self).set_field_2a10(static_cast<Word>(word_at(m_, entry + kQueue + 0xc)));
        for (Word k = 0; k < 0x20; ++k) {
            const Word record = self + 0x2a18 + 8 * k;
            if (byte_at(m_, record - 3) != 0 && word_at(m_, record) < skater_at(m_, self).field_2a10()) {
                set_byte_at(m_, record - 4, 0);
            }
        }
        trick_script_start(self, word_at(m_, entry + kQueue));
    }
    const std::int32_t next = (static_cast<std::int32_t>(skater_at(m_, self).field_2bdc()) + 1) % kQueueSize;
    skater_at(m_, self).set_field_2bd8(static_cast<Word>(skater_at(m_, self).field_2bd8() - 1));
    skater_at(m_, self).set_field_2bdc(static_cast<Word>(word_of(next)));
}

// 0x004925e0 (thiscall): the generated table of the player is a list of [count][count codes][script offset][flags], ended by a count of 0; the first entry whose codes the history shows (the last two
// from the newest records, the older ones further back) is queued and its records are used up. The flags: 0x3ff the age limit, 0x400 skip the special buttons, 0x2000 start at once, 0x4000 the jump
// button's delay, 0x8000 needs the spin word.
void SkaterKit::select_trick_sequence(Word self) {
    if (skater_at(m_, self).field_2f64() != 0) {
        return;
    }
    const Word table = self == global_word(m_, kPlayerOne) ? global_word(m_, kTableOne) : global_word(m_, kTableTwo);
    const Word base = global_word(m_, kTricksBase);
    const auto entry_word = [&](std::int32_t at) { return half_at(m_, table + word_of(at) * 2); };
    const auto direction = [](std::int32_t code) { return code >= 1 && code <= 8; };
    const auto action_at = [&](std::int32_t index) { return static_cast<std::int32_t>(byte_at(m_, self + kHistory + word_of(index) * 8)); };
    const auto time_at = [&](std::int32_t index) { return word_at(m_, self + kHistory + word_of(index) * 8 + 4); };
    const auto step_back = [](std::int32_t index) { return index - 1 < 0 ? kHistorySize - 1 : index - 1; };

    std::int32_t at = 0;
    while (true) {
        const std::int32_t count = signed16(entry_word(at));
        if (count == 0) {
            return;
        }
        const std::int32_t rel = at + count + 1;
        const Word flags = entry_word(rel + 1);
        const Word script = word_of(signed16(entry_word(rel))) + base;
        at = rel + 2;
        const Word flags_wide = word_of(signed16(flags));
        const Word max_age = flags & 0x3ff;
        const Word flag_2000 = flags_wide & 0x2000;
        const Word skip_special = flags_wide & 0x400;
        const Word flag_4000 = flags_wide & 0x4000;
        const Word flag_8000 = flags & 0x8000;
        const Word state = skater_at(m_, self).physics_state();
        if (flag_2000 == 0 && state != 1 && state != 2 && state != 3) {
            continue;
        }
        if (flag_8000 != 0 && (skater_at(m_, self).field_2f70() & 0xfffff000u) == 0) {
            continue;
        }
        const std::int32_t last = signed16(entry_word(rel - 1));
        const std::int32_t prev = signed16(entry_word(rel - 2));
        std::int32_t older = rel - 3;
        const std::int32_t newest = step_back(static_cast<std::int32_t>(skater_at(m_, self).field_2b14()));
        const auto match = [&](std::int32_t start, Word dir) { return match_trick_history(self, word_of(start), dir, max_age, word_of(newest), skip_special); };
        const auto consume = [&](std::int32_t code) { consume_trick_record(self, word_of(code), 1); };
        const auto queue = [&](Word timestamp) { queue_trick_start(self, script, word_of(last), timestamp, flag_4000, flag_2000, flag_8000); };

        if (count == 2 && direction(prev) && !direction(last)) {
            // a direction followed by a button
            const std::int32_t first = match(newest, 0);
            if (first == -1) {
                continue;
            }
            const std::int32_t second = match(first, 1);
            const Word pattern = derive_trick_pattern(self);
            enum class Go { Next, Both, Last } go = Go::Next;
            Word timestamp = 0;
            if (pattern != 0) {
                if (second != -1) {
                    go = Go::Both;
                } else if (static_cast<std::int32_t>(pattern) == prev && action_at(first) == last) {
                    timestamp = time_at(first);
                    go = Go::Last;
                }
            } else if (second != -1) {
                go = Go::Both;
            }
            if (go == Go::Both) {
                if (action_at(first) == prev && action_at(second) == last) {
                    timestamp = time_at(second);
                    if (flag_2000 == 0) {
                        consume(prev);
                        consume(last);
                    }
                    queue(timestamp);
                    continue;
                }
                if (action_at(second) == prev && action_at(first) == last) {
                    if (flag_2000 == 0) {
                        consume(prev);
                    }
                    timestamp = time_at(first);
                    go = Go::Last;
                } else if (static_cast<std::int32_t>(pattern) == prev) {
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
                if (flag_2000 == 0) {
                    consume(last);
                }
                queue(timestamp);
            }
            continue;
        }

        // a longer sequence, read backwards through the history
        const bool same_kind = direction(last) == direction(prev);
        const std::int32_t first = match(newest, 0);
        if (first == -1) {
            return;
        }
        const std::int32_t second = match(first, 1);
        if (second == -1) {
            continue;
        }
        Word timestamp = 0;
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
        std::int32_t extra[10] = {};
        std::int32_t extra_count = 0;
        std::int32_t remaining = count - 2;
        std::int32_t cursor = second;
        bool complete = remaining == 0;
        while (!complete) {
            const Word code = entry_word(older);
            --older;
            cursor = match(cursor, 1);
            if (cursor == -1 || static_cast<Word>(action_at(cursor)) != code) {
                break;
            }
            if (extra_count < 10) {
                extra[extra_count++] = signed16(code);
            }
            if (--remaining == 0) {
                complete = true;
            }
        }
        if (!complete) {
            continue;
        }
        if (flag_2000 == 0) {
            consume(prev);
            consume(last);
            for (std::int32_t k = 0; k < extra_count; ++k) {
                consume(extra[k]);
            }
        }
        queue(timestamp);
        return;
    }
}

// 0x00492ea0 (thiscall)
void SkaterKit::trick_script_update(Word self) {
    thiscall(kRefreshHistory, self);
    thiscall(kSelectSequence, self);
    thiscall(kStartPending, self);
    if (skater_at(m_, self).field_29c8() != 0) {
        thiscall(kScriptExecute, self);
    }
}

}  // namespace opentony::runtime::skaterkit
