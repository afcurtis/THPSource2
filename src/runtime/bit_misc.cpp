#include "bit_views.hpp"

// Phase 4.15d: the per-frame sweeps of the bit lists (0x00407250, 0x00407270) and bit2.cpp / blood.cpp / the small helpers between them (0x0040a740..0x0040b570): the polylines, the smoke
// generator, the line bits, the blood manager, the vector jitter and the camera's small random generator.

namespace opentony::runtime::bitkit {

using namespace views;

namespace {

using Word = std::uint32_t;

constexpr Word kPolyLineVtable = 0x00518488;
constexpr Word kGPolyLineVtable = 0x00518490;
constexpr Word kSmokeGeneratorVtable = 0x00518498;
constexpr Word kGLineVtable = 0x005184a0;
constexpr Word kGLineBitVtable = 0x005184a8;
constexpr Word kBloodVtable = 0x005184b0;
constexpr Word kClassVtable = 0x005184b4;

constexpr Word kLinePoint = 0x0056af28;          // three dwords: where a new polyline starts
constexpr Word kClassDelete = 0x00466bb0;        // CClass_OpDelete(block)
constexpr Word kQuaternionConstruct = 0x0040e000;
constexpr Word kCameraQuaternion = 0x0055f738;
constexpr Word kCameraRandA = 0x0055f7b8;
constexpr Word kCameraRandB = 0x0055f7bc;
constexpr Word kCameraRandSeed = 0x0055f9a8;
constexpr Word kSmokeName = 0x00524a10;

// the order Bit_Move walks the lists
constexpr Word kMoveOrder[] = {lists::kPixel, lists::kFlat,  lists::kGlass,     lists::kGenPoly,   lists::kChunk,  lists::kQuad, lists::kGlow,
                               lists::kNonRendered, lists::kLinked2Ended, lists::kPolyLine, lists::kGPolyLine, lists::kGLine, lists::kSpecial};

}  // namespace

// 0x00407250 MoveList(head): every live bit moves (slot 1 of its vtable); a dead one waits for the sweep
void BitKit::move_list(Word head) {
    for (Word bit = head; bit != 0; bit = word_at(m_, bit + bit::kNext)) {
        if (byte_at(m_, bit + bit::kDead) == 0) {
            virtual_call(bit, 1);
        }
    }
}

// 0x00407270 Bit_Move
void BitKit::bit_move_all() {
    for (const Word head : kMoveOrder) {
        move_list(word_at(m_, head));
    }
}

// 0x0040a740 CPolyLine::CPolyLine(segments): a record of 16 bytes per segment (point and colour), all at the start point
Word BitKit::polyline_construct(Word self, std::int32_t segments) {
    bit_construct(self);
    polyline_at(m_, self).set_frame_entry(0);
    polyline_at(m_, self).set_flags(0);
    polyline_at(m_, self).set_anim_speed(0);
    polyline_at(m_, self).set_vtable(static_cast<Word>(kPolyLineVtable));
    check(segments > 0 && segments < 100, 0x0052497c);
    const Word buffer = alloc(u(segments) << 4);
    polyline_at(m_, self).set_anim(static_cast<Word>(buffer));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + block_size(buffer) + 4);
    for (std::int32_t k = 0; k < segments; ++k) {
        const Word at = polyline_at(m_, self).anim() + 16 * u(k);
        set_word_at(m_, at + 0xc, 0x40ffffff);
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, at + 4 * j, word_at(m_, kLinePoint + 4 * j));
        }
    }
    polyline_at(m_, self).set_colour(static_cast<Word>(u(segments)));
    attach_to(self, lists::kPolyLine);
    return self;
}

// 0x0040a860
Word BitKit::polyline_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::polyline_destruct); }

// 0x0040a880
void BitKit::polyline_destruct(Word self) {
    polyline_at(m_, self).set_vtable(static_cast<Word>(kPolyLineVtable));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + (0u - 4 - block_size(polyline_at(m_, self).anim())));
    mem_delete(polyline_at(m_, self).anim());
    delete_from(self, lists::kPolyLine);
    bit_destruct(self);
}

