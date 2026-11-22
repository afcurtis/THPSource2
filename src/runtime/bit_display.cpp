#include "auto_views.hpp"
#include "bit_kit.hpp"

#include <algorithm>
#include <bit>
#include <string>

// Phase 4.15d: Bit_Display (0x00407310), the function that turns every list of bits into primitives for the frame: it walks the lists in a fixed order, projects each bit through the GTE and writes the
// primitives into the renderer's polygon arena, each linked into the depth bucket of its z (the bucket's winding value is the viewport's near plane over the bucket's depth, in float). The arena is a
// bump allocator (RenderPolygonArenaCursor .. RenderPolygonArenaEnd); a primitive that does not fit ends the whole function. Float values are the 24-bit x87 ones of the render window.

namespace opentony::runtime::bitkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kArenaCursor = 0x00560fd0;
constexpr Word kArenaEnd = 0x00568628;
constexpr Word kRenderer = 0x00560fd4;               // pointer: the renderer record (+0x84 the first bucket table, +0x8c the one in use)
constexpr Word kViewport = 0x00563a38;               // pointer: the viewport edges (+8 near, +0xa far, +0xe scale, +0x10, +0x12 centre)
constexpr Word kWindingThreshold = 0x00518484;       // float
constexpr Word kScreenWidth = 0x029da394;
constexpr Word kScreenHeight = 0x029da398;
constexpr Word kSoftwareRenderer = 0x029da3a0;
constexpr Word kViewIndex = 0x0055fa3c;
constexpr Word kViewMatrix = 0x0055f83c;
constexpr Word kViewPosition = 0x0055f7cc;
constexpr Word kViewStride = 0xa4;
constexpr Word kHeaderWords = 0x0055f6d0;            // the two words every list's header primitive starts with
constexpr Word kClipPoints = 0x006a6d6c;             // the points of two glow spokes (SClipLineBuf)
constexpr Word kBoxCamera = 0x005620e0;
constexpr Word kProjectBoxCorners = 0x004f3f30;      // Camera_ProjectBoxCorners(box, camera): the near depth bucket, or negative

// the order of the lists in Bit_Display, with the heads of the lists
constexpr Word kSpecialList = lists::kSpecial;

}  // namespace

// ---- the primitive arena ----

// the arena's next primitive of `size` bytes, or 0 when it does not fit (then Bit_Display ends); the cursor moves on
Word BitKit::arena_take(Word bytes) {
    const Word cursor = global_word(m_, kArenaCursor);
    if (cursor + bytes > global_word(m_, kArenaEnd)) {
        return 0;
    }
    set_global_word(m_, kArenaCursor, cursor + bytes);
    return cursor;
}

// the primitive's winding value (at `slot`, a dword index) and its link in the bucket of `index`: the float is the viewport's near plane over the bucket's depth (-1 when the renderer has one table)
void BitKit::bucket_link(Word prim, Word slot, Word index) {
    const Word renderer = global_word(m_, kRenderer);
    const Word table = word_at(m_, renderer + 0x8c);
    const float depth = static_cast<float>(static_cast<std::int32_t>(index * 4));
    if (table == word_at(m_, renderer + 0x84)) {
        set_word_at(m_, prim + 4 * slot, 0xbf800000);
    } else {
        const float near = static_cast<float>(static_cast<std::int32_t>(half_at(m_, global_word(m_, kViewport) + 8)));
        float value = near;
        if (!(depth <= std::bit_cast<float>(global_word(m_, kWindingThreshold)))) {
            value = near / depth;
        }
        set_word_at(m_, prim + 4 * slot, std::bit_cast<Word>(value));
    }
    set_word_at(m_, prim, word_at(m_, word_at(m_, global_word(m_, kRenderer) + 0x8c) + index * 8));
    set_word_at(m_, word_at(m_, global_word(m_, kRenderer) + 0x8c) + index * 8, prim);
}

// a primitive's three colour bytes of the word at `at`, scaled by alpha / 256 (the code byte is kept)
void BitKit::scale_colour(Word at, Word alpha) {
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, at + k, (static_cast<Word>(byte_at(m_, at + k)) * alpha) >> 8);
    }
}

// ---- Bit_Display ----

// 0x00407310
void BitKit::bit_display() {
    const Word view = global_word(m_, kViewIndex) * kViewStride;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, mathkit::gte::kTranslation + 4 * k, 0);
    }
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, mathkit::gte::kRotation + 2 * k, half_at(m_, kViewMatrix + view + 2 * k));
    }
    constexpr Word kCamera = kScratch + 0x1000;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kCamera + 4 * k, word_at(m_, kViewPosition + view + 4 * k));
    }
    const Word half = static_cast<Word>(half_at(m_, global_word(m_, kViewport) + 0xe)) >> 1;
    for (Word bit = global_word(m_, kSpecialList); bit != 0; bit = word_at(m_, bit + bit::kNext)) {
        virtual_call(bit, 2);
    }
    if (!display_glows(kCamera, half) || !display_gpolylines() || !display_polylines() || !display_flats(kCamera) || !display_glass(kCamera) || !display_glines(kCamera) ||
        !display_quads(kCamera) || !display_chunks(kCamera) || !display_pixels(kCamera) || !display_linked(kCamera)) {
        return;
    }
}

// V0 = the position (shifted down by `shift`, a Q12 world position in whole units) relative to the camera, as shorts; RTPS
void BitKit::project(Word position, Word shift, Word camera) {
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t value = (int_at(m_, position + 4 * k) >> shift) - int_at(m_, camera + 4 * k);
        set_half_at(m_, mathkit::gte::kV0 + 2 * k, static_cast<Word>(value));
    }
    math().rtps();
}

bool BitKit::z_in_range(std::int32_t z) {
    const Word viewport = global_word(m_, kViewport);
    return static_cast<std::int32_t>(half_at(m_, viewport + 8)) <= z && z <= static_cast<std::int32_t>(half_at(m_, viewport + 10));
}

// the depth bucket of a z (the projection's z less the bit's bias, a quarter of it, 12 bits)
Word BitKit::depth_bucket(std::int32_t z, Word bias) { return static_cast<Word>((z - static_cast<std::int32_t>(bias)) >> 2) & 0xfff; }

