#include "render_model_submit.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace opentony::trg {
namespace {

// Constants read from the executable's .rdata by the submission chain.
constexpr float kInv4096 = std::bit_cast<float>(0x39800000U);   // 0x00518910
constexpr float kInv65536 = std::bit_cast<float>(0x37800000U);  // 0x00519944
constexpr float kSixteenth = std::bit_cast<float>(0x3d800000U); // 0x00519940
constexpr float kGreyScale = std::bit_cast<float>(0x41233333U); // 0x0051993c
constexpr float kHalfTexel = std::bit_cast<float>(0x3f000000U); // 0x00519948
constexpr float kReciprocalCap = std::bit_cast<float>(0x3f7d70a4U); // 0x0051994c
constexpr float kModeTwoScale = std::bit_cast<float>(0x3f333333U);  // 0x00519950
constexpr float kModeOneScale = std::bit_cast<float>(0x3f733333U);  // 0x00519954
constexpr float kForcedDepth = std::bit_cast<float>(0x467ff000U);   // 0x00519958
constexpr float kYScale = std::bit_cast<float>(0x3f933333U);        // 0x00549d60
constexpr float kUnitScale = 1.0F;                                  // 0x00518d70
constexpr std::array<std::size_t, 4> kQuadOrder{0, 1, 3, 2};       // 0x00549d50

// x87 with the precision control at 24 bits: each operation rounds to binary32.
// double holds every binary32 operand and every 32-bit integer operand exactly,
// and 53 >= 2 * 24 + 2 bits make the double rounding innocuous for + - * / sqrt.
inline float mulf(double a, double b) { return static_cast<float>(a * b); }
inline float addf(double a, double b) { return static_cast<float>(a + b); }
inline float subf(double a, double b) { return static_cast<float>(a - b); }
inline float divf(double a, double b) { return static_cast<float>(a / b); }

inline float as_float(std::uint32_t bits) { return std::bit_cast<float>(bits); }
inline std::uint32_t as_bits(float value) { return std::bit_cast<std::uint32_t>(value); }

inline float word_f(const RetailPolygonImage& image, std::size_t index) {
    return as_float(image[index]);
}
inline void set_word_f(RetailPolygonImage& image, std::size_t index, float value) {
    image[index] = as_bits(value);
}

[[nodiscard]] std::uint8_t byte_at(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset >= data.size()) {
        throw RetailRenderError("model packet read past its end");
    }
    return data[offset];
}
[[nodiscard]] std::uint16_t u16_at(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(byte_at(data, offset) | (byte_at(data, offset + 1) << 8));
}
[[nodiscard]] std::uint32_t u32_at(std::span<const std::uint8_t> data, std::size_t offset) {
    return u16_at(data, offset) | (static_cast<std::uint32_t>(u16_at(data, offset + 2)) << 16);
}
[[nodiscard]] std::int16_t s16_at(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::int16_t>(u16_at(data, offset));
}

// __ftol at 0x005004f4 truncates toward zero; out-of-range values give the x87
// "integer indefinite" 0x80000000.
[[nodiscard]] std::int32_t ftol(float value) {
    if (!(value > -2147483648.0F && value < 2147483648.0F)) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<std::int32_t>(value);
}

[[nodiscard]] std::uint32_t lateral_flags(const RetailViewport& vp, float x, float y) {
    std::uint32_t flags = 0;
    if (x < static_cast<float>(vp.left)) flags |= 1;
    if (static_cast<float>(vp.right) <= x) flags |= 2;
    if (y < static_cast<float>(vp.top)) flags |= 4;
    if (static_cast<float>(vp.bottom) <= y) flags |= 8;
    return flags;
}

} // namespace

RetailModelRenderer::RetailModelRenderer(
    const RetailRenderGlobals& globals, std::uint32_t arena_cursor, std::uint32_t arena_end,
    std::function<RetailPolygonImage(std::uint32_t)> slot_seed)
    : g_(globals),
      cursor_(arena_cursor),
      end_(arena_end),
      heap2_cursor_(globals.secondary_heap_cursor),
      heap2_start_(globals.secondary_heap_cursor),
      heap_(kRetailVertexHeapCapacity, RetailVertexRecord{}),
      slot_seed_(std::move(slot_seed)) {}

void RetailModelRenderer::set_sink(RetailRenderSink sink) { sink_ = std::move(sink); }

void RetailModelRenderer::set_bucket_head(std::uint32_t bucket, std::uint32_t head) {
    bucket_heads_[bucket] = head;
}

std::uint32_t RetailModelRenderer::bucket_head(std::uint32_t bucket) const {
    const auto it = bucket_heads_.find(bucket);
    return it == bucket_heads_.end() ? 0U : it->second;
}

const RetailPolygonImage& RetailModelRenderer::slot(std::uint32_t address) {
    return slot_ref(address);
}

RetailPolygonImage& RetailModelRenderer::slot_ref(std::uint32_t address) {
    auto it = slots_.find(address);
    if (it == slots_.end()) {
        it = slots_.emplace(address, slot_seed_ ? slot_seed_(address) : RetailPolygonImage{}).first;
    }
    return it->second;
}

