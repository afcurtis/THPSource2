#include "auto_views.hpp"
#include "render_kit.hpp"

// Phase 4.15e: the pose side of the M3D renderer (`m3d.cpp` 0x00464b90..0x00465300): the hook and link packets, the position of a part or a hook of a model in the world, and the interpolation
// between the key frames of an animation. A model's pose is an array of 0x18-byte entries (a 3x3 matrix of shorts and a translation of three shorts) per bone; an animation with key frames
// stores every `segments` frames and the pose buffer at 0x00567cb0 holds the blend of two of them.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kRegionRecords = 0x0056d428;
constexpr Word kRegionStride = 0x44;
constexpr Word kPoseBuffer = 0x00567cb0;
constexpr Word kGteMac = 0x006a3eb8;
constexpr Word kGteIr = 0x006a3eb0;
constexpr Word kGteIr0 = 0x006a3e34;

}  // namespace

// 0x00464b90 M3D_ReadHooksPacket(model, packet) (cdecl): the hook table of the model's region starts after the packet's 4-byte head
void RenderKit::read_hooks_packet(Word model, Word packet) { set_word_at(m_, kRegionRecords + byte_at(m_, model + 0x1f) * kRegionStride + 0x24, packet + 4); }

// 0x00464bb0 M3D_ClearPoseStuff(model) (cdecl): frees the model's link pose and link table
void RenderKit::clear_pose_stuff(Word model) {
    const Word poses = word_at(m_, model + 0x138);
    set_word_at(m_, model + 0x140, 0);
    if (poses != 0) {
        mem_free(poses);
        set_word_at(m_, model + 0x138, 0);
    }
    const Word links = word_at(m_, model + 0x13c);
    if (links != 0) {
        mem_free(links);
        set_word_at(m_, model + 0x13c, 0);
    }
}

// 0x00464c00 M3D_ReadLinksPacket(model, packet) (cdecl): the link records (twelve bytes: a bone, its parent's bone, ...) are read in place; each one finds the record of its parent
void RenderKit::read_links_packet(Word model, Word packet) {
    const Word count = half_at(m_, packet + 2);
    const Word table = packet + 4;
    set_word_at(m_, model + 0x140, table);
    const Word region = kRegionRecords + byte_at(m_, model + 0x1f) * kRegionStride;
    const Word bones = half_at(m_, region + 0x38);
    set_word_at(m_, model + 0x138, alloc(bones * 24, 0));
    const Word zeroed = alloc(count * 12, 0);
    set_word_at(m_, model + 0x13c, zeroed);
    for (Word k = 0; static_cast<std::int32_t>(k) < static_cast<std::int32_t>(count); ++k) {
        for (Word offset = 0; offset < 12; offset += 2) {
            set_half_at(m_, word_at(m_, model + 0x13c) + k * 12 + offset, 0);
        }
    }
    for (Word k = 0; static_cast<std::int32_t>(k) < static_cast<std::int32_t>(count); ++k) {
        const Word wanted = half_at(m_, table + k * 12 + 2);
        Word found = 0;
        for (; static_cast<std::int32_t>(found) < static_cast<std::int32_t>(count); ++found) {
            if (half_at(m_, table + found * 12) == wanted) {
                set_half_at(m_, table + k * 12 + 0xa, found);
                break;
            }
        }
        if (found == count) {
            set_half_at(m_, table + k * 12 + 0xa, 0xffff);
        }
    }
}

// the pose entry of a bone of the model's current frame: the link pose, a blend of two key frames, or the decompressed frame
Word RenderKit::bone_pose(Word model, Word anims, Word anim, Word bone, Word bones) {
    if ((byte_at(m_, model + 4) & 4) != 0) {
        return word_at(m_, model + 0x138) + bone * 24;
    }
    const Word frames = word_at(m_, anims + anim * 8 + 8);
    if ((frames & 0xffff0000u) != 0) {
        tween_animation_buffer(4, (frames >> 16) + 1, anims, model, bone, bones);
        return kPoseBuffer + bone * 24;
    }
    return anim_pose(model) + bone * 24;
}

