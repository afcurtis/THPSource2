#include "script_world.hpp"

#include <algorithm>

namespace opentony::trg {

struct ScriptWorld::Impl final : ScriptMessageSink, WorldObjectHooks {
    Impl(const TrgFile& trg_file, ScriptWorldEnvironment& environment)
        : file(trg_file),
          env(environment),
          vm(trg_file, environment, ScriptClass::ScriptObjectCb),
          platform_vm(trg_file, environment, ScriptClass::Platform),
          traffic_vm(trg_file, environment, ScriptClass::Traffic),
          powerup_vm(trg_file, environment, ScriptClass::Powerup),
          pool(*this),
          messenger(trg_file, pool, *this) {}

    const TrgFile& file;
    ScriptWorldEnvironment& env;
    ObjectScriptVm vm;
    ObjectScriptVm platform_vm;
    ObjectScriptVm traffic_vm;
    ObjectScriptVm powerup_vm;
    std::uint32_t traffic_sequence{};  // DAT_0055faec: index of the next car
    WorldObjectPool pool;
    WorldMessenger messenger;
    std::size_t assertions{};
    std::size_t faulted_updates{};

    [[nodiscard]] ObjectScriptVm& vm_for(const WorldObject& object) {
        return object.klass == ScriptClass::Platform ? platform_vm : vm;
    }

    // WorldObjectHooks
    void on_assertion(std::string_view) override { ++assertions; }
    void die(ObjectId id) override {
        WorldObject& object = pool.at(id);
        if (object.klass == ScriptClass::Powerup) {
            powerup_vm.die_powerup(object);
        } else {
            vm.die(object);
        }
    }
    void hit(ObjectId id) override {
        WorldObject& object = pool.at(id);
        if (object.klass == ScriptClass::Platform) {
            // KillInList passes the object's own hit points, so the hit
            // always finishes an object that has any.
            platform_vm.hit_platform(object, static_cast<std::int16_t>(object.field_b4));
        }
    }
    void update(ObjectId id) override {
        try {
            WorldObject& object = pool.at(id);
            if (object.klass == ScriptClass::Platform) {
                (void)platform_vm.update_platform(object);
            } else if (object.klass == ScriptClass::Traffic) {
                (void)traffic_vm.update_traffic(object);
            } else if (object.klass == ScriptClass::Powerup) {
                powerup_vm.update_powerup(object);
            } else {
                (void)vm.update(object, env.frame_delta());
            }
        } catch (const FormatError&) {
            // A script that runs off the end of its node cannot continue;
            // end it the way a finished script ends.
            ++faulted_updates;
            pool.at(id).active = 0;
        }
    }
    void destroy(ObjectId id) override {
        WorldObject& object = pool.at(id);
        if (object.klass == ScriptClass::Powerup) {
            if (object.powerup.glow != 0) {
                env.destroy_powerup_glow(object.powerup.glow);
                object.powerup.glow = 0;
            }
            pool.release(id, ObjectListId::Powerup);
            return;
        }
        if (object.klass == ScriptClass::Platform) {
            if (object.platform.sound_handle != -1) {
                env.stop_sound(object.platform.sound_handle);
                object.platform.sound_handle = -1;
            }
            pool.release(id, ObjectListId::GameObject);
            return;
        }
        if (object.sound_handle != -1) {
            env.stop_sound(object.sound_handle);
            object.sound_handle = -1;
        }
        pool.release(id, ObjectListId::Baddy);
    }
    [[nodiscard]] bool video_playing() const override { return env.video_playing(); }
    [[nodiscard]] std::size_t player_count() const override { return env.player_count(); }
    [[nodiscard]] ScriptVec3 player_position(std::size_t index) const override {
        return env.player_position(index);
    }
    // TRG_SendKillFromNode's platform path (0x004c7a00): a kill message that
    // reaches a bouncy prop throws it instead of killing it.
    [[nodiscard]] bool kill_platform_at_node(std::uint16_t node, int /*mode*/) override {
        bool bounced = false;
        for (ObjectId cursor = pool.head(ObjectListId::GameObject); cursor != kNoObject; cursor = pool.at(cursor).next) {
            WorldObject& object = pool.at(cursor);
            if (object.node != node || (object.state & 0x100U) == 0 || (object.flags & 0x10U) == 0) {
                continue;
            }
            object.state = static_cast<std::uint16_t>(object.state | 0x20U);
            if ((object.flags & 0x20U) != 0) {
                continue;
            }
            execute_links(node);
            object.flags |= 0x100U;  // the byte at +0x179
            platform_vm.bounce_platform(object, env.player_context(), env.player_velocity());
            bounced = true;
        }
        return bounced;
    }