void RetailModelRenderer::seed_secondary_heap(std::span<const RetailVertexRecord> records) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        heap2_[kRetailSecondaryHeapBase + static_cast<std::uint32_t>(i * kRetailVertexRecordSize)] =
            records[i];
    }
}

std::vector<RetailVertexRecord> RetailModelRenderer::secondary_heap() const {
    std::vector<RetailVertexRecord> records;
    if (heap2_cursor_ <= heap2_start_) {
        return records;
    }
    const std::size_t count = (heap2_cursor_ - heap2_start_) / kRetailVertexRecordSize;
    records.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto it = heap2_.find(heap2_start_ + static_cast<std::uint32_t>(i * kRetailVertexRecordSize));
        if (it != heap2_.end()) {
            records[i] = it->second;
        }
    }
    return records;
}

void RetailModelRenderer::emit(RetailRenderEvent::Kind kind, std::uint32_t poly,
                               std::uint32_t a, std::uint32_t b) {
    if (!sink_) {
        return;
    }
    RetailRenderEvent event;
    event.kind = kind;
    event.poly = poly;
    event.cursor = cursor_;
    event.a = a;
    event.b = b;
    event.selector = selector_;
    event.colors = face_colors_;
    if (poly != 0 && kind != RetailRenderEvent::Kind::build &&
        kind != RetailRenderEvent::Kind::build_end && kind != RetailRenderEvent::Kind::video) {
        event.image = slot_ref(poly);
    }
    sink_(event);
}

std::uint32_t RetailModelRenderer::allocate_slot() {
    if (!(cursor_ < end_)) {
        // Retail logs "D3DModel:allocPoly(): Poly Heap Overflow" and then writes
        // through a null slot pointer.
        throw RetailRenderError("polygon arena exhausted");
    }
    const std::uint32_t address = cursor_;
    cursor_ += kRetailPolygonSlotSize;
    return address;
}

void RetailModelRenderer::rollback_slot() { cursor_ -= kRetailPolygonSlotSize; }

const RetailVertexRecord& RetailModelRenderer::vertex_at(std::uint32_t address) const {
    if (address >= kRetailVertexHeapBase &&
        address < kRetailVertexHeapBase + kRetailVertexHeapCapacity * kRetailVertexRecordSize &&
        (address - kRetailVertexHeapBase) % kRetailVertexRecordSize == 0) {
        return heap_[(address - kRetailVertexHeapBase) / kRetailVertexRecordSize];
    }
    const auto it = heap2_.find(address);
    if (it == heap2_.end()) {
        throw RetailRenderError("vertex pointer outside the working heaps");
    }
    return it->second;
}

const RetailVertexRecord& RetailModelRenderer::resolve_vertex(std::uint32_t index) const {
    const RetailVertexRecord& record = heap_.at(index);
    return record[6] != 0 ? vertex_at(record[6]) : record;
}

bool RetailModelRenderer::submit(const RetailSubmitInputs& inputs) {
    const std::uint32_t flags = inputs.flags;
    const auto packet = inputs.packet;
    render_flags_ = flags;
    const RetailViewport& vp = g_.viewport;
    depth_scale_ = static_cast<float>(vp.depth_scale);
    center_x_ = static_cast<float>(vp.center_x);
    center_y_ = static_cast<float>(vp.center_y);
    near_depth_ = static_cast<float>(vp.near_depth);
    yon_ = mulf(static_cast<double>(vp.yon_depth), g_.fog_yon_scale);
    if ((flags & 0x10) != 0 && g_.software_renderer != 0 && g_.software_renderer < 2) {
        return false;
    }
    prepare_matrices(flags);
    const std::uint32_t vertex_count = u16_at(packet, 2);
    const std::uint32_t normal_count = u16_at(packet, 4);
    const std::uint32_t face_count = u16_at(packet, 6);
    transform_vertices(packet, vertex_count);
    if ((flags & 4) == 0) {
        if ((flags & 8) != 0) {
            const std::uint32_t g = static_cast<std::uint32_t>(
                ftol(mulf(g_.grey_source, kGreyScale)));
            grey_ = ((((g | 0xffffff00U) << 8) | g) << 8) | g;
        }
        const std::size_t faces_offset = 0x1c + static_cast<std::size_t>(vertex_count) * 8 +
                                         static_cast<std::size_t>(normal_count) * 8;
        submit_faces(inputs, packet.subspan(std::min(faces_offset, packet.size())), face_count);
    }
    return true;
}

