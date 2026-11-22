#include "auto_views.hpp"
#include "render_kit.hpp"

// Phase 4.15e: the animation pose decompressor (`decomp.cpp`, 0x004305f0..0x00431190) and the model-list object that follows it (0x00431190..0x0043125f). An animation is a stream of channel
// codes (a literal, 2..14-bit deltas, a repeat, zeros); the pose of a model is a matrix and a translation for each bone, built from the decoded angles and positions of the frame and the bone
// hierarchy (parents before children). Assertion globals (0x00568638, 0x0056881c) are not compared by the replay.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kRegionRecords = 0x0056d428;         // the per-region records (0x44 bytes each)
constexpr Word kRegionStride = 0x44;
constexpr Word kPoseBuffer = 0x00567cb0;            // AnimationTweenPoseBuffer

// the model object's fields the pose reads and writes
namespace obj {

constexpr Word kRegion = 0x1f;                      // byte: the region the model's tables live in
constexpr Word kCalcOrder = 0xe8;                   // dword: the order the bones are posed in (shorts, parents first)
constexpr Word kPoseData = 0xec;                    // dword: the decoded channels and the matrices
constexpr Word kRoot = 0xf0;                        // short: the root bone
constexpr Word kCachedAnim = 0xf2;                  // byte: the animation decoded into the pose data
constexpr Word kCachedFrame = 0xf3;                 // byte: the frame whose matrices are in the pose data
constexpr Word kFrame = 0xf4;                       // short
constexpr Word kAnim = 0xf6;                        // short

}  // namespace obj

constexpr Word kGteRotation = 0x006a3e10;           // RT: nine shorts
constexpr Word kGteVector = 0x006a3e58;             // V0: three shorts
constexpr Word kGteIr = 0x006a3eb0;                 // IR1..3

}  // namespace

