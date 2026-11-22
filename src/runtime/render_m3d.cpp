#include "auto_views.hpp"
#include "render_kit.hpp"

// Phase 4.15e: the M3D scene renderer's small functions (`m3d.cpp` / `m3dinit.cpp`, 0x00461a50..0x00464a80 and 0x004657c0..0x00466a00): the thin wrappers that submit a model with the state flags the
// scene keeps (0x005620a4, 0x0056437c / 0x00564380, 0x00564378), the model's transform, the texture scrolling (u / v wibble) and the colour pulse, the fog range, the projection ratio and the
// start-up tables, the angles of a rotation matrix, and the clearing of the runtime blockmap grids. Everything outside them (the model state submission, the PSX library, the window size) is a
// service reported by address.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kStateFlags = 0x005620a4;             // dword: flags every submitted model gets
constexpr Word kPendingFlag = 0x0056437c;            // dword: the next submission is the second of a pair (adds 0x200)
constexpr Word kNextPendingFlag = 0x00564380;
constexpr Word kMirrorFlag = 0x00564378;
constexpr Word kSubmitModelState = 0x004d14d0;       // M3D_SubmitModelState(model, flags)
constexpr Word kRegionRecords = 0x0056d428;
constexpr Word kRegionStride = 0x44;
constexpr Word kTime = 0x00563af0;                   // dword: the animation clock (Q4)
constexpr Word kWibblePhase = 0x00563b30;
constexpr Word kWibbleTable = 0x00563b3c;            // 16 x 64 shorts
constexpr Word kGteIr0 = 0x006a3e34;
constexpr Word kGteIr = 0x006a3eb0;
constexpr Word kGteMac = 0x006a3eb8;
constexpr Word kGteRgb1 = 0x006a3e74;
constexpr Word kGteCode = 0x006a3f08;

// a 32-bit signed product with the wrap of the x86 imul
std::int32_t mul32(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<Word>(a) * static_cast<Word>(b)); }

}  // namespace

// the three submissions of a model share a shape: the state flags, plus 0x200 when the previous submission asked for a pair, then the pending flag moves on
void RenderKit::submit_with_flags(Word model, Word extra, bool pair_matters) {
    Word flags = global_word(m_, kStateFlags);
    if (pair_matters) {
        flags |= extra | (global_word(m_, kPendingFlag) != 0 ? 0x200u : 0u);
    } else {
        flags |= extra;
    }
    engine(kSubmitModelState, {model, flags});
}

// 0x00461a50 M3D_SubmitStateGeometry, 0x00461a90 the variant, 0x00461b70 RenderModelNonRotated (cdecl): the model with 0x200 added while a pair is pending
void RenderKit::submit_state_geometry(Word model) {
    submit_with_flags(model, 0, true);
    set_global_word(m_, kPendingFlag, global_word(m_, kNextPendingFlag));
}

// 0x00461ad0 RenderBackgroundModel, 0x00461bb0 RenderBackgroundModelNonRotated (cdecl): the background flag 0x10 (and 0x200 while a pair is pending)
void RenderKit::submit_background_model(Word model) {
    submit_with_flags(model, global_word(m_, kPendingFlag) != 0 ? 0x210u : 0x10u, false);
    set_global_word(m_, kPendingFlag, global_word(m_, kNextPendingFlag));
}

// 0x00461b10 M3D_SetGeometryState(model, flags) (cdecl)
void RenderKit::set_geometry_state(Word model, Word flags) {
    flags |= 1;
    if (global_word(m_, kPendingFlag) != 0) {
        flags |= 0x200;
    }
    if (global_word(m_, kMirrorFlag) != 0) {
        flags |= 8;
    }
    engine(kSubmitModelState, {model, global_word(m_, kStateFlags) | flags});
    set_global_word(m_, kPendingFlag, global_word(m_, kNextPendingFlag));
}

// 0x00461b50 RenderModelInSuperFast(model, flags) (cdecl)
void RenderKit::render_model_super_fast(Word model, Word flags) { engine(kSubmitModelState, {model, (global_word(m_, kStateFlags) | flags) | 1}); }

