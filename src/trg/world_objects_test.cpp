#include "script_world.hpp"
#include "world_objects.hpp"

#include "tests/test_check.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace opentony::trg;

struct Hooks final : WorldObjectHooks {
    std::vector<ObjectId> died;
    std::vector<ObjectId> updated;
    std::vector<ObjectId> destroyed;
    std::size_t assertions{};
    ScriptVec3 player{};

    void on_assertion(std::string_view) override { ++assertions; }
    void die(ObjectId id) override { died.push_back(id); }
    void update(ObjectId id) override { updated.push_back(id); }
    void destroy(ObjectId id) override { destroyed.push_back(id); }
    [[nodiscard]] ScriptVec3 player_position(std::size_t) const override { return player; }
};

WorldObject at_node(std::uint16_t node, ScriptVec3 position = {}) {
    WorldObject object;
    object.node = node;
    object.position = position;
    object.list_flags = kObjectSuspendWhenFar;
    return object;
}

void distance_formula() {
    CHECK(crap_distance({0, 0, 0}, {0x4000, 0, 0}) == 4);
    // Largest axis full, middle half, smallest quarter.
    CHECK(crap_distance({0x8000, 0x4000, 0x2000}, {0, 0, 0}) == (0x8000 + 0x2000 + 0x800) >> 12);
    CHECK(crap_distance({0x1000, -0x1000, 0x1000}, {0, 0, 0}) == (0x400 + 0x1000 + 0x800) >> 12);
}

void list_order_and_suspend_cycle() {
    Hooks hooks;
    WorldObjectPool pool(hooks);
    const ObjectId a = pool.create(ObjectListId::Baddy, at_node(1));
    const ObjectId b = pool.create(ObjectListId::Baddy, at_node(2));
    const ObjectId c = pool.create(ObjectListId::Baddy, at_node(3));
    CHECK((pool.members(ObjectListId::Baddy) == std::vector<ObjectId>{c, b, a}));

    pool.suspend(ObjectListId::Baddy, b);
    CHECK((pool.members(ObjectListId::Baddy) == std::vector<ObjectId>{c, a}));
    CHECK((pool.members(ObjectListId::Suspended) == std::vector<ObjectId>{b}));
    CHECK((pool.at(b).list_flags & kObjectSuspended) != 0);

    pool.unsuspend(b);
    CHECK((pool.members(ObjectListId::Baddy) == std::vector<ObjectId>{b, c, a}));
    CHECK(pool.members(ObjectListId::Suspended).empty());
    CHECK((pool.at(b).list_flags & kObjectSuspended) == 0);
    CHECK(hooks.assertions == 0);

    // Suspending twice trips the retail assertion.
    pool.suspend(ObjectListId::Baddy, c);
    pool.suspend(ObjectListId::Baddy, c);
    CHECK(hooks.assertions == 1);
}

void suspend_by_node_continues_through_the_suspended_list() {
    Hooks hooks;
    WorldObjectPool pool(hooks);
    const ObjectId parked = pool.create(ObjectListId::Baddy, at_node(9));
    pool.suspend(ObjectListId::Baddy, parked);
    const ObjectId a = pool.create(ObjectListId::Baddy, at_node(1));
    pool.suspend_by_node(ObjectListId::Baddy, 1);
    CHECK(pool.members(ObjectListId::Baddy).empty());
    CHECK((pool.members(ObjectListId::Suspended) == std::vector<ObjectId>{a, parked}));
    // The removed object still points at the old head of the suspended list.
    CHECK(pool.at(a).next == parked);

    pool.unsuspend_by_node(1);
    CHECK((pool.members(ObjectListId::Baddy) == std::vector<ObjectId>{a}));
    CHECK((pool.members(ObjectListId::Suspended) == std::vector<ObjectId>{parked}));
}

void signals_and_kills() {
    Hooks hooks;
    WorldObjectPool pool(hooks);
    const ObjectId a = pool.create(ObjectListId::Baddy, at_node(4));
    const ObjectId b = pool.create(ObjectListId::Baddy, at_node(5));
    pool.signal_node(ObjectListId::Baddy, 4);
    CHECK((pool.at(a).event_word & 1) == 1);
    CHECK((pool.at(b).event_word & 1) == 0);
    pool.kill_in_list(5, ObjectListId::Baddy, 0);
    CHECK((hooks.died == std::vector<ObjectId>{b}));
    pool.kill_in_list(5, ObjectListId::Baddy, 1);
    CHECK(hooks.died.size() == 1);
}

