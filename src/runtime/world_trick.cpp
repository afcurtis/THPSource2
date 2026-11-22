#include "world_views.hpp"

// Phase 4.15c: the trick objects (`trickob.cpp`, 0x004bd700..0x004be350): the things a trigger file marks as tags or goals (records of type 0xc / 0xe), kept in a list from 0x0056db90; in the
// tag game (mode 8) a skater paints them in turn. Each object (vtable 0x0051982c) is {+4 the model checksum, +8 the record index, +0xa activated, +0xb the colour/owner, +0xc the strength, +0x10
// the next object, +0x14 the model item}. The model items, the sound, the net game and the replay recorder are services.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kList = 0x0056db90;                     // head of the list of trick objects
constexpr Word kGameMode = 0x00533f38;                 // 8: the tag game
constexpr Word kRecords = 0x0056e210;
constexpr Word kRecordCount = 0x0056e214;
constexpr Word kOwner = 0x0056a954;
constexpr Word kPlayer0 = 0x0056a858;
constexpr Word kEnvironmentList = 0x0056db28;
constexpr Word kFlagA = 0x0056db8c;                    // nonzero: the objects are all up for grabs (a fresh round)
constexpr Word kFlagB = 0x0056db88;                    // nonzero while the pulse of an activation runs
constexpr Word kReminder = 0x0056db84;                 // nonzero: the "tag started" sound is still to play
constexpr Word kStealReminder = 0x0056db80;
constexpr Word kLastColour = 0x0056db78;               // byte/dword: the colour index of the last activation
constexpr Word kLastStrength = 0x0056db7c;             // word: its strength
constexpr Word kFrame = 0x0056e320;                    // the frame counter the flicker follows
constexpr Word kFlickerMasks = 0x00541548;             // 8 dwords: the colour channels the flicker modulates
constexpr Word kCursor = 0x00560fd0;                   // the primitive cursor
constexpr Word kDisplay = 0x00560fd4;                  // the display the primitives are linked into (+0x84: the ordering table)
constexpr Word kPrimitiveLimit = 0x00568628;
constexpr Word kLineScale = 0x00541570;
constexpr Word kLineOffsetX = 0x00541568;
constexpr Word kLineOffsetY = 0x0054156c;
constexpr Word kScreenScaleX = 0x029da394;
constexpr Word kScreenScaleY = 0x029da398;
constexpr Word kRegionTable = 0x0056d43c;              // 17 dwords per region: the table of models of the region

constexpr Word kVtable = 0x0051982c;
constexpr Word kPlainVtable = 0x005184b4;

constexpr Word kFindChecksum = 0x004b1f40;             // PSX_GetModelNameChecksum(object)
constexpr Word kFindItem = 0x004b1ef0;                 // Spool_FindEnviroItem(checksum)
constexpr Word kRegisterObject = 0x0048f800;           // Trick_RegisterObject (thiscall on the owner, one argument)
constexpr Word kPlaySound = 0x004ad620;                // SFX_PlayX(sound, flags, voice)
constexpr Word kNetPoint = 0x004df2c0;                 // NetGame_OnPoint(object, skater, strength, trick object)
constexpr Word kIsRemote = 0x004de010;                 // NetGame_IsRemotePlayer(skater)
constexpr Word kShowSteals = 0x0048e950;               // Panel_ShowStealsMessage (thiscall on the skater's panel)
constexpr Word kRecordTrickObject = 0x004cb4c0;        // Replay_RecordTrickObject(colour, index)
constexpr Word kSendPulse = 0x004c58b0;                // TRG_SendPulse(node)

constexpr std::int32_t kMagicX = static_cast<std::int32_t>(0x84210843u);
constexpr std::int32_t kMagicY = static_cast<std::int32_t>(0xae147ae1u);

}  // namespace

// 0x004bd700: deletes every trick object; the head is read again after each delete (the destructor unlinks the object, the probe scripts the new head)
void WorldKit::trick_delete_all() {
    for (Word head = global_word(m_, kList); head != 0; head = global_word(m_, kList)) {
        const Word answer = virtual_call(head, 0, {1});       // (the stub scripts the new head: the answer of the call; live, the destructor unlinks the object itself and answers the object)
        if (!s_.live()) {
            set_global_word(m_, kList, answer);
        }
    }
}

