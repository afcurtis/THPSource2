#include "auto_views.hpp"
#include "create_stitch.hpp"

#include <vector>

namespace opentony::runtime::create {

using namespace opentony::runtime::autoviews;

namespace {

std::uint32_t vertex(std::uint32_t model, std::uint32_t i) { return model + kModelVertices + i * kVertexStride; }

}  // namespace

std::int32_t CreateStitch::count_stitch_vertices(std::uint32_t model) {
    s_.check(model != 0, kMsgNoModelToCount);
    std::int32_t count = 0;
    for (std::uint32_t i = 0, n = half_at(m_, model + kModelVertexCount); i < n; ++i) {
        if ((byte_at(m_, vertex(model, i) + kVertexFlags) & 1) != 0) {
            ++count;
        }
    }
    return count;
}

std::uint32_t CreateStitch::model_size(std::uint32_t model) {
    s_.check(model != 0, kMsgNoModelSpecified);
    std::uint32_t at = model + kModelVertices +
                       (static_cast<std::uint32_t>(half_at(m_, model + kModelNormalCount)) + half_at(m_, model + kModelVertexCount)) * kVertexStride;
    for (std::uint32_t faces = half_at(m_, model + kModelFaceCount); faces != 0; --faces) {
        at += (word_at(m_, at) >> 18) * 4;
    }
    return at - model;
}

std::int32_t CreateStitch::map_lookup(std::int32_t source, std::uint32_t map) {
    s_.check(map != 0, kMsgNoMapping);
    for (std::uint32_t i = 0; i < kMapEntries; ++i) {
        if (int_at(m_, map + 8 * i) == source) {
            return int_at(m_, map + 8 * i + 4);
        }
    }
    return -1;
}

std::int32_t CreateStitch::map_reverse(std::int32_t build, std::uint32_t map) {
    for (std::uint32_t i = 0; i < kMapEntries; ++i) {
        if (int_at(m_, map + 8 * i + 4) == build) {
            return int_at(m_, map + 8 * i);
        }
    }
    return -1;
}

bool CreateStitch::map_has(std::int32_t source, std::uint32_t map) { return map_lookup(source, map) != -1; }

std::vector<std::int32_t> CreateStitch::stitch_log(std::uint32_t region) {
    std::vector<std::int32_t> log;
    std::int32_t total = 0;
    for (std::uint32_t i = 0, n = half_at(m_, region + kRegionPartCount); i < n; ++i) {
        const std::uint32_t model = word_at(m_, word_at(m_, region + kRegionModelTable) + 4 * i);
        s_.check(model != 0, kMsgNoModel);
        log.push_back(total);
        total += count_stitch_vertices(model);
    }
    return log;
}

void CreateStitch::log_stitches(std::uint32_t region, std::uint32_t log) {
    s_.check(region != 0, kMsgNoRegionToLog);
    s_.check(log != 0, kMsgNoLogBuffer);
    const std::vector<std::int32_t> values = stitch_log(region);
    for (std::size_t i = 0; i < values.size(); ++i) {
        set_word_at(m_, log + 4 * static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(values[i]));
    }
}

void CreateStitch::copy_models(std::uint32_t source, std::uint32_t build, std::uint32_t map) {
    s_.check(source != 0, kMsgNoSourceRegionCopy);
    s_.check(build != 0, kMsgNoBuildRegionCopy);
    s_.check(map != 0, kMsgNoMapping);
    set_half_at(m_, build + kRegionPartCount, 0);
    for (std::uint32_t slot = 0; slot < kStitchModelSlots; ++slot) {
        const std::uint32_t entry = word_at(m_, build + kRegionModelTable) + 4 * slot;
        if (word_at(m_, entry) != 0) {
            s_.note(kMemDelete, {word_at(m_, entry)});
            set_word_at(m_, word_at(m_, build + kRegionModelTable) + 4 * slot, 0);
        }
    }
    for (std::uint32_t p = 0; p < half_at(m_, source + kRegionPartCount); ++p) {
        const std::uint32_t model = word_at(m_, word_at(m_, source + kRegionModelTable) + 4 * p);
        s_.check(model != 0, kMsgNoModel);
        if ((byte_at(m_, model) & 0x20) == 0) {
            const std::int32_t target = map_lookup(static_cast<std::int32_t>(p), map);
            const std::uint32_t size = model_size(model);
            const std::uint32_t slot = word_at(m_, build + kRegionModelTable) + 4u * static_cast<std::uint32_t>(target);
            s_.check(word_at(m_, slot) == 0, kMsgModelLeak);
            s_.note(kAlloc, {size, 0, 1, 0});
            const auto block = static_cast<std::uint32_t>(s_.value(kAlloc));
            set_word_at(m_, word_at(m_, build + kRegionModelTable) + 4u * static_cast<std::uint32_t>(target), block);
            m_.copy(word_at(m_, word_at(m_, build + kRegionModelTable) + 4u * static_cast<std::uint32_t>(target)), model, size);
            set_half_at(m_, build + kRegionPartCount, half_at(m_, build + kRegionPartCount) + 1u);
        }
    }
    s_.check(half_at(m_, build + kRegionPartCount) == 0x13, kMsgPartCount);
}

void CreateStitch::reorder_stitches(std::uint32_t source, std::uint32_t build, std::uint32_t source_log, std::uint32_t map) {
    s_.check(build != 0, kMsgNoRegionToReorder);
    s_.check(source != 0, kMsgNoRegionToRead);
    s_.check(source_log != 0, kMsgNoSourceLog);
    // the running stitch count of the build region's own parts: retail keeps it in a local array
    const std::vector<std::int32_t> build_log = stitch_log(build);
    for (std::uint32_t p = 0; p < half_at(m_, build + kRegionPartCount); ++p) {
        const std::uint32_t model = word_at(m_, word_at(m_, build + kRegionModelTable) + 4 * p);
        s_.check(model != 0, kMsgNoModel);
        const std::uint32_t count = half_at(m_, model + kModelVertexCount);
        for (std::uint32_t v = 0; v < count; ++v) {
            const std::uint32_t vtx = vertex(model, v);
            const std::uint32_t normal = vtx + count * kVertexStride;
            if ((byte_at(m_, vtx + kVertexFlags) & 2) == 0) {
                continue;
            }
            const std::int32_t stitch = short_at(m_, vtx) >> 3;
            // the source part whose stitch numbers the vertex falls in
            std::int32_t part = 0;
            for (std::int32_t i = 0; i < static_cast<std::int32_t>(half_at(m_, source + kRegionPartCount)); ++i) {
                if (map_has(part, map) && stitch < int_at(m_, source_log + 4u * static_cast<std::uint32_t>(i))) {
                    break;
                }
                part = i;
            }
            const std::int32_t target = map_lookup(part, map);
            s_.check(target != -1, kMsgBadGhost);
            const auto shift = static_cast<std::uint16_t>(
                (build_log[static_cast<std::size_t>(target)] - int_at(m_, source_log + 4u * static_cast<std::uint32_t>(part))) * 8);
            set_half_at(m_, vtx, static_cast<std::uint16_t>(half_at(m_, vtx) + shift));
            set_half_at(m_, normal, static_cast<std::uint16_t>(half_at(m_, normal) + shift));
        }
    }
}

void CreateStitch::add_stitch(std::uint32_t region, std::uint32_t stitch_vertex, std::uint32_t ghost_vertex,
                              std::uint32_t stitch_normal, std::uint32_t ghost_normal) {
    s_.check(region != 0, kMsgNoBuildRegion);
    s_.check(stitch_vertex != 0, kMsgNoStitchVertex);
    s_.check(ghost_vertex != 0, kMsgNoGhostVertex);
    s_.check(stitch_normal != 0, kMsgNoStitchNormal);
    s_.check(ghost_normal != 0, kMsgNoGhostNormal);
    std::int32_t count = 0;   // stitch vertices before the new one
    bool seen = false;
    for (std::uint32_t p = 0; p < half_at(m_, region + kRegionPartCount); ++p) {
        const std::uint32_t model = word_at(m_, word_at(m_, region + kRegionModelTable) + 4 * p);
        const std::uint32_t n = half_at(m_, model + kModelVertexCount);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t vtx = vertex(model, i);
            const std::uint32_t normal = vtx + n * kVertexStride;
            if (vtx == stitch_vertex) {
                seen = true;
            }
            const std::uint16_t flags = half_at(m_, vtx + kVertexFlags);
            if ((flags & 1) != 0) {
                if (!seen) {
                    ++count;
                }
            } else if ((flags & 2) != 0 && (short_at(m_, vtx) >> 3) >= count && (byte_at(m_, stitch_vertex + kVertexFlags) & 1) == 0) {
                set_half_at(m_, vtx, static_cast<std::uint16_t>(half_at(m_, vtx) + 8));
                set_half_at(m_, normal, static_cast<std::uint16_t>(half_at(m_, normal) + 8));
            }
        }
    }
    const auto number = static_cast<std::uint16_t>(count << 3);
    set_half_at(m_, stitch_vertex + kVertexFlags, half_at(m_, stitch_vertex + kVertexFlags) | 1u);
    set_half_at(m_, ghost_vertex + kVertexFlags, half_at(m_, ghost_vertex + kVertexFlags) | 2u);
    set_half_at(m_, ghost_vertex + 2, 0);
    set_half_at(m_, ghost_vertex, number);
    set_half_at(m_, ghost_vertex + 4, 0);
    set_half_at(m_, stitch_normal + kVertexFlags, half_at(m_, stitch_normal + kVertexFlags) | 1u);
    set_half_at(m_, ghost_normal + kVertexFlags, half_at(m_, ghost_normal + kVertexFlags) | 2u);
    set_half_at(m_, ghost_normal + 2, 0);
    set_half_at(m_, ghost_normal + 4, 0);
    set_half_at(m_, ghost_normal, number);
}

}  // namespace opentony::runtime::create