// 0x00461bf0 M3d_BuildTransform(model) (cdecl): the model's matrix at +0x118 (from its angles, or from the matrix it was given with the first column's signs flipped), scaled when the model asks for it; its
// position in whole units at +0x12c
void RenderKit::build_transform(Word model) {
    const Word matrix = model + 0x118;
    if ((byte_at(m_, model + 0xd8) & 1) != 0) {
        m3d_.copy_matrix(model + 0x2e58, matrix);
        for (Word offset : {0x11au, 0x120u, 0x126u}) {
            set_half_at(m_, model + offset, 0u - half_at(m_, model + offset));
        }
    } else {
        m3d_.rotation_from_angles(model + 0x14, matrix);
    }
    if ((byte_at(m_, model + 5) & 2) != 0) {
        m3d_.scale_object_matrix(model, matrix);
    }
    set_word_at(m_, model + 0x12c, u(s(word_at(m_, model + 8)) >> 12));
    set_word_at(m_, model + 0x130, u(s(word_at(m_, model + 0xc)) >> 12));
    set_word_at(m_, model + 0x134, u(s(word_at(m_, model + 0x10)) >> 12));
}

// 0x00461c90 uWibble / 0x00461cc0 vWibble (cdecl, the wibble record): the amplitude (high nibble) and phase step (low nibble) of byte +2 / +3 give an entry of the 16 x 64 sine table
Word RenderKit::wibble(Word record, Word byte_offset) {
    const Word value = byte_at(m_, record + byte_offset);
    const Word amplitude = value >> 4;
    if (amplitude == 0) {
        return 0;
    }
    const Word index = ((global_word(m_, kWibblePhase) + (value & 0xf) * 4) & 0x3f) + (amplitude << 6);
    return u(static_cast<std::int16_t>(half_at(m_, kWibbleTable + index * 2)));
}

