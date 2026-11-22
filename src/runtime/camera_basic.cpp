#include "camera_views.hpp"
#include "free_look.hpp"

// Phase 4.15b: the small functions of camera.cpp and camera.h: the quaternion class, the camera rigs' constructors and getters, the camera's setters, the override save / restore, the shake, the blend
// target and the empty virtual hooks. A camera (CCamera) is an object of about 0x6a0 bytes; `self` is its address. The four rigs (0x94 bytes each, at +0xcc, +0x180, +0x234 and +0x2e8) and the
// quaternion records inside it use the same classes as the free-standing ones.

namespace opentony::runtime::camerakit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kObjectFree = 0x0047fd60;               // Object_Free(object)
constexpr Word kObjectListRemove = 0x004801f0;         // ObjectList_Remove (thiscall on the object, one argument: the list)
constexpr Word kBaseDestruct = 0x00480160;             // the base class destructor (fastcall)
constexpr Word kCameraList = 0x0055fa38;               // CameraList
constexpr Word kCameraCount = 0x0055fa34;              // how many cameras exist
constexpr Word kRigStride = 0xb4;
constexpr Word kFirstRig = 0xcc;
constexpr Word kLastRigPlus = 0x2e8;

// the rig record
constexpr Word kRigVtable = 0x005184dc;