// 0x0040a900: every segment becomes semi-transparent
void BitKit::polyline_set_semi_transparent(Word self) {
    for (std::int32_t k = 0; k < s(polyline_at(m_, self).colour()); ++k) {
        const Word at = polyline_at(m_, self).anim() + 16 * u(k) + 0xf;
        set_byte_at(m_, at, byte_at(m_, at) | 2);
    }
}

// 0x0040aa20 CGPolyLine::CGPolyLine(segments)
Word BitKit::gpolyline_construct(Word self, std::int32_t segments) {
    bit_construct(self);
    gpolyline_at(m_, self).set_anim(0);
    gpolyline_at(m_, self).set_frame_entry(0);
    gpolyline_at(m_, self).set_flags(0);
    gpolyline_at(m_, self).set_vtable(static_cast<Word>(kGPolyLineVtable));
    check(segments > 0 && segments < 100, 0x005249c4);
    const Word buffer = alloc(u(segments) << 4);
    gpolyline_at(m_, self).set_colour(static_cast<Word>(buffer));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + block_size(buffer) + 4);
    for (std::int32_t k = 0; k < segments; ++k) {
        const Word at = gpolyline_at(m_, self).colour() + 16 * u(k);
        set_word_at(m_, at + 0xc, 0x50ffffff);
        for (Word j = 0; j < 3; ++j) {
            set_word_at(m_, at + 4 * j, word_at(m_, kLinePoint + 4 * j));
        }
    }
    gpolyline_at(m_, self).set_trans_decay(static_cast<Word>(u(segments)));
    attach_to(self, lists::kGPolyLine);
    gpolyline_at(m_, self).set_anim_speed(0xff);
    gpolyline_at(m_, self).set_field_51(0xff);
    gpolyline_at(m_, self).set_scale(0xff);
    gpolyline_at(m_, self).set_field_53(0x50);
    return self;
}

// 0x0040ab50
Word BitKit::gpolyline_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::gpolyline_destruct); }

// 0x0040ab70
void BitKit::gpolyline_destruct(Word self) {
    gpolyline_at(m_, self).set_vtable(static_cast<Word>(kGPolyLineVtable));
    set_global_word(m_, kBitBytes, global_word(m_, kBitBytes) + (0u - 4 - block_size(gpolyline_at(m_, self).colour())));
    mem_delete(gpolyline_at(m_, self).colour());
    delete_from(self, lists::kGPolyLine);
    bit_destruct(self);
}

// 0x0040ad30 CSmokeGenerator::CSmokeGenerator(position, count, a, red, green, blue, b, c, d, e): puffs of smoke every frame for `duration` frames
Word BitKit::smoke_generator_construct(Word self, Word position, Word duration, Word a4, Word red, Word green, Word blue, Word a8, Word a9, Word a10, Word a11) {
    nonrendered_construct(self);
    smoke_gen_at(m_, self).set_vtable(static_cast<Word>(kSmokeGeneratorVtable));
    check(duration != 0, 0x005249f0);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    smoke_gen_at(m_, self).set_trans_decay(static_cast<Word>(a4));
    smoke_gen_at(m_, self).set_colour(static_cast<std::uint8_t>(red));
    smoke_gen_at(m_, self).set_field_42(static_cast<std::uint8_t>(blue));
    smoke_gen_at(m_, self).set_field_41(static_cast<std::uint8_t>(green));
    smoke_gen_at(m_, self).set_anim(static_cast<Word>(a8));
    smoke_gen_at(m_, self).set_flags(static_cast<Word>(a10));
    smoke_gen_at(m_, self).set_lifetime(static_cast<std::uint16_t>(duration));
    smoke_gen_at(m_, self).set_frame_entry(static_cast<Word>(a9));
    smoke_gen_at(m_, self).set_anim_speed(static_cast<Word>(a11));
    return self;
}

// 0x0040ae20
Word BitKit::smoke_generator_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::smoke_generator_destruct); }