// 0x00461cf0 M3D_ApplyTextureWib(region) (cdecl): scrolls and wobbles the texture coordinates of the faces named by the region's wibble table: per list, the object (and its model) the faces belong to, a
// scroll per frame and a phase; each face's four coordinate pairs get the scroll and the wibble of its record and are wrapped back into the texture page
void RenderKit::apply_texture_wib(Word region) {
    if (region == 0xffffffff || region == 0xff) {
        return;
    }
    const Word record = kRegionRecords + region * kRegionStride;
    Word list = word_at(m_, record + 0x2c);
    if (list == 0) {
        return;
    }
    check(word_at(m_, list - 8) == 6, 0x00533a04);
    Word header = word_at(m_, list);
    if (header == 0) {
        return;
    }
    Word time = global_word(m_, kTime);
    while (header != 0) {
        const Word index = (header - 0xc) / 0x24;
        const Word object = word_at(m_, record + 0x10) + index * 0x4c;
        Word entries = list + 0x10;
        const Word count = half_at(m_, list + 0xc);
        if ((byte_at(m_, object + 5) & 0x80) != 0) {
            list = entries + count * 16;
            header = word_at(m_, list);
            continue;
        }
        const Word model = word_at(m_, word_at(m_, kRegionRecords + byte_at(m_, object + 0x1f) * kRegionStride + 0x14) + half_at(m_, object + 0x1a) * 4);
        Word face = model + 0x1c + (half_at(m_, model + 4) + half_at(m_, model + 2)) * 8;
        const std::int32_t scroll_u = mul32(static_cast<std::int16_t>(half_at(m_, list + 4)), s(time)) >> 4;
        const std::int32_t scroll_v = mul32(static_cast<std::int16_t>(half_at(m_, list + 6)), s(time)) >> 4;
        set_global_word(m_, kWibblePhase, u(mul32(s(word_at(m_, list + 8)), s(time)) >> 10));
        if (count == 0) {
            list = entries;
            header = word_at(m_, list);
            continue;
        }
        for (Word k = count; k != 0; --k) {
            check((byte_at(m_, face) & 1) != 0, 0x005339e4);
            check((scroll_u == 0 && scroll_v == 0) || (word_at(m_, face) & 0x20) != 0, 0x005339c4);
            const Word texture = word_at(m_, face + 0x1c);
            const std::int32_t wrap_v = static_cast<std::int32_t>(((0u - (texture & 0x3e0)) & 0x3fc) * 0x40);
            const std::int32_t wrap_u = static_cast<std::int32_t>(((0u - (texture & 0x1f)) & 0x1f) << 11);
            std::int32_t u0 = byte_at(m_, entries) * 0x100;
            std::int32_t v0 = byte_at(m_, entries + 1) * 0x100;
            std::int32_t u1 = byte_at(m_, entries + 4) * 0x100;
            std::int32_t v1 = byte_at(m_, entries + 5) * 0x100;
            std::int32_t u2 = byte_at(m_, entries + 8) * 0x100;
            std::int32_t v2 = byte_at(m_, entries + 9) * 0x100;
            std::int32_t u3 = byte_at(m_, entries + 0xc) * 0x100;
            std::int32_t v3 = byte_at(m_, entries + 0xd) * 0x100;
            if (scroll_u != 0) {
                u0 += scroll_u;
                u1 += scroll_u;
                u2 += scroll_u;
                u3 += scroll_u;
            }
            if (scroll_v != 0) {
                v0 += scroll_v;
                v1 += scroll_v;
                v2 += scroll_v;
                v3 += scroll_v;
            }
            u0 += s(wibble(entries, 2));
            v0 += s(wibble(entries, 3));
            u1 += s(wibble(entries + 4, 2));
            v1 += s(wibble(entries + 4, 3));
            u2 += s(wibble(entries + 8, 2));
            v2 += s(wibble(entries + 8, 3));
            u3 += s(wibble(entries + 0xc, 2));
            v3 += s(wibble(entries + 0xc, 3));
            auto mixed = [](std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) { return (((a | b | c | d) ^ (a & b & c & d)) & 0x10000) != 0; };
            while (mixed(u0, u1, u2, u3)) {
                u0 += wrap_u;
                u1 += wrap_u;
                u2 += wrap_u;
                u3 += wrap_u;
            }
            while (mixed(v0, v1, v2, v3)) {
                v0 += wrap_v;
                v1 += wrap_v;
                v2 += wrap_v;
                v3 += wrap_v;
            }
            entries += 0x10;
            set_half_at(m_, face + 0x14, ((u(u0) >> 8) & 0xff) | (((u(v0) >> 8) & 0xff) << 8));
            set_half_at(m_, face + 0x16, ((u(u1) >> 8) & 0xff) | (((u(v1) >> 8) & 0xff) << 8));
            set_half_at(m_, face + 0x18, ((u(u2) >> 8) & 0xff) | (((u(v2) >> 8) & 0xff) << 8));
            set_half_at(m_, face + 0x1a, ((u(u3) >> 8) & 0xff) | (((u(v3) >> 8) & 0xff) << 8));
            face += (word_at(m_, face) >> 0x12) * 4;
            time = global_word(m_, kTime);
        }
        list = entries;
        header = word_at(m_, list);
    }
}

