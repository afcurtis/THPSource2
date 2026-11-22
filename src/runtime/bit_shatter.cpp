#include "bit_views.hpp"

// Phase 4.15d: the shatter of an object into pieces (shatter.cpp, 0x004ae090..0x004af660) and the effect object each piece becomes (the emitter at 0x004add10..0x004ae020, a chunk bit with a vtable of
// its own). A face of a model is transformed through the GTE, its colour found, and either cut into pieces (the triangle halved at a random point of its longest edge, down to a depth) or sprayed as
// shards of glass; each piece is an emitter given the model's colours and a velocity away from the shatter's centre.

namespace opentony::runtime::bitkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kModelTables = 0x0056d43c;          // per PSX region (0x44 bytes each): the table of the region's models
constexpr Word kRgbTables = 0x0056d450;            // per region: the table of the region's colours
constexpr Word kNormal = 0x0056c510;               // three shorts: the face's normal
constexpr Word kTexturePage = 0x0056c518;          // dword
constexpr Word kCentre = 0x0056c520;               // three dwords: the face's centre
constexpr Word kColourBytes = 0x0056c53c;          // three bytes: green, blue, red of the face (c53c, c53d, c53e)
constexpr Word kTextureClut = 0x0056c540;          // dword
constexpr Word kNearest = 0x0056c530;              // three dwords: the nearest glass burst to the camera
constexpr Word kBurstSeen = 0x0056c544;            // dword
constexpr Word kShatterOff = 0x0056a98c;           // dword: shattering is switched off
constexpr Word kGlassOff = 0x0056e770;             // dword: no glass
constexpr Word kSpriteFlag = 0x005244cc;
constexpr Word kViewIndex = 0x0055fa3c;
constexpr Word kViews = 0x0055f7cc;
constexpr Word kViewStride = 0xa4;
constexpr Word kPlayWithSound = 0x004ad620;        // SFX_PlayX(id, volume, pan)
constexpr Word kEmitterVtable = 0x0051972c;
constexpr Word kWindOrigin = 0x0056c520;

}  // namespace

// 0x004aeef0 (cdecl): a vertex of the face through the GTE (RT, TR) into whole units shifted back to Q12
void BitKit::shatter_corner(Word out, Word vertices, Word indices, Word which) {
    const Word vertex = vertices + static_cast<Word>(byte_at(m_, indices + which)) * 8;
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, mathkit::gte::kV0 + 2 * k, half_at(m_, vertex + 2 * k));
    }
    math().rtv0tr();
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, out + 4 * k, word_at(m_, mathkit::gte::kMac + 4 * k) << 12);
    }
}

// 0x004aef70 (cdecl): the face's colour in the three bytes at 0x0056c53c: the packed colour itself, or the mean of the table's colours it indexes (three or four of them)
void BitKit::shatter_colour(Word indexed, Word packed, Word count, Word table) {
    if (indexed == 0) {
        set_global_byte(m_, kColourBytes + 2, packed);
        set_global_byte(m_, kColourBytes, packed >> 8);
        set_global_byte(m_, kColourBytes + 1, packed >> 16);
        return;
    }
    const Word first = word_at(m_, table + (packed & 0xff) * 4);
    const Word second = word_at(m_, table + ((packed >> 8) & 0xff) * 4);
    const Word third = word_at(m_, table + ((packed >> 16) & 0xff) * 4);
    const Word green = ((third >> 8) & 0xff) + ((second >> 8) & 0xff) + ((first >> 8) & 0xff);
    const Word red = (third & 0xff) + (second & 0xff) + (first & 0xff);
    const Word blue = ((third >> 16) & 0xff) + ((second >> 16) & 0xff) + ((first >> 16) & 0xff);
    if (count == 4) {
        const Word fourth = word_at(m_, table + (packed >> 24) * 4);
        set_global_byte(m_, kColourBytes + 2, ((fourth & 0xff) + red) >> 2);
        set_global_byte(m_, kColourBytes, (((fourth >> 8) & 0xff) + green) >> 2);
        set_global_byte(m_, kColourBytes + 1, (((fourth >> 16) & 0xff) + blue) >> 2);
        return;
    }
    set_global_byte(m_, kColourBytes + 2, red / 3);
    set_global_byte(m_, kColourBytes, green / 3);
    set_global_byte(m_, kColourBytes + 1, blue / 3);
}

