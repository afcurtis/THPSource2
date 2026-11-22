#include "gen_views.hpp"

#include "player_spooler.hpp"

#include <cmath>

namespace opentony::runtime::genobjs {

using namespace views;

// The ring-of-points polygons of genobjs.cpp.  A point is 0x18 bytes: three 16-bit coordinates (x, height, z), then +8 the next point, +0xc and +0x10 two words the
// copies keep and +0x14 a mark byte.  A polygon is 0x1c bytes: its box (three minima then three maxima, 16 bits each), +0xc the first point, +0x10 the next polygon of a
// list, +0x14 whether the points close into a loop and +0x18 how many there are.  A list holds the first polygon at +0.
namespace {
constexpr std::uint32_t kMsgNewPoint = 0x0052b158;          // "new point is NULL"
constexpr std::uint32_t kMsgLoop = 0x0052b144;              // "cannot make a loop"
constexpr std::uint32_t kMsgAddToLoop = 0x0052b124;         // "can't add new points to a loop"
constexpr std::uint32_t kMsgRailLoop = 0x0052b16c;          // "messed up rail loop"
constexpr std::uint32_t kMsgNullRail = 0x0052b1a4;          // "NULL rail string"
constexpr std::uint32_t kMsgEmptyRail = 0x0052b190;         // "Empty rail string"
constexpr std::uint32_t kMsgRotation = 0x0052b180;          // "weird rotation"
constexpr std::uint32_t kMsgNonLoops = 0x0052b204;          // "both strings must be non-loops"
constexpr std::uint32_t kMsgToNext = 0x0052b1f0;            // "to next not defined"
constexpr std::uint32_t kMsgFromLastPrev = 0x0052b1d4;      // "from last prev not defined"
constexpr std::uint32_t kMsgToLastPrev = 0x0052b1b8;        // "to last prev not defined"
constexpr std::uint32_t kMsgBothLoops = 0x0052b2a8;         // "both strings must be loops"
constexpr std::uint32_t kMsgSourceNotLoop = 0x0052b294;
constexpr std::uint32_t kMsgDestNotLoop = 0x0052b284;
constexpr std::uint32_t kMsgSourceOne = 0x0052b26c;
constexpr std::uint32_t kMsgDestOne = 0x0052b258;
constexpr std::uint32_t kMsgNoFirstTo = 0x0052b240;
constexpr std::uint32_t kMsgNoFirstFrom = 0x0052b224;
constexpr std::uint32_t kMsgOneNonLoop = 0x0052b2c4;        // "at least one string must be non-loop"
constexpr std::uint32_t kMsgHeadNull = 0x0052b344;
constexpr std::uint32_t kMsgHeadSingle = 0x0052b318;
constexpr std::uint32_t kMsgTooFew = 0x0052b2ec;
constexpr std::uint32_t kMsgPointListNull = 0x0052b434;
constexpr std::uint32_t kMsgKillNull = 0x0052b410;
constexpr std::uint32_t kMsgKillLoop = 0x0052b3f0;
constexpr std::uint32_t kMsgUndefined = 0x0052b3dc;
constexpr std::uint32_t kMsgFirstShared = 0x0052b3c0;
constexpr std::uint32_t kMsgSameShared = 0x0052b39c;
constexpr std::uint32_t kMsgFragment = 0x0052b36c;
constexpr std::uint32_t kMsgTrimShort = 0x0052b44c;
constexpr std::uint32_t kMsgNullList = 0x0052b4a0;
constexpr std::uint32_t kMsgEmptySet = 0x0052b490;
constexpr std::uint32_t kMsgEmptyString = 0x0052b47c;
constexpr std::uint32_t kMsgUnexpected = 0x0052b468;
constexpr std::uint32_t kMsgPopEmpty = 0x0052b4b0;
constexpr std::uint32_t kMsgAppendNull = 0x0052b4e8;
constexpr std::uint32_t kMsgAppendEmpty = 0x0052b4c8;
constexpr std::uint32_t kLoadClock = 0x0056e31c;
constexpr std::uint32_t kYield = 0x0045d760;

// the engine's integer square root: fild, fsqrt (24-bit precision), truncate
std::int32_t isqrt(std::int32_t n) {
    if (n <= 0) {
        return 0;
    }
    const float root = static_cast<float>(std::sqrt(static_cast<double>(n)));
    return static_cast<std::int32_t>(root);
}
}  // namespace

// ---- points and polygons ---------------------------------------------------------------------------------------------------

// 0x004417c0
std::uint32_t GenObjs::point_init(std::uint32_t self) {
    gen_point_at(m_, self).set_field_8(0);
    gen_point_at(m_, self).set_field_c(0xffffffff);
    gen_point_at(m_, self).set_field_10(0);
    return self;
}

// 0x004417e0: are two points within 6 of each other in every coordinate?
bool GenObjs::point_near(std::uint32_t self, std::uint32_t other) {
    const std::int32_t dx = static_cast<std::int16_t>(gen_point_at(m_, self).field_0()) - static_cast<std::int16_t>(gen_point_at(m_, other).field_0());
    const std::int32_t dy = static_cast<std::int16_t>(gen_point_at(m_, self).field_2()) - static_cast<std::int16_t>(gen_point_at(m_, other).field_2());
    const std::int32_t dz = static_cast<std::int16_t>(gen_point_at(m_, self).field_4()) - static_cast<std::int16_t>(gen_point_at(m_, other).field_4());
    return dx >= -6 && dx <= 6 && dy >= -6 && dy <= 6 && dz >= -6 && dz <= 6;
}

// 0x00441830
std::uint32_t GenObjs::poly_init(std::uint32_t self) {
    gen_poly_at(m_, self).set_field_c(0);
    gen_poly_at(m_, self).set_field_10(0);
    gen_poly_at(m_, self).set_field_14(0);
    gen_poly_at(m_, self).set_field_18(0);
    return self;
}

// 0x00441850: free the points (the ring or the chain); the polygon keeps its stale head.
void GenObjs::poly_free_ring(std::uint32_t self) {
    std::uint32_t point = gen_poly_at(m_, self).field_c();
    if (point == 0) {
        return;
    }
    for (;;) {
        const std::uint32_t next = gen_point_at(m_, point).field_8();
        delete_block(point);
        const std::uint32_t head = gen_poly_at(m_, self).field_c();
        if (next == head) {
            break;
        }
        point = next;
        if (next == 0) {
            break;
        }
    }
}

// 0x00441880: add a point at the end; `closed` turns the chain into a loop (and then no more points may be added).
void GenObjs::poly_append(std::uint32_t self, std::uint32_t point, std::uint32_t closed) {
    const bool close = (closed & 0xff) != 0;
    s_.check(point != 0, kMsgNewPoint);
    s_.check(!(gen_poly_at(m_, self).field_c() == 0 && close), kMsgLoop);
    s_.check(gen_poly_at(m_, self).field_14() == 0, kMsgAddToLoop);
    gen_poly_at(m_, self).set_field_14(static_cast<std::uint8_t>(close ? 1 : 0));
    if (close) {
        gen_point_at(m_, point).set_field_8(static_cast<Word>(gen_poly_at(m_, self).field_c()));
    } else {
        gen_point_at(m_, point).set_field_8(0);
    }
    std::uint32_t last = gen_poly_at(m_, self).field_c();
    if (last == 0) {
        gen_poly_at(m_, self).set_field_c(static_cast<Word>(point));
        gen_poly_at(m_, self).set_field_18(1);
        return;
    }
    while (word_at(m_, last + 8) != 0) {
        last = word_at(m_, last + 8);
    }
    set_word_at(m_, last + 8, point);
    gen_poly_at(m_, self).set_field_18(static_cast<Word>(gen_poly_at(m_, self).field_18() + 1));
}

// 0x004419c0: a loop must run the same way round; when its first corner turns the wrong way it is reversed.
void GenObjs::poly_fix_winding(std::uint32_t self) {
    if (gen_poly_at(m_, self).field_14() == 0) {
        return;
    }
    std::uint32_t point = gen_poly_at(m_, self).field_c();
    std::int16_t sum_x = 0;
    std::int16_t sum_z = 0;
    std::uint32_t at = point;
    do {
        sum_x = static_cast<std::int16_t>(sum_x + short_at(m_, at));
        sum_z = static_cast<std::int16_t>(sum_z + short_at(m_, at + 4));
        at = word_at(m_, at + 8);
    } while (at != point);
    const std::int32_t count = static_cast<std::int32_t>(gen_poly_at(m_, self).field_18());
    const std::int32_t mean_x = static_cast<std::int16_t>(static_cast<std::int32_t>(sum_x) / count);
    const std::int32_t mean_z = static_cast<std::int16_t>(static_cast<std::int32_t>(sum_z) / count);
    for (;;) {
        const std::uint32_t next = gen_point_at(m_, point).field_8();
        const std::int32_t cur_x = static_cast<std::int16_t>(gen_point_at(m_, point).field_0());
        const std::int32_t cur_z = static_cast<std::int16_t>(gen_point_at(m_, point).field_4());
        std::int32_t cross = (short_at(m_, next + 4) - cur_z) * (cur_x - mean_x);
        cross -= (short_at(m_, next) - cur_x) * (cur_z - mean_z);
        if (cross > 0) {
            return;
        }
        s_.check(next != gen_poly_at(m_, self).field_c(), kMsgRailLoop);
        point = next;
        if (cross != 0) {
            poly_reverse(self);
            return;
        }
    }
}

// 0x00441aa0: turn the chain or loop round (a loop keeps its first point as the head).
void GenObjs::poly_reverse(std::uint32_t self) {
    std::uint32_t point = gen_poly_at(m_, self).field_c();
    std::uint32_t before = 0;
    if (point != 0) {
        for (;;) {
            const std::uint32_t next = gen_point_at(m_, point).field_8();
            gen_point_at(m_, point).set_field_8(static_cast<Word>(before));
            const std::uint32_t head = gen_poly_at(m_, self).field_c();
            before = point;
            if (next == head) {
                break;
            }
            point = next;
            if (next == 0) {
                break;
            }
        }
    }
    if (gen_poly_at(m_, self).field_14() != 0) {
        set_word_at(m_, gen_poly_at(m_, self).field_c() + 8, before);
        return;
    }
    gen_poly_at(m_, self).set_field_c(static_cast<Word>(before));
}

// 0x00441ae0: copy `source`'s points turned to a rotation (0..3 quarter turns) and moved, each tagged with `tag`.
std::uint32_t GenObjs::poly_copy_transformed(std::uint32_t self, std::uint32_t source, std::uint32_t dx, std::uint32_t dy, std::uint32_t dz, std::uint32_t rotation,
                                             std::uint32_t tag) {
    s_.check(source != 0, kMsgNullRail);
    s_.check(word_at(m_, source + 0xc) != 0, kMsgEmptyRail);
    s_.check(static_cast<std::int32_t>(rotation) >= 0 && static_cast<std::int32_t>(rotation) < 4, kMsgRotation);
    gen_poly_at(m_, self).set_field_14(static_cast<std::uint8_t>(byte_at(m_, source + 0x14)));
    std::uint32_t from = word_at(m_, source + 0xc);
    std::uint32_t previous = 0;
    std::uint32_t node = 0;
    for (std::int32_t i = 0; i < int_at(m_, source + 0x18); ++i) {
        const std::uint32_t block = new_zeroed(0x18);
        node = block != 0 ? point_init(block) : 0;
        set_half_at(m_, node + 8, 0);
        set_half_at(m_, node + 0xa, 0);
        if (i == 0) {
            gen_poly_at(m_, self).set_field_c(static_cast<Word>(node));
        } else {
            set_word_at(m_, previous + 8, node);
        }
        const std::uint32_t x = half_at(m_, from);
        const std::uint32_t z = half_at(m_, from + 4);
        switch (rotation) {
        case 0:
            set_half_at(m_, node, x);
            set_half_at(m_, node + 4, z);
            break;
        case 1:
            set_half_at(m_, node, -z);
            set_half_at(m_, node + 4, x);
            break;
        case 2:
            set_half_at(m_, node, -x);
            set_half_at(m_, node + 4, -z);
            break;
        case 3:
            set_half_at(m_, node, z);
            set_half_at(m_, node + 4, -x);
            break;
        default:
            break;
        }
        const std::uint32_t y = half_at(m_, from + 2);
        set_half_at(m_, node, half_at(m_, node) + dx);
        set_half_at(m_, node + 4, half_at(m_, node + 4) + dz);
        set_half_at(m_, node + 2, y);
        set_half_at(m_, node + 2, half_at(m_, node + 2) + dy);
        previous = node;
        set_word_at(m_, node + 0xc, tag);
        set_word_at(m_, node + 0x10, word_at(m_, from + 0x10));
        from = word_at(m_, from + 8);
    }
    if (gen_poly_at(m_, self).field_14() != 0) {
        set_word_at(m_, node + 8, gen_poly_at(m_, self).field_c());
    }
    return poly_update_bounds(self);
}

// 0x00441cd0: if the two polygons' boxes touch (with 6 to spare), join them as loops (mode 0) or as open strings (mode 1).
std::uint32_t GenObjs::poly_try_merge(std::uint32_t self, std::uint32_t other, std::uint32_t mode) {
    if (gen_poly_at(m_, other).field_c() == 0 || gen_poly_at(m_, self).field_c() == 0) {
        return 0;
    }
    if (static_cast<std::int16_t>(gen_poly_at(m_, other).field_6()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_0()) - 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_0()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_6()) + 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_8()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_2()) - 6 ||
        static_cast<std::int16_t>(gen_poly_at(m_, other).field_2()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_8()) + 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_a()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_4()) - 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_4()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_a()) + 6) {
        return 0;
    }
    const std::uint32_t mode_byte = mode & 0xff;
    if (mode_byte == 0) {
        if (gen_poly_at(m_, self).field_14() != 0 && gen_poly_at(m_, other).field_14() != 0) {
            return poly_merge_loops(self, other);
        }
        return 0;
    }
    if (mode_byte == 1 && gen_poly_at(m_, self).field_14() == 0 && gen_poly_at(m_, other).field_14() == 0) {
        return poly_join_ends(self, other);
    }
    return 0;
}