// 0x00462170 M3D_UpdateColourPulse(region) (cdecl): each pulse (a list of colours and the frames each lasts) steps with the clock and writes the colour between the current and the next one (a GTE
// interpolation) into the region's RGB table
void RenderKit::update_colour_pulse(Word region) {
    if (region == 0xffffffff || region == 0xff) {
        return;
    }
    const Word record = kRegionRecords + region * kRegionStride;
    Word pulse = word_at(m_, record + 0x30);
    if (pulse == 0) {
        return;
    }
    check(word_at(m_, pulse - 8) == 7, 0x00533a80);
    const Word end = pulse + word_at(m_, pulse - 4);
    const Word rgb = word_at(m_, record + 0x28);
    set_global_word(m_, 0x00563a58, rgb);
    set_global_word(m_, 0x00563a84, rgb);
    check(rgb != 0, 0x00533a5c);
    const Word now = global_word(m_, kTime);
    const Word previous = global_word(m_, 0x00564374);
    const std::int32_t delta = now >= previous ? s(now - previous) : 1;
    while (pulse < end) {
        check(byte_at(m_, pulse + 1) != 0, 0x00533a48);
        Word index = byte_at(m_, pulse + 2);
        const Word count = byte_at(m_, pulse + 1);
        std::int32_t fraction = static_cast<std::int32_t>(byte_at(m_, pulse + 3)) + delta;
        std::int32_t limit = byte_at(m_, pulse + index * 4 + 7);
        if (fraction >= limit) {
            do {
                fraction -= limit;
                ++index;
                if (index == count) {
                    index = 0;
                }
                limit = byte_at(m_, pulse + index * 4 + 7);
            } while (fraction >= limit);
        }
        set_byte_at(m_, pulse + 2, index);
        set_byte_at(m_, pulse + 3, u(fraction));
        const std::int32_t red = byte_at(m_, pulse + index * 4 + 4) * 16;
        const std::int32_t green = byte_at(m_, pulse + index * 4 + 5) * 16;
        const std::int32_t blue = byte_at(m_, pulse + index * 4 + 6) * 16;
        Word next = index + 1;
        if (next == count) {
            next = 0;
        }
        const std::int32_t diff_red = byte_at(m_, pulse + next * 4 + 4) * 16 - red;
        const std::int32_t diff_green = byte_at(m_, pulse + next * 4 + 5) * 16 - green;
        const std::int32_t diff_blue = byte_at(m_, pulse + next * 4 + 6) * 16 - blue;
        check(byte_at(m_, pulse + next * 4 + 7) != 0, 0x00533a38);
        for (Word k = 0; k < 4; ++k) {
            set_byte_at(m_, kGteCode + k, 0);
        }
        const std::int32_t step = mul32(fraction, 4096) / static_cast<std::int32_t>(byte_at(m_, pulse + index * 4 + 7));
        set_global_word(m_, kGteMac + 4, u(green));
        set_global_word(m_, kGteMac + 8, u(blue));
        set_global_half(m_, kGteIr + 2, u(diff_green));
        set_global_half(m_, kGteIr + 4, u(diff_blue));
        set_global_half(m_, kGteIr0, u(step));
        set_global_word(m_, kGteMac, u(red));
        set_global_half(m_, kGteIr, u(diff_red));
        math().interpolate_gpl12();
        set_word_at(m_, rgb + byte_at(m_, pulse) * 4, global_word(m_, kGteRgb1));
        pulse += count * 4 + 4;
    }
}

// 0x004645f0 M3dInit_InitAtStart: the projection ratio of the window, the fog range (near 0x1770, range 0x800), the table of the wibble sines (amplitude 0..15 times the sine of 64 phases) and the sky word
void RenderKit::m3d_init_at_start() {
    set_global_word(m_, 0x005620bc, 0x00562110);
    set_global_word(m_, 0x00563a68, 0x00562d90);
    if (global_word(m_, 0x0056a880) != 0) {
        window_ratio(global_word(m_, 0x00527d50), 0xf0);
    } else {
        window_ratio(global_word(m_, 0x00527d50), global_word(m_, 0x00527d54));
    }
    set_fog_range(0, 0x1770, 0x800);
    Word at = kWibbleTable;
    for (std::int32_t amplitude = 0; amplitude < 0x10; ++amplitude) {
        for (std::int32_t phase = 0; phase < 0x1000; phase += 0x40) {
            set_half_at(m_, at, u((math().sin_q12(phase) * amplitude) >> 4));
            at += 2;
        }
    }
    set_global_word(m_, 0x00563a60, 0x80000000);
}

// 0x00464690 (cdecl, two arguments the function ignores): reads the window's size from the platform layer and keeps the 4:3 correction ratios
void RenderKit::window_ratio(Word, Word) {
    engine(0x004f6d00, {kScratch + 0x20, kScratch + 0x24});
    Word width;
    Word height;
    if (s_.live()) {
        // a real call has written the two words (the scripted values are a replay's)
        width = global_word(m_, kScratch + 0x20);
        height = global_word(m_, kScratch + 0x24);
    } else {
        width = static_cast<Word>(s_.value(pcplatform::engine_name(0x004f6d00)));
        height = static_cast<Word>(s_.value(pcplatform::engine_name(0x004f6d00)));
        set_global_word(m_, kScratch + 0x20, width);
        set_global_word(m_, kScratch + 0x24, height);
    }
    set_global_word(m_, 0x005620a0, width);
    const Word wide = width * 3;
    const Word tall = height * 4;
    set_global_word(m_, 0x005620b8, height);
    if (wide > tall) {
        set_global_word(m_, 0x00563a6c, 0x1000);
        set_global_word(m_, 0x00563a70, (width * 3 << 12) / tall);
    } else {
        set_global_word(m_, 0x00563a70, 0x1000);
        set_global_word(m_, 0x00563a6c, (height << 14) / wide);
    }
}