// 0x004ae090 Shatter_Face(item, face, depth, ?, only marked, clear, quota): returns 0 (not shattered), 1 (pieces) or 2 (glass)
Word BitKit::shatter_face(Word item, Word face, Word depth, Word unused, Word only_marked, Word clear_mark, Word quota) {
    (void)unused;
    check(item != 0, 0x00537b14);
    check(face != 0, 0x00537b08);
    const Word indices = face + 4;
    const Word region = byte_at(m_, item + 0x1f);
    const Word model = word_at(m_, word_at(m_, kModelTables + region * 0x44) + static_cast<Word>(half_at(m_, item + 0x1a)) * 4);
    const Word vertices = model + 0x1c;
    const Word normals = vertices + static_cast<Word>(half_at(m_, model + 2)) * 8;
    if (only_marked != 0) {
        if ((half_at(m_, face + 0xe) & 8) == 0) {
            return 0;
        }
        if (clear_mark != 0) {
            set_half_at(m_, face + 0xe, (half_at(m_, face + 0xe) & 0xfff7) | 1);
            set_half_at(m_, face, half_at(m_, face) & 0xfe7f);
            const Word flags = half_at(m_, face);
            if (quota == 0) {
                if ((flags & 0x40) == 0 && (flags & 1) != 0) {
                    return 1;
                }
                set_half_at(m_, face, flags & 0xffbf);
                return 1;
            }
            goto record;
        }
    }
    if (quota == 0) {
        return 1;
    }
record:
    if ((half_at(m_, face) & 1) != 0) {
        quota = word_at(m_, face + 0x10);
        set_global_word(m_, kTexturePage, half_at(m_, quota + 2));
        set_global_word(m_, kTextureClut, half_at(m_, quota + 6));
    }
    {
        const Word normal = normals + (static_cast<Word>(half_at(m_, face + 0xc)) >> 3) * 8;
        set_global_half(m_, kNormal, half_at(m_, normal));
        set_global_half(m_, kNormal + 2, half_at(m_, normal + 2));
        set_global_half(m_, kNormal + 4, half_at(m_, normal + 4));
    }
    constexpr Word kMatrix = kScratch + 0x40;
    m3d_.rotation_from_angles(item + 0x14, kMatrix);
    for (Word k = 0; k < 9; ++k) {
        set_half_at(m_, mathkit::gte::kRotation + 2 * k, half_at(m_, kMatrix + 2 * k));
    }
    m3d_.set_translation(item + 8);
    constexpr Word kCorner0 = kScratch + 0x100;
    constexpr Word kCorner1 = kScratch + 0x10c;
    constexpr Word kCorner2 = kScratch + 0x118;
    constexpr Word kCorner3 = kScratch + 0x124;
    constexpr Word kWind = kScratch + 0x130;
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kCorner0 + 4 * k, 0);
        set_word_at(m_, kCorner1 + 4 * k, 0);
        set_word_at(m_, kCorner2 + 4 * k, 0);
    }
    shatter_corner(kCorner0, vertices, indices, 0);
    shatter_corner(kCorner1, vertices, indices, 1);
    shatter_corner(kCorner2, vertices, indices, 2);
    Word result = 1;
    const auto sar = [&](Word v, unsigned n) { return static_cast<std::int32_t>(v) >> n; };
    if ((half_at(m_, face) & 0x10) == 0) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, kCorner3 + 4 * k, 0);
        }
        shatter_corner(kCorner3, vertices, indices, 3);
        shatter_colour(half_at(m_, face) & 0x800, word_at(m_, face + 8), 4, word_at(m_, kRgbTables + region * 0x44));
        for (Word axis = 0; axis < 3; ++axis) {
            const Word total = (static_cast<Word>(sar(word_at(m_, kCorner3 + 4 * axis), 2)) & 0xfffffc00u) + (static_cast<Word>(sar(word_at(m_, kCorner2 + 4 * axis), 2)) & 0xfffffc00u) +
                               (static_cast<Word>(sar(word_at(m_, kCorner1 + 4 * axis), 2)) & 0xfffffc00u) + (static_cast<Word>(sar(word_at(m_, kCorner0 + 4 * axis), 2)) & 0xfffffc00u);
            set_word_at(m_, kCentre + 4 * axis, total & 0xfffff000u);
        }
        const Word flags = half_at(m_, face);
        if ((flags & 0x40) == 0 && (flags & 1) != 0) {
            const Word colour = (flags & 0x20) != 0 ? word_at(m_, quota + 0xc) : 0;
            if (global_word(m_, kShatterOff) == 0) {
                const auto u8 = [&](Word at) { return static_cast<Word>(byte_at(m_, at)); };
                shatter_triangle(kCorner0, kCorner1, kCorner2, u8(quota), u8(quota + 1), u8(quota + 4), u8(quota + 5), u8(quota + 8), u8(quota + 9), colour, depth, quota);
                shatter_triangle(kCorner1, kCorner2, kCorner3, u8(quota + 4), u8(quota + 5), u8(quota + 8), u8(quota + 9), u8(quota + 10), u8(quota + 11), colour, depth, quota);
                return 1;
            }
        } else {
            if (global_word(m_, kShatterOff) == 0) {
                set_global_word(m_, kWind, u(static_cast<std::int16_t>(global_half(m_, kNormal))));
                set_global_word(m_, kWind + 4, u(static_cast<std::int16_t>(global_half(m_, kNormal + 2))));
                set_global_word(m_, kWind + 8, u(static_cast<std::int16_t>(global_half(m_, kNormal + 4))));
                shatter_glass(0x1e, kCorner0, kCorner1, kCorner2, kWind, global_byte(m_, kColourBytes + 2), global_byte(m_, kColourBytes + 1), global_byte(m_, kColourBytes + 1));
                result = 2;
            }
            if (clear_mark != 0) {
                set_half_at(m_, face, half_at(m_, face) & 0xffbf);
            }
        }
        return result;
    }
    shatter_colour(half_at(m_, face) & 0x800, word_at(m_, face + 8), 3, word_at(m_, kRgbTables + region * 0x44));
    for (Word axis = 0; axis < 3; ++axis) {
        const std::int32_t total = sar(word_at(m_, kCorner2 + 4 * axis), 12) + sar(word_at(m_, kCorner1 + 4 * axis), 12) + sar(word_at(m_, kCorner0 + 4 * axis), 12);
        set_word_at(m_, kCentre + 4 * axis, u((total / 3) * 0x1000));
    }
    const Word flags = half_at(m_, face);
    if ((flags & 0x40) == 0 && (flags & 1) != 0) {
        const Word colour = (flags & 0x20) != 0 ? word_at(m_, quota + 0xc) : 0;
        if (global_word(m_, kShatterOff) == 0) {
            const auto u8 = [&](Word at) { return static_cast<Word>(byte_at(m_, at)); };
            shatter_triangle(kCorner0, kCorner1, kCorner2, u8(quota), u8(quota + 1), u8(quota + 4), u8(quota + 5), u8(quota + 8), u8(quota + 9), colour, depth, quota);
            return 1;
        }
    } else {
        if (global_word(m_, kShatterOff) == 0) {
            set_global_word(m_, kWind, u(static_cast<std::int16_t>(global_half(m_, kNormal))));
            set_global_word(m_, kWind + 4, u(static_cast<std::int16_t>(global_half(m_, kNormal + 2))));
            set_global_word(m_, kWind + 8, u(static_cast<std::int16_t>(global_half(m_, kNormal + 4))));
            shatter_glass(0xf, kCorner0, kCorner1, kCorner2, kWind, global_byte(m_, kColourBytes + 2), global_byte(m_, kColourBytes), global_byte(m_, kColourBytes + 1));
            result = 2;
        }
        if (clear_mark != 0) {
            set_half_at(m_, face, half_at(m_, face) & 0xffbf);
            return result;
        }
    }
    return result;
}