// Camera_ProjectBoxCorners(box, camera): the box's two corners (the near depth bucket, or negative) and the screen points it leaves at 0x006a6d84 / 0x006a6d88
std::int32_t BitKit::project_box(Word box) {
    engine(kProjectBoxCorners, {box, global_word(m_, kBoxCamera)});
    const std::string name = pcplatform::engine_name(kProjectBoxCorners);
    if (!s_.live()) {
        set_global_word(m_, kClipPoints + 0x18, static_cast<Word>(s_.value(name)));
        set_global_word(m_, kClipPoints + 0x1c, static_cast<Word>(s_.value(name)));
    }
    return s_.value(name);
}

// the gouraud polylines (SClipLineBuf is the box handed to the box projector: the previous point then the next)
bool BitKit::display_gpolylines() {
    for (Word line = global_word(m_, lists::kGPolyLine); line != 0; line = word_at(m_, line + bit::kNext)) {
        set_global_word(m_, kClipPoints, word_at(m_, line + 0x44));
        Word colour_a = word_at(m_, line + 0x50);
        set_global_word(m_, kClipPoints + 4, word_at(m_, line + 0x48));
        set_global_word(m_, kClipPoints + 8, word_at(m_, line + 0x4c));
        Word point = word_at(m_, line + 0x40);
        for (std::int32_t k = 0; k < int_at(m_, line + 0x3c); ++k) {
            set_global_word(m_, kClipPoints + 12, word_at(m_, point));
            const Word colour_b = word_at(m_, point + 12);
            set_global_word(m_, kClipPoints + 16, word_at(m_, point + 4));
            set_global_word(m_, kClipPoints + 20, word_at(m_, point + 8));
            const std::int32_t bucket = project_box(kClipPoints);
            if (bucket >= 0) {
                const Word cursor = global_word(m_, kArenaCursor);
                if (cursor + 0x28 > global_word(m_, kArenaEnd)) {
                    return false;
                }
                set_global_word(m_, kArenaCursor, cursor + 0x28);
                const Word prim = cursor + 0x10;
                set_word_at(m_, prim + 4, colour_a);
                set_word_at(m_, prim, 0x4000000);
                set_word_at(m_, cursor + 0x1c, colour_b);
                set_word_at(m_, cursor + 0x18, global_word(m_, kClipPoints + 0x18));
                set_word_at(m_, cursor + 0x20, global_word(m_, kClipPoints + 0x1c));
                const Word priority = byte_at(m_, line + 0x54);
                bucket_link(prim, 5, priority == 0 ? static_cast<Word>(bucket) : priority);
                if ((colour_a & 0x2000000) != 0) {
                    set_word_at(m_, cursor + 4, global_word(m_, kHeaderWords + 4));
                    set_word_at(m_, cursor, global_word(m_, kHeaderWords));
                    set_word_at(m_, cursor + 12, 0);
                    bucket_link(cursor, 2, priority);
                }
            }
            colour_a = colour_b;
            set_global_word(m_, kClipPoints, global_word(m_, kClipPoints + 12));
            set_global_word(m_, kClipPoints + 4, global_word(m_, kClipPoints + 16));
            set_global_word(m_, kClipPoints + 8, global_word(m_, kClipPoints + 20));
            point += 16;
        }
    }
    return true;
}

bool BitKit::display_polylines() {
    for (Word line = global_word(m_, lists::kPolyLine); line != 0; line = word_at(m_, line + bit::kNext)) {
        set_global_word(m_, kClipPoints, word_at(m_, line + 0x48));
        set_global_word(m_, kClipPoints + 4, word_at(m_, line + 0x4c));
        set_global_word(m_, kClipPoints + 8, word_at(m_, line + 0x50));
        Word point = word_at(m_, line + 0x44);
        for (std::int32_t k = 0; k < int_at(m_, line + 0x40); ++k) {
            set_global_word(m_, kClipPoints + 12, word_at(m_, point));
            const Word colour = word_at(m_, point + 12);
            set_global_word(m_, kClipPoints + 16, word_at(m_, point + 4));
            set_global_word(m_, kClipPoints + 20, word_at(m_, point + 8));
            const std::int32_t bucket = project_box(kClipPoints);
            if (bucket >= 0) {
                const Word cursor = global_word(m_, kArenaCursor);
                if (cursor + 0x24 > global_word(m_, kArenaEnd)) {
                    return false;
                }
                set_global_word(m_, kArenaCursor, cursor + 0x24);
                const Word prim = cursor + 0x10;
                set_word_at(m_, prim, 0x3000000);
                set_word_at(m_, cursor + 0x14, colour);
                set_word_at(m_, cursor + 0x18, global_word(m_, kClipPoints + 0x18));
                set_word_at(m_, cursor + 0x1c, global_word(m_, kClipPoints + 0x1c));
                const Word priority = byte_at(m_, line + 0x3c);
                bucket_link(prim, 4, priority == 0 ? static_cast<Word>(bucket) : priority);
                if ((colour & 0x2000000) != 0) {
                    set_word_at(m_, cursor + 4, global_word(m_, kHeaderWords + 4));
                    set_word_at(m_, cursor, global_word(m_, kHeaderWords));
                    set_word_at(m_, cursor + 12, 0);
                    bucket_link(cursor, 2, priority);
                }
            }
            set_global_word(m_, kClipPoints + 4, global_word(m_, kClipPoints + 16));
            set_global_word(m_, kClipPoints, global_word(m_, kClipPoints + 12));
            set_global_word(m_, kClipPoints + 8, global_word(m_, kClipPoints + 20));
            point += 16;
        }
    }
    return true;
}

