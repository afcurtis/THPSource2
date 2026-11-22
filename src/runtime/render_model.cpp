#include "auto_views.hpp"
#include "host_options.hpp"
#include "render_kit.hpp"
#include "render_presentation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Milestone 10: the model submission chain of the D3D layer (`d3dmodel.cpp`, 0x004d14d0..0x004d2d9e) on the retail memory model. M3D_SubmitModelState derives the view constants and the model's
// float matrix, M3D_TransformVertices projects the packet's vertices into the working records at 0x00570878 (and keeps some in the secondary heap at 0x0058f328), M3D_SubmitFacePolygons walks the
// faces, M3D_ResolveFaceColor and M3D_BuildD3DPolygon fill a 0xc0-byte slot of the polygon arena per face, and M3D_ClipAndBucketPolygon (with the near clip and the depth classification) links the
// slot into the ordering table. src/trg/render_model_submit.* is the same chain as value-owned C++, trace-validated against 279 retail calls; this is its arithmetic, operation for operation, on
// retail's words, so the globals, the slots, the heaps and the bucket links are where retail leaves them (render_model_kit_trace_test.cpp replays the same 279 calls through it).
//
// The chain sets the x87 precision control to 24 bits (_controlfp(0x20000, 0x30000) at 0x004d158a; 0x9001f on the way out), so every arithmetic result is rounded to binary32; the view constants
// computed before that are integer-by-float products and one quotient, which round the same at any precision. The comparisons follow the fcom / fnstsw / test sequences of the disassembly, which
// differ from C++'s for an unordered (NaN) operand, and the same test is not always made the same way (the left edge of an ordinary vertex and of a strip vertex).

