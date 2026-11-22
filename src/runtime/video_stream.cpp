#include "auto_views.hpp"
#include "video_kit.hpp"

#include <stdexcept>
#include <string>

// Phase 4.15b: the bit stream of the replay recorder and the small helpers around it (video.cpp, 0x004cb130..0x004cb4c0, 0x004cbc90..0x004cbf70, 0x004ccc40..0x004cd710, 0x004cde70..0x004cdef0).
// A stream is a state block (state::k*): a cursor into the buffer, the word being filled and the number of bits of it still free. Bits go in from the low end of the word.

namespace opentony::runtime::videokit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr std::uint32_t kSoundQueueRead = 0x0056e338;
constexpr std::uint32_t kSoundQueueWrite = 0x0056e438;
constexpr std::uint32_t kSoundQueue = 0x0056e340;          // thirty records of {kind, sound}
constexpr std::uint32_t kSoundQueueLength = 0x1e;
constexpr std::uint32_t kRecordingMuted = 0x00561c04;
constexpr std::uint32_t kSplatOther = 0x0056e43c;
constexpr std::uint32_t kSplatPlayer = 0x0056e440;
constexpr std::uint32_t kTrickObjectPlayer = 0x0056e430;
constexpr std::uint32_t kTrickObjectOther = 0x0056e434;
constexpr std::uint32_t kCarPositions = 0x0056e69c;        // eight records of 0x10 bytes: three dwords
constexpr std::uint32_t kCarAngles = 0x0056e71c;           // eight records of 8 bytes: three shorts

}  // namespace

// ---- vectors of shorts ----

// 0x004cb130: out[k] = (short)(*scalar * vector[k]); the first two components are stored as one dword
void VideoKit::scale_shorts(Word out, Word scalar, Word vector) {
    const std::int32_t factor = short_at(m_, scalar);
    const Word z = u(factor * short_at(m_, vector + 4));
    const Word x = u(factor * short_at(m_, vector));
    const Word y = u(factor * short_at(m_, vector + 2));
    set_half_at(m_, out, x);
    set_half_at(m_, out + 2, y);
    set_half_at(m_, out + 4, z);
}

// 0x004cb1d0: out[k] = vector[k] >> (*count & 31)
void VideoKit::shift_shorts(Word out, Word vector, Word count) {
    const Word n = half_at(m_, count) & 0xff;
    const Word z = u(sar(short_at(m_, vector + 4), n));
    const Word x = u(sar(short_at(m_, vector), n));
    const Word y = u(sar(short_at(m_, vector + 2), n));
    set_half_at(m_, out, x);
    set_half_at(m_, out + 2, y);
    set_half_at(m_, out + 4, z);
}

// ---- the sound command queue ----

void VideoKit::reset_sound_queue() {
    set_global_word(m_, kSoundQueueRead, 0);
    set_global_word(m_, kSoundQueueWrite, 0);
}

// 0x004cb330: store at the write index, step it modulo 30; the answer is the quotient (1 when it wrapped)
Word VideoKit::push_sound(Word kind, Word sound) {
    const Word at = global_word(m_, kSoundQueueWrite);
    set_word_at(m_, kSoundQueue + at * 8, kind);
    set_word_at(m_, kSoundQueue + at * 8 + 4, sound);
    const std::int32_t next = s(at) + 1;
    set_global_word(m_, kSoundQueueWrite, u(next % static_cast<std::int32_t>(kSoundQueueLength)));
    return u(next / static_cast<std::int32_t>(kSoundQueueLength));
}

Word VideoKit::sound_pending() { return global_word(m_, kSoundQueueRead) != global_word(m_, kSoundQueueWrite) ? 1u : 0u; }

