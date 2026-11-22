#pragma once

// Retail-faithful reconstruction of the model submission chain (Phase 4.6):
//
//   0x004d14d0  M3D_SubmitModelState     matrices, viewport constants, hand-off
//   0x004d29e0  M3D_TransformVertices    0x1c-byte working records at 0x00570878
//   0x004d18b0  M3D_SubmitFacePolygons   per-face selector, face walk
//   0x004d1d40  M3D_BuildD3DPolygon      0xc0-byte arena slot per attempt
//   0x004d20f0  M3D_ClipAndBucketPolygon near clip, winding, bucket link
//   0x004d2310  M3D_ClipPolygonNearPlane + 0x004d25c0 edge interpolation
//   0x004d26b0  M3D_ClassifyPolygonDepth depth adjustment and bucket index
//
// The retail code runs with the x87 precision control set to 24 bits
// (_controlfp(0x20000, 0x30000) at 0x004d158a), so every arithmetic result is
// rounded to binary32. The implementation rounds after each operation and keeps
// the retail operand order (the decompiler's expression order is not the
// machine's). Polygons are kept as the raw 0x30-word arena slot images so they
// can be compared word for word with a retail capture.
//
// ResolveFaceColor (0x004d1960, lighting) is a caller-owned input: the four
// resolved corner colors are supplied per face. Everything else the chain reads
// is in RetailRenderGlobals.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace opentony::trg {

class RetailRenderError final : public std::runtime_error {
public:
    explicit RetailRenderError(const char* message)
        : std::runtime_error(message) {}
};

inline constexpr std::uint32_t kRetailVertexHeapBase = 0x00570878;
inline constexpr std::uint32_t kRetailSecondaryHeapBase = 0x0058f328;
inline constexpr std::size_t kRetailVertexRecordSize = 0x1c;
inline constexpr std::size_t kRetailVertexHeapCapacity = 256;
inline constexpr std::size_t kRetailPolygonSlotWords = 0x30;
inline constexpr std::uint32_t kRetailPolygonSlotSize = 0xc0;
inline constexpr std::size_t kRetailBucketCount = 0x1000;

// One arena slot: word 0 is the bucket link, byte 7 the 0xb0 format, word 2 the
// state flags, word 4 the material pointer, word 5 the vertex count and the
// vertex stream starts at word 6 (five words solid, seven textured).
using RetailPolygonImage = std::array<std::uint32_t, kRetailPolygonSlotWords>;

// Seven-word record at 0x00570878 + 0x1c * index.
using RetailVertexRecord = std::array<std::uint32_t, 7>;

// The viewport record RenderViewportEdges (0x00563a38) points at; every field
// is an unsigned 16-bit value.
struct RetailViewport {
    std::uint16_t right{};
    std::uint16_t bottom{};
    std::uint16_t left{};
    std::uint16_t top{};
    std::uint16_t near_depth{};  // +0x08
    std::uint16_t yon_depth{};   // +0x0a
    std::uint16_t depth_scale{}; // +0x0e
    std::uint16_t center_x{};    // +0x10
    std::uint16_t center_y{};    // +0x12
};

// Runtime material/texture record reached through polygon word 4: flags at
// +0x10, width and height as signed 16-bit values at +0x14 and +0x16.
struct RetailTextureRecord {
    std::uint32_t flags{};
    std::int16_t width{1};
    std::int16_t height{1};
};

struct RetailRenderGlobals {
    RetailViewport viewport;
    float fog_yon_scale{1000.0F};                       // 0x00549d98
    float winding_threshold{0.0F};                      // 0x00518484

    std::array<std::int16_t, 9> matrix_a{};             // 0x00563a18
    std::array<std::int32_t, 3> translation_a{};        // 0x00563af8
    std::array<std::int16_t, 9> matrix_b{};             // 0x00563b10
    std::array<std::int32_t, 3> translation_b{};        // 0x00563b24
    std::array<std::int16_t, 9> matrix_c{};             // 0x00564340
    std::array<std::int16_t, 3> offset_c{};             // 0x00564352
    std::array<std::int16_t, 9> camera_matrix{};        // DAT_005620e0 + 0x74
    std::int32_t camera_value{};                        // DAT_005620e0 + 0x08
    std::array<float, 3> back_vector{};                 // 0x0057e878 (only read before branch B rewrites it)
    float back_bias{};                                  // 0x00562098
    float z_pushback{};                                 // SP_ZPushback 0x005620a8
    float grey_source{};                                // 0x00563a5c
    std::int16_t depth_offset_2000{};                   // 0x00563a64
    std::int16_t depth_offset_4000{};                   // 0x00563a7c
    std::int16_t depth_offset_6000{};                   // 0x00563a88
    std::uint16_t reverse_winding{};                    // 0x00563b08
    std::uint32_t scene_color{};                        // DAT_00563a74
    std::uint32_t model_color_word{};                   // DAT_00564384
    std::uint32_t selector_source{};                    // DAT_00564358
    std::uint32_t face_state_bits{};                    // DAT_0056437c
    std::uint32_t current_level{};                      // 0x0056a898
    std::uint32_t software_renderer{};                  // D3DSoftwareRenderer
    std::uint32_t texture_mode_bits{};                  // D3DTextureMode (compared as an integer)
    std::int16_t model_pushback{};                      // modelPushbacks[level * 2000 + model_id]
    std::uint32_t secondary_heap_cursor{kRetailSecondaryHeapBase}; // DAT_0059d330
};

