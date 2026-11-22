#include "heap_views.hpp"

// Phase 4.14b: mem.cpp, the game's heap. See heap_kit.hpp for the block layout. The header arithmetic is the compiler's bit-field code, kept as it is: the free headers carry whatever junk was in the
// tag nibble of the memory they were cut from, and the merge adds those junk nibbles into the size field (a retail quirk the replay pins).

namespace opentony::runtime::heap {

using namespace views;

namespace {

using Word = std::uint32_t;


// the assertion messages (addresses of the format strings)
constexpr Word kMsgZeroSize = 0x00534ad0;          // "Zero size sent to Mem_New"
constexpr Word kMsgTooBig = 0x00534ab4;            // "size exceeds 28 bit range"
constexpr Word kMsgPlacementUnhandled = 0x00534990;
constexpr Word kMsgDeleteNull = 0x00534b10;
constexpr Word kMsgDeleteZone = 0x00534aec;
constexpr Word kMsgShrinkSize = 0x00534b84;
constexpr Word kMsgShrinkAlign = 0x00534b64;
constexpr Word kMsgShrinkZone = 0x00534b30;
constexpr Word kMsgBlockZone = 0x00534bac;
constexpr Word kMsgCopyAlign = 0x00534bd8;
constexpr Word kMsgSwapSize = 0x00534bec;
constexpr Word kMsgInitZone = 0x00534944;

// the log lines (addresses of the format strings)
constexpr Word kLogInitBanner = 0x00534980;
constexpr Word kLogInitZone = 0x0053492c;
constexpr Word kLogInitDone = 0x0052a848;
constexpr Word kLogPlacement[4] = {0x00534a60, 0x00534a3c, 0x00534a24, 0x00534a08};
constexpr Word kLogBlockA = 0x005349ec;
constexpr Word kLogBlockNew = 0x005349cc;
constexpr Word kLogBlockB = 0x005349b0;

constexpr Word kBangMessage = 0x00534920;
constexpr Word kPsxPrint = 0x004e8730;

// the tag nibble of `header` replaced by `zone`
inline Word with_zone(Word header, Word zone) { return ((header ^ zone) & 0xf) ^ header; }

}  // namespace

void HeapKit::fill(Word address, Word words) {
    for (Word k = 0; k < words; ++k) {
        write_scalar<Word>(m_, address + 4 * k, mem::kFillWord);
    }
}

// the "low memory" flag follows zone 1's use (0x0056a970) against the limit
void HeapKit::update_low_flag() {
    globals(m_).set_low_memory(zone_used(m_, 1) >= globals(m_).limit() ? 1u : 0u);
}

HeapKit::Word HeapKit::tail_header(Word header, Word taken_size, Word old_word) const {
    Word value = (header & 0xfffffff0u) - (taken_size << 4);
    value -= 0x71;
    return value ^ (~old_word & 0xf);
}

// 0x0046e7f0: put `block` into the zone's free list (ordered by address), merging it with the block before it and the one after it when they touch
void HeapKit::add_to_free_list(Word block, Word zone) {
    Word next = zone_head(m_, zone);
    Word previous = 0;
    if (next >= block) {
        set_zone_head(m_, zone, block);
    } else {
        while (next != 0) {
            previous = next;
            next = next_of(m_, next);
            if (next >= block) {
                break;
            }
        }
        if (previous == 0) {
            set_zone_head(m_, zone, block);
        } else {
            const Word previous_header = hdr(m_, previous);
            const Word previous_end = (previous_header >> 4) + previous + 8;
            if (previous_end != block) {
                set_next_of(m_, previous, block);
            } else {
                const Word block_header = hdr(m_, block);
                const Word block_end = (block_header >> 4) + block + 8;
                if (block_end != next) {
                    // the block joins the one before it
                    Word merged = (previous_header & 0xfffffff0u) + block_header + 0x80;
                    merged ^= (block_header ^ previous_header) & 0xf;
                    set_hdr(m_, previous, merged);
                    return;
                }
                // all three touch: before, this and after become one
                set_next_of(m_, previous, next_of(m_, next));
                const Word next_header = hdr(m_, next);
                const Word block_header_now = hdr(m_, block);
                const Word previous_header_now = hdr(m_, previous);
                Word nibble = (next_header + block_header_now) ^ previous_header_now;
                Word merged = (previous_header_now & 0xfffffff0u) + next_header;
                nibble &= 0xf;
                merged = merged + block_header_now + 0x100;
                set_hdr(m_, previous, nibble ^ merged);
                return;
            }
        }
    }
    // the block does not join the one before it: it may join the one after it
    const Word block_header = hdr(m_, block);
    const Word block_end = (block_header >> 4) + block + 8;
    if (block_end != next) {
        set_next_of(m_, block, next);
        return;
    }
    set_next_of(m_, block, next_of(m_, next));
    const Word next_header = hdr(m_, next);
    Word merged = (block_header & 0xfffffff0u) + next_header + 0x80;
    merged ^= (next_header ^ block_header) & 0xf;
    set_hdr(m_, block, merged);
}

// 0x0046e8c0: every zone becomes one free block; the limit of zone 1 is 98 % of it
void HeapKit::init() {
    log_line(kLogInitBanner);
    for (Word zone = 0; zone < 2; ++zone) {
        const Word begin = zone_bounds(m_, zone).begin();
        const Word end = zone_bounds(m_, zone).end();
        check(begin + 8 < end, kMsgInitZone);
        set_zone_head(m_, zone, 0);
        set_zone_used(m_, zone, 0);
        set_hdr(m_, begin, ((end - begin - 8) << 4) ^ (hdr(m_, begin) & 0xf));
        add_to_free_list(begin, zone);
        log_line(kLogInitZone);
    }
    log_line(kLogInitDone);
    const Word begin = zone_bounds(m_, 1).begin();
    const Word end = zone_bounds(m_, 1).end();
    const Word span = 98u * end - 98u * begin;
    globals(m_).set_limit(static_cast<Word>(static_cast<std::uint64_t>(span) / 100u));
}

// 0x0046ee20: the out-of-memory stop. Retail prints the three numbers and spins; the port reports the print and returns
void HeapKit::bang(Word a, Word b, Word c) { engine(kPsxPrint, {kBangMessage, a, b, c}); }


// 0x0046e9c0 Mem_NewX(size, zone, flag, placement): zone 0 is the top allocation; otherwise the first free block of the zone that is big enough, or the free block that contains `placement`
HeapKit::Word HeapKit::new_x(Word size, Word zone, Word flag, Word placement) {
    if (zone == 0) {
        return new_top(size);
    }
    globals(m_).set_allocation_counter((globals(m_).allocation_counter() + 1) & 0x7fffffffu);
    const Word rounded = (size + 3) & 0xfffffffcu;
    check(rounded != 0, kMsgZeroSize);
    check(rounded < 0x0fffffffu, kMsgTooBig);

    if (placement != 0) {
        const Word wanted_end = placement + rounded + 8;
        Word block = zone_head(m_, zone);
        Word previous = 0;
        if ((hdr(m_, block) >> 4) + block < wanted_end) {
            while (true) {
                const Word next = next_of(m_, block);
                if (next == 0) {
                    break;
                }
                previous = block;
                block = next;
                if (!((hdr(m_, block) >> 4) + block < wanted_end)) {
                    break;
                }
            }
        }
        for (const Word line : kLogPlacement) {
            log_line(line);
        }
        const Word header_at = placement - 8;
        const Word tail = header_at + rounded + 8;
        const Word block_header = hdr(m_, block);
        if ((block_header >> 4) + block <= tail) {
            // the free block ends at (or before) the end of the new one: retail has no case for it, asserts, and hands the whole block out
            check(false, kMsgPlacementUnhandled);
            if (previous != 0) {
                set_next_of(m_, previous, next_of(m_, block));
            } else {
                set_zone_head(m_, zone, next_of(m_, block));
            }
            const Word header = with_zone(hdr(m_, block), zone);
            set_hdr(m_, block, header);
            set_zone_used(m_, zone, zone_used(m_, zone) + (header >> 4) + 8);
            fill(block + 8, rounded >> 2);
            if (zone == 1) {
                update_low_flag();
            }
            return block + 8;
        }
        // the free block is cut in three: what is before the new block, the new block, what is after it
        const Word tail_old = hdr(m_, tail);
        Word tail_header_word = block_header + ((block - tail) << 4);
        tail_header_word ^= (block_header ^ tail_old) & 0xf;
        set_hdr(m_, tail, tail_header_word);
        set_hdr(m_, block, (((header_at - block + 0x0ffffff8u) << 4)) ^ (hdr(m_, block) & 0xf));
        set_hdr(m_, header_at, (rounded << 4) | (hdr(m_, header_at) & 0xf));
        log_line(kLogBlockA);
        log_line(kLogBlockNew);
        log_line(kLogBlockB);
        set_hdr(m_, block, with_zone(hdr(m_, block), zone));
        set_hdr(m_, tail, with_zone(hdr(m_, tail), zone));
        set_hdr(m_, header_at, with_zone(hdr(m_, header_at), zone));
        set_zone_used(m_, zone, zone_used(m_, zone) + (hdr(m_, header_at) >> 4) + 8);
        add_to_free_list(tail, zone);
        fill(header_at + 8, rounded >> 2);
        if (zone == 1) {
            update_low_flag();
        }
        return header_at + 8;
    }

    Word block = zone_head(m_, zone);
    Word previous = 0;
    while ((hdr(m_, block) >> 4) < rounded) {
        const Word next = next_of(m_, block);
        if (next == 0) {
            break;
        }
        previous = block;
        block = next;
    }
    const Word header = hdr(m_, block);
    const Word available = header >> 4;
    if (available < rounded) {
        if (flag != 0 && zone == 1) {
            bang(0x80, 0, 0);
        }
        return 0;
    }
    if (available > rounded + 8) {
        // the block is cut: the front is handed out, the rest goes back to the list
        const Word tail = block + rounded + 8;
        set_hdr(m_, tail, tail_header(header, rounded, hdr(m_, tail)));
        if (previous != 0) {
            set_next_of(m_, previous, next_of(m_, block));
        } else {
            set_zone_head(m_, zone, next_of(m_, block));
        }
        set_hdr(m_, block, (rounded << 4) | (zone & 0xf));
        set_zone_used(m_, zone, zone_used(m_, zone) + rounded + 8);
        add_to_free_list(tail, zone);
    } else {
        if (previous != 0) {
            set_next_of(m_, previous, next_of(m_, block));
        } else {
            set_zone_head(m_, zone, next_of(m_, block));
        }
        const Word taken = with_zone(header, zone);
        set_hdr(m_, block, taken);
        set_zone_used(m_, zone, zone_used(m_, zone) + (taken >> 4) + 8);
    }
    fill(block + 8, rounded >> 2);
    if (zone == 1) {
        update_low_flag();
    }
    return block + 8;
}

// 0x0046ee40 Mem_NewTop: the last free block of zone 1 that is big enough, cut at its end
HeapKit::Word HeapKit::new_top(Word size) {
    globals(m_).set_allocation_counter((globals(m_).allocation_counter() + 1) & 0x7fffffffu);
    const Word rounded = (size + 3) & 0xfffffffcu;
    check(rounded != 0, kMsgZeroSize);
    check(rounded < 0x0fffffffu, kMsgTooBig);
    Word node = zone_head(m_, 1);
    Word best = node;
    Word best_previous = 0;
    Word previous = 0;
    while (node != 0) {
        if ((hdr(m_, node) >> 4) >= rounded) {
            best = node;
            best_previous = previous;
        }
        previous = node;
        node = next_of(m_, node);
    }
    const Word header = hdr(m_, best);
    const Word available = header >> 4;
    if (available < rounded) {
        bang(0x80, 0, 0);
        return 0;
    }
    Word taken_at;
    Word taken_header;
    if (available > rounded + 8) {
        set_hdr(m_, best, ((available - rounded + 0x0ffffff8u) << 4) ^ (header & 0xf));
        taken_at = best + available - rounded;
        taken_header = (rounded << 4) ^ 1;
    } else {
        if (best_previous != 0) {
            set_next_of(m_, best_previous, next_of(m_, best));
        } else {
            set_zone_head(m_, 1, next_of(m_, best));
        }
        taken_at = best;
        taken_header = (header & 0xfffffff0u) ^ 1;
    }
    set_hdr(m_, taken_at, taken_header);
    const Word used = zone_used(m_, 1) + (taken_header >> 4) + 8;
    set_zone_used(m_, 1, used);
    globals(m_).set_low_memory(used >= globals(m_).limit() ? 1u : 0u);
    fill(taken_at + 8, rounded >> 2);
    return taken_at + 8;
}

// 0x0046f000 Mem_DeleteX: the block goes back to its zone's list
void HeapKit::delete_x(Word payload) {
    check(payload != 0, kMsgDeleteNull);
    const Word header_at = payload - 8;
    const Word header = hdr(m_, header_at);
    const std::int32_t zone = static_cast<std::int32_t>(header << 28) >> 28;
    if (zone < 0 || zone >= 2) {
        check(false, kMsgDeleteZone);
        return;
    }
    const Word z = static_cast<Word>(zone);
    set_zone_used(m_, z, zone_used(m_, z) + (0xfffffff8u - (header >> 4)));
    add_to_free_list(header_at, z);
    if (z == 1) {
        update_low_flag();
    }
}

// 0x0046f0e0 Mem_ShrinkX: the tail of the block (from `size` on) goes back to the list
void HeapKit::shrink_x(Word payload, Word size) {
    const std::int32_t zone = static_cast<std::int32_t>(hdr(m_, payload - 8) << 28) >> 28;
    check(size <= (hdr(m_, payload - 8) >> 4), kMsgShrinkSize);
    check((size & 3) == 0, kMsgShrinkAlign);
    check(zone >= 0 && zone < 2, kMsgShrinkZone);
    const Word header = hdr(m_, payload - 8);
    if (size >= (header >> 4) - 8) {
        return;
    }
    const Word z = static_cast<Word>(zone);
    const Word tail = payload + size;
    set_hdr(m_, tail, tail_header(header, size, hdr(m_, tail)));
    set_zone_used(m_, z, zone_used(m_, z) + (size - (hdr(m_, payload - 8) >> 4)));
    set_hdr(m_, payload - 8, (hdr(m_, payload - 8) & 0xf) | (size << 4));
    add_to_free_list(tail, z);
    if (z == 1) {
        update_low_flag();
    }
}

// 0x0046f250 Mem_GetBlockSizeX
HeapKit::Word HeapKit::block_size_x(Word payload) {
    const Word header = hdr(m_, payload - 8);
    check(static_cast<std::int32_t>(header << 28) >= 0 && static_cast<std::int32_t>(header << 28) < 0x20000000, kMsgBlockZone);
    return hdr(m_, payload - 8) >> 4;
}

// 0x0046f2c0 Mem_GetFree: the bytes of the zone nobody holds
HeapKit::Word HeapKit::free_bytes(std::int8_t zone) {
    const Word z = static_cast<Word>(static_cast<std::int32_t>(zone));
    return zone_bounds(m_, z).end() - zone_used(m_, z) - zone_bounds(m_, z).begin();
}

// 0x0046f2e0 Mem_Copy: whole dwords, then the last n & 3 bytes (a negative count copies its low bits' worth of bytes, as retail does); answers the end of the source
HeapKit::Word HeapKit::copy(Word destination, Word source, std::int32_t count) {
    check((destination & 3) == 0, kMsgCopyAlign);
    check((source & 3) == 0, kMsgCopyAlign);
    std::int32_t words = count >> 2;
    Word from = source;
    Word to = destination;
    for (; words > 0; --words) {
        write_scalar<Word>(m_, to, read_scalar<Word>(m_, from));
        from += 4;
        to += 4;
    }
    for (std::int32_t bytes = count & 3; bytes > 0; --bytes) {
        write_scalar<std::uint8_t>(m_, to, read_scalar<std::uint8_t>(m_, from));
        ++from;
        ++to;
    }
    return from;
}

// 0x0046f3b0 Mem_CopyBytes
HeapKit::Word HeapKit::copy_bytes(Word destination, Word source, std::int32_t count) {
    Word done = 0;
    if (count <= 0) {
        return 0;
    }
    for (; static_cast<std::int32_t>(done) < count; ++done) {
        write_scalar<std::uint8_t>(m_, destination + done, read_scalar<std::uint8_t>(m_, source + done));
    }
    return done;
}

// 0x0046f3e0 Mem_Swap
void HeapKit::swap(Word a, Word b, std::int32_t count) {
    check(count > -1 && count < 0x16e360, kMsgSwapSize);
    for (Word left = static_cast<Word>(count); left != 0; --left, ++a, ++b) {
        const Word from_b = read_scalar<std::uint8_t>(m_, b);
        const Word from_a = read_scalar<std::uint8_t>(m_, a);
        write_scalar<std::uint8_t>(m_, a, static_cast<std::uint8_t>(from_b));
        write_scalar<std::uint8_t>(m_, b, static_cast<std::uint8_t>(from_a));
    }
}

// 0x0046f490 FileIO_AllocLoadedBuffer: the allocation of the loaders, 4 bytes shorter than the block it takes (the wrapper's own header)
HeapKit::Word HeapKit::alloc_loaded_buffer(Word size, Word zone, Word flag, Word placement) {
    const Word wanted = size > 4 ? size : 8;
    const Word at = new_x(wanted - 4, zone, flag, placement != 0 ? placement + 0x10 : 0);
    return at != 0 ? at - 4 : 0;
}

// 0x0046f4d0 Mem_Delete
void HeapKit::release(Word payload) { delete_x(payload + 4); }

// 0x0046f4e0 Mem_Shrink
void HeapKit::shrink(Word payload, Word size) { shrink_x(payload + 4, (size > 4 ? size : 8) - 4); }

// 0x0046f510 Mem_GetBlockSize
HeapKit::Word HeapKit::block_size(Word payload) { return block_size_x(payload + 4) + 4; }

}  // namespace opentony::runtime::heap