// the glass shards: three corners through RTPT, a triangle and its header
bool BitKit::display_glass(Word camera) {
    for (Word glass = global_word(m_, lists::kGlass); glass != 0; glass = word_at(m_, glass + bit::kNext)) {
        for (Word corner = 0; corner < 3; ++corner) {
            for (Word k = 0; k < 3; ++k) {
                const std::int32_t value = (int_at(m_, glass + 0x3c + 12 * corner + 4 * k) >> 12) - int_at(m_, camera + 4 * k);
                set_half_at(m_, mathkit::gte::kV0 + 8 * corner + 2 * k, static_cast<Word>(value));
            }
        }
        math().rtpt();
        const Word prim = arena_take(0x28);
        if (prim == 0) {
            return false;
        }
        const Word second = prim + 0x18;
        set_word_at(m_, prim, 0x4000000);
        set_byte_at(m_, prim + 7, 0x22);
        set_byte_at(m_, prim + 4, byte_at(m_, glass + 0x67));
        set_byte_at(m_, prim + 5, byte_at(m_, glass + 0x68));
        set_byte_at(m_, prim + 6, byte_at(m_, glass + 0x69));
        set_word_at(m_, prim + 0x1c, global_word(m_, kHeaderWords + 4));
        set_word_at(m_, second, global_word(m_, kHeaderWords));
        set_word_at(m_, prim + 8, global_word(m_, mathkit::gte::kScreen));
        set_word_at(m_, prim + 12, global_word(m_, mathkit::gte::kScreen + 8));
        set_word_at(m_, prim + 16, global_word(m_, mathkit::gte::kScreen + 4));
        const Word bucket = static_cast<Word>(global_int(m_, mathkit::gte::kMac + 8) >> 2) & 0xfff;
        bucket_link(prim, 5, bucket);
        set_word_at(m_, prim + 0x24, 0);
        bucket_link(second, 2, bucket);
    }
    return true;
}

// the line bits: two points, a line primitive and its mode primitive
bool BitKit::display_glines(Word camera) {
    for (Word line = global_word(m_, lists::kGLine); line != 0; line = word_at(m_, line + bit::kNext)) {
        project(line + 0x44, 12, camera);
        const std::int32_t z1 = global_int(m_, mathkit::gte::kMac + 8);
        const Word s1 = global_word(m_, mathkit::gte::kScreen + 4);
        if (!z_in_range(z1)) {
            continue;
        }
        project(line + 0x50, 12, camera);
        const std::int32_t z2 = global_int(m_, mathkit::gte::kMac + 8);
        const Word s2 = global_word(m_, mathkit::gte::kScreen + 4);
        if (!z_in_range(z2)) {
            continue;
        }
        const Word prim = arena_take(0x18);
        if (prim == 0) {
            return false;
        }
        set_word_at(m_, prim, 0x4000000);
        set_word_at(m_, prim + 4, word_at(m_, line + 0x3c));
        set_word_at(m_, prim + 12, word_at(m_, line + 0x40));
        set_word_at(m_, prim + 8, s1);
        set_word_at(m_, prim + 16, s2);
        const Word bucket = static_cast<Word>((z2 < z1 ? z2 : z1) >> 2) & 0xfff;
        bucket_link(prim, 5, bucket);
        const Word cursor = global_word(m_, kArenaCursor);
        set_global_word(m_, kArenaCursor, cursor + 0x10);
        set_word_at(m_, cursor + 4, 0xe1000720);
        set_word_at(m_, cursor + 12, 0);
        bucket_link(cursor, 2, bucket);
    }
    return true;
}

// the quads: four corners; the primitive is dropped (the cursor restored) when a corner is out of the depth range
bool BitKit::display_quads(Word camera) {
    for (Word quad = global_word(m_, lists::kQuad); quad != 0; quad = word_at(m_, quad + bit::kNext)) {
        const Word saved = global_word(m_, kArenaCursor);
        if (word_at(m_, quad + 0x64) == 0) {
            continue;
        }
        project(quad + 0x10, 12, camera);
        std::int32_t depth = global_int(m_, mathkit::gte::kMac + 8);
        const Word s0 = global_word(m_, mathkit::gte::kScreen + 4);
        const Word texture = word_at(m_, quad + 0x64);
        if (saved + 0x30 > global_word(m_, kArenaEnd)) {
            return false;
        }
        if (!z_in_range(depth)) {
            continue;
        }
        const Word prim = saved;
        set_global_word(m_, kArenaCursor, saved + 0x30);
        set_word_at(m_, prim + 8, s0);
        project(quad + 0x3c, 12, camera);
        const std::int32_t z1 = global_int(m_, mathkit::gte::kMac + 8);
        if (!z_in_range(z1)) {
            set_global_word(m_, kArenaCursor, saved);
            continue;
        }
        set_word_at(m_, prim + 16, global_word(m_, mathkit::gte::kScreen + 4));
        project(quad + 0x48, 12, camera);
        set_word_at(m_, prim, 0x9000000);
        set_word_at(m_, prim + 4, word_at(m_, quad + 0x60));
        if (depth < z1) {
            depth = z1;
        }
        const std::int32_t z2 = global_int(m_, mathkit::gte::kMac + 8);
        if (!z_in_range(z2)) {
            set_global_word(m_, kArenaCursor, saved);
            continue;
        }
        set_word_at(m_, prim + 24, global_word(m_, mathkit::gte::kScreen + 4));
        project(quad + 0x54, 12, camera);
        set_word_at(m_, prim + 20, word_at(m_, texture + 4));
        set_word_at(m_, prim + 12, word_at(m_, texture));
        set_word_at(m_, prim + 28, word_at(m_, texture + 8));
        set_half_at(m_, prim + 36, half_at(m_, texture + 10));
        if ((byte_at(m_, quad + 0x68) & 4) != 0) {
            set_half_at(m_, prim + 0x16, (half_at(m_, prim + 0x16) & 0xffdf) | 0x40);
        }
        const std::int32_t z3 = global_int(m_, mathkit::gte::kMac + 8);
        if (depth < z2) {
            depth = z2;
        }
        if (!z_in_range(z3)) {
            set_global_word(m_, kArenaCursor, saved);
            continue;
        }
        set_word_at(m_, prim + 32, global_word(m_, mathkit::gte::kScreen + 4));
        if (depth < z3) {
            depth = z3;
        }
        set_word_at(m_, prim + 44, texture);
        bucket_link(prim, 10, depth_bucket(depth, half_at(m_, quad + 0x38)));
    }
    return true;
}

