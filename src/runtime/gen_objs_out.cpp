#include "gen_views.hpp"

#include "player_spooler.hpp"

namespace opentony::runtime::genobjs {

using namespace views;

namespace {
constexpr std::uint32_t kMsgModel1 = 0x0052b060;        // "model1 is NULL"
constexpr std::uint32_t kMsgModel2 = 0x0052b050;
constexpr std::uint32_t kMsgFace1 = 0x0052b040;
constexpr std::uint32_t kMsgFace2 = 0x0052b030;
constexpr std::uint32_t kMsgSourceFace = 0x0052b018;    // "source face not defined"
constexpr std::uint32_t kMsgNeighbour = 0x0052b094;     // "null pointer in neighbor"
constexpr std::uint32_t kMsgSelfNeighbour = 0x0052b070; // "model references itself as neighbor"
constexpr std::uint32_t kMsgFaceCount = 0x0052b0c4;     // "NumFaces should equal num source faces"
constexpr std::uint32_t kMsgWireModel = 0x0052b0b0;     // "wire model is NULL"
constexpr std::uint32_t kMsgNoPiece = 0x0052affc;       // (the in-model's byte 0xb must not be negative)
constexpr std::uint32_t kShadeBase = 0x0052a410;        // per-kind shade table for the theme

constexpr std::uint32_t mul(std::uint32_t a, std::uint32_t b) { return a * b; }
}  // namespace

// 0x00440190: build the out-model of an in-model: the sizes of the model, one ring node per face element, the rotation of the piece; nothing is placed yet.
std::uint32_t GenObjs::out_construct(std::uint32_t self, std::uint32_t in) {
    gen_out_at(m_, self).set_field_224(static_cast<Word>(in));
    const std::uint32_t model = gen_in_at(m_, in).field_4();
    gen_out_at(m_, self).set_field_10(static_cast<std::uint16_t>(half_at(m_, model + 2)));
    gen_out_at(m_, self).set_field_12(static_cast<std::uint16_t>(half_at(m_, model + 4)));
    gen_out_at(m_, self).set_field_14(static_cast<std::uint16_t>(half_at(m_, model + 6)));
    m_.fill(self + 0x18, 0, 0x40);
    gen_out_at(m_, self).set_field_230(0);
    gen_out_at(m_, self).set_field_218(0);
    std::uint32_t previous = 0;
    for (std::uint32_t element = gen_in_at(m_, in).field_0(); element != 0; element = word_at(m_, element + 4)) {
        const std::uint32_t node = node_alloc();
        set_word_at(m_, node + 8, element);
        if (previous == 0) {
            gen_out_at(m_, self).set_field_218(static_cast<Word>(node));
        } else {
            set_word_at(m_, previous + 0xc, node);
        }
        previous = node;
    }
    gen_out_at(m_, self).set_field_b(static_cast<std::uint8_t>(gen_in_at(m_, in).field_d()));
    s_.check(static_cast<std::int8_t>(gen_out_at(m_, self).field_b()) >= 0, kMsgNoPiece);
    gen_out_at(m_, self).set_field_d(static_cast<std::uint8_t>(piece_is_special(gen_in_at(m_, in).field_8()) ? 1 : 0));
    gen_out_at(m_, self).set_field_228(0);
    gen_out_at(m_, self).set_field_2(0);
    gen_out_at(m_, self).set_field_0(0);
    gen_out_at(m_, self).set_field_234(0);
    gen_out_at(m_, self).set_field_1(1);
    return self;
}

// 0x00440290: hand the nodes back to the ring.
void GenObjs::out_release_nodes(std::uint32_t self) {
    while (gen_out_at(m_, self).field_218() != 0) {
        set_word_at(m_, gen_out_at(m_, self).field_218(), kRingFree);
        set_global_word(m_, kRingUsed, global_word(m_, kRingUsed) - 1);
        gen_out_at(m_, self).set_field_218(static_cast<Word>(word_at(m_, gen_out_at(m_, self).field_218() + 0xc)));
    }
}

// 0x004402d0: every node is in use and visible again.
void GenObjs::out_show_nodes(std::uint32_t self) {
    for (std::uint32_t node = gen_out_at(m_, self).field_218(); node != 0; node = word_at(m_, node + 0xc)) {
        set_byte_at(m_, node + 4, 1);
        set_word_at(m_, node, kRingLive);
    }
}

// 0x004402f0: hide the faces of this piece and `other` that face each other.  Mode 0/1: the live nodes are paired and the pair test decides; mode 2/3: every pair is
// tested after the nodes of both are marked in use.
void GenObjs::out_hide_against(std::uint32_t self, std::uint32_t other, std::uint32_t mode) {
    if (gen_out_at(m_, self).field_1() != 2 && gen_out_at(m_, other).field_1() != 2) {
        for (std::uint32_t a = gen_out_at(m_, self).field_218(); a != 0; a = word_at(m_, a + 0xc)) {
            if (byte_at(m_, a + 4) == 0) {
                continue;
            }
            for (std::uint32_t b = gen_out_at(m_, other).field_218(); b != 0; b = word_at(m_, b + 0xc)) {
                if (byte_at(m_, b + 4) != 0) {
                    out_hide_pair(self, other, a, b, mode);
                }
            }
        }
        return;
    }
    for (std::uint32_t b = gen_out_at(m_, other).field_218(); b != 0; b = word_at(m_, b + 0xc)) {
        if (byte_at(m_, b + 4) != 0) {
            set_word_at(m_, b, kRingLive);
        }
    }
    for (std::uint32_t a = gen_out_at(m_, self).field_218(); a != 0; a = word_at(m_, a + 0xc)) {
        if (byte_at(m_, a + 4) != 0) {
            set_word_at(m_, a, kRingLive);
        }
        for (std::uint32_t b = gen_out_at(m_, other).field_218(); b != 0; b = word_at(m_, b + 0xc)) {
            out_hide_pair(self, other, a, b, mode);
        }
    }
}

// 0x004403c0: two faces of two pieces: when they lie in one plane facing each other, build both as rings of points on the park grid, take the overlap and take its area
// off both faces; a face with (nearly) nothing left is hidden.
void GenObjs::out_hide_pair(std::uint32_t self, std::uint32_t other, std::uint32_t a, std::uint32_t b, std::uint32_t mode) {
    s_.check(self != 0, kMsgModel1);
    s_.check(other != 0, kMsgModel2);
    s_.check(a != 0, kMsgFace1);
    s_.check(b != 0, kMsgFace2);
    points_reset();
    const std::uint32_t models[2] = {self, other};
    const std::uint32_t nodes[2] = {a, b};
    std::uint32_t turn[2];
    for (int k = 0; k < 2; ++k) {
        s_.check(word_at(m_, nodes[k] + 8) != 0, kMsgSourceFace);
        std::int32_t t = sbyte_at(m_, word_at(m_, nodes[k] + 8));
        if ((t & 1) == 0) {
            t = 2 - t;
        }
        std::uint32_t sum = static_cast<std::uint32_t>(byte_at(m_, models[k] + 0xc)) + static_cast<std::uint32_t>(t);
        t = static_cast<std::int32_t>(sum & 3);
        if ((t & 1) == 0) {
            t = 2 - t;
        }
        turn[k] = static_cast<std::uint32_t>(t);
        set_byte_at(m_, nodes[k] + 5, turn[k]);
    }
    const std::uint32_t wanted = mode & 0xff;
    if (turn[1] != wanted || ((wanted ^ turn[0]) & 2) == 0 || ((wanted ^ turn[0]) & 1) != 0) {
        return;
    }
    std::uint32_t ring[2] = {0, 0};
    std::uint32_t last = nodes[0];           // (the register keeps the last point made when a face has no points)
    for (int k = 0; k < 2; ++k) {
        const std::uint32_t model = models[k];
        const std::uint32_t element = word_at(m_, nodes[k] + 8);
        std::uint32_t previous = 0;
        const std::int32_t n = sbyte_at(m_, element + 2);
        for (std::int32_t j = 0; j < n; ++j) {
            const std::uint32_t point = point_new();
            if (previous != 0) {
                set_word_at(m_, previous + 0x10, point);
            } else {
                ring[k] = point;
            }
            previous = point;
            last = point;
            const std::uint32_t at = element + 6 * static_cast<std::uint32_t>(j);
            const std::int32_t px = short_at(m_, at + 0xc);
            const std::int32_t pz = short_at(m_, at + 0x10);
            const std::int32_t off_z = short_at(m_, model + 0x21c);
            const std::int32_t off_x = short_at(m_, model + 0x220);
            switch (turn[k]) {
            case 0:
                set_word_at(m_, point + 8, static_cast<std::uint32_t>(off_z - pz));
                set_word_at(m_, point, static_cast<std::uint32_t>(off_x - px));
                break;
            case 1:
                set_word_at(m_, point, static_cast<std::uint32_t>(off_z - px));
                set_word_at(m_, point + 8, static_cast<std::uint32_t>(pz + off_x));
                break;
            case 2:
                set_word_at(m_, point + 8, static_cast<std::uint32_t>(pz + off_z));
                set_word_at(m_, point, static_cast<std::uint32_t>(px + off_x));
                break;
            case 3:
                set_word_at(m_, point, static_cast<std::uint32_t>(px + off_z));
                set_word_at(m_, point + 8, static_cast<std::uint32_t>(off_x - pz));
                break;
            default:
                break;
            }
            set_word_at(m_, point + 4, static_cast<std::uint32_t>(static_cast<std::int32_t>(short_at(m_, at + 0xe)) + short_at(m_, model + 0x21e)));
        }
        set_word_at(m_, last + 0x10, ring[k]);
        if ((turn[k] & 2) == 0) {
            std::uint32_t before = 0;
            std::uint32_t cursor = ring[k];
            std::uint32_t next;
            do {
                next = word_at(m_, cursor + 0x10);
                if (before != 0) {
                    set_word_at(m_, cursor + 0x10, before);
                }
                before = cursor;
                cursor = next;
            } while (next != ring[k]);
            set_word_at(m_, ring[k] + 0x10, before);
        }
        if (word_at(m_, nodes[k]) == kRingLive) {
            set_word_at(m_, nodes[k], points_area(ring[k]));
        }
    }
    const std::int32_t removed = faces_overlap(ring[0], ring[1]);
    for (int k = 0; k < 2; ++k) {
        const std::int32_t before = int_at(m_, nodes[k]);
        const std::int32_t after = before - removed;
        set_word_at(m_, nodes[k], static_cast<std::uint32_t>(after));
        if (after < 0x12d || after <= (before >> 5)) {
            set_byte_at(m_, nodes[k] + 4, 0);
        }
    }
}

// 0x004407c0: against each neighbour (the other pieces round it) hide what the two hide of each other.
void GenObjs::out_remove_hidden(std::uint32_t self) {
    const std::uint32_t in = gen_out_at(m_, self).field_224();
    if ((gen_in_at(m_, in).field_9() & 1) != 0 || piece_is_special(gen_in_at(m_, in).field_8()) || gen_out_at(m_, self).field_3() == 0) {
        return;
    }
    for (std::int32_t i = 0; i < gen_out_at(m_, self).field_3(); ++i) {
        const std::uint32_t other = word_at(m_, self + 0x18 + 4 * static_cast<std::uint32_t>(i));
        s_.check(other != 0, kMsgNeighbour);
        s_.check(self != other, kMsgSelfNeighbour);
        const std::uint32_t other_in = gen_out_at(m_, other).field_224();
        if ((byte_at(m_, other_in + 9) & 1) != 0 || piece_is_special(byte_at(m_, other_in + 8))) {
            continue;
        }
        if (gen_out_at(m_, self).field_1() == 0 && gen_out_at(m_, other).field_1() == 0) {
            continue;
        }
        std::uint32_t mode;
        if ((byte_at(m_, gen_out_at(m_, self).field_224() + 9) & 8) != 0) {
            mode = i < gen_out_at(m_, self).field_4() ? 3 : 2;
        } else {
            mode = i < gen_out_at(m_, self).field_4() ? 1 : 0;
        }
        out_hide_against(self, other, mode);
    }
}

// 0x00440920: the size of the model data the piece will have: the header and vertices and every face that is not hidden.
std::uint32_t GenObjs::out_data_size(std::uint32_t self, std::uint32_t flag) {
    std::uint32_t node = gen_out_at(m_, self).field_218();
    std::uint32_t element = word_at(m_, gen_out_at(m_, self).field_224());
    gen_out_at(m_, self).set_field_14(0);
    const std::uint32_t model = word_at(m_, gen_out_at(m_, self).field_224() + 4);
    const std::uint32_t head = (static_cast<std::uint32_t>(gen_out_at(m_, self).field_12()) + gen_out_at(m_, self).field_10()) * 8;
    std::uint32_t count = half_at(m_, model + 6);
    std::uint32_t face = model + 0x1c + head;
    std::uint32_t total = 0;
    for (; count != 0; --count) {
        const std::uint32_t stride = half_at(m_, face + 2);
        bool keep = true;
        if ((flag & 0xff) != 0 && element != 0 && word_at(m_, element + 8) == face) {
            if (byte_at(m_, node + 4) == 0) {
                keep = false;
            }
            node = word_at(m_, node + 0xc);
            element = word_at(m_, element + 4);
        }
        if (keep) {
            total += stride;
            gen_out_at(m_, self).set_field_14(static_cast<std::uint16_t>(gen_out_at(m_, self).field_14() + 1));
        }
        face += stride;
    }
    const std::uint32_t size = total + head + 0x1c;
    gen_out_at(m_, self).set_field_e(static_cast<std::uint16_t>(size));
    return size & 0xffff;
}

// 0x004409e0: write the piece's model into `buffer`: the header and vertices turned to the piece's rotation, then every face that is not hidden, its vertex shades
// taken from the park lights.
std::uint32_t GenObjs::out_create_data(std::uint32_t self, std::uint32_t buffer, std::uint32_t flag) {
    const std::uint32_t in = gen_out_at(m_, self).field_224();
    gen_out_at(m_, self).set_field_228(static_cast<Word>(buffer));
    const std::uint32_t model = gen_in_at(m_, in).field_4();
    const std::uint32_t head = (static_cast<std::uint32_t>(gen_out_at(m_, self).field_12()) + gen_out_at(m_, self).field_10()) * 8 + 0x1c;
    mem_copy(buffer, model, head);
    std::uint32_t out = buffer + head;
    const std::uint32_t header = gen_out_at(m_, self).field_228();
    const std::int32_t h0 = short_at(m_, header + 0xc);
    const std::int32_t h14 = short_at(m_, header + 0x14);
    const std::int32_t h16 = short_at(m_, header + 0x16);
    const std::int32_t he = short_at(m_, header + 0xe);
    const std::uint32_t rotation = gen_out_at(m_, self).field_c();
    auto put = [&](std::uint32_t at, std::int32_t value) { set_half_at(m_, at, static_cast<std::uint32_t>(value)); };
    switch (rotation) {
    case 0:
        put(header + 0xe, he);
        put(header + 0xc, h0);
        put(header + 0x16, h16);
        put(header + 0x14, h14);
        break;
    case 1:
        put(header + 0xe, -h14);
        put(header + 0xc, -h16);
        put(header + 0x16, he);
        put(header + 0x14, h0);
        break;
    case 2:
        put(header + 0xe, -h0);
        put(header + 0xc, -he);
        put(header + 0x16, -h14);
        put(header + 0x14, -h16);
        break;
    case 3:
        put(header + 0xe, h16);
        put(header + 0xc, h14);
        put(header + 0x16, -h0);
        put(header + 0x14, -he);
        break;
    default:
        break;
    }
    const std::uint32_t vertices = gen_out_at(m_, self).field_228() + 0x1c;
    auto turn_vertices = [&](std::uint32_t first, std::uint32_t count) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t at = first + 8 * i;
            const std::int32_t x = short_at(m_, at);
            const std::int32_t z = short_at(m_, at + 4);
            switch (gen_out_at(m_, self).field_c()) {
            case 1:
                put(at, -z);
                put(at + 4, x);
                break;
            case 2:
                put(at, -x);
                put(at + 4, -z);
                break;
            case 3:
                put(at, z);
                put(at + 4, -x);
                break;
            default:
                break;
            }
        }
    };
    turn_vertices(vertices, gen_out_at(m_, self).field_10());
    turn_vertices(vertices + static_cast<std::uint32_t>(gen_out_at(m_, self).field_10()) * 8, gen_out_at(m_, self).field_12());

    std::uint32_t node = gen_out_at(m_, self).field_218();
    std::uint32_t element = gen_in_at(m_, in).field_0();
    std::uint32_t faces = half_at(m_, gen_in_at(m_, in).field_4() + 6);
    std::uint32_t face = gen_in_at(m_, in).field_4() + head;
    for (; faces != 0; --faces) {
        bool wire = false;
        bool copy = true;
        const std::uint32_t stride = half_at(m_, face + 2);
        if (element != 0 && word_at(m_, element + 8) == face) {
            if (byte_at(m_, node + 4) == 0) {
                if ((flag & 0xff) == 0) {
                    wire = true;
                } else {
                    copy = false;
                }
            }
            element = word_at(m_, element + 4);
            node = word_at(m_, node + 0xc);
        }
        if (copy) {
            mem_copy(out, face, stride);
            if (wire) {
                set_half_at(m_, out, half_at(m_, out) & 0xff3f);
            }
            set_byte_at(m_, out + 1, byte_at(m_, out + 1) | 8);
            const std::uint32_t colour = half_at(m_, out + 0xe);
            const std::uint32_t kind = (colour >> 9) & 0xf;
            std::int32_t shade_offset = -1;
            switch (kind) {
            case 1: shade_offset = 0; break;
            case 4: shade_offset = 4; break;
            case 5: shade_offset = 0x10; break;
            case 6: shade_offset = 0xc; break;
            case 8: shade_offset = 8; break;
            case 0xe: shade_offset = 0x14; break;
            default: break;
            }
            if (shade_offset >= 0) {
                const std::uint32_t value = byte_at(m_, kShadeBase + static_cast<std::uint32_t>(shade_offset) + global_word(m_, kTheme));
                set_half_at(m_, out + 0xe, (value << 9) | (colour & 0xe1ff));
            }
            const std::int32_t lights = global_int(m_, kLightCount);
            const std::uint32_t corners = 4 - ((half_at(m_, out) & 0x10) != 0 ? 1 : 0);
            for (std::uint32_t k = 0; k < corners; ++k) {
                const std::uint32_t index = byte_at(m_, out + 4 + k);
                std::uint32_t sum = 0;
                if (lights > 0) {
                    for (std::int32_t i = 0; i < lights; ++i) {
                        const std::uint32_t light = kLightPositions + 12 * static_cast<std::uint32_t>(i);
                        const std::int32_t x = gen_out_at(m_, self).field_21c() - int_at(m_, light) + short_at(m_, vertices + index * 8);
                        const std::int32_t z = short_at(m_, vertices + index * 8 + 4) - int_at(m_, light + 8) + gen_out_at(m_, self).field_220();
                        set_global_half(m_, kGteInput, static_cast<std::uint32_t>(x));
                        set_global_half(m_, kGteInput + 2, 0);
                        set_global_half(m_, kGteInput + 4, static_cast<std::uint32_t>(z));
                        s_.note(pspool::service_name(kGteSqr0));
                        // (live, the GTE routine left the three squares in the register file itself; a replay scripts them)
                        for (std::uint32_t r = 0; !s_.live() && r < 3; ++r) {
                            set_word_at(m_, kGteSquares + 4 * r, static_cast<std::uint32_t>(s_.value(pspool::service_name(kGteSqr0))));
                        }
                        const std::int32_t distance = static_cast<std::int32_t>(global_word(m_, kGteSquares + 4) + global_word(m_, kGteSquares + 8) + global_word(m_, kGteSquares));
                        std::int32_t fall;
                        if (distance == 0) {
                            fall = 0x3ff;
                        } else {
                            fall = static_cast<std::int32_t>(global_word(m_, kLightB) << 10) / distance;
                            if (fall >= 0x400) {
                                fall = 0x3ff;
                            }
                        }
                        sum += byte_at(m_, global_word(m_, kFalloffTable) + static_cast<std::uint32_t>(fall));
                    }
                    if (sum > 0xff) {
                        sum = 0xff;
                    }
                }
                set_byte_at(m_, out + 8 + k, byte_at(m_, global_word(m_, kShadeTable) + sum));
            }
            out += stride;
        }
        face += stride;
    }
    set_half_at(m_, gen_out_at(m_, self).field_228() + 6, gen_out_at(m_, self).field_14());
    if ((gen_in_at(m_, in).field_9() & 8) != 0) {
        set_byte_at(m_, gen_out_at(m_, self).field_228() + 1, byte_at(m_, gen_out_at(m_, self).field_228() + 1) | 2);
    }
    if ((flag & 0xff) != 0 && (gen_in_at(m_, in).field_9() & 0x20) != 0) {
        set_byte_at(m_, gen_out_at(m_, self).field_228(), byte_at(m_, gen_out_at(m_, self).field_228()) | 0x20);
    }
    return gen_out_at(m_, self).field_e();
}