struct RetailRenderEvent {
    enum class Kind : std::uint8_t {
        build,
        build_end,
        clip_bucket,
        clip_bucket_end,
        near_clip,
        near_clip_end,
        classify,
        classify_end,
        video,
    };

    Kind kind{};
    std::uint32_t poly{};     // slot address (virtual, from the arena cursor)
    std::uint32_t cursor{};   // RenderPolygonArenaCursor at the event
    std::uint32_t a{};        // clip_bucket: clip flags; near_clip_end: return;
                              // classify: parameter 3 bits; classify_end: bucket;
                              // video: result
    std::uint32_t b{};        // classify: parameter 4 bits; classify_end: bucket head
                              // before the store
    std::uint32_t selector{}; // RenderDepthModeSelector
    RetailPolygonImage image{};
    std::array<std::uint32_t, 4> colors{}; // build: DAT_00580888.. as M3D_ResolveFaceColor left them
};

using RetailRenderSink = std::function<void(const RetailRenderEvent&)>;

struct RetailSubmitInputs {
    std::span<const std::uint8_t> packet;
    std::uint32_t flags{};
    // M3D_ResolveFaceColor's table reads: the per-vertex light array at
    // 0x0057e888 (indexed by a face vertex byte or by the u16 at face + 0x0c
    // shifted right by three) and the color ramp DAT_00563a84 points to.
    std::function<std::uint32_t(std::uint32_t)> light_color;
    std::function<std::uint32_t(std::uint32_t)> ramp_color;
    // Runtime material record lookup for textured faces.
    std::function<RetailTextureRecord(std::uint32_t material)> texture_lookup;
    // Video_Playing() as seen by M3D_ClassifyPolygonDepth's mode 2.
    bool video_playing{};
};

class RetailModelRenderer final {
public:
    // `arena_cursor`/`arena_end` are the retail RenderPolygonArenaCursor and
    // RenderPolygonArenaEnd values. `slot_seed` supplies the prior content of a
    // slot the first time it is touched (arena memory is reused between frames;
    // the stale words are observable, e.g. the unwritten color byte that the
    // edge interpolator leaves in place).
    RetailModelRenderer(const RetailRenderGlobals& globals,
                        std::uint32_t arena_cursor, std::uint32_t arena_end,
                        std::function<RetailPolygonImage(std::uint32_t)> slot_seed = {});

    void set_sink(RetailRenderSink sink);
    void set_bucket_head(std::uint32_t bucket, std::uint32_t head);

    // M3D_SubmitModelState. Returns false when the retail routine leaves early
    // (state bit 0x10 with the software renderer below level 2).
    bool submit(const RetailSubmitInputs& inputs);

    [[nodiscard]] std::uint32_t arena_cursor() const noexcept { return cursor_; }
    [[nodiscard]] const RetailPolygonImage& slot(std::uint32_t address);
    [[nodiscard]] const std::vector<RetailVertexRecord>& vertex_heap() const noexcept { return heap_; }
    // Records a previous submit left in the secondary heap (0x0058f328.. up to
    // DAT_0059d330 at entry); vertices with source flag 2 point into them.
    void seed_secondary_heap(std::span<const RetailVertexRecord> records);
    // Address where this submit began appending to the secondary heap, and the
    // records it appended (source-flag 1 copies) up to the cursor.
    [[nodiscard]] std::uint32_t secondary_heap_start() const noexcept { return heap2_start_; }
    [[nodiscard]] std::vector<RetailVertexRecord> secondary_heap() const;
    [[nodiscard]] std::uint32_t bucket_head(std::uint32_t bucket) const;
    [[nodiscard]] std::uint32_t grey_color() const noexcept { return grey_; }

private:
    void prepare_matrices(std::uint32_t flags);
    void transform_vertices(std::span<const std::uint8_t> packet, std::uint32_t count);
    void submit_faces(const RetailSubmitInputs& inputs, std::span<const std::uint8_t> faces,
                      std::uint32_t face_count);
    void resolve_face_color(const RetailSubmitInputs& inputs, std::span<const std::uint8_t> face);
    void build_polygon(const RetailSubmitInputs& inputs, std::span<const std::uint8_t> face);
    void clip_and_bucket(std::uint32_t poly, std::span<const std::uint8_t> face,
                         const RetailSubmitInputs& inputs, std::uint32_t clip_flags);
    bool clip_near_plane(std::uint32_t poly);
    std::uint32_t classify_depth(std::uint32_t poly, std::span<const std::uint8_t> face,
                                 const RetailSubmitInputs& inputs, float offset);