// 0x00441da0: two open strings whose ends meet are made one string.
bool GenObjs::poly_join_ends(std::uint32_t self, std::uint32_t other) {
    s_.check(gen_poly_at(m_, self).field_14() == 0 && gen_poly_at(m_, other).field_14() == 0, kMsgNonLoops);
    std::uint32_t b_first = gen_poly_at(m_, other).field_c();
    std::uint32_t a_first = gen_poly_at(m_, self).field_c();
    std::uint32_t b_last = b_first;
    std::uint32_t b_prev = 0;
    for (std::uint32_t at = word_at(m_, b_first + 8); at != 0; at = word_at(m_, at + 8)) {
        b_prev = b_last;
        b_last = at;
    }
    std::uint32_t a_last = a_first;
    std::uint32_t a_prev = 0;
    for (std::uint32_t at = word_at(m_, a_first + 8); at != 0; at = word_at(m_, at + 8)) {
        a_prev = a_last;
        a_last = at;
    }
    if (point_near(b_first, a_first) || point_near(b_last, a_last)) {
        const std::uint32_t second = word_at(m_, b_first + 8);
        b_last = b_first;
        b_prev = second;
        poly_reverse(other);
        b_first = gen_poly_at(m_, other).field_c();
    }
    std::uint32_t finish_with = 0;
    if (point_near(a_first, b_last)) {
        s_.check(word_at(m_, a_first + 8) != 0, kMsgToNext);
        s_.check(b_prev != 0, kMsgFromLastPrev);
        if (point_near(b_prev, word_at(m_, a_first + 8)) || !edge_straight(b_prev, b_last, word_at(m_, a_first + 8), 0x800)) {
            return false;
        }
        set_word_at(m_, b_last + 8, word_at(m_, a_first + 8));
        set_word_at(m_, b_last + 0x10, word_at(m_, a_first + 0x10));
        set_word_at(m_, b_last + 0xc, word_at(m_, a_first + 0xc));
        delete_block(a_first);
        gen_poly_at(m_, self).set_field_c(static_cast<Word>(b_first));
        if (point_near(b_first, a_last)) {
            set_word_at(m_, a_last + 8, word_at(m_, b_first + 8));
            set_word_at(m_, a_last + 0x10, word_at(m_, b_first + 0x10));
            set_word_at(m_, a_last + 0xc, word_at(m_, b_first + 0xc));
            gen_poly_at(m_, self).set_field_c(static_cast<Word>(word_at(m_, b_first + 8)));
            delete_block(b_first);
            gen_poly_at(m_, self).set_field_14(1);
        }
        finish_with = self;
    } else {
        if (!point_near(b_first, a_last)) {
            return false;
        }
        s_.check(word_at(m_, b_first + 8) != 0, kMsgToNext);
        s_.check(a_prev != 0, kMsgToLastPrev);
        if (point_near(a_prev, word_at(m_, b_first + 8)) || !edge_straight(a_prev, a_last, word_at(m_, b_first + 8), 0x800)) {
            return false;
        }
        set_word_at(m_, a_last + 8, word_at(m_, b_first + 8));
        set_word_at(m_, a_last + 0x10, word_at(m_, b_first + 0x10));
        set_word_at(m_, a_last + 0xc, word_at(m_, b_first + 0xc));
        delete_block(b_first);
        if (point_near(a_first, b_last)) {
            set_word_at(m_, b_last + 8, word_at(m_, a_first + 8));
            set_word_at(m_, b_last + 0x10, word_at(m_, a_first + 0x10));
            set_word_at(m_, b_last + 0xc, word_at(m_, a_first + 0xc));
            gen_poly_at(m_, self).set_field_c(static_cast<Word>(word_at(m_, a_first + 8)));
            delete_block(a_first);
            gen_poly_at(m_, self).set_field_14(1);
        }
        finish_with = self;
    }
    poly_update_bounds(finish_with);
    gen_poly_at(m_, other).set_field_c(0);
    return true;
}

