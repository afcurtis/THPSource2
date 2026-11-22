#include "gen_views.hpp"

namespace opentony::runtime::genobjs {

using namespace views;

namespace {
constexpr std::uint32_t kMsgMemberNotSet = 0x0052b0ec;      // "model member of GapPiece not set"
constexpr std::uint32_t kMsgTargetNotSet = 0x0052b110;      // "target item not set"
constexpr std::uint32_t kPrimTexture = 0x00533944;          // the 16-bit texture word the primitives carry

constexpr std::uint32_t mul(std::uint32_t a, std::uint32_t b) { return a * b; }
}  // namespace

// 0x004410b0
std::uint32_t GenObjs::gap_init(std::uint32_t self) {
    gen_gap_at(m_, self).set_field_4(0);
    gen_gap_at(m_, self).set_field_8(0);
    gen_gap_at(m_, self).set_field_0(0);
    gen_gap_at(m_, self).set_field_c(0);
    gen_gap_at(m_, self).set_field_10(0);
    gen_gap_at(m_, self).set_field_14(0);
    gen_gap_at(m_, self).set_field_38(1);
    gen_gap_at(m_, self).set_field_39(0);
    return self;
}

// 0x004410e0: the box a gap member covers on the park grid: its two corners (x then z) from the member record's cell position, size and offsets, turned to the rotation,
// plus the span `before` and `after` the piece along the gap.
void GenObjs::gap_compute_edges(std::uint32_t self, std::uint32_t rotation, std::uint32_t before, std::uint32_t after) {
    s_.check(gen_gap_at(m_, self).field_4() != 0, kMsgMemberNotSet);
    const std::uint32_t record = gen_gap_at(m_, self).field_4();
    const std::uint32_t scale = global_word(m_, kCellX);
    gen_gap_at(m_, self).set_field_50(static_cast<Word>(rotation));
    gen_gap_at(m_, self).set_field_54(static_cast<Word>(0x10 - mul(byte_at(m_, record + 6), global_word(m_, kCellZ))));
    const std::uint32_t x = mul(byte_at(m_, record + 5) + static_cast<std::uint32_t>(-static_cast<std::int32_t>(global_word(m_, kGridWidth)) >> 1), scale);
    const std::uint32_t grow = mul(static_cast<std::uint32_t>(sbyte_at(m_, record + 8)), scale);
    const std::uint32_t z = mul(static_cast<std::uint32_t>(static_cast<std::int32_t>(global_word(m_, kGridDepth)) >> 1) - byte_at(m_, record + 7), scale);
    const std::uint32_t depth = mul(static_cast<std::uint32_t>(sbyte_at(m_, record + 0xa)), scale);
    const std::uint32_t span_before = mul(scale, before);
    const std::uint32_t span_after = mul(scale, after);
    switch (rotation) {
    case 0:
        gen_gap_at(m_, self).set_field_20(static_cast<Word>(x + grow));
        gen_gap_at(m_, self).set_field_28(static_cast<Word>(x + grow));
        gen_gap_at(m_, self).set_field_24(static_cast<Word>(z - depth - span_before));
        gen_gap_at(m_, self).set_field_2c(static_cast<Word>(z + span_after));
        break;
    case 1:
        gen_gap_at(m_, self).set_field_20(static_cast<Word>(x - span_after));
        gen_gap_at(m_, self).set_field_24(static_cast<Word>(z));
        gen_gap_at(m_, self).set_field_28(static_cast<Word>(x + grow + span_before));
        gen_gap_at(m_, self).set_field_2c(static_cast<Word>(z));
        break;
    case 2:
        gen_gap_at(m_, self).set_field_20(static_cast<Word>(x));
        gen_gap_at(m_, self).set_field_28(static_cast<Word>(x));
        gen_gap_at(m_, self).set_field_24(static_cast<Word>(z - depth - span_after));
        gen_gap_at(m_, self).set_field_2c(static_cast<Word>(z + span_before));
        break;
    case 3:
        gen_gap_at(m_, self).set_field_20(static_cast<Word>(x - span_before));
        gen_gap_at(m_, self).set_field_24(static_cast<Word>(z - depth));
        gen_gap_at(m_, self).set_field_28(static_cast<Word>(x + grow + span_after));
        gen_gap_at(m_, self).set_field_2c(static_cast<Word>(z - depth));
        break;
    default:
        break;
    }
    gen_gap_at(m_, self).set_field_30(static_cast<Word>(gen_gap_at(m_, self).field_28() - gen_gap_at(m_, self).field_20()));
    gen_gap_at(m_, self).set_field_34(static_cast<Word>(gen_gap_at(m_, self).field_2c() - gen_gap_at(m_, self).field_24()));
}

// 0x00441240: the sizes of the gap member's primitive data; returns the size of the data (the region builder adds it up).
std::uint32_t GenObjs::gap_set_constants(std::uint32_t self) {
    gen_gap_at(m_, self).set_field_40(0x1c);
    set_word_at(m_, self + 0x48, 0x48);
    set_word_at(m_, self + 0x4c, 0x31);
    gen_gap_at(m_, self).set_field_44(0x3c8);
    gen_gap_at(m_, self).set_field_3c(0x864);
    return 0x864;
}

// 0x00441270: the primitive list of a gap member: the header, the grid of points (gap_build_header), then for a 6 x 6 grid of cells two textured quads each.
std::uint32_t GenObjs::gap_build_prims(std::uint32_t self, std::uint32_t buffer) {
    const std::uint32_t prims = gen_gap_at(m_, self).field_40() + buffer;
    gen_gap_at(m_, self).set_field_10(static_cast<Word>(buffer));
    set_half_at(m_, buffer, 0x418);
    if (gen_gap_at(m_, self).field_38() == 0 || gen_gap_at(m_, self).field_39() == 0) {
        set_half_at(m_, buffer, 0x438);
    }
    set_half_at(m_, buffer + 2, half_at(m_, self + 0x4c));
    set_half_at(m_, buffer + 4, half_at(m_, self + 0x48));
    set_half_at(m_, buffer + 6, half_at(m_, self + 0x48));
    set_half_at(m_, buffer + 0x18, global_half(m_, kPrimTexture));
    set_half_at(m_, buffer + 0x1a, 0xffff);
    gap_build_header(self);
    std::uint32_t at = prims + (word_at(m_, self + 0x48) + word_at(m_, self + 0x4c)) * 8;
    const std::uint32_t lit = gen_gap_at(m_, self).field_38() != 0 ? 2 : 0;
    for (std::uint32_t i = 0; i < 6; ++i) {
        for (std::uint32_t j = 0; j < 6; ++j) {
            const std::uint32_t b = (i * 7 + j) & 0xff;
            const std::uint32_t b1 = (b + 1) & 0xff;
            const std::uint32_t b7 = (b + 7) & 0xff;
            const std::uint32_t b8 = (b + 8) & 0xff;
            const std::uint32_t ij = (j * i) & 0xffffffff;
            set_half_at(m_, at, 0x80);
            set_half_at(m_, at + 2, 0x10);
            set_half_at(m_, at + 8, 0x8080);
            set_half_at(m_, at + 4, (b1 << 8) + b);
            set_half_at(m_, at + 0xa, 0xff);
            set_half_at(m_, at + 6, (b8 << 8) + b7);
            set_half_at(m_, at + 0xc, (ij << 4) + 8);
            set_half_at(m_, at + 0xe, lit);
            at += 0x10;
            set_half_at(m_, at, 0x80);
            set_half_at(m_, at + 2, 0x10);
            set_half_at(m_, at + 4, (b << 8) + b1);
            set_half_at(m_, at + 6, (b7 << 8) + b8);
            set_half_at(m_, at + 8, 0x8080);
            set_half_at(m_, at + 0xa, 0xff);
            set_half_at(m_, at + 0xc, ij << 4);
            set_half_at(m_, at + 0xe, lit);
            at += 0x10;
        }
    }
    return gen_gap_at(m_, self).field_3c();
}

// 0x00441400: the header of the grid of points: the extent of the box about its centre, a 7 x 7 lattice of points (x, a height that drops 0x2a8 a row, z) and the pairs of
// unit directions after them; then the target item is placed at the centre.
void GenObjs::gap_build_header(std::uint32_t self) {
    const std::int32_t x0 = static_cast<std::int32_t>(gen_gap_at(m_, self).field_20());
    const std::int32_t z0 = static_cast<std::int32_t>(gen_gap_at(m_, self).field_24());
    const std::int32_t x1 = static_cast<std::int32_t>(gen_gap_at(m_, self).field_28());
    const std::int32_t z1 = static_cast<std::int32_t>(gen_gap_at(m_, self).field_2c());
    const std::uint32_t header = gen_gap_at(m_, self).field_10();
    const std::int32_t cx = (x0 + x1) >> 1;
    const std::int32_t cz = (z0 + z1) >> 1;
    const std::int32_t a = x0 - cx;
    const std::int32_t b = z0 - cz;
    const std::int32_t c = x1 - cx;
    const std::int32_t d = z1 - cz;
    std::uint32_t at = gen_gap_at(m_, self).field_40() + header;
    const std::int32_t max_x = a > c ? a : c;
    set_half_at(m_, header + 0xc, static_cast<std::uint32_t>(max_x));
    const std::int32_t min_x = a < c ? a : c;
    set_half_at(m_, header + 0xe, static_cast<std::uint32_t>(min_x));
    set_half_at(m_, header + 0x10, 0);
    set_half_at(m_, header + 0x12, 0xf010);
    const std::int32_t max_z = b > d ? b : d;
    set_half_at(m_, header + 0x14, static_cast<std::uint32_t>(max_z));
    const std::int32_t min_z = b < d ? b : d;
    set_half_at(m_, header + 0x16, static_cast<std::uint32_t>(min_z));
    set_word_at(m_, header + 8, (static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(max_z))) +
                          static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(max_x))) + 0xff0) << 12);
    auto sixth = [](std::int32_t v) {
        std::int32_t q = static_cast<std::int32_t>((static_cast<std::int64_t>(v) * 0x2aaaaaab) >> 32);
        return q + static_cast<std::int32_t>(static_cast<std::uint32_t>(q) >> 31);
    };
    const std::int32_t step_x = sixth(c - a);
    const std::int32_t step_z = sixth(d - b);
    std::int32_t height = 0;
    for (int row = 0; row < 7; ++row) {
        std::int32_t x = a;
        std::int32_t z = b;
        for (int col = 0; col < 7; ++col) {
            set_half_at(m_, at, static_cast<std::uint32_t>(x));
            x += step_x;
            set_half_at(m_, at + 2, static_cast<std::uint32_t>(height));
            set_half_at(m_, at + 4, static_cast<std::uint32_t>(z));
            set_half_at(m_, at + 6, 0);
            z += step_z;
            at += 8;
        }
        height -= 0x2a8;
    }
    std::int32_t along;
    std::int32_t across;
    switch (gen_gap_at(m_, self).field_50()) {
    case 0:
    case 2:
        along = 0x1000;
        across = 0;
        break;
    case 1:
    case 3:
        along = 0;
        across = -0x1000;
        break;
    default:
        along = step_z;
        across = step_z;
        break;
    }
    const std::int32_t count = int_at(m_, self + 0x48);
    if (count > 0) {
        std::uint32_t p = at + 4;
        for (std::int32_t k = 0; k < count; k += 2) {
            set_half_at(m_, p - 4, static_cast<std::uint32_t>(along));
            set_half_at(m_, p - 2, 0);
            set_half_at(m_, p, static_cast<std::uint32_t>(across));
            set_half_at(m_, p + 2, 0);
            set_half_at(m_, p + 4, static_cast<std::uint32_t>(-along));
            set_half_at(m_, p + 6, 0);
            set_half_at(m_, p + 8, static_cast<std::uint32_t>(-across));
            set_half_at(m_, p + 0xa, 0);
            p += 0x10;
        }
    }
    s_.check(gen_gap_at(m_, self).field_14() != 0, kMsgTargetNotSet);
    const std::uint32_t target = gen_gap_at(m_, self).field_14();
    set_word_at(m_, target + 8, (gen_gap_at(m_, self).field_20() + gen_gap_at(m_, self).field_28()) << 11);
    set_word_at(m_, gen_gap_at(m_, self).field_14() + 0x10, (gen_gap_at(m_, self).field_24() + gen_gap_at(m_, self).field_2c()) << 11);
    set_word_at(m_, gen_gap_at(m_, self).field_14() + 0xc, gen_gap_at(m_, self).field_54() << 12);
}