void object_loop() {
    Hooks hooks;
    WorldObjectPool pool(hooks);
    hooks.player = {0, 0, 0};
    const ObjectId near_object = pool.create(ObjectListId::Baddy, at_node(1, {0x2000, 0, 0}));
    const ObjectId far_object = pool.create(ObjectListId::Baddy, at_node(2, {0x3300000, 0, 0}));
    WorldObject dead = at_node(3);
    dead.list_flags = kObjectDead;
    const ObjectId dead_object = pool.create(ObjectListId::Baddy, dead);

    pool.run_frame(ObjectListId::Baddy);
    CHECK((hooks.updated == std::vector<ObjectId>{near_object}));
    CHECK(pool.at(near_object).distance == 2);
    CHECK((pool.at(far_object).list_flags & kObjectSuspended) != 0);
    CHECK((pool.at(dead_object).list_flags & kObjectDeathSeen) != 0);
    CHECK(hooks.destroyed.empty());

    pool.run_frame(ObjectListId::Baddy);
    CHECK((hooks.destroyed == std::vector<ObjectId>{dead_object}));
    CHECK(pool.at(dead_object).destroyed);
    CHECK((pool.members(ObjectListId::Baddy) == std::vector<ObjectId>{near_object}));
}

// ---- ScriptWorld over a synthetic TRG --------------------------------------

void put16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xff));
    out.push_back(static_cast<std::byte>(value >> 8));
}
void put32(std::vector<std::byte>& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value));
    put16(out, static_cast<std::uint16_t>(value >> 16));
}

// A type-1 subtype-0xcb node. `options` is the preamble byte list.
std::vector<std::byte> cb_node(const std::vector<std::uint8_t>& options, const std::vector<std::uint16_t>& script) {
    std::vector<std::byte> node;
    put16(node, 1);
    put16(node, 0xcb);
    put16(node, 0);
    put16(node, 0);  // no links
    for (const std::uint8_t option : options) {
        node.push_back(static_cast<std::byte>(option));
    }
    node.push_back(std::byte{0xff});
    while (node.size() % 4 != 0) {
        node.push_back(std::byte{0});
    }
    put32(node, 0x10);
    put32(node, 0x20);
    put32(node, 0x30);
    put16(node, 0);
    put16(node, 0);
    put16(node, 0);
    for (const std::uint16_t word : script) {
        put16(node, word);
    }
    while (node.size() % 4 != 0) {
        node.push_back(std::byte{0});
    }
    return node;
}

TrgFile build_file(const std::vector<std::vector<std::byte>>& nodes) {
    std::vector<std::byte> bytes{std::byte{0x5f}, std::byte{0x54}, std::byte{0x52}, std::byte{0x47}};
    put32(bytes, 2);
    put32(bytes, static_cast<std::uint32_t>(nodes.size()));
    std::uint32_t offset = static_cast<std::uint32_t>(12 + nodes.size() * 4);
    for (const auto& node : nodes) {
        put32(bytes, offset);
        offset += static_cast<std::uint32_t>(node.size());
    }
    for (const auto& node : nodes) {
        bytes.insert(bytes.end(), node.begin(), node.end());
    }
    return TrgFile::parse(bytes);
}

struct World final : ScriptWorldEnvironment {
    std::vector<std::uint16_t> pulses;
    std::vector<std::string> sounds;
    std::uint32_t delta{0x1000};
    // Far from every test object (which sit at the origin) unless a test moves it.
    ScriptVec3 player{0x40000000, 0, 0};
    void send_pulse(std::uint16_t node) override { pulses.push_back(node); }
    void play_sound(std::int16_t id) override { sounds.push_back(std::to_string(id)); }
    [[nodiscard]] std::uint32_t frame_delta() const override { return delta; }
    [[nodiscard]] ScriptVec3 player_position(std::size_t) const override { return player; }
};