// 0x00442100: look round the loop `a` (twice about) for a run of marked points that follows an unmarked one: `first` is where the run starts (two marked in a row) and
// `last` its last marked point; then round the loop `b` for a point near `last` and, after it, one near `first`.
bool GenObjs::poly_edge_runs(std::uint32_t a, std::uint32_t b, std::int32_t count_a, std::int32_t count_b, std::uint32_t* first, std::uint32_t* last,
                             std::uint32_t* b_near_last, std::uint32_t* b_near_first) {
    std::uint32_t state = 0;
    std::uint32_t node = a;
    bool found = false;
    const std::int32_t steps = count_a + count_a;
    if (steps > 0) {
        for (std::int32_t i = 0; i < steps; ++i) {
            if (state == 0) {
                if (byte_at(m_, node + 0x14) == 0) {
                    state = 1;
                }
            } else if (state == 1) {
                if (byte_at(m_, node + 0x14) != 0 && byte_at(m_, word_at(m_, node + 8) + 0x14) != 0) {
                    *first = node;
                    state = 2;
                }
            } else if (state == 2) {
                if (byte_at(m_, node + 0x14) == 0) {
                    found = true;
                    break;
                }
                *last = node;
            } else if (state == 3) {
                found = true;
                break;
            }
            node = word_at(m_, node + 8);
        }
        if (!found && state == 3) {
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    std::uint32_t phase = 0;
    std::uint32_t at = b;
    const std::int32_t rounds = count_b + count_b;
    for (std::int32_t i = 0; i < rounds; ++i) {
        if (phase == 0) {
            if (point_near(at, *last)) {
                phase = 1;
                *b_near_last = at;
            }
        } else if (phase == 1) {
            if (byte_at(m_, at + 0x14) == 0) {
                phase = 3;
                break;
            }
            if (point_near(at, *first)) {
                phase = 2;
                *b_near_first = at;
                break;
            }
        } else {
            break;
        }
        at = word_at(m_, at + 8);
    }
    return phase == 2;
}

// 0x00442210: two loops that share a run of points are made one loop (the shared points are dropped).
std::uint32_t GenObjs::poly_merge_loops(std::uint32_t self, std::uint32_t other) {
    const std::uint32_t a_head = gen_poly_at(m_, self).field_c();
    const std::uint32_t b_head = gen_poly_at(m_, other).field_c();
    s_.check(gen_poly_at(m_, self).field_14() != 0 && gen_poly_at(m_, other).field_14() != 0, kMsgBothLoops);
    s_.check(word_at(m_, b_head + 8) != 0, kMsgSourceNotLoop);
    s_.check(word_at(m_, a_head + 8) != 0, kMsgDestNotLoop);
    s_.check(word_at(m_, word_at(m_, b_head + 8) + 8) != 0, kMsgSourceOne);
    s_.check(word_at(m_, word_at(m_, a_head + 8) + 8) != 0, kMsgDestOne);
    std::uint32_t at = b_head;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(gen_poly_at(m_, other).field_18()); ++i) {
        set_byte_at(m_, at + 0x14, 0);
        at = word_at(m_, at + 8);
    }
    at = a_head;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(gen_poly_at(m_, self).field_18()); ++i) {
        set_byte_at(m_, at + 0x14, 0);
        std::uint32_t b = b_head;
        for (std::int32_t j = 0; j < static_cast<std::int32_t>(gen_poly_at(m_, other).field_18()); ++j) {
            if (point_near(at, b)) {
                set_byte_at(m_, at + 0x14, 1);
                set_byte_at(m_, b + 0x14, 1);
            }
            b = word_at(m_, b + 8);
        }
        at = word_at(m_, at + 8);
    }
    std::uint32_t s10 = 0;
    std::uint32_t s14 = 0;
    std::uint32_t s18 = 0;
    std::uint32_t s30 = 0;
    if (!poly_edge_runs(a_head, b_head, static_cast<std::int32_t>(gen_poly_at(m_, self).field_18()), static_cast<std::int32_t>(gen_poly_at(m_, other).field_18()), &s14, &s30, &s18, &s10) &&
        !poly_edge_runs(b_head, a_head, static_cast<std::int32_t>(gen_poly_at(m_, other).field_18()), static_cast<std::int32_t>(gen_poly_at(m_, self).field_18()), &s18, &s10, &s14, &s30)) {
        return 0;
    }
    s_.check(s14 != 0, kMsgNoFirstTo);
    s_.check(s30 != 0, kMsgNoFirstTo);
    s_.check(s18 != 0, kMsgNoFirstFrom);
    s_.check(s10 != 0, kMsgNoFirstFrom);
    for (std::uint32_t p = word_at(m_, s14 + 8); p != s30;) {
        const std::uint32_t next = word_at(m_, p + 8);
        delete_block(p);
        p = next;
    }
    for (std::uint32_t p = word_at(m_, s18 + 8); p != s10;) {
        const std::uint32_t next = word_at(m_, p + 8);
        delete_block(p);
        p = next;
    }
    set_word_at(m_, s14 + 8, word_at(m_, s10 + 8));
    set_word_at(m_, s14 + 0x10, word_at(m_, s10 + 0x10));
    set_word_at(m_, s14 + 0xc, word_at(m_, s10 + 0xc));
    set_word_at(m_, s18 + 8, word_at(m_, s30 + 8));
    set_word_at(m_, s18 + 0x10, word_at(m_, s30 + 0x10));
    set_word_at(m_, s18 + 0xc, word_at(m_, s30 + 0xc));
    delete_block(s10);
    delete_block(s30);
    gen_poly_at(m_, self).set_field_c(static_cast<Word>(s14));
    poly_update_bounds(self);
    gen_poly_at(m_, other).set_field_c(0);
    return 1;
}