// 0x004bd720: the same, taking the next object before the delete
void WorldKit::trick_delete_all_preserve() {
    for (Word at = global_word(m_, kList); at != 0;) {
        const Word next = word_at(m_, at + 0x10);
        const Word answer = virtual_call(at, 0, {1});      // (the stub scripts the new head; retail's destructor unlinks the object and answers it)
        if (!s_.live()) {
            set_global_word(m_, kList, answer);
        }
        at = next;
    }
}

// 0x004bd740 (cdecl): is the record a goal object (type 0xe)
Word WorldKit::trg_is_goal_object_node(Word index) {
    const Word record = word_at(m_, global_word(m_, kRecords) + 4 * (index & 0xffff));
    return half_at(m_, record) == 0xe ? 1u : 0u;
}

// 0x004bd760 (thiscall, two arguments)
Word WorldKit::trick_object_construct(Word self, Word index, Word kind) {
    trick_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(kVtable)});
    const Word record_index = index & 0xffff;
    check(s(record_index) < s(global_word(m_, kRecordCount)), 0x005415c0);
    check((kind & 0xffff) < 3, 0x005415a4);
    const Word type = half_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * record_index));
    check(type == 0xc || type == 0xe, 0x00541588);
    trick_at(m_, self).set_next_trick_object(GuestPtr<guest::RuntimeTrickObject>{static_cast<Word>(global_word(m_, kList))});
    set_global_word(m_, kList, self);
    trick_at(m_, self).set_resolved_runtime_object(GuestPtr<void>{0});
    trick_at(m_, self).set_source_trg_node_index(static_cast<std::uint16_t>(index));
    trick_at(m_, self).set_activated(0);
    const Word record = word_at(m_, global_word(m_, kRecords) + 4 * record_index);
    Word at = record + 2 * half_at(m_, record + 2) + 4;
    if ((at & 3u) != 0) {
        at += 2;
    }
    trick_at(m_, self).set_model_name_checksum(static_cast<Word>(word_at(m_, at)));
    check(trick_at(m_, self).model_name_checksum() != 0, 0x00541574);
    return self;
}

// 0x004bd920 (thiscall)
Word WorldKit::trick_object_scalar_delete(Word self, Word flags) {
    trick_object_destruct(self);
    if ((flags & 1u) != 0) {
        core_.class_delete(self);
    }
    return self;
}

// 0x004bd940: leaves the list
void WorldKit::trick_object_destruct(Word self) {
    trick_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(kVtable)});
    const Word head = global_word(m_, kList);
    if (head == self) {
        set_global_word(m_, kList, trick_at(m_, self).next_trick_object().address);
    } else {
        Word before = head;
        Word at = head;
        do {
            before = at;
            at = word_at(m_, at + 0x10);
        } while (at != self);
        set_word_at(m_, before + 0x10, trick_at(m_, self).next_trick_object().address);
    }
    trick_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(kPlainVtable)});
}

