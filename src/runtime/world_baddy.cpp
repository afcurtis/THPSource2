#include "world_views.hpp"
#include "seam_addresses_generated.hpp"

// Phase 4.15c: the classes at the head of the executable (`0x00401000..0x00403410`): the script object (list 0x0055f6b0), the baddy base class (vtable 0x00518388, count at 0x0055f6b8), the
// type-0xcb baddy (vtable 0x005183b0, list 0x0055f6bc), the movers' per-frame physics step and the assertion routine every other function reports its failures to. `self` is the object.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kObjectFree = 0x0047fd60;               // Object_Free(object)
constexpr Word kListRemove = 0x004801f0;               // ObjectList_Remove (thiscall, the list)
constexpr Word kListPushHead = 0x004801d0;             // ObjectList_PushHead (thiscall, the list)
constexpr Word kStopVoice = 0x004ad9f0;                // SFX_StopVoice(voice)
constexpr Word kPlatformAi = 0x004c9fd0;               // CPlatform__AI(object, target, pitch, yaw, distance)
constexpr Word kApplyScriptFieldWord = 0x004027f0;     // TRG_ApplyScriptFieldWord (thiscall, one argument)
constexpr Word kVsnprintf = seam::crt::CRT_vsnprintf;                // CRT_vsnprintf(buffer, size, format, arguments)

constexpr Word kScriptObjectList = 0x0055f6b0;         // the script objects (linked through +0x20)
constexpr Word kScriptObjectCount = 0x0055f6b4;        // word
constexpr Word kBaddyCount = 0x0055f6b8;               // dword: how many baddies exist
constexpr Word kBaddyList = 0x0055f6bc;
constexpr Word kPlayerRecord = 0x0056a960;             // the second-camera / owner record (an object)
constexpr Word kOwner = 0x0056a954;
constexpr Word kStepWordA = 0x0056af38;                // the frame's two scalars (dword, word)
constexpr Word kStepWordB = 0x0056af3c;

constexpr Word kScriptVtable = 0x0051836c;
constexpr Word kBaddyVtable = 0x00518388;
constexpr Word kSubtypeVtable = 0x005183b0;
constexpr Word kAssertionFormat = 0x00524198;          // "ASSERT %s line %d: %s"-like format of the log line
constexpr Word kAssertionFile = 0x0056881c;            // where the assertion macro leaves the file name
constexpr Word kAssertionLine = 0x00568638;            // ... and the line number

// the fake locals of the assertion routine (a 0x200-byte buffer and the argument list pointer)
constexpr Word kAssertionBuffer = kScratch;
constexpr Word kAssertionArguments = kScratch + 0x200;

}  // namespace

// ---- the small vector operators the step uses (the retail operators work on the object's own memory, a component at a time) ----

void WorldKit::vec3_add(Word self, Word rhs) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 4 * k, word_at(m_, self + 4 * k) + word_at(m_, rhs + 4 * k));
    }
}

// each component -= component >> byte count (arithmetic, count masked to five bits; the counts are re-read after the earlier components were written)
void WorldKit::vec3_decay_shift(Word self, Word counts) {
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t value = s(word_at(m_, self + 4 * k));
        const Word shift = byte_at(m_, counts + k) & 31u;
        set_word_at(m_, self + 4 * k, u(value) - u(value >> shift));
    }
}

void WorldKit::vec3s_add(Word self, Word rhs) {
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 2 * k, half_at(m_, self + 2 * k) + half_at(m_, rhs + 2 * k));
    }
}

void WorldKit::vec3s_mask12(Word self) {
    for (Word k = 0; k < 3; ++k) {
        set_half_at(m_, self + 2 * k, half_at(m_, self + 2 * k) & 0x0fff);
    }
}

void WorldKit::vec3s_decay_shift(Word self, Word counts) {
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t value = static_cast<std::int16_t>(half_at(m_, self + 2 * k));
        const Word shift = byte_at(m_, counts + k) & 31u;
        set_half_at(m_, self + 2 * k, u(value) - u(value >> shift));
    }
}

void WorldKit::vec3s_snap_near_zero(Word self) {
    for (Word k = 0; k < 3; ++k) {
        const std::int32_t value = static_cast<std::int16_t>(half_at(m_, self + 2 * k));
        if (value >= -1 && value <= 1) {
            set_half_at(m_, self + 2 * k, 0);
        }
    }
}