// 0x004ae730 (cdecl): a triangle of pieces; with depth left the triangle is halved at a random eighth of its longest edge and each half goes on, at the depth, to be made one piece
void BitKit::shatter_triangle(Word v1, Word v2, Word v3, Word u1, Word w1, Word u2, Word w2, Word u3, Word w3, Word context, Word depth, Word record) {
    if (global_word(m_, kShatterOff) != 0) {
        return;
    }
    // (each level of the recursion keeps its own midpoint: the retail frames are on the stack)
    struct Level {
        BitKit& kit;
        Word index;
        explicit Level(BitKit& k) : kit(k), index(k.triangle_level_++) {}
        ~Level() { --kit.triangle_level_; }
    } level(*this);
    const Word frame = kScratch + 0x2e0 + (level.index & 7) * 0x30;
    const Word kMid = frame;
    const Word kTemp = frame + 0x10;
    const Word kLength = frame + 0x20;
    if (depth != 0) {
        const auto length = [&](Word a, Word b) {
            core_.vector_subtract_out(kTemp, a, b);
            return core_.vector_length(kTemp);
        };
        std::int32_t best = s(length(v2, v3));
        std::int32_t longest = 0;
        const std::int32_t l13 = s(length(v1, v3));
        if (l13 > best) {
            best = l13;
            longest = 1;
        }
        const std::int32_t l12 = s(length(v1, v2));
        const std::int32_t third = l12 > best ? 2 : longest;
        const std::int32_t r = rnd(9);
        set_global_word(m_, kLength, 3);
        const auto midpoint = [&](Word from, Word to) {
            core_.vector_subtract_out(kTemp, to, from);
            set_global_word(m_, kLength + 4, u(r));
            core_.vector_mul_out(kTemp, kTemp, kLength + 4);
            core_.vector_shift_right_out(kTemp, kTemp, kLength);
            core_.vector_add_out(kMid, from, kTemp);
        };
        const auto mix = [&](Word a, Word b) { return ((s(b) - s(a)) * r >> 3) + s(a); };
        if (third == 0) {
            midpoint(v2, v3);
            const Word mu = u(mix(u2, u3)), mv = u(mix(w2, w3));
            shatter_triangle(v1, v2, kMid, u1, w1, u2, w2, mu, mv, context, depth - 1, record);
            shatter_triangle(v1, v3, kMid, u1, w1, u3, w3, mu, mv, context, depth - 1, record);
        } else if (third == 1) {
            midpoint(v1, v3);
            const Word mu = u(mix(u1, u3)), mv = u(mix(w1, w3));
            shatter_triangle(v2, v1, kMid, u2, w2, u1, w1, mu, mv, context, depth - 1, record);
            shatter_triangle(v2, v3, kMid, u2, w2, u3, w3, mu, mv, context, depth - 1, record);
        } else {
            midpoint(v1, v2);
            const Word mu = u(mix(u1, u2)), mv = u(mix(w1, w2));
            shatter_triangle(v3, v1, kMid, u3, w3, u1, w1, mu, mv, context, depth - 1, record);
            shatter_triangle(v3, v2, kMid, u3, w3, u2, w2, mu, mv, context, depth - 1, record);
        }
        return;
    }
    // the leaf: the corners in whole units, their centre, the corners relative to it as shorts
    constexpr Word kShift = kScratch + 0x230;
    constexpr Word kA = kScratch + 0x240;       // three shorts
    constexpr Word kB = kScratch + 0x248;
    constexpr Word kC = kScratch + 0x250;
    constexpr Word kCentreVector = kScratch + 0x260;    // three dwords
    constexpr Word kT1 = kScratch + 0x270;
    constexpr Word kT2 = kScratch + 0x280;
    constexpr Word kT3 = kScratch + 0x290;
    constexpr Word kScalar = kScratch + 0x2a0;
    constexpr Word kProduct = kScratch + 0x2b0;
    set_global_word(m_, kShift, 12);
    core_.vector_shift_right_out(kT1, v1, kShift);
    core_.vector_shift_right_out(kT2, v2, kShift);
    core_.vector_shift_right_out(kT3, v3, kShift);
    const std::int32_t x1 = global_int(m_, kT1), y1 = global_int(m_, kT1 + 4), z1 = global_int(m_, kT1 + 8);
    const std::int32_t x2 = global_int(m_, kT2), y2 = global_int(m_, kT2 + 4), z2 = global_int(m_, kT2 + 8);
    const std::int32_t x3 = global_int(m_, kT3), y3 = global_int(m_, kT3 + 4), z3 = global_int(m_, kT3 + 8);
    const std::int32_t cx = (x3 + x2 + x1) / 3;
    const std::int32_t cy = (y3 + y2 + y1) / 3;
    const std::int32_t cz = (z3 + z2 + z1) / 3;
    set_global_word(m_, kCentreVector, u(cx));
    set_global_word(m_, kCentreVector + 4, u(cy));
    set_global_word(m_, kCentreVector + 8, u(cz));
    const auto put = [&](Word at, std::int32_t x, std::int32_t y, std::int32_t z) {
        set_half_at(m_, at, u(x));
        set_half_at(m_, at + 2, u(y));
        set_half_at(m_, at + 4, u(z));
    };
    put(kA, static_cast<std::int16_t>(x1) - static_cast<std::int16_t>(cx), static_cast<std::int16_t>(y1) - static_cast<std::int16_t>(cy), static_cast<std::int16_t>(z1) - static_cast<std::int16_t>(cz));
    put(kB, static_cast<std::int16_t>(x2) - static_cast<std::int16_t>(cx), static_cast<std::int16_t>(y2) - static_cast<std::int16_t>(cy), static_cast<std::int16_t>(z2) - static_cast<std::int16_t>(cz));
    put(kC, static_cast<std::int16_t>(x3) - static_cast<std::int16_t>(cx), static_cast<std::int16_t>(y3) - static_cast<std::int16_t>(cy), static_cast<std::int16_t>(z3) - static_cast<std::int16_t>(cz));
    // each corner is pushed along the face's normal by a random part of it
    for (const Word corner : {kA, kB, kC}) {
        set_global_half(m_, kScalar, u(rnd(100)));
        set_global_word(m_, kShift, 12);
        // (ScalarMulVector3sOut: 16-bit products; Vector3sShiftRightOut: 16-bit arithmetic shifts; Vector3sAdd: 16-bit sums)
        for (Word k = 0; k < 3; ++k) {
            const auto product = static_cast<std::int16_t>(static_cast<std::int16_t>(global_half(m_, kScalar)) * static_cast<std::int16_t>(half_at(m_, kNormal + 2 * k)));
            set_half_at(m_, kProduct + 2 * k, static_cast<std::uint16_t>(product));
        }
        for (Word k = 0; k < 3; ++k) {
            const auto shifted = static_cast<std::int16_t>(static_cast<std::int16_t>(half_at(m_, kProduct + 2 * k)) >> (global_byte(m_, kShift) & 0x1f));
            set_half_at(m_, corner + 2 * k, half_at(m_, corner + 2 * k) + static_cast<std::uint16_t>(shifted));
        }
    }
    set_global_word(m_, kShift, 12);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, kCentreVector + 4 * k, word_at(m_, kCentreVector + 4 * k) << 12);
    }
    set_global_word(m_, kSpriteFlag, 0);
    const Word block = bit_new(0xc4);
    Word piece = 0;
    if (block != 0) {
        piece = emitter_construct(block, kA, kB, kC, kCentreVector, 0);
    }
    set_global_word(m_, kSpriteFlag, 1);
    chunk_set_rgb(piece, global_byte(m_, kColourBytes + 2), global_byte(m_, kColourBytes), global_byte(m_, kColourBytes + 1));
    chunk_set_texture(piece, global_word(m_, kTexturePage) & 0xffff, global_word(m_, kTextureClut) & 0xffff, u1, w1, u2, w2, u3, w3);
    set_word_at(m_, piece + 0xb0, context);
    set_word_at(m_, piece + 0xb4, record);
}

