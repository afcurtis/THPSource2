#include "gen_views.hpp"

namespace opentony::runtime::genobjs {

using namespace views;

// faceremoval.cpp: the points of a face are 0x18 bytes from a small pool: three 32-bit coordinates (x, height, depth), +0xc a state byte (1 a corner of the original,
// 2 a point made by a cut), +0x10 the next point of the ring and +0x14 a partner.
namespace {
constexpr std::uint32_t kPoolBlock = 0x005611b0;
constexpr std::uint32_t kPoolCursor = 0x00561164;
constexpr std::uint32_t kPoolOverflow = 0x0056115c;
constexpr std::uint32_t kPoolUsed = 0x00561190;
constexpr std::uint32_t kPointBytes = 0x18;
constexpr std::uint32_t kMsgNoPool = 0x0052b4fc;
constexpr std::uint32_t kMsgOverflow = 0x0052b55c;
constexpr std::uint32_t kMsgUnderflow = 0x0052b54c;
constexpr std::uint32_t kLogPoint = 0x0052b540;
constexpr std::uint32_t kLogHeader = 0x0052b578;
constexpr std::uint32_t kLogCorner = 0x0052b56c;
constexpr std::uint32_t kLogFooter = 0x0052a848;

std::int32_t mul(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)); }
}  // namespace

// 0x004436e0: the pool for a run of face removals.
void GenObjs::face_setup() {
    const std::uint32_t block = alloc(0x480, 1, 1, 0);
    set_global_word(m_, kPoolBlock, block);
    set_global_word(m_, kPoolCursor, block);
    set_global_byte(m_, kPoolOverflow, 0);
}

// 0x00443710
void GenObjs::face_cleanup() {
    free_block(global_word(m_, kPoolBlock));
    set_global_word(m_, kPoolBlock, 0);
}

// 0x00443730: the next point of the pool (more than 40 in one run sets the overflow flag).
std::uint32_t GenObjs::point_new() {
    s_.check(global_word(m_, kPoolBlock) != 0, kMsgNoPool);
    if (global_int(m_, kPoolUsed) >= 0x28) {
        set_global_byte(m_, kPoolOverflow, 1);
    }
    set_global_word(m_, kPoolUsed, global_word(m_, kPoolUsed) + 1);
    const std::uint32_t at = global_word(m_, kPoolCursor);
    set_byte_at(m_, at + 0xc, 1);
    set_word_at(m_, at + 0x10, 0);
    set_word_at(m_, at + 0x14, 0);
    set_global_word(m_, kPoolCursor, at + kPointBytes);
    return at;
}

// 0x004437d0: start again from the first point of the pool.
void GenObjs::points_reset() {
    set_global_word(m_, kPoolCursor, global_word(m_, kPoolBlock));
    set_global_word(m_, kPoolUsed, 0);
    set_global_byte(m_, kPoolOverflow, 0);
}

// 0x004437f0: log a point ('*' for a corner of the original, '/' for a made one).
void GenObjs::point_print(std::uint32_t point) { log(kLogPoint); (void)point; }

// 0x00443eb0: do two points lie within 3 of each other in every coordinate?
bool GenObjs::points_equal(std::uint32_t p, std::uint32_t q) {
    for (std::uint32_t offset = 0; offset <= 8; offset += 4) {
        const std::int32_t d = int_at(m_, p + offset) - int_at(m_, q + offset);
        if (d > 3 || d < -3) {
            return false;
        }
    }
    return true;
}

// 0x00443f00: the area of a ring of points in (x, height).
std::int32_t GenObjs::points_area(std::uint32_t ring) {
    std::int32_t sum = 0;
    std::uint32_t at = ring;
    do {
        const std::uint32_t next = word_at(m_, at + 0x10);
        sum += mul(int_at(m_, next + 4), int_at(m_, at)) - mul(int_at(m_, at + 4), int_at(m_, next));
        at = next;
    } while (at != ring);
    sum >>= 1;
    return sum < 0 ? -sum : sum;
}

// 0x00443f30: (d.y - c.y) * (b.x - a.x) - (b.y - a.y) * (d.x - c.x)
std::int32_t GenObjs::points_cross(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    std::int32_t value = mul(int_at(m_, d + 4) - int_at(m_, c + 4), int_at(m_, b) - int_at(m_, a));
    value -= mul(int_at(m_, b + 4) - int_at(m_, a + 4), int_at(m_, d) - int_at(m_, c));
    return value;
}

