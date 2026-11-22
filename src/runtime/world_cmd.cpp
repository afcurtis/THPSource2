#include "world_views.hpp"
#include "seam_addresses_generated.hpp"

// Phase 4.15c: the trigger-record writers (`trig.cpp`, 0x004c87c0..0x004c8f30): a small class family whose objects describe one record of a `_TRG` file (a node, a link, a command) and write it
// into a buffer. The base class (vtable 0x00519894) keeps the record's size at +8 and the number of bytes the writer did not need at +0xc; each derived class (vtables 0x005198a0..0x005198e8)
// keeps its fields from +0x10. `self` is the writer, `buffer` the record being written (the formatter and the log are services).

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kPlainVtable = 0x005184b4;              // what every destructor leaves behind
constexpr Word kBaseVtable = 0x00519894;
constexpr Word kSprintf = seam::crt::CRT_sprintf;                  // CRT_sprintf(destination, format[, value])
constexpr Word kFloorHeight = 0x00561488;              // the editor's grid scale (a dword)
constexpr Word kGridStride = 0x0056148c;
constexpr Word kGridHeight = 0x0052839c;
constexpr Word kGridWidth = 0x00528398;
constexpr Word kGateTable = 0x005614c8;                // dword: the index into the table of gate numbers at 0x005286f0
constexpr Word kGateNumbers = 0x005286f0;

}  // namespace

// 0x004c87c0
Word WorldKit::cmd_construct(Word self) {
    cmd_at(m_, self).set_field_0(static_cast<Word>(kBaseVtable));
    cmd_at(m_, self).set_field_8(0);
    cmd_at(m_, self).set_field_4(0);
    return self;
}

// the deleting destructors of every class of the family (0x004c87e0, 0x004c88c0, 0x004c8a50, 0x004c8bf0, 0x004c8c60, 0x004c8d00, 0x004c8dd0, 0x004c8e80)
Word WorldKit::cmd_scalar_delete(Word self, Word flags) {
    cmd_destruct(self);
    if ((flags & 1u) != 0) {
        core_.class_delete(self);
    }
    return self;
}

// the destructors (0x004c8800, 0x004c88e0, 0x004c8a70, 0x004c8c10, 0x004c8c80, 0x004c8d20, 0x004c8df0, 0x004c8ea0)
void WorldKit::cmd_destruct(Word self) { set_word_at(m_, self, kPlainVtable); }

// 0x004c8810
Word WorldKit::cmd_size(Word self) {
    check(cmd_at(m_, self).field_8() != 0, 0x005480f0);
    return cmd_at(m_, self).field_8();
}

// ---- the node (vtable 0x005198a0, record type 8) ----

// 0x004c8870 (thiscall, seven arguments)
Word WorldKit::node_construct(Word self, Word a0, Word a1, Word a2, Word a3, Word a4, Word a5, Word a6) {
    cmd_construct(self);
    node_cmd_at(m_, self).set_field_10(static_cast<Word>(a0));
    node_cmd_at(m_, self).set_field_14(static_cast<Word>(a1));
    node_cmd_at(m_, self).set_field_18(static_cast<Word>(a2));
    set_word_at(m_, self + 0x1c, a3);
    set_word_at(m_, self + 0x28, a6);
    node_cmd_at(m_, self).set_field_0(0x005198a0);
    node_cmd_at(m_, self).set_field_20(static_cast<Word>(a4));
    node_cmd_at(m_, self).set_field_24(static_cast<Word>(a5));
    node_cmd_at(m_, self).set_field_8(0x26);
    return self;
}

// 0x004c88f0: the editor's grid cell (x, y, z, rotation) as a node record: the position in world units, the angle in twelve bits, the name and a trailing marker
Word WorldKit::node_write(Word self, Word buffer) {
    Word at = buffer + 4;
    set_half_at(m_, buffer, 8);
    set_half_at(m_, buffer + 2, 0);
    if ((at & 3u) != 0) {
        at += 2;
    }
    log_line(0x0054815c);
    const Word scale = global_word(m_, kFloorHeight);
    const Word stride = global_word(m_, kGridStride) * node_cmd_at(m_, self).field_14();
    const Word depth = u(s(global_word(m_, kGridHeight)) >> 1) - node_cmd_at(m_, self).field_18();
    const Word x = u(s(0u - global_word(m_, kGridWidth)) >> 1) + node_cmd_at(m_, self).field_10();
    set_word_at(m_, at + 4, 0xfffffff6u - stride);
    set_word_at(m_, at, x * scale + 0xaa);
    set_word_at(m_, at + 8, depth * scale - 0xaa);
    set_half_at(m_, at + 0xc, 0);
    set_half_at(m_, at + 0x10, 0);
    set_half_at(m_, at + 0xe, ((0xffffu - half_at(m_, self + 0x1c)) << 10) & 0xfffu);
    Word text = at + 0x12;
    const Word gate = node_cmd_at(m_, self).field_24();
    if (gate != 0xffffffffu) {
        engine(kSprintf, {text, 0x00548154, gate + 0x41});
    } else if (node_cmd_at(m_, self).field_20() == 0) {
        engine(kSprintf, {text, 0x0052ca00});
    } else {
        engine(kSprintf, {text, 0x00547834});
    }
    take_filled(text, 8, pcplatform::engine_name(kSprintf));      // (the probe's formatter writes eight bytes)
    text += 6;
    set_half_at(m_, text, 0xca);
    log_line(0x00548124);
    text += 6;
    set_half_at(m_, text - 4, s(word_at(m_, self + 0x28)) >> 16);
    set_half_at(m_, text - 2, half_at(m_, self + 0x28));
    set_half_at(m_, text, 0xffff);
    const Word written = text - buffer + 2;
    node_cmd_at(m_, self).set_field_c(static_cast<Word>(node_cmd_at(m_, self).field_8() - written));
    return written;
}