    // ScriptMessageSink
    void signal_links(std::uint16_t node) override {
        const std::vector<std::uint16_t> links = file.links(node);
        messenger.signal_nodes(links);
    }
    // TRG_ExecuteCommandList: pulse every linked node in order.
    void execute_links(std::uint16_t node) override {
        for (const std::uint16_t target : file.links(node)) {
            env.send_pulse(target);
        }
    }
    void signal_objects(std::uint16_t node) override {
        pool.signal_node(ObjectListId::Baddy, node);
        pool.signal_node(ObjectListId::GameObject, node);
    }
};

ScriptWorld::ScriptWorld(const TrgFile& file, ScriptWorldEnvironment& environment)
    : impl_(std::make_unique<Impl>(file, environment)) {
    environment.sink_ = impl_.get();
}

ScriptWorld::~ScriptWorld() {
    impl_->env.sink_ = nullptr;
}

// TRG_CreateObject (0x004c5460), subtype-0xcb and subtype-0x0192 branches.
ObjectId ScriptWorld::pulse_node(std::size_t node) {
    const NodeView& view = impl_->file.node(node);
    if (view.type == 5) {
        // TRG_CreateObject, type-5 branch: the node's subtype, its position, the
        // word after the position (zero: drop to the ground) and the timer word
        // after that. The power-up is created awake on the power-up list.
        const std::array<std::uint16_t, 3> words = impl_->file.node_orientation(node);
        const std::uint8_t flags = words[1] == 0 ? 4 : 0;
        WorldObject object;
        static_cast<ScriptObject&>(object) = impl_->powerup_vm.create_powerup(
            static_cast<std::uint16_t>(node), impl_->file.node_subtype(node), impl_->file.node_position(node),
            ScriptVec3{}, flags, words[2]);
        object.klass = ScriptClass::Powerup;
        if (!impl_->file.links(node).empty()) {
            object.powerup.link_flag = 1;  // CPowerUp__SetNode
            object.powerup.link_node = static_cast<std::uint16_t>(node);
        }
        return impl_->pool.create(ObjectListId::Powerup, object);
    }
    if (view.type != 1 && view.type != 7) {
        return kNoObject;
    }
    const std::uint16_t subtype = impl_->file.node_subtype(node);
    const bool traffic = subtype >= 0xd5 && subtype <= 0xdc;
    if (subtype != 0xcb && subtype != 0x192 && !traffic) {
        return kNoObject;
    }
    const std::vector<std::uint8_t> options = impl_->file.node_spawn_options(node);
    const bool starts_active = std::find(options.begin(), options.end(), std::uint8_t{2}) != options.end();
    const bool stays_near = std::find(options.begin(), options.end(), std::uint8_t{4}) != options.end();

    const bool platform = subtype == 0x192;
    WorldObject object;
    if (traffic) {
        // TRG_ConstructTrafficObject: the car sits on the node its one link
        // names, lowered to the road surface, and starts its positional sound.
        const std::vector<std::uint16_t> road = impl_->file.links(node);
        if (road.size() != 1) {
            impl_->assertions++;  // "Incorrect car segment link"
        }
        const std::int32_t road_node = road.empty() ? -1 : road.front();
        ScriptVec3 where = road.empty() ? impl_->file.node_position(node) : impl_->file.node_position(road.front());
        where[1] = static_cast<std::int32_t>(static_cast<std::uint32_t>(where[1]) - 0x6e000U);
        static_cast<ScriptObject&>(object) = ScriptObject::construct_traffic(
            static_cast<std::uint16_t>(node), subtype, road_node, where, impl_->file.node_orientation(node),
            impl_->traffic_sequence++);
        object.klass = ScriptClass::Traffic;
        std::int16_t sound = -1;
        bool plays = true;
        switch (subtype) {
        case 0xd5: sound = 0x71; break;
        case 0xd6: plays = false; break;
        case 0xd7: sound = 0x121; break;
        case 0xd8: sound = 0x86; break;
        case 0xd9: sound = 0x111; break;
        case 0xda: sound = 0xdd; break;
        case 0xdb: sound = 0x145; break;
        default: break;  // 0xdc: -1
        }
        if (plays) {
            object.traffic.sound_handle = impl_->env.start_object_sound(sound, true);
        }
    } else {
        static_cast<ScriptObject&>(object) = platform
            ? ScriptObject::construct_platform(
                static_cast<std::uint16_t>(node),
                impl_->file.node_factory_cursor_offset(node),
                impl_->file.node_position(node),
                impl_->file.node_orientation(node))
            : ScriptObject::construct_cb(
                static_cast<std::uint16_t>(node),
                impl_->file.node_factory_cursor_offset(node),
                impl_->file.node_position(node),
                impl_->file.node_orientation(node));
        object.klass = platform ? ScriptClass::Platform : ScriptClass::ScriptObjectCb;
    }
    if (stays_near) {
        object.list_flags = static_cast<std::uint16_t>(object.list_flags & ~kObjectSuspendWhenFar);
    }
    if (view.type == 7) {
        object.list_flags = static_cast<std::uint16_t>(object.list_flags | 0x0004U);
    }
    const ObjectListId list = platform ? ObjectListId::GameObject : ObjectListId::Baddy;
    const ObjectId id = impl_->pool.create(list, object);
    if (!starts_active) {
        impl_->pool.suspend(list, id);
    }
    return id;
}

// The loop order of the retail frame (0x00469e60..0x00469ed5).
void ScriptWorld::run_frame() {
    WorldObjectPool& pool = impl_->pool;
    pool.clear_input_flags(ObjectListId::Baddy);
    pool.clear_input_flags(ObjectListId::GameObject);
    pool.run_frame(ObjectListId::Powerup);  // Ob_AI on the pickup list
    pool.run_quick_frame(ObjectListId::GameObject);
    pool.wake_near_objects();
    pool.run_frame(ObjectListId::Baddy);
}

// Platform_HandleBounce (0x004a12d0)
void ScriptWorld::probe_bounce(const ScriptVec3& previous, const ScriptVec3& position, const ScriptVec3& velocity) {
    constexpr std::int32_t kMargin = 0x14000;
    ScriptVec3 low{};
    ScriptVec3 high{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        low[axis] = std::min(previous[axis], position[axis]);
        high[axis] = std::max(previous[axis], position[axis]);
    }
    const auto edge = [](std::int16_t units, std::int32_t origin) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(units)) * 0x1000U
                                         + static_cast<std::uint32_t>(origin));
    };
    WorldObjectPool& pool = impl_->pool;
    for (ObjectId cursor = pool.head(ObjectListId::GameObject); cursor != kNoObject; cursor = pool.at(cursor).next) {
        WorldObject& object = pool.at(cursor);
        if ((object.state & 0x100U) == 0 || (object.flags & 0x10U) == 0) {
            continue;
        }
        object.state = static_cast<std::uint16_t>(object.state | 0x20U);
        if ((object.flags & 0x20U) != 0) {
            continue;
        }
        const ScriptModelInfo info = impl_->env.model_info(object.region, object.model_index);
        const auto& box = info.box;
        if (edge(box[1], object.position[0]) <= high[0] + kMargin && low[0] - kMargin <= edge(box[0], object.position[0])
            && edge(box[5], object.position[2]) <= high[2] + kMargin && low[2] - kMargin <= edge(box[4], object.position[2])
            && edge(box[3], object.position[1]) <= high[1] + kMargin && low[1] - kMargin <= edge(box[2], object.position[1])) {
            impl_->platform_vm.bounce_platform(object, impl_->env.player_context(), velocity);
        }
    }
}