// 0x00442670: lay this open string over `other`: what a trimmed copy of this string leaves of `other` is cut out of it and joined back on; `rest` gets the piece of
// `other` that is left over (set only when the trimmed copy was made).
bool GenObjs::poly_cut_by(std::uint32_t self, std::uint32_t other, std::uint32_t& rest) {
    s_.check(!(gen_poly_at(m_, self).field_14() != 0 && gen_poly_at(m_, other).field_14() != 0), kMsgOneNonLoop);
    if (static_cast<std::int16_t>(gen_poly_at(m_, other).field_6()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_0()) - 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_0()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_6()) + 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_8()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_2()) - 6 ||
        static_cast<std::int16_t>(gen_poly_at(m_, other).field_2()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_8()) + 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_a()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_4()) - 6 || static_cast<std::int16_t>(gen_poly_at(m_, other).field_4()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_a()) + 6) {
        return false;
    }
    const std::uint32_t trimmed = poly_trim(self, other);
    if (trimmed == 0) {
        return false;
    }
    poly_cut_out(other, trimmed, rest);
    poly_update_bounds(self);
    const bool joined = poly_join_ends(self, trimmed);
    poly_free_ring(trimmed);
    delete_block(trimmed);
    return joined;
}

// 0x00442780: a loop that touches itself is split in two at the touching points; the second loop is returned (0 when there is nothing to split).
std::uint32_t GenObjs::poly_split(std::uint32_t self) {
    if (gen_poly_at(m_, self).field_14() == 0) {
        return 0;
    }
    std::int32_t count = static_cast<std::int32_t>(gen_poly_at(m_, self).field_18());
    std::uint32_t outer = gen_poly_at(m_, self).field_c();
    std::uint32_t last_outer = 0;           // ebx: the last outer point with a partner
    std::uint32_t first_outer = 0;          // [esp+0x14]
    std::uint32_t first_inner = 0;          // [esp+0x18]
    std::uint32_t last_inner = 0;           // [esp+0x1c]
    bool primed = false;
    if (count + count <= 0) {
        return 0;
    }
    for (std::int32_t i = 0; i < count + count; ++i) {
        std::uint32_t inner = gen_poly_at(m_, self).field_c();
        bool hit = false;
        for (std::int32_t j = 0; j < count; ++j) {
            if (point_near(outer, inner) && outer != inner) {
                if (primed) {
                    if (first_outer == 0) {
                        first_outer = outer;
                        first_inner = inner;
                    }
                    last_outer = outer;
                    last_inner = inner;
                }
                hit = true;
                break;
            }
            inner = word_at(m_, inner + 8);
            count = static_cast<std::int32_t>(gen_poly_at(m_, self).field_18());
        }
        if (!hit) {
            primed = last_outer == 0;
        }
        count = static_cast<std::int32_t>(gen_poly_at(m_, self).field_18());
        outer = word_at(m_, outer + 8);
    }
    if (first_outer == 0 || last_outer == 0) {
        return 0;
    }
    if (first_outer == last_outer) {
        const std::uint32_t block = new_zeroed(0x18);
        const std::uint32_t copy = block != 0 ? point_init(block) : 0;
        m_.copy(copy, first_outer, 0x18);
        set_word_at(m_, first_outer + 8, copy);
        last_outer = copy;
    }
    if (last_inner == first_inner) {
        const std::uint32_t block = new_zeroed(0x18);
        const std::uint32_t copy = block != 0 ? point_init(block) : 0;
        m_.copy(copy, last_inner, 0x18);
        first_inner = copy;
        set_word_at(m_, last_inner + 8, copy);
    }
    for (std::uint32_t p = word_at(m_, first_outer + 8); p != last_outer;) {
        const std::uint32_t next = word_at(m_, p + 8);
        delete_block(p);
        p = next;
    }
    for (std::uint32_t p = word_at(m_, last_inner + 8); p != first_inner;) {
        const std::uint32_t next = word_at(m_, p + 8);
        delete_block(p);
        p = next;
    }
    const std::uint32_t block = new_zeroed(0x1c);
    const std::uint32_t loop = block != 0 ? poly_init(block) : 0;
    set_word_at(m_, loop + 0xc, last_inner);
    set_word_at(m_, last_inner + 8, word_at(m_, last_outer + 8));
    delete_block(last_outer);
    set_byte_at(m_, loop + 0x14, 1);
    poly_update_bounds(loop);
    gen_poly_at(m_, self).set_field_c(static_cast<Word>(first_outer));
    set_word_at(m_, first_outer + 8, word_at(m_, first_inner + 8));
    delete_block(first_inner);
    poly_update_bounds(self);
    return loop;
}