// 0x004af0b0 Shatter_Item(item, only marked, clear): every face of the item's model is offered to Shatter_Face; a fifth of them (while fewer than six pieces have been made) are cut finer
void BitKit::shatter_item(Word item, Word only_marked, Word clear_mark) {
    const Word region = byte_at(m_, item + 0x1f);
    const Word model = word_at(m_, word_at(m_, kModelTables + region * 0x44) + static_cast<Word>(half_at(m_, item + 0x1a)) * 4);
    Word face = model + 0x1c + (static_cast<Word>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
    bool any = false;
    std::int32_t pieces = 0;
    for (Word faces = half_at(m_, model + 6); faces != 0; --faces) {
        const Word fine = rnd(5) == 0 ? 1 : 0;
        const Word result = shatter_face(item, face, fine, 0, only_marked, clear_mark, pieces < 6 ? 1 : 0);
        if (result != 0) {
            any = true;
            if (result == 1) {
                ++pieces;
            }
        }
        face += (word_at(m_, face) >> 18) * 4;
    }
    if (clear_mark != 0 && any) {
        set_byte_at(m_, item + 4, byte_at(m_, item + 4) | 1);
    }
}

// 0x004af190 / 0x004af1a0: the nearest burst is forgotten
void BitKit::shatter_forget_burst() {
    set_global_word(m_, kNearest, 0);
    set_global_word(m_, kNearest + 4, 0);
    set_global_word(m_, kNearest + 8, 0);
}

// 0x004af1c0 (cdecl): `count` glass shards sprayed from the face's edge midpoint, down to the floor the zone query finds, kept to the burst nearest the camera
void BitKit::shatter_glass(Word count, Word v1, Word v2, Word v3, Word wind, Word red, Word green, Word blue) {
    constexpr Word kShift = kScratch + 0x300;
    constexpr Word kMidpoint = kScratch + 0x310;
    constexpr Word kTemp = kScratch + 0x320;
    constexpr Word kEdgeA = kScratch + 0x330;
    constexpr Word kEdgeB = kScratch + 0x340;
    constexpr Word kPoint = kScratch + 0x350;
    constexpr Word kDirection = kScratch + 0x360;
    constexpr Word kRandomA = kScratch + 0x370;
    constexpr Word kRandomB = kScratch + 0x374;
    constexpr Word kSpeedA = kScratch + 0x378;
    constexpr Word kSpeedB = kScratch + 0x37c;
    constexpr Word kT1 = kScratch + 0x380;
    constexpr Word kT2 = kScratch + 0x390;
    constexpr Word kT3 = kScratch + 0x3a0;
    constexpr Word kCamera = kScratch + 0x3b0;
    set_global_word(m_, kShift, 1);
    core_.vector_add_out(kTemp, v2, v3);
    core_.vector_shift_right_out(kMidpoint, kTemp, kShift);
    const Word mx = global_word(m_, kMidpoint), my = global_word(m_, kMidpoint + 4), mz = global_word(m_, kMidpoint + 8);
    const Word start[3] = {mx + word_at(m_, wind) * 500, my + word_at(m_, wind + 4) * 500, mz + word_at(m_, wind + 8) * 500};
    const Word end[3] = {start[0], start[1] + 0xbb8000, start[2]};
    // (the retail code keeps the query record in its own stack local: Shatter_Item runs inside the face test of a trigger query, whose record - and generation, which the rest of that query's items
    // are tested against - the glass's must not overwrite. The fixtures' probe maps the local to the shared record, so only a live run gets its own)
    const Word kRecord = s_.live() ? kScratch + 0x480 : collision::kInfoScratch;
    const Word body = query_line(start, end, kRecord);
    std::int32_t floor = 0x7fffffff;
    if (body != 0) {
        floor = global_int(m_, kRecord + collision::line::kHitY);
    }
    set_global_word(m_, kShift, 12);
    core_.vector_subtract_out(kTemp, v3, v1);
    core_.vector_shift_right_out(kEdgeB, kTemp, kShift);
    set_global_word(m_, kShift, 12);
    core_.vector_subtract_out(kTemp, v2, v1);
    core_.vector_shift_right_out(kEdgeA, kTemp, kShift);
    if (s(count) > 0) {
        for (std::int32_t k = s(count); k != 0; --k) {
            set_global_word(m_, kRandomA, u(rnd(0x1000)));
            set_global_word(m_, kRandomB, u(rnd(0x1000)));
            set_global_word(m_, kShift, 12);
            core_.scalar_mul_out(kT1, kRandomB, kEdgeA);
            core_.scalar_mul_out(kT2, kRandomA, kEdgeB);
            core_.vector_add_out(kT3, kT2, kT1);
            core_.vector_shift_right_out(kPoint, kT3, kShift);
            core_.vector_shift_left_out(kT1, kPoint, kShift);
            core_.vector_add_out(kPoint, kT1, v1);
            core_.vector_subtract_out(kDirection, kPoint, kMidpoint);
            core_.unit_vector(kDirection, kDirection);
            set_global_word(m_, kSpeedA, u(rnd(0x28)));
            set_global_word(m_, kSpeedB, u(rnd(0x28)));
            core_.vector_mul_out(kT1, wind, kSpeedA);
            core_.vector_mul_out(kT2, kDirection, kSpeedB);
            core_.vector_add_out(kDirection, kT2, kT1);
            if (global_word(m_, kGlassOff) == 0) {
                const Word block = bit_new(0x6c);
                if (block != 0) {
                    glass_construct(block, kPoint, kDirection, u(floor), red, green, blue, 100, 100, 100);
                }
            }
        }
    }
    if (global_word(m_, kBurstSeen) == 0) {
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, kNearest + 4 * j, word_at(m_, kMidpoint + 4 * j));
        }
        set_global_word(m_, kBurstSeen, 1);
        return;
    }
    const Word view = kViews + global_word(m_, kViewIndex) * kViewStride;
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, kCamera + 4 * j, word_at(m_, view + 4 * j));
    }
    set_global_word(m_, kShift, 12);
    for (Word j = 0; j < 3; ++j) {
        set_word_at(m_, kCamera + 4 * j, word_at(m_, kCamera + 4 * j) << 12);
    }
    const Word nearest = core_.crap_distance_of(kCamera, kNearest);
    const Word candidate = core_.crap_distance_of(kCamera, kMidpoint);
    if (candidate < nearest) {
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, kNearest + 4 * j, word_at(m_, kMidpoint + 4 * j));
        }
    }
}