// 0x004bd980 (cdecl, three arguments): a skater touches a model: if it is one of the trick objects it is activated (painted in the skater's colour) and the records it links to run
void WorldKit::trick_object_activate_for_object(Word object, Word skater, Word strength) {
    const Word colour = (skater != global_word(m_, kPlayer0) ? 1u : 0u) + 1;
    const Word checksum = engine_value(kFindChecksum, {object});
    Word node = global_word(m_, kList);
    Word index = 0;
    if (node == 0) {
        return;
    }
    while (word_at(m_, node + 4) != checksum) {
        node = word_at(m_, node + 0x10);
        ++index;
        if (node == 0) {
            return;
        }
    }
    if (strength == 0xffffffffu && trg_is_goal_object_node(half_at(m_, node + 8)) == 0) {
        return;
    }
    if (byte_at(m_, node + 0xa) != 0) {
        if (!(global_word(m_, kGameMode) == 8 && byte_at(m_, node + 0xb) != colour)) {
            if (global_word(m_, kFlagA) == 0) {
                return;
            }
        }
    }
    const Word goal = trg_is_goal_object_node(half_at(m_, node + 8));
    if (goal != 0 && global_word(m_, kGameMode) == 1) {
        engine(kPlaySound, {0x14, 0x2000, 0});
        set_byte_at(m_, object + 5, byte_at(m_, object + 5) | 0x04);
        set_word_at(m_, object + 0x24, 0x202020);
    } else {
        if (global_word(m_, kGameMode) != 8 && global_word(m_, kFlagA) == 0) {
            return;
        }
        if (s(half_at(m_, node + 0xc)) > s(strength)) {
            return;
        }
        set_half_at(m_, node + 0xc, strength);
        set_byte_at(m_, object + 5, byte_at(m_, object + 5) | 0x04);
        engine(kNetPoint, {object, skater, strength, node});
        set_word_at(m_, object + 0x24, 0x202020);
        if (global_word(m_, kFlagA) == 0 && (engine_value(kIsRemote, {skater}) & 0xff) == 0) {
            if (global_word(m_, kReminder) != 0) {
                set_global_word(m_, kReminder, 0);
                engine(kPlaySound, {0x18, 0x2000, 0});
            }
            if (byte_at(m_, node + 0xa) != 0) {
                const Word owner = word_at(m_, skater + 0x29bc);
                set_word_at(m_, owner + 0x2cdc, word_at(m_, owner + 0x2cdc) - 1);
                set_word_at(m_, skater + 0x2ce0, word_at(m_, skater + 0x2ce0) + 1);
                engine(kShowSteals, {skater + 0x168});
                if (global_word(m_, kStealReminder) != 0) {
                    set_global_word(m_, kStealReminder, 0);
                    engine(kPlaySound, {0x17e, 0x2000, 0});
                }
            }
        }
        set_byte_at(m_, node + 0xb, colour);
    }
    set_global_word(m_, kLastColour, colour);
    set_global_word(m_, kLastStrength, strength);
    engine(kRecordTrickObject, {colour, index});
    const Word links = trg_get_node_links(half_at(m_, node + 8));
    Word at = links + 2;
    for (Word left = half_at(m_, links); left != 0; --left, at += 2) {
        const Word link = half_at(m_, at);
        const Word type = half_at(m_, word_at(m_, global_word(m_, kRecords) + 4 * link));
        if (type == 0xe || type == 0xc) {
            trick_object_activate_goal_object(link);
        } else {
            set_global_word(m_, kFlagB, 1);
            engine(kSendPulse, {link});
            set_global_word(m_, kFlagB, 0);
        }
    }
    set_byte_at(m_, node + 0xa, 1);
    set_word_at(m_, node + 0x14, object);
}

// 0x004bdbd0 (cdecl): in the tag game the owner takes the object of this record; elsewhere it is activated
void WorldKit::trick_object_take(Word index) {
    if (global_word(m_, kFlagB) != 0) {
        trick_object_activate_goal_object(index);
        return;
    }
    if (global_word(m_, kGameMode) != 8 || index == 0) {
        return;
    }
    Word node = global_word(m_, kList);
    if (node == 0) {
        return;
    }
    const Word owner = global_word(m_, kOwner);
    for (; node != 0; node = word_at(m_, node + 0x10)) {
        if (half_at(m_, node + 8) == index && owner != 0) {
            const Word item = engine_value(kFindItem, {word_at(m_, node + 4)});
            thiscall(kRegisterObject, global_word(m_, kOwner), {item});
            return;
        }
    }
}

// 0x004bdc40 (cdecl): the object of this record takes its model item
void WorldKit::trick_object_activate_goal_object(Word index) {
    if (global_word(m_, kGameMode) != 8 || index == 0) {
        return;
    }
    Word node = global_word(m_, kList);
    if (node == 0) {
        return;
    }
    while (half_at(m_, node + 8) != index) {
        node = word_at(m_, node + 0x10);
        if (node == 0) {
            return;
        }
    }
    const Word item = engine_value(kFindItem, {word_at(m_, node + 4)});
    check(item != 0, 0x005415f8);
    set_word_at(m_, node + 0x14, item);
    set_half_at(m_, node + 0xc, global_half(m_, kLastStrength));
    set_byte_at(m_, node + 0xb, global_byte(m_, kLastColour));
    set_byte_at(m_, item + 5, byte_at(m_, item + 5) | 0x04);
    set_word_at(m_, item + 0x24, 0x202020);
    set_byte_at(m_, node + 0xa, 1);
}