// 0x00442970: recompute the box and the number of points (stopping at the end of a chain or on coming round to the head).
std::int32_t GenObjs::poly_update_bounds(std::uint32_t self) {
    s_.check(gen_poly_at(m_, self).field_c() != 0, kMsgHeadNull);
    s_.check(word_at(m_, gen_poly_at(m_, self).field_c() + 8) != 0, kMsgHeadSingle);
    const std::uint32_t head = gen_poly_at(m_, self).field_c();
    gen_poly_at(m_, self).set_field_0(0x2710);
    gen_poly_at(m_, self).set_field_2(0x2710);
    gen_poly_at(m_, self).set_field_4(0x2710);
    gen_poly_at(m_, self).set_field_18(0);
    gen_poly_at(m_, self).set_field_6(0xd8f0);
    gen_poly_at(m_, self).set_field_8(0xd8f0);
    gen_poly_at(m_, self).set_field_a(0xd8f0);
    std::uint32_t point = head;
    while (point != 0) {
        if (static_cast<std::int16_t>(gen_point_at(m_, point).field_0()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_0())) {
            gen_poly_at(m_, self).set_field_0(static_cast<std::uint16_t>(gen_point_at(m_, point).field_0()));
        }
        if (static_cast<std::int16_t>(gen_point_at(m_, point).field_0()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_6())) {
            gen_poly_at(m_, self).set_field_6(static_cast<std::uint16_t>(gen_point_at(m_, point).field_0()));
        }
        if (static_cast<std::int16_t>(gen_point_at(m_, point).field_2()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_2())) {
            gen_poly_at(m_, self).set_field_2(static_cast<std::uint16_t>(gen_point_at(m_, point).field_2()));
        }
        if (static_cast<std::int16_t>(gen_point_at(m_, point).field_2()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_8())) {
            gen_poly_at(m_, self).set_field_8(static_cast<std::uint16_t>(gen_point_at(m_, point).field_2()));
        }
        if (static_cast<std::int16_t>(gen_point_at(m_, point).field_4()) < static_cast<std::int16_t>(gen_poly_at(m_, self).field_4())) {
            gen_poly_at(m_, self).set_field_4(static_cast<std::uint16_t>(gen_point_at(m_, point).field_4()));
        }
        if (static_cast<std::int16_t>(gen_point_at(m_, point).field_4()) > static_cast<std::int16_t>(gen_poly_at(m_, self).field_a())) {
            gen_poly_at(m_, self).set_field_a(static_cast<std::uint16_t>(gen_point_at(m_, point).field_4()));
        }
        gen_poly_at(m_, self).set_field_18(static_cast<Word>(gen_poly_at(m_, self).field_18() + 1));
        point = gen_point_at(m_, point).field_8();
        if (point == head) {
            break;
        }
    }
    s_.check(static_cast<std::int32_t>(gen_poly_at(m_, self).field_18()) >= 2, kMsgTooFew);
    return static_cast<std::int32_t>(gen_poly_at(m_, self).field_18());
}