// the pixels (sparks): one or a few points of a primitive each
bool BitKit::display_pixels(Word camera) {
    for (Word pixel = global_word(m_, lists::kPixel); pixel != 0; pixel = word_at(m_, pixel + bit::kNext)) {
        project(pixel + 0x10, 12, camera);
        const Word prim = global_word(m_, kArenaCursor);
        if ((byte_at(m_, pixel + 0x44) & 0xf) == 1) {
            set_half_at(m_, prim + 0xe, 1);
            set_half_at(m_, prim + 0xc, 1);
            set_half_at(m_, prim + 0xc, global_word(m_, kScreenWidth) >> 9);
            set_half_at(m_, prim + 0xe, global_word(m_, kScreenHeight) / 0xf0);
            if (prim + 0x14 > global_word(m_, kArenaEnd)) {
                return false;
            }
            set_global_word(m_, kArenaCursor, prim + 0x14);
            set_word_at(m_, prim, word_at(m_, pixel + 0x3c));
            set_word_at(m_, prim + 4, word_at(m_, pixel + 0x40));
            const std::int32_t z = global_int(m_, mathkit::gte::kMac + 8);
            if (z_in_range(z)) {
                set_word_at(m_, prim + 8, global_word(m_, mathkit::gte::kScreen + 4));
                bucket_link(prim, 4, depth_bucket(z, half_at(m_, pixel + 0x38)));
            } else {
                set_global_word(m_, kArenaCursor, prim);
            }
        } else {
            if (prim + 0x14 > global_word(m_, kArenaEnd)) {
                return false;
            }
            set_global_word(m_, kArenaCursor, prim + 0x14);
            set_word_at(m_, prim, word_at(m_, pixel + 0x3c));
            set_word_at(m_, prim + 4, word_at(m_, pixel + 0x40));
            set_word_at(m_, prim + 12, word_at(m_, pixel + 0x44));
            const std::int32_t z = global_int(m_, mathkit::gte::kMac + 8);
            if (z_in_range(z)) {
                set_word_at(m_, prim + 8, global_word(m_, mathkit::gte::kScreen + 4));
                bucket_link(prim, 4, depth_bucket(z, half_at(m_, pixel + 0x38)));
            } else {
                set_global_word(m_, kArenaCursor, prim);
            }
        }
    }
    return true;
}

// the chunks: four corners (whole-unit world coordinates), a mode primitive each side of up to four triangles that face the camera
bool BitKit::display_chunks(Word camera) {
    for (Word chunk = global_word(m_, lists::kChunk); chunk != 0; chunk = word_at(m_, chunk + bit::kNext)) {
        const Word saved = global_word(m_, kArenaCursor);
        if (saved + 0xc0 > global_word(m_, kArenaEnd)) {
            return false;
        }
        project(chunk + 0x5c, 0, camera);
        std::int32_t depth = global_int(m_, mathkit::gte::kMac + 8);
        const std::int32_t z0 = depth;
        const Word s0 = global_word(m_, mathkit::gte::kScreen + 4);
        project(chunk + 0x68, 0, camera);
        const std::int32_t z1 = global_int(m_, mathkit::gte::kMac + 8);
        const Word s1 = global_word(m_, mathkit::gte::kScreen + 4);
        if (!(z_in_range(z0))) {
            continue;
        }
        std::int32_t furthest = depth >= 0 ? depth : -1;
        project(chunk + 0x74, 0, camera);
        const std::int32_t z2 = global_int(m_, mathkit::gte::kMac + 8);
        if (!z_in_range(z1)) {
            continue;
        }
        if (furthest < z1) {
            furthest = z1;
        }
        const Word s2 = global_word(m_, mathkit::gte::kScreen + 4);
        project(chunk + 0x80, 0, camera);
        const std::int32_t z3 = global_int(m_, mathkit::gte::kMac + 8);
        if (!z_in_range(z2)) {
            continue;
        }
        if (furthest < z2) {
            furthest = z2;
        }
        if (!z_in_range(z3)) {
            continue;
        }
        if (furthest < z3) {
            furthest = z3;
        }
        const Word s3 = global_word(m_, mathkit::gte::kScreen + 4);
        Word first_mode = 0;
        Word second_mode = 0;
        Word cursor = saved;
        if (word_at(m_, chunk + 0xb0) != 0) {
            first_mode = cursor;
            second_mode = cursor + 0x10;
            cursor += 0x20;
            set_global_word(m_, kArenaCursor, cursor);
            set_word_at(m_, first_mode + 8, 0);
            set_word_at(m_, first_mode + 4, 0xe2000000);
            set_word_at(m_, second_mode + 8, 0);
            set_word_at(m_, second_mode + 4, 0xe2000000);
            set_word_at(m_, second_mode + 4, word_at(m_, chunk + 0xb0));
        }
        const Word bucket = static_cast<Word>(furthest >> 2) & 0xfff;
        if (word_at(m_, chunk + 0xb0) != 0) {
            bucket_link(first_mode, 3, bucket);
        }
        cursor = global_word(m_, kArenaCursor);
        const Word s[4] = {s0, s1, s2, s3};
        // (the four faces, in the retail code's order; the registers hold the corners as written at 0x006a3e04, 0x006a3e0c, 0x006a3e08)
        const auto face = [&](Word colour_at, int sx0, int sx2, int sx1, int p2, int p4, int p6) {
            set_global_word(m_, mathkit::gte::kScreen, s[sx0]);
            set_global_word(m_, mathkit::gte::kScreen + 8, s[sx2]);
            set_global_word(m_, mathkit::gte::kScreen + 4, s[sx1]);
            math().nclip();
            if (global_int(m_, mathkit::gte::kMac0) < 1) {
                set_word_at(m_, cursor, 0x7000000);
                set_word_at(m_, cursor + 4, word_at(m_, chunk + colour_at) | 0x24000000);
                set_word_at(m_, cursor + 12, word_at(m_, chunk + 0x94));
                set_word_at(m_, cursor + 20, word_at(m_, chunk + 0x98));
                set_half_at(m_, cursor + 28, half_at(m_, chunk + 0x9c));
                set_word_at(m_, cursor + 24, s[p6]);
                set_word_at(m_, cursor + 8, s[p2]);
                set_word_at(m_, cursor + 16, s[p4]);
                set_word_at(m_, cursor + 36, word_at(m_, chunk + 0xb4));
                bucket_link(cursor, 8, bucket);
                cursor += 40;
            }
        };
        face(0xa0, 0, 1, 2, 0, 1, 2);
        face(0xa4, 0, 3, 1, 0, 3, 1);
        face(0xa8, 1, 3, 2, 1, 3, 2);
        face(0xac, 2, 3, 0, 2, 3, 0);
        set_global_word(m_, kArenaCursor, cursor);
        if (word_at(m_, chunk + 0xb0) != 0) {
            bucket_link(second_mode, 3, bucket);
        }
    }
    return true;
}