// section 1: the fields cleared before the pad is looked at; 2: the vtable and the three words after it; 3: after the pad
struct ConstructStore {
    std::uint16_t offset;
    std::uint8_t size;
    std::uint8_t section;
    Word value;
};
constexpr ConstructStore kConstructStores[] = {
    {0x3ac, 1, 1, 0x1},
    {0x3b0, 4, 1, 0},
    {0x3b4, 4, 1, 0},
    {0x3b8, 4, 1, 0},
    {0x3bc, 1, 1, 0},
    {0x3c0, 4, 1, 0},
    {0x3c4, 4, 1, 0},
    {0x3c8, 4, 1, 0},
    {0x3cc, 4, 1, 0},
    {0x3d0, 4, 1, 0},
    {0x3d4, 4, 1, 0},
    {0x3e0, 1, 1, 0x1},
    {0x3e4, 4, 1, 0},
    {0x3e8, 4, 1, 0},
    {0x3ec, 4, 1, 0},
    {0x3f0, 4, 1, 0},
    {0x3f4, 4, 1, 0},
    {0x3f8, 4, 1, 0},
    {0x3fc, 4, 1, 0},
    {0x400, 4, 1, 0},
    {0x404, 4, 1, 0},
    {0x40c, 4, 1, 0xc},
    {0x418, 1, 1, 0},
    {0x419, 1, 1, 0},
    {0x41c, 4, 1, 0},
    {0x420, 4, 1, 0},
    {0x424, 4, 1, 0},
    {0x428, 4, 1, 0},
    {0x42c, 4, 1, 0},
    {0x430, 4, 1, 0},
    {0x434, 2, 1, 0},
    {0x436, 2, 1, 0},
    {0x438, 4, 1, 0},
    {0x43c, 4, 1, 0},
    {0x440, 4, 1, 0},
    {0x448, 4, 1, 0},
    {0x44c, 4, 1, 0},
    {0x450, 4, 1, 0},
    {0x454, 4, 1, 0x1000},
    {0x444, 4, 1, 0x5184d8},
    {0x45c, 4, 1, 0},
    {0x460, 4, 1, 0},
    {0x464, 4, 1, 0},
    {0x468, 4, 1, 0x1000},
    {0x458, 4, 1, 0x5184d8},
    {0x470, 4, 1, 0},
    {0x474, 4, 1, 0},
    {0x478, 4, 1, 0},
    {0x47c, 4, 1, 0x1000},
    {0x46c, 4, 1, 0x5184d8},
    {0x484, 4, 1, 0},
    {0x488, 4, 1, 0},
    {0x48c, 4, 1, 0},
    {0x490, 4, 1, 0x1000},
    {0x480, 4, 1, 0x5184d8},
    {0x498, 4, 1, 0},
    {0x49c, 4, 1, 0},
    {0x4a0, 4, 1, 0},
    {0x4a4, 4, 1, 0x1000},
    {0x494, 4, 1, 0x5184d8},
    {0x4aa, 2, 1, 0},
    {0x4ac, 2, 1, 0},
    {0x4ae, 2, 1, 0},
    {0x4b0, 4, 1, 0},
    {0x4b4, 4, 1, 0},
    {0x4b8, 4, 1, 0},
    {0x4bc, 4, 1, 0},
    {0x4c0, 4, 1, 0},
    {0x4c4, 4, 1, 0},
    {0x4c8, 4, 1, 0},
    {0x4cc, 4, 1, 0},
    {0x4d0, 4, 1, 0},
    {0x4d4, 4, 1, 0xffffffff},
    {0x4d8, 4, 1, 0xffffffff},
    {0x4dc, 2, 1, 0x200},
    {0x4de, 2, 1, 0x310},
    {0x4e0, 2, 1, 0x568},
    {0x4f2, 2, 1, 0},
    {0x4f4, 2, 1, 0},
    {0x4f6, 2, 1, 0},
    {0x4f8, 1, 1, 0},
    {0x4f9, 1, 1, 0},
    {0x4fa, 1, 1, 0},
    {0x4fc, 2, 1, 0},
    {0x4fe, 2, 1, 0},
    {0x500, 2, 1, 0},
    {0x504, 4, 1, 0x1},
    {0x508, 4, 1, 0},
    {0x50c, 4, 1, 0xf},
    {0x520, 4, 1, 0},
    {0x524, 4, 1, 0},
    {0x528, 4, 1, 0},
    {0x52c, 4, 1, 0x1000},
    {0x51c, 4, 1, 0x5184d8},
    {0x514, 4, 1, 0x5184d4},
    {0x550, 1, 1, 0},
    {0x554, 4, 1, 0},
    {0x558, 4, 1, 0},
    {0x560, 1, 1, 0},
    {0x564, 4, 1, 0},
    {0x568, 4, 1, 0},
    {0x56c, 4, 1, 0},
    {0x574, 4, 1, 0},
    {0x578, 4, 1, 0},
    {0x57c, 4, 1, 0},
    {0x584, 4, 1, 0},
    {0x588, 4, 1, 0},
    {0x58c, 4, 1, 0},
    {0x590, 4, 1, 0x1000},
    {0x580, 4, 1, 0x5184d8},
    {0x594, 4, 1, 0},
    {0x598, 4, 1, 0},
    {0x59c, 4, 1, 0},
    {0x5a4, 4, 1, 0},
    {0x5a8, 4, 1, 0},
    {0x5ac, 4, 1, 0},
    {0x5b0, 4, 1, 0x1000},
    {0x5a0, 4, 1, 0x5184d8},
    {0x5b8, 4, 1, 0},
    {0x5bc, 4, 1, 0},
    {0x5c0, 4, 1, 0},
    {0x5c4, 4, 1, 0},
    {0x5c8, 4, 1, 0},
    {0x5cc, 4, 1, 0},
    {0x610, 4, 1, 0},
    {0x614, 4, 1, 0},
    {0x618, 4, 1, 0},
    {0x638, 2, 1, 0},
    {0x650, 4, 1, 0},
    {0x654, 4, 1, 0},
    {0x658, 4, 1, 0},
    {0x65c, 4, 1, 0x1000},
    {0x64c, 4, 1, 0x5184d8},
    {0x0, 4, 2, 0x5184b8},
    {0x670, 4, 2, 0},
    {0x63c, 4, 2, 0},
    {0x640, 4, 2, 0},
    {0x644, 4, 2, 0},
    {0x610, 4, 3, 0},
    {0x614, 4, 3, 0},
    {0x618, 4, 3, 0x1000},
    {0x5d4, 1, 3, 0},
    {0x5e8, 4, 3, 0x3e8},
    {0x5b4, 4, 3, 0},
};

}  // namespace

// ---- the quaternion class ----

// 0x0040e000 (thiscall, four arguments)
Word CameraKit::quaternion_construct(Word self, Word x, Word y, Word z, Word w) {
    quaternion_at(m_, self).set_field_4(static_cast<Word>(x));
    quaternion_at(m_, self).set_field_8(static_cast<Word>(y));
    quaternion_at(m_, self).set_field_c(static_cast<Word>(z));
    quaternion_at(m_, self).set_field_10(static_cast<Word>(w));
    quaternion_at(m_, self).set_field_0(static_cast<Word>(quaternion::kVtableValue));
    return self;
}

// 0x0040e050
void CameraKit::quaternion_destruct(Word self) { set_word_at(m_, self, quaternion::kPlainVtable); }

// 0x0040e030
Word CameraKit::quaternion_scalar_delete(Word self, Word flags) {
    quaternion_destruct(self);
    if ((flags & 1u) != 0) {
        core_.class_delete(self);
    }
    return self;
}