// 0x00442ae0: `kill` is an open string running through this one: the points of this string that touch it are marked, the first run of marked points is removed (what
// lies between its first and last point goes) and, when that cuts an open string in two, the second part becomes a polygon of its own (*rest).
bool GenObjs::poly_cut_out(std::uint32_t self, std::uint32_t kill, std::uint32_t& rest) {
    rest = 0;
    s_.check(gen_poly_at(m_, self).field_c() != 0 && word_at(m_, gen_poly_at(m_, self).field_c() + 8) != 0, kMsgPointListNull);
    s_.check(word_at(m_, kill + 0xc) != 0 && word_at(m_, word_at(m_, kill + 0xc) + 8) != 0, kMsgKillNull);
    s_.check(byte_at(m_, kill + 0x14) == 0, kMsgKillLoop);
    std::uint32_t at = gen_poly_at(m_, self).field_c();
    do {
        set_byte_at(m_, at + 0x14, 0);
        for (std::uint32_t k = word_at(m_, kill + 0xc); k != 0; k = word_at(m_, k + 8)) {
            if (point_near(at, k)) {
                set_byte_at(m_, at + 0x14, 1);
                break;
            }
        }
        at = word_at(m_, at + 8);
    } while (at != 0 && at != gen_poly_at(m_, self).field_c());
    const bool closed = gen_poly_at(m_, self).field_14() != 0;
    std::int32_t steps = closed ? static_cast<std::int32_t>(gen_poly_at(m_, self).field_18()) * 2 : static_cast<std::int32_t>(gen_poly_at(m_, self).field_18());
    std::uint32_t state = closed ? 0 : 1;
    std::uint32_t node = gen_poly_at(m_, self).field_c();
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    for (; steps > 0; --steps) {
        s_.check(node != 0, kMsgUndefined);
        if (state == 0) {
            if (byte_at(m_, node + 0x14) == 0) {
                state = 1;
            }
        } else if (state == 1) {
            if (byte_at(m_, node + 0x14) != 0) {
                first = node;
                state = 2;
            }
        } else if (state == 2) {
            if (byte_at(m_, node + 0x14) == 0) {
                state = 3;
            } else {
                last = node;
            }
        }
        node = word_at(m_, node + 8);
    }
    s_.check(first != 0 && last != 0, kMsgFirstShared);
    s_.check(first != last, kMsgSameShared);
    for (std::uint32_t p = word_at(m_, first + 8); p != last;) {
        const std::uint32_t next = word_at(m_, p + 8);
        delete_block(p);
        p = next;
    }
    set_word_at(m_, first + 8, 0);
    if (closed) {
        gen_poly_at(m_, self).set_field_14(0);
        gen_poly_at(m_, self).set_field_c(static_cast<Word>(last));
        poly_update_bounds(self);
        return true;
    }
    if (word_at(m_, gen_poly_at(m_, self).field_c() + 8) == 0) {
        delete_block(gen_poly_at(m_, self).field_c());
        gen_poly_at(m_, self).set_field_c(static_cast<Word>(last));
    }
    if (word_at(m_, last + 8) == 0) {
        delete_block(last);
        poly_update_bounds(self);
        return true;
    }
    if (gen_poly_at(m_, self).field_c() != last) {
        const std::uint32_t block = new_zeroed(0x1c);
        const std::uint32_t piece = block != 0 ? poly_init(block) : 0;
        rest = piece;
        set_word_at(m_, piece + 0xc, last);
        poly_update_bounds(piece);
        s_.check(int_at(m_, rest + 0x18) >= 2, kMsgFragment);
    }
    poly_update_bounds(self);
    return true;
}

// 0x00442e90: do the turns p -> q and q -> r keep within the angle whose cosine (Q12) is `limit`?
bool GenObjs::edge_straight(std::uint32_t p, std::uint32_t q, std::uint32_t r, std::int32_t limit) {
    const std::int32_t dx1 = short_at(m_, q) - short_at(m_, p);
    const std::int32_t dz1 = short_at(m_, q + 4) - short_at(m_, p + 4);
    const std::int32_t dx2 = short_at(m_, r) - short_at(m_, q);
    const std::int32_t dz2 = short_at(m_, r + 4) - short_at(m_, q + 4);
    const std::int32_t length1 = isqrt(dz1 * dz1 + dx1 * dx1);
    const std::int32_t x1 = static_cast<std::int32_t>(static_cast<std::uint32_t>(dx1) << 12) / length1;
    const std::int32_t z1 = static_cast<std::int32_t>(static_cast<std::uint32_t>(dz1) << 12) / length1;
    const std::int32_t length2 = isqrt(dz2 * dz2 + dx2 * dx2);
    const std::int32_t x2 = static_cast<std::int32_t>(static_cast<std::uint32_t>(dx2) << 12) / length2;
    const std::int32_t z2 = static_cast<std::int32_t>(static_cast<std::uint32_t>(dz2) << 12) / length2;
    return ((x2 * x1 + z2 * z1) >> 12) > limit;
}