// 0x004305f0 Animation_DecompressStream(stream, destination, stride, count) (cdecl): one channel of `count` frames, `stride` shorts apart, from the stream; answers the next stream position
Word RenderKit::anim_decompress_stream(Word stream, Word dest, std::int32_t stride, std::int32_t count) {
    Word in = stream + 1;
    const Word head = byte_at(m_, stream);
    const Word divisions = (head >> 4) + 1;                // frames are stored every `divisions` frames and interpolated between
    const Word kind = head & 0xf;
    std::int32_t whole;                                    // the number of full intervals
    std::int32_t rest;                                     // the frames in the last, shorter interval
    if (divisions < 2) {
        whole = count - 1;
        rest = 0;
    } else {
        whole = (count - 1) / static_cast<std::int32_t>(divisions);
        rest = count - whole * static_cast<std::int32_t>(divisions) - 1;
    }
    const Word step = static_cast<Word>(stride) * 2;      // (in bytes)
    auto literal = [&](Word at) -> std::uint32_t { return byte_at(m_, at) + byte_at(m_, at + 1) * 0x100u; };
    if (kind == 0) {
        std::uint32_t current = literal(in);
        in += 2;
        Word out = dest;
        set_half_at(m_, out, current);
        out += step;
        std::int32_t previous = current;
        for (std::int32_t k = whole; k > 0; --k) {
            const std::uint32_t next = literal(in);
            in += 2;
            const std::int32_t delta = (static_cast<std::int16_t>(next) - static_cast<std::int16_t>(previous)) / static_cast<std::int32_t>(divisions);
            std::int32_t value = previous;
            for (Word j = divisions - 1; j != 0; --j) {
                value += delta;
                set_half_at(m_, out, value);
                out += step;
            }
            set_half_at(m_, out, next);
            out += step;
            previous = static_cast<std::int32_t>(next);
        }
        if (rest != 0) {
            const std::uint32_t last = literal(in);
            in += 2;
            const std::int32_t delta = (static_cast<std::int16_t>(last) - static_cast<std::int16_t>(previous)) / rest;
            std::int32_t value = previous;
            for (std::int32_t j = rest - 1; j > 0; --j) {
                value += delta;
                set_half_at(m_, out, value);
                out += step;
            }
            set_half_at(m_, out, last);
            return in;
        }
        return in;
    }
    if (kind <= 0xd) {
        const std::int32_t width = static_cast<std::int32_t>(kind) + 1;
        std::uint32_t current = literal(in);
        set_half_at(m_, dest, current);
        in += 2;
        Word out = dest + step;
        std::uint32_t bit = 0;
        std::int32_t base = static_cast<std::int32_t>(current);
        auto field = [&]() -> std::int32_t {
            const std::uint32_t bits = (byte_at(m_, in) << 16) | (byte_at(m_, in + 1) << 8) | byte_at(m_, in + 2);
            const std::int32_t value = static_cast<std::int32_t>(bits << (bit + 8)) >> ((32 - width) & 31);
            return value;
        };
        auto advance = [&]() {
            const std::uint32_t total = bit + static_cast<std::uint32_t>(width);
            bit = total & 7;
            in += static_cast<std::int32_t>(total) >> 3;
        };
        if (whole > 0) {
            for (std::int32_t k = whole; k != 0; --k) {
                const std::int32_t delta = field();
                const std::int32_t reached = static_cast<std::int16_t>(delta + base);
                advance();
                if (divisions != 0) {
                    for (Word j = divisions - 1; j != 0; --j) {
                        base += static_cast<std::int16_t>(delta / static_cast<std::int32_t>(divisions));
                        set_half_at(m_, out, base);
                        out += step;
                    }
                }
                set_half_at(m_, out, reached);
                out += step;
                base = reached;
            }
        }
        if (rest != 0) {
            const std::int32_t delta = field();
            advance();
            const std::int32_t last = static_cast<std::int16_t>(delta + base);
            for (std::int32_t j = rest - 1; j > 0; --j) {
                base += static_cast<std::int16_t>(delta / rest);
                set_half_at(m_, out, base);
                out += step;
            }
            set_half_at(m_, out, last);
        }
        return bit != 0 ? in + 1 : in;
    }
    if (kind == 0xe) {
        const std::uint32_t value = literal(in);
        in += 2;
        Word out = dest;
        for (std::int32_t k = count; k > 0; --k) {
            set_half_at(m_, out, value);
            out += step;
        }
        return in;
    }
    Word out = dest;
    for (std::int32_t k = count; k > 0; --k) {
        set_half_at(m_, out, 0);
        out += step;
    }
    return in;
}