// 0x00440e80: mark the faces of the built model that are hidden (wire_pass 0: the hidden ones lose bit 7 of their flags) or, for the wire pass, those whose ring node
// says they face `flags` (bit 7 is set).
void GenObjs::out_mark_faces(std::uint32_t self, std::uint32_t wire_pass, std::uint32_t wire, std::uint32_t flags) {
    const std::uint32_t in = gen_out_at(m_, self).field_224();
    if ((gen_in_at(m_, in).field_9() & 8) != 0) {
        return;
    }
    const std::uint32_t built = gen_out_at(m_, self).field_228();
    std::uint32_t out_face = built + 0x1c + (static_cast<std::uint32_t>(half_at(m_, built + 4)) + half_at(m_, built + 2)) * 8;
    const std::uint32_t model = gen_in_at(m_, in).field_4();
    std::uint32_t count = half_at(m_, model + 6);
    std::uint32_t source = model + 0x1c + (static_cast<std::uint32_t>(half_at(m_, model + 4)) + half_at(m_, model + 2)) * 8;
    s_.check(gen_out_at(m_, self).field_14() == count, kMsgFaceCount);
    std::uint32_t node = gen_out_at(m_, self).field_218();
    std::uint32_t element = word_at(m_, gen_out_at(m_, self).field_224());
    if ((wire_pass & 0xff) == 0) {
        for (; count != 0; --count) {
            if (element != 0 && word_at(m_, element + 8) == source) {
                if (byte_at(m_, node + 4) == 0) {
                    set_half_at(m_, out_face, half_at(m_, out_face) & 0xff7f);
                }
                element = word_at(m_, element + 4);
                node = word_at(m_, node + 0xc);
            }
            out_face += half_at(m_, out_face + 2);
            source += half_at(m_, source + 2);
        }
        return;
    }
    s_.check(wire != 0, kMsgWireModel);
    for (; count != 0; --count) {
        if (element != 0 && word_at(m_, element + 8) == source) {
            if (byte_at(m_, node + 4) == 0) {
                const std::uint32_t turned = byte_at(m_, node + 5) ^ (flags & 0xff);
                if ((turned & 1) == 0 && (turned & 2) != 0) {
                    set_byte_at(m_, out_face, byte_at(m_, out_face) | 0x80);
                }
            }
            element = word_at(m_, element + 4);
            node = word_at(m_, node + 0xc);
        }
        out_face += half_at(m_, out_face + 2);
        source += half_at(m_, source + 2);
    }
}

// 0x00441050: fog on the piece's header: a density and a packed colour.
void GenObjs::out_set_fog(std::uint32_t self, std::uint32_t on, std::uint32_t colour) {
    const std::uint32_t header = gen_out_at(m_, self).field_22c();
    if ((on & 0xff) == 0) {
        set_half_at(m_, header + 4, 0);
        set_word_at(m_, gen_out_at(m_, self).field_22c() + 0x24, 0);
        return;
    }
    set_half_at(m_, header + 4, 0x400);
    const std::uint32_t packed = (((colour & 0x3fffffc) + (colour << 10)) << 6) + static_cast<std::uint32_t>(static_cast<std::int32_t>(colour) >> 2);
    set_word_at(m_, gen_out_at(m_, self).field_22c() + 0x24, packed);
}

}  // namespace opentony::runtime::genobjs