void RetailModelRenderer::prepare_matrices(std::uint32_t flags) {
    if ((flags & 1) == 0) {
        for (std::size_t i = 0; i < 9; ++i) {
            matrix_[i] = mulf(static_cast<double>(g_.matrix_a[i]), kInv4096);
        }
        std::array<float, 9> local{};
        for (std::size_t i = 0; i < 9; ++i) {
            local[i] = mulf(static_cast<double>(g_.camera_matrix[i]), kInv4096);
        }
        for (std::size_t row = 0; row < 3; ++row) {
            // ((l[2] * v2) + (l[1] * v1)) + (l[0] * v0): the machine's order.
            const double v0 = g_.translation_a[0];
            const double v1 = g_.translation_a[1];
            const double v2 = g_.translation_a[2];
            const float sum = addf(mulf(local[row * 3 + 2], v2), mulf(local[row * 3 + 1], v1));
            translation_[row] = addf(sum, mulf(local[row * 3 + 0], v0));
        }
        if ((flags & 0x40) != 0) {
            back_vector_ = {-local[1], -local[4], -local[7]};
        } else {
            back_vector_ = g_.back_vector;
        }
        heap2_cursor_ = kRetailSecondaryHeapBase;
        heap2_start_ = kRetailSecondaryHeapBase;
        return;
    }
    std::array<float, 9> m1{};
    for (std::size_t i = 0; i < 9; ++i) {
        m1[i] = mulf(static_cast<double>(g_.matrix_b[i]), kInv4096);
    }
    const double tx = g_.translation_b[0];
    const float ty = static_cast<float>(g_.translation_b[1]);  // stored through fstp
    const double tz = g_.translation_b[2];
    std::array<float, 9> m2{};
    for (std::size_t i = 0; i < 9; ++i) {
        m2[i] = mulf(static_cast<double>(g_.matrix_c[i]), kInv65536);
    }
    const float a = mulf(static_cast<double>(g_.offset_c[0]), kSixteenth);
    const float b = mulf(static_cast<double>(g_.offset_c[1]), kSixteenth);
    const float c = mulf(static_cast<double>(g_.offset_c[2]), kSixteenth);
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            const float outer = addf(mulf(m2[col], m1[row * 3 + 0]),
                                     mulf(m2[6 + col], m1[row * 3 + 2]));
            matrix_[row * 3 + col] = addf(outer, mulf(m2[3 + col], m1[row * 3 + 1]));
        }
    }
    const auto offset = [&](std::size_t row) {
        return addf(addf(mulf(m1[row * 3 + 2], c), mulf(m1[row * 3 + 1], b)),
                    mulf(m1[row * 3 + 0], a));
    };
    translation_[0] = addf(offset(0), tx);
    translation_[1] = addf(offset(1), ty);
    translation_[2] = addf(offset(2), tz);
    back_vector_ = g_.back_vector;
    if ((flags & 2) != 0) {
        heap2_cursor_ = kRetailSecondaryHeapBase;
        heap2_start_ = kRetailSecondaryHeapBase;
    }
}

void RetailModelRenderer::transform_vertices(std::span<const std::uint8_t> packet,
                                             std::uint32_t count) {
    const RetailViewport& vp = g_.viewport;
    const bool backface_enabled = (render_flags_ & 0x40) != 0;
    const bool flag10 = (render_flags_ & 0x10) != 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t src = 0x1c + static_cast<std::size_t>(i) * 8;
        RetailVertexRecord& rec = heap_.at(i);
        const std::uint16_t src_flags = u16_at(packet, src + 6);
        rec[4] = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(src_flags)));
        if ((src_flags & 0x10) != 0) {
            // Two-point strip vertex: built from two already projected records.
            const RetailVertexRecord& first = resolve_vertex(u16_at(packet, src + 0) >> 3);
            const RetailVertexRecord& second = resolve_vertex(u16_at(packet, src + 2) >> 3);
            const double z = s16_at(packet, src + 4);
            const float dx = subf(as_float(second[0]), as_float(first[0]));
            const float ndy = -subf(as_float(second[1]), as_float(first[1]));
            const float length = static_cast<float>(
                std::sqrt(static_cast<double>(addf(mulf(ndy, ndy), mulf(dx, dx)))));
            const float recip = as_float(first[3]);
            const float term_x = mulf(mulf(divf(dx, length), z), recip);
            const float term_y = mulf(mulf(divf(ndy, length), z), recip);
            rec[0] = as_bits(addf(term_y, as_float(first[0])));
            rec[1] = as_bits(addf(term_x, as_float(first[1])));
            rec[2] = first[2];
            rec[3] = first[3];
            rec[6] = 0;
            const float x = as_float(rec[0]);
            const float y = as_float(rec[1]);
            const float depth = as_float(rec[2]);
            std::uint32_t clip = lateral_flags(vp, x, y);
            if (depth < near_depth_) clip |= 0x10;
            if ((render_flags_ & 0x10) == 0 && !(depth < yon_)) clip |= 0x20;
            rec[5] = (first[5] & 0x100U) | clip;
            continue;
        }
        if ((src_flags & 2) != 0) {
            const std::int32_t packed = static_cast<std::int32_t>(u32_at(packet, src + 0));
            rec[6] = kRetailSecondaryHeapBase +
                     static_cast<std::uint32_t>((packed >> 3) * static_cast<std::int32_t>(kRetailVertexRecordSize));
            continue;
        }
        const double x = s16_at(packet, src + 0);
        const double y = s16_at(packet, src + 2);
        const double z = s16_at(packet, src + 4);
        const auto row = [&](std::size_t base, float bias) {
            const float sum = addf(mulf(matrix_[base + 2], z), mulf(matrix_[base + 1], y));
            return addf(addf(sum, mulf(matrix_[base + 0], x)), bias);
        };
        const float px = row(0, translation_[0]);
        const float py = row(3, translation_[1]);
        const float pz = row(6, translation_[2]);
        bool back = false;
        if (backface_enabled) {
            const float lhs = addf(mulf(back_vector_[2], pz), mulf(back_vector_[0], px));
            const float dot = addf(lhs, mulf(back_vector_[1], py));
            const float bias = subf(g_.back_bias, static_cast<double>(g_.camera_value));
            back = addf(dot, bias) > g_.winding_threshold;
        }
        const float recip = divf(depth_scale_, pz);
        const float scaled_y = flag10 ? mulf(kYScale, py) : py;
        const float sx = addf(mulf(recip, px), center_x_);
        const float sy = addf(mulf(recip, scaled_y), center_y_);
        std::uint32_t clip = lateral_flags(vp, sx, sy);
        if (pz < near_depth_) clip |= 0x10;
        if (!flag10 && !(pz < yon_)) clip |= 0x20;
        if (back) clip |= 0x100;
        rec[0] = as_bits(sx);
        rec[1] = as_bits(sy);
        rec[2] = as_bits(pz);
        rec[3] = as_bits(recip);
        rec[5] = clip;
        rec[6] = 0;
        if ((src_flags & 1) != 0) {
            heap2_[heap2_cursor_] = rec;
            heap2_cursor_ += static_cast<std::uint32_t>(kRetailVertexRecordSize);
        }
    }
}