// the glows: a fan of sections around the projected centre, each section a band of quads out to the fringes; the points of two neighbouring spokes are kept in two buffers that swap
bool BitKit::display_glows(Word camera, Word half) {
    constexpr Word kCount = kScratch + 0x1040;
    constexpr Word kShifted = kScratch + 0x1050;
    constexpr Word kDifference = kScratch + 0x1060;
    const auto clamp_screen = [&](std::int32_t v, Word limit) { return std::max(-100, std::min(v, static_cast<std::int32_t>(limit) + 100)); };
    for (Word glow = global_word(m_, lists::kGlow); glow != 0; glow = word_at(m_, glow + bit::kNext)) {
        set_global_word(m_, kCount, 12);
        core_.vector_shift_right_out(kShifted, glow + bit::kPosition, kCount);
        core_.vector_subtract_out(kDifference, kShifted, camera);
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, mathkit::gte::kV0 + 2 * k, word_at(m_, kDifference + 4 * k));
        }
        math().rtps();
        const Word header = arena_take(0x10);
        if (header == 0) {
            return false;
        }
        set_word_at(m_, header, global_word(m_, kHeaderWords));
        set_word_at(m_, header + 4, global_word(m_, kHeaderWords + 4));
        const std::int32_t sections_count = int_at(m_, glow + 0x44);
        const std::int32_t step = short_at(m_, glow + 0x50);
        std::int32_t angle = short_at(m_, glow + 0x54);
        const Word mac1 = global_word(m_, mathkit::gte::kMac);
        const std::int32_t z = global_int(m_, mathkit::gte::kMac + 8);
        Word sections = word_at(m_, glow + 0x3c);
        if (!(z > 0 && z <= 0x4e20)) {
            continue;
        }
        Word alpha = 0;
        if (z < 700) {
            alpha = static_cast<Word>((z * 255) / 700 + 1);
        }
        const Word viewport = global_word(m_, kViewport);
        std::int32_t sx, sy;
        if (z < static_cast<std::int32_t>(half)) {
            const std::int32_t scale = half_at(m_, viewport + 0xe);
            const std::int32_t qx = (scale * static_cast<std::int32_t>(mac1)) / z;
            auto xs = static_cast<std::int16_t>(qx + short_at(m_, viewport + 0x10));
            sx = xs > 0x3ff ? 0x3ff : (xs < -0x400 ? -0x400 : xs);
            const std::int32_t qy = (scale * global_int(m_, mathkit::gte::kMac + 4)) / z;
            auto ys = static_cast<std::int16_t>(qy + short_at(m_, viewport + 0x12));
            sy = ys > 0x3ff ? 0x3ff : (ys < -0x400 ? -0x400 : ys);
        } else {
            const Word packed = global_word(m_, mathkit::gte::kScreen + 4);
            sx = static_cast<std::int16_t>(packed & 0xffff);
            sy = static_cast<std::int16_t>(packed >> 16);
        }
        if (sx == -0x400 || sx == 0x3ff || sy == -0x400 || sy == 0x3ff) {
            continue;
        }
        const Word bucket = static_cast<Word>(z >> 2) & 0xfff;
        if (global_word(m_, kSoftwareRenderer) != 0) {
            set_word_at(m_, header + 12, 0);
            bucket_link(header, 2, bucket);
        }
        const Word fringes = word_at(m_, glow + 0x48);
        const Word fringe_base = word_at(m_, glow + 0x40);
        const Word width = global_word(m_, kScreenWidth);
        const Word height = global_word(m_, kScreenHeight);
        // the first spoke: the outermost section, then each fringe outward
        const Word first_radius = word_at(m_, sections + static_cast<Word>(sections_count) * 8 - 8);
        const Word r0 = (first_radius * 400u) / static_cast<Word>(z);
        const auto trig = [&](std::int32_t a, std::int32_t& sin400, std::int32_t& cosine) {
            const std::int32_t s = math().sin_q12(a);
            const std::int32_t product = s * 400;
            sin400 = (product + ((product >> 31) & 0xff)) >> 8;
            cosine = math().cos_q12(a);
        };
        const auto place = [&](std::int32_t radius, std::int32_t sin400, std::int32_t cosine, std::int32_t& x, std::int32_t& y) {
            const std::int32_t px = sin400 * radius;
            x = ((px + ((px >> 31) & 0xfff)) >> 12) + sx;
            const std::int32_t py = cosine * radius;
            y = sy - ((py + ((py >> 31) & 0xfff)) >> 12);
            x = clamp_screen(x, width);
            y = clamp_screen(y, height);
        };
        std::int32_t sin400, cosine;
        trig(angle, sin400, cosine);
        std::int32_t x, y;
        place(static_cast<std::int32_t>(r0), sin400, cosine, x, y);
        Word previous = kClipPoints;
        Word current = kClipPoints + 4 + 4 * fringes;
        set_half_at(m_, previous, static_cast<Word>(x));
        set_half_at(m_, previous + 2, static_cast<Word>(y));
        {
            std::int32_t radius = int_at(m_, sections + static_cast<Word>(sections_count) * 8 - 8);
            Word fringe = fringe_base + static_cast<Word>(sections_count) * 8 - 8;
            for (Word k = 0; k < fringes; ++k) {
                radius += int_at(m_, fringe);
                const std::int32_t q = (radius * 400) / z;
                place(q, sin400, cosine, x, y);
                set_half_at(m_, previous + 4 + 4 * k, static_cast<Word>(x));
                set_half_at(m_, previous + 6 + 4 * k, static_cast<Word>(y));
                fringe += static_cast<Word>(sections_count) * 8;
            }
        }
        angle += step;
        for (std::int32_t spoke = 0; spoke < sections_count; ++spoke) {
            const Word lit = (word_at(m_, glow + 0x58) >> (static_cast<Word>(spoke) & 31)) & 1;
            if (spoke == sections_count - 1) {
                angle = short_at(m_, glow + 0x54);
            }
            const Word radius_word = word_at(m_, sections);
            const Word ru = (radius_word * 400u) / static_cast<Word>(z);
            trig(angle, sin400, cosine);
            place(static_cast<std::int32_t>(ru), sin400, cosine, x, y);
            set_half_at(m_, current, static_cast<Word>(x));
            set_half_at(m_, current + 2, static_cast<Word>(y));
            if (byte_at(m_, glow + 0x52) == 0 && lit != 0) {
                const Word tri = arena_take(0x20);
                if (tri == 0) {
                    return false;
                }
                set_word_at(m_, tri, 0x6000000);
                set_word_at(m_, tri + 4, word_at(m_, glow + 0x4c));
                set_word_at(m_, tri + 12, word_at(m_, sections + 4));
                set_word_at(m_, tri + 20, word_at(m_, sections + 4));
                if (alpha != 0) {
                    for (const Word at : {4u, 12u, 20u}) {
                        scale_colour(tri + at, alpha);
                    }
                }
                set_half_at(m_, tri + 8, static_cast<Word>(sx));
                set_half_at(m_, tri + 10, static_cast<Word>(sy));
                set_half_at(m_, tri + 0x10, half_at(m_, previous));
                set_half_at(m_, tri + 0x12, half_at(m_, previous + 2));
                set_half_at(m_, tri + 0x18, half_at(m_, current));
                set_half_at(m_, tri + 0x1a, half_at(m_, current + 2));
                bucket_link(tri, 7, bucket);
            }
            Word fringe = fringe_base + static_cast<Word>(spoke) * 8;
            std::int32_t radius = int_at(m_, sections);
            for (Word k = 0; k < fringes; ++k) {
                radius += int_at(m_, fringe);
                const std::int32_t q = (radius * 400) / z;
                place(q, sin400, cosine, x, y);
                set_half_at(m_, current + 4 + 4 * k, static_cast<Word>(x));
                set_half_at(m_, current + 6 + 4 * k, static_cast<Word>(y));
                if (lit != 0) {
                    const Word quad = arena_take(0x28);
                    if (quad == 0) {
                        return false;
                    }
                    set_word_at(m_, quad, 0x8000000);
                    const Word colour = word_at(m_, fringe + 4);
                    set_word_at(m_, quad + 4, colour);
                    set_word_at(m_, quad + 12, colour);
                    Word inner;
                    if (k == 0) {
                        inner = word_at(m_, sections + 4);
                    } else {
                        inner = word_at(m_, fringe - static_cast<Word>(sections_count) * 8 + 4);
                    }
                    set_word_at(m_, quad + 20, inner);
                    set_word_at(m_, quad + 28, inner);
                    if (alpha != 0) {
                        for (const Word at : {4u, 12u, 20u, 28u}) {
                            scale_colour(quad + at, alpha);
                        }
                    }
                    set_half_at(m_, quad + 0x18, half_at(m_, previous + 4 * k));
                    set_half_at(m_, quad + 0x1a, half_at(m_, previous + 4 * k + 2));
                    set_half_at(m_, quad + 0x20, half_at(m_, current + 4 * k));
                    set_half_at(m_, quad + 0x22, half_at(m_, current + 4 * k + 2));
                    set_half_at(m_, quad + 8, half_at(m_, previous + 4 * k + 4));
                    set_half_at(m_, quad + 10, half_at(m_, previous + 4 * k + 6));
                    set_half_at(m_, quad + 0x10, half_at(m_, current + 4 * k + 4));
                    set_half_at(m_, quad + 0x12, half_at(m_, current + 4 * k + 6));
                    bucket_link(quad, 9, bucket);
                }
                fringe += static_cast<Word>(sections_count) * 8;
            }
            std::swap(previous, current);
            sections += 8;
            angle += step;
        }
        if (global_word(m_, kSoftwareRenderer) == 0) {
            set_word_at(m_, header + 12, 0);
            bucket_link(header, 2, bucket);
        }
    }
    (void)half;
    return true;
}