// 0x0040e060
Word CameraKit::quaternion_copy_construct(Word self, Word source) {
    quaternion_at(m_, self).set_field_0(static_cast<Word>(quaternion::kPlainVtable));
    quaternion_at(m_, self).set_field_4(static_cast<Word>(word_at(m_, source + quaternion::kX)));
    quaternion_at(m_, self).set_field_8(static_cast<Word>(word_at(m_, source + quaternion::kY)));
    quaternion_at(m_, self).set_field_c(static_cast<Word>(word_at(m_, source + quaternion::kZ)));
    quaternion_at(m_, self).set_field_10(static_cast<Word>(word_at(m_, source + quaternion::kW)));
    quaternion_at(m_, self).set_field_0(static_cast<Word>(quaternion::kVtableValue));
    return self;
}

// ---- the camera rigs ----

// the stores of the rig constructors: the three quaternions are the identity, the vectors zero
void CameraKit::rig_clear(Word self) {
    for (Word offset : {0x0cu, 0x10u, 0x14u, 0x20u, 0x24u, 0x28u, 0x30u, 0x34u, 0x38u, 0x44u, 0x48u, 0x4cu, 0x54u, 0x58u, 0x5cu, 0x68u, 0x6cu, 0x70u, 0x78u, 0x7cu, 0x80u, 0x88u, 0x8cu, 0x90u}) {
        set_word_at(m_, self + offset, 0);
    }
    rig_at(m_, self).set_field_2c(0x1000);
    rig_at(m_, self).set_field_50(0x1000);
    rig_at(m_, self).set_field_84(0x1000);
    rig_at(m_, self).set_field_1c(static_cast<Word>(quaternion::kVtableValue));
    rig_at(m_, self).set_field_40(static_cast<Word>(quaternion::kVtableValue));
    rig_at(m_, self).set_field_74(static_cast<Word>(quaternion::kVtableValue));
    rig_at(m_, self).set_field_0(static_cast<Word>(kRigVtable));
}

// 0x004117f0 (fastcall)
void CameraKit::rig_construct(Word self) { rig_clear(self); }

// 0x004119e0 (fastcall)
void CameraKit::rig_destruct(Word self) {
    rig_at(m_, self).set_field_74(static_cast<Word>(quaternion::kPlainVtable));
    rig_at(m_, self).set_field_40(static_cast<Word>(quaternion::kPlainVtable));
    rig_at(m_, self).set_field_1c(static_cast<Word>(quaternion::kPlainVtable));
    rig_at(m_, self).set_field_0(static_cast<Word>(quaternion::kPlainVtable));
}

// 0x00411880
Word CameraKit::rig_scalar_delete(Word self, Word flags) {
    rig_destruct(self);
    if ((flags & 1u) != 0) {
        core_.class_delete(self);
    }
    return self;
}

// 0x004118a0: a rig from a level record of shorts {x, y, z, w of the orientation, the distance word, three offsets}: the quaternion is scaled to unit length, the offsets to Q12
Word CameraKit::rig_construct_from_record(Word self, Word record) {
    rig_clear(self);
    rig_at(m_, self).set_field_20(static_cast<Word>(u(short_at(m_, record))));
    rig_at(m_, self).set_field_24(static_cast<Word>(u(short_at(m_, record + 2))));
    rig_at(m_, self).set_field_28(static_cast<Word>(u(short_at(m_, record + 4))));
    rig_at(m_, self).set_field_2c(static_cast<Word>(u(short_at(m_, record + 6))));
    const Word x = rig_at(m_, self).field_20();
    const Word y = rig_at(m_, self).field_24();
    const Word z = rig_at(m_, self).field_28();
    const Word w = rig_at(m_, self).field_2c();
    std::int32_t length = math_.isqrt(s(x * x + y * y + z * z + w * w));
    if (length == 0) {
        length = 1;
    }
    for (Word offset : {0x20u, 0x24u, 0x28u, 0x2cu}) {
        set_word_at(m_, self + offset, u(divide(s(shl(word_at(m_, self + offset), 12)), length)));
    }
    rig_at(m_, self).set_field_3c(static_cast<std::uint16_t>(half_at(m_, record + 8)));
    rig_at(m_, self).set_field_30(static_cast<Word>(u(short_at(m_, record + 10)) << 12));
    rig_at(m_, self).set_field_34(static_cast<Word>(u(short_at(m_, record + 12)) << 12));
    rig_at(m_, self).set_field_38(static_cast<Word>(u(short_at(m_, record + 14)) << 12));
    return self;
}

