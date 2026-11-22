#include "world_views.hpp"

#include "animation_cursor.hpp"
#include "host_options.hpp"

// Phase 4.15c: the object base class of the world objects (`ob.cpp` and the head of the object lists): the constructors and destructors, the flash, the sprite object, the trigger-record
// readers, the model-part order of the skater and the animation pose update. `self` is the object; the record helper (0x0047fdb0 / 0x0047fe20), the allocators and the sprite object's methods
// are services.

namespace opentony::runtime::worldkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kAssertionLine = 0x00568638;
constexpr Word kAssertionFile = 0x0056881c;
const std::string kObFile = "H:\\TonyHawk\\Pc2\\ob.cpp";   // 0x00535b38

constexpr Word kObjectFree = 0x0047fd60;               // Object_Free(object)
constexpr Word kInitRecord = 0x0047fdb0;               // PSX_InitObjectRecord (thiscall)
constexpr Word kDestroyRecord = 0x0047fe20;            // PSX_DestroyObjectRecord (thiscall)
constexpr Word kFindRegion = 0x004b3230;               // PSX_FindLoadedRegionByName(name) -> region (al)
constexpr Word kReadLinks = 0x00464c00;                // M3D_ReadLinksPacket(object, frame)
constexpr Word kInterpolatePose = 0x00465060;          // M3D_InterpolateAnimationPose(object)
constexpr Word kBuildTransform = 0x00461bf0;           // M3d_BuildTransform(object)
constexpr Word kBuildPose = 0x00465300;                // M3D_BuildPose(object)
constexpr Word kSpriteAlloc = 0x00403d30;              // the display's allocator (size)
constexpr Word kSpriteConstruct = 0x00404590;          // thiscall
constexpr Word kSpriteSetTexture = 0x004046f0;         // thiscall, two arguments
constexpr Word kSpriteSetMode = 0x00404810;            // thiscall, one argument
constexpr Word kSpriteSetup = 0x00404840;              // thiscall
constexpr Word kSpriteSetFrame = 0x004048c0;           // thiscall, five arguments
constexpr Word kSpriteSetScale = 0x00404860;           // thiscall, one argument

constexpr Word kAnimationTables = 0x0056d444;          // AnimationTablesByRegion: a 0x44-byte cell a region, the table pointer first
constexpr Word kFlashUpdate = 0x0047ffc0;             // CItem__FlashUpdate (thiscall)
constexpr Word kSpriteUpdate = 0x00480460;             // Object_SpriteUpdate (thiscall)
constexpr Word kInvokeUpdateHooks = 0x00480fa0;        // Animation_InvokeUpdateHooks (thiscall)
constexpr Word kSuspendToList = 0x004802f0;            // Object_SuspendToList (thiscall: the list head's address)
constexpr Word kPlayerNumber = 0x00533f3c;             // GNumberOfPlayers
constexpr Word kPlayerOne = 0x0056a858;                // the first skater (its position at +8)
constexpr Word kPlayerTwo = 0x0056a85c;                // the second skater, or 0
constexpr Word kSuspendDistance = 0x00535af8;          // dword: the distance beyond which a suspend-when-far object is parked
constexpr Word kPlayerRecord = 0x0056a960;             // the first of the two player records the suspended objects are measured from (its successor is the second)
constexpr Word kUnSuspend = 0x004803b0;                // CBody__UnSuspend (thiscall)
constexpr Word kInputFlags = 0x68;                     // word: the input flags Ob_ClearInputFlags keeps bit 0 of
constexpr Word kPowerupVtable = 0x00519684;            // the vtable of a power-up (cash, the letters, a goal's items)

// The suspend distance an object is parked beyond. A power-up is one that may be suspended, and parking it hides it (slot 1), so with the player's fog distance it is
// stretched as the power-up's own reach is (item_reach: School II's cash past 13000 units); every other object, and every object with the game's fog, keeps the game's.
template <typename Memory>
Word suspend_reach(Memory& m, Word object) {
    const Word limit = global_word(m, kSuspendDistance);
    if (host_options().item_reach_scale > 1.0f && word_at(m, object) == kPowerupVtable) {
        return item_reach(limit);
    }
    return limit;
}

constexpr Word kDerivedVtable = 0x005193a8;            // the vtable of the animated object class