// the flicker period of a strength: about strength * 64 / 1000 half-steps, 0x40 beyond 2000
static std::int32_t flicker_steps(Word strength) {
    if ((strength & 0xffff) > 0x7d0) {
        return 0x40;
    }
    const std::int64_t product = static_cast<std::int64_t>(0x10624dd3) * static_cast<std::int32_t>((strength & 0xffff) << 6);
    std::int32_t high = static_cast<std::int32_t>(product >> 32) >> 7;
    high += static_cast<std::int32_t>(static_cast<Word>(high) >> 31);
    return high;
}

// 0x004bdd00: the colour of every activated object flickers (or, in the tag game, fades from the strength it was painted with)
void WorldKit::trick_object_update() {
    for (Word node = global_word(m_, kList); node != 0; node = word_at(m_, node + 0x10)) {
        if (byte_at(m_, node + 0xa) == 0 && global_word(m_, kFlagA) == 0) {
            continue;
        }
        const Word item = word_at(m_, node + 0x14);
        if (item == 0) {
            continue;
        }
        if (global_word(m_, kFlagA) != 0) {
            Word phase = ((static_cast<Word>(s(word_at(m_, item + 8)) >> 12) + global_word(m_, kFrame)) << 2) % 200u;
            if (s(phase) > 100) {
                phase = 200u - phase;
            }
            const Word mask = word_at(m_, kFlickerMasks + 4 * ((static_cast<Word>(s(item) >> 2)) & 7u));
            const Word grey = ((phase << 8) + phase) << 8;
            const Word value = (grey + phase) & ~mask;
            set_word_at(m_, item + 0x24, value | ((mask | 0xff000000u) & word_at(m_, item + 0x24)));
            continue;
        }
        if (global_word(m_, kGameMode) != 8) {
            Word phase = (global_word(m_, kFrame) >> 1) & 0x3f;
            if (s(phase) > 0x20) {
                phase = 0x40 - phase;
            }
            set_word_at(m_, item + 0x24, (word_at(m_, item + 0x24) & 0xffffff00u) | (phase + 0x20));
            continue;
        }
        const Word strength = half_at(m_, node + 0xc);
        const Word period = static_cast<Word>(flicker_steps(strength)) * 2 + 2;
        Word step = (global_word(m_, kFrame) << 2) % period;
        const Word half = static_cast<Word>(flicker_steps(strength)) + 1;
        if (s(step) > s(half)) {
            step = static_cast<Word>(flicker_steps(strength)) * 2 + 2 - step;
        }
        if (byte_at(m_, node + 0xb) == 1) {
            set_word_at(m_, item + 0x24, ((step + 0x40) << 16) | (word_at(m_, item + 0x24) & 0xff00ffffu));
        } else {
            set_word_at(m_, item + 0x24, (word_at(m_, item + 0x24) & 0xffffff00u) | (step + 0x40));
        }
    }
}

// the division by multiplication retail's compiler made of "x / 15872" (add the product back, shift, round towards zero)
static std::int32_t scale_x(std::int32_t value) {
    const std::int64_t product = static_cast<std::int64_t>(kMagicX) * value;
    std::int32_t result = (static_cast<std::int32_t>(product >> 32) + value) >> 13;
    result += static_cast<std::int32_t>(static_cast<Word>(result) >> 31);
    return result;
}

static std::int32_t scale_y(std::int32_t value) {
    const std::int64_t product = static_cast<std::int64_t>(kMagicY) * value;
    std::int32_t result = static_cast<std::int32_t>(product >> 32) >> 13;
    result += static_cast<std::int32_t>(static_cast<Word>(result) >> 31);
    return result;
}