// the world position of a hook in the pose entry: the transformed vector in Q12 units pulled a sixteenth of the way from the model's position (the model's own position was already added)
void RenderKit::hook_to_world(Word model, Word out) {
    set_word_at(m_, out, global_word(m_, kGteMac));
    set_word_at(m_, out + 4, global_word(m_, kGteMac + 4));
    set_word_at(m_, out + 8, global_word(m_, kGteMac + 8));
    Word x = word_at(m_, out) << 12;
    Word y = word_at(m_, out + 4) << 12;
    Word z = word_at(m_, out + 8) << 12;
    const std::int32_t px = static_cast<std::int32_t>(word_at(m_, model + 8));
    const std::int32_t py = static_cast<std::int32_t>(word_at(m_, model + 0xc));
    const std::int32_t pz = static_cast<std::int32_t>(word_at(m_, model + 0x10));
    set_word_at(m_, out, u((s(x) - px) >> 4) + u(px));
    set_word_at(m_, out + 4, u((s(y) - py) >> 4) + u(py));
    set_word_at(m_, out + 8, u((s(z) - pz) >> 4) + u(pz));
}

// 0x00464d10 M3D_GetPartPosition(out, model, part) (cdecl; the kit's arguments are in the order model, part, out): the world position of a part's hook; a mirrored model has its lighting matrix's first column negated around the call
void RenderKit::part_position(Word model, Word part, Word out) {
    const Word region = kRegionRecords + byte_at(m_, model + 0x1f) * kRegionStride;
    const std::uint8_t flags = byte_at(m_, model + 0xd8);
    const Word hooks = word_at(m_, region + 0x24);
    const Word bone = half_at(m_, hooks + part * 8 + 6);
    const Word hook = hooks + part * 8;
    const Word anims = word_at(m_, region + 0x1c);
    const Word anim = half_at(m_, model + 0xf6);
    if ((flags & 2) != 0) {
        for (Word offset : {0x118u, 0x11eu, 0x124u}) {
            set_half_at(m_, model + offset, 0u - half_at(m_, model + offset));
        }
    }
    const Word pose = bone_pose(model, anims, anim, bone, half_at(m_, region + 0x38));
    m3d_.apply_hook_transform(hook, pose, model + 0x118);
    hook_to_world(model, out);
    if ((byte_at(m_, model + 0xd8) & 2) != 0) {
        const Word a = half_at(m_, model + 0x118);
        const Word b = half_at(m_, model + 0x11e);
        const Word c = half_at(m_, model + 0x124);
        set_half_at(m_, model + 0x118, 0u - a);
        set_half_at(m_, model + 0x11e, 0u - b);
        set_half_at(m_, model + 0x124, 0u - c);
    }
}

// 0x00464e90 M3D_GetHookPosition(out, model, hook) (cdecl; the kit's arguments are in the order model, hook, out): the same for a hook record the caller holds
void RenderKit::hook_position(Word model, Word hook, Word out) {
    const Word bone = half_at(m_, hook + 6);
    const Word region = kRegionRecords + byte_at(m_, model + 0x1f) * kRegionStride;
    const Word anims = word_at(m_, region + 0x1c);
    const Word anim = half_at(m_, model + 0xf6);
    check(word_at(m_, region + 0x14) != 0, 0x00533d80);
    check(bone < half_at(m_, region + 0x38), 0x00533d48);
    const Word pose = bone_pose(model, anims, anim, bone, half_at(m_, region + 0x38));
    m3d_.apply_hook_transform(hook, pose, model + 0x118);
    hook_to_world(model, out);
}

// 0x00465060 M3D_InterpolateAnimationPose(model) (cdecl): blends the key frames around the model's frame into the pose buffer, for every bone
void RenderKit::interpolate_animation_pose(Word model) {
    const Word region = kRegionRecords + byte_at(m_, model + 0x1f) * kRegionStride;
    const Word anim = half_at(m_, model + 0xf6);
    const Word anims = word_at(m_, region + 0x1c);
    const Word segments = (word_at(m_, anims + anim * 8 + 8) >> 16) + 1;
    if (segments == 1) {
        return;
    }
    const Word bones = half_at(m_, region + 0x38);
    check(s(bones) <= 0x1e, 0x00533dd0);
    tween_animation_buffer(bones * 4, segments, anims, model, 0, bones);
}