// the animated object (CBody-like) fields
constexpr Word kAnimFrame = 0xf4;                      // word: the current frame (Q16 integer part)
constexpr Word kPoseLinks = 0x13c;                     // dword: the links packet the pose uses
constexpr Word kPoseFrame = 0x144;                     // dword: the frame the links were read for
constexpr Word kPartOrderStale = 0x14c;                // dword: the model part order changed
constexpr Word kAnimationClock = 0x005685f4;            // the mode-3 clock
constexpr Word kAnimationTimeScale = 0x0056865c;        // 8.8 scale of every animation rate
constexpr Word kPartOrder = 0x150;                     // 0x13 bytes: the order of each model part

}  // namespace

// 0x0047fe30
void WorldKit::select_psx_region(Word self, Word name) {
    check(name != 0, 0x00535afc);
    const Word region = engine_value(kFindRegion, {name});
    list_object_at(m_, self).set_region(static_cast<std::uint8_t>(region));
    list_object_at(m_, self).set_model_index(0);
}

// 0x004b1ef0 (cdecl): the first object of the list at 0x0056db28 (the next object at +0x20) whose item index (the word at +0x1a) names `key` in the key array its region (the byte
// at +0x1f) owns, the region's cell being 0x44 bytes on from the last one at 0x0056d434; zero when none does. Retail keeps the array of the last region it looked up (the register starts
// as the key itself and is replaced on the first object, whose region is never the initial -1).
Word WorldKit::spool_find_enviro_item(Word key) {
    constexpr Word kEnviroList = 0x0056db28;
    constexpr Word kRegionCells = 0x0056d434;
    Word keys = key;
    Word region_seen = 0xffffffff;
    for (Word object = global_word(m_, kEnviroList); object != 0; object = word_at(m_, object + 0x20)) {
        const Word region = byte_at(m_, object + 0x1f);
        if (region != region_seen) {
            region_seen = region;
            keys = word_at(m_, kRegionCells + region * 0x44);
        }
        if (word_at(m_, keys + u(half_at(m_, object + 0x1a)) * 4) == key) {
            return object;
        }
    }
    return 0;
}

// 0x0047fe90
void WorldKit::item_flash(Word self, std::int32_t frames, Word red, Word green, Word blue) {
    if (frames == 0) {
        list_object_at(m_, self).set_flash_count(0);
        set_byte_at(m_, self + object::kFlags + 1, byte_at(m_, self + object::kFlags + 1) | 0x04);
        list_object_at(m_, self).set_colour(static_cast<Word>(((blue & 0xff) << 16) | ((green & 0xff) << 8) | (red & 0xff)));
        return;
    }
    if (frames < 0) {
        const Word count = u(-frames);
        const std::int32_t divisor = s(count & 0xffff);
        list_object_at(m_, self).set_colour(0x808080);
        list_object_at(m_, self).set_flash_count(static_cast<std::uint16_t>(count));
        list_object_at(m_, self).set_field_46(static_cast<std::uint16_t>(u(divide(s(red & 0xff) - 0x80, divisor))));
        set_half_at(m_, self + object::kFlashStep + 2, u(divide(s(green & 0xff) - 0x80, divisor)));
        set_byte_at(m_, self + object::kFlags + 1, byte_at(m_, self + object::kFlags + 1) | 0x04);
        set_half_at(m_, self + object::kFlashStep + 4, u(divide(s(blue & 0xff) - 0x80, divisor)));
        return;
    }
    const std::int32_t red_in = s(red & 0xff);
    const std::int32_t green_in = s(green & 0xff);
    const std::int32_t blue_in = s(blue & 0xff);
    list_object_at(m_, self).set_flash_count(static_cast<std::uint16_t>(u(frames + 1)));
    list_object_at(m_, self).set_field_46(static_cast<std::uint16_t>(u(divide(0x80 - red_in, frames))));
    set_half_at(m_, self + object::kFlashStep + 2, u(divide(0x80 - green_in, frames)));
    set_half_at(m_, self + object::kFlashStep + 4, u(divide(0x80 - blue_in, frames)));
    Word saved = list_object_at(m_, self).saved_colour();
    if ((saved & 0x01000000) == 0) {
        saved = (list_object_at(m_, self).colour() & 0x00ffffff) | 0x01000000;
        list_object_at(m_, self).set_saved_colour(static_cast<Word>(saved));
        if ((list_object_at(m_, self).visibility_flags() & 0x400) != 0) {
            list_object_at(m_, self).set_saved_colour(static_cast<Word>(saved | 0x02000000));
        }
    }
    list_object_at(m_, self).set_visibility_flags(static_cast<std::uint16_t>(list_object_at(m_, self).visibility_flags() | 0x400));
    list_object_at(m_, self).set_colour(static_cast<Word>((blue_in << 16 & 0xff0000) | (green_in << 8) | red_in));
}