// 0x0040ae40
void BitKit::smoke_generator_destruct(Word self) {
    smoke_gen_at(m_, self).set_vtable(static_cast<Word>(kSmokeGeneratorVtable));
    nonrendered_destruct(self);
}

// 0x0040ae50 CSmokeGenerator::Move: each frame `count` motion-blur puffs rise from the position; the generator ends after `lifetime` frames (0xffff: never)
void BitKit::smoke_generator_move(Word self) {
    constexpr Word kVelocity = kScratch + 0x100;
    set_global_word(m_, kVelocity, 0);
    set_global_word(m_, kVelocity + 4, 0);
    set_global_word(m_, kVelocity + 8, 0);
    for (std::int32_t k = 0; k < s(smoke_gen_at(m_, self).trans_decay()); ++k) {
        const std::int32_t lift = rnd(smoke_gen_at(m_, self).frame_entry());
        set_global_word(m_, kVelocity + 4, u((lift + static_cast<std::int32_t>(smoke_gen_at(m_, self).anim())) * -0x1000));
        const Word block = bit_new(100);
        Word puff = 0;
        if (block != 0) {
            const std::int32_t scale = rnd(smoke_gen_at(m_, self).anim_speed());
            puff = motion_blur_construct(block, self + bit::kPosition, kVelocity, kSmokeName, u(scale + static_cast<std::int32_t>(smoke_gen_at(m_, self).flags())), 0, 10);
        }
        set_half_at(m_, puff + 0x38, 100);
        set_half_at(m_, puff + 0x54, u(rnd(0x1000)));
        set_tint(puff, smoke_gen_at(m_, self).colour(), smoke_gen_at(m_, self).field_41(), smoke_gen_at(m_, self).field_42());
    }
    smoke_gen_at(m_, self).set_age(static_cast<std::uint16_t>(smoke_gen_at(m_, self).age() + 1));
    if (static_cast<std::int16_t>(smoke_gen_at(m_, self).age()) == static_cast<std::int32_t>(smoke_gen_at(m_, self).lifetime()) && smoke_gen_at(m_, self).lifetime() != 0xffff) {
        die(self);
    }
}

// 0x0040af70 CGLine::CGLine
Word BitKit::gline_construct(Word self) {
    bit_construct(self);
    for (Word k = 0; k < 6; ++k) {
        set_word_at(m_, self + 0x44 + 4 * k, 0);
    }
    gline_at(m_, self).set_vtable(static_cast<Word>(kGLineVtable));
    gline_at(m_, self).set_trans_decay(0x50808080);
    gline_at(m_, self).set_colour(0x55808080);
    attach_to(self, lists::kGLine);
    return self;
}

// 0x0040afe0
Word BitKit::gline_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::gline_destruct); }

// 0x0040b000
void BitKit::gline_destruct(Word self) {
    gline_at(m_, self).set_vtable(static_cast<Word>(kGLineVtable));
    delete_from(self, lists::kGLine);
    bit_destruct(self);
}