// 0x00411c50: the rig's 0x20-byte matrix to the caller's buffer
void CameraKit::rig_copy_matrix(Word self, Word destination) {
    for (Word k = 0; k < 8; ++k) {
        set_word_at(m_, destination + 4 * k, word_at(m_, self + 0x94 + 4 * k));
    }
}

// 0x00411c70
void CameraKit::rig_get_position(Word self, Word destination) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, destination + 4 * k, word_at(m_, self + 0x88 + 4 * k));
    }
}

// 0x00411ca0
void CameraKit::rig_get_target(Word self, Word destination) {
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, destination + 4 * k, word_at(m_, self + 0x68 + 4 * k));
    }
}

// 0x00411ec0: a camera point record: the vtable back to the base class (two places)
void CameraKit::point_destruct(Word self) {
    set_word_at(m_, self + 8, quaternion::kPlainVtable);
    set_word_at(m_, self, quaternion::kPlainVtable);
}

// 0x00411ed0
Word CameraKit::point_scalar_delete(Word self, Word flags) {
    point_destruct(self);
    if ((flags & 1u) != 0) {
        core_.class_delete(self);
    }
    return self;
}

// 0x00411ef0, 0x00411f00
void CameraKit::points_clear() { set_global_word(m_, 0x0055fae4, 0); }

void CameraKit::points_clear_and_reset_tuning() {
    set_global_word(m_, 0x0055fae4, 0);
    set_global_word(m_, 0x00524a40, 0x122);
    set_global_word(m_, 0x00524a44, 0x96);
    set_global_word(m_, 0x00524a48, 0xffffff94);
}

// ---- the camera ----

// 0x0040b650 (thiscall; two stack arguments, the skater the camera follows and the player number as a short): the camera is built. The base object, the four rigs, then the fields: the target and the
// player number, the history vectors, the ten quaternions (identity), the overrides (all ones), the window (0x200 x 0x310, 0x568), the mode (1) and the rest zero; the player number is then taken from the
// pad the skater listens to (the first pad 0, the second 1; any other is logged); the look-at point and the look target start at the skater's position; the viewport is committed and the camera joins
// the camera list.
Word CameraKit::construct(Word self, Word tripod, Word player) {
    engine(0x00480060, {self});
    cam_at(m_, self).set_field_0c8(static_cast<std::uint16_t>(player));
    rig_construct(self + kFirstRig);
    rig_construct(self + 0x180);
    rig_construct(self + 0x234);
    rig_construct(self + kLastRigPlus);
    auto apply = [&](unsigned section) {
        for (const ConstructStore& store : kConstructStores) {
            if (store.section != section) {
                continue;
            }
            switch (store.size) {
                case 1: set_byte_at(m_, self + store.offset, store.value); break;
                case 2: set_half_at(m_, self + store.offset, store.value); break;
                default: set_word_at(m_, self + store.offset, store.value); break;
            }
        }
    };
    apply(1);
    cam_at(m_, self).set_tripod_link(GuestPtr<guest::RuntimeGameplaySkater>{static_cast<Word>(tripod)});
    cam_at(m_, self).set_secondary_target_link(GuestPtr<guest::RuntimeGameplaySkater>{static_cast<Word>(tripod)});
    cam_at(m_, self).set_update_tick(0);
    cam_at(m_, self).set_field_518(static_cast<Word>(self + 0x510));
    apply(2);
    const Word pad = word_at(m_, cam_at(m_, self).tripod_link().address + 0x2ccc);
    if (pad == 0x0056aff8) {
        cam_at(m_, self).set_field_0c8(0);
    } else if (pad == 0x0056b164) {
        cam_at(m_, self).set_field_0c8(1);
    } else {
        log_line(0x00524b0c);
    }
    if (freelook::enabled()) {   // a new camera (every run builds its own) starts with the free look home (free_look.hpp); off: nothing is read
        freelook::place(cam_at(m_, self).field_0c8());
    }
    apply(3);
    check(cam_at(m_, self).tripod_link().address != 0, 0x00524ad4);
    const Word position = cam_at(m_, self).tripod_link().address + 8;
    const Word x = word_at(m_, position);
    const Word y = word_at(m_, position + 4);
    const Word z = word_at(m_, position + 8);
    set_word_at(m_, self + 0x3c0, x);
    set_word_at(m_, self + 0x3c4, y);
    set_word_at(m_, self + 0x3c8, z);
    cam_at(m_, self).set_field_3d8(0);
    const Word focus = cam_at(m_, self).secondary_target_link().address + 8;
    set_word_at(m_, self + 0x3f0, word_at(m_, focus));
    set_word_at(m_, self + 0x3f4, word_at(m_, focus + 4));
    cam_at(m_, self).set_field_408(0);
    set_word_at(m_, self + 0x3f8, word_at(m_, focus + 8));
    cam_at(m_, self).set_field_3c(0x63);
    const Word first = u(s(static_cast<std::int16_t>(global_half(m_, 0x00524a72))));
    const Word second = u(s(static_cast<std::int16_t>(global_half(m_, 0x00524a74))));
    const Word third = u(s(static_cast<std::int16_t>(global_half(m_, 0x00524a70))));
    set_global_word(m_, 0x00524a44, first);
    set_global_word(m_, 0x00524a50, 2);
    set_global_word(m_, 0x00524a40, second);
    set_global_word(m_, 0x00524a48, third);
    cam_at(m_, self).set_field_4(1);
    commit_viewport(self);
    engine(0x004801d0, {self, kCameraList});
    set_global_word(m_, kCameraCount, global_word(m_, kCameraCount) + 1u);
    cam_at(m_, self).set_field_6a(static_cast<std::uint16_t>(cam_at(m_, self).field_6a() & 0xfffdu));
    cam_at(m_, self).set_field_ae(0);
    return self;
}