// 0x0047ffc0
void WorldKit::item_flash_update(Word self) {
    const Word count = list_object_at(m_, self).flash_count();
    if (count == 0) {
        return;
    }
    const Word colour = list_object_at(m_, self).colour();
    const Word blue = (u(s(static_cast<std::int16_t>(half_at(m_, self + object::kFlashStep + 4)))) + (colour >> 16)) << 16;
    const Word green = ((half_at(m_, self + object::kFlashStep + 2) + (colour >> 8)) << 8) & 0xff00;
    const Word red = (list_object_at(m_, self).field_46() + colour) & 0xff;
    set_byte_at(m_, self + object::kFlags + 1, byte_at(m_, self + object::kFlags + 1) | 0x04);
    Word flags = list_object_at(m_, self).visibility_flags();
    const Word left = (count - 1) & 0xffff;
    list_object_at(m_, self).set_flash_count(static_cast<std::uint16_t>(left));
    list_object_at(m_, self).set_colour(static_cast<Word>((blue & 0xff0000) | green | red));
    if (left != 0) {
        return;
    }
    const Word saved = list_object_at(m_, self).saved_colour();
    if ((saved & 0x02000000) != 0) {
        flags |= 0x0400;
    } else {
        flags &= 0xfbff;
    }
    list_object_at(m_, self).set_colour(static_cast<Word>(saved));
    list_object_at(m_, self).set_visibility_flags(static_cast<std::uint16_t>(flags));
    list_object_at(m_, self).set_saved_colour(0);
}

// 0x00480060
Word WorldKit::object_construct(Word self) {
    thiscall(kInitRecord, self);
    for (Word offset : {0x4cu, 0x50u, 0x54u, 0x58u, 0x5cu, 0x60u, 0x90u, 0x94u, 0x98u, 0xbcu, 0xc0u, 0xc4u}) {
        set_word_at(m_, self + offset, 0);
    }
    for (Word offset : {0x70u, 0x72u, 0x74u, 0x76u, 0x78u, 0x7au, 0x80u, 0x82u, 0x84u, 0x9cu, 0x9eu, 0xa0u}) {
        set_half_at(m_, self + offset, 0);
    }
    for (Word offset : {0x6cu, 0x6du, 0x6eu, 0x7cu, 0x7du, 0x7eu}) {
        set_byte_at(m_, self + offset, 1);
    }
    list_object_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(object::kBaseVtable)});
    set_byte_at(m_, self + object::kState, byte_at(m_, self + object::kState) | 0x12);
    list_object_at(m_, self).set_field_a8(0x32);
    list_object_at(m_, self).set_bounding_radius(0x32);
    list_object_at(m_, self).set_field_b0(0xffff);
    list_object_at(m_, self).set_field_ac(0x12c);
    return self;
}