void RetailModelRenderer::submit_faces(const RetailSubmitInputs& inputs,
                                       std::span<const std::uint8_t> faces,
                                       std::uint32_t face_count) {
    model_pushback_ = g_.model_pushback;
    const std::uint32_t source = g_.selector_source;
    std::size_t offset = 0;
    for (std::uint32_t i = 0; i < face_count; ++i) {
        const auto face = faces.subspan(std::min(offset, faces.size()));
        selector_ = ((source >> 16) & u16_at(face, 0)) | (source & 0xffff);
        if ((render_flags_ & 8) != 0) {
            selector_ &= 0xfffffffeU;
        }
        if ((selector_ & 0xc0) != 0) {
            resolve_face_color(inputs, face);
            build_polygon(inputs, face);
        }
        const std::uint16_t length = u16_at(face, 2);
        if (length < 8) {
            throw RetailRenderError("face record with no length");
        }
        offset += length;
    }
}

namespace {

// (c >> 16 & 0xff) | (c & 0xff00) | ((c | 0xffffff00) << 16): the retail red/blue swap that also
// forces the top byte to 0xff (only the low byte of c survives the final shift).
[[nodiscard]] std::uint32_t swap_red_blue(std::uint32_t c) {
    return ((c >> 16) & 0xffU) | (c & 0xff00U) | ((c | 0xffffff00U) << 16);
}

// FUN_004d1cd0: per-channel modulate; byte 3 comes from the first operand.
[[nodiscard]] std::uint32_t modulate(std::uint32_t a, std::uint32_t b) {
    std::uint32_t out = a & 0xff000000U;
    for (unsigned shift : {0U, 8U, 16U}) {
        out |= ((((a >> shift) & 0xffU) * ((b >> shift) & 0xffU)) / 0xffU) << shift;
    }
    return out;
}

} // namespace