// 0x00442f40: the string that this open string and `b`'s string leave when b's string is laid over it (a fresh polygon, or 0 if they do not meet).
std::uint32_t GenObjs::poly_trim(std::uint32_t self, std::uint32_t b) {
    if (gen_poly_at(m_, self).field_14() != 0) {
        return 0;
    }
    s_.check(gen_poly_at(m_, self).field_c() != 0 && word_at(m_, gen_poly_at(m_, self).field_c() + 8) != 0, kMsgTrimShort);
    std::uint32_t a_prev = gen_poly_at(m_, self).field_c();
    std::uint32_t a_last = word_at(m_, a_prev + 8);
    while (word_at(m_, a_last + 8) != 0) {
        a_prev = a_last;
        a_last = word_at(m_, a_prev + 8);
    }
    const std::uint32_t block = new_zeroed(0x1c);
    const std::uint32_t result = block != 0 ? poly_init(block) : 0;
    std::uint32_t a_node = b;
    std::uint32_t b_node = b;
    std::uint32_t kept = 0;
    bool hit = false;
    for (std::uint32_t at = word_at(m_, b + 0xc); at != 0;) {
        if (point_near(gen_poly_at(m_, self).field_c(), at)) {
            a_node = gen_poly_at(m_, self).field_c();
            kept = word_at(m_, a_node + 8);
            const std::uint32_t point_block = new_zeroed(0x18);
            const std::uint32_t point = point_block != 0 ? point_init(point_block) : 0;
            set_word_at(m_, result + 0xc, point);
            m_.copy(point, at, 0x18);
            set_word_at(m_, word_at(m_, result + 0xc) + 8, 0);
            b_node = at;
            hit = true;
            break;
        }
        if (point_near(word_at(m_, a_prev + 8), at)) {
            a_node = word_at(m_, a_prev + 8);
            kept = a_prev;
            const std::uint32_t point_block = new_zeroed(0x18);
            const std::uint32_t point = point_block != 0 ? point_init(point_block) : 0;
            set_word_at(m_, result + 0xc, point);
            m_.copy(point, at, 0x18);
            set_word_at(m_, word_at(m_, result + 0xc) + 8, 0);
            b_node = at;
            hit = true;
            break;
        }
        at = word_at(m_, at + 8);
        if (at == word_at(m_, b + 0xc)) {
            break;
        }
    }
    (void)hit;
    const auto drop = [&]() -> std::uint32_t {
        if (result != 0) {
            poly_free_ring(result);
            delete_block(result);
        }
        return 0;
    };
    if (word_at(m_, result + 0xc) == 0) {
        return drop();
    }
    for (;;) {
        // the points before and after b_node in b's string
        std::uint32_t before = word_at(m_, b + 0xc);
        if (before != 0) {
            while (word_at(m_, before + 8) != b_node) {
                before = word_at(m_, before + 8);
                if (before == 0) {
                    break;
                }
            }
        }
        std::uint32_t pair[2] = {before, word_at(m_, b_node + 8)};
        if (pair[0] != 0 && pair[1] != 0 && edge_straight(pair[0], b_node, pair[1], 0xb50)) {
            break;
        }
        std::uint32_t k = 0;
        for (; k < 2; ++k) {
            if (pair[k] != 0 && edge_straight(kept, a_node, pair[k], 0xb50)) {
                break;
            }
        }
        if (k == 2) {
            break;
        }
        const std::uint32_t point_block = new_zeroed(0x18);
        const std::uint32_t point = point_block != 0 ? point_init(point_block) : 0;
        const std::uint32_t chosen = pair[k];
        m_.copy(point, chosen, 0x18);
        gen_point_at(m_, point).set_field_8(0);
        const std::uint32_t previous_a = a_node;
        poly_append(result, point, 0);
        kept = previous_a;
        a_node = point;
        b_node = chosen;
    }
    if (word_at(m_, word_at(m_, result + 0xc) + 8) != 0) {
        poly_update_bounds(result);
        return result;
    }
    return drop();
}

// ---- lists of polygons -----------------------------------------------------------------------------------------------------

// 0x004431f0
std::uint32_t GenObjs::plist_init(std::uint32_t self) {
    gen_plist_at(m_, self).set_field_0(0);
    return self;
}

// 0x00443210: a new empty polygon at the end of the list (also kept at +4).
std::uint32_t GenObjs::plist_new(std::uint32_t self) {
    const std::uint32_t block = new_zeroed(0x1c);
    const std::uint32_t poly = block != 0 ? poly_init(block) : 0;
    gen_plist_at(m_, self).set_field_4(static_cast<Word>(poly));
    const std::uint32_t first = gen_plist_at(m_, self).field_0();
    if (first == 0) {
        gen_plist_at(m_, self).set_field_0(static_cast<Word>(poly));
        return poly;
    }
    std::uint32_t last = first;
    while (word_at(m_, last + 0x10) != 0) {
        last = word_at(m_, last + 0x10);
    }
    set_word_at(m_, last + 0x10, poly);
    return poly;
}

// 0x00443250 (0x00443200 jumps here)
void GenObjs::plist_free_all(std::uint32_t self) {
    std::uint32_t poly = gen_plist_at(m_, self).field_0();
    if (poly == 0) {
        gen_plist_at(m_, self).set_field_0(0);
        return;
    }
    std::uint32_t next;
    do {
        next = gen_poly_at(m_, poly).field_10();
        if (poly != 0) {
            poly_free_ring(poly);
            delete_block(poly);
        }
        poly = next;
    } while (next != 0);
    gen_plist_at(m_, self).set_field_0(static_cast<Word>(next));
}

