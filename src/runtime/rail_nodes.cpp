#include "rail_views.hpp"

// Phase 4.15a: the node accessors, the lookup table and the runtime record list of rail.cpp (0x004a9f70..0x004aa8c0, 0x004aaf10, 0x004aaf30).

namespace opentony::runtime::rail {

using namespace views;

namespace {

using Word = std::uint32_t;

}  // namespace

// a service that filled `bytes` bytes at `destination`: the probe scripted them as dwords under the service's name, the last one padded
void RailKit::take_filled(Word destination, Word bytes, const std::string& service) {
    if (s_.live()) {
        return;  // (the real service already filled the bytes; the scripted words are replay-only)
    }
    std::vector<std::uint8_t> data;
    for (Word k = 0; k < (bytes + 3) / 4; ++k) {
        const auto word = static_cast<Word>(s_.value(service));
        for (unsigned b = 0; b < 4; ++b) {
            data.push_back(static_cast<std::uint8_t>(word >> (8 * b)));
        }
    }
    data.resize(bytes);
    if (bytes != 0) {
        m_.write(destination, data);
    }
}

Word RailKit::decode_node(Word node) {
    const std::string service = pcplatform::engine_name(kDecodeNodePosition);
    s_.note(service, {kPositionScratch, node});
    take_filled(kPositionScratch, 12, service);
    return static_cast<Word>(s_.value(service));
}

// 0x004a9f70: the word after the node's position (6 bytes further on from the level 10 on, the level 13 excepted); 0xffff reads as 0
Word RailKit::trigger_pointer(Word node) {
    Word at = decode_node(node & 0xffff);
    const std::int32_t level = global_int(m_, globals::kCurrentLevel);
    if (!(level < 10) && level != 13) {
        at += 6;
    }
    return at;
}

Word RailKit::read_trigger_word(Word node) {
    const Word at = trigger_pointer(node);
    const Word value = half_at(m_, at);
    return value == 0xffff ? 0 : value;
}

// 0x004a9fd0
Word RailKit::rail_type(Word node) { return read_trigger_word(node) & word::kTypeMask; }

// 0x004a9ff0: the address of the trigger word, 0 when it is "none"
Word RailKit::flags_pointer(Word node) {
    const Word at = trigger_pointer(node);
    return half_at(m_, at) == 0xffff ? 0 : at;
}

// 0x004aa040: a type-10/11 node used in this player count (the word's bits 4 and 5 are a mask of the player counts; 0 means all)
Word RailKit::is_rail_node(Word node) {
    const Word record = node_record(m_, node & 0xffff);
    const std::uint16_t type = static_cast<std::uint16_t>(half_at(m_, record));
    if (type != node::kRailStart && type != node::kRailPoint) {
        return 0;
    }
    const Word players = (read_trigger_word(node) >> 4) & 3;
    if (players == 0) {
        return 1;
    }
    return (players & global_word(m_, globals::kNumberOfPlayers)) != 0 ? 1 : 0;
}

// 0x004aa090: the index-th link of a node (three assertions: the node, the index, the linked node)
Word RailKit::get_link(Word node, Word index) {
    const Word id = node & 0xffff;
    constexpr Word kMsgNode = 0x005375d0;
    constexpr Word kMsgIndex = 0x005375bc;
    constexpr Word kMsgLink = 0x00537588;
    check(static_cast<std::int32_t>(id) < global_int(m_, globals::kNumNodes), kMsgNode);
    s_.note(pcplatform::engine_name(kGetNodeLinks), {id});
    const Word links = static_cast<Word>(s_.value(pcplatform::engine_name(kGetNodeLinks)));
    const std::int32_t i = sext16(index);
    check(i >= 0 && i < static_cast<std::int32_t>(half_at(m_, links)), kMsgIndex);
    const std::int32_t link = sext16(half_at(m_, links + 2 + 2 * static_cast<Word>(i)));
    check(link >= 0 && link < global_int(m_, globals::kNumNodes), kMsgLink);
    return static_cast<Word>(link) & 0xffff;
}

// 0x004aa1c0: the number of links of a node
Word RailKit::num_links(Word node) {
    constexpr Word kMsgNode = 0x00537604;
    const Word id = node & 0xffff;
    check(static_cast<std::int32_t>(id) < global_int(m_, globals::kNumNodes), kMsgNode);
    s_.note(pcplatform::engine_name(kGetNodeLinks), {id});
    const Word links = static_cast<Word>(s_.value(pcplatform::engine_name(kGetNodeLinks)));
    return half_at(m_, links);
}

// 0x004aa220: the node the rail continues at: of the rail-node links the last one whose trigger word is not yet marked done (0: the rail ends)
Word RailKit::next_node(Word node) {
    const Word count = num_links(node) & 0xffff;
    if (count == 0) {
        return 0;
    }
    Word next = 0;
    for (Word i = 0; i < count; ++i) {
        const Word link = get_link(node, i);
        if (is_rail_node(link) != 0 && (read_trigger_word(link) & word::kDone) == 0) {
            next = link & 0xffff;
        }
    }
    return next;
}

// 0x004aa290: the first rail node (searched from 1) whose rail continues at `node`; 0 when none does
Word RailKit::prev_node(Word node) {
    constexpr Word kMsgNode = 0x00537604;
    check(static_cast<std::int32_t>(node & 0xffff) < global_int(m_, globals::kNumNodes), kMsgNode);
    Word at = 1;
    while (at < global_word(m_, globals::kNumNodes)) {
        if (is_rail_node(at) != 0 && (next_node(at) & 0xffff) == (node & 0xffff)) {
            return at & 0xffff;
        }
        ++at;
    }
    return 0;
}

// 0x004aa330: the node before `node` found by following the rail forward from `start` (0 at the end of the rail or when it comes round to `start`)
Word RailKit::prev_node_quick(Word node, Word start) {
    constexpr Word kMsgNode = 0x00537604;
    check(static_cast<std::int32_t>(node & 0xffff) < global_int(m_, globals::kNumNodes), kMsgNode);
    Word at = start;
    for (;;) {
        const Word next = next_node(at) & 0xffff;
        if (next == (node & 0xffff)) {
            return at & 0xffff;
        }
        if (next == 0 || next == (start & 0xffff)) {
            return 0;
        }
        at = next;
    }
}

// 0x004aa3c0: sets the state byte of the record whose chain holds `node`
void RailKit::set_state(Word node, Word state) {
    Word record = global_word(m_, globals::kListHead);
    while (record != 0) {
        const RuntimeTriggerRailRecordView rail = record_at(m_, record);
        Word at = static_cast<Word>(sext16(rail.source_trg_node_index()));
        for (;;) {
            if (at == node) {
                rail.set_rail_state(static_cast<std::uint8_t>(state));
                return;
            }
            at = next_node(at) & 0xffff;
            if (at == 0 || at == static_cast<Word>(sext16(rail.source_trg_node_index()))) {
                break;
            }
        }
        record = rail.next_rail_record().address;
    }
}

// 0x004aa430: the index of a node in the lookup table (entry 0 is not a rail)
Word RailKit::index_of_node(Word node) {
    constexpr Word kMsgMissing = 0x00537618;
    const std::uint16_t wanted = static_cast<std::uint16_t>(node);
    if (wanted == 0) {
        return 0;
    }
    const std::int32_t count = global_int(m_, globals::kLookupCount);
    Word at = 1;
    if (count > 1) {
        const Word table = global_word(m_, globals::kLookup);
        for (; at < static_cast<Word>(count); ++at) {
            if (lookup_entry(m_, table, at).node() == wanted) {
                return at;
            }
        }
    }
    check(false, kMsgMissing);
    return 0;
}

// 0x004aa4b0: numbers the rail nodes (entry 1 up) and, per entry, records which entry leads to it
void RailKit::setup_lookup() {
    constexpr Word kMsgRoom = 0x0053762c;
    s_.note(pcplatform::engine_name(kAllocLoadedBuffer), {kLookupBytes, 1, 1, 0});
    set_global_word(m_, globals::kLookup, static_cast<Word>(s_.value(pcplatform::engine_name(kAllocLoadedBuffer))));
    Word counter = 1;
    if (global_int(m_, globals::kNumNodes) > 0) {
        Word node = 0;
        do {
            if (is_rail_node(node) != 0) {
                check((counter & 0xffff) < kLookupEntries, kMsgRoom);
                const Word at = counter & 0xffff;
                ++counter;
                lookup_entry(m_, global_word(m_, globals::kLookup), at).set_node(static_cast<std::uint16_t>(node));
                lookup_entry(m_, global_word(m_, globals::kLookup), at).set_leads_from(0);
            }
            ++node;
        } while ((node & 0xffff) < global_word(m_, globals::kNumNodes));
    }
    counter &= 0xffff;
    lookup_entry(m_, global_word(m_, globals::kLookup), 0).set_node(0);
    set_global_word(m_, globals::kLookupCount, counter);
    if (counter > 1) {
        Word entry = 1;
        do {
            const Word node = lookup_entry(m_, global_word(m_, globals::kLookup), entry).node();
            const Word target = index_of_node(next_node(node));
            if (target != 0) {
                lookup_entry(m_, global_word(m_, globals::kLookup), target).set_leads_from(static_cast<std::uint16_t>(entry));
            }
            ++entry;
        } while ((entry & 0xffff) < global_word(m_, globals::kLookupCount));
    }
}

// ---- the runtime records ----------------------------------------------------------------------------------------------------------------------------------------------------

// the virtual destructor (slot 0, one argument: 1 deletes the block)
void RailKit::destroy_object(Word object) {
    const Word target = word_at(m_, record_at(m_, object).vtable().address);
    if (target == record::kDestructor) {
        crail_destruct(object, 1);
    } else {
        virtual_call(object, 0, {1});
    }
}

// 0x004aa870
void RailKit::rail_init() {
    while (global_word(m_, globals::kListHead) != 0) {
        destroy_object(global_word(m_, globals::kListHead));
    }
    set_global_word(m_, globals::kListHead, 0);
}

// 0x004aa8a0
void RailKit::delete_all() {
    Word object = global_word(m_, globals::kListHead);
    while (object != 0) {
        const Word next = record_at(m_, object).next_rail_record().address;
        destroy_object(object);
        object = next;
    }
}

// 0x004aaf10
Word RailKit::crail_destruct(Word self, Word flags) {
    crail_destroy(self);
    if ((flags & 1) != 0) {
        engine(kOperatorDelete, {self});
    }
    return self;
}

// 0x004aaf30: leaves the list
void RailKit::crail_destroy(Word self) {
    const RuntimeTriggerRailRecordView rail = record_at(m_, self);
    rail.set_vtable(GuestPtr<void>{record::kRailVtable});
    if (global_word(m_, globals::kListHead) == self) {
        set_global_word(m_, globals::kListHead, rail.next_rail_record().address);
    }
    const Word previous = rail.previous_rail_record().address;
    if (previous != 0) {
        record_at(m_, previous).set_next_rail_record(rail.next_rail_record());
    }
    const Word next = rail.next_rail_record().address;
    if (next != 0) {
        record_at(m_, next).set_previous_rail_record(rail.previous_rail_record());
    }
    rail.set_vtable(GuestPtr<void>{record::kBaseVtable});
}

}  // namespace opentony::runtime::rail