    [[nodiscard]] const RetailVertexRecord& resolve_vertex(std::uint32_t index) const;
    [[nodiscard]] const RetailVertexRecord& vertex_at(std::uint32_t address) const;
    RetailPolygonImage& slot_ref(std::uint32_t address);
    std::uint32_t allocate_slot();
    void rollback_slot();
    void emit(RetailRenderEvent::Kind kind, std::uint32_t poly, std::uint32_t a = 0,
              std::uint32_t b = 0);

    RetailRenderGlobals g_;
    std::uint32_t render_flags_{};
    std::uint32_t selector_{};
    std::uint32_t cursor_{};
    std::uint32_t end_{};
    std::uint32_t grey_{};
    std::uint32_t heap2_cursor_{};
    std::uint32_t heap2_start_{};
    std::int16_t model_pushback_{};

    // 0x0058089c.. derived at 0x004d14d0
    float near_depth_{};
    float depth_scale_{};
    float center_x_{};
    float center_y_{};
    float yon_{};

    std::array<float, 9> matrix_{};       // 0x0056e84c
    std::array<float, 3> translation_{};  // 0x0058f318
    std::array<float, 3> back_vector_{};  // 0x0057e878

    std::vector<RetailVertexRecord> heap_;
    std::map<std::uint32_t, RetailVertexRecord> heap2_;
    std::map<std::uint32_t, RetailPolygonImage> slots_;
    std::unordered_map<std::uint32_t, std::uint32_t> bucket_heads_;
    std::function<RetailPolygonImage(std::uint32_t)> slot_seed_;
    RetailRenderSink sink_;
    std::array<std::uint32_t, 4> face_colors_{};  // DAT_00580888.. (persists between faces)
};

// The retail ordering table: CurrentOTSize (4098) 8-byte entries. RenderBucketHeads
// (0x0056433c) points 0x10 bytes into it, so bucket b is entry b + 2 and entries 0
// and 1 sit below bucket 0. FUN_004d3b00 (the PC port of ClearOTagR, run by
// Db_WaitRender after every frame) zeroes the table and then links entry i to entry
// i-1 and entry 0 to null. M3D_ClipAndBucketPolygon prepends a polygon after its
// bucket's entry (poly.next = entry.word0; entry.word0 = poly), so a polygon chain
// always ends in the previous entry. Db_DisplayDraw/Db_Vblank hand
// D3D_DrawPolygonList the address of the LAST entry, which therefore walks bucket
// 0xfff down to 0 (then entries 1 and 0) and, inside a bucket, most-recently-added
// first; nodes whose word 1 is zero (the entries) draw nothing. A larger bucket is a
// larger depth, so this is far to near.
class RetailOrderingTable final {
public:
    inline static constexpr std::size_t kBucketEntryOffset = 2;

    // `base_address` is the table start; there are bucket_count + 2 entries.
    RetailOrderingTable(std::uint32_t base_address, std::size_t bucket_count);

    void clear();
    [[nodiscard]] std::size_t size() const noexcept { return links_.size(); }
    [[nodiscard]] std::uint32_t entry_address(std::size_t entry) const;
    [[nodiscard]] std::uint32_t head(std::size_t bucket) const {
        return links_.at(bucket + kBucketEntryOffset);
    }
    [[nodiscard]] std::uint32_t raw_entry(std::size_t entry) const { return links_.at(entry); }

    // Returns the value for polygon word 0.
    std::uint32_t prepend(std::size_t bucket, std::uint32_t polygon);

    // D3D_DrawPolygonList's walk. `polygon_next` returns word 0 of a polygon
    // (only called for polygon addresses). The result lists polygon addresses
    // in draw order.
    [[nodiscard]] std::vector<std::uint32_t> draw_order(
        const std::function<std::uint32_t(std::uint32_t)>& polygon_next) const;

private:
    [[nodiscard]] bool is_entry(std::uint32_t address) const noexcept;

    std::uint32_t base_{};
    std::vector<std::uint32_t> links_;
};

} // namespace opentony::trg