// 0x00443290
std::uint32_t GenObjs::plist_count(std::uint32_t self) {
    std::uint32_t count = 0;
    for (std::uint32_t poly = gen_plist_at(m_, self).field_0(); poly != 0; poly = gen_poly_at(m_, poly).field_10()) {
        ++count;
    }
    return count;
}

// 0x004432b0: the number of points of all the polygons (their boxes are recomputed on the way).
std::uint32_t GenObjs::plist_points(std::uint32_t self) {
    std::uint32_t total = 0;
    for (std::uint32_t poly = gen_plist_at(m_, self).field_0(); poly != 0; poly = gen_poly_at(m_, poly).field_10()) {
        total += static_cast<std::uint32_t>(poly_update_bounds(poly));
    }
    return total;
}

// 0x004432d0: the polygons of `incoming` are laid against those of this list one at a time: the first polygon of `incoming` is tried against each polygon of this list; a
// polygon it joins is used up (1) or moved over to `incoming` (2), a polygon it neither joins nor cuts leaves it unchanged, and one that meets nothing is moved to the end of
// this list.  The late clock check asks the engine to show progress.
void GenObjs::plist_merge(std::uint32_t self, std::uint32_t incoming, std::uint32_t mode) {
    s_.check(incoming != 0, kMsgNullList);
    s_.check(word_at(m_, incoming) != 0, kMsgEmptySet);
    s_.check(word_at(m_, word_at(m_, incoming) + 0xc) != 0, kMsgEmptyString);
    if (word_at(m_, incoming) == 0) {
        return;
    }
    for (;;) {
        bool moved = false;
        std::uint32_t poly = gen_plist_at(m_, self).field_0();
        if (poly == 0) {
            gen_plist_at(m_, self).set_field_0(static_cast<Word>(plist_pop(incoming)));
            moved = true;
        } else {
            std::uint32_t before = 0;
            while (poly != 0) {
                const std::uint32_t result = plist_pair(incoming, poly, mode);
                if (result == 0) {
                    const std::uint32_t next = gen_poly_at(m_, poly).field_10();
                    if (next == 0) {
                        gen_poly_at(m_, poly).set_field_10(static_cast<Word>(plist_pop(incoming)));
                        moved = true;
                        break;
                    }
                    before = poly;
                    poly = next;
                    continue;
                }
                if (result == 1 || result == 2) {
                    if (before == 0) {
                        gen_plist_at(m_, self).set_field_0(static_cast<Word>(gen_poly_at(m_, poly).field_10()));
                    } else {
                        set_word_at(m_, before + 0x10, gen_poly_at(m_, poly).field_10());
                    }
                }
                if (result == 1) {
                    if (poly != 0) {
                        poly_free_ring(poly);
                        delete_block(poly);
                    }
                } else if (result == 2) {
                    plist_append(incoming, poly);
                } else if (result != 3) {
                    s_.check(false, kMsgUnexpected);
                }
                break;
            }
        }
        if (!moved) {
            continue;
        }
        const std::uint32_t late = global_word(m_, kLoadClock) > gen_plist_at(m_, self).field_8() + 0x3c ? 1 : 0;
        s_.note(pspool::service_name(kYield), {late});
        if (word_at(m_, incoming) == 0) {
            return;
        }
    }
}

// 0x004434e0: lay the first polygon of this list against `poly`: 1 the two were joined (the caller's polygon is used up), 3 and 2 the one cut the other (3: `poly` was cut
// by the list's polygon, 2: the other way round), 0 nothing happened.  A piece that is left over goes to the end of this list.
std::uint32_t GenObjs::plist_pair(std::uint32_t self, std::uint32_t poly, std::uint32_t mode) {
    if ((poly_try_merge(gen_plist_at(m_, self).field_0(), poly, mode) & 0xff) != 0) {
        if (const std::uint32_t split = poly_split(gen_plist_at(m_, self).field_0()); split != 0) {
            plist_append(self, split);
        }
        return 1;
    }
    if ((mode & 0xff) != 1) {
        return 0;
    }
    const std::uint32_t first = gen_plist_at(m_, self).field_0();
    if (byte_at(m_, first + 0x14) != 0 && gen_poly_at(m_, poly).field_14() != 0) {
        return 0;
    }
    std::uint32_t rest = 0;
    std::uint32_t result = 0;
    if (poly_cut_by(first, poly, rest)) {
        result = 3;
    } else if (poly_cut_by(poly, first, rest)) {
        result = 2;
    }
    if (rest != 0) {
        plist_append(self, rest);
    }
    return result;
}

// 0x00443580: take the first polygon off the list.
std::uint32_t GenObjs::plist_pop(std::uint32_t self) {
    s_.check(gen_plist_at(m_, self).field_0() != 0, kMsgPopEmpty);
    const std::uint32_t poly = gen_plist_at(m_, self).field_0();
    gen_plist_at(m_, self).set_field_0(static_cast<Word>(gen_poly_at(m_, poly).field_10()));
    gen_poly_at(m_, poly).set_field_10(0);
    return poly;
}

// 0x004435e0: put a polygon at the end of the list.
void GenObjs::plist_append(std::uint32_t self, std::uint32_t poly) {
    s_.check(poly != 0, kMsgAppendNull);
    s_.check(gen_poly_at(m_, poly).field_c() != 0, kMsgAppendEmpty);
    gen_poly_at(m_, poly).set_field_10(0);
    const std::uint32_t first = gen_plist_at(m_, self).field_0();
    if (first == 0) {
        gen_plist_at(m_, self).set_field_0(static_cast<Word>(poly));
        return;
    }
    std::uint32_t last = first;
    while (word_at(m_, last + 0x10) != 0) {
        last = word_at(m_, last + 0x10);
    }
    set_word_at(m_, last + 0x10, poly);
}

}  // namespace opentony::runtime::genobjs