// 0x00465110 M3D_TweenAnimationBuffer(vectors, segments, animations, model, first bone, bones) (cdecl): the pose-buffer entries from `first bone` on (`vectors` six-byte vectors) are the blend of
// the key frames before and after the model's frame, by the fraction of the way (Q12) between them; at the end of the animation the next key frame is frame 0 when the model loops
void RenderKit::tween_animation_buffer(Word vectors, Word segments, Word anims, Word model, Word first_bone, Word bones) {
    const std::int32_t frame = static_cast<std::int16_t>(half_at(m_, model + 0xf4));
    const Word anim = half_at(m_, model + 0xf6);
    const std::int32_t frames = static_cast<std::int32_t>(word_at(m_, anims + anim * 8 + 8) & 0xffff);
    const Word data = word_at(m_, anims + anim * 8 + 4) + anims;
    const std::int32_t span = static_cast<std::int32_t>(segments);
    std::int32_t start = frame - frame % span;
    std::int32_t next = start + span;
    std::int32_t fraction;
    if (next >= frames) {
        if (byte_at(m_, model + 0xf8) == 1) {
            next = 0;
            fraction = ((frame - start) << 12) / (frames - start);
        } else {
            start -= span;
            if (start < 0) {
                start = 0;
            }
            next -= span;
            if (next == 0) {
                fraction = 0;
            } else {
                fraction = ((frame - start) << 12) / (next - start);
            }
        }
    } else {
        fraction = ((frame - start) << 12) / (next - start);
    }
    Word dest = kPoseBuffer + first_bone * 24;
    const std::int32_t key_a = start * static_cast<std::int32_t>(bones) / span;
    const Word from = data + u(key_a + static_cast<std::int32_t>(first_bone)) * 24;
    if (fraction == 0) {
        Word source = from;
        for (Word k = vectors; static_cast<std::int32_t>(k) > 0; --k) {
            set_word_at(m_, dest, word_at(m_, source));
            set_half_at(m_, dest + 4, half_at(m_, source + 4));
            dest += 6;
            source += 6;
        }
        return;
    }
    set_global_half(m_, kGteIr0, static_cast<Word>(fraction));
    const std::int32_t key_b = next * static_cast<std::int32_t>(bones) / span;
    const Word to = from + u(key_b - key_a) * 24;
    Word a = from;
    Word b = to;
    for (Word k = vectors; static_cast<std::int32_t>(k) > 0; --k) {
        const std::int32_t ax = static_cast<std::int16_t>(half_at(m_, a));
        const std::int32_t ay = static_cast<std::int16_t>(half_at(m_, a + 2));
        const std::int32_t az = static_cast<std::int16_t>(half_at(m_, a + 4));
        set_global_word(m_, kGteMac, u(ax));
        set_global_half(m_, kGteIr, half_at(m_, b) - half_at(m_, a));
        set_global_word(m_, kGteMac + 4, u(ay));
        set_global_half(m_, kGteIr + 2, half_at(m_, b + 2) - half_at(m_, a + 2));
        set_global_word(m_, kGteMac + 8, u(az));
        set_global_half(m_, kGteIr + 4, half_at(m_, b + 4) - half_at(m_, a + 4));
        math().interpolate_gpl12();
        set_half_at(m_, dest, global_word(m_, kGteMac));
        set_half_at(m_, dest + 2, global_word(m_, kGteMac + 4));
        set_half_at(m_, dest + 4, global_word(m_, kGteMac + 8));
        a += 6;
        b += 6;
        dest += 6;
    }
}