void ScriptWorld::visible(std::span<const std::uint16_t> links, std::uint16_t value) {
    const std::uint16_t mask = impl_->env.hidden_mask();
    for (const std::uint16_t target : links) {
        if (impl_->file.node(target).type != 5) {
            continue;
        }
        ObjectId id = impl_->pool.find_node(ObjectListId::Powerup, target);
        if (id == kNoObject) {
            id = impl_->pool.find_node(ObjectListId::Suspended, target);
        }
        if (id == kNoObject || impl_->pool.at(id).klass != ScriptClass::Powerup) {
            continue;
        }
        WorldObject& object = impl_->pool.at(id);
        object.state = value != 0 ? static_cast<std::uint16_t>(object.state & ~mask)
                                  : static_cast<std::uint16_t>(object.state | mask);
    }
}

void ScriptWorld::collect_powerup(ObjectId id, std::uint32_t skater, bool primary_skater) {
    WorldObject& object = impl_->pool.at(id);
    if (object.klass == ScriptClass::Powerup) {
        impl_->powerup_vm.take_powerup(object, skater, primary_skater);
    }
}

void ScriptWorld::signal(std::span<const std::uint16_t> links) { impl_->messenger.signal_nodes(links); }

void ScriptWorld::activate_or_suspend(std::span<const std::uint16_t> links, std::uint16_t opcode) {
    impl_->messenger.activate_or_suspend(links, opcode);
}

void ScriptWorld::kill(std::span<const std::uint16_t> links, int mode) {
    impl_->messenger.kill_from_node(links, mode);
}

WorldObjectPool& ScriptWorld::pool() noexcept { return impl_->pool; }
const WorldObjectPool& ScriptWorld::pool() const noexcept { return impl_->pool; }

std::size_t ScriptWorld::assertion_count() const noexcept {
    return impl_->assertions + impl_->vm.assertion_count() + impl_->platform_vm.assertion_count();
}

std::size_t ScriptWorld::faulted_updates() const noexcept { return impl_->faulted_updates; }

} // namespace opentony::trg