// 0x0040b060: a line bit that flies from a point of an object (CGLineBit(object, position, speed, gravity, life))
Word BitKit::gline_bit_construct(Word self, Word object, Word position, std::int32_t speed, Word gravity, Word life) {
    gline_construct(self);
    gline_bit_at(m_, self).set_field_5c(0);
    gline_bit_at(m_, self).set_field_60(0);
    gline_bit_at(m_, self).set_field_64(0);
    gline_bit_at(m_, self).set_vtable(static_cast<Word>(kGLineBitVtable));
    std::int32_t magnitude;
    if (speed < 0) {
        const std::int32_t r = rnd(u(-speed));
        magnitude = -(r + ((-speed) >> 1));
    } else {
        const std::int32_t r = rnd(u(speed));
        magnitude = r + (speed >> 1);
    }
    calculate_spark_velocity(self + bit::kVelocity, u(magnitude));
    gline_bit_at(m_, self).set_field_2c(static_cast<Word>(gravity));
    for (Word k = 0; k < 3; ++k) {
        set_byte_at(m_, self + bit::kFriction + k, 3);
    }
    const Word lifetime = u(rnd(life));
    gline_bit_at(m_, self).set_lifetime(static_cast<std::uint16_t>(lifetime + 1));
    gline_bit_at(m_, self).set_field_68(static_cast<Word>(object));
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0x5c + 4 * k, word_at(m_, position + 4 * k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + bit::kPosition + 4 * k, word_at(m_, position + 4 * k));
    }
    constexpr Word kEnd = kScratch + 0x110;
    core_.vector_add_out(kEnd, position, object + 8);
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0x50 + 4 * k, word_at(m_, kEnd + 4 * k));
    }
    for (Word k = 0; k < 3; ++k) {
        set_word_at(m_, self + 0x44 + 4 * k, word_at(m_, kEnd + 4 * k));
    }
    gline_bit_at(m_, self).set_trans_decay(0x52000080);
    const std::int32_t shade = rnd(0x9f);
    gline_bit_at(m_, self).set_colour(0x55000000);
    gline_bit_at(m_, self).set_trans_decay(static_cast<Word>(gline_bit_at(m_, self).trans_decay() | u(((shade >> 5) + 2) * 0x1000)));
    return self;
}

// 0x0040b1a0
Word BitKit::gline_bit_scalar_delete(Word self, Word flags) { return delete_with(self, flags, &BitKit::gline_bit_destruct); }

// 0x0040b1c0
void BitKit::gline_bit_destruct(Word self) {
    gline_bit_at(m_, self).set_vtable(static_cast<Word>(kGLineBitVtable));
    gline_destruct(self);
}

// 0x0040b1d0 CGLineBit::Move (fastcall, slot 1): the line's head moves by the velocity (the old head is kept in +0x5c..+0x64), the two ends are the head plus the object's position (+8, +0xc, +0x10 of
// the object at +0x68: the old ends in +0x50..+0x58, the new in +0x44..+0x4c), then each velocity component takes its acceleration (+0x28..+0x30) and loses an eighth; the age counts up and the bit dies
// when it reaches its life (the age as a signed short against the life as an unsigned one)
void BitKit::gline_bit_move(Word self) {
    const Word x = gline_bit_at(m_, self).field_10(), y = gline_bit_at(m_, self).field_14(), z = gline_bit_at(m_, self).field_18();
    const Word vx = gline_bit_at(m_, self).field_1c(), vy = gline_bit_at(m_, self).field_20(), vz = gline_bit_at(m_, self).field_24();
    const Word object = gline_bit_at(m_, self).field_68();
    const Word ox = word_at(m_, object + 8), oy = word_at(m_, object + 0xc), oz = word_at(m_, object + 0x10);
    const Word nx = vx + x, ny = vy + y, nz = vz + z;
    gline_bit_at(m_, self).set_field_5c(static_cast<Word>(x));
    gline_bit_at(m_, self).set_field_60(static_cast<Word>(y));
    gline_bit_at(m_, self).set_field_64(static_cast<Word>(z));
    gline_bit_at(m_, self).set_anim_speed(static_cast<Word>(x + ox));
    gline_bit_at(m_, self).set_field_54(static_cast<Word>(y + oy));
    gline_bit_at(m_, self).set_field_58(static_cast<Word>(oz + z));
    gline_bit_at(m_, self).set_field_10(static_cast<Word>(nx));
    gline_bit_at(m_, self).set_field_14(static_cast<Word>(ny));
    gline_bit_at(m_, self).set_field_18(static_cast<Word>(nz));
    gline_bit_at(m_, self).set_anim(static_cast<Word>(ox + nx));
    gline_bit_at(m_, self).set_frame_entry(static_cast<Word>(oy + ny));
    gline_bit_at(m_, self).set_flags(static_cast<Word>(oz + nz));
    const auto damped = [&](Word acceleration, Word velocity) {
        const std::int32_t sum = s(acceleration + velocity);
        return u(sum - (sum >> 3));
    };
    const Word dx = damped(gline_bit_at(m_, self).field_28(), vx);
    const Word dy = damped(gline_bit_at(m_, self).field_2c(), vy);
    const Word dz = damped(gline_bit_at(m_, self).field_30(), vz);
    gline_bit_at(m_, self).set_field_1c(static_cast<Word>(dx));
    gline_bit_at(m_, self).set_field_20(static_cast<Word>(dy));
    gline_bit_at(m_, self).set_field_24(static_cast<Word>(dz));
    gline_bit_at(m_, self).set_age(static_cast<std::uint16_t>(gline_bit_at(m_, self).age() + 1));
    if (static_cast<std::int16_t>(gline_bit_at(m_, self).age()) >= static_cast<std::int32_t>(gline_bit_at(m_, self).lifetime())) {
        die(self);
    }
}