// M3D_ResolveFaceColor (0x004d1960): fills DAT_00580888..0x00580894.
void RetailModelRenderer::resolve_face_color(const RetailSubmitInputs& inputs,
                                             std::span<const std::uint8_t> face) {
    const std::uint32_t sel = selector_;
    const auto light = [&](std::uint32_t index) {
        return inputs.light_color ? inputs.light_color(index) : 0U;
    };
    const auto ramp = [&](std::uint32_t index) {
        return inputs.ramp_color ? inputs.ramp_color(index) : 0U;
    };
    if ((render_flags_ & 0x200) != 0) {
        std::uint32_t low = 0x80;
        std::uint32_t blue = 0;
        if (g_.model_color_word != 0) {
            blue = (g_.model_color_word >> 16) & 0xffU;
            low = g_.model_color_word;
            if (g_.model_color_word == 0x00800080U) {
                if ((sel & 0x2000) != 0) {
                    low = 0x8000;
                    blue = 0;
                }
                if ((sel & 0x4000) != 0) {
                    low = 0x80;
                    blue = 0;
                }
            }
        }
        const std::uint32_t c = (low & 0xff00U) | blue | ((low | 0xffffff00U) << 16);
        face_colors_ = {c, c, c, c};
        return;
    }
    const std::uint32_t word = u32_at(face, 8);
    if ((sel & 0x800) == 0) {
        const std::uint32_t c = (word & 0xff00U) | ((word >> 16) & 0xffU) | ((word | 0xffffff00U) << 16);
        face_colors_ = {c, c, c, c};
    } else {
        face_colors_[0] = swap_red_blue(ramp(word & 0xffU));
        face_colors_[1] = swap_red_blue(ramp((word >> 8) & 0xffU));
        face_colors_[2] = swap_red_blue(ramp((word >> 16) & 0xffU));
        if ((sel & 0x10) == 0) {
            face_colors_[3] = swap_red_blue(ramp(word >> 24));
        }
    }
    if ((render_flags_ & 0x10) != 0) {
        if (g_.current_level == 3) {
            for (auto& color : face_colors_) {
                for (unsigned shift : {0U, 8U, 16U}) {
                    const std::uint32_t channel = (color >> shift) & 0xffU;
                    color = (color & ~(0xffU << shift)) | (((channel * 0xffU) >> 7 & 0xffU) << shift);
                }
            }
        } else if (g_.current_level == 6) {
            face_colors_ = {0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU};
        }
    }
    if ((sel & 4) == 0) {
        if ((sel & 8) == 0) {
            return;
        }
        const std::uint32_t scene = swap_red_blue(g_.scene_color);
        for (auto& color : face_colors_) color = modulate(color, scene);
    } else if ((sel & 8) == 0) {
        const std::uint32_t lit = light(u16_at(face, 0xc) >> 3);
        for (auto& color : face_colors_) color = modulate(color, lit);
    } else {
        for (std::size_t i = 0; i < 4; ++i) {
            face_colors_[i] = modulate(face_colors_[i], light(byte_at(face, 4 + i)));
        }
    }
}

void RetailModelRenderer::build_polygon(const RetailSubmitInputs& inputs,
                                        std::span<const std::uint8_t> face) {
    const auto& colors = face_colors_;
    emit(RetailRenderEvent::Kind::build, 0, (render_flags_ & 8) != 0 ? grey_ : 0U);
    const bool textured = (selector_ & 1) != 0;
    std::uint32_t all_clip = 0;
    std::uint32_t any_clip = 0;
    std::uint32_t address = allocate_slot();
    RetailPolygonImage* image = &slot_ref(address);
    auto& im = *image;
    im[1] = (im[1] & 0x00ffffffU) | 0xb0000000U;
    im[2] = selector_;
    im[5] = (selector_ & 0x10) != 0 ? 3U : 4U;
    if ((render_flags_ & 0x400) != 0) im[2] |= 0x80000000U;
    if ((render_flags_ & 0x200) != 0) {
        const std::uint32_t before = im[2];
        im[2] = before & 0xcfffffffU;
        if ((g_.face_state_bits & 1) != 0) im[2] = (before & 0xcfffffffU) | 0x10000000U;
        if ((g_.face_state_bits & 2) != 0) im[2] |= 0x20000000U;
    }
    if (g_.software_renderer != 0 && (render_flags_ & 0x80) != 0) im[2] |= 0x40000000U;
    RetailTextureRecord texture;
    if (textured) {
        im[4] = u32_at(face, 0x10);
        texture = inputs.texture_lookup ? inputs.texture_lookup(im[4]) : RetailTextureRecord{};
    } else {
        im[4] = 0;
    }
    const std::uint32_t count = im[5];
    std::size_t word = 6;
    for (std::uint32_t corner = 0; corner < count; ++corner) {
        const std::size_t source = count == 4 ? kQuadOrder[corner] : corner;
        const RetailVertexRecord& record = resolve_vertex(byte_at(face, 4 + source));
        const std::uint32_t clip = record[5];
        all_clip = corner != 0 ? (all_clip & clip) : clip;
        any_clip |= clip;
        im[word + 0] = record[0];
        im[word + 1] = record[1];
        im[word + 2] = record[2];
        im[word + 3] = record[3];
        std::uint32_t color = colors[source];
        if ((im[2] & 0x1c0) == 0x140 && g_.software_renderer == 0) {
            std::uint32_t r = ((color >> 16) & 0xff) * 5;
            std::uint32_t gg = ((color >> 8) & 0xff) * 5;
            std::uint32_t b = (color & 0xff) * 5;
            r = std::min<std::uint32_t>(r, 0xff);
            gg = std::min<std::uint32_t>(gg, 0xff);
            b = std::min<std::uint32_t>(b, 0xff);
            color = ((((r | 0xffffff00U) << 8) | gg) << 8) | b;
        }
        if ((render_flags_ & 8) != 0) color = grey_;
        im[word + 4] = color;
        if (textured) {
            const float u = divf(addf(static_cast<double>(byte_at(face, 0x14 + source * 2)), kHalfTexel),
                                 static_cast<double>(texture.width));
            const float v = divf(addf(static_cast<double>(byte_at(face, 0x15 + source * 2)), kHalfTexel),
                                 static_cast<double>(texture.height));
            set_word_f(im, word + 5, u);
            set_word_f(im, word + 6, v);
            word += 7;
        } else {
            word += 5;
        }
    }
    if ((all_clip & 0x10) != 0) {
        rollback_slot();
        emit(RetailRenderEvent::Kind::build_end, 0);
        return;
    }
    if ((all_clip & 0x3f) != 0 && (any_clip & 0x10) == 0) {
        rollback_slot();
        emit(RetailRenderEvent::Kind::build_end, 0);
        return;
    }
    if (textured && im[5] == 4 && (g_.software_renderer == 0 || g_.software_renderer > 1)) {
        const RetailPolygonImage original = im;
        for (std::size_t i = 0; i < 7; ++i) im[0x14 + i] = im[0x1b + i];
        im[5] = 3;
        clip_and_bucket(address, face, inputs, any_clip);
        address = allocate_slot();
        auto& second = slot_ref(address);
        second = original;
        for (std::size_t i = 0; i < 7; ++i) second[6 + i] = second[0x1b + i];
        second[5] = 3;
        clip_and_bucket(address, face, inputs, any_clip);
    } else {
        clip_and_bucket(address, face, inputs, any_clip);
    }
    emit(RetailRenderEvent::Kind::build_end, 0);
}