// 0x004af630: the shatter's sound, once per burst
void BitKit::shatter_burst_sound() {
    if (global_word(m_, kBurstSeen) != 0) {
        engine(kPlayWithSound, {0x2a, 0x2000, 0});
    }
    set_global_word(m_, kBurstSeen, 0);
}

// 0x004add10 the emitter: a chunk bit whose spin and speed it chooses (thiscall, five arguments)
Word BitKit::emitter_construct(Word self, Word a, Word b, Word c, Word position, Word unused) {
    (void)unused;
    chunk_construct(self, a, b, c);
    emitter_at(m_, self).set_field_b8(0);
    emitter_at(m_, self).set_field_ba(0);
    emitter_at(m_, self).set_field_bc(0);
    emitter_at(m_, self).set_vtable(static_cast<Word>(kEmitterVtable));
    if (rnd(2) == 0) {
        const Word spin = u(rnd(0xa0));
        emitter_at(m_, self).set_field_b8(0);
        emitter_at(m_, self).set_field_ba(static_cast<std::uint16_t>(spin + 0x50));
    } else {
        const Word spin = u(rnd(0xa0));
        emitter_at(m_, self).set_field_b8(static_cast<std::uint16_t>(spin + 0x50));
        emitter_at(m_, self).set_field_ba(0);
    }
    emitter_at(m_, self).set_field_bc(0);
    emitter_at(m_, self).set_lifetime(static_cast<std::uint16_t>(u(rnd(0x1e)) + 0x2d));
    emitter_set_velocity(self, position);
    chunk_world_coords(self);
    if (rnd(2) != 0) {
        const Word block = bit_new(0x58);
        const Word trail = block != 0 ? smoke_trail_construct(block, self + bit::kPosition, 4, 0x80, 0x80, 0x80) : 0;
        emitter_at(m_, self).set_field_c0(static_cast<Word>(trail));
        set_byte_at(m_, trail + bit::kProtected, 1);
        ribbon_set_scale(trail, 0x4b0);
        ribbon_set_tint(trail, 0x20, 0x20, 0x20);
    }
    return self;
}