namespace opentony::runtime::renderkit {

using namespace opentony::runtime::autoviews;

namespace {

using Word = std::uint32_t;

constexpr Word kModelFlags = 0x0057e884;          // renderModelFlags: the submission's flags
constexpr Word kViewportEdges = 0x00563a38;       // RenderViewportEdges (a pointer): right, bottom, left, top, near, yon (u16), the depth scale at +0xe, the centre at +0x10 / +0x12
constexpr Word kFogYonScale = 0x00549d98;
constexpr Word kNearOverYon = 0x00580898;
constexpr Word kNearDepth = 0x0058089c;           // RenderNearDepth
constexpr Word kCentreX = 0x005808a0;
constexpr Word kCentreY = 0x005808a4;
constexpr Word kDepthScale = 0x005808a8;
constexpr Word kYon = 0x0059d33c;
constexpr Word kFacesWalked = 0x0059d338;         // the faces the last submission walked
constexpr Word kSoftwareRenderer = 0x029da3a0;    // D3DSoftwareRenderer (compared signed)
constexpr Word kMatrixA = 0x00563a18;             // nine Q12 shorts and three dwords at 0x00563af8: the model's transform when state bit 0x01 is clear
constexpr Word kTranslationA = 0x00563af8;
constexpr Word kReverseWinding = 0x00563b08;      // u16
constexpr Word kMatrixB = 0x00563b10;             // nine Q12 shorts and three dwords at 0x00563b24 (state bit 0x01 set)
constexpr Word kTranslationB = 0x00563b24;
constexpr Word kMatrixC = 0x00564340;             // nine Q16 shorts, then three Q4 offsets (state bit 0x01 set)
constexpr Word kOffsetC = 0x00564352;
constexpr Word kCamera = 0x005620e0;              // a pointer: the camera matrix (Q12 shorts) at +0x74, a depth at +8
constexpr Word kBackBias = 0x00562098;
constexpr Word kZPushback = 0x005620a8;           // SP_ZPushback
constexpr Word kMatrix = 0x0056e84c;              // nine floats: model to view
constexpr Word kModel = 0x0056e870;               // the packet being drawn
constexpr Word kTranslation = 0x0058f318;         // three floats
constexpr Word kBackVector = 0x0057e878;          // three floats
constexpr Word kLights = 0x0057e888;              // a colour per lit vertex
constexpr Word kFaceColours = 0x00580888;         // four dwords: the corner colours of the face being built
constexpr Word kPushbacks = 0x005808ac;           // modelPushbacks: a short per level (2000 each) and model
constexpr Word kModelPushback = 0x0056e848;
constexpr Word kModelId = 0x00563a78;
constexpr Word kCurrentLevel = 0x0056a898;
constexpr Word kGrey = 0x0059d32c;                // the colour of every corner under state bit 0x08
constexpr Word kGreySource = 0x00563a5c;
constexpr Word kVertices = 0x0059d334;            // the packet's vertex, normal and face arrays
constexpr Word kNormals = 0x0059d328;
constexpr Word kFaces = 0x0058f310;
constexpr Word kVertexHeap = 0x00570878;          // modelVertHeap: x, y, depth, the projection scale, the source flags, the clip flags, otherheap (a record that stands in for this one)
constexpr Word kSecondaryHeap = 0x0058f328;       // records kept for later submissions (source flag 1) and named by them (source flag 2)
constexpr Word kSecondaryCursor = 0x0059d330;
constexpr Word kSelectorSource = 0x00564358;      // the high half masks a face's first word, the low half is or-ed in
constexpr Word kSelector = 0x0058f30c;            // RenderDepthModeSelector: the face being built
constexpr Word kSceneColour = 0x00563a74;
constexpr Word kRamp = 0x00563a84;                // a pointer: the colour ramp
constexpr Word kModelColour = 0x00564384;         // state bit 0x200's colour (0x00800080: chosen by the selector)
constexpr Word kFaceStateBits = 0x0056437c;
constexpr Word kArenaCursor = 0x00560fd0;         // RenderPolygonArenaCursor
constexpr Word kArenaEnd = 0x00568628;            // RenderPolygonArenaEnd
constexpr Word kQuadOrder = 0x00549d50;           // four dwords: 0, 1, 3, 2
constexpr Word kYScale = 0x00549d60;              // float: the y stretch of state bit 0x10
constexpr Word kPolyHeapOverflow = 0x00549d64;    // "D3DModel:allocPoly(): Poly Heap Overflow"
constexpr Word kBucketHeads = 0x0056433c;         // RenderBucketHeads (a pointer): 8-byte entries, word 0 the chain
constexpr Word kDepthOffset2000 = 0x00563a64;     // shorts: the depth offsets of the selector's 0x6000 field
constexpr Word kDepthOffset4000 = 0x00563a7c;
constexpr Word kDepthOffset6000 = 0x00563a88;
constexpr Word kTextureMode = 0x006a6964;         // D3DTextureMode
constexpr Word kWindingThreshold = 0x00518484;    // RenderWindingThreshold (float)
constexpr Word kUnitScale = 0x00518d70;           // RenderProjectionUnitScale (float, 1.0)
constexpr Word kVideoPlaying = 0x004cde70;        // Video_Playing()

constexpr Word kRecordSize = 0x1c;
constexpr Word kSlotSize = 0xc0;
constexpr std::size_t kSlotWords = 0x30;

// .rdata constants
constexpr float kInv4096 = std::bit_cast<float>(0x39800000u);        // 0x00518910
constexpr float kGreyScale = std::bit_cast<float>(0x41233333u);      // 0x0051993c
constexpr float kSixteenth = std::bit_cast<float>(0x3d800000u);      // 0x00519940
constexpr float kInv65536 = std::bit_cast<float>(0x37800000u);       // 0x00519944
constexpr float kHalf = std::bit_cast<float>(0x3f000000u);           // 0x00519948
constexpr Word kReciprocalCap = 0x3f7d70a4u;                         // 0x0051994c
constexpr float kModeTwoScale = std::bit_cast<float>(0x3f333333u);   // 0x00519950
constexpr float kModeOneScale = std::bit_cast<float>(0x3f733333u);   // 0x00519954
constexpr float kForcedDepth = std::bit_cast<float>(0x467ff000u);    // 0x00519958 (16380)

// precision control 24: each operation rounds to binary32 (a double holds every operand exactly, and 53 >= 2 * 24 + 2 bits keep the double rounding innocuous)
float mulf(double a, double b) { return static_cast<float>(a * b); }
float addf(double a, double b) { return static_cast<float>(a + b); }
float subf(double a, double b) { return static_cast<float>(a - b); }
float divf(double a, double b) { return static_cast<float>(a / b); }

// fcom st0, src then fnstsw: C0 is "st0 below src", C3 "equal"; an unordered compare sets both
bool c0(float st0, float src) { return !(st0 >= src); }
bool c0_or_c3(float st0, float src) { return !(st0 > src); }

std::int32_t ftol(float value) { return mathkit::x87::ftol(value); }

// (c >> 16 & 0xff) | (c & 0xff00) | ((c | 0xffffff00) << 16): red and blue swapped, the top byte 0xff
Word swap_red_blue(Word c) { return ((c >> 16) & 0xffu) | (c & 0xff00u) | ((c | 0xffffff00u) << 16); }

// 0x004d1cd0 M3D_ModulateColor: each of the low three bytes a * b / 255, the top byte the first colour's
Word modulate(Word a, Word b) {
    Word out = a & 0xff000000u;
    for (const unsigned shift : {0u, 8u, 16u}) {
        out |= ((((a >> shift) & 0xffu) * ((b >> shift) & 0xffu)) / 0xffu) << shift;
    }
    return out;
}

struct Edges {
    float right, bottom, left, top;
};

Edges edges(const RetailMemory& m) {
    const Word vp = guest::read_scalar<std::uint32_t>(m, kViewportEdges);
    return {static_cast<float>(guest::read_scalar<std::uint16_t>(m, vp)), static_cast<float>(guest::read_scalar<std::uint16_t>(m, vp + 2)), static_cast<float>(guest::read_scalar<std::uint16_t>(m, vp + 4)), static_cast<float>(guest::read_scalar<std::uint16_t>(m, vp + 6))};
}

// 1 left of the viewport, 2 right, 4 above, 8 below, each `fild edge; fcomp coordinate` (the strip vertices and the near clip); an ordinary vertex of M3D_TransformVertices compares x with
// the left edge the other way round (`fcompp` with x on top), which differs for a NaN
Word edge_flags(const Edges& e, float x, float y, bool ordinary) {
    Word flags = (ordinary ? c0(x, e.left) : !c0_or_c3(e.left, x)) ? 1u : 0u;
    if (c0_or_c3(e.right, x)) {
        flags |= 2;
    }
    if (!c0_or_c3(e.top, y)) {
        flags |= 4;
    }
    if (c0_or_c3(e.bottom, y)) {
        flags |= 8;
    }
    return flags;
}

}  // namespace

// 0x004d14d0 M3D_SubmitModelState(model, flags) (cdecl): the view constants, the model's matrix (from the model transform and the camera when state bit 0x01 is clear, from the two-level transform when
// it is set), the packet's arrays, the vertices, the grey of state bit 0x08 and the faces (unless state bit 0x04). The software renderer below level 2 draws no background (state bit 0x10).
void RenderKit::model_submit_state(Word model, Word flags) {
    set_global_word(m_, kModelFlags, flags);
    const Word vp = global_word(m_, kViewportEdges);
    set_global_word(m_, kDepthScale, float_bits(static_cast<float>(half_at(m_, vp + 0xe))));
    set_global_word(m_, kCentreX, float_bits(static_cast<float>(half_at(m_, vp + 0x10))));
    set_global_word(m_, kCentreY, float_bits(static_cast<float>(half_at(m_, vp + 0x12))));
    const float near_depth = static_cast<float>(half_at(m_, vp + 8));
    set_global_word(m_, kNearDepth, float_bits(near_depth));
    set_global_word(m_, kFacesWalked, 0);
    const float yon = mulf(half_at(m_, vp + 0xa), float_at(kFogYonScale));
    set_global_word(m_, kYon, float_bits(yon));
    set_global_word(m_, kNearOverYon, float_bits(divf(near_depth, yon)));
    const std::int32_t software = global_int(m_, kSoftwareRenderer);
    if ((flags & 0x10) != 0 && software != 0 && software < 2) {
        return;
    }
    if ((flags & 1) == 0) {
        for (Word k = 0; k < 9; ++k) {
            set_word_at(m_, kMatrix + 4 * k, float_bits(mulf(short_at(m_, kMatrixA + 2 * k), kInv4096)));
        }
        const Word camera = global_word(m_, kCamera);
        std::array<float, 9> local{};
        for (Word k = 0; k < 9; ++k) {
            local[k] = mulf(short_at(m_, camera + 0x74 + 2 * k), kInv4096);
        }
        const double v0 = global_int(m_, kTranslationA);
        const double v1 = global_int(m_, kTranslationA + 4);
        const double v2 = global_int(m_, kTranslationA + 8);
        for (Word row = 0; row < 3; ++row) {
            // ((l[2] * v2) + (l[1] * v1)) + (l[0] * v0): the machine's order
            const float sum = addf(mulf(local[row * 3 + 2], v2), mulf(local[row * 3 + 1], v1));
            set_word_at(m_, kTranslation + 4 * row, float_bits(addf(sum, mulf(local[row * 3], v0))));
        }
        if ((global_byte(m_, kModelFlags) & 0x40) != 0) {
            set_global_word(m_, kBackVector, float_bits(-local[1]));
            set_global_word(m_, kBackVector + 4, float_bits(-local[4]));
            set_global_word(m_, kBackVector + 8, float_bits(-local[7]));
        }
        set_global_word(m_, kSecondaryCursor, kSecondaryHeap);
    } else {
        std::array<float, 9> m1{};
        std::array<float, 9> m2{};
        for (Word k = 0; k < 9; ++k) {
            m1[k] = mulf(short_at(m_, kMatrixB + 2 * k), kInv4096);
        }
        const double tx = global_int(m_, kTranslationB);
        const float ty = static_cast<float>(global_int(m_, kTranslationB + 4));   // stored through fstp
        const double tz = global_int(m_, kTranslationB + 8);
        for (Word k = 0; k < 9; ++k) {
            m2[k] = mulf(short_at(m_, kMatrixC + 2 * k), kInv65536);
        }
        const float a = mulf(global_short(m_, kOffsetC), kSixteenth);
        const float b = mulf(global_short(m_, kOffsetC + 2), kSixteenth);
        const float c = mulf(global_short(m_, kOffsetC + 4), kSixteenth);
        for (Word row = 0; row < 3; ++row) {
            for (Word col = 0; col < 3; ++col) {
                const float outer = addf(mulf(m2[col], m1[row * 3]), mulf(m2[6 + col], m1[row * 3 + 2]));
                set_word_at(m_, kMatrix + 4 * (row * 3 + col), float_bits(addf(outer, mulf(m2[3 + col], m1[row * 3 + 1]))));
            }
        }
        const auto offset = [&](Word row) { return addf(addf(mulf(m1[row * 3 + 2], c), mulf(m1[row * 3 + 1], b)), mulf(m1[row * 3], a)); };
        set_global_word(m_, kTranslation, float_bits(addf(offset(0), tx)));
        set_global_word(m_, kTranslation + 4, float_bits(addf(offset(1), ty)));
        set_global_word(m_, kTranslation + 8, float_bits(addf(offset(2), tz)));
        if ((flags & 2) != 0) {
            set_global_word(m_, kSecondaryCursor, kSecondaryHeap);
        }
    }
    set_global_word(m_, kModel, model);
    const Word vertices = model + 0x1c;
    set_global_word(m_, kVertices, vertices);
    const Word normals = vertices + static_cast<Word>(half_at(m_, model + 2)) * 8;
    set_global_word(m_, kNormals, normals);
    set_global_word(m_, kFaces, normals + static_cast<Word>(half_at(m_, model + 4)) * 8);
    model_transform_vertices(vertices, half_at(m_, model + 2));
    if ((flags & 4) != 0) {
        return;
    }
    if ((global_byte(m_, kModelFlags) & 8) != 0) {
        const Word g = static_cast<Word>(ftol(mulf(float_at(kGreySource), kGreyScale)));
        set_global_word(m_, kGrey, ((((g | 0xffffff00u) << 8) | g) << 8) | g);
    }
    model_submit_faces();
}

// 0x004d29e0 M3D_TransformVertices(vertices, count, flag) (cdecl): each 8-byte source vertex (x, y, z shorts, then its flags) into its record: projected through the matrix (flag 0x01 keeps a copy in
// the secondary heap), named in the secondary heap (flag 0x02), or (flag 0x10) offset from a projected vertex along the line to another, perpendicular to it, by z pixels at its scale
void RenderKit::model_transform_vertices(Word vertices, std::int32_t count) {
    const Word flags = global_word(m_, kModelFlags);
    const float near_depth = float_at(kNearDepth);
    const float yon = float_at(kYon);
    for (std::int32_t i = 0; i < count; ++i) {
        const Edges e = edges(m_);
        const Word source = vertices + 8 * static_cast<Word>(i);
        const Word record = kVertexHeap + kRecordSize * static_cast<Word>(i);
        const std::uint16_t source_flags = half_at(m_, source + 6);
        set_word_at(m_, record + 0x10, static_cast<Word>(static_cast<std::int32_t>(static_cast<std::int16_t>(source_flags))));
        if ((source_flags & 0x10) != 0) {
            const auto resolve = [&](Word index) {
                const Word at = kVertexHeap + kRecordSize * index;
                const Word other = word_at(m_, at + 0x18);
                return other != 0 ? other : at;
            };
            const Word first = resolve(half_at(m_, source) >> 3);
            const Word second = resolve(half_at(m_, source + 2) >> 3);
            const double z = short_at(m_, source + 4);
            const float dx = subf(float_at(second), float_at(first));
            const float ndy = -subf(float_at(second + 4), float_at(first + 4));
            const float length = static_cast<float>(std::sqrt(static_cast<double>(addf(mulf(ndy, ndy), mulf(dx, dx)))));
            const float scale = float_at(first + 0xc);
            const float term_x = mulf(mulf(divf(dx, length), z), scale);
            const float term_y = mulf(mulf(divf(ndy, length), z), scale);
            set_word_at(m_, record, float_bits(addf(term_y, float_at(first))));
            set_word_at(m_, record + 4, float_bits(addf(term_x, float_at(first + 4))));
            set_word_at(m_, record + 8, word_at(m_, first + 8));
            set_word_at(m_, record + 0xc, word_at(m_, first + 0xc));
            set_word_at(m_, record + 0x18, 0);
            const float depth = float_at(record + 8);
            Word clip = edge_flags(e, float_at(record), float_at(record + 4), false);
            if (c0(depth, near_depth)) {
                clip |= 0x10;
            }
            if ((flags & 0x10) == 0 && !c0(depth, yon)) {
                clip |= 0x20;
            }
            set_word_at(m_, record + 0x14, clip);
            set_word_at(m_, record + 0x14, (word_at(m_, first + 0x14) & 0x100) | clip);
            continue;
        }
        if ((source_flags & 2) != 0) {
            set_word_at(m_, record + 0x18, kSecondaryHeap + static_cast<Word>(int_at(m_, source) >> 3) * kRecordSize);
            continue;
        }
        const double x = short_at(m_, source);
        const double y = short_at(m_, source + 2);
        const double z = short_at(m_, source + 4);
        const auto row = [&](Word base, Word bias) {
            const float sum = addf(mulf(float_at(kMatrix + 4 * (base + 2)), z), mulf(float_at(kMatrix + 4 * (base + 1)), y));
            return addf(addf(sum, mulf(float_at(kMatrix + 4 * base), x)), float_at(bias));
        };
        set_word_at(m_, record, float_bits(row(0, kTranslation)));
        set_word_at(m_, record + 4, float_bits(row(3, kTranslation + 4)));
        set_word_at(m_, record + 8, float_bits(row(6, kTranslation + 8)));
        bool back = false;
        if ((flags & 0x40) != 0) {
            const float lhs = addf(mulf(float_at(kBackVector + 8), float_at(record + 8)), mulf(float_at(kBackVector), float_at(record)));
            const float dot = addf(lhs, mulf(float_at(kBackVector + 4), float_at(record + 4)));
            const float bias = subf(float_at(kBackBias), int_at(m_, global_word(m_, kCamera) + 8));
            back = !c0_or_c3(addf(dot, bias), float_at(kWindingThreshold));
        }
        const float depth = float_at(record + 8);
        const float scale = divf(float_at(kDepthScale), depth);
        if ((flags & 0x10) != 0) {
            set_word_at(m_, record + 4, float_bits(mulf(float_at(kYScale), float_at(record + 4))));
        }
        set_word_at(m_, record + 0x18, 0);
        const float sx = addf(mulf(scale, float_at(record)), float_at(kCentreX));
        set_word_at(m_, record, float_bits(sx));
        const float sy = addf(mulf(scale, float_at(record + 4)), float_at(kCentreY));
        set_word_at(m_, record + 4, float_bits(sy));
        set_word_at(m_, record + 0xc, float_bits(scale));
        Word clip = edge_flags(e, sx, sy, true);
        if (c0(depth, near_depth)) {
            clip |= 0x10;
        }
        if ((flags & 0x10) == 0 && !c0(depth, yon)) {
            clip |= 0x20;
        }
        if (back) {
            clip |= 0x100;
        }
        set_word_at(m_, record + 0x14, clip);
        if ((byte_at(m_, source + 6) & 1) != 0) {
            const Word cursor = global_word(m_, kSecondaryCursor);
            m_.copy(cursor, record, kRecordSize);
            set_global_word(m_, kSecondaryCursor, cursor + kRecordSize);
        }
    }
}

// 0x004d18b0 M3D_SubmitFacePolygons: the model's pushback, then for each face its selector (the face's word masked by the selector source); a face with a 0xc0 bit is coloured and built
void RenderKit::model_submit_faces() {
    const Word index = global_word(m_, kCurrentLevel) * 2000 + global_word(m_, kModelId);
    set_global_word(m_, kModelPushback, static_cast<Word>(static_cast<std::int32_t>(short_at(m_, kPushbacks + 2 * index))));
    Word face = global_word(m_, kFaces);
    Word source = global_word(m_, kSelectorSource);
    for (Word k = 0; k < half_at(m_, global_word(m_, kModel) + 6); ++k) {
        Word selector = ((source >> 16) & half_at(m_, face)) | (source & 0xffffu);
        set_global_word(m_, kSelector, selector);
        if ((global_byte(m_, kModelFlags) & 8) != 0) {
            selector &= 0xfffffffeu;
            set_global_word(m_, kSelector, selector);
        }
        if ((selector & 0xc0) != 0) {
            model_face_colour(face);
            model_build_polygon(face);
            source = global_word(m_, kSelectorSource);
        }
        const Word length = half_at(m_, face + 2);
        if (length == 0) {
            throw std::runtime_error("M3D_SubmitFacePolygons: a face record of length 0 (retail walks it for ever)");
        }
        face += length;
        set_global_word(m_, kFacesWalked, global_word(m_, kFacesWalked) + 1);
    }
}

// 0x004d1960 M3D_ResolveFaceColor(face) (cdecl): the four corner colours at 0x00580888: the model colour (state bit 0x200), the face's colour or its ramp entries (selector 0x800), the background's level
// tint (state bit 0x10), then lit by the scene colour (selector 0x08), the light of the face (0x04) or of each corner (both)
void RenderKit::model_face_colour(Word face) {
    const Word flags = global_word(m_, kModelFlags);
    const Word selector = global_word(m_, kSelector);
    const auto colour = [&](Word corner) { return kFaceColours + 4 * corner; };
    const auto set_all = [&](Word value) {
        for (Word corner = 0; corner < 4; ++corner) {
            set_word_at(m_, colour(corner), value);
        }
    };
    if ((flags & 0x200) != 0) {
        Word low = 0x80;
        Word blue = 0;
        const Word word = global_word(m_, kModelColour);
        if (word != 0) {
            blue = (word >> 16) & 0xffu;
            low = word;
            if (word == 0x00800080u) {
                if ((selector & 0x2000) != 0) {
                    low = 0x8000;
                    blue = 0;
                }
                if ((selector & 0x4000) != 0) {
                    low = 0x80;
                    blue = 0;
                }
            }
        }
        set_all((low & 0xff00u) | blue | ((low | 0xffffff00u) << 16));
        return;
    }
    const Word word = word_at(m_, face + 8);
    if ((selector & 0x800) == 0) {
        set_all(swap_red_blue(word));
    } else {
        const Word ramp = global_word(m_, kRamp);
        set_word_at(m_, colour(0), swap_red_blue(word_at(m_, ramp + 4 * (word & 0xffu))));
        set_word_at(m_, colour(1), swap_red_blue(word_at(m_, ramp + 4 * ((word >> 8) & 0xffu))));
        set_word_at(m_, colour(2), swap_red_blue(word_at(m_, ramp + 4 * ((word >> 16) & 0xffu))));
        if ((selector & 0x10) == 0) {
            set_word_at(m_, colour(3), swap_red_blue(word_at(m_, ramp + 4 * (word_at(m_, face + 8) >> 24))));
        }
    }
    if ((flags & 0x10) != 0) {
        const Word level = global_word(m_, kCurrentLevel);
        if (level == 3) {
            for (Word corner = 0; corner < 4; ++corner) {
                for (Word byte = 0; byte < 3; ++byte) {
                    const Word at = colour(corner) + byte;
                    set_byte_at(m_, at, (static_cast<Word>(byte_at(m_, at)) * 0xffu) >> 7);
                }
            }
        } else if (level == 6) {
            set_all(0xffffffffu);
        }
    }
    const auto modulate_all = [&](const auto& light) {
        for (Word corner = 0; corner < 4; ++corner) {
            set_word_at(m_, colour(corner), modulate(word_at(m_, colour(corner)), light(corner)));
        }
    };
    if ((selector & 4) == 0) {
        if ((selector & 8) == 0) {
            return;
        }
        const Word scene = swap_red_blue(global_word(m_, kSceneColour));
        modulate_all([&](Word) { return scene; });
    } else if ((selector & 8) == 0) {
        const Word light = word_at(m_, kLights + 4 * (static_cast<Word>(half_at(m_, face + 0xc)) >> 3));
        modulate_all([&](Word) { return light; });
    } else {
        modulate_all([&](Word corner) { return word_at(m_, kLights + 4 * byte_at(m_, face + 4 + corner)); });
    }
}

// the next arena slot (the cursor moves on); retail logs the overflow and then writes through a null slot.
// With an enlarged arena (HostOptions::poly_buffer_area_scale above 1, docs: THPS2 post_phase_5/polybuffer.md P4) the face is dropped instead: 0 is answered (no slot), the drop is counted,
// and retail's own message is logged once per level (Init_Cleanup starts the next count). The frame draws a polygon short rather than the program stopping.
Word RenderKit::model_allocate_polygon() {
    const Word cursor = global_word(m_, kArenaCursor);
    if (cursor < global_word(m_, kArenaEnd)) {
        set_global_word(m_, kArenaCursor, cursor + kSlotSize);
        return cursor;
    }
    HostOptions& host = host_options();
    if (host.poly_buffer_area_scale > 1) {
        ++host.poly_overflows;
        if (!host.poly_overflow_logged) {
            host.poly_overflow_logged = true;
            log_line(kPolyHeapOverflow);
        }
        return 0;
    }
    log_line(kPolyHeapOverflow);
    throw std::runtime_error("M3D_BuildD3DPolygon: the polygon arena is full (retail logs \"Poly Heap Overflow\" and writes through a null slot)");
}

// 0x004d1d40 M3D_BuildD3DPolygon(face) (cdecl): a 0xb0 packet in the next arena slot (word 2 the selector and the state's flags, word 4 the material, word 5 the corner count, then per corner the
// projected record, the colour and for a textured face its texture coordinates at the texel centre); a face wholly behind the near plane or wholly outside one edge gives its slot back; the hardware
// renderer draws a textured quad as two triangles (v0, v1, v3 then v3, v1, v2)
void RenderKit::model_build_polygon(Word face) {
    const Word selector = global_word(m_, kSelector);
    const bool textured = (selector & 1) != 0;
    Word all = 0;
    Word any = 0;
    const Word slot = model_allocate_polygon();
    if (slot == 0) {
        return;   // the enlarged arena is full: the face is dropped (model_allocate_polygon)
    }
    set_byte_at(m_, slot + 7, 0xb0);
    set_word_at(m_, slot + 8, selector);
    set_word_at(m_, slot + 0x14, (selector & 0x10) != 0 ? 3u : 4u);
    const Word flags = global_word(m_, kModelFlags);
    if ((flags & 0x400) != 0) {
        set_word_at(m_, slot + 8, word_at(m_, slot + 8) | 0x80000000u);
    }
    if ((flags & 0x200) != 0) {
        const Word before = word_at(m_, slot + 8);
        set_word_at(m_, slot + 8, before & 0xcfffffffu);
        if ((global_byte(m_, kFaceStateBits) & 1) != 0) {
            set_word_at(m_, slot + 8, (before & 0xcfffffffu) | 0x10000000u);
        }
        if ((global_byte(m_, kFaceStateBits) & 2) != 0) {
            set_word_at(m_, slot + 8, word_at(m_, slot + 8) | 0x20000000u);
        }
    }
    const std::int32_t software = global_int(m_, kSoftwareRenderer);
    if (software != 0 && (flags & 0x80) != 0) {
        set_word_at(m_, slot + 8, word_at(m_, slot + 8) | 0x40000000u);
    }
    Word texture = 0;
    if (textured) {
        set_word_at(m_, slot + 0x10, word_at(m_, face + 0x10));
        texture = word_at(m_, word_at(m_, face + 0x10) + 0x14);
    } else {
        set_word_at(m_, slot + 0x10, 0);
    }
    const Word state = word_at(m_, slot + 8);
    const std::int32_t count = int_at(m_, slot + 0x14);
    Word out = slot + 0x18;
    for (std::int32_t corner = 0; corner < count; ++corner) {
        const Word source = count == 4 ? word_at(m_, kQuadOrder + 4 * static_cast<Word>(corner)) : static_cast<Word>(corner);
        Word record = kVertexHeap + kRecordSize * byte_at(m_, face + 4 + source);
        if (const Word other = word_at(m_, record + 0x18); other != 0) {
            record = other;
        }
        const Word clip = word_at(m_, record + 0x14);
        all = corner != 0 ? (all & clip) : clip;
        any |= clip;
        for (Word w = 0; w < 16; w += 4) {
            set_word_at(m_, out + w, word_at(m_, record + w));
        }
        Word value = word_at(m_, kFaceColours + 4 * source);
        set_word_at(m_, out + 0x10, value);
        if ((state & 0x1c0) == 0x140 && global_int(m_, kSoftwareRenderer) == 0) {
            const Word r = std::min<Word>(((value >> 16) & 0xffu) * 5, 0xff);
            const Word g = std::min<Word>(((value >> 8) & 0xffu) * 5, 0xff);
            const Word b = std::min<Word>((value & 0xffu) * 5, 0xff);
            value = ((((r | 0xffffff00u) << 8) | g) << 8) | b;
            set_word_at(m_, out + 0x10, value);
        }
        if ((flags & 8) != 0) {
            set_word_at(m_, out + 0x10, global_word(m_, kGrey));
        }
        if (textured) {
            set_word_at(m_, out + 0x14, float_bits(divf(addf(byte_at(m_, face + 0x14 + 2 * source), kHalf), short_at(m_, texture + 0x14))));
            set_word_at(m_, out + 0x18, float_bits(divf(addf(byte_at(m_, face + 0x15 + 2 * source), kHalf), short_at(m_, texture + 0x16))));
            out += 0x1c;
        } else {
            out += 0x14;
        }
    }
    if ((all & 0x10) != 0 || ((all & 0x3f) != 0 && (any & 0x10) == 0)) {
        set_global_word(m_, kArenaCursor, global_word(m_, kArenaCursor) - kSlotSize);
        return;
    }
    if (textured && int_at(m_, slot + 0x14) == 4 && (software == 0 || software > 1)) {
        const std::vector<std::uint8_t> original = m_.bytes(slot, kSlotSize);
        m_.copy(slot + 0x50, slot + 0x6c, 0x1c);
        set_word_at(m_, slot + 0x14, 3);
        model_clip_and_bucket(slot, face, any);
        const Word second = model_allocate_polygon();
        if (second == 0) {
            return;   // the first triangle is in; the second is dropped
        }
        m_.write(second, original);
        m_.copy(second + 0x18, second + 0x6c, 0x1c);
        set_word_at(m_, second + 0x14, 3);
        model_clip_and_bucket(second, face, any);
        return;
    }
    model_clip_and_bucket(slot, face, any);
}

// 0x004d20f0 M3D_ClipAndBucketPolygon(polygon, face, clip) (cdecl): the near clip when a corner is behind the near plane, the winding test (both triangles of a quad must agree before a quad is
// dropped), the depth offset of the selector's 0x6000 field, the bucket, the flat-texture rewrite of word 2, and the link at the head of the bucket
void RenderKit::model_clip_and_bucket(Word polygon, Word face, Word clip) {
    if ((clip & 0x10) != 0 && model_clip_near_plane(polygon) == 0) {
        set_global_word(m_, kArenaCursor, global_word(m_, kArenaCursor) - kSlotSize);
        return;
    }
    const std::int32_t count = int_at(m_, polygon + 0x14);
    const Word stride = word_at(m_, polygon + 0x10) != 0 ? 7 : 5;
    const auto x = [&](Word word) { return float_at(polygon + 4 * word); };
    const Word v0 = 6;
    const Word v1 = 6 + stride;
    const Word v2 = 6 + 2 * stride;
    const Word v3 = 6 + 3 * stride;
    const Word last = stride == 7 ? static_cast<Word>(count) * 7 - 1 : static_cast<Word>(count) * 5 + 1;
    const float a = subf(x(v1), x(v0));
    const float b = subf(x(v1 + 1), x(v0 + 1));
    const float c = subf(x(last), x(v0));
    const float d = subf(x(last + 1), x(v0 + 1));
    const float threshold = float_at(kWindingThreshold);
    bool reject = c0_or_c3(subf(mulf(d, a), mulf(c, b)), threshold);
    bool keep = false;
    if (count > 3) {
        const float e = subf(x(v1), x(v2));
        const float f = subf(x(v1 + 1), x(v2 + 1));
        const float h = subf(x(v3), x(v2));
        const float k = subf(x(v3 + 1), x(v2 + 1));
        keep = c0_or_c3(subf(mulf(h, f), mulf(k, e)), threshold) != reject;
    }
    if (!keep) {
        if (global_half(m_, kReverseWinding) != 0) {
            reject = !reject;
        }
        if (reject) {
            set_global_word(m_, kArenaCursor, global_word(m_, kArenaCursor) - kSlotSize);
            return;
        }
    }
    std::int32_t offset = 0;
    switch (global_word(m_, kSelector) & 0x6000) {
    case 0x2000: offset = global_short(m_, kDepthOffset2000); break;
    case 0x4000: offset = global_short(m_, kDepthOffset4000); break;
    case 0x6000: offset = global_short(m_, kDepthOffset6000); break;
    default: break;
    }
    const Word bucket = model_classify_depth(polygon, face, float_bits(static_cast<float>(offset)), 0);
    const Word level = global_word(m_, kCurrentLevel);
    const Word material = word_at(m_, polygon + 0x10);
    if (level != 6 && level != 10 && material != 0) {
        const Word state = word_at(m_, polygon + 8);
        if ((state & 0x1c0) == 0x40 && (word_at(m_, word_at(m_, material + 0x14) + 0x10) & 0x110) == 0) {
            set_word_at(m_, polygon + 8, (state & 0xffffffbfu) | 0x80u);
        }
    }
    const Word entry = global_word(m_, kBucketHeads) + 8 * bucket;
    set_word_at(m_, polygon, word_at(m_, entry));
    set_word_at(m_, entry, polygon);
    // Presentation (not retail; no effect on memory): a part-width view's polygon on a wide screen follows the view into the 4:3 layout when it is drawn
    const Word vp = global_word(m_, kViewportEdges);
    drawkit::presentation_view_polygon(polygon, static_cast<std::int16_t>(half_at(m_, vp + 4)), static_cast<std::int16_t>(half_at(m_, vp)));
}

// 0x004d2310 M3D_ClipPolygonNearPlane(polygon) (cdecl): the corners back to camera space (a copy of the slot on the stack), the polygon cut at the near depth (the colours' low three bytes and the
// texture coordinates interpolated, M3D_InterpolatePolygonEdge 0x004d25c0), projected again; the polygon, or 0 when fewer than three corners remain, more than six, or all are outside one edge
Word RenderKit::model_clip_near_plane(Word polygon) {
    const float near_depth = float_at(kNearDepth);
    std::array<Word, kSlotWords> local{};
    for (std::size_t w = 0; w < kSlotWords; ++w) {
        local[w] = word_at(m_, polygon + 4 * static_cast<Word>(w));
    }
    const auto at = [&](std::size_t w) { return float_from(local.at(w)); };
    const bool textured = local[4] != 0;
    const std::size_t stride = textured ? 7 : 5;
    const std::int32_t count = static_cast<std::int32_t>(local[5]);
    const float centre_x = float_at(kCentreX);
    const float centre_y = float_at(kCentreY);
    const float depth_scale = float_at(kDepthScale);
    const float unit = float_at(kUnitScale);
    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t base = 6 + static_cast<std::size_t>(i) * stride;
        const float factor = divf(unit, at(base + 3));
        local.at(base) = float_bits(mulf(subf(at(base), centre_x), factor));
        local.at(base + 1) = float_bits(mulf(subf(at(base + 1), centre_y), factor));
        local.at(base + 3) = float_bits(mulf(depth_scale, factor));
    }
    const auto out = [&](std::size_t w) { return polygon + 4 * static_cast<Word>(w); };
    // 0x004d25c0 M3D_InterpolatePolygonEdge(out, current, next, near, textured)
    const auto interpolate = [&](std::size_t dst, std::size_t cur, std::size_t next) {
        set_word_at(m_, out(dst + 3), float_bits(near_depth));
        const float t = divf(subf(near_depth, at(cur + 3)), subf(at(next + 3), at(cur + 3)));
        set_word_at(m_, out(dst), float_bits(addf(mulf(subf(at(next), at(cur)), t), at(cur))));
        set_word_at(m_, out(dst + 1), float_bits(addf(mulf(subf(at(next + 1), at(cur + 1)), t), at(cur + 1))));
        for (const Word byte : {2u, 1u, 0u}) {
            const Word from = (local.at(cur + 4) >> (8 * byte)) & 0xffu;
            const Word to = (local.at(next + 4) >> (8 * byte)) & 0xffu;
            const std::int32_t delta = ftol(mulf(static_cast<double>(to) - static_cast<double>(from), t));
            set_byte_at(m_, out(dst + 4) + byte, static_cast<Word>(delta) + from);
        }
        if (textured) {
            set_word_at(m_, out(dst + 5), float_bits(addf(mulf(subf(at(next + 5), at(cur + 5)), t), at(cur + 5))));
            set_word_at(m_, out(dst + 6), float_bits(addf(mulf(subf(at(next + 6), at(cur + 6)), t), at(cur + 6))));
        }
    };
    const auto copy_corner = [&](std::size_t dst, std::size_t src) {
        for (std::size_t w = 0; w < stride; ++w) {
            set_word_at(m_, out(dst + w), local.at(src + w));
        }
    };
    std::int32_t produced = 0;
    std::size_t dest = 6;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t cur = 6 + static_cast<std::size_t>(i) * stride;
        const std::size_t next = i == count - 1 ? 6 : cur + stride;
        const bool cur_behind = c0(at(cur + 3), near_depth);
        const bool next_behind = c0(at(next + 3), near_depth);
        if (!cur_behind) {
            copy_corner(dest, cur);
            dest += stride;
            ++produced;
            if (next_behind) {
                interpolate(dest, cur, next);
                dest += stride;
                ++produced;
            }
        } else if (!next_behind) {
            interpolate(dest, cur, next);
            dest += stride;
            ++produced;
        }
    }
    Word result = 0;
    if (produced >= 3) {
        Word all = 0;
        for (std::int32_t i = 0; i < produced; ++i) {
            const std::size_t base = 6 + static_cast<std::size_t>(i) * stride;
            const float depth = float_at(out(base + 3));
            set_word_at(m_, out(base + 2), float_bits(depth));
            const float scale = divf(depth_scale, depth);
            set_word_at(m_, out(base + 3), float_bits(scale));
            const float sx = addf(mulf(scale, float_at(out(base))), centre_x);
            set_word_at(m_, out(base), float_bits(sx));
            const float sy = addf(mulf(float_at(out(base + 1)), scale), centre_y);
            set_word_at(m_, out(base + 1), float_bits(sy));
            const Word flags = edge_flags(edges(m_), sx, sy, false);
            all = i == 0 ? flags : (all & flags);
        }
        result = all != 0 ? 0u : static_cast<Word>(produced);
    }
    set_word_at(m_, polygon + 0x14, result);
    return result >= 3 && result <= 6 ? polygon : 0u;
}