// 0x00430920 Animation_GetDecompressedPose(model): the pose buffer of the model's current animation and frame; answers the pointer
Word RenderKit::anim_pose(Word model) {
    check(model != 0, 0x0052802c);
    const Word region = byte_at(m_, model + obj::kRegion);
    const Word record = kRegionRecords + region * kRegionStride;
    const Word anims = word_at(m_, record + 0x1c);
    const Word models = word_at(m_, record + 0x14);
    Word count = word_at(m_, models - 4);
    const Word raw = word_at(m_, record + 0x18);
    const Word frame_length = word_at(m_, raw + 8);
    check(anims != 0, 0x00528010);
    const Word anim = half_at(m_, model + obj::kAnim);
    check(anim < word_at(m_, anims), 0x00527ff4);
    check(word_at(m_, anims + anim * 8 + 8) != 0, 0x00527fc8);
    const std::int32_t frame = static_cast<std::int16_t>(half_at(m_, model + obj::kFrame));
    check(static_cast<Word>(frame) < word_at(m_, anims + anim * 8 + 8), 0x00527fac);
    const Word kind = word_at(m_, anims - 8);
    if (kind == 0x2a) {
        if ((word_at(m_, anims + anim * 8 + 8) & 0xffff0000u) != 0) {
            interpolate_animation_pose(model);
            return kPoseBuffer;
        }
        return anims + word_at(m_, anims + anim * 8 + 4) + static_cast<Word>(frame) * frame_length * 0x18;
    }
    if (kind != 0x2c) {
        check(false, 0x00527fa0);
        return 0;
    }
    check(count == frame_length, 0x00527f74);
    bool fresh = false;
    if (word_at(m_, model + obj::kPoseData) == 0) {
        Word longest = 0;
        Word at = anims;
        for (std::int32_t k = static_cast<std::int32_t>(word_at(m_, anims)); k > 0; --k) {
            if (longest < word_at(m_, at + 8)) {
                longest = word_at(m_, at + 8);
            }
            at += 8;
        }
        set_word_at(m_, model + obj::kPoseData, alloc((longest + 2) * count * 12));
        check(word_at(m_, record + 0x20) != 0, 0x00527f4c);
        check(word_at(m_, model + obj::kCalcOrder) == 0, 0x00527f24);
        set_word_at(m_, model + obj::kCalcOrder, alloc(count * 2, 0));
        set_half_at(m_, model + obj::kRoot, 0xffff);
        const Word hierarchy = word_at(m_, record + 0x20);
        for (Word k = 0; k < count; ++k) {
            if (half_at(m_, hierarchy + k * 2) == k) {
                check(half_at(m_, model + obj::kRoot) == 0xffff, 0x00527f04);
                set_half_at(m_, model + obj::kRoot, k);
            }
        }
        check(half_at(m_, model + obj::kRoot) != 0xffff, 0x00527ee8);
        const Word order = word_at(m_, model + obj::kCalcOrder);
        set_half_at(m_, order, half_at(m_, model + obj::kRoot));
        Word parent = order;
        Word next = order + 2;
        std::int32_t remaining = static_cast<std::int32_t>(count) - 1;
        while (remaining != 0) {
            Word table = hierarchy;
            for (Word j = 0; j < count; ++j) {
                if (j != half_at(m_, model + obj::kRoot) && half_at(m_, table) == half_at(m_, parent)) {
                    set_half_at(m_, next, j);
                    next += 2;
                    --remaining;
                    check(remaining >= 0, 0x00527ecc);
                }
                table += 2;
            }
            parent += 2;
        }
        fresh = true;
    }
    bool decoded = false;
    if (byte_at(m_, model + obj::kCachedAnim) != anim || fresh) {
        Word stream = anims + word_at(m_, anims + anim * 8 + 4);
        const Word frames = word_at(m_, anims + anim * 8 + 8);
        const Word stride = count * 6;
        const Word data = word_at(m_, model + obj::kPoseData);
        Word channels = data + count * 0x18;
        for (Word k = count; k != 0; --k) {
            for (Word offset = 0; offset < 12; offset += 2) {
                stream = anim_decompress_stream(stream, channels + offset, static_cast<std::int32_t>(stride), static_cast<std::int32_t>(frames));
            }
            channels += 0xc;
        }
        decoded = true;
        set_byte_at(m_, model + obj::kCachedAnim, anim);
    }
    if (static_cast<std::int16_t>(static_cast<std::int8_t>(byte_at(m_, model + obj::kCachedFrame))) != static_cast<std::int16_t>(half_at(m_, model + obj::kFrame)) || decoded) {
        const Word data = word_at(m_, model + obj::kPoseData);
        const Word frame_base = data + (static_cast<Word>(static_cast<std::int16_t>(half_at(m_, model + obj::kFrame))) + 2) * count * 12;
        const Word root = half_at(m_, model + obj::kRoot);
        const Word root_record = frame_base + root * 12;
        const Word angles = kScratch + 0x40;
        const Word hierarchy = word_at(m_, record + 0x20);
        const Word root_matrix = data + root * 0x18;
        set_half_at(m_, angles, half_at(m_, root_record));
        set_half_at(m_, angles + 2, half_at(m_, root_record + 2));
        set_half_at(m_, angles + 4, half_at(m_, root_record + 4));
        math().rotation_from_angles(angles, root_matrix);
        set_half_at(m_, root_matrix + 0x12, half_at(m_, root_record + 6));
        set_half_at(m_, root_matrix + 0x14, half_at(m_, root_record + 8));
        set_half_at(m_, root_matrix + 0x16, half_at(m_, root_record + 10));
        Word order = word_at(m_, model + obj::kCalcOrder) + 2;
        for (Word k = count - 1; count > 1 && k != 0; --k) {
            const Word bone = half_at(m_, order);
            order += 2;
            const Word parent_matrix = data + half_at(m_, hierarchy + bone * 2) * 0x18;
            const Word matrix = data + bone * 0x18;
            const Word bone_record = frame_base + bone * 12;
            set_half_at(m_, angles, half_at(m_, bone_record));
            set_half_at(m_, angles + 2, half_at(m_, bone_record + 2));
            set_half_at(m_, angles + 4, half_at(m_, bone_record + 4));
            math().rotation_from_angles(angles, matrix);
            check(true, 0x00527eac);
            set_global_half(m_, kGteVector, half_at(m_, bone_record + 6));
            set_global_half(m_, kGteVector + 2, half_at(m_, bone_record + 8));
            set_global_half(m_, kGteVector + 4, half_at(m_, bone_record + 10));
            for (Word i = 0; i < 18; i += 2) {
                set_half_at(m_, kGteRotation + i, half_at(m_, parent_matrix + i));
            }
            math().mvmva(1, 0);
            set_half_at(m_, matrix + 0x12, global_half(m_, kGteIr));
            set_half_at(m_, matrix + 0x14, global_half(m_, kGteIr + 2));
            set_half_at(m_, matrix + 0x16, global_half(m_, kGteIr + 4));
            set_half_at(m_, matrix + 0x12, half_at(m_, matrix + 0x12) + half_at(m_, parent_matrix + 0x12));
            set_half_at(m_, matrix + 0x14, half_at(m_, matrix + 0x14) + half_at(m_, parent_matrix + 0x14));
            set_half_at(m_, matrix + 0x16, half_at(m_, matrix + 0x16) + half_at(m_, parent_matrix + 0x16));
        }
        set_byte_at(m_, model + obj::kCachedFrame, byte_at(m_, model + obj::kFrame));
    }
    return word_at(m_, model + obj::kPoseData);
}

