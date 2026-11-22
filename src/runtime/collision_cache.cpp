#include "collision_views.hpp"

// Phase 4.15a: the collision cache and the line query shared by the world code.

namespace opentony::runtime::collision {

using namespace views;

// 0x004629f0: forgets every cached model and the cache entries
void CollisionKit::flush_cache() {
    write_scalar<Word>(m_, globals::kCacheModelA, 0xffffffffu);
    write_scalar<Word>(m_, globals::kCacheModelB, 0xffffffffu);
    write_scalar<Word>(m_, globals::kCacheCountA, 0);
    write_scalar<Word>(m_, globals::kCacheCountB, 0);
    for (Word slot = 0; slot < static_cast<Word>(guest::CollisionModelCacheView::entries_count); ++slot) {
        cache_slot(m_, slot).set_item(GuestPtr<RuntimeCollisionObject>{});
    }
}

// 0x00462490: a new generation for the collision queries (a 16-bit counter, stamped on the objects at +6); when it wraps to 0 it restarts at 1 and every object of the environment and game lists is un-stamped
void CollisionKit::advance_query_generation() {
    const std::uint16_t next = static_cast<std::uint16_t>(read_scalar<std::uint16_t>(m_, globals::kQueryGeneration) + 1);
    write_scalar<std::uint16_t>(m_, globals::kQueryGeneration, next);
    if (next != 0) {
        return;
    }
    write_scalar<std::uint16_t>(m_, globals::kQueryGeneration, 1);
    for (const Word head : {globals::kEnvironmentList, globals::kGameList}) {
        for (Word object = read_scalar<Word>(m_, head); object != 0; object = RuntimeCollisionObjectView(m_, object).collision_next().address) {
            RuntimeCollisionObjectView(m_, object).set_query_stamp(0);
        }
    }
}

// 0x00463580: the model cache is a ring of 20 entries (0x10 bytes each from 0x00567a70: the item at +0, the first face of its faces at +0xc) over a ring of 500 faces. Does the face cache have room for
// `faces` more? Two assertions (the oldest entry's first face is the cache's first face; at most 500 faces); a full ring of entries says no; otherwise the new faces must fit between the write position and
// the oldest face (wrapping the write position to 0 when they do not fit before the end)
std::uint32_t CollisionKit::cache_can_take(std::int32_t faces) {
    constexpr std::uint32_t kMsgStart = 0x00533be8;
    constexpr std::uint32_t kMsgFit = 0x00533bb0;
    const std::int32_t oldest = read_scalar<std::int32_t>(m_, globals::kCacheModelA);
    check(oldest == -1 || static_cast<std::int32_t>(cache_slot(m_, static_cast<Word>(oldest)).face_cache_base_index()) == read_scalar<std::int32_t>(m_, globals::kCacheModelB), kMsgStart);
    check(faces <= 500, kMsgFit);
    std::int32_t newest = read_scalar<std::int32_t>(m_, globals::kCacheCountA);
    if (newest >= 0x14) {
        newest = 0;
        write_scalar<Word>(m_, globals::kCacheCountA, 0);
    }
    if (newest == read_scalar<std::int32_t>(m_, globals::kCacheModelA)) {
        return 0;
    }
    const std::int32_t first = read_scalar<std::int32_t>(m_, globals::kCacheModelB);
    std::int32_t write = read_scalar<std::int32_t>(m_, globals::kCacheCountB);
    if (first < write) {
        if (write + faces > 500) {
            write = 0;
            write_scalar<Word>(m_, globals::kCacheCountB, 0);
        }
        if (write > first) {
            return 1;
        }
    }
    return write + faces > first ? 0 : 1;
}

// 0x00463690: drop the oldest entry of the model cache (assertions: there is one, and its first face is the cache's first face); the oldest becomes the next entry, or the cache is empty again
void CollisionKit::cache_remove_oldest() {
    constexpr std::uint32_t kMsgEmpty = 0x00533c1c;
    constexpr std::uint32_t kMsgStart = 0x00533be8;
    check(read_scalar<std::int32_t>(m_, globals::kCacheModelA) != -1, kMsgEmpty);
    std::int32_t oldest = read_scalar<std::int32_t>(m_, globals::kCacheModelA);
    check(static_cast<std::int32_t>(cache_slot(m_, static_cast<Word>(oldest)).face_cache_base_index()) == read_scalar<std::int32_t>(m_, globals::kCacheModelB), kMsgStart);
    oldest = read_scalar<std::int32_t>(m_, globals::kCacheModelA);
    cache_slot(m_, static_cast<Word>(oldest)).set_item(GuestPtr<RuntimeCollisionObject>{});
    oldest += 1;
    write_scalar<Word>(m_, globals::kCacheModelA, static_cast<Word>(oldest));
    if (oldest >= 0x14) {
        oldest = 0;
        write_scalar<Word>(m_, globals::kCacheModelA, 0);
    }
    if (oldest == read_scalar<std::int32_t>(m_, globals::kCacheCountA)) {
        write_scalar<Word>(m_, globals::kCacheModelA, 0xffffffffu);
        write_scalar<Word>(m_, globals::kCacheCountA, 0);
        write_scalar<Word>(m_, globals::kCacheModelB, 0xffffffffu);
        write_scalar<Word>(m_, globals::kCacheCountB, 0);
        return;
    }
    write_scalar<Word>(m_, globals::kCacheModelB, cache_slot(m_, static_cast<Word>(oldest)).face_cache_base_index());
}

// the zone query (a service) fills in three fields of the record the caller keeps in a local: the body it hit, the slope of the surface and the height; the probe scripts the three as dwords in
// that order
std::uint32_t CollisionKit::query_line(const std::uint32_t start[3], const std::uint32_t end[3], std::uint32_t record) {
    const SLineInfoView info(m_, record);
    info.set_start_x(static_cast<std::int32_t>(start[0]));
    info.set_end_x(static_cast<std::int32_t>(end[0]));
    info.set_start_y(static_cast<std::int32_t>(start[1]));
    info.set_end_y(static_cast<std::int32_t>(end[1]));
    info.set_start_z(static_cast<std::int32_t>(start[2]));
    info.set_end_z(static_cast<std::int32_t>(end[2]));
    engine(kInitCollisionQuery, {record});
    engine(kZoneCollision, {record, 1});
    const auto name = pcplatform::engine_name(kZoneCollision);
    if (!s_.live()) {
        info.set_hit_body(GuestPtr<RuntimeCollisionObject>{static_cast<std::uint32_t>(s_.value(name))});
        info.set_hit_normal(1, static_cast<std::int16_t>(s_.value(name)));
        info.set_hit_position(1, static_cast<std::int32_t>(s_.value(name)));
    }
    return info.hit_body().address;
}

}  // namespace opentony::runtime::collision