// 0x00464710 M3D_SetFogRange(unused, near, range) (cdecl): the range must be a power of two; the shift that brings it to 0x1000 is kept
void RenderKit::set_fog_range(Word, Word near, Word range) {
    check(range == (range & (0u - range)), 0x00533ccc);
    const Word far = near + range;
    set_global_word(m_, 0x00563a44, near);
    set_global_word(m_, 0x00563a3c, far);
    Word shift = 0;
    set_global_word(m_, 0x0056209c, 0);
    Word value = range;
    if (value < 0x1000) {
        do {
            value <<= 1;
            ++shift;
        } while (value < 0x1000);
        set_global_word(m_, 0x0056209c, shift);
    }
    if (value > 0x1000) {
        do {
            value >>= 1;
            --shift;
        } while (value > 0x1000);
        set_global_word(m_, 0x0056209c, shift);
    }
    set_global_word(m_, 0x00567ca8, far);
    set_global_word(m_, 0x00567ca4, near);
    set_global_word(m_, 0x00567cac, shift);
}

// 0x00464a80 M3D_PrepareTextureWib(list) (cdecl): each wibble list is marked as scrolling only in u (+0xe) or only in v (+0xf) when none of its faces' coordinates reach into the upper nibble
void RenderKit::prepare_texture_wib(Word list) {
    check(list != 0, 0x00533d30);
    while (word_at(m_, list) != 0) {
        const Word head = list;
        set_byte_at(m_, head + 0xe, 1);
        set_byte_at(m_, head + 0xf, 1);
        Word entry = head;
        Word count = half_at(m_, head + 0xc);
        for (;; --count) {
            list = entry + 0x10;
            if (count == 0) {
                break;
            }
            for (Word offset : {0x12u, 0x16u, 0x1au, 0x1eu}) {
                if (0x10 < (byte_at(m_, entry + offset) & 0xf0)) {
                    set_byte_at(m_, head + 0xe, 0);
                }
                if (0x10 < (byte_at(m_, entry + offset + 1) & 0xf0)) {
                    set_byte_at(m_, head + 0xf, 0);
                }
            }
            entry += 0x10;
        }
    }
}

// 0x004657c0 (cdecl: out, matrix): the three angles of a rotation matrix (shorts at +4, +6, +8, +0xa and +0x10)
void RenderKit::matrix_angles(Word out, Word matrix) {
    const std::int32_t a = static_cast<std::int16_t>(half_at(m_, matrix + 4));
    const std::int32_t b = static_cast<std::int16_t>(half_at(m_, matrix + 0x10));
    const std::int32_t c = static_cast<std::int16_t>(half_at(m_, matrix + 6));
    const std::int32_t d = static_cast<std::int16_t>(half_at(m_, matrix + 8));
    const std::int32_t e = static_cast<std::int16_t>(half_at(m_, matrix + 0xa));
    const std::int32_t length = static_cast<std::int32_t>(sqrt_truncated(u(b * b + a * a)));
    set_half_at(m_, out, u(math().ratan2(-e, length)));
    set_half_at(m_, out + 2, u(math().ratan2(a, b)));
    set_half_at(m_, out + 4, u(math().ratan2(c, d)));
}

// 0x00466030 Blockmap_ClearRuntimeGrid(index) (cdecl): a grid with a non-positive index is cleared (a count and 0x190 words); 0x00466080 clears grid 0
void RenderKit::blockmap_clear_grid(std::int32_t index) {
    if (index > 0) {
        return;
    }
    const Word base = u(index) * 0x660u;
    set_word_at(m_, base + 0x00567f80, 0);
    for (Word offset = 0; offset < 0x190 * 4; offset += 4) {
        set_word_at(m_, base + 0x00567fa0 + offset, 0);
    }
}

}  // namespace opentony::runtime::renderkit