// the flat bits: a sprite (a quad built from the current animation frame), turned about the screen point by its angle and scaled by its depth
bool BitKit::display_flats(Word camera) {
    const Word half = static_cast<Word>(half_at(m_, global_word(m_, kViewport) + 0xe)) >> 1;
    for (Word bit_at = global_word(m_, lists::kFlat); bit_at != 0; bit_at = word_at(m_, bit_at + bit::kNext)) {
        project(bit_at + bit::kPosition, 12, camera);
        const Word prim = global_word(m_, kArenaCursor);
        if (prim + 0x30 > global_word(m_, kArenaEnd)) {
            return false;
        }
        set_global_word(m_, kArenaCursor, prim + 0x30);
        set_word_at(m_, prim, 0x9000000);
        set_word_at(m_, prim + 4, word_at(m_, bit_at + bit::kColour));
        const std::int32_t z = global_int(m_, mathkit::gte::kMac + 8);
        const auto drop = [&]() { set_global_word(m_, kArenaCursor, prim); };
        if (z < static_cast<std::int32_t>(half) || z > static_cast<std::int32_t>(half_at(m_, global_word(m_, kViewport) + 10))) {
            drop();
            continue;
        }
        const Word scale = (static_cast<Word>(static_cast<std::int32_t>(short_at(m_, bit_at + 0x52)) << 7)) / static_cast<Word>(z);
        if (scale > 0x200 && (byte_at(m_, bit_at + bit::kFlags) & 8) == 0) {
            drop();
            continue;
        }
        const Word centre = global_word(m_, mathkit::gte::kScreen + 4);
        const Word entry = word_at(m_, bit_at + bit::kFrameEntry);
        const Word record = word_at(m_, entry + 4);
        const Word first = word_at(m_, record);
        const Word second = word_at(m_, record + 4);
        const Word third = word_at(m_, record + 8);
        const Word clut = half_at(m_, record + 10);
        const Word high = half_at(m_, bit_at + 0x62);
        set_word_at(m_, prim + 12, high != 0 ? ((high << 16) | (first & 0xffff)) : first);
        set_half_at(m_, prim + 0x24, clut);
        set_word_at(m_, prim + 0x1c, third);
        set_word_at(m_, prim + 0x14, (static_cast<Word>(byte_at(m_, bit_at + 0x61)) << 16) | (second & 0xff9fffffu));
        const Word angle16 = half_at(m_, bit_at + 0x54);
        if (scale >= 0x100 || angle16 != 0) {
            for (const Word at : {0x14u, 0x1du, 0x24u, 0x25u}) {
                check(byte_at(m_, prim + at) != 0, 0x00524958);
                set_byte_at(m_, prim + at, byte_at(m_, prim + at) - 1);
            }
        }
        const Word flip = byte_at(m_, bit_at + bit::kFlags) & 1;
        const std::int32_t offset_x = static_cast<std::int8_t>(byte_at(m_, entry));
        const std::int32_t offset_y = static_cast<std::int8_t>(byte_at(m_, entry + 1));
        const Word width = byte_at(m_, entry + 2);
        const Word height = byte_at(m_, entry + 3);
        const Word post_x = word_at(m_, bit_at + 0x5c) & 0xffff;
        const Word post_y = word_at(m_, bit_at + 0x5c) >> 16;
        if (angle16 == 0) {
            Word x = (static_cast<Word>(offset_y) * scale & 0x7fff80u) << 9;
            x += centre;
            x += static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int16_t>((static_cast<Word>(offset_x) * scale) >> 7)));
            Word w = (width * scale) >> 7;
            Word h = ((height * scale) & 0xffffff80u) << 9;
            if (w < 5) {
                w = 2;
            }
            if (h < 0x50000) {
                h = 0x10000;
            }
            if (flip == 0) {
                set_word_at(m_, prim + 8, x);
                set_word_at(m_, prim + 0x10, w + x);
                set_word_at(m_, prim + 0x18, h + x);
                set_word_at(m_, prim + 0x20, h + w + x);
            } else {
                set_word_at(m_, prim + 0x10, x);
                set_word_at(m_, prim + 8, w + x);
                set_word_at(m_, prim + 0x20, h + x);
                set_word_at(m_, prim + 0x18, h + w + x);
            }
        } else {
            const std::int32_t angle = static_cast<std::int16_t>(angle16);
            const Word sn = static_cast<Word>(math().sin_q12(angle));
            const Word cs = static_cast<Word>(math().cos_q12(angle));
            const Word oy = static_cast<Word>(offset_y), ox = static_cast<Word>(offset_x);
            const Word sin_oy = sn * oy, sin_ox = sn * ox, cos_ox = cs * ox, cos_oy = cs * oy;
            const Word sin_h = sn * height + sin_oy;
            const Word cos_h = cs * height + cos_oy;
            const Word ax = (post_x * scale) >> 12;
            const Word ay = (post_y * scale) >> 12;
            const Word sin_w = sn * width + sin_ox;
            const Word cos_w = cs * width + cos_ox;
            const auto corner = [&](Word horizontal, Word vertical_base, Word vertical_shift) {
                return (((horizontal - vertical_base) * ax) >> 19) + centre + ((((vertical_shift) * ay) >> 3) & 0x1fff0000u);
            };
            const Word a = corner(cos_ox, sin_oy, cos_oy + sin_ox);
            const Word b = corner(cos_w, sin_oy, cos_oy + sin_w);
            const Word c = corner(cos_ox, sin_h, cos_h + sin_ox);
            const Word d = corner(cos_w, sin_h, cos_h + sin_w);
            if (flip == 0) {
                set_word_at(m_, prim + 8, a);
                set_word_at(m_, prim + 0x10, b);
                set_word_at(m_, prim + 0x18, c);
                set_word_at(m_, prim + 0x20, d);
            } else {
                set_word_at(m_, prim + 0x10, a);
                set_word_at(m_, prim + 8, b);
                set_word_at(m_, prim + 0x20, c);
                set_word_at(m_, prim + 0x18, d);
            }
        }
        set_word_at(m_, prim + 0x2c, record);
        const Word priority = byte_at(m_, bit_at + 0x60);
        bucket_link(prim, 10, priority != 0 ? priority : depth_bucket(z, half_at(m_, bit_at + 0x38)));
    }
    return true;
}