// 0x0040bc00 (fastcall)
void CameraKit::destruct(Word self) {
    cam_at(m_, self).set_vtable(GuestPtr<void>{0x005184b8});
    engine(kObjectListRemove, {self, kCameraList});
    set_global_word(m_, kCameraCount, global_word(m_, kCameraCount) - 1u);
    for (Word offset : {0x64cu, 0x5a0u, 0x580u, 0x51cu, 0x514u, 0x494u, 0x480u, 0x46cu, 0x458u, 0x444u}) {
        set_word_at(m_, self + offset, quaternion::kPlainVtable);
    }
    rig_destruct(self + kLastRigPlus);
    rig_destruct(self + 0x234);
    rig_destruct(self + 0x180);
    rig_destruct(self + kFirstRig);
    engine(kBaseDestruct, {self});
}

// 0x0040bbe0
Word CameraKit::scalar_delete(Word self, Word flags) {
    destruct(self);
    if ((flags & 1u) != 0) {
        engine(kObjectFree, {self});
    }
    return self;
}

// the setters of the overrides and the window (0x0040bce0..0x0040bd30)
void CameraKit::set_overrides(Word self, Word value) {
    cam_at(m_, self).set_field_4d4(static_cast<Word>(value));
    cam_at(m_, self).set_field_4d8(static_cast<Word>(value));
}
void CameraKit::set_override_a(Word self, Word value) { set_word_at(m_, self + 0x4d4, value); }
void CameraKit::set_override_b(Word self, Word value) { set_word_at(m_, self + 0x4d8, value); }
void CameraKit::set_window_width(Word self, Word value) { set_half_at(m_, self + 0x4dc, value); }
void CameraKit::set_window_height(Word self, Word value) { set_half_at(m_, self + 0x4de, value); }

// 0x0040c070: the overrides and the window shorts are saved and cleared
void CameraKit::push_overrides(Word self) {
    const Word a = cam_at(m_, self).field_4d4();
    const Word b = cam_at(m_, self).field_4d8();
    cam_at(m_, self).set_field_4e4(static_cast<Word>(a));
    cam_at(m_, self).set_field_4ec(static_cast<std::uint16_t>(cam_at(m_, self).field_4dc()));
    const Word window = cam_at(m_, self).field_4e0();
    cam_at(m_, self).set_field_4e8(static_cast<Word>(b));
    const Word height = cam_at(m_, self).field_4de();
    cam_at(m_, self).set_field_4f0(static_cast<std::uint16_t>(window));
    cam_at(m_, self).set_field_4ee(static_cast<std::uint16_t>(height));
    cam_at(m_, self).set_field_4d4(0);
    cam_at(m_, self).set_field_4d8(0);
    cam_at(m_, self).set_field_4dc(0);
    cam_at(m_, self).set_field_4de(0);
    cam_at(m_, self).set_field_4e0(0);
}