// 0x004bdea0 (cdecl, five arguments): one line (x0, y0, x1, y1, colour) clipped to the 512x240 screen and linked into the ordering table as a primitive of 0x14 bytes
void WorldKit::trick_object_line_render(Word x0, Word y0, Word x1, Word y1, Word colour) {
    const Word factor = global_word(m_, kLineScale);
    std::int32_t left = scale_x(s(factor * x0));
    std::int32_t right = scale_x(s(factor * x1));
    std::int32_t top = scale_y(s(factor * y0));
    std::int32_t bottom = scale_y(s(factor * y1));
    if (left == right && top == bottom) {
        return;
    }
    left += s(global_word(m_, kLineOffsetX));
    right += s(global_word(m_, kLineOffsetX));
    top += s(global_word(m_, kLineOffsetY));
    bottom += s(global_word(m_, kLineOffsetY));
    if (left > right) {
        std::swap(left, right);
        std::swap(top, bottom);
    }
    if (right < 0 || left > 0x1ff) {
        return;
    }
    if (left < 0) {
        top -= divide((bottom - top) * left, right - left);
        left = 0;
    }
    if (right > 0x1ff) {
        bottom = divide((bottom - top) * (0x1ff - left), right - left) + top;
        right = 0x1ff;
    }
    if (top > bottom) {
        std::swap(left, right);
        std::swap(top, bottom);
    }
    if (bottom < 0 || top > 0xef) {
        return;
    }
    if (top < 0) {
        left -= divide((right - left) * top, bottom - top);
        top = 0;
    }
    if (bottom > 0xef) {
        right = divide((right - left) * (0xef - top), bottom - top) + left;
        bottom = 0xef;
    }
    const Word at = global_word(m_, kCursor);
    if (at + 0x14 > global_word(m_, kPrimitiveLimit)) {
        return;
    }
    set_global_word(m_, kCursor, at + 0x14);
    set_word_at(m_, at, 0x03000000);
    set_word_at(m_, at + 4, colour + 0x40000000);
    const Word scale_h = global_word(m_, kScreenScaleX);
    const Word scale_v = global_word(m_, kScreenScaleY);
    set_half_at(m_, at + 8, (u(static_cast<std::int16_t>(left)) * scale_h) >> 9);
    set_half_at(m_, at + 0xa, static_cast<Word>((static_cast<std::uint64_t>(0x88888889u) * (u(static_cast<std::int16_t>(top)) * scale_v)) >> 39));
    set_half_at(m_, at + 0xc, (u(static_cast<std::int16_t>(right)) * scale_h) >> 9);
    set_word_at(m_, at + 0x10, 0xbf800000);
    set_half_at(m_, at + 0xe, static_cast<Word>((static_cast<std::uint64_t>(0x88888889u) * (u(static_cast<std::int16_t>(bottom)) * scale_v)) >> 39));
    const Word table = word_at(m_, global_word(m_, kDisplay) + 0x84);
    set_word_at(m_, at, word_at(m_, table));
    set_word_at(m_, word_at(m_, global_word(m_, kDisplay) + 0x84), at);
}

// 0x004be0b0 (cdecl, two arguments): the outline of an object's model, as the four sides of its box
void WorldKit::trick_object_draw_box(Word object, Word colour) {
    const Word region = byte_at(m_, object + 0x1f);
    const Word table = word_at(m_, kRegionTable + 4 * (region * 17));
    const Word model = word_at(m_, table + 4 * half_at(m_, object + 0x1a));
    const Word x = static_cast<Word>(s(word_at(m_, object + 8)) >> 12);
    const Word z = static_cast<Word>(s(word_at(m_, object + 0x10)) >> 12);
    const Word near_z = u(static_cast<std::int16_t>(half_at(m_, model + 0x14))) + z;
    const Word far_z = static_cast<Word>(s(word_at(m_, model + 0x14)) >> 16) + z;
    const Word far_x = static_cast<Word>(s(word_at(m_, model + 0xc)) >> 16) + x;
    const Word near_x = u(static_cast<std::int16_t>(half_at(m_, model + 0xc))) + x;
    trick_object_line_render(near_x, near_z, far_x, near_z, colour);
    trick_object_line_render(near_x, near_z, near_x, far_z, colour);
    trick_object_line_render(near_x, far_z, far_x, far_z, colour);
    trick_object_line_render(far_x, near_z, far_x, far_z, colour);
}

// 0x004be350: at the start of a round every model of the environment is offered to the trick objects
void WorldKit::trick_object_offer_all() {
    if (global_word(m_, kGameMode) == 8) {
        set_global_word(m_, kFlagA, 0);
        return;
    }
    for (Word at = global_word(m_, kEnvironmentList); at != 0; at = word_at(m_, at + object::kNext)) {
        if (global_word(m_, kFlagA) != 0) {
            const Word strength = static_cast<Word>(rnd(0x3e8)) + 0x3e8;
            trick_object_activate_for_object(at, global_word(m_, kPlayer0), strength);
        }
    }
}

}  // namespace opentony::runtime::worldkit