// the linked two-ended bits (ribbons): each bit is a quad between its two projected points, the width following the bits' sprite widths; a strip starts at a bit flagged 0x10 (or after a bit that was
// dropped) and each following bit continues it from the far edge of the one before
bool BitKit::display_linked(Word camera) {
    std::int32_t saved_x = 0, saved_y = 0;                            // the previous bit's second point on the screen
    std::int32_t carry_x2 = 0, carry_y2 = 0, carry_x1 = 0, carry_y1 = 0;      // its far edge, which this bit's near edge continues
    std::int32_t snap_x2 = 0, snap_y2 = 0, snap_x1 = 0, snap_y1 = 0;          // the near edge of the strip's first bit
    std::int32_t near_half = 0;
    bool broken = false;
    const auto lower16 = [](std::int32_t v) { return static_cast<Word>(v) & 0xffff; };
    for (Word link = global_word(m_, lists::kLinked2Ended); link != 0; link = word_at(m_, link + bit::kNext)) {
        const Word flags = byte_at(m_, link + bit::kFlags);
        std::int32_t x2, y2;
        Word wbyte, record;
        bool strip_start;
        if (!((flags & 0x10) != 0 || broken)) {
            const Word entry = word_at(m_, link + bit::kFrameEntry);
            wbyte = byte_at(m_, entry + 2);
            record = word_at(m_, entry + 4);
            strip_start = false;
            x2 = saved_x;
            y2 = saved_y;
        } else {
            strip_start = true;
            project(link + 0x54, 12, camera);
            const Word entry = word_at(m_, link + bit::kFrameEntry);
            const std::int32_t z = global_int(m_, mathkit::gte::kMac + 8);
            wbyte = byte_at(m_, entry + 2);
            record = word_at(m_, entry + 4);
            if (!z_in_range(z)) {
                broken = true;
                continue;
            }
            const Word packed = global_word(m_, mathkit::gte::kScreen + 4);
            y2 = static_cast<std::int32_t>(packed) >> 16;
            x2 = static_cast<std::int16_t>(packed & 0xffff);
            const Word product = static_cast<Word>(static_cast<std::int32_t>(short_at(m_, link + 0x52)) * static_cast<std::int32_t>(wbyte));
            Word half = (product / static_cast<Word>(z)) >> 1;
            near_half = static_cast<std::int32_t>(half) >= 2 ? static_cast<std::int32_t>(half) : 2;
        }
        project(link + 0x60, 12, camera);
        const Word prim = global_word(m_, kArenaCursor);
        if (prim + 0x30 > global_word(m_, kArenaEnd)) {
            return false;
        }
        set_global_word(m_, kArenaCursor, prim + 0x30);
        set_word_at(m_, prim, 0x9000000);
        set_word_at(m_, prim + 4, word_at(m_, link + 0x40));
        set_word_at(m_, prim + 12, word_at(m_, record));
        set_word_at(m_, prim + 20, word_at(m_, record + 4));
        set_word_at(m_, prim + 28, word_at(m_, record + 8));
        set_half_at(m_, prim + 0x24, half_at(m_, record + 10));
        for (const Word at : {0x14u, 0x1du, 0x24u, 0x25u}) {
            set_byte_at(m_, prim + at, byte_at(m_, prim + at) - 1);
        }
        const std::int32_t z3 = global_int(m_, mathkit::gte::kMac + 8);
        if (!z_in_range(z3)) {
            set_global_word(m_, kArenaCursor, prim);
            broken = true;
            continue;
        }
        const Word packed3 = global_word(m_, mathkit::gte::kScreen + 4);
        const std::int32_t y3 = static_cast<std::int32_t>(packed3) >> 16;
        const std::int32_t x3 = static_cast<std::int16_t>(packed3 & 0xffff);
        saved_y = y3;
        saved_x = x3;
        const Word product3 = static_cast<Word>(static_cast<std::int32_t>(short_at(m_, link + 0x52)) * static_cast<std::int32_t>(wbyte));
        std::int32_t far_half = static_cast<std::int32_t>((product3 / static_cast<Word>(z3)) >> 1);
        if (far_half < 2) {
            far_half = 2;
        }
        const std::int32_t dy = y3 - y2;
        const std::int32_t dx = x3 - x2;
        const Word length = sqrt_truncated(static_cast<Word>(dx * dx + dy * dy));
        if (length == 0) {
            set_global_word(m_, kArenaCursor, prim);
            broken = true;
            continue;
        }
        const std::int32_t len = static_cast<std::int32_t>(length);
        std::int32_t n_x2, n_x1, n_y2, n_y1;
        if (strip_start) {
            const std::int32_t a = (dy * near_half) / len;
            const std::int32_t b = (dx * near_half) / len;
            n_x2 = a + x2;
            n_x1 = x2 - a;
            n_y2 = y2 - b;
            n_y1 = y2 + b;
        } else {
            n_x2 = carry_x2;
            n_x1 = carry_x1;
            n_y2 = carry_y2;
            n_y1 = carry_y1;
        }
        if ((flags & 0x10) != 0) {
            snap_y2 = n_y2;
            snap_x2 = n_x2;
            snap_x1 = n_x1;
            snap_y1 = n_y1;
        }
        std::int32_t f_x2, f_y2, f_x1, f_y1;
        if ((flags & 0x20) != 0) {
            f_x2 = snap_x2;
            f_y2 = snap_y2;
            f_x1 = snap_x1;
            f_y1 = snap_y1;
        } else {
            const std::int32_t c = (dy * far_half) / len;
            const std::int32_t d = (dx * far_half) / len;
            f_x2 = x3 + c;
            f_x1 = x3 - c;
            f_y2 = y3 - d;
            f_y1 = d + y3;
        }
        carry_x2 = f_x2;
        carry_y2 = f_y2;
        carry_x1 = f_x1;
        carry_y1 = f_y1;
        const auto put = [&](Word at, std::int32_t v) { set_half_at(m_, prim + at, lower16(v)); };
        if ((flags & 2) != 0) {
            if ((flags & 1) == 0) {
                put(0x08, f_x1); put(0x10, f_x2); put(0x0a, f_y1); put(0x12, f_y2);
                put(0x18, n_x1); put(0x20, n_x2); put(0x1a, n_y1); put(0x22, n_y2);
            } else {
                put(0x08, f_x2); put(0x10, f_x1); put(0x0a, f_y2); put(0x12, f_y1);
                put(0x18, n_x2); put(0x20, n_x1); put(0x1a, n_y2); put(0x22, n_y1);
            }
        } else {
            if ((flags & 1) == 0) {
                put(0x08, n_x2); put(0x10, f_x2); put(0x0a, n_y2); put(0x12, f_y2);
                put(0x18, n_x1); put(0x20, f_x1); put(0x1a, n_y1); put(0x22, f_y1);
            } else {
                put(0x08, n_x1); put(0x10, f_x1); put(0x0a, n_y1); put(0x12, f_y1);
                put(0x18, n_x2); put(0x20, f_x2); put(0x1a, n_y2); put(0x22, f_y2);
            }
        }
        set_word_at(m_, prim + 0x2c, record);
        bucket_link(prim, 10, depth_bucket(z3, half_at(m_, link + 0x38)));
        broken = false;
    }
    return true;
}


}  // namespace opentony::runtime::bitkit