// ---- the script object ----

// 0x00401000
void WorldKit::script_object_clear_flag(Word id) {
    for (Word at = global_word(m_, kScriptObjectList); at != 0; at = word_at(m_, at + object::kNext)) {
        if (half_at(m_, at + 0xc8) == (id & 0xffff)) {
            set_half_at(m_, at + object::kFlags, half_at(m_, at + object::kFlags) & 0xfffe);
            return;
        }
    }
}

// 0x00401030
void WorldKit::script_object_set_flag(Word id) {
    for (Word at = global_word(m_, kScriptObjectList); at != 0; at = word_at(m_, at + object::kNext)) {
        if (half_at(m_, at + 0xc8) == (id & 0xffff)) {
            set_byte_at(m_, at + object::kFlags, byte_at(m_, at + object::kFlags) | 0x01);
            return;
        }
    }
}

// 0x00401100
Word WorldKit::script_object_scalar_delete(Word self, Word flags) {
    script_object_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x00401120
void WorldKit::script_object_destruct(Word self) {
    script_object_at(m_, self).set_vtable_or_class_dispatch(GuestPtr<void>{static_cast<Word>(kScriptVtable)});
    thiscall(kListRemove, self, {kScriptObjectList});
    check(global_half(m_, kScriptObjectCount) != 0, 0x00524160);
    set_global_half(m_, kScriptObjectCount, global_half(m_, kScriptObjectCount) - 1);
    object_destruct(self);
}

// 0x004011d0
void WorldKit::add_spin(Word self) { vec3s_add(self + 0x14, self + 0x70); }

// 0x004011e0: when the condition's low byte is zero the message is formatted into a local buffer and logged with the file and line the assertion macro stored
void WorldKit::assertion(Word condition, Word format, Word arguments) {
    (void)arguments;
    if ((condition & 0xff) != 0) {
        return;
    }
    engine(kVsnprintf, {kAssertionBuffer, 0x200, format, kAssertionArguments}, m_.cstring(format, 256));
    if (s_.live()) {
        s_.note("printf_log", {kAssertionFormat}, m_.cstring(format, 256));   // (live the log shows the message too; a replay records the format only)
    } else {
        log_line(kAssertionFormat);
    }
}

// ---- the baddy ----

// 0x00401270
Word WorldKit::baddy_construct(Word self) {
    animated_construct(self);
    baddy_at(m_, self).set_field_1c4(0);
    baddy_at(m_, self).set_field_1c8(0);
    baddy_at(m_, self).set_field_1cc(0);
    baddy_at(m_, self).set_vtable(static_cast<Word>(kBaddyVtable));
    set_global_word(m_, kBaddyCount, global_word(m_, kBaddyCount) + 1);
    baddy_at(m_, self).set_field_1c0(0);
    baddy_at(m_, self).set_field_b0(0xffff);
    baddy_at(m_, self).set_field_1d0(0xffffffff);
    baddy_at(m_, self).set_field_1d4(0xffffffff);
    baddy_at(m_, self).set_field_ae(0x80);
    return self;
}

// 0x004012d0
Word WorldKit::baddy_scalar_delete(Word self, Word flags) {
    baddy_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x004012f0
void WorldKit::baddy_destruct(Word self) {
    baddy_at(m_, self).set_vtable(static_cast<Word>(kBaddyVtable));
    check(s(global_word(m_, kBaddyCount)) > 0, 0x005241b0);
    set_global_word(m_, kBaddyCount, global_word(m_, kBaddyCount) - 1);
    const Word voice = baddy_at(m_, self).field_1d0();
    if (voice != 0xffffffff) {
        engine(kStopVoice, {voice});
    }
    animated_destruct(self);
}

// 0x00401410
void WorldKit::step_physics(Word self) {
    baddy_at(m_, self).set_field_80(static_cast<Word>(global_word(m_, kStepWordA)));
    baddy_at(m_, self).set_field_b2(0);
    baddy_at(m_, self).set_field_84(static_cast<std::uint16_t>(global_half(m_, kStepWordB)));
    vec3_add(self + 0x4c, self + 0x58);
    vec3_decay_shift(self + 0x4c, self + 0x6c);
    core_.vector_dead_zone(self + 0x4c);
    vec3_add(self + 0x08, self + 0x4c);
    vec3s_add(self + 0x14, self + 0x70);
    vec3s_mask12(self + 0x14);
    vec3s_add(self + 0x70, self + 0x76);
    vec3s_decay_shift(self + 0x70, self + 0x7c);
    vec3s_snap_near_zero(self + 0x70);
}

// 0x00402f80
Word WorldKit::ai_view_of_player(Word self, Word pitch, Word yaw, Word distance) {
    if (is_dying(global_word(m_, kPlayerRecord)) != 0) {
        return 0;
    }
    return engine_value(kPlatformAi, {global_word(m_, kPlayerRecord), self, pitch, yaw, distance});
}

// 0x00402fc0
Word WorldKit::ai_view_by_player(Word self, Word pitch, Word yaw, Word distance) {
    if (is_dying(global_word(m_, kPlayerRecord)) != 0) {
        return 0;
    }
    return engine_value(kPlatformAi, {self, global_word(m_, kPlayerRecord), pitch, yaw, distance});
}

// ---- the type-0xcb baddy ----

// 0x00403000
Word WorldKit::construct_subtype_0xcb(Word self, Word record, Word link) {
    baddy_construct(self);
    subtype_cb_at(m_, self).set_vtable_or_class_dispatch(GuestPtr<void>{static_cast<Word>(kSubtypeVtable)});
    subtype_cb_at(m_, self).set_runtime_context(GuestPtr<void>{static_cast<Word>(global_word(m_, kOwner))});
    const Word after_position = read_fixed_position(self, record);
    const Word after_parameters = read_object_parameters(self, after_position);
    set_byte_at(m_, self + object::kFlags, byte_at(m_, self + object::kFlags) | 0x41);
    subtype_cb_at(m_, self).set_trigger_state_flags(static_cast<std::uint16_t>(subtype_cb_at(m_, self).trigger_state_flags() & 0xffef));
    subtype_cb_at(m_, self).set_constructor_sentinel(static_cast<std::int32_t>(0xffffffff));
    subtype_cb_at(m_, self).set_script_stream_cursor(GuestPtr<void>{static_cast<Word>(after_parameters)});
    subtype_cb_at(m_, self).set_constructor_active_byte(1);
    subtype_cb_at(m_, self).set_source_trg_node_index(static_cast<std::uint16_t>(link));
    thiscall(kListPushHead, self, {kBaddyList});
    subtype_cb_at(m_, self).set_script_field_ae(0);
    subtype_cb_at(m_, self).set_object_subtype(0xcb);
    return self;
}

// 0x004030b0
Word WorldKit::subtype_0xcb_scalar_delete(Word self, Word flags) {
    subtype_0xcb_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x004030d0
void WorldKit::subtype_0xcb_destruct(Word self) {
    subtype_cb_at(m_, self).set_vtable_or_class_dispatch(GuestPtr<void>{static_cast<Word>(kSubtypeVtable)});
    thiscall(kListRemove, self, {kBaddyList});
    const Word voice = static_cast<Word>(subtype_cb_at(m_, self).positional_sound_handle());
    if (voice != 0xffffffff) {
        engine(kStopVoice, {voice});
    }
    const Word sprite = subtype_cb_at(m_, self).field_1e4();
    if (sprite != 0) {
        virtual_call(sprite, 0, {1});
    }
    if (subtype_cb_at(m_, self).field_1e0() != 0) {
        const Word count = subtype_cb_at(m_, self).field_1dc();
        for (Word index = 0; index < count; ++index) {
            const Word child = word_at(m_, subtype_cb_at(m_, self).field_1e0() + 0x10 * index + 0xc);
            if (child != 0) {
                virtual_call(child, 0, {1});
            }
        }
        mem_delete(subtype_cb_at(m_, self).field_1e0());
    }
    baddy_destruct(self);
}

// 0x00403400
Word WorldKit::apply_script_field_word_thunk(Word self, Word word) { return thiscall_value(kApplyScriptFieldWord, self, {word}); }

}  // namespace opentony::runtime::worldkit