// 0x00480140
Word WorldKit::object_scalar_delete(Word self, Word flags) {
    object_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x00480160
void WorldKit::object_destruct(Word self) {
    list_object_at(m_, self).set_vtable(GuestPtr<void>{static_cast<Word>(object::kBaseVtable)});
    const Word sprite = list_object_at(m_, self).sprite();
    if (sprite != 0) {
        virtual_call(sprite, 0, {1});
    }
    thiscall(kDestroyRecord, self);
}

// 0x004801c0
void WorldKit::record_destruct_thunk(Word self) { thiscall(kDestroyRecord, self); }

// 0x004801d0 (thiscall on the object, one argument: the address of the list's head word): the object becomes the head of the doubly linked list (next at +0x20, previous at +0x34).
void WorldKit::object_list_push_head(Word self, Word list) {
    const Word head = word_at(m_, list);
    list_object_at(m_, self).set_previous(0);
    list_object_at(m_, self).set_next(GuestPtr<guest::RuntimeListObject>{static_cast<Word>(head)});
    set_word_at(m_, list, self);
    if (head != 0) {
        set_word_at(m_, head + object::kPrevious, self);
    }
}

// 0x004801f0 (thiscall, the list): unlinks the object. A suspended object is first returned to the list it came from, unless the list given is the suspended list itself. The object keeps its own links.
void WorldKit::object_list_remove(Word self, Word list) {
    if ((byte_at(m_, self + object::kState) & 1) != 0 && list != kSuspendedList) {
        object_unsuspend(self);
    }
    const Word next = list_object_at(m_, self).next().address;
    if (next != 0) {
        set_word_at(m_, next + object::kPrevious, list_object_at(m_, self).previous());
    }
    const Word previous = list_object_at(m_, self).previous();
    if (previous != 0) {
        set_word_at(m_, previous + object::kNext, list_object_at(m_, self).next().address);
    }
    if (word_at(m_, list) == self) {
        set_word_at(m_, list, list_object_at(m_, self).next().address);
    }
}

// 0x004802f0 (thiscall, the list the object is in): moves the object to the suspended list and remembers where it came from (+0x64).
void WorldKit::object_suspend_to_list(Word self, Word list) {
    ob_stamp(0x187);
    check((byte_at(m_, self + object::kState) & 1) == 0, 0x00535b68);
    ob_stamp(0x189);
    check(list != 0, 0x00535b60);
    virtual_call(self, 1);
    list_object_at(m_, self).set_suspended_list(static_cast<Word>(list));
    object_list_remove(self, list);
    object_list_push_head(self, kSuspendedList);
    set_byte_at(m_, self + object::kState, byte_at(m_, self + object::kState) | 1);
}

// 0x004803b0 (thiscall): the reverse of the above.
void WorldKit::object_unsuspend(Word self) {
    if ((byte_at(m_, self + object::kState) & 1) == 0) {
        return;
    }
    object_list_remove(self, kSuspendedList);
    object_list_push_head(self, list_object_at(m_, self).suspended_list());
    set_half_at(m_, self + object::kState, half_at(m_, self + object::kState) & 0xfffe);
}

// the assertion macro's line and file name of ob.cpp (0x00535b38)
void WorldKit::ob_stamp(Word line) {
    set_global_word(m_, kAssertionLine, line);
    set_string(kAssertionFile, kObFile);
}

// 0x00480240
Word WorldKit::read_fixed_position(Word self, Word record) {
    check((record & 3u) == 0, 0x00535b50);
    set_word_at(m_, self + 0x08, word_at(m_, record) << 12);
    set_word_at(m_, self + 0x0c, word_at(m_, record + 4) << 12);
    set_word_at(m_, self + 0x10, word_at(m_, record + 8) << 12);
    return record + 12;
}

// 0x004802c0
Word WorldKit::read_object_parameters(Word self, Word record) {
    list_object_at(m_, self).set_pitch_angle(static_cast<std::int16_t>(half_at(m_, record)));
    list_object_at(m_, self).set_yaw_angle(static_cast<std::int16_t>(half_at(m_, record + 2)));
    list_object_at(m_, self).set_field_18(static_cast<std::uint16_t>(half_at(m_, record + 4)));
    return record + 6;
}

// 0x004803e0
Word WorldKit::set_state_bit(Word self, Word mode, Word bits) {
    if (mode == 1) {
        const Word current = list_object_at(m_, self).field_86();
        if (((bits & 0xff) & current) != 0) {
            return 0;
        }
        list_object_at(m_, self).set_field_86(static_cast<std::uint8_t>(current | (bits & 0xff)));
        return 1;
    }
    list_object_at(m_, self).set_field_86(static_cast<std::uint8_t>(list_object_at(m_, self).field_86() & ~bits));
    return 0;
}

// 0x00480420
void WorldKit::want_sprite(Word self) { set_byte_at(m_, self + object::kState, byte_at(m_, self + object::kState) | 0x08); }

// 0x00480430
void WorldKit::release_sprite(Word self) {
    const Word sprite = list_object_at(m_, self).sprite();
    set_half_at(m_, self + object::kState, half_at(m_, self + object::kState) & 0xfff7);
    if (sprite != 0) {
        virtual_call(sprite, 0, {1});
        list_object_at(m_, self).set_sprite(0);
    }
}

// 0x00480460
void WorldKit::sprite_update(Word self) {
    if ((byte_at(m_, self + object::kState) & 0x08) == 0) {
        release_sprite(self);
        return;
    }
    if (list_object_at(m_, self).sprite() == 0) {
        set_global_word(m_, kSpriteFlag, 0);
        Word block = engine_value(kSpriteAlloc, {0x6c});
        if (block != 0) {
            block = thiscall_value(kSpriteConstruct, block);
        }
        list_object_at(m_, self).set_sprite(static_cast<Word>(block));
        set_global_word(m_, kSpriteFlag, 1);
        const Word sprite = list_object_at(m_, self).sprite();
        thiscall(kSpriteSetTexture, sprite, {0x005344dc, 0});
        thiscall(kSpriteSetMode, sprite, {4});
        thiscall(kSpriteSetup, sprite);
        set_half_at(m_, list_object_at(m_, self).sprite() + 0x38, 0x20);
        set_byte_at(m_, list_object_at(m_, self).sprite() + 0x3a, 1);
    }
    if (static_cast<std::int16_t>(list_object_at(m_, self).field_9e()) < -0x80) {
        // the three words (x, y, z) the call is handed live in a local of the caller
        // (the probe records the bytes of the local as the call's text)
        std::string local;
        for (const Word offset : {0x9cu, 0x9eu, 0xa0u}) {
            local.push_back(static_cast<char>(byte_at(m_, self + offset)));
            local.push_back(static_cast<char>(byte_at(m_, self + offset + 1)));
        }
        const Word size = list_object_at(m_, self).field_a8();
        engine(kSpriteSetFrame, {list_object_at(m_, self).sprite(), self + 0x90, kScratch, size, size, 0}, local);
    }
    const std::int32_t whole = static_cast<std::int16_t>(list_object_at(m_, self).field_ac());
    const std::int32_t part = static_cast<std::int16_t>(list_object_at(m_, self).field_aa());
    std::int32_t scale = divide((whole - part) << 7, whole);
    if (scale < 0) {
        scale = 0;
    }
    const std::int32_t high = static_cast<std::int32_t>((static_cast<std::int64_t>(scale) * 0x2aaaaaab) >> 32);
    thiscall(kSpriteSetScale, list_object_at(m_, self).sprite(), {u(high + static_cast<std::int32_t>(static_cast<Word>(high) >> 31))});
}

// 0x004805f0
Word WorldKit::is_dying(Word self) { return (byte_at(m_, self + object::kState) >> 6) & 1u; }

// 0x004805d0
void WorldKit::mark_dying_if_idle(Word self) {
    if (is_dying(self) == 0) {
        set_byte_at(m_, self + object::kState, byte_at(m_, self + object::kState) | 0x40);
    }
}

// 0x00480600
Word WorldKit::always_true() { return 1; }

// 0x00480610
Word WorldKit::animated_construct(Word self) {
    object_construct(self);
    animated_at(m_, self).set_visibility_flags(static_cast<std::uint8_t>(animated_at(m_, self).visibility_flags() | 0x02));
    animated_at(m_, self).set_field_106(1);
    animated_at(m_, self).set_field_107(1);
    animated_at(m_, self).set_field_163(1);
    animated_at(m_, self).set_vtable(static_cast<Word>(kDerivedVtable));
    animated_at(m_, self).set_field_d8(0);
    animated_at(m_, self).set_field_108(0x10000);
    set_word_at(m_, self + kPartOrderStale, 0);
    return self;
}

// 0x00480660
Word WorldKit::animated_scalar_delete(Word self, Word flags) {
    animated_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// 0x00480680
void WorldKit::animated_destruct(Word self) {
    animated_at(m_, self).set_vtable(static_cast<Word>(kDerivedVtable));
    for (Word offset : {0x138u, 0x13cu, 0xecu, 0xe8u}) {
        const Word block = word_at(m_, self + offset);
        if (block != 0) {
            mem_delete(block);
        }
    }
    object_destruct(self);
}

// 0x00480c30
Word WorldKit::frame_reached(Word self, Word track, std::int32_t frame) {
    if (track != animated_at(m_, self).field_110()) {
        return 0;
    }
    const std::int32_t first = static_cast<std::int16_t>(animated_at(m_, self).field_10c());
    const std::int32_t last = static_cast<std::int16_t>(animated_at(m_, self).field_10e());
    const bool backwards = static_cast<std::int8_t>(animated_at(m_, self).field_112()) < 0;
    if (first <= last) {
        if (!backwards) {
            return (first > frame || frame > last) ? 0 : 1;
        }
        if (frame <= first) {
            return 1;
        }
        return frame < last ? 0 : 1;
    }
    if (!backwards) {
        if (frame <= last) {
            return 1;
        }
        return first > frame ? 0 : 1;
    }
    if (last > frame) {
        return 0;
    }
    return frame > first ? 0 : 1;
}

// the model-part order table (0x00480cd0, native since the skater work): part -> its place in the model
void WorldKit::assign_model_part_order(Word self, Word part, Word order) {
    check((part & 0xff) < 0x13, 0x00535c04);
    check((order & 0xff) < 0x13, 0x00535bec);
    set_byte_at(m_, self + kPartOrder + (part & 0xff), order);
    set_word_at(m_, self + kPartOrderStale, 1);
}

bool WorldKit::strings_equal(Word a, Word b) const {
    for (;;) {
        const Word left = byte_at(m_, a++);
        const Word right = byte_at(m_, b++);
        if (left != right) {
            return false;
        }
        if (left == 0) {
            return true;
        }
    }
}

// 0x00480d90
void WorldKit::merge_model_parts(Word self, Word order_names, std::int32_t order_count, Word part_names, std::int32_t part_count) {
    check(order_names != 0, 0x00535c54);
    check(part_names != 0, 0x00535c38);
    check(order_count != 0, 0x00535c28);
    check(part_count != 0, 0x00535c18);
    for (std::int32_t part = 0; part < part_count; ++part) {
        if (order_count <= 0) {
            continue;
        }
        const Word wanted = word_at(m_, part_names + 4u * static_cast<Word>(part));
        for (std::int32_t order = 0; order < order_count; ++order) {
            if (strings_equal(wanted, word_at(m_, order_names + 4u * static_cast<Word>(order)))) {
                assign_model_part_order(self, u(part), u(order));
                break;
            }
        }
    }
}

// 0x00480950 (fastcall on the object): the animation cursor in the object's words (frame +0xf4, mode +0xf8, direction +0x100, endpoints +0x101/+0x102, fraction +0x104,
// finished +0x107, the saved start +0x114) steps by the rate times the global animation time scale; the cursor model does the arithmetic.
void WorldKit::animation_advance(Word self) {
    AnimationCursor cursor;
    cursor.frame = static_cast<std::int16_t>(animated_at(m_, self).field_f4());
    cursor.id = static_cast<std::uint16_t>(animated_at(m_, self).field_f6());
    cursor.mode = static_cast<std::uint8_t>(animated_at(m_, self).field_f8());
    cursor.target_frame = static_cast<std::int16_t>(animated_at(m_, self).field_fa());
    cursor.target_frame2 = animated_at(m_, self).field_fc();
    cursor.mode3_clock = animated_at(m_, self).field_fe();
    cursor.direction = static_cast<std::int8_t>(animated_at(m_, self).field_100());
    cursor.endpoint = static_cast<std::int8_t>(animated_at(m_, self).field_101());
    cursor.alternate_endpoint = static_cast<std::int8_t>(animated_at(m_, self).field_102());
    cursor.fraction = animated_at(m_, self).field_104();
    cursor.frame_count = static_cast<std::uint8_t>(animated_at(m_, self).field_106());
    cursor.finished = animated_at(m_, self).field_107() != 0;
    cursor.rate = animated_at(m_, self).field_108();
    cursor.request_start = static_cast<std::int16_t>(animated_at(m_, self).field_114());
    const bool was_finished = cursor.finished;
    (void)cursor.advance(global_int(m_, kAnimationTimeScale), global_int(m_, kAnimationClock));
    animated_at(m_, self).set_field_f4(static_cast<std::uint16_t>(static_cast<std::uint16_t>(cursor.frame)));
    animated_at(m_, self).set_field_fa(static_cast<std::uint16_t>(static_cast<std::uint16_t>(cursor.target_frame)));
    animated_at(m_, self).set_field_100(static_cast<std::uint8_t>(static_cast<std::uint8_t>(cursor.direction)));
    animated_at(m_, self).set_field_101(static_cast<std::uint8_t>(static_cast<std::uint8_t>(cursor.endpoint)));
    animated_at(m_, self).set_field_102(static_cast<std::uint8_t>(static_cast<std::uint8_t>(cursor.alternate_endpoint)));
    animated_at(m_, self).set_field_104(static_cast<std::uint16_t>(cursor.fraction));
    if (cursor.finished != was_finished) {
        animated_at(m_, self).set_field_107(static_cast<std::uint8_t>(cursor.finished ? 1 : 0));
    }
    animated_at(m_, self).set_field_114(static_cast<std::uint16_t>(static_cast<std::uint16_t>(cursor.request_start)));
}

// 0x00480fa0 (fastcall on the object): an object that plays an animation (state bit 0x02) steps it unless a movie is playing, then its vtable's slots 4 and 3 run (the update and
// the pose, in that order).
void WorldKit::invoke_update_hooks(Word self) {
    if ((animated_at(m_, self).visibility_flags() & 2) != 0 && (engine_value(0x004cde70) & 0xff) == 0) {   // Video_Playing()
        animation_advance(self);
    }
    virtual_call(self, 4);
    virtual_call(self, 3);
}

// 0x00480730 (thiscall, four arguments): starts the animation `animation` of the object's region (a number past the region's table plays 0x2e) from frame `from` to frame `to` (-1 is the last
// frame, both are clamped into the animation); `end_mode` is kept at +0x102. The direction (+0x100) is 1 forward, -1 backward, 0 for a single frame.
void WorldKit::animation_start(Word self, Word animation, std::int32_t from, std::int32_t to, Word end_mode) {
    animated_at(m_, self).set_field_f6(static_cast<std::uint16_t>(animation));
    ob_stamp(0x237);
    const Word slot = kAnimationTables + (animated_at(m_, self).region() * 0x44);
    check(animated_at(m_, self).field_f6() < word_at(m_, word_at(m_, slot)), 0x00535b88);
    if (animated_at(m_, self).field_f6() >= word_at(m_, word_at(m_, slot))) {
        animated_at(m_, self).set_field_f6(0x2e);
    }
    const Word frames = byte_at(m_, word_at(m_, slot) + 8 + animated_at(m_, self).field_f6() * 8);
    animated_at(m_, self).set_field_106(static_cast<std::uint8_t>(frames));
    if (from == -1) {
        from = s(frames) - 1;
    }
    if (to == -1) {
        to = s(frames) - 1;
    }
    from = from < 0 ? 0 : from;
    to = to < 0 ? 0 : to;
    from = from >= s(frames) ? s(frames) - 1 : from;
    to = to >= s(frames) ? s(frames) - 1 : to;
    animated_at(m_, self).set_field_f8(0);
    const std::int32_t direction = to > from ? 1 : (to >= from ? 0 : -1);
    animated_at(m_, self).set_field_101(static_cast<std::uint8_t>(u(to)));
    animated_at(m_, self).set_field_100(static_cast<std::uint8_t>(u(direction)));
    animated_at(m_, self).set_field_102(static_cast<std::uint8_t>(end_mode));
    animated_at(m_, self).set_field_114(static_cast<std::uint16_t>(u(from)));
    animated_at(m_, self).set_field_f4(static_cast<std::uint16_t>(u(from)));
    animated_at(m_, self).set_field_104(0);
    animated_at(m_, self).set_field_107(static_cast<std::uint8_t>((from & 0xffff) == (static_cast<std::int8_t>(to) & 0xffff) ? 1 : 0));
}

// 0x00480ff0 (cdecl, the list head's address): the per-frame loop over one object list. The next object is read before the current one runs, so an object that parks itself or is deleted does not
// break the walk. A live object is flashed, then (when it may be suspended and every skater is beyond the suspend distance) parked on the suspended list, otherwise its nearest-skater
// distance is kept at +0xb8 and its animation and update run; a dying object (0x40) waits one more frame (0x80) and is then deleted through its vtable.
void WorldKit::object_ai_frame(Word list) {
    Word object = word_at(m_, list);
    while (object != 0) {
        const Word next = word_at(m_, object + object::kNext);
        if (half_at(m_, object + object::kFlashCount) != 0) {
            thiscall(kFlashUpdate, object);
        }
        ob_stamp(0x396);
        check((byte_at(m_, object + object::kState) & 1) == 0, 0x00535b68);
        const Word flags = half_at(m_, object + object::kState);
        if ((flags & 0x40) == 0) {
            Word distance = core_.crap_distance_of(global_word(m_, kPlayerOne) + 8, object + object::kPosition);
            if (global_word(m_, kPlayerNumber) == 2 && global_word(m_, kPlayerTwo) != 0) {
                const Word other = core_.crap_distance_of(global_word(m_, kPlayerTwo) + 8, object + object::kPosition);
                if (other < distance) {
                    distance = other;
                }
            }
            set_word_at(m_, object + 0xb8, distance);
            if ((byte_at(m_, object + object::kState) & 2) == 0 || distance <= suspend_reach(m_, object)) {
                thiscall(kInvokeUpdateHooks, object);
                if ((byte_at(m_, object + object::kState) & 8) != 0) {
                    thiscall(kSpriteUpdate, object);
                }
            } else {
                thiscall(kSuspendToList, object, {list});
            }
        } else if ((flags & 0x80) == 0) {
            set_half_at(m_, object + object::kState, flags | 0x80);
        } else {
            virtual_call(object, 0, {1});
        }
        object = next;
    }
}

// 0x00480fd0 (cdecl, the first object of a list)
void WorldKit::object_clear_input_flags(Word first) {
    Word object = first;
    while (object != 0) {
        const Word next = word_at(m_, object + object::kNext);
        set_half_at(m_, object + kInputFlags, half_at(m_, object + kInputFlags) & 1);
        object = next;
    }
}

// 0x00481110 (cdecl, the list head's address): the next object is read before the current one runs. A live object gets its virtual update (slot 3); a dying one (0x40) waits one frame (0x80) and is
// then deleted through slot 0.
void WorldKit::object_quick_ai(Word list) {
    Word object = word_at(m_, list);
    while (object != 0) {
        const Word next = word_at(m_, object + object::kNext);
        const Word flags = half_at(m_, object + object::kState);
        if ((flags & 0x40) == 0) {
            virtual_call(object, 3);
        } else if ((flags & 0x80) == 0) {
            set_half_at(m_, object + object::kState, flags | 0x80);
        } else {
            virtual_call(object, 0, {1});
        }
        object = next;
    }
}

// 0x00481150 (cdecl): a suspended object that may be suspended (bit 2) is woken when the nearest of the two player records (the record at 0x0056a960 and its successor, for two players) is within the
// suspend distance of it.
void WorldKit::object_maybe_unsuspend() {
    Word object = global_word(m_, kSuspendedList);
    while (object != 0) {
        const Word next = word_at(m_, object + object::kNext);
        if ((byte_at(m_, object + object::kState) & 2) != 0) {
            Word distance = core_.crap_distance_of(global_word(m_, kPlayerRecord) + 8, object + object::kPosition);
            if (global_word(m_, kPlayerNumber) == 2) {
                const Word other = word_at(m_, global_word(m_, kPlayerRecord) + object::kNext);
                if (other != 0) {
                    const Word second = core_.crap_distance_of(other + 8, object + object::kPosition);
                    if (second < distance) {
                        distance = second;
                    }
                }
            }
            if (distance <= suspend_reach(m_, object)) {
                thiscall(kUnSuspend, object);
            }
        }
        object = next;
    }
}

// 0x00480f50
void WorldKit::update_pose_frame(Word self, Word frame) {
    if (word_at(m_, self + kPoseLinks) == 0) {
        engine(kReadLinks, {self, frame});
        set_word_at(m_, self + kPoseFrame, frame);
    }
    engine(kInterpolatePose, {self});
    if ((animated_at(m_, self).visibility_flags() & 4) != 0) {
        engine(kBuildTransform, {self});
        engine(kBuildPose, {self});
    }
}

}  // namespace opentony::runtime::worldkit