Word VideoKit::pop_sound(Word kind_out, Word sound_out) {
    const Word at = global_word(m_, kSoundQueueRead);
    set_word_at(m_, kind_out, word_at(m_, kSoundQueue + at * 8));
    set_word_at(m_, sound_out, word_at(m_, kSoundQueue + at * 8 + 4));
    const std::int32_t next = s(at) + 1;
    set_global_word(m_, kSoundQueueRead, u(next % static_cast<std::int32_t>(kSoundQueueLength)));
    return u(next / static_cast<std::int32_t>(kSoundQueueLength));
}

void VideoKit::record_sound_start(Word sound) {
    if (global_word(m_, globals::kState0) == 1 && global_word(m_, kRecordingMuted) == 0) {
        push_sound(1, sound);
    }
}

void VideoKit::record_sound_stop(Word sound) {
    if (global_word(m_, globals::kState0) == 1 && global_word(m_, kRecordingMuted) == 0) {
        push_sound(2, sound);
    }
}

void VideoKit::reset_splats() {
    set_global_word(m_, kSplatOther, 0xffffffff);
    set_global_word(m_, kSplatPlayer, 0xffffffff);
}

void VideoKit::record_splat(Word skater, Word part) {
    check(s(part) < 4, message::kBodyPart);
    if (global_word(m_, globals::kState0) == 1) {
        if (skater == global_word(m_, globals::kPlayerOne)) {
            set_global_word(m_, kSplatPlayer, part);
        } else {
            set_global_word(m_, kSplatOther, part);
        }
    }
}

void VideoKit::reset_trick_objects() {
    set_global_word(m_, kTrickObjectOther, 0xffffffff);
    set_global_word(m_, kTrickObjectPlayer, 0xffffffff);
}

void VideoKit::record_trick_object(Word player, Word index) {
    check(s(index) < 0x200, message::kTrickObIndex);
    if (global_word(m_, globals::kState0) == 1) {
        if (player != 1) {
            if (player == 2) {
                set_global_word(m_, kTrickObjectOther, index);
                return;
            }
            check(false, message::kWrongSkater);
            return;
        }
        set_global_word(m_, kTrickObjectPlayer, index);
    }
}

// ---- the bit stream ----

// 0x004ccc40: the number of bits of a field that holds the value (a sign bit included): 2 for -2..1, one more for every doubling
Word VideoKit::bits_needed(std::int32_t value) {
    std::int32_t bits = 2;
    if (value >= 0x40000000 || value < -0x40000000) {
        throw std::runtime_error("Replay_BitsNeeded: the retail loop never ends for " + std::to_string(value));
    }
    if (value < 0) {
        std::int32_t limit = -2;
        if (value < -2) {
            do {
                limit = s(u(limit) << 1);
                ++bits;
            } while (value < limit);
        }
    } else {
        std::int32_t limit = 2;
        if (value > 1) {
            do {
                limit = s(u(limit) << 1);
                ++bits;
            } while (limit <= value);
        }
    }
    return u(bits);
}

// 0x004ccc70: `bits` (0..32) low bits of the value go into the word being filled; a word that fills up is stored and a new one started
void VideoKit::write_bits(Word state, Word value, std::int32_t bits) {
    check(bits >= 0 && bits <= 0x20, message::kBitsZeroTo32);
    if (bits == 0) {
        return;
    }
    Word left = word_at(m_, state + state::kBitsLeft);
    std::int32_t count = bits;
    if (s(left) < count) {
        const Word shift = 0x20 - left;
        count -= s(left);
        Word filled = shl(value, shift);
        // the part of the value that does not fit goes on to the next word (an arithmetic shift, as the retail code has it)
        const Word rest = u(sar(s(value), left));
        filled = ((shl(1, shift) - 1u) & word_at(m_, state + state::kWord)) | filled;
        set_word_at(m_, state + state::kWord, filled);
        set_word_at(m_, word_at(m_, state + state::kCursor), filled);
        set_word_at(m_, state + state::kWord, 0);
        set_word_at(m_, state + state::kCursor, word_at(m_, state + state::kCursor) + 4);
        set_word_at(m_, state + state::kBitsLeft, 0x20);
        value = rest;
    }
    left = word_at(m_, state + state::kBitsLeft);
    const Word shift = 0x20 - left;
    const Word word = ((shl(1, shift) - 1u) & word_at(m_, state + state::kWord)) | shl(value, shift);
    const std::int32_t remaining = s(left) - count;
    set_word_at(m_, state + state::kWord, word);
    set_word_at(m_, state + state::kBitsLeft, u(remaining));
    if (remaining == 0) {
        set_word_at(m_, word_at(m_, state + state::kCursor), word_at(m_, state + state::kWord));
        set_word_at(m_, state + state::kWord, 0);
        set_word_at(m_, state + state::kCursor, word_at(m_, state + state::kCursor) + 4);
        set_word_at(m_, state + state::kBitsLeft, 0x20);
    }
}