// 0x004d26b0 M3D_ClassifyPolygonDepth(polygon, face, offset, depth) (cdecl; the last two floats): the depth mode (1 a texture's, 2 state bits 0x88, 3 the back faces and state bit 0x20, 4 the
// background), each corner's depth adjusted, scaled and turned into the reciprocal the draw tests (at most 0.99), and the bucket of the farthest corner (the nearest for an untextured mode-1
// model with no pushback), a quarter of its depth
Word RenderKit::model_classify_depth(Word polygon, Word face, Word offset_bits, Word depth_bits) {
    const Word selector = global_word(m_, kSelector);
    const bool textured = (selector & 1) != 0;
    bool texture_mode;
    if (!textured) {
        texture_mode = ((selector >> 6) & 1) != 0;
    } else {
        texture_mode = !((selector & 0x40) == 0 && (byte_at(m_, word_at(m_, word_at(m_, face + 0x10) + 0x14) + 0x10) & 0x10) != 0);
    }
    const Word flags = global_word(m_, kModelFlags);
    bool back = ((flags >> 5) & 1) != 0;
    const std::int32_t count = int_at(m_, polygon + 0x14);
    if ((flags & 0x40) != 0) {
        bool any_back = false;
        for (std::int32_t k = 0; k < count; ++k) {
            Word record = kVertexHeap + kRecordSize * byte_at(m_, face + 4 + static_cast<Word>(k));
            if (const Word other = word_at(m_, record + 0x18); other != 0) {
                record = other;
            }
            if ((word_at(m_, record + 0x14) & 0x100) != 0) {
                any_back = true;
            }
        }
        if (!any_back) {
            back = true;
        }
    }
    int mode = texture_mode ? 1 : 0;
    if ((flags & 0x88) != 0) {
        mode = 2;
    }
    if (back) {
        mode = 3;
    }
    if ((flags & 0x10) != 0) {
        mode = 4;
        set_word_at(m_, polygon + 8, word_at(m_, polygon + 8) | 0x80000000u);
    }
    const Word stride = textured ? 0x1c : 0x14;
    const float offset = float_from(offset_bits);
    float selected = float_from(depth_bits);
    if (global_int(m_, kSoftwareRenderer) != 0) {
        for (std::int32_t k = 0; k < count; ++k) {
            const Word corner = polygon + 0x18 + stride * static_cast<Word>(k);
            if (k == 0 || c0(selected, float_at(corner + 8))) {
                selected = float_at(corner + 8);
            }
            if (depth_bits != 0) {
                set_word_at(m_, corner + 0x10, depth_bits);
            }
        }
        if (mode == 3) {
            selected = addf(addf(selected, float_at(kZPushback)), offset);
        } else if (mode == 4) {
            selected = addf(kForcedDepth, offset);
        } else {
            selected = addf(selected, offset);
        }
    } else {
        const Word texture_mode_word = global_word(m_, kTextureMode);
        bool nearest = false;
        float scale = float_at(kUnitScale);
        float adjust = offset;
        if (texture_mode_word == 0 && global_word(m_, kCurrentLevel) != 0xd) {
            adjust = 0.0f;
            if ((flags & 0x800) != 0) {
                adjust = static_cast<float>(global_int(m_, kModelPushback));
            }
        }
        switch (mode) {
        case 1:
            scale = kModeOneScale;
            if (texture_mode_word == 0 && global_word(m_, kModelPushback) == 0) {
                nearest = true;
            }
            break;
        case 2:
            scale = (engine_value(kVideoPlaying) & 0xffu) != 0 ? kModeOneScale : kModeTwoScale;
            break;
        case 3:
            adjust = addf(float_at(kZPushback), adjust);
            break;
        case 4:
            adjust = background_depth(kForcedDepth, float_at(kYon));   // the game's 16380 unless the host puts the sky behind a far plane beyond it (host_options.hpp)
            break;
        default:
            break;
        }
        if (global_word(m_, kTextureMode) != 0) {
            nearest = false;
        }
        const float near_depth = float_at(kNearDepth);
        for (std::int32_t k = 0; k < count; ++k) {
            const Word z = polygon + 0x18 + stride * static_cast<Word>(k) + 8;
            const float depth = mulf(addf(adjust, float_at(z)), scale);
            set_word_at(m_, z, float_bits(depth));
            if (depth_bits != 0) {
                set_word_at(m_, z + 8, depth_bits);
            }
            if (nearest ? (k == 0 || c0(depth, selected)) : (k == 0 || !c0_or_c3(depth, selected))) {
                selected = depth;
            }
            if (c0(depth, near_depth)) {
                set_word_at(m_, z, float_bits(near_depth));
            }
            const float reciprocal = divf(near_depth, float_at(z));
            set_word_at(m_, z, float_bits(reciprocal));
            if (!c0_or_c3(reciprocal, float_from(kReciprocalCap))) {
                set_word_at(m_, z, kReciprocalCap);
            }
        }
    }
    std::int32_t bucket = ftol(addf(selected, kHalf));
    if (bucket < 0) {
        bucket = 0;
    }
    bucket >>= 2;
    if (bucket > 0xfff) {
        bucket = 0xfff;
    }
    return static_cast<Word>(bucket);
}

}  // namespace opentony::runtime::renderkit