// 0x0040c0e0
void CameraKit::pop_overrides(Word self) {
    const Word a = cam_at(m_, self).field_4e4();
    const Word b = cam_at(m_, self).field_4e8();
    cam_at(m_, self).set_field_4d4(static_cast<Word>(a));
    const Word width = cam_at(m_, self).field_4ec();
    cam_at(m_, self).set_field_4d8(static_cast<Word>(b));
    const Word height = cam_at(m_, self).field_4ee();
    cam_at(m_, self).set_field_4dc(static_cast<std::uint16_t>(width));
    const Word window = cam_at(m_, self).field_4f0();
    cam_at(m_, self).set_field_4de(static_cast<std::uint16_t>(height));
    cam_at(m_, self).set_field_4e0(static_cast<std::uint16_t>(window));
}

// 0x0040c130
void CameraKit::push_override_b(Word self) {
    const Word b = cam_at(m_, self).field_4d8();
    const Word height = cam_at(m_, self).field_4de();
    cam_at(m_, self).set_field_4e8(static_cast<Word>(b));
    const Word window = cam_at(m_, self).field_4e0();
    cam_at(m_, self).set_field_4f0(static_cast<std::uint16_t>(window));
    cam_at(m_, self).set_field_4ee(static_cast<std::uint16_t>(height));
    cam_at(m_, self).set_field_4d8(0);
    cam_at(m_, self).set_field_4de(0);
    cam_at(m_, self).set_field_4e0(0);
}

// 0x0040c170
void CameraKit::pop_override_b(Word self) {
    const Word b = cam_at(m_, self).field_4e8();
    const Word height = cam_at(m_, self).field_4ee();
    cam_at(m_, self).set_field_4d8(static_cast<Word>(b));
    const Word window = cam_at(m_, self).field_4f0();
    cam_at(m_, self).set_field_4de(static_cast<std::uint16_t>(height));
    cam_at(m_, self).set_field_4e0(static_cast<std::uint16_t>(window));
}

// 0x0040be40 (cdecl; the first and third arguments are not used): every object of the list gets bit 1 of its flag word
void CameraKit::flag_objects_for_shake(Word, Word list, Word) {
    for (Word object = list; object != 0; object = word_at(m_, object + 0x20)) {
        set_half_at(m_, object + 0x68, half_at(m_, object + 0x68) | 2u);
    }
}

// 0x0040bd40: a shake of one of three types; the type's three constants are copied into the camera and the objects of both lists are flagged
void CameraKit::apply_shake(Word self, Word argument, Word type) {
    Word first = 0;
    Word second = 0;
    bool known = true;
    if (type == 0) {
        first = 0x0055f792;
        second = 0x0055f796;
    } else if (type == 1) {
        first = 0x0055f788;
        second = 0x0055f78c;
    } else if (type == 2) {
        first = 0x0055f990;
        second = 0x0055f994;
    } else {
        check(false, 0x00524b40);
        known = false;
    }
    if (known) {
        set_word_at(m_, self + 0x4f2, word_at(m_, first));
        set_half_at(m_, self + 0x4f6, half_at(m_, second));
        set_half_at(m_, self + 0x4f8, global_half(m_, 0x0055f9bc));
        set_byte_at(m_, self + 0x4fa, global_byte(m_, 0x0055f9be));
        set_word_at(m_, self + 0x4fc, global_word(m_, 0x0055f750));
        cam_at(m_, self).set_shake_phase_raw(static_cast<std::uint16_t>(global_half(m_, 0x0055f754)));
    }
    flag_objects_for_shake(argument, global_word(m_, 0x0055f6bc), type);
    flag_objects_for_shake(argument, global_word(m_, 0x0056af40), type);
}

// 0x0040f1a0: `length` frames to go from the blend value to the target (the step per frame), or, with no length, the value jumps there
void CameraKit::set_blend_target(Word self, Word target, Word length) {
    cam_at(m_, self).set_field_410_s16(static_cast<std::int16_t>(length));
    if ((length & 0xffffu) != 0) {
        const std::int32_t distance = s((target & 0xffffu) - word_at(m_, self + 0x40c));
        cam_at(m_, self).set_field_414(static_cast<std::int32_t>(u(divide(distance, static_cast<std::int32_t>(length & 0xffffu)))));
        return;
    }
    set_word_at(m_, self + 0x40c, target & 0xffffu);
}

Word CameraKit::blend_value(Word self) { return word_at(m_, self + 0x40c); }

void CameraKit::clear_flag_419(Word self) { set_byte_at(m_, self + 0x419, 0); }

Word CameraKit::in_competition_intro(Word self) { return word_at(m_, self + 0x504) == 0x17 ? 1u : 0u; }

}  // namespace opentony::runtime::camerakit
