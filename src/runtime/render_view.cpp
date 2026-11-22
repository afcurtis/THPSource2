#include "auto_views.hpp"
#include "render_kit.hpp"

// Phase 4.15e: the start of a view (`m3d.cpp`, 0x0045e8e0 M3D_BeginRenderView, 0x0045ef80 M3d_RenderSetupCamera): the viewport edges of the view are turned into the projection constants (the half
// width and the centre in the edges record: +0xe, +0x10, +0x12), the four frustum planes (eight shorts each at 0x00563a90..0x00563adc: the normals and their offsets), the light and colour matrices
// the frame keeps (the view's matrix applied to the plane normals through the GTE) and the aspect-scaled copy of the view's transposed matrix. BeginRenderView also opens the view's ordering table
// with a rectangle, advances the animation clock and updates the colour pulses and the texture wibble; SetupCamera does only the projection part for the camera used by single-object renders.

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kClockPrevious = 0x00564374;
constexpr Word kClock = 0x00563af0;
constexpr Word kSimulationTime = 0x0056e320;
constexpr Word kTickCount = 0x00564370;
constexpr Word kView = 0x005620e0;
constexpr Word kEdges = 0x00563a38;
constexpr Word kBuckets = 0x0056433c;
constexpr Word kEdgeDepth = 0x00563a3c;
constexpr Word kPixelAspectX = 0x00563a6c;
constexpr Word kPixelAspectY = 0x00563a70;
constexpr Word kPlanes = 0x00563a90;
constexpr Word kLightMatrices = 0x005620e8;
constexpr Word kColourMatrices = 0x005620c0;
constexpr Word kGteRotation = 0x006a3e10;
constexpr Word kGteVector = 0x006a3e58;
constexpr Word kGteIr = 0x006a3eb0;

}  // namespace

// the edges record's centre and half-width fields, the frustum planes and the GTE loop that transforms them (shared by both functions after their own first lines)
void RenderKit::view_planes(Word edges) {
    set_global_half(m_, kPlanes, 0);
    set_global_half(m_, kPlanes + 2, 0);
    set_global_half(m_, kPlanes + 4, 0xf000);
    set_global_half(m_, kPlanes + 6, half_at(m_, edges + 0xa));
    set_global_half(m_, kPlanes + 8, 0);
    set_global_half(m_, kPlanes + 0xa, 0);
    set_global_half(m_, kPlanes + 0xc, 0x1000);
    set_global_half(m_, kPlanes + 0xe, 0u - half_at(m_, edges + 8));
    const Word left = half_at(m_, edges);
    const Word right = half_at(m_, edges + 4);
    const Word half = half_at(m_, edges + 0xe);
    const Word span = ((right * 0x1fffffu + left) << 11);
    // the planes come in two layouts: a, the negation of a and b in the odd places (A, C), or a first and its negation at +8 (B, D)
    auto first_layout = [this](Word base, Word a, Word b) {
        set_half_at(m_, base, 0);
        set_half_at(m_, base + 2, a);
        set_half_at(m_, base + 4, b);
        set_half_at(m_, base + 6, 0);
        set_half_at(m_, base + 8, 0);
        set_half_at(m_, base + 0xa, 0u - a);
        set_half_at(m_, base + 0xc, b);
        set_half_at(m_, base + 0xe, 0);
    };
    auto second_layout = [this](Word base, Word a, Word b) {
        set_half_at(m_, base, a);
        set_half_at(m_, base + 2, 0);
        set_half_at(m_, base + 4, b);
        set_half_at(m_, base + 6, 0);
        set_half_at(m_, base + 8, 0u - a);
        set_half_at(m_, base + 0xa, 0);
        set_half_at(m_, base + 0xc, b);
        set_half_at(m_, base + 0xe, 0);
    };
    // the left and right planes (the horizontal aspect, then the vertical aspect)
    {
        const Word scaled = half * global_word(m_, kPixelAspectX);
        const Word length = sqrt_truncated((span >> 12) * (span >> 12) + (scaled >> 12) * (scaled >> 12));
        first_layout(0x00563aa0, static_cast<std::uint16_t>(scaled / length), static_cast<std::uint16_t>(span / length));
    }
    {
        const Word scaled = half * global_word(m_, kPixelAspectY);
        const Word length = sqrt_truncated((span >> 12) * (span >> 12) + (scaled >> 12) * (scaled >> 12));
        second_layout(0x00563ab0, static_cast<std::uint16_t>(scaled / length), static_cast<std::uint16_t>(span / length));
    }
    // the top and bottom planes
    const std::int32_t top = (static_cast<std::int32_t>(half_at(m_, edges + 2)) - static_cast<std::int32_t>(half_at(m_, edges + 6))) >> 1;
    const std::int32_t side = (static_cast<std::int32_t>(left) - static_cast<std::int32_t>(right)) >> 1;
    {
        const Word length = sqrt_truncated(half * half + u(top) * u(top));
        const Word a = static_cast<std::uint16_t>((half << 12) / length);
        const Word b = static_cast<std::uint16_t>((u(top) << 12) / length);
        first_layout(0x00563ac0, a, b);
    }
    {
        const Word length = sqrt_truncated(half * half + u(side) * u(side));
        const Word a = static_cast<std::uint16_t>((half << 12) / length);
        const Word b = static_cast<std::uint16_t>((u(side) << 12) / length);
        second_layout(0x00563ad0, a, b);
    }
}