// ---- the link (vtable 0x005198ac, record type 0xa) ----

// 0x004c8a20
Word WorldKit::link_construct(Word self, Word record) {
    cmd_construct(self);
    link_cmd_at(m_, self).set_field_0(0x005198ac);
    link_cmd_at(m_, self).set_field_18(static_cast<Word>(record));
    link_cmd_at(m_, self).set_field_8(0x1a);
    return self;
}

// 0x004c8a80 (thiscall, two arguments): the two ends (-1 for none); +0x10 counts the ones that are set
void WorldKit::link_set_ends(Word self, Word first, Word second) {
    link_cmd_at(m_, self).set_field_14(static_cast<Word>(first));
    link_cmd_at(m_, self).set_field_1c(static_cast<Word>(second));
    link_cmd_at(m_, self).set_field_10(static_cast<std::uint8_t>((first != 0xffffffffu ? 1u : 0u) + (second != 0xffffffffu ? 1u : 0u)));
}

// 0x004c8ab0
Word WorldKit::link_write(Word self, Word buffer) {
    set_half_at(m_, buffer, 0xa);
    set_half_at(m_, buffer + 2, link_cmd_at(m_, self).field_10());
    Word index = 2;
    if (link_cmd_at(m_, self).field_14() != 0xffffffffu) {
        set_half_at(m_, buffer + 4, link_cmd_at(m_, self).field_14());
        index = 3;
    }
    if (link_cmd_at(m_, self).field_1c() != 0xffffffffu) {
        set_half_at(m_, buffer + 2 * index, link_cmd_at(m_, self).field_1c());
        ++index;
    }
    const Word pad = (((buffer & 0xff) & 3u) != 0 ? 1u : 0u) + link_cmd_at(m_, self).field_10();
    link_cmd_at(m_, self).set_field_11(static_cast<std::uint8_t>(pad & 1u));
    if ((pad & 1u) != 0) {
        ++index;
    }
    index += 6;
    const Word record = link_cmd_at(m_, self).field_18();
    set_word_at(m_, buffer + 2 * index - 0xc, u(static_cast<std::int16_t>(half_at(m_, record))));
    set_word_at(m_, buffer + 2 * index - 0x8, u(static_cast<std::int16_t>(half_at(m_, record + 2))));
    set_word_at(m_, buffer + 2 * index - 0x4, u(static_cast<std::int16_t>(half_at(m_, record + 4))));
    const Word kind = byte_at(m_, link_cmd_at(m_, self).field_18() + 0x10);
    Word word = kind;
    if (kind >= 0xd && kind <= 0xf) {
        word = byte_at(m_, global_word(m_, kGateTable) + kind * 4 + kGateNumbers);
    }
    set_half_at(m_, buffer + 2 * index, word);
    ++index;
    set_half_at(m_, buffer + 2 * index, 0xffff);
    ++index;
    const Word size = (link_cmd_at(m_, self).field_10() + link_cmd_at(m_, self).field_11()) * 2 + 0x14;
    check(index * 2 == size, 0x0054817c);
    link_cmd_at(m_, self).set_field_c(static_cast<Word>(link_cmd_at(m_, self).field_8() - size));
    return size;
}

// ---- the end marker (vtable 0x005198b8) ----

// 0x004c8bd0
Word WorldKit::end_construct(Word self) {
    cmd_construct(self);
    end_cmd_at(m_, self).set_field_0(0x005198b8);
    end_cmd_at(m_, self).set_field_8(2);
    return self;
}

// 0x004c8c20
Word WorldKit::end_write(Word self, Word buffer) {
    set_half_at(m_, buffer, 0xff);
    return end_cmd_at(m_, self).field_8();
}

// ---- the one-field command (vtable 0x005198c4, record type 0xc) ----

