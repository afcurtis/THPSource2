#ifndef OPENTONY_HOOK_SESSION_HASH_H
#define OPENTONY_HOOK_SESSION_HASH_H

#include <stdint.h>

/* The block hash of session.trace (Phase 5.1s), shared by the hooked build's session probe
 * (session_trace.cpp) and the standalone's recorder (kits/standalone_session.cpp, milestone 9):
 * a trace of either is compared with the other block by block, so both must hash alike.
 *
 * Four interleaved lanes of multiply-xor over 32-bit words: a change detector, not a checksum,
 * and fast enough for a 38 MB image every frame. */
static inline uint32_t ot_session_hash_block(const uint32_t *words, uint32_t count) {
    uint32_t a = 0x811c9dc5u, b = 0x01000193u, c = 0x9e3779b9u, d = 0x85ebca6bu;
    uint32_t index = 0;
    for (; index + 4 <= count; index += 4) {
        a = (a ^ words[index]) * 0x01000193u;
        b = (b ^ words[index + 1]) * 0x01000193u;
        c = (c ^ words[index + 2]) * 0x01000193u;
        d = (d ^ words[index + 3]) * 0x01000193u;
    }
    for (; index < count; ++index) {
        a = (a ^ words[index]) * 0x01000193u;
    }
    return a ^ (b * 3u) ^ (c * 5u) ^ (d * 7u);
}

/* The same hash over the words of a block, except that a word pointing into the game's memory zones is
 * hashed as an offset from the lowest zone address. The zones are malloc'd by the executable, and any
 * native or probe code that allocates first shifts them by a few bytes, so absolute heap pointers differ
 * between retail and a hooked run while the game state does not. */
static inline uint32_t ot_session_hash_block_normalized(const uint32_t *words, uint32_t count, uint32_t low, uint32_t high) {
    uint32_t a = 0x811c9dc5u, b = 0x01000193u, c = 0x9e3779b9u, d = 0x85ebca6bu;
    uint32_t index = 0;
#define OT_NORM(w) ((w) - low < high - low ? (w) - low + 0x5a5a0000u : (w))
    for (; index + 4 <= count; index += 4) {
        uint32_t w0 = words[index], w1 = words[index + 1], w2 = words[index + 2], w3 = words[index + 3];
        a = (a ^ OT_NORM(w0)) * 0x01000193u;
        b = (b ^ OT_NORM(w1)) * 0x01000193u;
        c = (c ^ OT_NORM(w2)) * 0x01000193u;
        d = (d ^ OT_NORM(w3)) * 0x01000193u;
    }
    for (; index < count; ++index) {
        uint32_t w = words[index];
        a = (a ^ OT_NORM(w)) * 0x01000193u;
    }
#undef OT_NORM
    return a ^ (b * 3u) ^ (c * 5u) ^ (d * 7u);
}

#endif /* OPENTONY_HOOK_SESSION_HASH_H */