// 0x004ade60
Word BitKit::emitter_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::emitter_destruct); }

// 0x004ade80
void BitKit::emitter_destruct(Word self) {
    emitter_at(m_, self).set_vtable(static_cast<Word>(kEmitterVtable));
    if (emitter_at(m_, self).field_c0() != 0) {
        virtual_call(emitter_at(m_, self).field_c0(), 0, {1});
    }
    chunk_destruct(self);
}

// 0x004adee0 (thiscall, one argument): the piece's position and its velocity, away from the wind's origin, a random part of the way plus the wind and a fall
void BitKit::emitter_set_velocity(Word self, Word position) {
    constexpr Word kDifference = kScratch + 0x400;
    constexpr Word kLengthSlot = kScratch + 0x410;
    constexpr Word kFraction = kScratch + 0x414;
    constexpr Word kProduct = kScratch + 0x420;
    constexpr Word kQuotient = kScratch + 0x430;
    constexpr Word kCount = kScratch + 0x440;
    set_pos(self, position);
    core_.vector_subtract_out(kDifference, position, kWindOrigin);
    const Word length = core_.vector_length(kDifference);
    set_global_word(m_, kLengthSlot, length);
    if (length == 0) {
        die(self);
    } else {
        set_global_word(m_, kFraction, u(rnd(0x30)));
        core_.scalar_mul_out(kProduct, kFraction, kDifference);
        core_.vector_div_out(kQuotient, kProduct, kLengthSlot);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + bit::kVelocity + 4 * k, word_at(m_, kQuotient + 4 * k));
        }
    }
    const std::int32_t push = rnd(0x2d);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kVelocity + 4 * k, word_at(m_, self + bit::kVelocity + 4 * k) + u(static_cast<std::int32_t>(short_at(m_, kNormal + 2 * k)) * push));
    }
    emitter_at(m_, self).set_field_20(static_cast<Word>(emitter_at(m_, self).field_20() - (u(rnd(0xaf)) << 12)));
    if (rnd(0x14) == 0) {
        set_global_word(m_, kCount, 1);
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, self + bit::kVelocity + 4 * k, word_at(m_, self + bit::kVelocity + 4 * k) << 1);
        }
    }
    if (static_cast<std::int32_t>(emitter_at(m_, self).field_20()) > 0) {
        emitter_at(m_, self).set_field_20(0);
    }
    if (emitter_at(m_, self).field_c0() != 0) {
        ribbon_set_pos(emitter_at(m_, self).field_c0(), self + bit::kPosition);
    }
}

// 0x004ae020 (fastcall): the piece falls and spins, its trail follows it, its corners are placed; at the end of its life it dies
void BitKit::emitter_move(Word self) {
    vec3_add(self + bit::kPosition, self + bit::kVelocity);
    emitter_at(m_, self).set_field_20(static_cast<Word>(emitter_at(m_, self).field_20() + 0x5014));
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 0x8c + 2 * k, half_at(m_, self + 0x8c + 2 * k) + half_at(m_, self + 0xb8 + 2 * k));
    }
    if (emitter_at(m_, self).field_c0() != 0) {
        ribbon_set_pos(emitter_at(m_, self).field_c0(), self + bit::kPosition);
    }
    chunk_world_coords(self);
    if (emitter_at(m_, self).lifetime() != 0) {
        emitter_at(m_, self).set_lifetime(static_cast<std::uint16_t>(emitter_at(m_, self).lifetime() - 1));
        return;
    }
    die(self);
}

}  // namespace opentony::runtime::bitkit