// 0x00443f70: do the directions a -> b and c -> d point the same way (a dot product that is not negative)?
bool GenObjs::points_side(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    std::int32_t value = mul(int_at(m_, b + 4) - int_at(m_, a + 4), int_at(m_, d + 4) - int_at(m_, c + 4));
    value += mul(int_at(m_, d) - int_at(m_, c), int_at(m_, b) - int_at(m_, a));
    return value >= 0;
}

// 0x00443fc0: log a ring of points.
void GenObjs::ring_print(std::uint32_t start) {
    log(kLogHeader);
    std::uint32_t at = start;
    do {
        log(kLogCorner);
        at = word_at(m_, at + 0x10);
    } while (at != start);
    log(kLogFooter);
}

// 0x00443820: lay two faces of one plane over each other: the rings are clipped against each other point by point, new points are made where their edges cross or
// overlap, and the area of what the two share is returned (0 when they do not meet, when the pool overflowed or when no clean ring comes out).
std::int32_t GenObjs::faces_overlap(std::uint32_t ring_a, std::uint32_t ring_b) {
    const std::uint32_t rings[2] = {ring_a, ring_b};
    if (int_at(m_, ring_a + 8) - int_at(m_, ring_b + 8) > 1 || int_at(m_, ring_b + 8) - int_at(m_, ring_a + 8) > 1) {
        return 0;
    }
    std::uint32_t best = 0;
    std::uint32_t best_pass = 0;
    for (std::int32_t pass = 1; pass >= 0; --pass) {
        const std::uint32_t p0 = rings[1 - pass];
        const std::uint32_t q0 = rings[pass];
        std::uint32_t p = p0;
        do {
            std::uint32_t q = q0;
            do {
                const std::uint32_t q_next = word_at(m_, q + 0x10);
                const std::int32_t outside = mul(int_at(m_, p + 4) - int_at(m_, q_next + 4), int_at(m_, q_next) - int_at(m_, q)) -
                                             mul(int_at(m_, q_next + 4) - int_at(m_, q + 4), int_at(m_, p) - int_at(m_, q_next));
                if (outside < 0 && byte_at(m_, p + 0xc) == 1) {
                    set_byte_at(m_, p + 0xc, 0);
                }
                const std::uint32_t r = word_at(m_, p + 0x10);
                std::uint32_t out1 = 0;
                std::uint32_t out2 = 0;
                const std::uint32_t s = word_at(m_, q + 0x10);
                const std::int32_t c7 = mul(int_at(m_, q + 4) - int_at(m_, r + 4), int_at(m_, r) - int_at(m_, p)) - mul(int_at(m_, r + 4) - int_at(m_, p + 4), int_at(m_, q) - int_at(m_, r));
                const std::int32_t c8 = points_cross(p, r, r, s);
                const std::int32_t c9 = points_cross(q, s, s, p);
                const std::int32_t c10 = points_cross(q, s, s, r);
                bool skip = false;
                if (c7 == 0 && c8 == 0 && c9 == 0 && c10 == 0) {
                    const bool s1 = points_side(p, r, r, q);
                    const bool s2 = points_side(p, r, r, s);
                    const bool s3 = points_side(r, p, p, q);
                    const bool s4 = points_side(r, p, p, s);
                    const bool s5 = points_side(q, s, s, p);
                    const bool s6 = points_side(q, s, s, r);
                    const bool s7 = points_side(s, q, q, p);
                    const bool s8 = points_side(s, q, q, r);
                    auto copy_of = [&](std::uint32_t source) {
                        const std::uint32_t point = point_new();
                        set_word_at(m_, point, word_at(m_, source));
                        set_word_at(m_, point + 4, word_at(m_, source + 4));
                        set_word_at(m_, point + 8, word_at(m_, source + 8));
                        return point;
                    };
                    if (s1 == s2) {
                        if (s3 == s4 && s5 != s6 && s7 != s8) {
                            out1 = copy_of(q);
                            out2 = copy_of(s);
                        } else if (s3 == s4 || s5 == s6 || s7 != s8) {
                            skip = true;
                        } else {
                            out1 = copy_of(s);
                        }
                    } else if (s3 != s4 || s5 != s6 || s7 == s8) {
                        skip = true;
                    } else {
                        out1 = copy_of(q);
                    }
                    if (!skip) {
                        if (out1 != 0 && (points_equal(out1, p) || points_equal(out1, word_at(m_, p + 0x10)))) {
                            out1 = 0;
                        }
                        if (out2 != 0 && (points_equal(out2, p) || points_equal(out2, word_at(m_, p + 0x10)))) {
                            out2 = 0;
                        }
                        if (out1 == 0) {
                            if (out2 != 0) {
                                out1 = out2;
                            }
                        } else if (out2 != 0) {
                            set_word_at(m_, out1 + 0x10, out2);
                        }
                    }
                } else {
                    const bool opposite_a = !((c7 > 0 && c8 > 0) || (c7 < 0 && c8 < 0));
                    const bool opposite_b = !((c9 > 0 && c10 > 0) || (c9 < 0 && c10 < 0));
                    if (!points_equal(p, q) && !points_equal(p, word_at(m_, q + 0x10)) && !points_equal(word_at(m_, p + 0x10), q) &&
                        !points_equal(word_at(m_, p + 0x10), word_at(m_, q + 0x10)) && opposite_a && opposite_b) {
                        out1 = point_new();
                        const std::int32_t d1 = int_at(m_, r) - int_at(m_, p);
                        const std::int32_t d2 = int_at(m_, r + 4) - int_at(m_, p + 4);
                        const std::int32_t t = mul(int_at(m_, q + 4) - int_at(m_, p + 4), d1) - mul(int_at(m_, q) - int_at(m_, p), d2);
                        const std::int32_t den = mul(int_at(m_, s) - int_at(m_, q), d2) - mul(int_at(m_, s + 4) - int_at(m_, q + 4), d1);
                        const std::int32_t u = mul(t, 0x40) / (den >> 4);
                        s_.check(t > -0x2000000 && t < 0x2000000, kMsgOverflow);
                        s_.check(den != 0, kMsgUnderflow);
                        set_word_at(m_, out1, static_cast<std::uint32_t>((mul(int_at(m_, s) - int_at(m_, q), u) >> 10) + int_at(m_, q)));
                        set_word_at(m_, out1 + 4, static_cast<std::uint32_t>((mul(int_at(m_, s + 4) - int_at(m_, q + 4), u) >> 10) + int_at(m_, q + 4)));
                        set_word_at(m_, out1 + 8, word_at(m_, q + 8));
                        if (points_equal(out1, p) || points_equal(out1, word_at(m_, p + 0x10))) {
                            out1 = 0;
                        }
                    }
                }
                if (global_byte(m_, kPoolOverflow) != 0) {
                    return 0;
                }
                if (out1 != 0) {
                    set_byte_at(m_, out1 + 0xc, 2);
                    while (word_at(m_, out1 + 0x10) != 0) {
                        set_byte_at(m_, out1 + 0xc, 2);
                        out1 = word_at(m_, out1 + 0x10);
                    }
                    set_word_at(m_, out1 + 0x10, word_at(m_, p + 0x10));
                    set_word_at(m_, p + 0x10, out1);
                }
                q = word_at(m_, q + 0x10);
            } while (q != q0);
            if (byte_at(m_, p + 0xc) != 0 && best == 0) {
                best_pass = static_cast<std::uint32_t>(pass);
                best = p;
            }
            p = word_at(m_, p + 0x10);
        } while (p != p0);
    }
    if (best == 0) {
        return 0;
    }
    const std::uint32_t ring = rings[best_pass];
    std::uint32_t at = best;
    for (;;) {
        std::uint32_t other = ring;
        do {
            if (points_equal(at, other)) {
                set_byte_at(m_, at + 0xc, 1);
                set_byte_at(m_, other + 0xc, 1);
                set_word_at(m_, at + 0x14, word_at(m_, other + 0x10));
                set_word_at(m_, other + 0x14, word_at(m_, at + 0x10));
                break;
            }
            other = word_at(m_, other + 0x10);
        } while (other != ring);
        at = word_at(m_, at + 0x10);
        if (at == best) {
            break;
        }
    }
    std::uint32_t walk = best;
    for (;;) {
        const std::uint32_t partner = word_at(m_, walk + 0x14);
        if (partner != 0) {
            if (byte_at(m_, word_at(m_, walk + 0x10) + 0xc) == 0 && byte_at(m_, partner + 0xc) == 0) {
                return 0;
            }
            if (byte_at(m_, partner + 0xc) != 0) {
                set_word_at(m_, walk + 0x10, partner);
            }
        }
        if (points_equal(word_at(m_, walk + 0x10), best)) {
            set_word_at(m_, walk + 0x10, best);
            return points_area(best);
        }
        walk = word_at(m_, walk + 0x10);
    }
}

}  // namespace opentony::runtime::genobjs