// 0x00465300 M3D_BuildPose(model) (cdecl): poses a model whose bones follow links (a tree of local rotations and offsets: twelve-byte records, the parent's record number at +0xa of the link). The pose of
// each bone is the decoded frame's entry for it composed with the stack of its ancestors' transforms; a matrix stack of five entries (0x1c bytes: a transform and the link's number) is kept while the
// links are walked, popped back to the parent of each link. Bones the links do not reach keep the decoded entry (marked 0x8000 first).
void RenderKit::build_pose(Word model) {
    const Word flags = byte_at(m_, model + 4);
    const Word pose = word_at(m_, model + 0x138);
    const Word links = word_at(m_, model + 0x140);
    const Word local = word_at(m_, model + 0x13c);
    if ((flags & 4) == 0 || pose == 0 || local == 0) {
        return;
    }
    const Word region = kRegionRecords + byte_at(m_, model + 0x1f) * kRegionStride;
    const Word bones = half_at(m_, region + 0x38);
    const Word anims = word_at(m_, region + 0x1c);
    for (Word k = 0; k < bones; ++k) {
        set_half_at(m_, pose + k * 24, 0x8000);
    }
    const std::int32_t link_count = half_at(m_, links - 2);
    const Word decoded = anim_pose(model);
    const Word stack = kScratch + 0x800;           // the matrix stack (entries of 0x1c bytes)
    const Word matrix = kScratch + 0x7c0;          // the link's local transform (24 bytes)
    const Word outer = kScratch + 0x7e0;           // the decoded entry with the link's offset added
    std::int32_t top = -1;
    for (std::int32_t k = 0; k < link_count; ++k) {
        const Word record = local + 12 * static_cast<Word>(k);
        const Word bone = half_at(m_, links + 12 * static_cast<Word>(k));
        const Word parent = half_at(m_, links + 12 * static_cast<Word>(k) + 0xa);
        while (top >= 0 && parent < word_at(m_, stack + 0x1c * static_cast<Word>(top) + 0x18)) {
            --top;
        }
        bool identity = true;
        for (Word offset = 0; offset < 12; offset += 2) {
            identity = identity && half_at(m_, record + offset) == 0;
        }
        if (identity) {
            if (top >= 0) {
                m3d_.compose_transforms(pose + bone * 24, stack + 0x1c * static_cast<Word>(top), decoded + bone * 24);
            } else {
                for (Word offset = 0; offset < 24; offset += 2) {
                    set_half_at(m_, pose + bone * 24 + offset, half_at(m_, decoded + bone * 24 + offset));
                }
            }
            continue;
        }
        m3d_.rotation_from_angles(record, matrix);
        set_half_at(m_, matrix + 0x12, 0);
        set_half_at(m_, matrix + 0x14, 0);
        set_half_at(m_, matrix + 0x16, 0);
        for (Word offset = 0; offset < 24; offset += 4) {
            set_word_at(m_, outer + offset, word_at(m_, decoded + bone * 24 + offset));
        }
        set_half_at(m_, outer + 0x12, half_at(m_, outer + 0x12) + half_at(m_, record + 6));
        set_half_at(m_, outer + 0x14, half_at(m_, outer + 0x14) + half_at(m_, record + 8));
        set_half_at(m_, outer + 0x16, half_at(m_, outer + 0x16) + half_at(m_, record + 10));
        m3d_.compose_transforms(matrix, outer, matrix);
        if (top >= 0) {
            m3d_.compose_transforms(matrix, stack + 0x1c * static_cast<Word>(top), matrix);
        }
        ++top;
        const Word entry = stack + 0x1c * static_cast<Word>(top);
        check(top < 5, 0x00533df0);
        m3d_.compose_with_transpose(entry, matrix, decoded + bone * 24);
        set_word_at(m_, entry + 0x18, static_cast<Word>(k));
        for (Word offset = 0; offset < 18; offset += 2) {
            set_half_at(m_, pose + bone * 24 + offset, half_at(m_, matrix + offset));
        }
        set_half_at(m_, pose + bone * 24 + 0x12, half_at(m_, matrix + 0x12));
        set_half_at(m_, pose + bone * 24 + 0x14, half_at(m_, matrix + 0x14));
        set_half_at(m_, pose + bone * 24 + 0x16, half_at(m_, matrix + 0x16));
    }
    const Word anim = half_at(m_, model + 0xf6);
    if ((word_at(m_, anims + anim * 8 + 8) & 0xffff0000u) == 0) {
        for (Word k = 0; k < bones; ++k) {
            const Word entry = pose + k * 24;
            if (half_at(m_, entry) == 0x8000) {
                for (Word offset = 0; offset < 24; offset += 4) {
                    set_word_at(m_, entry + offset, word_at(m_, decoded + k * 24 + offset));
                }
            }
        }
    }
}