// the light and colour matrices of the frame from the view's matrix and the plane normals, the GTE's projection constants, the transposed matrix and its aspect-scaled copy
void RenderKit::view_matrices(Word view, Word edges) {
    Word plane = 0x00563a92;
    for (Word at = 0; at < 18; at += 6, plane += 8) {
        for (Word k = 0; k < 18; k += 2) {
            set_half_at(m_, kGteRotation + k, half_at(m_, view + 0x34 + k));
        }
        set_global_half(m_, kGteVector, half_at(m_, plane - 2));
        set_global_half(m_, kGteVector + 2, half_at(m_, plane));
        set_global_half(m_, kGteVector + 4, half_at(m_, plane + 2));
        math().rtv0();
        set_half_at(m_, kLightMatrices + at, global_half(m_, kGteIr));
        set_half_at(m_, kLightMatrices + at + 2, global_half(m_, kGteIr + 2));
        set_half_at(m_, kLightMatrices + at + 4, global_half(m_, kGteIr + 4));
        for (Word k = 0; k < 18; k += 2) {
            set_half_at(m_, kGteRotation + k, half_at(m_, view + 0x34 + k));
        }
        set_global_half(m_, kGteVector, half_at(m_, plane + 0x16));
        set_global_half(m_, kGteVector + 2, half_at(m_, plane + 0x18));
        set_global_half(m_, kGteVector + 4, half_at(m_, plane + 0x1a));
        math().rtv0();
        set_half_at(m_, kColourMatrices + at, global_half(m_, kGteIr));
        set_half_at(m_, kColourMatrices + at + 2, global_half(m_, kGteIr + 2));
        set_half_at(m_, kColourMatrices + at + 4, global_half(m_, kGteIr + 4));
    }
    set_global_word(m_, 0x006a3e00, half_at(m_, edges + 0xe));
    set_global_word(m_, 0x006a3ee8, half_at(m_, edges + 0x10));
    set_global_word(m_, 0x006a3eec, half_at(m_, edges + 0x12));
    m3d_.transpose_matrix(view + 0x34, view + 0x54);
    scale_view_matrix(view);
}

// the transposed matrix (at +0x54) with its first row scaled by the vertical aspect and its second by the horizontal one, copied to +0x74
void RenderKit::scale_view_matrix(Word view) {
    for (Word k = 0; k < 3; ++k) {
        const Word at = view + 0x74 + 2 * k;
        set_half_at(m_, at, u((static_cast<std::int16_t>(half_at(m_, at - 0x20)) * s(global_word(m_, kPixelAspectY))) >> 12));
        set_half_at(m_, at + 6, u((static_cast<std::int16_t>(half_at(m_, at - 0x1a)) * s(global_word(m_, kPixelAspectX))) >> 12));
        set_half_at(m_, at + 0xc, half_at(m_, at - 0x14));
    }
}

