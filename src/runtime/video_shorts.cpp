#include "auto_views.hpp"
#include "video_kit.hpp"

#include <stdexcept>

// Phase 4.15b: the short-vector class of vector.cpp (0x004cadc0..0x004cb2c0): vectors of three 16-bit components, stored as a dword (x, y) and a word (z). The methods that change a vector in place do their
// arithmetic on 16-bit operands (`imul dx, ax`, `sar word ptr [eax], cl`), where the count of a shift is taken modulo 32 (a count of 16 or more empties the operand); the `Out` variants build the result in
// a local and store it whole.

namespace opentony::runtime::videokit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

// a 16-bit shift as the processor does it: the count is taken modulo 32, an operand shifted by 16 or more is all fill
inline Word shl16(Word value, Word count) {
    count &= 31u;
    return count >= 16 ? 0u : (value << count) & 0xffffu;
}
inline Word sar16(Word value, Word count) {
    count &= 31u;
    const std::int32_t v = static_cast<std::int16_t>(value);
    return count >= 16 ? (v < 0 ? 0xffffu : 0u) : (static_cast<Word>(v >> count) & 0xffffu);
}
inline std::int32_t divide_checked(std::int32_t numerator, std::int32_t denominator) {
    if (denominator == 0 || (numerator == INT32_MIN && denominator == -1)) {
        throw std::runtime_error("division fault in the short-vector math");
    }
    return numerator / denominator;
}

}  // namespace

// 0x004cadc0: the GTE squares the components (IR1..3 in, MAC1..3 out), the sum of the three goes through the integer square root
Word VideoKit::shorts_length(Word self) {
    const Word x = u(video_shorts_shorts_at(m_, self).field_0());
    const Word y = u(video_shorts_shorts_at(m_, self).field_2());
    const Word z = u(video_shorts_shorts_at(m_, self).field_4());
    set_global_half(m_, 0x006a3eb0, x);
    set_global_half(m_, 0x006a3eb2, y);
    set_global_half(m_, 0x006a3eb4, z);
    math_.sqr0();
    const Word sum = global_word(m_, 0x006a3ec0) + global_word(m_, 0x006a3ebc) + global_word(m_, 0x006a3eb8);
    return sqrt_truncated(sum);
}

// 0x004cae50: each component times the short a pointer names (read again for each component)
Word VideoKit::shorts_mul_assign(Word self, Word scalar) {
    for (Word k = 0; k < 3; ++k) {
        const Word product = static_cast<Word>(short_at(m_, scalar)) * static_cast<Word>(short_at(m_, self + 2 * k));
        set_half_at(m_, self + 2 * k, product);
    }
    return self;
}

// 0x004cae80: each component divided by the dword a pointer names (signed, read again for each component)
Word VideoKit::shorts_div_assign(Word self, Word divisor) {
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 2 * k, u(divide_checked(short_at(m_, self + 2 * k), int_at(m_, divisor))));
    }
    return self;
}

// 0x004caeb0, 0x004caed0
void VideoKit::shorts_shift_right(Word self, Word count) {
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 2 * k, sar16(half_at(m_, self + 2 * k), half_at(m_, count)));
    }
}

void VideoKit::shorts_shift_left(Word self, Word count) {
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 2 * k, shl16(half_at(m_, self + 2 * k), half_at(m_, count)));
    }
}

// 0x004cafa0: this - other, each component folded into -0x800..0x800 (below -0x800 add 0x1000, then above 0x800 subtract 0x1000)
Word VideoKit::shorts_angle_difference(Word self, Word out, Word other) {
    Word value[3];
    for (Word k = 0; k < 3; ++k) {
        Word d = (half_at(m_, self + 2 * k) - half_at(m_, other + 2 * k)) & 0xffffu;
        if (static_cast<std::int16_t>(d) < static_cast<std::int16_t>(0xf800)) {
            d = (d + 0x1000u) & 0xffffu;
        }
        if (static_cast<std::int16_t>(d) > 0x800) {
            d = (d + 0xf000u) & 0xffffu;
        }
        value[k] = d;
    }
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, out + 2 * k, value[k]);
    }
    return out;
}

// 0x004cb0e0: out = vector * the short a pointer names (every product truncated to a short; the scalar is read first)
Word VideoKit::shorts_mul_out(Word out, Word vector, Word scalar) {
    const Word factor = static_cast<Word>(short_at(m_, scalar));
    Word value[3];
    for (Word k = 0; k < 3; ++k) {
        value[k] = static_cast<Word>(short_at(m_, vector + 2 * k)) * factor;
    }
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, out + 2 * k, value[k]);
    }
    return out;
}

// 0x004cb180: out = vector / the dword a pointer names (signed)
Word VideoKit::shorts_div_out(Word out, Word vector, Word divisor) {
    const std::int32_t d = int_at(m_, divisor);
    Word value[3];
    for (Word k = 0; k < 3; ++k) {
        value[k] = u(divide_checked(short_at(m_, vector + 2 * k), d));
    }
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, out + 2 * k, value[k]);
    }
    return out;
}

// 0x004cb220: out = vector << the count the low byte of a dword names (16-bit shifts)
Word VideoKit::shorts_shift_left_out(Word out, Word vector, Word count) {
    const Word n = word_at(m_, count) & 0xffu;
    Word value[3];
    for (Word k = 0; k < 3; ++k) {
        value[k] = shl16(half_at(m_, vector + 2 * k), n);
    }
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, out + 2 * k, value[k]);
    }
    return out;
}

// 0x004cb270: out = -this
Word VideoKit::shorts_negate_out(Word self, Word out) {
    Word value[3];
    for (Word k = 0; k < 3; ++k) {
        value[k] = 0u - half_at(m_, self + 2 * k);
    }
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, out + 2 * k, value[k]);
    }
    return out;
}

// 0x004cb2c0: eighteen dwords of the playback states are zeroed
void VideoKit::clear_body_state() {
    for (Word address : {0x0056e604u, 0x0056e608u, 0x0056e60cu, 0x0056e670u, 0x0056e674u, 0x0056e678u, 0x0056e67cu, 0x0056e680u, 0x0056e684u, 0x0056e47cu, 0x0056e480u, 0x0056e484u, 0x0056e4e8u,
                         0x0056e4ecu, 0x0056e4f0u, 0x0056e4f4u, 0x0056e4f8u, 0x0056e4fcu}) {
        set_word_at(m_, address, 0);
    }
}

}  // namespace opentony::runtime::videokit