// 0x004cd5a0: `bits` (1..32) bits from the low end of the word being read; a word that runs out is replaced by the next one
Word VideoKit::read_bits(Word state, std::int32_t bits) {
    check(bits >= 1 && bits <= 0x20, message::kBitsOneTo32);
    if (word_at(m_, state + state::kBitsLeft) == 0) {
        set_word_at(m_, state + state::kWord, word_at(m_, word_at(m_, state + state::kCursor)));
        set_word_at(m_, state + state::kCursor, word_at(m_, state + state::kCursor) + 4);
        set_word_at(m_, state + state::kBitsLeft, 0x20);
    }
    const std::int32_t left = int_at(m_, state + state::kBitsLeft);
    if (bits <= left) {
        set_word_at(m_, state + state::kBitsLeft, u(left - bits));
        const Word word = word_at(m_, state + state::kWord);
        set_word_at(m_, state + state::kWord, shr(word, u(bits)));
        return (shl(1, u(bits)) - 1u) & word;
    }
    const Word word = word_at(m_, state + state::kWord);
    const Word next = word_at(m_, word_at(m_, state + state::kCursor));
    set_word_at(m_, state + state::kCursor, word_at(m_, state + state::kCursor) + 4);
    const Word more = u(bits - left);
    set_word_at(m_, state + state::kWord, next);
    set_word_at(m_, state + state::kWord, shr(next, more));
    set_word_at(m_, state + state::kBitsLeft, 0x20 - more);
    return ((shl(1, u(left)) - 1u) & word) | (((shl(1, more) - 1u) & next) << (u(left) & 31u));
}

// 0x004cd690: the field sign-extended from its top bit
Word VideoKit::read_signed_bits(Word state, std::int32_t bits) {
    check(bits >= 1 && bits <= 0x20, message::kBitsOneTo32);
    Word value = read_bits(state, bits);
    if ((value & shl(1, u(bits) - 1u)) != 0) {
        value |= ~(shl(1, u(bits)) - 1u);
    }
    return value;
}

// 0x004cd710
Word VideoKit::read_flag(Word state) {
    if (word_at(m_, state + state::kBitsLeft) == 0) {
        set_word_at(m_, state + state::kWord, word_at(m_, word_at(m_, state + state::kCursor)));
        set_word_at(m_, state + state::kCursor, word_at(m_, state + state::kCursor) + 4);
        set_word_at(m_, state + state::kBitsLeft, 0x20);
    }
    const Word word = word_at(m_, state + state::kWord);
    set_word_at(m_, state + state::kBitsLeft, word_at(m_, state + state::kBitsLeft) - 1u);
    set_word_at(m_, state + state::kWord, word >> 1);
    return word & 1u;
}

// 0x004cbf70: a partly filled word is stored and the next one started
void VideoKit::flush_input_block(Word state) {
    if (word_at(m_, state + state::kBitsLeft) != 0x20) {
        set_word_at(m_, word_at(m_, state + state::kCursor), word_at(m_, state + state::kWord));
        set_word_at(m_, state + state::kWord, 0);
        set_word_at(m_, state + state::kCursor, word_at(m_, state + state::kCursor) + 4);
        set_word_at(m_, state + state::kBitsLeft, 0x20);
    }
}