// 0x00441630
void GenObjs::gap_set_active(std::uint32_t self, std::uint32_t flag) { set_byte_at(m_, self + 0x38, flag); }

// 0x00441640: show or hide the member's primitive list (bit 5 of its first flags word).
void GenObjs::gap_set_visible(std::uint32_t self, std::uint32_t flag) {
    if ((flag & 0xff) == gen_gap_at(m_, self).field_39()) {
        return;
    }
    gen_gap_at(m_, self).set_field_39(static_cast<std::uint8_t>(flag));
    if ((flag & 0xff) != 0 && gen_gap_at(m_, self).field_38() != 0) {
        set_half_at(m_, gen_gap_at(m_, self).field_10(), half_at(m_, gen_gap_at(m_, self).field_10()) & 0xffdf);
        return;
    }
    set_byte_at(m_, gen_gap_at(m_, self).field_10(), byte_at(m_, gen_gap_at(m_, self).field_10()) | 0x20);
}

// 0x00441690; returns the size of a start position's marker data (the region builder adds it up).
std::uint32_t GenObjs::gap_set_defaults(std::uint32_t self) {
    gen_gap_at(m_, self).set_field_14(0x1c);
    gen_gap_at(m_, self).set_field_18(0x28);
    gen_gap_at(m_, self).set_field_10(0x54);
    return 0x54;
}

