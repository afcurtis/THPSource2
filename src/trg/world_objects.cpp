#include "world_objects.hpp"

#include <algorithm>

namespace opentony::trg {

std::uint32_t crap_distance(const ScriptVec3& a, const ScriptVec3& b) noexcept {
    auto magnitude = [](std::int32_t left, std::int32_t right) noexcept {
        const std::int32_t delta = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
        return delta < 0 ? -delta : delta;
    };
    const std::int32_t dx = magnitude(a[0], b[0]);
    const std::int32_t dy = magnitude(a[1], b[1]);
    const std::int32_t dz = magnitude(a[2], b[2]);
    // Largest axis at full weight, the middle at 1/2 and the smallest at 1/4;
    // ties resolve exactly as the retail comparison chain does.
    std::int32_t sum = 0;
    if (dy <= dx) {
        if (dx < dz) {
            sum = (dx >> 1) + dz + (dy >> 2);
        } else if (dz < dy) {
            sum = (dy >> 1) + dx + (dz >> 2);
        } else {
            sum = (dy >> 2) + dx + (dz >> 1);
        }
    } else if (dy < dz) {
        sum = (dx >> 2) + dz + (dy >> 1);
    } else if (dz < dx) {
        sum = (dx >> 1) + dy + (dz >> 2);
    } else {
        sum = (dx >> 2) + dy + (dz >> 1);
    }
    return static_cast<std::uint32_t>(sum >> 12);
}

ObjectId WorldObjectPool::create(ObjectListId list, WorldObject prototype) {
    objects_.push_back(prototype);
    const ObjectId id = static_cast<ObjectId>(objects_.size() - 1);
    objects_.back().owner = list;
    push_head(list, id);
    return id;
}

std::vector<ObjectId> WorldObjectPool::members(ObjectListId list) const {
    std::vector<ObjectId> result;
    for (ObjectId cursor = heads_[index(list)]; cursor != kNoObject; cursor = at(cursor).next) {
        result.push_back(cursor);
        if (result.size() > objects_.size()) {
            throw FormatError("object list is cyclic");
        }
    }
    return result;
}

ObjectId WorldObjectPool::find_node(ObjectListId list, std::uint16_t node) const {
    for (const ObjectId id : members(list)) {
        if (at(id).node == node) {
            return id;
        }
    }
    return kNoObject;
}

// ObjectList_PushHead (0x004801d0)
void WorldObjectPool::push_head(ObjectListId list, ObjectId id) {
    WorldObject& object = at(id);
    object.prev = kNoObject;
    object.next = heads_[index(list)];
    heads_[index(list)] = id;
    if (object.next != kNoObject) {
        at(object.next).prev = id;
    }
}

// ObjectList_Remove (0x004801f0). The removed object keeps its own links.
void WorldObjectPool::remove(ObjectListId list, ObjectId id) {
    if ((at(id).list_flags & kObjectSuspended) != 0 && list != ObjectListId::Suspended) {
        unsuspend(id);
    }
    const WorldObject& object = at(id);
    if (object.next != kNoObject) {
        at(object.next).prev = object.prev;
    }
    if (object.prev != kNoObject) {
        at(object.prev).next = object.next;
    }
    if (heads_[index(list)] == id) {
        heads_[index(list)] = object.next;
    }
}

// Object_SuspendToList (0x004802f0)
void WorldObjectPool::suspend(ObjectListId from, ObjectId id) {
    assert_that((at(id).list_flags & kObjectSuspended) == 0, "Suspended flag illegally set");
    at(id).owner = from;
    remove(from, id);
    push_head(ObjectListId::Suspended, id);
    at(id).list_flags = static_cast<std::uint16_t>(at(id).list_flags | kObjectSuspended);
}

// CBody__UnSuspend (0x004803b0)
void WorldObjectPool::unsuspend(ObjectId id) {
    if ((at(id).list_flags & kObjectSuspended) == 0) {
        return;
    }
    remove(ObjectListId::Suspended, id);
    push_head(at(id).owner, id);
    at(id).list_flags = static_cast<std::uint16_t>(at(id).list_flags & ~kObjectSuspended);
}

void WorldObjectPool::signal_node(ObjectListId list, std::uint16_t node) {
    for (ObjectId cursor = heads_[index(list)]; cursor != kNoObject; cursor = at(cursor).next) {
        if (at(cursor).node == node) {
            at(cursor).event_word = static_cast<std::uint16_t>(at(cursor).event_word | 1U);
        }
    }
}

void WorldObjectPool::suspend_by_node(ObjectListId list, std::uint16_t node) {
    // The loop steps through the object's own next pointer after the suspend
    // call, so it carries on through the suspended list (retail behaviour).
    for (ObjectId cursor = heads_[index(list)]; cursor != kNoObject; cursor = at(cursor).next) {
        if (at(cursor).node == node) {
            suspend(list, cursor);
        }
    }
}

void WorldObjectPool::unsuspend_by_node(std::uint16_t node) {
    for (ObjectId cursor = heads_[index(ObjectListId::Suspended)]; cursor != kNoObject;
         cursor = at(cursor).next) {
        if (at(cursor).node == node) {
            unsuspend(cursor);
        }
    }
}

void WorldObjectPool::kill_in_list(std::uint16_t node, ObjectListId list, int mode) {
    for (ObjectId cursor = heads_[index(list)]; cursor != kNoObject; cursor = at(cursor).next) {
        if (at(cursor).node != node) {
            continue;
        }
        if (mode == 0) {
            hooks_.die(cursor);
        } else if (mode == 1) {
            hooks_.hit(cursor);
        }
    }
}

void WorldObjectPool::release(ObjectId id, ObjectListId list) {
    remove(list, id);
    at(id).destroyed = true;
}

// Ob_AI (0x00480ff0)
void WorldObjectPool::run_frame(ObjectListId list) {
    ObjectId cursor = heads_[index(list)];
    while (cursor != kNoObject) {
        const ObjectId current = cursor;
        cursor = at(current).next;  // read before the object can move or die
        assert_that((at(current).list_flags & kObjectSuspended) == 0, "Suspended flag illegally set");
        WorldObject& object = at(current);
        if ((object.list_flags & kObjectDead) == 0) {
            std::uint32_t distance = crap_distance(hooks_.player_position(0), object.position);
            if (hooks_.player_count() == 2) {
                distance = std::min(distance, crap_distance(hooks_.player_position(1), object.position));
            }
            object.distance = distance;
            if ((object.list_flags & kObjectSuspendWhenFar) == 0 || distance <= kSuspendDistance) {
                // Animation_InvokeUpdateHooks (0x00480fa0)
                if ((object.state & 2U) != 0 && !hooks_.video_playing()) {
                    hooks_.advance_animation(current);
                }
                hooks_.update(current);
            } else {
                suspend(list, current);
            }
        } else if ((object.list_flags & kObjectDeathSeen) == 0) {
            object.list_flags = static_cast<std::uint16_t>(object.list_flags | kObjectDeathSeen);
        } else {
            hooks_.destroy(current);
            if (!at(current).destroyed) {
                release(current, list);
            }
        }
    }
}

// Ob_QuickAI (0x00481110). The next object is fetched one step ahead, and its
// own successor is read before the current object is processed, as in retail.
void WorldObjectPool::run_quick_frame(ObjectListId list) {
    ObjectId current = heads_[index(list)];
    if (current == kNoObject) {
        return;
    }
    ObjectId next = at(current).next;
    while (true) {
        WorldObject& object = at(current);
        if ((object.list_flags & kObjectDead) == 0) {
            hooks_.update(current);
        } else if ((object.list_flags & kObjectDeathSeen) == 0) {
            object.list_flags = static_cast<std::uint16_t>(object.list_flags | kObjectDeathSeen);
        } else {
            hooks_.destroy(current);
            if (!at(current).destroyed) {
                release(current, list);
            }
        }
        if (next == kNoObject) {
            break;
        }
        current = next;
        next = at(current).next;
    }
}

// Ob_ClearInputFlags (0x00480fd0)
void WorldObjectPool::clear_input_flags(ObjectListId list) {
    for (ObjectId cursor = heads_[index(list)]; cursor != kNoObject; cursor = at(cursor).next) {
        at(cursor).event_word = static_cast<std::uint16_t>(at(cursor).event_word & 1U);
    }
}

// Ob_MaybeUnSuspendOrCull (0x00481150)
void WorldObjectPool::wake_near_objects() {
    ObjectId current = heads_[index(ObjectListId::Suspended)];
    if (current == kNoObject) {
        return;
    }
    ObjectId next = at(current).next;
    while (true) {
        const WorldObject& object = at(current);
        if ((object.list_flags & kObjectSuspendWhenFar) != 0) {
            std::uint32_t distance = crap_distance(hooks_.player_position(0), object.position);
            if (hooks_.player_count() == 2) {
                distance = std::min(distance, crap_distance(hooks_.player_position(1), object.position));
            }
            if (distance <= kSuspendDistance) {
                unsuspend(current);
            }
        }
        if (next == kNoObject) {
            break;
        }
        current = next;
        next = at(current).next;
    }
}

// TRG_SendSignalToNodes (0x004c5b60)
void WorldMessenger::signal_nodes(std::span<const std::uint16_t> links) {
    if (links.empty()) {
        hooks_.on_assertion("Node sending a signal is not linked to anything");
    }
    for (const std::uint16_t target : links) {
        const std::uint16_t type = file_.node(target).type;
        if (type == 1 || type == 7) {
            pool_.signal_node(ObjectListId::Baddy, target);
            pool_.signal_node(ObjectListId::GameObject, target);
        }
    }
}

// TRG_ApplyActivationOrSuspendList (0x004c5c70)
void WorldMessenger::activate_or_suspend(std::span<const std::uint16_t> links, std::uint16_t opcode) {
    if (opcode != 4 && opcode != 5) {
        hooks_.on_assertion("Bad signaltype");
    }
    if (links.empty()) {
        hooks_.on_assertion("Node sending an activate or suspend is not linked to anything");
    }
    for (const std::uint16_t target : links) {
        const std::uint16_t type = file_.node(target).type;
        if (type != 1 && type != 7) {
            continue;
        }
        if (opcode == 5) {
            pool_.suspend_by_node(ObjectListId::Baddy, target);
            pool_.suspend_by_node(ObjectListId::GameObject, target);
        } else {
            pool_.unsuspend_by_node(target);
        }
    }
}

// TRG_SendKillFromNode (0x004c7a00)
void WorldMessenger::kill_from_node(std::span<const std::uint16_t> links, int mode) {
    for (const std::uint16_t target : links) {
        const std::uint16_t type = file_.node(target).type;
        if (type == 1) {
            if (hooks_.kill_platform_at_node(target, mode)) {
                continue;
            }
            pool_.kill_in_list(target, ObjectListId::Baddy, mode);
            pool_.kill_in_list(target, ObjectListId::GameObject, mode);
        } else if (type == 2 || type == 9 || type == 10 || type == 11 || type == 500 || type == 501) {
            hooks_.kill_special_node(target, type, mode);
        }
    }
}

} // namespace opentony::trg