// 0x00431190 (fastcall): the model list: its vtable, twelve cleared pointers and the count
Word RenderKit::model_list_construct(Word self) {
    render_anim_model_at(m_, self).set_field_0(0x0051884c);
    for (Word offset = 4; offset < 0x34; offset += 4) {
        set_word_at(m_, self + offset, 0);
    }
    render_anim_model_at(m_, self).set_field_34(0);
    render_anim_model_at(m_, self).set_field_3c(0);
    return self;
}

// 0x004311b0 (thiscall, one argument): the deleting destructor
Word RenderKit::model_list_scalar_delete(Word self, Word flags) {
    model_list_destruct(self);
    if ((flags & 1) != 0) {
        engine(0x00466bb0, {self});
    }
    return self;
}

// 0x004311d0 (fastcall): deletes every object of the list through its vtable
void RenderKit::model_list_destruct(Word self) {
    render_anim_model_at(m_, self).set_field_0(0x0051884c);
    if (static_cast<std::int32_t>(render_anim_model_at(m_, self).field_34()) > 0) {
        Word slot = self;
        Word index = 0;
        do {
            slot += 4;
            if (word_at(m_, slot) != 0) {
                virtual_call(word_at(m_, slot), 0, {1});
            }
            set_word_at(m_, slot, 0);
            ++index;
        } while (static_cast<std::int32_t>(index) < static_cast<std::int32_t>(render_anim_model_at(m_, self).field_34()));
    }
    render_anim_model_at(m_, self).set_field_34(0);
    render_anim_model_at(m_, self).set_field_0(0x005184b4);
}

// 0x004305e0 (cdecl): an empty function
void RenderKit::animation_empty_hook() {}

}  // namespace opentony::runtime::renderkit