// 0x004cbf30
void VideoKit::reset_skater_input(Word skater) {
    if (skater != 0) {
        const Word block = state_of(skater);
        if (word_at(m_, block) == 1) {
            flush_input_block(block);
        }
        set_word_at(m_, block, 0);
        set_global_word(m_, globals::kHighlightCursor, 0);
    }
}

void VideoKit::clear_playback_state() {
    set_global_word(m_, globals::kState0, 0);
    set_global_word(m_, globals::kState1, 0);
}

// 0x004cbca0 (the buffers are released through Mem_Delete)
void VideoKit::free_stream_buffers() {
    if (global_word(m_, globals::kStreamBuffer0) != 0) {
        s_.note("mem_delete", {global_word(m_, globals::kStreamBuffer0)});
    }
    if (global_word(m_, globals::kStreamBuffer1) != 0) {
        s_.note("mem_delete", {global_word(m_, globals::kStreamBuffer1)});
    }
    set_global_word(m_, globals::kStreamBuffer0, 0);
    set_global_word(m_, globals::kStreamBuffer1, 0);
}

// 0x004cbfa0: no mode, a poisoned cursor and no bits left
void VideoKit::stop_stream(Word skater) {
    check(skater != 0, 0x0054833c);
    const Word block = state_of(skater);
    set_word_at(m_, block, 0);
    set_word_at(m_, block + state::kCursor, 0xdeadbeef);
    set_word_at(m_, block + state::kBitsLeft, 0);
}

// 0x004cc020: the cursor back at the boundary, no bits left, every frame still to play
void VideoKit::rewind_stream(Word skater) {
    check(skater != 0, 0x0054833c);
    const Word block = state_of(skater);
    const Word boundary = word_at(m_, block + state::kBoundary);
    const Word frames = word_at(m_, block + state::kFrames);
    set_word_at(m_, block + state::kCursor, boundary);
    set_word_at(m_, block + state::kBitsLeft, 0);
    set_word_at(m_, block + state::kFramesLeft, frames);
}

Word VideoKit::is_playing() { return global_word(m_, globals::kState0) == 2 ? 1u : 0u; }

// 0x004cde80: the position (three dwords) and angles (three shorts) of a car, eight slots
void VideoKit::store_car_position(Word, std::int32_t slot, Word x, Word y, Word z, Word angles, Word last) {
    if (7 < slot) {
        log_line(0x00548514);
        return;
    }
    // (the log line's arguments are the slot and the limit: the probe's printf stub reports the format only)
    const Word at = u(slot) * 0x10;
    set_word_at(m_, kCarPositions + at, x);
    set_word_at(m_, kCarPositions + at + 4, y);
    set_word_at(m_, kCarPositions + at + 8, z);
    set_half_at(m_, kCarAngles + u(slot) * 8, angles);
    set_half_at(m_, kCarAngles + u(slot) * 8 + 2, angles >> 16);
    set_half_at(m_, kCarAngles + u(slot) * 8 + 4, last);
}

// 0x004cdef0
void VideoKit::load_car_position(Word, std::int32_t slot, Word position, Word angles) {
    if (7 < slot) {
        log_line(0x0054853c);
        return;
    }
    const Word at = u(slot) * 0x10;
    set_word_at(m_, position, word_at(m_, kCarPositions + at));
    set_word_at(m_, position + 4, word_at(m_, kCarPositions + at + 4));
    set_word_at(m_, position + 8, word_at(m_, kCarPositions + at + 8));
    set_half_at(m_, angles, half_at(m_, kCarAngles + u(slot) * 8));
    set_half_at(m_, angles + 2, half_at(m_, kCarAngles + u(slot) * 8 + 2));
    set_half_at(m_, angles + 4, half_at(m_, kCarAngles + u(slot) * 8 + 4));
}

}  // namespace opentony::runtime::videokit