void RetailModelRenderer::clip_and_bucket(std::uint32_t poly, std::span<const std::uint8_t> face,
                                          const RetailSubmitInputs& inputs,
                                          std::uint32_t clip_flags) {
    emit(RetailRenderEvent::Kind::clip_bucket, poly, clip_flags & 0xff);
    auto finish = [&] { emit(RetailRenderEvent::Kind::clip_bucket_end, poly); };
    if ((clip_flags & 0x10) != 0) {
        emit(RetailRenderEvent::Kind::near_clip, poly);
        const bool kept = clip_near_plane(poly);
        emit(RetailRenderEvent::Kind::near_clip_end, poly, kept ? poly : 0U);
        if (!kept) {
            rollback_slot();
            finish();
            return;
        }
    }
    auto& im = slot_ref(poly);
    const std::uint32_t count = im[5];
    const bool textured = im[4] != 0;
    const std::size_t stride = textured ? 7 : 5;
    const auto x_of = [&](std::size_t word) { return word_f(im, word); };
    const std::size_t v0 = 6;
    const std::size_t v1 = 6 + stride;
    const std::size_t v2 = 6 + 2 * stride;
    const std::size_t v3 = 6 + 3 * stride;
    const std::size_t last = textured ? count * 7 - 1 : count * 5 + 1;
    const float a = subf(x_of(v1), x_of(v0));
    const float b = subf(x_of(v1 + 1), x_of(v0 + 1));
    const float c = subf(x_of(last), x_of(v0));
    const float d = subf(x_of(last + 1), x_of(v0 + 1));
    const float w0 = subf(mulf(d, a), mulf(c, b));
    float w1 = 0.0F;
    if (count > 3) {
        const float e = subf(x_of(v1), x_of(v2));
        const float f = subf(x_of(v1 + 1), x_of(v2 + 1));
        const float h = subf(x_of(v3), x_of(v2));
        const float k = subf(x_of(v3 + 1), x_of(v2 + 1));
        w1 = subf(mulf(h, f), mulf(k, e));
    }
    const float threshold = g_.winding_threshold;
    bool reject = w0 <= threshold;
    if (count < 4 || ((w1 <= threshold) == reject)) {
        if (g_.reverse_winding != 0) {
            reject = threshold < w0;
        }
        if (reject) {
            rollback_slot();
            finish();
            return;
        }
    }
    float offset = 0.0F;
    switch (selector_ & 0x6000) {
    case 0x2000: offset = static_cast<float>(g_.depth_offset_2000); break;
    case 0x4000: offset = static_cast<float>(g_.depth_offset_4000); break;
    case 0x6000: offset = static_cast<float>(g_.depth_offset_6000); break;
    default: break;
    }
    const std::uint32_t bucket = classify_depth(poly, face, inputs, offset);
    if (g_.current_level != 6 && g_.current_level != 10 && im[4] != 0 &&
        (im[2] & 0x1c0) == 0x40) {
        const RetailTextureRecord record =
            inputs.texture_lookup ? inputs.texture_lookup(im[4]) : RetailTextureRecord{};
        if ((record.flags & 0x110) == 0) {
            im[2] = (im[2] & 0xffffffbfU) | 0x80U;
        }
    }
    im[0] = bucket_head(bucket);
    bucket_heads_[bucket] = poly;
    finish();
}

