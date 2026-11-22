// LevelRuntime owns a live ScriptWorld: node pulses create objects, messages
// reach them, and tick() runs the object loop (Phase 4.4c wiring).
//
//   opentony_level_world_test DATA_DIR
//
// Exit code 77 (skipped) when the extracted Warehouse files are not present.
#include "level_runtime.hpp"

#include "tests/test_check.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace opentony::trg;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: opentony_level_world_test DATA_DIR\n";
        return 2;
    }
    const std::filesystem::path root = argv[1];
    const std::string trg = (root / "SKWARE_T.TRG").string();
    const std::string psx = (root / "SKWARE.PSX").string();
    if (!std::filesystem::is_regular_file(trg) || !std::filesystem::is_regular_file(psx)) {
        std::cout << "fixture unavailable\n";
        return 77;
    }

    LevelRuntime runtime(trg, psx);
    CHECK(runtime.objects() == nullptr);
    runtime.initialize();
    ScriptWorld* world = runtime.objects();
    CHECK(world != nullptr);

    // Every 0xcb node of the level.
    std::vector<std::uint16_t> script_nodes;
    for (const NodeView& node : runtime.file().nodes()) {
        if ((node.type == 1 || node.type == 7) && runtime.file().node_subtype(node.index) == 0xcb) {
            script_nodes.push_back(static_cast<std::uint16_t>(node.index));
        }
    }
    CHECK(!script_nodes.empty());

    // A pulse that reaches the trigger state creates the node's object, asleep
    // unless its option list carries option 2.
    for (const std::uint16_t node : script_nodes) {
        runtime.pulse_node(node);
    }
    for (const std::uint16_t node : script_nodes) {
        const bool live = world->pool().find_node(ObjectListId::Baddy, node) != kNoObject
            || world->pool().find_node(ObjectListId::Suspended, node) != kNoObject;
        CHECK(live);
    }

    // An activate message delivered through the trigger state reaches the
    // native object: it leaves the suspended list.
    const std::uint16_t target = script_nodes.front();
    const std::vector<std::uint16_t> targets{target};
    runtime.state().on_suspend_activate(0, 5, targets);
    CHECK(world->pool().find_node(ObjectListId::Suspended, target) != kNoObject);
    runtime.state().on_suspend_activate(0, 4, targets);
    const ObjectId woken = world->pool().find_node(ObjectListId::Baddy, target);
    CHECK(woken != kNoObject);
    CHECK((world->pool().at(woken).list_flags & kObjectSuspended) == 0);

    // A signal sets the object's signal bit.
    runtime.state().on_signal(0, targets);
    CHECK((world->pool().at(woken).event_word & 1U) != 0);

    // tick() runs the object loop against the skater position.
    runtime.set_player_position(runtime.file().node_position(target));
    for (int frame = 0; frame < 600; ++frame) {
        runtime.tick(16);
    }
    CHECK(world->assertion_count() == 0);
    CHECK(world->faulted_updates() == 0);
    CHECK(runtime.object_faulted_pulses() == 0);

    // Platforms (subtype 0x0192) live on the game-object list.
    std::vector<std::uint16_t> platform_nodes;
    for (const NodeView& node : runtime.file().nodes()) {
        if ((node.type == 1 || node.type == 7) && runtime.file().node_subtype(node.index) == 0x192) {
            platform_nodes.push_back(static_cast<std::uint16_t>(node.index));
        }
    }
    CHECK(platform_nodes.size() == 41);
    for (const std::uint16_t node : platform_nodes) {
        runtime.pulse_node(node);
    }
    std::size_t bouncy = 0;
    ObjectId first_bouncy = kNoObject;
    for (const std::uint16_t node : platform_nodes) {
        ObjectId id = world->pool().find_node(ObjectListId::GameObject, node);
        if (id == kNoObject) {
            id = world->pool().find_node(ObjectListId::Suspended, node);
        }
        CHECK(id != kNoObject);
        CHECK(world->pool().at(id).klass == ScriptClass::Platform);
    }

    // The skater arriving wakes parked platforms; their scripts run and the
    // bouncy props announce themselves by raising flag 0x10 (command 0x4309).
    for (const std::uint16_t node : platform_nodes) {
        const ObjectId id = [&]() {
            const ObjectId active = world->pool().find_node(ObjectListId::GameObject, node);
            return active != kNoObject ? active : world->pool().find_node(ObjectListId::Suspended, node);
        }();
        runtime.set_player_position(world->pool().at(id).position);
        for (int frame = 0; frame < 4; ++frame) {
            runtime.tick(16);
        }
    }
    for (const std::uint16_t node : platform_nodes) {
        const ObjectId id = world->pool().find_node(ObjectListId::GameObject, node);
        if (id != kNoObject && (world->pool().at(id).flags & 0x10U) != 0) {
            ++bouncy;
            if (first_bouncy == kNoObject) {
                first_bouncy = id;
            }
        }
    }
    CHECK(bouncy >= 20);  // the retail Warehouse trace has 30 awake bouncy props

    // A skater sweeping through a bouncy prop throws it; it then flies, bounces
    // off the level and settles without any assertion.
    const ScriptVec3 where = world->pool().at(first_bouncy).position;
    world->probe_bounce(where, where, ScriptVec3{0x30000, 0, 0x10000});
    CHECK((world->pool().at(first_bouncy).flags & 0x20U) != 0);  // thrown
    for (int frame = 0; frame < 600; ++frame) {
        runtime.tick(16);
    }
    CHECK(world->assertion_count() == 0);
    CHECK(world->faulted_updates() == 0);

    // Re-initialising builds a fresh world and attaches it again.
    runtime.initialize();
    CHECK(runtime.objects() != nullptr);

    // New York has 20 cars (taxis and cable cars) on roads of linked nodes.
    const std::string ny_trg = (root / "SKNY_T.TRG").string();
    const std::string ny_psx = (root / "SKNY.PSX").string();
    if (std::filesystem::is_regular_file(ny_trg) && std::filesystem::is_regular_file(ny_psx)) {
        LevelRuntime ny(ny_trg, ny_psx);
        ny.initialize();
        ScriptWorld* cars = ny.objects();
        std::vector<std::uint16_t> car_nodes;
        for (const NodeView& node : ny.file().nodes()) {
            if (node.type == 1 && ny.file().node_subtype(node.index) >= 0xd5
                && ny.file().node_subtype(node.index) <= 0xdc) {
                car_nodes.push_back(static_cast<std::uint16_t>(node.index));
            }
        }
        CHECK(car_nodes.size() == 20);
        for (const std::uint16_t node : car_nodes) {
            ny.pulse_node(node);
        }
        // Wake the cars by standing in the middle of them, then let them drive.
        ObjectId first = cars->pool().find_node(ObjectListId::Baddy, car_nodes.front());
        if (first == kNoObject) {
            first = cars->pool().find_node(ObjectListId::Suspended, car_nodes.front());
        }
        CHECK(first != kNoObject);
        CHECK(cars->pool().at(first).klass == ScriptClass::Traffic);
        const ScriptVec3 start = cars->pool().at(first).position;
        ny.set_player_position(start);
        for (int frame = 0; frame < 200; ++frame) {
            ny.tick(16);
        }
        CHECK(cars->assertion_count() == 0);
        CHECK(cars->faulted_updates() == 0);
        CHECK(cars->pool().at(first).position != start);  // the car drove
    }

    // Hangar's power-ups live on their own list, hover, and are collected through the world.
    const std::string han_trg = (root / "SKHAN_T.TRG").string();
    const std::string han_psx = (root / "SKHAN.PSX").string();
    if (std::filesystem::is_regular_file(han_trg) && std::filesystem::is_regular_file(han_psx)) {
        LevelRuntime hangar(han_trg, han_psx);
        hangar.initialize();
        ScriptWorld* pickups = hangar.objects();
        std::size_t created = 0;
        ObjectId first = kNoObject;
        for (const NodeView& node : hangar.file().nodes()) {
            if (node.type != 5) {
                continue;
            }
            hangar.pulse_node(node.index);
            const ObjectId id = pickups->pool().find_node(ObjectListId::Powerup, node.index);
            if (id != kNoObject) {
                ++created;
                first = first == kNoObject ? id : first;
            }
        }
        CHECK(created > 0);
        CHECK(pickups->pool().at(first).klass == ScriptClass::Powerup);
        for (int frame = 0; frame < 200; ++frame) {
            hangar.tick(16);
        }
        CHECK(pickups->assertion_count() == 0);
        CHECK(pickups->faulted_updates() == 0);
    }

    std::cout << "level world ok\n";
    return 0;
}