// 0x0045e8e0 M3D_BeginRenderView(view, edges, buckets) (cdecl)
void RenderKit::begin_render_view(Word view, Word edges, Word buckets) {
    set_global_word(m_, kClockPrevious, global_word(m_, kClock));
    const Word now = global_word(m_, kSimulationTime);
    const bool changed = global_word(m_, kClock) != now;
    set_global_word(m_, kClock, now);
    if (changed) {
        set_global_word(m_, kTickCount, global_word(m_, kTickCount) + 1);
    }
    set_global_word(m_, kView, view);
    set_global_word(m_, kEdges, edges);
    set_half_at(m_, edges + 0xa, global_half(m_, kEdgeDepth));
    set_global_word(m_, kBuckets, buckets);
    Word x, y, width, height;
    if (global_word(m_, 0x029da3a0) == 0) {
        width = global_half(m_, 0x005620a0);
        height = global_half(m_, 0x005620b8);
        x = 0;
        y = 0;
    } else {
        x = half_at(m_, edges + 4);
        y = half_at(m_, edges + 6);
        width = static_cast<std::uint16_t>(half_at(m_, edges) - half_at(m_, edges + 4));
        height = static_cast<std::uint16_t>(half_at(m_, edges + 2) - half_at(m_, edges + 6));
    }
    const Word rect = kScratch;
    const Word scale_x = global_word(m_, 0x029da394);
    const Word scale_y = global_word(m_, 0x029da398);
    set_half_at(m_, rect, u(static_cast<std::int16_t>(x) * 0x200) / scale_x);
    set_half_at(m_, rect + 2, u(static_cast<std::int16_t>(y) * 240) / scale_y);
    set_half_at(m_, rect + 4, u(static_cast<std::int16_t>(width) * 0x200) / scale_x);
    set_half_at(m_, rect + 6, u(static_cast<std::int16_t>(height) * 240) / scale_y);
    const Word prim = global_word(m_, 0x00560fd0);
    engine(0x004e87f0, {prim, rect}, bytes_text(rect, 8));
    set_global_word(m_, 0x00560fd0, prim + 0x14);
    if (global_word(m_, kBuckets) == word_at(m_, global_word(m_, 0x00560fd4) + 0x84)) {
        set_word_at(m_, prim + 0x10, 0xbf800000);
    } else {
        set_word_at(m_, prim + 0x10, float_bits(static_cast<float>(half_at(m_, global_word(m_, kEdges) + 8))));
    }
    set_word_at(m_, prim, word_at(m_, global_word(m_, kBuckets)));
    set_word_at(m_, global_word(m_, kBuckets), prim);
    // the projection constants
    const std::int32_t aspect = s(global_word(m_, kPixelAspectY));
    const std::int32_t unit = (static_cast<std::int32_t>(half_at(m_, edges + 0xc)) << 12) / aspect;
    const Word left = half_at(m_, edges);
    const Word right = half_at(m_, edges + 4);
    set_half_at(m_, edges + 0x10, (left + right) >> 1);
    set_half_at(m_, edges + 0xe, u(s(((left - right) << 11) & 0xfffff000u) / unit));
    set_half_at(m_, edges + 0x12, (half_at(m_, edges + 6) + half_at(m_, edges + 2)) >> 1);
    view_planes(edges);
    view_matrices(view, edges);
    engine(0x00462170, {global_word(m_, 0x00537c80)});
    engine(0x00462170, {global_word(m_, 0x00537c84)});
    engine(0x00462170, {global_byte(m_, 0x0056dcb4)});
    engine(0x00461cf0, {global_byte(m_, 0x0056dcb4)});
    engine(0x00461cf0, {global_byte(m_, 0x0056dcac)});
    set_global_half(m_, 0x005620b0, 0u - half_at(m_, view + 0x3a));
    set_global_half(m_, 0x005620b2, 0u - half_at(m_, view + 0x3c));
    set_global_half(m_, 0x005620b4, 0u - half_at(m_, view + 0x3e));
    set_global_half(m_, 0x005620b6, u(s(global_word(m_, 0x00533f90)) >> 12) - half_at(m_, view + 8));
}

// 0x0045ef80 M3d_RenderSetupCamera(view) (cdecl): the projection part, for the edges the frame already holds
void RenderKit::setup_camera(Word view) {
    const Word edges = global_word(m_, kEdges);
    set_global_word(m_, kView, view);
    set_half_at(m_, edges + 0xa, global_half(m_, kEdgeDepth));
    const std::int32_t left = static_cast<std::int32_t>(half_at(m_, edges));
    const std::int32_t right = static_cast<std::int32_t>(half_at(m_, edges + 4));
    const std::int32_t half_width = static_cast<std::int32_t>(half_at(m_, edges + 0xc));
    std::int32_t value = s(((u(left) - u(right)) << 11) & 0xfffff000u) / half_width;
    set_half_at(m_, edges + 0x10, u((left + right) >> 1));
    value = s(u(value) << 12) / s(global_word(m_, kPixelAspectY));
    set_half_at(m_, edges + 0xe, u(value));
    set_half_at(m_, edges + 0x12, (half_at(m_, edges + 6) + half_at(m_, edges + 2)) >> 1);
    view_planes(edges);
    view_matrices(view, edges);
}

}  // namespace opentony::runtime::renderkit