bool RetailModelRenderer::clip_near_plane(std::uint32_t poly) {
    auto& out = slot_ref(poly);
    RetailPolygonImage local = out;
    const bool textured = local[4] != 0;
    const std::size_t stride = textured ? 7 : 5;
    const std::int32_t count = static_cast<std::int32_t>(local[5]);
    const float near_depth = near_depth_;
    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t base = 6 + static_cast<std::size_t>(i) * stride;
        const float factor = divf(kUnitScale, word_f(local, base + 3));
        set_word_f(local, base + 0, mulf(subf(word_f(local, base + 0), center_x_), factor));
        set_word_f(local, base + 1, mulf(subf(word_f(local, base + 1), center_y_), factor));
        set_word_f(local, base + 3, mulf(depth_scale_, factor));
    }
    std::int32_t produced = 0;
    std::size_t dest = 6;
    const auto interpolate = [&](std::size_t dst, std::size_t cur, std::size_t next) {
        set_word_f(out, dst + 3, near_depth);
        const float t = divf(subf(near_depth, word_f(local, cur + 3)),
                             subf(word_f(local, next + 3), word_f(local, cur + 3)));
        set_word_f(out, dst + 0,
                   addf(mulf(subf(word_f(local, next + 0), word_f(local, cur + 0)), t),
                        word_f(local, cur + 0)));
        set_word_f(out, dst + 1,
                   addf(mulf(subf(word_f(local, next + 1), word_f(local, cur + 1)), t),
                        word_f(local, cur + 1)));
        std::uint32_t color = out[dst + 4];
        for (unsigned shift : {16U, 8U, 0U}) {
            const std::uint32_t from = (local[cur + 4] >> shift) & 0xff;
            const std::uint32_t to = (local[next + 4] >> shift) & 0xff;
            const std::int32_t delta = ftol(mulf(static_cast<double>(to) - static_cast<double>(from), t));
            const std::uint32_t value = (static_cast<std::uint32_t>(delta) + from) & 0xff;
            color = (color & ~(0xffU << shift)) | (value << shift);
        }
        out[dst + 4] = color;
        if (textured) {
            set_word_f(out, dst + 5,
                       addf(mulf(subf(word_f(local, next + 5), word_f(local, cur + 5)), t),
                            word_f(local, cur + 5)));
            set_word_f(out, dst + 6,
                       addf(mulf(subf(word_f(local, next + 6), word_f(local, cur + 6)), t),
                            word_f(local, cur + 6)));
        }
    };
    const auto copy_vertex = [&](std::size_t dst, std::size_t src) {
        for (std::size_t w = 0; w < stride; ++w) out[dst + w] = local[src + w];
    };
    for (std::int32_t i = 0; i < count; ++i) {
        const std::size_t cur = 6 + static_cast<std::size_t>(i) * stride;
        const std::size_t next = (i == count - 1) ? 6 : cur + stride;
        const bool cur_outside = word_f(local, cur + 3) < near_depth;
        const bool next_outside = word_f(local, next + 3) < near_depth;
        if (!cur_outside) {
            copy_vertex(dest, cur);
            dest += stride;
            ++produced;
            if (next_outside) {
                interpolate(dest, cur, next);
                dest += stride;
                ++produced;
            }
        } else if (!next_outside) {
            interpolate(dest, cur, next);
            dest += stride;
            ++produced;
        }
    }
    std::uint32_t final_count = 0;
    if (produced >= 3) {
        std::uint32_t all = 0;
        for (std::int32_t i = 0; i < produced; ++i) {
            const std::size_t base = 6 + static_cast<std::size_t>(i) * stride;
            const float depth = word_f(out, base + 3);
            out[base + 2] = as_bits(depth);
            const float recip = divf(depth_scale_, depth);
            set_word_f(out, base + 3, recip);
            set_word_f(out, base + 0, addf(mulf(recip, word_f(out, base + 0)), center_x_));
            set_word_f(out, base + 1, addf(mulf(word_f(out, base + 1), recip), center_y_));
            const std::uint32_t flags =
                lateral_flags(g_.viewport, word_f(out, base + 0), word_f(out, base + 1));
            all = i == 0 ? flags : (all & flags);
        }
        final_count = all != 0 ? 0U : static_cast<std::uint32_t>(produced);
    }
    out[5] = final_count;
    return final_count >= 3 && final_count <= 6;
}