// 0x00465830 (cdecl, eight pointers): the closest points of two segments (p1 -> p2 and p3 -> p4, Q12 vectors): the point on each (p5, p7) and where on the segment it is (p6, p8: 0..0x1000), and the distance
// between them (answered). Everything runs in whole units (the vectors are shifted down 12) through the GTE's cross product and square: the cross products of the directions are shifted down until
// they fit the 16-bit registers (0x6882 at most), the dot product against the offset gives the parameter along the segment.
Word RenderKit::segment_distance(Word p1, Word p2, Word p3, Word p4, Word p5, Word p6, Word p7, Word p8) {
    const Word frame = kScratch + 0x600;
    const Word count12 = frame;
    const Word shift_a = frame + 4;
    const Word one = frame + 8;
    const Word scratch = frame + 0x10;
    const Word e1 = frame + 0x20, e2 = frame + 0x30, r = frame + 0x40, q1 = frame + 0x50, q3 = frame + 0x60, n = frame + 0x70, m = frame + 0x80, squares = frame + 0x90;
    constexpr Word kRotation = 0x006a3e10, kIr = 0x006a3eb0, kMac = 0x006a3eb8, kIr0 = 0x006a3e34;
    auto set_ir = [&](Word vec) {
        for (Word k = 0; k < 3; ++k) {
            set_half_at(m_, kIr + 2 * k, half_at(m_, vec + 4 * k));
        }
    };
    auto set_diagonal = [&](Word vec) {
        set_global_half(m_, kRotation, half_at(m_, vec));
        set_global_half(m_, kRotation + 8, half_at(m_, vec + 4));
        set_global_half(m_, kRotation + 16, half_at(m_, vec + 8));
    };
    auto shift_in_place = [&](Word vec, Word count_address) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, vec + 4 * k, u(s(word_at(m_, vec + 4 * k)) >> (word_at(m_, count_address) & 31)));
        }
    };
    auto out_of_range = [&](Word vec) {
        for (Word k = 0; k < 3; ++k) {
            const std::int32_t value = s(word_at(m_, vec + 4 * k));
            if (value > 0x6882 || value < -0x6882) {
                return true;
            }
        }
        return false;
    };
    auto read_mac = [&](Word vec) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, vec + 4 * k, word_at(m_, kMac + 4 * k));
        }
    };
    auto copy_vector = [&](Word to, Word from) {
        for (Word k = 0; k < 3; ++k) {
            set_word_at(m_, to + 4 * k, word_at(m_, from + 4 * k));
        }
    };
    set_word_at(m_, count12, 12);
    core_.vector_subtract_out(scratch, p2, p1);
    core_.vector_shift_right_out(e1, scratch, count12);
    core_.vector_subtract_out(scratch, p4, p3);
    core_.vector_shift_right_out(e2, scratch, count12);
    core_.vector_subtract_out(scratch, p1, p3);
    core_.vector_shift_right_out(r, scratch, count12);
    core_.vector_shift_right_out(q1, p1, count12);
    core_.vector_shift_right_out(q3, p3, count12);
    // the normal of the two directions, scaled to fit
    set_diagonal(e1);
    set_ir(e2);
    math().cross_sf0();
    read_mac(n);
    set_word_at(m_, shift_a, 0);
    while (out_of_range(n)) {
        set_word_at(m_, one, 1);
        shift_in_place(n, one);
        set_word_at(m_, shift_a, word_at(m_, shift_a) + 1);
    }
    set_ir(n);
    math().sqr0();
    const std::int32_t square_x = s(global_word(m_, kMac));
    const std::int32_t square_y = s(global_word(m_, kMac + 4));
    const std::int32_t square_z = s(global_word(m_, kMac + 8));
    // one half of the answer, for the segment from `start` (the point `q`, the direction `direction`) towards `end`: `other` is the offset from the segment to the other line's start
    auto half = [&](Word direction, Word other_direction, Word other_offset, Word start, Word end, Word q, Word point_out, Word parameter_out) {
        set_diagonal(other_direction);
        set_ir(other_offset);
        math().cross_sf0();
        read_mac(m);
        shift_in_place(m, shift_a);
        Word shift_b = 0;
        while (out_of_range(m)) {
            set_word_at(m_, one, 1);
            shift_in_place(m, one);
            ++shift_b;
        }
        set_ir(m);
        set_global_half(m_, kRotation, u(s(word_at(m_, n)) >> shift_b));
        set_global_half(m_, kRotation + 2, u(s(word_at(m_, n + 4)) >> shift_b));
        set_global_half(m_, kRotation + 4, u(s(word_at(m_, n + 8)) >> shift_b));
        set_global_half(m_, kRotation + 6, 0);
        math().mvmva(0, 3);
        std::int32_t denominator = (square_x + square_y + square_z) >> (2 * shift_b);
        const std::int32_t dot = s(global_word(m_, kMac));
        if (dot < 0) {
            set_word_at(m_, parameter_out, 0);
            copy_vector(point_out, start);
        } else if (dot < denominator) {
            std::int32_t numerator = dot;
            if (denominator < 0x100000) {
                numerator = dot << 12;
            } else {
                denominator >>= 12;
            }
            const std::int32_t t = numerator / denominator;
            set_word_at(m_, parameter_out, u(t));
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kMac + 4 * k, word_at(m_, q + 4 * k));
            }
            set_ir(direction);
            set_global_half(m_, kIr0, u(t));
            math().interpolate_gpl12();
            read_mac(point_out);
            set_word_at(m_, one, 12);
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, point_out + 4 * k, word_at(m_, point_out + 4 * k) << (word_at(m_, one) & 31));
            }
        } else {
            set_word_at(m_, parameter_out, 0x1000);
            copy_vector(point_out, end);
        }
    };
    half(e1, e2, r, p1, p2, q1, p5, p6);
    // the second half: the same with the segments swapped (the normal's cross product is of the first direction and the offset)
    set_diagonal(e1);
    set_ir(r);
    {
        math().cross_sf0();
        read_mac(m);
        shift_in_place(m, shift_a);
        Word shift_b = 0;
        while (out_of_range(m)) {
            set_word_at(m_, one, 1);
            shift_in_place(m, one);
            ++shift_b;
        }
        set_ir(m);
        set_global_half(m_, kRotation, u(s(word_at(m_, n)) >> shift_b));
        set_global_half(m_, kRotation + 2, u(s(word_at(m_, n + 4)) >> shift_b));
        set_global_half(m_, kRotation + 4, u(s(word_at(m_, n + 8)) >> shift_b));
        set_global_half(m_, kRotation + 6, 0);
        math().mvmva(0, 3);
        std::int32_t denominator = (square_x + square_y + square_z) >> (2 * shift_b);
        const std::int32_t dot = s(global_word(m_, kMac));
        if (dot < 0) {
            set_word_at(m_, p8, 0);
            copy_vector(p7, p3);
        } else if (dot < denominator) {
            std::int32_t numerator = dot;
            if (denominator < 0x100000) {
                numerator = dot << 12;
            } else {
                denominator >>= 12;
            }
            const std::int32_t t = numerator / denominator;
            set_word_at(m_, p8, u(t));
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, kMac + 4 * k, word_at(m_, q3 + 4 * k));
            }
            set_ir(e2);
            set_global_half(m_, kIr0, u(t));
            math().interpolate_gpl12();
            read_mac(p7);
            set_word_at(m_, one, 12);
            for (Word k = 0; k < 3; ++k) {
                set_word_at(m_, p7 + 4 * k, word_at(m_, p7 + 4 * k) << (word_at(m_, one) & 31));
            }
        } else {
            set_word_at(m_, p8, 0x1000);
            copy_vector(p7, p4);
        }
    }
    set_word_at(m_, one, 12);
    core_.vector_subtract_out(scratch, p5, p7);
    core_.vector_shift_right_out(squares, scratch, one);
    set_ir(squares);
    math().sqr0();
    return sqrt_truncated(global_word(m_, kMac + 8) + global_word(m_, kMac + 4) + global_word(m_, kMac));
}

}  // namespace opentony::runtime::renderkit
