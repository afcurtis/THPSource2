#include "rail_views.hpp"

// Phase 4.15a: the rail set-up of rail.cpp (0x004aa5d0 Rail_CreateRails, 0x004aa8c0 the CRail constructor): the chains of rail nodes become runtime records, each with the bounding box of its nodes and,
// when segments are checked against the world, ended where the world says the rail does not go on.

namespace opentony::runtime::rail {

using namespace views;

namespace {

using Word = std::uint32_t;
using Vec = std::array<Word, 3>;

inline std::int32_t s(Word v) { return static_cast<std::int32_t>(v); }
inline Word u(std::int32_t v) { return static_cast<Word>(v); }

}  // namespace

RailKit::Vec RailKit::decode_position(Word node) {
    decode_node(node);
    const RuntimeIntVector3View position(m_, kPositionScratch);
    return {component(position, 0), component(position, 1), component(position, 2)};
}

// 0x00465f60: a Q12 unit vector. The vector is halved until every component is strictly inside +-0x6883 (so the squares fit), the length is the square root of the sum of the squares of
// the 16-bit components (the GTE's squaring of IR1..3: `gte_sqr0` leaves them in MAC1..3 and their clamp in IR1..3), and each component is (component << 12) / length; a zero
// length gives the x axis
void RailKit::normalize_fixed12(Vec& v) {
    const auto outside = [&v] {
        for (const Word w : v) {
            if (s(w) >= 0x6883 || s(w) <= -0x6883) {
                return true;
            }
        }
        return false;
    };
    while (outside()) {
        for (Word& w : v) {
            w = u(s(w) >> 1);
        }
    }
    for (Word k = 0; k < 3; ++k) {
        set_component(gte_file(m_).ir(), k, static_cast<std::int16_t>(v[k]));
    }
    math_.sqr0();
    const RuntimeIntVector3View mac = gte_file(m_).mac();
    const std::int32_t length = integer_sqrt(s(component(mac, 1) + component(mac, 2) + component(mac, 0)));
    if (length == 0) {
        v = {0x1000, 0, 0};
        return;
    }
    for (Word& w : v) {
        w = u(s(w << 12) / length);
    }
}

// 0x004aa8c0: the CRail constructor. The record joins the list, takes the kind and the initial state from the first node's trigger word, and follows the chain from there: every node that is
// reached makes a type-11 node (so the set-up does not start a second record there), widens the bounding box, and, when the segments are checked, is tested against the world: two horizontal
// probes across the segment's quarter point 10 units up (a wall: the rail ends), then two probes down on either side (floor on both: ends), then two long horizontal probes 20 units lower
// (nothing at all: ends). Where the chain ends for that reason the next node's trigger word gets its done bit, so no walk goes on through it.
Word RailKit::create_runtime_record(Word self, Word node) {
    constexpr Word kMsgRange = 0x005376c8;
    constexpr Word kMsgType = 0x005376a4;
    const RuntimeTriggerRailRecordView rail = record_at(m_, self);
    const RuntimeIntVector3View low = rail.bounds_minimum();
    const RuntimeIntVector3View high = rail.bounds_maximum();
    for (unsigned k = 0; k < 3; ++k) {
        set_component(low, k, 0);
    }
    for (unsigned k = 0; k < 3; ++k) {
        set_component(high, k, 0);
    }
    rail.set_vtable(GuestPtr<void>{record::kRailVtable});
    Word head = global_word(m_, globals::kListHead);
    if (head != 0) {
        record_at(m_, head).set_previous_rail_record(GuestPtr<RuntimeTriggerRailRecord>{self});
        head = global_word(m_, globals::kListHead);
    }
    rail.set_next_rail_record(GuestPtr<RuntimeTriggerRailRecord>{head});
    rail.set_previous_rail_record(GuestPtr<RuntimeTriggerRailRecord>{0});
    set_global_word(m_, globals::kListHead, self);
    check(s(node) >= 0 && s(node) < global_int(m_, globals::kNumNodes), kMsgRange);
    check(half_at(m_, node_record(m_, node)) == node::kRailStart || half_at(m_, node_record(m_, node)) == node::kRailPoint, kMsgType);
    rail.set_source_trg_node_index(static_cast<std::uint16_t>(node));
    rail.set_rail_kind(static_cast<std::uint8_t>(read_trigger_word(node) & 0xf));
    rail.set_rail_state((read_trigger_word(rail.source_trg_node_index()) & 0x40) != 0 ? 0 : 1);

    for (unsigned k = 0; k < 3; ++k) {
        set_component(low, k, 0x7fffffffu);
    }
    for (unsigned k = 0; k < 3; ++k) {
        set_component(high, k, 0x80000000u);
    }
    const Word first = u(sext16(rail.source_trg_node_index()));
    Word current = first;
    set_half_at(m_, node_record(m_, current), node::kRailStart);
    for (;;) {
        const Vec position = decode_position(current);
        for (unsigned k = 0; k < 3; ++k) {
            if (s(position[k]) < s(component(low, k))) {
                set_component(low, k, position[k]);
            }
            if (s(position[k]) > s(component(high, k))) {
                set_component(high, k, position[k]);
            }
        }
        const Word following = next_node(current);
        if ((following & 0xffff) == 0) {
            break;
        }
        const Word next = following & 0xffff;
        const Vec a = decode_position(current);
        const Vec b = decode_position(next);
        // c: the point a quarter of the way from a to b, 10 units up; d: the horizontal direction across the segment, 10 units long
        Vec c{a[0] + b[0], a[1] + b[1], a[2] + b[2]};
        for (Word& w : c) {
            w = u(s(w) / 2);
        }
        for (unsigned k = 0; k < 3; ++k) {
            c[k] -= a[k];
        }
        for (Word& w : c) {
            w = u(s(w) / 2);
        }
        for (unsigned k = 0; k < 3; ++k) {
            c[k] += a[k];
        }
        c[1] -= 0xa000;
        Vec d{b[0] - a[0], 0, b[2] - a[2]};
        normalize_fixed12(d);
        d = {d[2], d[1], 0 - d[0]};
        for (Word& w : d) {
            w *= 10;
        }
        set_global_word(m_, collision::globals::kQueryFlagB, 1);
        set_global_word(m_, collision::globals::kQueryFlagA, 1);
        Word hit = 0;
        const bool checked = global_byte(m_, globals::kCheckSegments) != 0;
        if (checked) {
            const Word s1[3] = {c[0] + d[0], c[1], c[2] + d[2]};
            const Word e1[3] = {c[0] - d[0], c[1], c[2] - d[2]};
            hit = query_line(s1, e1);
            if (hit == 0) {
                hit = query_line(e1, s1);
            }
        }
        Word floor_left = 0;
        if (global_byte(m_, globals::kCheckSegments) != 0) {
            if (hit == 0) {
                // floor under the points two directions across, 30 units down: floor on both sides ends the rail
                d[0] *= 2;
                d[2] *= 2;
                const Word s2[3] = {c[0] - d[0], c[1], c[2] - d[2]};
                const Word e2[3] = {c[0] - d[0], c[1] + 0x1e000, c[2] - d[2]};
                hit = query_line(s2, e2);
                if (hit != 0) {
                    floor_left = 1;
                }
                const Word s3[3] = {c[0] + d[0], c[1], c[2] + d[2]};
                const Word e3[3] = {c[0] + d[0], c[1] + 0x1e000, c[2] + d[2]};
                hit = query_line(s3, e3);
                if (hit != 0 && floor_left == 0) {
                    hit = 0;
                }
            }
            if (global_byte(m_, globals::kCheckSegments) != 0 && hit == 0) {
                // two long probes across, 20 units lower: nothing in either direction ends the rail
                d[0] *= 2;
                d[2] *= 2;
                c[1] += 0x14000;
                const Word s4[3] = {c[0] + d[0], c[1], c[2] + d[2]};
                const Word e4[3] = {c[0] - d[0], c[1], c[2] - d[2]};
                if (query_line(s4, e4) != 0) {
                    hit = 0;
                } else {
                    hit = query_line(e4, s4) == 0 ? 0xffffffffu : 0;
                }
            }
        }
        set_global_word(m_, collision::globals::kQueryFlagB, 0);
        if (global_byte(m_, globals::kCheckSegments) == 0 || hit == 0) {
            set_half_at(m_, node_record(m_, next), node::kRailStart);
            next_node(next);
            if (next == first) {
                break;
            }
            current = next;
            continue;
        }
        const Word flags = flags_pointer(following);
        set_byte_at(m_, flags + 1, byte_at(m_, flags + 1) | 0x80);
        break;
    }
    return self;
}

// 0x004aa5d0: all rails of the level. Every type-10/11 node is made a type-10 node, the lookup table numbers the rail nodes, and then, in node order, every rail node that still is a type-10 node
// (no earlier chain went through it) starts a record: at the head of its chain, found by walking back through the lookup table to a node without a predecessor or one marked done.
void RailKit::create_rails() {
    constexpr Word kMsgNodes = 0x0053768c;
    constexpr Word kMsgFirst = 0x00537660;
    constexpr Word kMsgEntry = 0x0053764c;
    check(global_int(m_, globals::kNumNodes) > 0, kMsgNodes);
    flush_cache();
    {
        const std::uint16_t type = static_cast<std::uint16_t>(half_at(m_, node_record(m_, 0)));
        check(type != node::kRailStart && type != node::kRailPoint, kMsgFirst);
    }
    if (global_int(m_, globals::kNumNodes) > 0) {
        Word index = 0;
        do {
            const Word record = node_record(m_, index);
            const std::int32_t type = sext16(half_at(m_, record));
            if (type >= node::kRailPoint && type <= node::kRailStart) {
                set_half_at(m_, record, node::kRailPoint);
            }
            index = (index + 1) & 0xffff;
        } while (index < global_word(m_, globals::kNumNodes));
    }
    setup_lookup();
    Word slot = 1;  // the lookup entry of the next rail node (entry 0 is not a rail)
    if (global_int(m_, globals::kNumNodes) > 0) {
        Word i = 0;
        do {
            const Word is_rail = is_rail_node(i);
            const Word record = node_record(m_, i & 0xffff);
            if (half_at(m_, record) == node::kRailPoint && is_rail != 0) {
                Word table = global_word(m_, globals::kLookup);
                Word entry = lookup_entry(m_, table, slot).leads_from();
                Word before = lookup_entry(m_, table, entry).node();
                Word head = i;
                const bool done = (read_trigger_word(i) & word::kDone) != 0;
                if (!done && before != 0) {
                    for (;;) {
                        check(static_cast<std::int32_t>(entry) < global_int(m_, globals::kLookupCount), kMsgEntry);
                        const Word previous = before;
                        table = global_word(m_, globals::kLookup);
                        entry = lookup_entry(m_, table, entry).leads_from();
                        before = lookup_entry(m_, table, entry).node();
                        if (before == (i & 0xffff)) {
                            head = i;
                            break;
                        }
                        head = previous;
                        if ((read_trigger_word(previous) & word::kDone) != 0) {
                            break;
                        }
                        if (before == 0) {
                            break;
                        }
                    }
                }
                const Word object = engine_value(kOperatorNew, {0x28});
                if (object != 0) {
                    create_runtime_record(object, head & 0xffff);
                }
            }
            if (is_rail != 0) {
                ++slot;
            }
            ++i;
        } while ((i & 0xffff) < global_word(m_, globals::kNumNodes));
    }
    engine(kMemDelete, {global_word(m_, globals::kLookup)});
}

}  // namespace opentony::runtime::rail