std::uint32_t RetailModelRenderer::classify_depth(std::uint32_t poly,
                                                  std::span<const std::uint8_t> face,
                                                  const RetailSubmitInputs& inputs, float offset) {
    auto& im = slot_ref(poly);
    emit(RetailRenderEvent::Kind::classify, poly, as_bits(offset), 0);
    const bool textured_selector = (selector_ & 1) != 0;
    const auto texture_flags = [&] {
        return inputs.texture_lookup ? inputs.texture_lookup(u32_at(face, 0x10)).flags : 0U;
    };
    bool texture_mode;
    if (!textured_selector) {
        texture_mode = ((selector_ >> 6) & 1) != 0;
    } else if ((selector_ & 0x40) == 0 && (texture_flags() & 0x10) != 0) {
        texture_mode = false;
    } else {
        texture_mode = true;
    }
    bool back = ((render_flags_ >> 5) & 1) != 0;
    const std::int32_t count = static_cast<std::int32_t>(im[5]);
    if ((render_flags_ & 0x40) != 0) {
        bool any_back = false;
        for (std::int32_t k = 0; k < count; ++k) {
            if ((resolve_vertex(byte_at(face, 4 + static_cast<std::size_t>(k)))[5] & 0x100U) != 0) {
                any_back = true;
            }
        }
        if (count <= 0 || !any_back) {
            back = true;
        }
    }
    std::uint32_t mode = texture_mode ? 1 : 0;
    if ((render_flags_ & 0x88) != 0) mode = 2;
    if (back) mode = 3;
    if ((render_flags_ & 0x10) != 0) {
        mode = 4;
        im[2] |= 0x80000000U;
    }
    const std::size_t stride = textured_selector ? 7 : 5;
    float selected = 0.0F;  // parameter 4 doubles as the running depth
    if (g_.software_renderer != 0) {
        for (std::int32_t k = 0; k < count; ++k) {
            const float depth = word_f(im, 6 + static_cast<std::size_t>(k) * stride + 2);
            if (k == 0 || selected < depth) selected = depth;
        }
        if (mode == 3) {
            selected = addf(addf(selected, g_.z_pushback), offset);
        } else if (mode == 4) {
            selected = addf(kForcedDepth, offset);
        } else {
            selected = addf(selected, offset);
        }
    } else {
        bool nearest = false;
        float scale = kUnitScale;
        float adjust = offset;
        if (g_.texture_mode_bits == 0 && g_.current_level != 0xd) {
            adjust = (render_flags_ & 0x800) != 0 ? static_cast<float>(model_pushback_) : 0.0F;
        }
        switch (mode) {
        case 1:
            scale = kModeOneScale;
            if (g_.texture_mode_bits == 0 && model_pushback_ == 0) nearest = true;
            break;
        case 2:
            emit(RetailRenderEvent::Kind::video, 0, inputs.video_playing ? 1U : 0U);
            scale = inputs.video_playing ? kModeOneScale : kModeTwoScale;
            break;
        case 3:
            adjust = addf(g_.z_pushback, adjust);
            break;
        case 4:
            adjust = kForcedDepth;
            break;
        default:
            break;
        }
        if (g_.texture_mode_bits != 0) nearest = false;
        for (std::int32_t k = 0; k < count; ++k) {
            const std::size_t z = 6 + static_cast<std::size_t>(k) * stride + 2;
            const float depth = mulf(addf(adjust, word_f(im, z)), scale);
            set_word_f(im, z, depth);
            if (nearest) {
                if (k == 0 || depth < selected) selected = depth;
            } else if (k == 0 || selected < depth) {
                selected = depth;
            }
            float clamped = depth;
            if (clamped < near_depth_) clamped = near_depth_;
            float reciprocal = divf(near_depth_, clamped);
            if (kReciprocalCap < reciprocal) reciprocal = kReciprocalCap;
            set_word_f(im, z, reciprocal);
        }
    }
    std::int32_t bucket = ftol(addf(selected, kHalfTexel));
    if (bucket < 0) bucket = 0;
    bucket >>= 2;
    if (bucket >= 0x1000) bucket = 0xfff;
    emit(RetailRenderEvent::Kind::classify_end, poly, static_cast<std::uint32_t>(bucket),
         bucket_head(static_cast<std::uint32_t>(bucket)));
    return static_cast<std::uint32_t>(bucket);
}

RetailOrderingTable::RetailOrderingTable(std::uint32_t base_address, std::size_t bucket_count)
    : base_(base_address), links_(bucket_count + kBucketEntryOffset, 0U) {
    clear();
}

void RetailOrderingTable::clear() {
    std::fill(links_.begin(), links_.end(), 0U);
    for (std::size_t i = 1; i < links_.size(); ++i) {
        links_[i] = entry_address(i - 1);
    }
    if (!links_.empty()) {
        links_[0] = 0;
    }
}

std::uint32_t RetailOrderingTable::entry_address(std::size_t entry) const {
    return base_ + static_cast<std::uint32_t>(entry * 8);
}

bool RetailOrderingTable::is_entry(std::uint32_t address) const noexcept {
    return address >= base_ && address < base_ + links_.size() * 8 && (address - base_) % 8 == 0;
}

std::uint32_t RetailOrderingTable::prepend(std::size_t bucket, std::uint32_t polygon) {
    const std::uint32_t previous = links_.at(bucket + kBucketEntryOffset);
    links_[bucket + kBucketEntryOffset] = polygon;
    return previous;
}

std::vector<std::uint32_t> RetailOrderingTable::draw_order(
    const std::function<std::uint32_t(std::uint32_t)>& polygon_next) const {
    std::vector<std::uint32_t> order;
    if (links_.empty()) {
        return order;
    }
    std::uint32_t node = entry_address(links_.size() - 1);
    while (node != 0) {
        if (is_entry(node)) {
            node = links_[(node - base_) / 8];
        } else {
            order.push_back(node);
            node = polygon_next(node);
        }
    }
    return order;
}

} // namespace opentony::trg