// 0x0040b2e0 the blood manager's deleting destructor (the class delete is a service)
Word BitKit::blood_scalar_delete(Word self, Word flags) {
    blood_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kClassDelete, {self});
    }
    return self;
}

// 0x0040b300
void BitKit::blood_destruct(Word self) {
    blood_at(m_, self).set_vtable(static_cast<Word>(kBloodVtable));
    blood_reset(self);
    blood_at(m_, self).set_vtable(static_cast<Word>(kClassVtable));
}

// 0x0040b350 Blood::Reset: every blood bit is deleted
void BitKit::blood_reset(Word self) {
    for (std::int32_t k = 0; k < s(blood_at(m_, self).field_54()); ++k) {
        const Word piece = word_at(m_, self + 4 + 4 * u(k));
        if (piece != 0) {
            virtual_call(piece, 0, {1});
        }
        set_word_at(m_, self + 4 + 4 * u(k), 0);
    }
    blood_at(m_, self).set_field_54(0);
    blood_at(m_, self).set_field_58(0);
}

// 0x0040b3e0
void BitKit::class_destruct(Word self) { set_word_at(m_, self, kClassVtable); }

// 0x0040b3f0 the deleting destructor of the plain base class (the class delete is a service)
Word BitKit::class_scalar_delete(Word self, Word flags) {
    class_destruct(self);
    if ((flags & 1u) != 0) {
        engine(kClassDelete, {self});
    }
    return self;
}

// 0x0040b410: each component of a vector moved by a random amount about zero (x4, x2, x4)
void BitKit::jitter_vector(Word vector, std::int32_t range) {
    const std::int32_t half = range >> 1;
    set_word_at(m_, vector, word_at(m_, vector) + u((rnd(u(range)) - half) * 4));
    set_word_at(m_, vector + 4, word_at(m_, vector + 4) + u((rnd(u(range)) - half) * 2));
    set_word_at(m_, vector + 8, word_at(m_, vector + 8) + u((rnd(u(range)) - half) * 4));
}

// 0x0040b4e0
void BitKit::camera_seed(Word seed) {
    set_global_word(m_, kCameraRandA, 0x12b9b0a1);
    set_global_word(m_, kCameraRandSeed, seed);
    set_global_word(m_, kCameraRandB, 0x0aa2fb3f);
}

// 0x0040b500: the camera's own generator: a number below the limit
std::int32_t BitKit::camera_rand(Word limit) {
    const Word a = global_word(m_, kCameraRandA);
    const Word b = global_word(m_, kCameraRandB);
    const Word seed = a * global_word(m_, kCameraRandSeed) + b;
    set_global_word(m_, kCameraRandSeed, seed);
    set_global_word(m_, kCameraRandA, (a ^ seed) + u(s(seed) >> 4));
    set_global_word(m_, kCameraRandB, b + u(s(seed) >> 3) - 0x10101010u);
    return static_cast<std::int32_t>((seed & 0xffff) * limit) >> 16;
}

// 0x0040b570: the camera's quaternion is the identity
void BitKit::camera_quaternion_identity() { thiscall(kQuaternionConstruct, kCameraQuaternion, {0, 0, 0, 0x1000}); }

}  // namespace opentony::runtime::bitkit