void script_world_lifecycle() {
    const std::vector<std::byte> end_node{std::byte{0xff}, std::byte{0}};
    // node 0: parked until activated; node 1: active from the start (option 2)
    // and never parked for distance (option 4). Its script plays a sound,
    // pulses node 7, and finishes.
    const TrgFile file = build_file({
        cb_node({}, {0x4290, 5, 0x4100}),
        cb_node({2, 4}, {0x4290, 6, 0x42b4, 7, 0x4100}),
        end_node,
    });
    World world;
    ScriptWorld script_world(file, world);

    const ObjectId parked = script_world.pulse_node(0);
    const ObjectId active = script_world.pulse_node(1);
    CHECK(script_world.pulse_node(2) == kNoObject);
    CHECK((script_world.pool().at(parked).list_flags & kObjectSuspended) != 0);
    CHECK((script_world.pool().at(active).list_flags & kObjectSuspended) == 0);
    CHECK((script_world.pool().members(ObjectListId::Baddy) == std::vector<ObjectId>{active}));

    script_world.run_frame();
    CHECK(world.sounds == std::vector<std::string>{"6"});
    CHECK(world.pulses == std::vector<std::uint16_t>{7});
    CHECK(script_world.pool().at(active).active == 0);

    // The finished script kills the object on the next update; deletion takes
    // two more frames (dead, then death seen).
    script_world.run_frame();
    CHECK((script_world.pool().at(active).list_flags & kObjectDead) != 0);
    script_world.run_frame();
    CHECK(!script_world.pool().at(active).destroyed);
    script_world.run_frame();
    CHECK(script_world.pool().at(active).destroyed);

    // Activating node 0 brings the parked object into the loop.
    const std::uint16_t links[] = {0};
    script_world.activate_or_suspend(links, 4);
    CHECK((script_world.pool().members(ObjectListId::Baddy) == std::vector<ObjectId>{parked}));
    world.player = {0, 0, 0};  // in range, so the loop does not park it again
    script_world.run_frame();
    CHECK(world.sounds.back() == "5");
    CHECK(script_world.assertion_count() == 0);
}

// Ob_MaybeUnSuspendOrCull: a parked object flagged suspend-when-far wakes on its
// own once a player is within 0x32c8 world units; one without the flag does not.
void proximity_wakes_parked_objects() {
    const std::vector<std::byte> end_node{std::byte{0xff}, std::byte{0}};
    const TrgFile file = build_file({
        cb_node({}, {0x4290, 1, 0x4100}),
        cb_node({4}, {0x4290, 2, 0x4100}),  // option 4: not flagged suspend-when-far
        end_node,
    });
    World world;
    ScriptWorld script_world(file, world);
    const ObjectId flagged = script_world.pulse_node(0);
    const ObjectId unflagged = script_world.pulse_node(1);
    CHECK((script_world.pool().at(flagged).list_flags & kObjectSuspended) != 0);
    CHECK((script_world.pool().at(unflagged).list_flags & kObjectSuspended) != 0);

    script_world.run_frame();
    CHECK((script_world.pool().at(flagged).list_flags & kObjectSuspended) != 0);  // player is far

    world.player = {0, 0, 0};
    script_world.run_frame();
    CHECK((script_world.pool().at(flagged).list_flags & kObjectSuspended) == 0);
    CHECK((script_world.pool().at(unflagged).list_flags & kObjectSuspended) != 0);
    CHECK(world.sounds == std::vector<std::string>{"1"});
}

void signalled_object_leaves_a_wait() {
    const std::vector<std::byte> end_node{std::byte{0xff}, std::byte{0}};
    const TrgFile file = build_file({cb_node({2, 4}, {0x4281, 0x4290, 3, 0x4100}), end_node});
    World world;
    ScriptWorld script_world(file, world);
    script_world.pulse_node(0);
    script_world.run_frame();
    CHECK(world.sounds.empty());
    const std::uint16_t links[] = {0};
    script_world.signal(links);
    script_world.run_frame();
    CHECK(world.sounds == std::vector<std::string>{"3"});
}

} // namespace

int main() {
    distance_formula();
    list_order_and_suspend_cycle();
    suspend_by_node_continues_through_the_suspended_list();
    signals_and_kills();
    object_loop();
    script_world_lifecycle();
    proximity_wakes_parked_objects();
    signalled_object_leaves_a_wait();
    return 0;
}