// 0x004416b0: the four-point quad primitive (and the list that follows it) of a gap piece's marker.
std::uint32_t GenObjs::gap_build_quad(std::uint32_t self, std::uint32_t buffer) {
    const std::uint32_t data = gen_gap_at(m_, self).field_14() + buffer;
    set_half_at(m_, buffer, 0x38);
    set_half_at(m_, buffer + 2, 4);
    set_half_at(m_, buffer + 4, 1);
    set_half_at(m_, buffer + 6, 1);
    set_half_at(m_, buffer + 0x18, global_half(m_, kPrimTexture));
    set_half_at(m_, buffer + 0x1a, 0xffff);
    const std::int32_t cell_z = static_cast<std::int16_t>(global_word(m_, kCellZ));
    const std::int32_t cell_x = static_cast<std::int16_t>(global_word(m_, kCellX));
    const std::uint32_t x = static_cast<std::uint32_t>(cell_x);
    const std::uint32_t nx = static_cast<std::uint32_t>(-cell_x);
    const std::uint32_t z = static_cast<std::uint32_t>(cell_z);
    set_half_at(m_, buffer + 0xc, x);
    set_half_at(m_, buffer + 0xe, nx);
    set_half_at(m_, buffer + 0x10, z);
    set_half_at(m_, buffer + 0x12, z);
    set_half_at(m_, buffer + 0x14, x);
    set_half_at(m_, buffer + 0x16, nx);
    set_word_at(m_, buffer + 8, static_cast<std::uint32_t>(cell_z + cell_x * 2) << 12);
    set_half_at(m_, data, nx);
    set_half_at(m_, data + 0x10, nx);
    set_half_at(m_, data + 0x14, nx);
    set_half_at(m_, data + 0x1c, nx);
    set_half_at(m_, data + 4, x);
    set_half_at(m_, data + 8, x);
    set_half_at(m_, data + 0xc, x);
    set_half_at(m_, data + 0x18, x);
    set_half_at(m_, data + 2, z);
    set_half_at(m_, data + 6, 0);
    set_half_at(m_, data + 0xa, z);
    set_half_at(m_, data + 0xe, 0);
    set_half_at(m_, data + 0x12, z);
    set_half_at(m_, data + 0x16, 0);
    set_half_at(m_, data + 0x1a, z);
    set_half_at(m_, data + 0x1e, 0);
    set_half_at(m_, data + 0x20, 0);
    set_half_at(m_, data + 0x22, 0xf000);
    set_half_at(m_, data + 0x24, 0);
    set_half_at(m_, data + 0x26, 0);
    set_half_at(m_, data + 0x28, 0x80);
    set_half_at(m_, data + 0x2a, 0x10);
    set_half_at(m_, data + 0x2c, 0x100);
    set_half_at(m_, data + 0x2e, 0x302);
    set_half_at(m_, data + 0x30, 0x8080);
    set_half_at(m_, data + 0x32, 0xff);
    set_half_at(m_, data + 0x34, 0);
    set_half_at(m_, data + 0x36, 2);
    return gen_gap_at(m_, self).field_10();
}

}  // namespace opentony::runtime::genobjs