// 0x004c8c30
Word WorldKit::command_c_construct(Word self, Word value) {
    cmd_construct(self);
    cmd_c_at(m_, self).set_field_0(0x005198c4);
    cmd_c_at(m_, self).set_field_10(static_cast<Word>(value));
    cmd_c_at(m_, self).set_field_8(0xc);
    return self;
}

// 0x004c8c90
Word WorldKit::command_c_write(Word self, Word buffer) {
    Word at = buffer;
    set_half_at(m_, at, 0xc);
    set_half_at(m_, at + 2, 0);
    Word room = cmd_c_at(m_, self).field_8();
    at += 4;
    if ((at & 3u) != 0) {
        at += 2;
    } else {
        room -= 2;
    }
    set_word_at(m_, at, cmd_c_at(m_, self).field_10());
    set_half_at(m_, at + 4, 0xffff);
    cmd_c_at(m_, self).set_field_c(static_cast<Word>(cmd_c_at(m_, self).field_8() - room));
    return room;
}

// ---- the three-field command (vtable 0x005198d0, record type 6) ----

// 0x004c8cd0 (thiscall, three arguments)
Word WorldKit::command_e_construct(Word self, Word a0, Word a1, Word a2) {
    cmd_construct(self);
    cmd_e_at(m_, self).set_field_10(static_cast<Word>(a0));
    cmd_e_at(m_, self).set_field_0(0x005198d0);
    cmd_e_at(m_, self).set_field_14(static_cast<Word>(a1));
    set_word_at(m_, self + 0x18, a2);
    cmd_e_at(m_, self).set_field_8(0x16);
    return self;
}

// 0x004c8d30
Word WorldKit::command_e_write(Word self, Word buffer) {
    Word at = buffer;
    set_half_at(m_, at, 6);
    set_half_at(m_, at + 2, 0);
    Word room = cmd_e_at(m_, self).field_8();
    at += 4;
    if ((at & 2u) != 0) {
        at += 2;
    } else {
        room -= 2;
    }
    set_word_at(m_, at, cmd_e_at(m_, self).field_10());
    at += 4;
    set_half_at(m_, at, 0xc9);
    at += 2;
    if ((at & 2u) != 0) {
        at += 2;
    } else {
        room -= 2;
    }
    set_word_at(m_, at, cmd_e_at(m_, self).field_14());
    const Word tail = half_at(m_, self + 0x18);
    at += 4;
    set_half_at(m_, at, tail);
    set_half_at(m_, at + 2, 0xffff);
    cmd_e_at(m_, self).set_field_c(static_cast<Word>(cmd_e_at(m_, self).field_8() - room));
    return room;
}

// ---- the two-field command (vtable 0x005198dc, record type 6 with one word) ----

// 0x004c8da0 (thiscall, two arguments)
Word WorldKit::command_f_construct(Word self, Word a0, Word a1) {
    cmd_construct(self);
    set_word_at(m_, self + 0x10, a0);
    cmd_f_at(m_, self).set_field_0(0x005198dc);
    cmd_f_at(m_, self).set_field_14(static_cast<Word>(a1));
    cmd_f_at(m_, self).set_field_8(0x10);
    return self;
}

// 0x004c8e00
Word WorldKit::command_f_write(Word self, Word buffer) {
    set_half_at(m_, buffer, 6);
    set_half_at(m_, buffer + 2, 1);
    set_half_at(m_, buffer + 4, half_at(m_, self + 0x10));
    Word at = buffer + 6;
    Word room = cmd_f_at(m_, self).field_8();
    if ((at & 2u) != 0) {
        at += 2;
    } else {
        room -= 2;
    }
    set_word_at(m_, at, cmd_f_at(m_, self).field_14());
    at += 4;
    set_half_at(m_, at, 0x98);
    set_half_at(m_, at + 2, 0xffff);
    cmd_f_at(m_, self).set_field_c(static_cast<Word>(cmd_f_at(m_, self).field_8() - room));
    return room;
}

// ---- the one-field command (vtable 0x005198e8) ----

// 0x004c8e50
Word WorldKit::command_g_construct(Word self, Word value) {
    cmd_construct(self);
    cmd_g_at(m_, self).set_field_0(0x005198e8);
    cmd_g_at(m_, self).set_field_10(static_cast<Word>(value));
    cmd_g_at(m_, self).set_field_8(0x10);
    return self;
}

// 0x004c8eb0
Word WorldKit::command_g_write(Word buffer) {
    set_half_at(m_, buffer, 6);
    set_half_at(m_, buffer + 2, 0);
    return 6;
}

// 0x004c8ed0 (the writer of a command that has no record)
Word WorldKit::command_none_write() { return 0; }

// 0x004c8ef0 (and its jump thunk at 0x004c8ee0): nine words of the editor's state cleared
void WorldKit::clear_editor_words() {
    for (Word at = 0x0056e230; at < 0x0056e254; at += 4) {
        set_word_at(m_, at, 0);
    }
}

}  // namespace opentony::runtime::worldkit
